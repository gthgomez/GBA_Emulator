#pragma once

// Platform-neutral name for the runtime facade that hosts use to drive the
// emulator engine one frame at a time. The implementation class keeps its
// historical AndroidRuntime name (Android consumes it via the opaque C bridge
// and JNI), but the surface contains nothing Android-specific: framebuffer
// access, keypad input, APU sample batches, save/load state, and video
// diagnostics are all host-agnostic. Desktop and lab hosts depend on this
// header, not on the Android-facing C bridge.

#include "gba/core/android_runtime.hpp"

namespace gba::core {

using EmulatorRuntime = AndroidRuntime;

// Shared spellings for the facade's enums so host code never needs to spell
// "Android" to drive the engine.
using EmulatorRuntimeStatus = AndroidRuntimeStatus;
using EmulatorRuntimeFrameResult = AndroidRuntimeFrameResult;

}  // namespace gba::core
