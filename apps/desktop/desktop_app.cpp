#include "desktop_app.hpp"

#include <SDL3/SDL.h>

#include "stb_image_write.h"

#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace gba::desktop {

namespace {

using gba::core::EmulatorRuntime;
using gba::core::KeypadButton;

constexpr std::uint32_t kCyclesPerFrame = 280896;  // 280896 @ 16.777216 MHz
constexpr double kTargetFps = 16777216.0 / static_cast<double>(kCyclesPerFrame);
constexpr std::uint32_t kMaxStepsPerFrame = 500'000U;
constexpr int kAudioChannels = 2;
// Ring sizing in frames of audio (~10 ms each): enough to absorb scheduling
// jitter, small enough that mute/underrun effects stay audible-honest.
constexpr std::uint32_t kAudioRingSamples = 1024 * kAudioChannels * 4;

const std::unordered_map<SDL_Scancode, KeypadButton> kKeyBindings{
    {SDL_SCANCODE_Z, KeypadButton::a},
    {SDL_SCANCODE_X, KeypadButton::b},
    {SDL_SCANCODE_BACKSPACE, KeypadButton::select},
    {SDL_SCANCODE_RETURN, KeypadButton::start},
    {SDL_SCANCODE_RIGHT, KeypadButton::right},
    {SDL_SCANCODE_LEFT, KeypadButton::left},
    {SDL_SCANCODE_UP, KeypadButton::up},
    {SDL_SCANCODE_DOWN, KeypadButton::down},
    {SDL_SCANCODE_S, KeypadButton::l},
    {SDL_SCANCODE_A, KeypadButton::r},
};

const std::unordered_map<SDL_GamepadButton, KeypadButton> kPadBindings{
    {SDL_GAMEPAD_BUTTON_EAST, KeypadButton::a},
    {SDL_GAMEPAD_BUTTON_SOUTH, KeypadButton::b},
    {SDL_GAMEPAD_BUTTON_BACK, KeypadButton::select},
    {SDL_GAMEPAD_BUTTON_START, KeypadButton::start},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, KeypadButton::right},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, KeypadButton::left},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, KeypadButton::up},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, KeypadButton::down},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, KeypadButton::l},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, KeypadButton::r},
};

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!file) {
    return {};
  }
  return bytes;
}

bool write_binary_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  if (!bytes.empty()) {
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(file);
}

const char* save_kind_name(gba::core::GamePakSaveType type) {
  switch (type) {
    case gba::core::GamePakSaveType::none: return "none";
    case gba::core::GamePakSaveType::sram32k: return "sram32k";
    case gba::core::GamePakSaveType::flash64k: return "flash64k";
    case gba::core::GamePakSaveType::flash128k: return "flash128k";
    case gba::core::GamePakSaveType::eeprom512: return "eeprom512";
    case gba::core::GamePakSaveType::eeprom8k: return "eeprom8k";
    default: return "unknown";
  }
}

}  // namespace

DesktopApp::DesktopApp(HostOptions options) : options_(std::move(options)) {}

DesktopApp::~DesktopApp() {
  if (audio_stream_ != nullptr) {
    SDL_DestroyAudioStream(audio_stream_);
  }
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
  }
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

bool DesktopApp::init_window() {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    std::cerr << "gba-desktop: SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }
  char title[512];
  std::snprintf(title, sizeof(title), "gba-desktop - %s",
                std::filesystem::path(options_.rom_path).filename().string().c_str());
  const int width = 240 * options_.initial_scale;
  const int height = 160 * options_.initial_scale;
  const SDL_WindowFlags flags = options_.start_fullscreen
                                    ? SDL_WINDOW_FULLSCREEN
                                    : SDL_WINDOW_RESIZABLE;
  window_ = SDL_CreateWindow(title, width, height, flags);
  if (window_ == nullptr) {
    std::cerr << "gba-desktop: SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    std::cerr << "gba-desktop: SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    return false;
  }
  // The engine framebuffer is already RGB565; upload it directly and let the
  // GPU do nearest-neighbor scaling (SDL_ScaleModeNearest).
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                               SDL_TEXTUREACCESS_STREAMING, 240, 160);
  if (texture_ == nullptr) {
    std::cerr << "gba-desktop: SDL_CreateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }
  SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderLogicalPresentation(renderer_, 240, 160, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  return true;
}

bool DesktopApp::init_audio() {
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_S16;
  spec.channels = kAudioChannels;
  spec.freq = static_cast<int>(gba::core::Apu::kAudioSampleRate);
  audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                            nullptr, nullptr);
  if (audio_stream_ == nullptr) {
    std::cerr << "gba-desktop: audio device unavailable (" << SDL_GetError()
              << "); continuing without sound\n";
    return false;
  }
  SDL_ResumeAudioStreamDevice(audio_stream_);
  return true;
}

bool DesktopApp::load_rom_and_saves() {
  std::vector<std::uint8_t> rom = read_binary_file(options_.rom_path);
  if (rom.empty()) {
    std::cerr << "gba-desktop: cannot read ROM: " << options_.rom_path << "\n";
    return false;
  }
  if (runtime_.load_rom(rom) != gba::core::EmulatorRuntimeStatus::ok) {
    std::cerr << "gba-desktop: ROM rejected by engine: " << options_.rom_path << "\n";
    return false;
  }

  const auto detected = runtime_.session().memory().detect_game_pak_save_type();
  stats_.cartridge_save_kind = detected ? save_kind_name(*detected) : "none";

  cartridge_save_path_ = options_.save_directory.empty()
                             ? options_.rom_path + ".sav"
                             : (std::filesystem::path(options_.save_directory) /
                                (std::filesystem::path(options_.rom_path).filename().string() +
                                 ".sav"))
                                   .string();
  if (detected && *detected != gba::core::GamePakSaveType::none) {
    auto existing = read_binary_file(cartridge_save_path_);
    if (!existing.empty()) {
      if (runtime_.session().memory().import_game_pak_save(*detected, existing)) {
        stats_.cartridge_save_loaded = true;
        std::cerr << "gba-desktop: loaded cartridge save (" << stats_.cartridge_save_kind
                  << ") from " << cartridge_save_path_ << "\n";
      } else {
        std::cerr << "gba-desktop: cartridge save rejected (" << stats_.cartridge_save_kind
                  << ", " << existing.size() << " bytes); starting with a fresh save\n";
      }
    }
    cartridge_save_snapshot_ = runtime_.session().memory().export_game_pak_save();
  }
  return true;
}

void DesktopApp::apply_key(SDL_Scancode scancode, bool down) {
  const auto it = kKeyBindings.find(scancode);
  if (it == kKeyBindings.end()) {
    return;
  }
  const std::uint16_t bit = static_cast<std::uint16_t>(it->second);
  input_mask_ = down ? static_cast<std::uint16_t>(input_mask_ | bit)
                     : static_cast<std::uint16_t>(input_mask_ & ~bit);
  runtime_.set_button_mask(input_mask_);
}

void DesktopApp::pump_input() {
  // Gamepads feed the same mask as the keyboard.
  int pad_count = 0;
  SDL_JoystickID* pads = SDL_GetGamepads(&pad_count);
  if (pads != nullptr) {
    for (int i = 0; i < pad_count; ++i) {
      SDL_Gamepad* pad = SDL_OpenGamepad(pads[i]);
      if (pad == nullptr) {
        continue;
      }
      for (const auto& [button, gba_button] : kPadBindings) {
        const std::uint16_t bit = static_cast<std::uint16_t>(gba_button);
        const bool pressed = SDL_GetGamepadButton(pad, button);
        input_mask_ = pressed ? static_cast<std::uint16_t>(input_mask_ | bit)
                              : static_cast<std::uint16_t>(input_mask_ & ~bit);
      }
      SDL_CloseGamepad(pad);
    }
    SDL_free(pads);
  }
  runtime_.set_button_mask(input_mask_);
}

void DesktopApp::toggle_fullscreen() {
  const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
      SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
  SDL_SetWindowFullscreen(window_, flags ? false : true);
}

void DesktopApp::reset_runtime() {
  // Reload the ROM through load_rom so boot configuration (save type, HLE
  // entry) is re-established exactly as at startup.
  std::vector<std::uint8_t> rom = read_binary_file(options_.rom_path);
  if (!rom.empty() && runtime_.load_rom(rom) == gba::core::EmulatorRuntimeStatus::ok) {
    std::cerr << "gba-desktop: runtime reset\n";
  } else {
    std::cerr << "gba-desktop: reset failed to reload ROM\n";
  }
}

void DesktopApp::save_cartridge_save(bool force) {
  if (cartridge_save_path_.empty()) {
    return;
  }
  const auto current = runtime_.session().memory().export_game_pak_save();
  ++frames_since_save_flush_;
  if (!force && current == cartridge_save_snapshot_) {
    return;
  }
  if (!force && frames_since_save_flush_ < 300) {
    return;  // flush at most ~every 5 seconds of dirty frames
  }
  if (write_binary_file(cartridge_save_path_, current)) {
    cartridge_save_snapshot_ = current;
    frames_since_save_flush_ = 0;
  } else {
    std::cerr << "gba-desktop: failed to write cartridge save: "
              << cartridge_save_path_ << "\n";
  }
}

bool DesktopApp::write_save_state(int slot) {
  const std::string path = cartridge_save_path_ + ".state" + std::to_string(slot);
  const auto blob = gba::core::SaveStateCodec::encode(runtime_.session());
  if (!write_binary_file(path, blob)) {
    std::cerr << "gba-desktop: failed to write save state: " << path << "\n";
    return false;
  }
  std::cerr << "gba-desktop: save state " << slot << " written to " << path << "\n";
  return true;
}

bool DesktopApp::read_save_state(int slot) {
  const std::string path = cartridge_save_path_ + ".state" + std::to_string(slot);
  const auto blob = read_binary_file(path);
  if (blob.empty()) {
    std::cerr << "gba-desktop: no save state " << slot << " at " << path << "\n";
    return false;
  }
  const auto decoded = gba::core::SaveStateCodec::decode_into(runtime_.session(), blob);
  if (decoded.status != gba::core::SaveStateDecodeStatus::ok) {
    std::cerr << "gba-desktop: save state " << slot
              << " failed to load (status " << static_cast<int>(decoded.status)
              << ", blob version " << decoded.version << ")\n";
    return false;
  }
  std::cerr << "gba-desktop: save state " << slot << " loaded\n";
  return true;
}

void DesktopApp::open_rom_dialog() {
  // Replacing the ROM at runtime: pick a file, then re-run boot with it.
  SDL_DialogFileFilter filters[] = {{const_cast<char*>("GBA ROMs"), const_cast<char*>("gba")}};
  SDL_ShowOpenFileDialog(
      [](void* userdata, const char* const* filelist, int) {
        auto* self = static_cast<DesktopApp*>(userdata);
        if (filelist != nullptr && filelist[0] != nullptr) {
          self->options_.rom_path = filelist[0];
          self->cartridge_save_path_.clear();
          self->load_rom_and_saves();
        }
      },
      this, window_, filters, 1, nullptr, false);
}

void DesktopApp::handle_event(const union SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      running_ = false;
      break;
    case SDL_EVENT_KEY_DOWN:
      if (!event.key.repeat) {
        switch (event.key.scancode) {
          case SDL_SCANCODE_ESCAPE: running_ = false; break;
          case SDL_SCANCODE_P: paused_ = !paused_; break;
          case SDL_SCANCODE_F5: write_save_state(1); break;
          case SDL_SCANCODE_F8: read_save_state(1); break;
          case SDL_SCANCODE_F9: reset_runtime(); break;
          case SDL_SCANCODE_F11: toggle_fullscreen(); break;
          case SDL_SCANCODE_M: mute_ = !mute_; break;
          case SDL_SCANCODE_O: open_rom_dialog(); break;
          case SDL_SCANCODE_SPACE: paused_ = !paused_; break;
          default: apply_key(event.key.scancode, true); break;
        }
      }
      break;
    case SDL_EVENT_KEY_UP:
      apply_key(event.key.scancode, false);
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      running_ = false;
      break;
    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr) {
        options_.rom_path = event.drop.data;
        cartridge_save_path_.clear();
        load_rom_and_saves();
      }
      break;
    default:
      break;
  }
}

void DesktopApp::push_audio() {
  if (audio_stream_ == nullptr) {
    return;
  }
  const auto& batch = runtime_.last_audio_batch();
  if (batch.empty()) {
    return;
  }
  if (mute_) {
    return;
  }
  // Backpressure: if the device is not consuming (paused tab, slow sink),
  // drop oldest by truncating rather than growing memory unboundedly.
  const int queued = SDL_GetAudioStreamAvailable(audio_stream_);
  if (queued > static_cast<int>(kAudioRingSamples * 2)) {
    SDL_ClearAudioStream(audio_stream_);
  }
  SDL_PutAudioStreamData(audio_stream_, batch.data(),
                         static_cast<int>(batch.size() * sizeof(gba::core::ApuMixedSample)));
  stats_.audio_samples_consumed += batch.size();
  stats_.audio_buffer_fill = static_cast<std::uint32_t>(
      SDL_GetAudioStreamAvailable(audio_stream_) / kAudioChannels);
  if (queued == 0) {
    ++stats_.audio_underruns;
  }
}

bool DesktopApp::capture_window_screenshot() {
  if (options_.window_screenshot_path.empty()) {
    return true;
  }
  SDL_Surface* raw = SDL_RenderReadPixels(renderer_, nullptr);
  if (raw == nullptr) {
    std::cerr << "gba-desktop: SDL_RenderReadPixels failed: " << SDL_GetError() << std::endl;
    return false;
  }
  SDL_Surface* converted =
      SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ABGR8888);
  SDL_DestroySurface(raw);
  if (converted == nullptr) {
    std::cerr << "gba-desktop: SDL_ConvertSurface failed: " << SDL_GetError() << std::endl;
    return false;
  }
  const bool ok = stbi_write_png(options_.window_screenshot_path.c_str(), converted->w,
                                 converted->h, 4, converted->pixels, converted->pitch) != 0;
  SDL_DestroySurface(converted);
  if (!ok) {
    std::cerr << "gba-desktop: failed to write window screenshot" << std::endl;
  }
  return ok;
}

void DesktopApp::present_frame() {
  const auto& fb = runtime_.framebuffer();
  if (!SDL_UpdateTexture(texture_, nullptr, fb.data(),
                         static_cast<int>(240 * sizeof(std::uint16_t)))) {
    std::cerr << "gba-desktop: SDL_UpdateTexture failed: " << SDL_GetError() << "\n";
  }
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
  ++stats_.frames_presented;
}

void DesktopApp::pace_frame(bool fast_forward) {
  if (fast_forward) {
    return;
  }
  // Audio-clock pacing: when the stream queue is comfortably full, the device
  // is consuming slower than we produce, so present immediately; otherwise
  // wait on the wall-clock frame budget.
  static auto next_deadline = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (now < next_deadline) {
    std::this_thread::sleep_for(next_deadline - now);
  } else {
    next_deadline = now;
  }
  next_deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / kTargetFps));
}

int DesktopApp::run() {
  if (!init_window() || !load_rom_and_saves()) {
    return 3;
  }
  init_audio();

  std::uint64_t perf_frames = 0;
  const auto perf_start = std::chrono::steady_clock::now();
  auto perf_mark = perf_start;

  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handle_event(event);
    }
    pump_input();

    const bool fast_forward = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] != 0;
    if (paused_) {
      SDL_Delay(10);
      continue;
    }

    const auto result = runtime_.step_frame(kMaxStepsPerFrame);
    if (result.status != gba::core::EmulatorRuntimeStatus::ok) {
      std::cerr << "gba-desktop: engine step failed with status "
                << static_cast<int>(result.status) << "\n";
      return 3;
    }
    if (!result.frame_complete) {
      std::cerr << "gba-desktop: engine stopped ("
                << (result.run.stop_reason == gba::core::CoreRunStopReason::unsupported_instruction
                        ? "unsupported instruction"
                        : "fetch failure")
                << ") at PC 0x" << std::hex << result.run.final_pc << std::dec << "\n";
      return 3;
    }
    push_audio();
    present_frame();
    save_cartridge_save(false);
    pace_frame(fast_forward);

    if (options_.quit_after_frames != 0 &&
        stats_.frames_presented >= options_.quit_after_frames) {
      running_ = false;
    }

    ++perf_frames;
    const auto now = std::chrono::steady_clock::now();
    const double since_mark = std::chrono::duration<double>(now - perf_mark).count();
    if (since_mark >= 1.0) {
      stats_.host_fps = static_cast<double>(perf_frames) / since_mark;
      perf_frames = 0;
      perf_mark = now;
    }
  }

  save_cartridge_save(true);
  if (options_.quit_after_frames != 0 && !capture_window_screenshot()) {
    return 3;
  }
  const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - perf_start).count();
  stats_.avg_frame_ms = total * 1000.0 / static_cast<double>(stats_.frames_presented ? stats_.frames_presented : 1);
  std::cerr << "gba-desktop: session ended (" << stats_.frames_presented << " frames, "
            << stats_.host_fps << " fps last window, " << stats_.audio_underruns
            << " audio underruns)\n";
  return 0;
}

}  // namespace gba::desktop
