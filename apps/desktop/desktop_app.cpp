#include "desktop_app.hpp"
#include "frame_interval_stats.hpp"

#include <SDL3/SDL.h>

#include "desktop_persistence.hpp"
#include "desktop_rom_file.hpp"
#include "save_state_guard.hpp"
#include "save_type_selection.hpp"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace gba::desktop {

namespace {

using gba::core::EmulatorRuntime;
using gba::core::KeypadButton;

constexpr std::uint32_t kCyclesPerFrame = 280896;  // 280896 @ 16.777216 MHz
constexpr double kTargetFps = 16777216.0 / static_cast<double>(kCyclesPerFrame);
constexpr std::uint32_t kMaxStepsPerFrame = 500'000U;
constexpr int kAudioChannels = 2;
// Audio queue limits, expressed in frames (one frame = one stereo sample).
// The device normally drains far faster than we fill; these bounds only kick
// in when the sink stalls, where dropping stale audio beats unbounded growth.
constexpr std::uint32_t kAudioQueueLimitFrames = 8192;
constexpr std::uint32_t kAudioBackpressureFrames = kAudioQueueLimitFrames * 2;
// Flush a dirty cartridge save at most this often (~5 s at native speed).
constexpr std::uint32_t kSnapshotFlushFrames = 300;

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

}  // namespace

struct DesktopApp::RomDialogResult {
  std::atomic<bool> ready{false};
  std::string path;
  std::string error;
};

DesktopApp::DesktopApp(HostOptions options)
    : options_(std::move(options)), override_rom_path_(options_.rom_path) {}

DesktopApp::~DesktopApp() {
  collect_save_state(true);
  close_all_gamepads();
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
  // Cache already-connected gamepads once; per-frame SDL_GetGamepads()/
  // SDL_OpenGamepad() churn is both wasteful and prone to losing state.
  int pad_count = 0;
  SDL_JoystickID* pads = SDL_GetGamepads(&pad_count);
  if (pads != nullptr) {
    for (int i = 0; i < pad_count; ++i) {
      open_gamepad(static_cast<unsigned int>(pads[i]));
    }
    SDL_free(pads);
  }

  const int width = 240 * options_.initial_scale;
  const int height = 160 * options_.initial_scale;
  const SDL_WindowFlags flags = options_.start_fullscreen
                                    ? SDL_WINDOW_FULLSCREEN
                                    : SDL_WINDOW_RESIZABLE;
  window_ = SDL_CreateWindow("gba-desktop", width, height, flags);
  if (window_ == nullptr) {
    std::cerr << "gba-desktop: SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return false;
  }
  update_window_title();
  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    std::cerr << "gba-desktop: SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    return false;
  }
  // Native GBA pixels have five bits per channel, with red in bits 0-4.
  // SDL's XBGR1555 matches that layout, including the unused high bit.
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XBGR1555,
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
  prime_audio_queue();
  SDL_ResumeAudioStreamDevice(audio_stream_);
  return true;
}

bool DesktopApp::load_rom_from_path(const std::string& path, bool allow_rollback) {
  const auto rom = read_gba_rom(path);
  if (rom.empty()) {
    std::cerr << "gba-desktop: cannot read ROM (expected a file up to 32 MiB): "
              << path << "\n";
    return false;
  }
  if (!has_gba_cartridge_header(rom)) {
    std::cerr << "gba-desktop: missing or invalid GBA cartridge header: " << path << "\n";
    return false;
  }

  // Keep the running session recoverable if the incoming ROM is rejected:
  // AndroidRuntime::load_rom() resets the session before validating.
  std::optional<std::vector<std::uint8_t>> rollback;
  if (allow_rollback && session_loaded_) {
    rollback = gba::core::SaveStateCodec::encode(runtime_.session());
  }

  const auto selection = save_type_for_rom(
      override_rom_path_, path, options_.save_type_override);
  if (runtime_.load_rom(rom) != gba::core::EmulatorRuntimeStatus::ok ||
      !apply_save_type(runtime_.session().memory(), selection)) {
    std::cerr << "gba-desktop: ROM rejected by engine: " << path << "\n";
    if (rollback.has_value()) {
      const auto restored =
          gba::core::SaveStateCodec::decode_into(runtime_.session(), *rollback);
      if (restored.status == gba::core::SaveStateDecodeStatus::ok) {
        std::cerr << "gba-desktop: previous session restored; still playing "
                  << options_.rom_path << "\n";
      } else {
        // The session could not be recovered and the outgoing save was already
        // flushed by switch_rom(). Disable further cartridge writes so a broken
        // in-memory save can never truncate the good on-disk file, and stop
        // cleanly rather than continuing on a ROM-less session.
        cartridge_save_enabled_ = false;
        cartridge_save_path_.clear();
        session_loaded_ = false;
        running_ = false;
        std::cerr << "gba-desktop: previous session could not be restored after a failed"
                     " ROM load; save writes disabled, exiting to protect the on-disk save\n";
      }
    }
    return false;
  }

  options_.rom_path = path;

  const auto configured = runtime_.session().memory().game_pak_save_type();
  stats_.cartridge_save_kind = save_kind_name(configured);
  if (selection) {
    std::cerr << "gba-desktop: cartridge save override (" << save_kind_name(configured)
              << ") for " << path << "\n";
  }
  stats_.cartridge_save_loaded = false;
  save_prefix_ = persistence::save_data_prefix(options_.rom_path, options_.save_directory);
  cartridge_save_path_ = persistence::cartridge_save_path(save_prefix_);
  cartridge_save_enabled_ =
      configured != gba::core::GamePakSaveType::none &&
      runtime_.session().memory().has_game_pak_save();
  cartridge_save_snapshot_.clear();
  cartridge_save_initialized_on_disk_ = false;
  retry_cartridge_save_enable_ = false;
  frames_since_save_flush_ = 0;

  if (cartridge_save_enabled_) {
    if (!options_.save_directory.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(
          persistence::native_path(options_.save_directory), ec);
      if (ec) {
        std::cerr << "gba-desktop: warning: cannot create save directory "
                  << options_.save_directory << ": " << ec.message() << "\n";
      }
    }
    const auto save_path = persistence::native_path(cartridge_save_path_);
    std::error_code save_ec;
    const bool recovery_needed = !std::filesystem::exists(save_path, save_ec) &&
        std::filesystem::exists(persistence::native_path(cartridge_save_path_ + ".old"),
                                save_ec);
    if (!persistence::recover_file_orphan(save_path, save_ec)) {
      std::cerr << "gba-desktop: cannot safely read cartridge save " << save_path
                << "; cartridge save writes are DISABLED\n";
      cartridge_save_enabled_ = false;
      retry_cartridge_save_enable_ = true;
    } else if (recovery_needed) {
      std::cerr << "gba-desktop: recovered interrupted save write from "
                << cartridge_save_path_ << ".old\n";
    }
    const std::vector<std::uint8_t> existing =
        cartridge_save_enabled_ ? persistence::read_binary_file(cartridge_save_path_)
                                : std::vector<std::uint8_t>{};
    if (cartridge_save_enabled_ && std::filesystem::exists(save_path, save_ec)) {
      if (runtime_.session().memory().import_game_pak_save(configured, existing)) {
        cartridge_save_initialized_on_disk_ = true;
        stats_.cartridge_save_loaded = true;
        std::cerr << "gba-desktop: loaded cartridge save (" << stats_.cartridge_save_kind
                  << ") from " << cartridge_save_path_ << "\n";
      } else {
        const auto backup = persistence::backup_file(cartridge_save_path_, ".rejected");
        std::cerr << "gba-desktop: cartridge save rejected (" << stats_.cartridge_save_kind
                  << ", " << existing.size() << " bytes)";
        if (backup.has_value()) {
          std::cerr << "; preserved copy at " << *backup;
        } else {
          // Fail closed: without a preserved copy, flushing this session's
          // (fresh) save later would overwrite the only on-disk copy of the
          // player's data. Keep playing, but never write cartridge saves.
          cartridge_save_enabled_ = false;
          retry_cartridge_save_enable_ = true;
          std::cerr << "; could not preserve a copy -- cartridge save writes are"
                       " DISABLED to protect the original file";
        }
        std::cerr << "; starting with a fresh save\n";
      }
    }
      cartridge_save_snapshot_ = runtime_.session().memory().export_game_pak_save();
  } else {
    // No cartridge-backed save: leave the path empty so shutdown cannot create
    // a spurious zero-length .sav next to the ROM.
    cartridge_save_path_.clear();
  }

  session_loaded_ = true;
  paused_ = false;
  update_window_title();
  clear_input();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  return true;
}

bool DesktopApp::load_rom_and_saves() {
  return load_rom_from_path(options_.rom_path, /*allow_rollback=*/false);
}

bool DesktopApp::switch_rom(const std::string& path) {
  // Persist the outgoing game's progress before any destructive step, and
  // refuse the switch when that write fails: replacing the running game with
  // unsaved dirty progress on disk would silently discard it. The session
  // keeps running, so the user can fix the problem (disk space, permissions)
  // and retry.
  if (!flush_cartridge_save(true)) {
    std::cerr << "gba-desktop: ROM switch refused: could not write cartridge save "
              << cartridge_save_path_ << "; keeping the running game\n";
    return false;
  }
  return load_rom_from_path(path, /*allow_rollback=*/true);
}

void DesktopApp::apply_key(SDL_Scancode scancode, bool down) {
  const auto it = kKeyBindings.find(scancode);
  if (it == kKeyBindings.end()) {
    return;
  }
  const std::uint16_t bit = static_cast<std::uint16_t>(it->second);
  keyboard_mask_ = down ? static_cast<std::uint16_t>(keyboard_mask_ | bit)
                        : static_cast<std::uint16_t>(keyboard_mask_ & ~bit);
  sync_input_mask();
}

void DesktopApp::sync_input_mask() {
  input_mask_ = static_cast<std::uint16_t>(keyboard_mask_ | pad_mask_);
  if (session_loaded_) {
    (void)runtime_.set_button_mask(input_mask_);
  }
}

void DesktopApp::open_gamepad(unsigned int instance_id) {
  const auto id = static_cast<SDL_JoystickID>(instance_id);
  for (SDL_Gamepad* existing : gamepads_) {
    if (existing != nullptr && SDL_GetGamepadID(existing) == id) {
      return;
    }
  }
  SDL_Gamepad* pad = SDL_OpenGamepad(id);
  if (pad == nullptr) {
    std::cerr << "gba-desktop: could not open gamepad " << instance_id << ": "
              << SDL_GetError() << "\n";
    return;
  }
  const char* name = SDL_GetGamepadName(pad);
  std::cerr << "gba-desktop: gamepad connected: " << (name != nullptr ? name : "unknown")
            << "\n";
  gamepads_.push_back(pad);
}

void DesktopApp::close_gamepad(unsigned int instance_id) {
  const auto id = static_cast<SDL_JoystickID>(instance_id);
  for (auto it = gamepads_.begin(); it != gamepads_.end(); ++it) {
    if (*it != nullptr && SDL_GetGamepadID(*it) == id) {
      SDL_CloseGamepad(*it);
      gamepads_.erase(it);
      break;
    }
  }
  // Drop this controller's contribution immediately; pump_input() re-derives
  // the full pad mask from the remaining controllers on the next frame.
  pad_mask_ = 0;
  sync_input_mask();
}

void DesktopApp::close_all_gamepads() {
  for (SDL_Gamepad* pad : gamepads_) {
    if (pad != nullptr) {
      SDL_CloseGamepad(pad);
    }
  }
  gamepads_.clear();
}

void DesktopApp::clear_input() {
  keyboard_mask_ = 0;
  pad_mask_ = 0;
  input_mask_ = 0;
  if (session_loaded_) {
    (void)runtime_.set_button_mask(0);
  }
}

void DesktopApp::pump_input() {
  if (!session_loaded_) {
    return;
  }
  // Gamepads contribute to their own mask; the keyboard keeps its own. An
  // idle pad therefore never clears a held key, and two pads cannot cancel
  // each other because presses are OR-combined.
  std::uint16_t pad_mask = 0;
  for (SDL_Gamepad* pad : gamepads_) {
    if (pad == nullptr) {
      continue;
    }
    for (const auto& [button, gba_button] : kPadBindings) {
      if (SDL_GetGamepadButton(pad, button)) {
        pad_mask = static_cast<std::uint16_t>(pad_mask |
                                              static_cast<std::uint16_t>(gba_button));
      }
    }
  }
  pad_mask_ = pad_mask;
  sync_input_mask();
}

void DesktopApp::toggle_fullscreen() {
  const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
      SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
  SDL_SetWindowFullscreen(window_, flags ? false : true);
}

void DesktopApp::reset_runtime() {
  if (!session_loaded_) {
    return;
  }
  const auto rom = runtime_.session().memory().export_game_pak_rom();
  // A console reset clears work RAM but leaves cartridge-backed save memory
  // intact, so capture it before reloading and re-import it afterwards. This
  // is distinct from load-state, which restores a full snapshot.
  const gba::core::GamePakSaveType save_type =
      runtime_.session().memory().game_pak_save_type();
  const std::vector<std::uint8_t> cartridge_save =
      runtime_.session().memory().export_game_pak_save();
  const std::vector<std::uint8_t> rollback =
      gba::core::SaveStateCodec::encode(runtime_.session());

  if (runtime_.load_rom(rom) != gba::core::EmulatorRuntimeStatus::ok ||
      !apply_save_type(runtime_.session().memory(), save_type)) {
    const auto restored = gba::core::SaveStateCodec::decode_into(runtime_.session(), rollback);
    std::cerr << "gba-desktop: reset failed to reload ROM";
    if (restored.status == gba::core::SaveStateDecodeStatus::ok) {
      std::cerr << " (session restored)";
    }
    std::cerr << "\n";
    return;
  }
  bool preserved = true;
  if (save_type != gba::core::GamePakSaveType::none && !cartridge_save.empty()) {
    if (!runtime_.session().memory().import_game_pak_save(save_type, cartridge_save)) {
      // Never let a blanked in-memory save overwrite the good on-disk one.
      cartridge_save_enabled_ = false;
      retry_cartridge_save_enable_ = false;
      preserved = false;
      std::cerr << "gba-desktop: warning: could not restore cartridge save across reset;"
                   " disabling save writes to protect the on-disk save\n";
    }
  }
  // Re-apply the physical input masks to the freshly reset session so a key
  // held across F9 keeps working (the OS will not resend a filtered KEY_DOWN).
  sync_input_mask();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  std::cerr << "gba-desktop: runtime reset ("
            << (preserved ? "cartridge save preserved" : "cartridge save NOT preserved")
            << ")\n";
}

bool DesktopApp::flush_cartridge_save(bool force) {
  if (cartridge_save_path_.empty()) {
    return true;
  }
  if (!force && ++frames_since_save_flush_ < kSnapshotFlushFrames) {
    return true;
  }
  frames_since_save_flush_ = 0;
  auto current = runtime_.session().memory().export_game_pak_save();
  const auto target = persistence::native_path(cartridge_save_path_);
  std::error_code ec;
  if (current == cartridge_save_snapshot_) {
    if (!force || !cartridge_save_enabled_ ||
        (cartridge_save_initialized_on_disk_ && std::filesystem::is_regular_file(target, ec))) {
      return true;
    }
  }
  if (!cartridge_save_enabled_ && retry_cartridge_save_enable_ &&
      persistence::recover_file_orphan(target, ec)) {
    const bool exists = std::filesystem::exists(target, ec);
    if (!ec && (!exists || persistence::backup_file(cartridge_save_path_, ".rejected"))) {
      cartridge_save_enabled_ = true;
      retry_cartridge_save_enable_ = false;
      update_window_title();
      std::cerr << "gba-desktop: cartridge save destination ready again: "
                << cartridge_save_path_ << "\n";
    }
  }
  if (!cartridge_save_enabled_) {
    return false;
  }
  if (current.empty()) {
    // A cartridge-less machine state must never replace a sized save file:
    // treat this like a failed write and keep the on-disk data.
    std::cerr << "gba-desktop: refusing to write empty cartridge save over "
              << cartridge_save_path_ << "\n";
    return false;
  }
  if (persistence::write_file_atomic(cartridge_save_path_, current)) {
    cartridge_save_initialized_on_disk_ = true;
    cartridge_save_snapshot_ = std::move(current);
    return true;
  }
  std::cerr << "gba-desktop: failed to write cartridge save: " << cartridge_save_path_
            << "\n";
  return false;
}

bool DesktopApp::write_save_state(int slot) {
  if (save_prefix_.empty()) {
    std::cerr << "gba-desktop: no ROM loaded; cannot write save state\n";
    return false;
  }
  const std::string path = persistence::save_state_path(save_prefix_, slot);
  collect_save_state(false);
  if (save_state_writer_.busy()) {
    std::cerr << "gba-desktop: previous save state is still in progress\n";
    return false;
  }
  try {
    // The short capture remains on the emulation thread. Hashing, encoding
    // and file IO run on a private machine, with no live emulator references.
    auto snapshot = std::make_unique<gba::core::CoreSessionState>(
        runtime_.session().save_state());
    const bool started = save_state_writer_.start(
        path, [snapshot = std::move(snapshot)]() mutable {
          auto machine = std::make_unique<gba::core::CoreSession>();
          machine->load_state(*snapshot);
          snapshot.reset();
          return gba::core::SaveStateCodec::encode(*machine);
        });
    if (!started) return false;
    std::cerr << "gba-desktop: save state " << slot << " queued for " << path << "\n";
    return true;
  } catch (const std::exception& error) {
    std::cerr << "gba-desktop: failed to start save state: " << path << ": "
              << error.what() << "\n";
    return false;
  }
}

void DesktopApp::collect_save_state(bool wait) {
  try {
    const auto result = wait ? save_state_writer_.finish() : save_state_writer_.poll();
    if (!result) return;
    if (result->written) {
      std::cerr << "gba-desktop: save state written to " << result->path << "\n";
    } else {
      std::cerr << "gba-desktop: failed to write save state: " << result->path
                << ": " << result->error << "\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "gba-desktop: save state worker failed: " << error.what() << "\n";
  }
}

bool DesktopApp::read_save_state(int slot) {
  collect_save_state(true);
  if (save_prefix_.empty()) {
    std::cerr << "gba-desktop: no ROM loaded; cannot load save state\n";
    return false;
  }
  const std::string path = persistence::save_state_path(save_prefix_, slot);
  return read_save_state_path(path, "save state " + std::to_string(slot));
}

bool DesktopApp::read_save_state_path(const std::string& path, const std::string& label) {
  const auto blob = persistence::read_checkpoint(path);
  if (blob.empty()) {
    std::cerr << "gba-desktop: cannot read " << label << " at " << path << "\n";
    return false;
  }
  // The core codec restores whatever ROM its blob carries, so the host also
  // requires the state to belong to the currently loaded ROM. On any refusal
  // the live session is left untouched. The decode scratch is a full ~405 KB
  // machine; heap-allocate it so this interactive path never stacks one on
  // the (heap-resident) DesktopApp against the thread-stack reservation.
  auto scratch = std::make_unique<gba::core::CoreSession>();
  const SaveStateLoadStatus status =
      load_save_state_for_session(runtime_.session(), blob, *scratch);
  if (status == SaveStateLoadStatus::decode_failed) {
    std::cerr << "gba-desktop: " << label
              << " failed to load (corrupt, unsupported, or mismatched)\n";
    return false;
  }
  if (status == SaveStateLoadStatus::wrong_rom) {
    std::cerr << "gba-desktop: " << label
              << " belongs to a different ROM; refusing to load\n";
    return false;
  }
  if (status == SaveStateLoadStatus::wrong_save_type) {
    std::cerr << "gba-desktop: " << label
              << " uses a different save type; refusing to load\n";
    return false;
  }
  // Keep held buttons working against the restored session.
  sync_input_mask();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  std::cerr << "gba-desktop: " << label << " loaded\n";
  return true;
}

void DesktopApp::open_rom_dialog() {
  if (rom_dialog_) {
    return;
  }
  static constexpr SDL_DialogFileFilter filters[] = {{"GBA ROMs", "gba"}};
  rom_dialog_ = std::make_shared<RomDialogResult>();
  // SDL may finish on another thread or after the window starts closing.
  // Its callback owns the result only. The event loop changes the session.
  auto* callback_result = new std::shared_ptr<RomDialogResult>(rom_dialog_);
  clear_queued_audio();
  update_window_title();
  SDL_ShowOpenFileDialog(
      [](void* userdata, const char* const* filelist, int) {
        const std::unique_ptr<std::shared_ptr<RomDialogResult>> result(
            static_cast<std::shared_ptr<RomDialogResult>*>(userdata));
        if (filelist == nullptr) {
          (*result)->error = SDL_GetError();
          if ((*result)->error.empty()) {
            (*result)->error = "The system file chooser could not open.";
          }
        } else if (filelist[0] != nullptr) {
          (*result)->path = filelist[0];
        }
        (*result)->ready.store(true, std::memory_order_release);
      },
      callback_result, window_, filters, 1, nullptr, false);
}

void DesktopApp::poll_rom_dialog() {
  if (!rom_dialog_ || !rom_dialog_->ready.load(std::memory_order_acquire)) {
    return;
  }
  const auto result = std::move(rom_dialog_);
  if (quit_after_dialog_) {
    request_quit();
    return;
  }
  if (!result->error.empty()) {
    std::cerr << "gba-desktop: file chooser failed: " << result->error
              << ". Pass a ROM path on the command line.\n";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open file chooser",
                             "Use the ROM's path when launching gba-desktop.", window_);
    if (!session_loaded_) {
      startup_dialog_failed_ = true;
      running_ = false;
    }
  } else if (result->path.empty()) {
    if (!session_loaded_) {
      running_ = false;
    }
  } else if (!switch_rom(result->path)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open ROM",
                            "Choose a readable GBA ROM and check that the save directory is writable.",
                             window_);
  }
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  update_window_title();
}

void DesktopApp::request_quit() {
  if (rom_dialog_) {
    quit_after_dialog_ = true;
  } else {
    quit_requested_ = true;
  }
}

void DesktopApp::update_window_title() {
  std::string title = "gba-desktop";
  if (!options_.rom_path.empty()) {
    title += " - " + persistence::native_path(options_.rom_path).filename().u8string();
  }
  if (rom_dialog_) title += " [Choosing ROM]";
  if (paused_) title += " [Paused]";
  if (mute_) title += " [Muted]";
  if (!cartridge_save_enabled_ && !cartridge_save_path_.empty()) {
    title += " [Save writes disabled]";
  }
  title += " (O: Open ROM, F1: Controls)";
  SDL_SetWindowTitle(window_, title.c_str());
}

void DesktopApp::show_controls() {
  SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_INFORMATION, "GBA controls",
      "D-pad    Arrow keys\nA / B    Z / X\nStart / Select    Enter / Backspace\n"
      "L / R    S / A\n\nPause    P / Space\nSave / Load state    F5 / F8\n"
      "Reset    F9\nMute    M\nFullscreen    F11\nFast-forward    Hold Tab\n"
      "Open ROM    O\nQuit    Esc", window_);
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
}

void DesktopApp::handle_event(const union SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      request_quit();
      break;
    case SDL_EVENT_KEY_DOWN:
      if (!event.key.repeat) {
        switch (event.key.scancode) {
          case SDL_SCANCODE_ESCAPE:
            request_quit();
            discard_requested_ = confirm_unsaved_quit_ &&
                                 (event.key.mod & SDL_KMOD_SHIFT) != 0;
            break;
          case SDL_SCANCODE_P:
          case SDL_SCANCODE_SPACE:
            paused_ = !paused_;
            if (paused_) {
              clear_queued_audio();
            } else {
              clear_queued_audio();
              next_frame_deadline_ = Clock::now();
            }
            update_window_title();
            break;
          case SDL_SCANCODE_F1: show_controls(); break;
          case SDL_SCANCODE_F5: write_save_state(1); break;
          case SDL_SCANCODE_F8: read_save_state(1); break;
          case SDL_SCANCODE_F9: reset_runtime(); break;
          case SDL_SCANCODE_F11: toggle_fullscreen(); break;
          case SDL_SCANCODE_M:
            mute_ = !mute_;
            clear_queued_audio();
            update_window_title();
            break;
          case SDL_SCANCODE_O: open_rom_dialog(); break;
          default: apply_key(event.key.scancode, true); break;
        }
      }
      break;
    case SDL_EVENT_KEY_UP:
      apply_key(event.key.scancode, false);
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      // Key-up events may be delivered elsewhere while unfocused; drop the
      // keyboard mask so no button stays stuck down.
      keyboard_mask_ = 0;
      sync_input_mask();
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      request_quit();
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      open_gamepad(static_cast<unsigned int>(event.gdevice.which));
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      close_gamepad(static_cast<unsigned int>(event.gdevice.which));
      break;
    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr && !rom_dialog_) {
        switch_rom(event.drop.data);
      }
      break;
    default:
      break;
  }
}

void DesktopApp::clear_queued_audio() {
  if (audio_stream_ != nullptr) {
    SDL_ClearAudioStream(audio_stream_);
    if (!paused_ && !mute_) {
      prime_audio_queue();
    }
  }
}

void DesktopApp::prime_audio_queue() {
  if (audio_stream_ == nullptr) {
    return;
  }
  // Restore the same 62.5 ms lead after a queue reset as at startup. Without
  // this lead, resuming can repeatedly starve the device even on timely frames.
  const std::array<gba::core::ApuMixedSample, 2048> initial_silence{};
  if (!SDL_PutAudioStreamData(audio_stream_, initial_silence.data(),
                              static_cast<int>(sizeof(initial_silence)))) {
    std::cerr << "gba-desktop: cannot prime audio queue (" << SDL_GetError() << ")\n";
  }
}

void DesktopApp::push_audio() {
  if (audio_stream_ == nullptr || mute_) {
    return;
  }
  const auto& batch = runtime_.last_audio_batch();
  if (batch.empty()) {
    return;
  }
  constexpr int kBytesPerFrame = kAudioChannels * static_cast<int>(sizeof(std::int16_t));
  const int queued_bytes = SDL_GetAudioStreamAvailable(audio_stream_);
  if (queued_bytes > static_cast<int>(kAudioBackpressureFrames) * kBytesPerFrame) {
    // Sink is not draining (stalled device): drop queued audio instead of
    // growing memory or falling ever further behind.
    SDL_ClearAudioStream(audio_stream_);
  }
  SDL_PutAudioStreamData(audio_stream_, batch.data(),
                         static_cast<int>(batch.size() * sizeof(gba::core::ApuMixedSample)));
  stats_.audio_samples_consumed += batch.size();
  stats_.audio_buffer_fill = static_cast<std::uint32_t>(
      SDL_GetAudioStreamAvailable(audio_stream_) / kBytesPerFrame);
  // Heuristic underrun signal: the device had fully drained its queue when
  // this frame's samples arrived. It is a proxy for audible glitches, not a
  // direct measurement of them.
  if (queued_bytes <= 0) {
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
  SDL_Surface* converted = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ABGR8888);
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
  // Wall-clock pacing at the native GBA frame rate. The (small) audio queue
  // absorbs the resulting jitter; locking presentation to the audio clock
  // would add latency and complexity without a measured benefit here.
  const auto frame_duration = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(1.0 / kTargetFps));
  next_frame_deadline_ += frame_duration;
  const auto now = Clock::now();
  if (next_frame_deadline_ > now) {
    // Coarse host sleeps can overshoot a frame; SDL's precise wait trades
    // some CPU for tighter pacing and can still be delayed by scheduling.
    const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
        next_frame_deadline_ - now);
    SDL_DelayPrecise(static_cast<Uint64>(remaining.count()));
  } else if (now - next_frame_deadline_ > frame_duration * 4) {
    // Fell far behind (slow frame, debugger, resume): resynchronize rather
    // than accumulate an unbounded catch-up burst.
    next_frame_deadline_ = now;
  }
}

int DesktopApp::run() {
  if (!init_window()) {
    return 3;
  }
  if (!options_.rom_path.empty()) {
    if (!load_rom_and_saves()) {
      return 3;
    }
  } else {
    std::cerr << "gba-desktop: no ROM specified; opening file dialog\n";
  }
  if (!options_.initial_state_path.empty() &&
      !read_save_state_path(options_.initial_state_path, "save state")) {
    return 3;
  }
  init_audio();
  if (!session_loaded_) {
    open_rom_dialog();
  }

  std::uint64_t perf_frames = 0;
  const auto perf_start = Clock::now();
  auto perf_mark = perf_start;
  FrameIntervalStats frame_intervals;
  std::optional<Clock::time_point> previous_present;

  while (running_) {
    collect_save_state(false);
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handle_event(event);
    }
    if (!running_) {
      break;
    }
    poll_rom_dialog();
    if (!running_) {
      break;
    }
    if (quit_requested_) {
      quit_requested_ = false;
      if (!session_loaded_ || flush_cartridge_save(true)) {
        break;
      }
      if (discard_requested_) {
        return 3;
      }
      const SDL_MessageBoxButtonData buttons[] = {
          {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
           0, "Keep playing"},
          {0, 1, "Quit without saving"},
      };
      const SDL_MessageBoxData warning = {
          SDL_MESSAGEBOX_WARNING, window_, "Game progress could not be saved",
          "Keep playing to retry the save. Quitting now will lose unsaved game progress.",
          2, buttons, nullptr,
      };
      int choice = 0;
      if (SDL_ShowMessageBox(&warning, &choice)) {
        if (choice == 1) {
          return 3;
        }
      } else {
        confirm_unsaved_quit_ = true;
        SDL_SetWindowTitle(window_, "gba-desktop - Save failed (Esc: Retry, Shift+Esc: Quit without saving)");
        std::cerr << "gba-desktop: cannot show save warning: " << SDL_GetError()
                  << ". Press Esc to retry, or Shift+Esc to quit without saving.\n";
      }
      next_frame_deadline_ = Clock::now();
    }

    if (!session_loaded_ || rom_dialog_) {
      previous_present.reset();
      // Waiting for the user to pick (or cancel) a ROM in the file dialog.
      SDL_Delay(10);
      continue;
    }
    pump_input();

    const bool fast_forward = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] != 0;
    if (paused_) {
      previous_present.reset();
      SDL_Delay(10);
      continue;
    }

    const auto result = runtime_.step_frame(kMaxStepsPerFrame);
    if (result.status != gba::core::EmulatorRuntimeStatus::ok) {
      std::cerr << "gba-desktop: engine step failed with status "
                << static_cast<int>(result.status) << "\n";
      // An engine abort must not discard in-game progress.
      flush_cartridge_save(true);
      return 3;
    }
    if (!result.frame_complete) {
      std::cerr << "gba-desktop: engine stopped ("
                << (result.run.stop_reason ==
                            gba::core::CoreRunStopReason::unsupported_instruction
                        ? "unsupported instruction"
                        : "fetch failure")
                << ") at PC 0x" << std::hex << result.run.final_pc << std::dec << "\n";
      flush_cartridge_save(true);
      return 3;
    }
    push_audio();
    // Present at the scheduled deadline. Waiting after presentation makes
    // each interval include the change in work duration between two frames.
    pace_frame(fast_forward);
    present_frame();
    if (options_.frame_timing) {
      const auto presented = Clock::now();
      if (previous_present) {
        frame_intervals.add(std::chrono::duration<double, std::milli>(
                                presented - *previous_present).count());
      }
      previous_present = presented;
    }
    flush_cartridge_save(false);

    if (options_.reset_after_frames != 0 &&
        stats_.frames_presented == options_.reset_after_frames) {
      reset_runtime();
    }
    if (options_.switch_after_frames != 0 && !options_.switch_to_path.empty() &&
        stats_.frames_presented == options_.switch_after_frames) {
      switch_rom(options_.switch_to_path);
    }
    if (options_.quit_after_frames != 0 &&
        stats_.frames_presented >= options_.quit_after_frames) {
      running_ = false;
    }

    ++perf_frames;
    const auto now = Clock::now();
    const double since_mark = std::chrono::duration<double>(now - perf_mark).count();
    if (since_mark >= 1.0) {
      stats_.host_fps = static_cast<double>(perf_frames) / since_mark;
      perf_frames = 0;
      perf_mark = now;
    }
  }

  const auto perf_end = Clock::now();
  if (session_loaded_ && !flush_cartridge_save(true)) {
    std::cerr << "gba-desktop: exiting with unsaved cartridge progress: "
              << cartridge_save_path_ << "\n";
    return 3;
  }
  if (options_.quit_after_frames != 0 && session_loaded_ && !capture_window_screenshot()) {
    return 3;
  }
  const double total =
      std::chrono::duration<double>(perf_end - perf_start).count();
  stats_.avg_frame_ms =
      total * 1000.0 / static_cast<double>(stats_.frames_presented ? stats_.frames_presented : 1);
  const double average_fps =
      total > 0.0 ? static_cast<double>(stats_.frames_presented) / total : 0.0;
  std::cerr << "gba-desktop: session ended (" << stats_.frames_presented << " frames, "
            << stats_.host_fps << " fps last window, " << average_fps << " fps average, "
            << stats_.audio_underruns << " audio underruns)\n";
  if (options_.frame_timing) {
    std::cerr << "gba-desktop: presentation intervals (count=" << frame_intervals.count()
              << ", p95_upper_ms=" << frame_intervals.percentile_upper_ms(95)
              << ", p99_upper_ms=" << frame_intervals.percentile_upper_ms(99)
              << ", max_ms=" << frame_intervals.maximum_ms()
              << ", over_25ms=" << frame_intervals.over_25ms() << ")\n";
  }
  return startup_dialog_failed_ ? 3 : 0;
}

}  // namespace gba::desktop
