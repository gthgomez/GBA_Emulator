#pragma once

// SDL3 reference host for the shared emulator engine. This layer owns
// windowing, input, audio presentation, and persistence only; every
// emulation decision goes through gba::core::EmulatorRuntime.

#include "gba/core/emulator_runtime.hpp"
#include "gba/core/save_state_codec.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_AudioStream;
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
  std::string window_screenshot_path;
};

struct HostStats {
  std::uint64_t frames_presented = 0;
  std::uint64_t audio_samples_consumed = 0;
  std::uint64_t audio_underruns = 0;
  std::uint32_t audio_buffer_fill = 0;  // samples, latest observation
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
  bool init_window();
  bool init_audio();
  bool load_rom_and_saves();
  void pump_input();
  void apply_key(SDL_Scancode scancode, bool down);
  void handle_event(const union SDL_Event& event);
  void present_frame();
  bool capture_window_screenshot();
  void push_audio();
  void pace_frame(bool fast_forward);
  void toggle_fullscreen();
  void save_cartridge_save(bool force);
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

  std::uint16_t input_mask_ = 0;
  bool paused_ = false;
  bool mute_ = false;
  bool running_ = true;
  std::uint32_t frames_to_run_ = 0;  // >0 while frame-stepping when paused

  std::string cartridge_save_path_;
  std::vector<std::uint8_t> cartridge_save_snapshot_;
  std::uint32_t frames_since_save_flush_ = 0;
};

}  // namespace gba::desktop
