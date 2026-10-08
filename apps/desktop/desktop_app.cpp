#include "desktop_app.hpp"

#include <SDL3/SDL.h>

#include "desktop_persistence.hpp"
#include "save_state_guard.hpp"
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>
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

  const std::string title =
      options_.rom_path.empty()
          ? std::string("gba-desktop")
          : "gba-desktop - " +
                std::filesystem::path(options_.rom_path).filename().string();
  const int width = 240 * options_.initial_scale;
  const int height = 160 * options_.initial_scale;
  const SDL_WindowFlags flags = options_.start_fullscreen
                                    ? SDL_WINDOW_FULLSCREEN
                                    : SDL_WINDOW_RESIZABLE;
  window_ = SDL_CreateWindow(title.c_str(), width, height, flags);
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

bool DesktopApp::load_rom_from_path(const std::string& path, bool allow_rollback) {
  const std::vector<std::uint8_t> rom = persistence::read_binary_file(path);
  if (rom.empty()) {
    std::cerr << "gba-desktop: cannot read ROM: " << path << "\n";
    return false;
  }

  // Keep the running session recoverable if the incoming ROM is rejected:
  // AndroidRuntime::load_rom() resets the session before validating.
  std::optional<std::vector<std::uint8_t>> rollback;
  if (allow_rollback && session_loaded_) {
    rollback = gba::core::SaveStateCodec::encode(runtime_.session());
  }

  if (runtime_.load_rom(rom) != gba::core::EmulatorRuntimeStatus::ok) {
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

  const auto detected = runtime_.session().memory().detect_game_pak_save_type();
  stats_.cartridge_save_kind = detected ? save_kind_name(*detected) : "none";
  stats_.cartridge_save_loaded = false;
  save_prefix_ = persistence::save_data_prefix(options_.rom_path, options_.save_directory);
  cartridge_save_path_ = persistence::cartridge_save_path(save_prefix_);
  cartridge_save_enabled_ =
      detected.has_value() && *detected != gba::core::GamePakSaveType::none &&
      runtime_.session().memory().has_game_pak_save();
  cartridge_save_snapshot_.clear();
  frames_since_save_flush_ = 0;

  if (cartridge_save_enabled_) {
    if (!options_.save_directory.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(options_.save_directory, ec);
      if (ec) {
        std::cerr << "gba-desktop: warning: cannot create save directory "
                  << options_.save_directory << ": " << ec.message() << "\n";
      }
    }
    const std::vector<std::uint8_t> existing =
        persistence::read_binary_file(cartridge_save_path_);
    if (!existing.empty()) {
      if (runtime_.session().memory().import_game_pak_save(*detected, existing)) {
        stats_.cartridge_save_loaded = true;
        std::cerr << "gba-desktop: loaded cartridge save (" << stats_.cartridge_save_kind
                  << ") from " << cartridge_save_path_ << "\n";
      } else {
        // Never destroy an unreadable save: keep a recovery copy, then start
        // fresh. The original is only overwritten by the next successful flush.
        const auto backup = persistence::backup_file(cartridge_save_path_, ".rejected");
        std::cerr << "gba-desktop: cartridge save rejected (" << stats_.cartridge_save_kind
                  << ", " << existing.size() << " bytes)";
        if (backup.has_value()) {
          std::cerr << "; preserved copy at " << *backup;
        } else {
          std::cerr << "; warning: could not preserve the rejected file";
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
  clear_input();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  return true;
}

bool DesktopApp::load_rom_and_saves() {
  return load_rom_from_path(options_.rom_path, /*allow_rollback=*/false);
}

bool DesktopApp::switch_rom(const std::string& path) {
  // Persist the outgoing game's progress before any destructive step. Even if
  // the new ROM is rejected, the old save is already safe on disk.
  flush_cartridge_save(true);
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
  const std::vector<std::uint8_t> rom = persistence::read_binary_file(options_.rom_path);
  if (rom.empty()) {
    std::cerr << "gba-desktop: reset failed: cannot read ROM " << options_.rom_path << "\n";
    return;
  }
  // A console reset clears work RAM but leaves cartridge-backed save memory
  // intact, so capture it before reloading and re-import it afterwards. This
  // is distinct from load-state, which restores a full snapshot.
  const gba::core::GamePakSaveType save_type =
      runtime_.session().memory().game_pak_save_type();
  const std::vector<std::uint8_t> cartridge_save =
      runtime_.session().memory().export_game_pak_save();
  const std::vector<std::uint8_t> rollback =
      gba::core::SaveStateCodec::encode(runtime_.session());

  if (runtime_.load_rom(rom) != gba::core::EmulatorRuntimeStatus::ok) {
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
      preserved = false;
      std::cerr << "gba-desktop: warning: could not restore cartridge save across reset;"
                   " disabling save writes to protect the on-disk save\n";
    }
  }
  cartridge_save_snapshot_ = cartridge_save;
  frames_since_save_flush_ = 0;
  // Re-apply the physical input masks to the freshly reset session so a key
  // held across F9 keeps working (the OS will not resend a filtered KEY_DOWN).
  sync_input_mask();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  std::cerr << "gba-desktop: runtime reset ("
            << (preserved ? "cartridge save preserved" : "cartridge save NOT preserved")
            << ")\n";
}

void DesktopApp::flush_cartridge_save(bool force) {
  if (!cartridge_save_enabled_ || cartridge_save_path_.empty()) {
    return;
  }
  const std::vector<std::uint8_t> current =
      runtime_.session().memory().export_game_pak_save();
  ++frames_since_save_flush_;
  if (!force && current == cartridge_save_snapshot_) {
    return;
  }
  if (!force && frames_since_save_flush_ < kSnapshotFlushFrames) {
    return;  // flush at most ~every 5 seconds of dirty frames
  }
  if (persistence::write_file_atomic(cartridge_save_path_, current)) {
    cartridge_save_snapshot_ = current;
    frames_since_save_flush_ = 0;
  } else {
    // Back off one full flush interval before retrying so a persistent write
    // failure (e.g. a read-only directory) does not spam stderr every frame.
    frames_since_save_flush_ = 0;
    std::cerr << "gba-desktop: failed to write cartridge save: " << cartridge_save_path_
              << "\n";
  }
}

bool DesktopApp::write_save_state(int slot) {
  if (save_prefix_.empty()) {
    std::cerr << "gba-desktop: no ROM loaded; cannot write save state\n";
    return false;
  }
  const std::string path = persistence::save_state_path(save_prefix_, slot);
  const auto blob = gba::core::SaveStateCodec::encode(runtime_.session());
  if (!persistence::write_file_atomic(path, blob)) {
    std::cerr << "gba-desktop: failed to write save state: " << path << "\n";
    return false;
  }
  std::cerr << "gba-desktop: save state " << slot << " written to " << path << "\n";
  return true;
}

bool DesktopApp::read_save_state(int slot) {
  if (save_prefix_.empty()) {
    std::cerr << "gba-desktop: no ROM loaded; cannot load save state\n";
    return false;
  }
  const std::string path = persistence::save_state_path(save_prefix_, slot);
  const auto blob = persistence::read_binary_file(path);
  if (blob.empty()) {
    std::cerr << "gba-desktop: no save state " << slot << " at " << path << "\n";
    return false;
  }
  // The core codec restores whatever ROM its blob carries, so the host also
  // requires the state to belong to the currently loaded ROM. On any refusal
  // the live session is left untouched.
  gba::core::CoreSession scratch;
  const SaveStateLoadStatus status =
      load_save_state_for_session(runtime_.session(), blob, scratch);
  if (status == SaveStateLoadStatus::decode_failed) {
    std::cerr << "gba-desktop: save state " << slot
              << " failed to load (corrupt, unsupported, or mismatched)\n";
    return false;
  }
  if (status == SaveStateLoadStatus::wrong_rom) {
    std::cerr << "gba-desktop: save state " << slot
              << " belongs to a different ROM; refusing to load\n";
    return false;
  }
  // The restored machine owns the cartridge save again; resync the dirty
  // snapshot so a later flush compares against the restored contents.
  cartridge_save_snapshot_ = runtime_.session().memory().export_game_pak_save();
  frames_since_save_flush_ = 0;
  // Keep held buttons working against the restored session.
  sync_input_mask();
  clear_queued_audio();
  next_frame_deadline_ = Clock::now();
  std::cerr << "gba-desktop: save state " << slot << " loaded\n";
  return true;
}

void DesktopApp::open_rom_dialog() {
  SDL_DialogFileFilter filters[] = {{const_cast<char*>("GBA ROMs"),
                                     const_cast<char*>("gba")}};
  SDL_ShowOpenFileDialog(
      [](void* userdata, const char* const* filelist, int) {
        auto* self = static_cast<DesktopApp*>(userdata);
        if (filelist == nullptr) {
          // Dialog cancelled or failed. If no game has ever loaded, quit
          // rather than spin on an empty window.
          if (!self->session_loaded_) {
            self->running_ = false;
          }
          return;
        }
        if (filelist[0] != nullptr) {
          self->switch_rom(filelist[0]);
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
          case SDL_SCANCODE_P:
          case SDL_SCANCODE_SPACE:
            paused_ = !paused_;
            if (paused_) {
              clear_queued_audio();
            } else {
              next_frame_deadline_ = Clock::now();
            }
            break;
          case SDL_SCANCODE_F5: write_save_state(1); break;
          case SDL_SCANCODE_F8: read_save_state(1); break;
          case SDL_SCANCODE_F9: reset_runtime(); break;
          case SDL_SCANCODE_F11: toggle_fullscreen(); break;
          case SDL_SCANCODE_M:
            mute_ = !mute_;
            clear_queued_audio();
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
      running_ = false;
      break;
    case SDL_EVENT_GAMEPAD_ADDED:
      open_gamepad(static_cast<unsigned int>(event.gdevice.which));
      break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      close_gamepad(static_cast<unsigned int>(event.gdevice.which));
      break;
    case SDL_EVENT_DROP_FILE:
      if (event.drop.data != nullptr) {
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
    std::this_thread::sleep_for(next_frame_deadline_ - now);
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
  init_audio();
  if (!session_loaded_) {
    open_rom_dialog();
  }

  std::uint64_t perf_frames = 0;
  const auto perf_start = Clock::now();
  auto perf_mark = perf_start;

  while (running_) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      handle_event(event);
    }

    if (!session_loaded_) {
      // Waiting for the user to pick (or cancel) a ROM in the file dialog.
      SDL_Delay(10);
      continue;
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
    present_frame();
    flush_cartridge_save(false);
    pace_frame(fast_forward);

    if (options_.reset_after_frames != 0 &&
        stats_.frames_presented == options_.reset_after_frames) {
      reset_runtime();
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

  if (session_loaded_) {
    flush_cartridge_save(true);
  }
  if (options_.quit_after_frames != 0 && session_loaded_ && !capture_window_screenshot()) {
    return 3;
  }
  const double total =
      std::chrono::duration<double>(Clock::now() - perf_start).count();
  stats_.avg_frame_ms =
      total * 1000.0 / static_cast<double>(stats_.frames_presented ? stats_.frames_presented : 1);
  std::cerr << "gba-desktop: session ended (" << stats_.frames_presented << " frames, "
            << stats_.host_fps << " fps last window, " << stats_.audio_underruns
            << " audio underruns)\n";
  return 0;
}

}  // namespace gba::desktop
