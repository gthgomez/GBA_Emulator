#pragma once

// SDL3 reference host for the shared emulator engine. This layer owns
// windowing, input, audio presentation, and persistence only; every
// emulation decision goes through gba::core::EmulatorRuntime.

#include "gba/core/emulator_runtime.hpp"
#include "gba/core/save_state_codec.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_AudioStream;
struct SDL_Gamepad;
union SDL_Event;

#include <SDL3/SDL_scancode.h>

namespace gba::desktop {

struct HostOptions {
  std::string rom_path;
  // When empty, cartridge saves live next to the ROM as <rom>.sav.
  std::string save_directory;
  int initial_scale = 3;
  bool start_fullscreen = false;
  // Automation hooks: quit after N presented frames (CI smoke), and capture
  // the presented framebuffer via SDL_RenderReadPixels (no screen capture).
  std::uint32_t quit_after_frames = 0;
  // Automation hook: perform a full reset after N presented frames (lifecycle
  // testing of cartridge-save preservation across reset).
  std::uint32_t reset_after_frames = 0;
  // Automation hook: switch to `switch_to_path` after N presented frames
  // (lifecycle testing of the switch-time save flush and its refusal path).
  std::uint32_t switch_after_frames = 0;
  std::string switch_to_path;
  std::string window_screenshot_path;
};

struct HostStats {
  std::uint64_t frames_presented = 0;
  std::uint64_t audio_samples_consumed = 0;
  std::uint64_t audio_underruns = 0;
  std::uint32_t audio_buffer_fill = 0;  // frames, latest observation
  double host_fps = 0.0;
  double avg_frame_ms = 0.0;
  bool cartridge_save_loaded = false;
  std::string cartridge_save_kind;
};

struct KeyBinding {
  SDL_Scancode scancode;
  gba::core::KeypadButton button;
};

class DesktopApp {
 public:
  explicit DesktopApp(HostOptions options);
  ~DesktopApp();

  // Runs the interactive loop until the user quits. Returns a process exit
  // code: 0 on clean exit, 3 on engine failure (unsupported instruction /
  // fetch failure stops are surfaced in the message and artifact-friendly
  // diagnostics on stderr).
  int run();

  [[nodiscard]] const HostStats& stats() const { return stats_; }

 private:
  using Clock = std::chrono::steady_clock;

  bool init_window();
  bool init_audio();

  // Loads a ROM and its persisted cartridge save. `allow_rollback` keeps a
  // snapshot of any already-running session and restores it when the new ROM
  // is rejected, so a bad replacement never destroys the current game.
  bool load_rom_from_path(const std::string& path, bool allow_rollback);
  bool load_rom_and_saves();
  // Persists the outgoing game's cartridge save, then loads the new ROM.
  bool switch_rom(const std::string& path);

  void pump_input();
  void apply_key(SDL_Scancode scancode, bool down);
  void sync_input_mask();
  void open_gamepad(unsigned int instance_id);
  void close_gamepad(unsigned int instance_id);
  void close_all_gamepads();
  void clear_input();

  void handle_event(const union SDL_Event& event);
  void present_frame();
  bool capture_window_screenshot();
  void push_audio();
  void clear_queued_audio();
  void pace_frame(bool fast_forward);
  void toggle_fullscreen();
  // Writes the cartridge save when it is dirty (or always, when `force`).
  // Returns true when no write was needed or the write succeeded; false when
  // a required write failed (callers decide whether that is fatal).
  bool flush_cartridge_save(bool force);
  bool write_save_state(int slot);
  bool read_save_state(int slot);
  void reset_runtime();
  void open_rom_dialog();

  HostOptions options_;
  gba::core::EmulatorRuntime runtime_;
  HostStats stats_{};

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  SDL_AudioStream* audio_stream_ = nullptr;

  // Input is aggregated from independent sources so an idle gamepad can never
  // cancel a held key (and two gamepads cannot cancel one another).
  std::uint16_t keyboard_mask_ = 0;
  std::uint16_t pad_mask_ = 0;
  std::uint16_t input_mask_ = 0;
  std::vector<SDL_Gamepad*> gamepads_;

  bool paused_ = false;
  bool mute_ = false;
  bool running_ = true;
  bool session_loaded_ = false;

  std::string save_prefix_;           // <rom path> or <save dir>/<rom filename>
  std::string cartridge_save_path_;   // empty when the game has no backup save
  bool cartridge_save_enabled_ = false;
  std::vector<std::uint8_t> cartridge_save_snapshot_;
  std::uint32_t frames_since_save_flush_ = 0;
  Clock::time_point next_frame_deadline_{};
};

}  // namespace gba::desktop
