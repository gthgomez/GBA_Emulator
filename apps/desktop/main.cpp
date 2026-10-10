// gba-desktop: reference host for the shared GBA emulator engine.
//
// Two modes backed by the same EmulatorRuntime instance:
//   * headless lab mode (--headless): deterministic frame-budgeted run with
//     scripted input, hashes, screenshots, and a JSON artifact. This is the
//     primary verification surface for engine work.
//   * interactive play mode: SDL3 window/input/audio via desktop_app.cpp,
//     backed by the same EmulatorRuntime as the lab.
//
// Emulator behavior lives entirely in gba_core; this file only orchestrates.

#include "gba/core/emulator_runtime.hpp"

#include "desktop_app.hpp"
#include "desktop_persistence.hpp"
#include "gba/core/save_state_codec.hpp"

#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifndef GBA_ENGINE_COMMIT
#define GBA_ENGINE_COMMIT "unknown"
#endif

#ifndef GBA_BUILD_TYPE
#define GBA_BUILD_TYPE "unknown"
#endif

namespace {

using gba::core::AndroidRuntimeStatus;
using gba::core::CoreRunStopReason;
using gba::core::EmulatorRuntime;
using gba::core::KeypadButton;

constexpr std::uint32_t kDefaultFrames = 600;
constexpr std::uint32_t kDefaultMaxStepsPerFrame = 500'000U;

struct CliOptions {
  std::string rom_path;
  bool headless = false;
  std::uint32_t frames = kDefaultFrames;
  std::uint32_t max_steps_per_frame = kDefaultMaxStepsPerFrame;
  std::string input_script_path;
  std::string artifact_path;
  std::string screenshot_dir;
  std::vector<std::uint32_t> screenshot_frames;
  bool frame_hash = false;
  bool audio_hash = false;
  bool state_hash = false;
  std::string save_state_output;
  std::string load_state_input;
  std::uint32_t quit_after_frames = 0;
  std::uint32_t reset_after_frames = 0;
  std::uint32_t switch_after_frames = 0;
  std::string switch_to_path;
  std::string window_screenshot_path;
  std::string save_directory;
};

struct InputEvent {
  std::uint32_t frame = 0;
  KeypadButton button = KeypadButton::a;
  bool down = true;
};

[[noreturn]] void usage(int exit_code) {
  std::fputs(
      "Usage: gba-desktop [game.gba] [--headless] [options]\n"
      "\n"
      "Play mode:\n"
      "  gba-desktop game.gba                 (interactive; SDL host)\n"
      "  gba-desktop                          (opens a ROM file dialog)\n"
      "\n"
      "Headless lab mode:\n"
      "  --headless                          run deterministically without a window\n"
      "  --frames N                          frame budget (default 600)\n"
      "  --max-steps-per-frame N             scheduler step budget per frame\n"
      "  --input-script PATH                 JSON input events ({frame,button,state})\n"
      "  --artifact PATH                     write JSON run artifact\n"
      "  --screenshot-frame N (repeatable)   capture framebuffer PNG at frame N\n"
      "  --screenshot-output DIR             PNG output directory\n"
      "  --frame-hash                        record per-frame framebuffer CRC32s\n"
      "  --audio-hash                        record audio sample hash\n"
      "  --state-hash                        record final machine state hash\n"
      "  --save-state PATH                   write save state blob after the run\n"
      "  --load-state PATH                   load save state blob before the run\n"
      "\n"
      "Automation hooks (play mode):\n"
      "  --quit-after N                      quit after N presented frames\n"
      "  --reset-after N                     reset after N presented frames\n"
      "  --switch-after N                    switch to --switch-to ROM after N frames\n"
      "  --switch-to PATH                    ROM used by --switch-after\n"
      "  --window-screenshot PATH            capture the presented window to PNG\n"
      "  --save-directory DIR                store .sav/.state files in DIR\n",
      stderr);
  std::exit(exit_code);
}

CliOptions parse_cli(int argc, char** argv) {
  CliOptions opts;
  bool rom_seen = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next_string = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "gba-desktop: missing value for " << arg << "\n";
        usage(2);
      }
      return argv[++i];
    };
    // argv strings are ANSI-code-page encoded; the rest of the program
    // stores path strings as UTF-8 (the encoding SDL dialogs and
    // drag-and-drop provide), so convert once at the boundary.
    auto next_path = [&]() -> std::string {
      return std::filesystem::path(next_string()).u8string();
    };
    auto next_u32 = [&]() -> std::uint32_t {
      const std::string value = next_string();
      try {
        return static_cast<std::uint32_t>(std::stoul(value));
      } catch (const std::exception&) {
        std::cerr << "gba-desktop: invalid integer for " << arg << ": " << value << "\n";
        std::exit(2);
      }
    };
    if (arg == "--rom") {
      opts.rom_path = next_path();
      rom_seen = true;
    } else if (arg == "--headless") {
      opts.headless = true;
    } else if (arg == "--quit-after") {
      opts.quit_after_frames = next_u32();
    } else if (arg == "--reset-after") {
      opts.reset_after_frames = next_u32();
    } else if (arg == "--switch-after") {
      opts.switch_after_frames = next_u32();
    } else if (arg == "--switch-to") {
      opts.switch_to_path = next_path();
    } else if (arg == "--window-screenshot") {
      opts.window_screenshot_path = next_path();
    } else if (arg == "--save-directory") {
      opts.save_directory = next_path();
    } else if (arg == "--frames") {
      opts.frames = next_u32();
    } else if (arg == "--max-steps-per-frame") {
      opts.max_steps_per_frame = next_u32();
    } else if (arg == "--input-script") {
      opts.input_script_path = next_path();
    } else if (arg == "--artifact") {
      opts.artifact_path = next_path();
    } else if (arg == "--screenshot-frame") {
      opts.screenshot_frames.push_back(next_u32());
      if (opts.screenshot_dir.empty()) {
        opts.screenshot_dir = ".";
      }
    } else if (arg == "--screenshot-output") {
      opts.screenshot_dir = next_path();
    } else if (arg == "--frame-hash") {
      opts.frame_hash = true;
    } else if (arg == "--audio-hash") {
      opts.audio_hash = true;
    } else if (arg == "--state-hash") {
      opts.state_hash = true;
    } else if (arg == "--save-state") {
      opts.save_state_output = next_path();
    } else if (arg == "--load-state") {
      opts.load_state_input = next_path();
    } else if (arg == "--help" || arg == "-h") {
      usage(0);
    } else if (!rom_seen && arg.rfind("-", 0) != 0) {
      // Positional ROM path: `gba-desktop game.gba`.
      opts.rom_path = std::filesystem::path(arg).u8string();
      rom_seen = true;
    } else {
      std::cerr << "gba-desktop: unknown argument: " << arg << "\n";
      usage(2);
    }
  }
  if (!rom_seen && opts.headless) {
    std::cerr << "gba-desktop: --headless requires a ROM (--rom <path.gba>)\n";
    usage(2);
  }
  std::sort(opts.screenshot_frames.begin(), opts.screenshot_frames.end());
  return opts;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
  // Path strings are UTF-8 (see the argv conversion in parse_cli); a plain
  // narrow-string constructor would decode with the ANSI code page.
  std::ifstream file(gba::desktop::persistence::native_path(path), std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!file || static_cast<std::streamsize>(file.gcount()) != size) {
    return {};
  }
  return bytes;
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(gba::desktop::persistence::native_path(path), std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  if (!bytes.empty()) {
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(file);
}

// ---------------------------------------------------------------------------
// Hashing: SHA-256 for ROM identity, CRC32/FNV-1a for run evidence. These are
// host-side artifact concerns and intentionally do not live in the core.
// ---------------------------------------------------------------------------

constexpr std::uint32_t rotr32(std::uint32_t v, int s) {
  return (v >> s) | (v << (32 - s));
}

class Sha256 {
 public:
  void update(const std::uint8_t* data, std::size_t length) {
    total_length_ += length;
    while (length > 0) {
      const std::size_t take = std::min(length, std::size_t{64} - buffer_length_);
      std::memcpy(buffer_.data() + buffer_length_, data, take);
      buffer_length_ += take;
      data += take;
      length -= take;
      if (buffer_length_ == 64) {
        process_block(buffer_.data());
        buffer_length_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> digest() {
    const std::uint64_t bit_length = total_length_ * 8;
    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0x00;
    while (buffer_length_ != 56) {
      update(&zero, 1);
    }
    std::array<std::uint8_t, 8> length_bytes{};
    for (std::size_t i = 0; i < 8; ++i) {
      length_bytes[i] = static_cast<std::uint8_t>(bit_length >> (56 - i * 8));
    }
    // Direct block write so the total-length accounting is not disturbed.
    std::memcpy(buffer_.data() + 56, length_bytes.data(), 8);
    process_block(buffer_.data());
    buffer_length_ = 0;
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
      out[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
      out[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
      out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return out;
  }

 private:
  void process_block(const std::uint8_t* block) {
    static constexpr std::uint32_t kRoundConstants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
      const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      h = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_length_ = 0;
  std::uint64_t total_length_ = 0;
};

std::string hex_fixed(const std::uint8_t* data, std::size_t length) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (std::size_t i = 0; i < length; ++i) {
    out.push_back(kDigits[data[i] >> 4]);
    out.push_back(kDigits[data[i] & 0x0F]);
  }
  return out;
}

std::uint32_t framebuffer_crc32(const std::uint16_t* pixels, std::size_t count) {
  std::uint32_t crc = 0xFFFFFFFFu;
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(pixels);
  for (std::size_t i = 0; i < count * 2; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t length) {
  std::uint64_t hash = 0xcbf29ce484222325ull;
  for (std::size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 0x100000001b3ull;
  }
  return hash;
}

std::string hex32(std::uint32_t value) {
  std::array<std::uint8_t, 4> bytes{};
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
  }
  return hex_fixed(bytes.data(), 4);
}

std::string hex64(std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (std::size_t i = 0; i < 8; ++i) {
    bytes[i] = static_cast<std::uint8_t>(value >> (56 - i * 8));
  }
  return hex_fixed(bytes.data(), 8);
}

// ---------------------------------------------------------------------------
// Input script: minimal JSON parser restricted to the documented schema.
// Unknown fields or malformed events are rejected rather than ignored.
// ---------------------------------------------------------------------------

std::optional<KeypadButton> parse_button(std::string name) {
  for (char& c : name) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (name == "a") return KeypadButton::a;
  if (name == "b") return KeypadButton::b;
  if (name == "select") return KeypadButton::select;
  if (name == "start") return KeypadButton::start;
  if (name == "right") return KeypadButton::right;
  if (name == "left") return KeypadButton::left;
  if (name == "up") return KeypadButton::up;
  if (name == "down") return KeypadButton::down;
  if (name == "r") return KeypadButton::r;
  if (name == "l") return KeypadButton::l;
  return std::nullopt;
}

struct JsonCursor {
  const std::string& text;
  std::size_t pos = 0;

  void skip_ws() {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
      ++pos;
    }
  }
  bool consume(char c) {
    skip_ws();
    if (pos < text.size() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
  std::optional<std::string> parse_string() {
    skip_ws();
    if (pos >= text.size() || text[pos] != '"') {
      return std::nullopt;
    }
    ++pos;
    std::string out;
    while (pos < text.size() && text[pos] != '"') {
      if (text[pos] == '\\' && pos + 1 < text.size()) {
        ++pos;
      }
      out.push_back(text[pos++]);
    }
    if (pos >= text.size()) {
      return std::nullopt;
    }
    ++pos;
    return out;
  }
  std::optional<std::uint32_t> parse_u32() {
    skip_ws();
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      value = value * 10 + static_cast<std::uint64_t>(text[pos++] - '0');
      if (value > 0xFFFFFFFFull) {
        return std::nullopt;
      }
    }
    return static_cast<std::uint32_t>(value);
  }
};

std::vector<InputEvent> parse_input_script(const std::string& text) {
  JsonCursor cur{text};
  std::vector<InputEvent> events;
  if (!cur.consume('{')) {
    std::cerr << "gba-desktop: input script must be a JSON object\n";
    std::exit(2);
  }
  cur.skip_ws();
  if (cur.consume('}')) {
    return events;
  }
  bool saw_events_key = false;
  do {
    const auto key = cur.parse_string();
    if (!key || !cur.consume(':')) {
      std::cerr << "gba-desktop: malformed input script object\n";
      std::exit(2);
    }
    if (*key != "events") {
      std::cerr << "gba-desktop: unknown input-script key: " << *key << "\n";
      std::exit(2);
    }
    saw_events_key = true;
    if (!cur.consume('[')) {
      std::cerr << "gba-desktop: input script events must be an array\n";
      std::exit(2);
    }
    cur.skip_ws();
    if (cur.consume(']')) {
      continue;
    }
    do {
      if (!cur.consume('{')) {
        std::cerr << "gba-desktop: input event must be an object\n";
        std::exit(2);
      }
      InputEvent event{};
      bool have_frame = false, have_button = false, have_state = false;
      do {
        const auto field = cur.parse_string();
        if (!field || !cur.consume(':')) {
          std::cerr << "gba-desktop: malformed input event\n";
          std::exit(2);
        }
        if (*field == "frame") {
          const auto frame = cur.parse_u32();
          if (!frame) {
            std::cerr << "gba-desktop: invalid frame in input event\n";
            std::exit(2);
          }
          event.frame = *frame;
          have_frame = true;
        } else if (*field == "button") {
          const auto button = cur.parse_string();
          const auto parsed = button ? parse_button(*button) : std::nullopt;
          if (!parsed) {
            std::cerr << "gba-desktop: unknown button in input script: "
                      << (button ? *button : "") << "\n";
            std::exit(2);
          }
          event.button = *parsed;
          have_button = true;
        } else if (*field == "state") {
          const auto state = cur.parse_string();
          if (!state || (*state != "down" && *state != "up")) {
            std::cerr << "gba-desktop: input event state must be \"down\" or \"up\"\n";
            std::exit(2);
          }
          event.down = (*state == "down");
          have_state = true;
        } else {
          std::cerr << "gba-desktop: unknown input-event field: " << *field << "\n";
          std::exit(2);
        }
      } while (cur.consume(','));
      if (!cur.consume('}') || !have_frame || !have_button || !have_state) {
        std::cerr << "gba-desktop: incomplete input event (needs frame, button, state)\n";
        std::exit(2);
      }
      events.push_back(event);
    } while (cur.consume(','));
    if (!cur.consume(']')) {
      std::cerr << "gba-desktop: unterminated events array\n";
      std::exit(2);
    }
  } while (cur.consume(','));
  if (!saw_events_key || !cur.consume('}')) {
    std::cerr << "gba-desktop: malformed input script\n";
    std::exit(2);
  }
  std::sort(events.begin(), events.end(),
            [](const InputEvent& a, const InputEvent& b) { return a.frame < b.frame; });
  return events;
}

// ---------------------------------------------------------------------------
// Screenshots: RGB565 framebuffer -> PNG (RGB888).
// ---------------------------------------------------------------------------

bool write_screenshot(const std::string& dir, std::uint32_t frame,
                      const gba::core::PpuRenderer::Framebuffer& fb) {
  constexpr int kWidth = 240;
  constexpr int kHeight = 160;
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(kWidth * kHeight * 3));
  for (std::size_t i = 0; i < rgb.size() / 3; ++i) {
    const std::uint16_t px = fb[i];
    rgb[i * 3 + 0] = static_cast<std::uint8_t>(((px >> 10) & 0x1F) * 255 / 31);
    rgb[i * 3 + 1] = static_cast<std::uint8_t>(((px >> 5) & 0x1F) * 255 / 31);
    rgb[i * 3 + 2] = static_cast<std::uint8_t>((px & 0x1F) * 255 / 31);
  }
  std::error_code ec;
  // dir arrives as UTF-8; note stbi_write_png ultimately uses narrow fopen,
  // so a screenshot directory outside the ANSI code page will fail on Windows.
  const std::filesystem::path dir_native = gba::desktop::persistence::native_path(dir);
  std::filesystem::create_directories(dir_native, ec);
  char name[64];
  std::snprintf(name, sizeof(name), "frame-%04u.png", static_cast<unsigned>(frame));
  const std::string path = (dir_native / name).u8string();
  return stbi_write_png(path.c_str(), kWidth, kHeight, 3, rgb.data(), kWidth * 3) != 0;
}

const char* stop_reason_name(CoreRunStopReason reason) {
  switch (reason) {
    case CoreRunStopReason::max_steps: return "max_steps";
    case CoreRunStopReason::fetch_failed: return "fetch_failure";
    case CoreRunStopReason::unsupported_instruction: return "unsupported_instruction";
    default: return "unknown";
  }
}

std::string json_escape(const std::string& in) {
  static constexpr char kBackslash = static_cast<char>(0x5C);
  std::string out;
  out.reserve(in.size() + 8);
  for (const char c : in) {
    if (c == kBackslash || c == '"') {
      out.push_back(kBackslash);
    }
    out.push_back(c);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Headless lab run.
// ---------------------------------------------------------------------------

int run_headless(const CliOptions& opts) {
  const auto rom = read_file(opts.rom_path);
  if (rom.empty()) {
    std::cerr << "gba-desktop: cannot read ROM: " << opts.rom_path << "\n";
    return 2;
  }

  std::vector<InputEvent> events;
  if (!opts.input_script_path.empty()) {
    const auto script = read_file(opts.input_script_path);
    if (script.empty()) {
      std::cerr << "gba-desktop: cannot read input script: " << opts.input_script_path << "\n";
      return 2;
    }
    events = parse_input_script(std::string(script.begin(), script.end()));
  }

  Sha256 sha;
  sha.update(rom.data(), rom.size());
  const std::string rom_sha256 = hex_fixed(sha.digest().data(), 32);

  EmulatorRuntime runtime;
  runtime.set_state_hash_enabled(true);

  if (!opts.load_state_input.empty()) {
    const auto blob = read_file(opts.load_state_input);
    if (blob.empty()) {
      std::cerr << "gba-desktop: cannot read save state: " << opts.load_state_input << "\n";
      return 2;
    }
    const auto decoded = gba::core::SaveStateCodec::decode_into(runtime.session(), blob);
    if (decoded.status != gba::core::SaveStateDecodeStatus::ok) {
      std::cerr << "gba-desktop: save state load failed (status "
                << static_cast<int>(decoded.status) << ", blob version "
                << decoded.version << ")\n";
      return 3;
    }
    std::cerr << "gba-desktop: loaded save state from " << opts.load_state_input << "\n";
  } else if (runtime.load_rom(rom) != AndroidRuntimeStatus::ok) {
    std::cerr << "gba-desktop: ROM rejected by engine: " << opts.rom_path << "\n";
    return 3;
  }

  std::size_t next_event = 0;
  std::uint64_t audio_samples_total = 0;
  std::uint64_t audio_hash = 0xcbf29ce484222325ull;
  std::uint32_t completed_frames = 0;
  std::uint32_t underruns_total = 0;
  std::uint32_t unsupported_instructions = 0;
  std::uint32_t fetch_failures = 0;
  CoreRunStopReason abnormal_reason = CoreRunStopReason::max_steps;
  bool abnormal_seen = false;
  std::uint32_t abnormal_frame = 0;
  gba::core::AndroidRuntimeUnsupportedDump dump{};
  std::vector<std::uint64_t> frame_hashes;

  const auto started = std::chrono::steady_clock::now();
  std::uint16_t input_mask = 0;
  for (std::uint32_t frame = 0; frame < opts.frames; ++frame) {
    while (next_event < events.size() && events[next_event].frame == frame) {
      const InputEvent& event = events[next_event++];
      const std::uint16_t bit = static_cast<std::uint16_t>(event.button);
      input_mask = event.down ? static_cast<std::uint16_t>(input_mask | bit)
                              : static_cast<std::uint16_t>(input_mask & ~bit);
      (void)runtime.session().set_input_mask(input_mask);
    }
    const auto result = runtime.step_frame_with_fetch_trace(opts.max_steps_per_frame, &dump);
    if (result.status != AndroidRuntimeStatus::ok) {
      std::cerr << "gba-desktop: frame " << frame << " step failed with status "
                << static_cast<int>(result.status) << "\n";
      return 3;
    }
    if (result.frame_complete) {
      ++completed_frames;
    }
    if (result.run.stop_reason != CoreRunStopReason::max_steps && !abnormal_seen) {
      abnormal_seen = true;
      abnormal_reason = result.run.stop_reason;
      abnormal_frame = frame;
    }
    unsupported_instructions += result.run.unsupported_steps;
    fetch_failures += result.run.fetch_failures;
    audio_samples_total += result.audio_samples;
    underruns_total += result.audio_underruns;
    if (opts.audio_hash) {
      for (const auto& sample : runtime.last_audio_batch()) {
        const std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(sample.left & 0xFF),
            static_cast<std::uint8_t>(sample.left >> 8),
            static_cast<std::uint8_t>(sample.right & 0xFF),
            static_cast<std::uint8_t>(sample.right >> 8)};
        audio_hash ^= fnv1a64(bytes, 4);
        audio_hash *= 0x100000001b3ull;
      }
    }
    if (opts.frame_hash && (frame % 30 == 0 || frame + 1 == opts.frames)) {
      const auto& fb = runtime.framebuffer();
      frame_hashes.push_back(framebuffer_crc32(fb.data(), fb.size()));
    }
    if (std::binary_search(opts.screenshot_frames.begin(), opts.screenshot_frames.end(),
                           frame) &&
        !opts.screenshot_dir.empty()) {
      if (!write_screenshot(opts.screenshot_dir, frame, runtime.framebuffer())) {
        std::cerr << "gba-desktop: failed to write screenshot for frame " << frame << "\n";
        return 3;
      }
    }
  }
  const auto ended = std::chrono::steady_clock::now();
  const double elapsed_ms = std::chrono::duration<double, std::milli>(ended - started).count();

  const std::uint64_t final_state_hash = runtime.session().state_hash();

  if (!opts.save_state_output.empty()) {
    const auto blob = gba::core::SaveStateCodec::encode(runtime.session());
    if (!write_file(opts.save_state_output, blob)) {
      std::cerr << "gba-desktop: failed to write save state: " << opts.save_state_output << "\n";
      return 3;
    }
    std::cerr << "gba-desktop: save state written to " << opts.save_state_output << "\n";
  }

  const auto& fb = runtime.framebuffer();
  const std::uint32_t fb_crc = framebuffer_crc32(fb.data(), fb.size());

  std::string artifact;
  artifact += "{\n";
  artifact += "  \"schema_version\": 1,\n";
  artifact += "  \"engine\": {\n";
  artifact += "    \"commit\": \"" GBA_ENGINE_COMMIT "\",\n";
  artifact += "    \"build_type\": \"" GBA_BUILD_TYPE "\",\n";
  artifact += "    \"host\": \"gba-desktop\"\n";
  artifact += "  },\n";
  artifact += "  \"rom\": {\n";
  artifact += "    \"path\": \"" + json_escape(opts.rom_path) + "\",\n";
  artifact += "    \"sha256\": \"" + rom_sha256 + "\",\n";
  artifact += "    \"size\": " + std::to_string(rom.size()) + "\n";
  artifact += "  },\n";
  artifact += "  \"run\": {\n";
  artifact += "    \"requested_frames\": " + std::to_string(opts.frames) + ",\n";
  artifact += "    \"completed_frames\": " + std::to_string(completed_frames) + ",\n";
  artifact += "    \"max_steps_per_frame\": " + std::to_string(opts.max_steps_per_frame) + ",\n";
  artifact += "    \"scheduler_cycles\": " +
              std::to_string(runtime.session().scheduler().scheduler_cycles()) + ",\n";
  artifact += "    \"stop_reason\": \"" +
              std::string(abnormal_seen ? stop_reason_name(abnormal_reason) : "completed") +
              "\",\n";
  artifact += "    \"stop_frame\": " + std::to_string(abnormal_frame) + ",\n";
  artifact += "    \"final_pc\": " + std::to_string(dump.final_pc) + ",\n";
  artifact += "    \"unsupported_instructions\": " + std::to_string(unsupported_instructions) +
              ",\n";
  artifact += "    \"fetch_failures\": " + std::to_string(fetch_failures) + "\n";
  artifact += "  },\n";
  artifact += "  \"video\": {\n";
  artifact += "    \"framebuffer_crc32\": \"0x" + hex32(fb_crc) + "\",\n";
  artifact += "    \"frame_hashes\": [";
  for (std::size_t i = 0; i < frame_hashes.size(); ++i) {
    artifact += (i == 0 ? "" : ",") + std::to_string(frame_hashes[i]);
  }
  artifact += "]\n";
  artifact += "  },\n";
  artifact += "  \"audio\": {\n";
  artifact += "    \"sample_count\": " + std::to_string(audio_samples_total) + ",\n";
  artifact += "    \"underruns\": " + std::to_string(underruns_total) + ",\n";
  artifact += "    \"hash\": \"0x" + hex64(audio_hash) + "\"\n";
  artifact += "  },\n";
  artifact += "  \"state\": {\n";
  artifact += "    \"final_hash\": \"" + hex64(final_state_hash) + "\"\n";
  artifact += "  },\n";
  artifact += "  \"performance\": {\n";
  artifact += "    \"elapsed_ms\": " + std::to_string(elapsed_ms) + "\n";
  artifact += "  }\n";
  artifact += "}\n";

  if (!opts.artifact_path.empty()) {
    const std::filesystem::path artifact_path(opts.artifact_path);
    if (artifact_path.has_parent_path()) {
      std::error_code ec;
      std::filesystem::create_directories(artifact_path.parent_path(), ec);
    }
    std::ofstream out(opts.artifact_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "gba-desktop: cannot write artifact: " << opts.artifact_path << "\n";
      return 3;
    }
    out << artifact;
    std::cout << "gba-desktop: artifact written to " << opts.artifact_path << "\n";
  } else {
    std::cout << artifact;
  }

  // Genuine execution failures exit non-zero so scripts can gate on them.
  if (unsupported_instructions != 0 || fetch_failures != 0 || abnormal_seen) {
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const CliOptions opts = parse_cli(argc, argv);
  if (!opts.headless) {
    gba::desktop::HostOptions host;
    host.rom_path = opts.rom_path;
    host.initial_scale = 3;
    host.quit_after_frames = opts.quit_after_frames;
    host.reset_after_frames = opts.reset_after_frames;
    host.switch_after_frames = opts.switch_after_frames;
    host.switch_to_path = opts.switch_to_path;
    host.window_screenshot_path = opts.window_screenshot_path;
    host.save_directory = opts.save_directory;
    return gba::desktop::DesktopApp(host).run();
  }
  return run_headless(opts);
}
