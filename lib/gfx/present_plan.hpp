#pragma once

#include <array>
#include <cstdint>

// When each present of one native frame goes out and which game time it shows. Pure: the
// interpolator turns slot weights into staged uniforms, the render worker follows the times.
namespace aurora::gfx {
// Inserted presents per native frame. 360 fps over 60 Hz game frames needs five.
constexpr uint32_t MaxPresentPlanSlots = 5;

enum class PacingMode : uint32_t {
  Off = 0,
  // Blend between the previous and the current frame; the native frame shows last.
  Smooth = 1,
  // Show the native frame at once, then extrapolate past it.
  Responsive = 2,
};

struct PlannedPresent {
  // steady_clock nanoseconds; 0 presents as soon as the frame is encoded.
  uint64_t timeNanos = 0;
  // 0 = previous frame, 1 = current frame, above 1 extrapolates.
  float weight = 1.0f;
  // Interpolated slot index, or -1 for the native frame.
  int32_t slot = -1;
};

struct PresentPlan {
  std::array<PlannedPresent, MaxPresentPlanSlots + 1> presents{};
  uint32_t count = 0;
};

// `anchor` is when this frame's presents may start, `interval` the native frame interval and
// `period` the present period of the effective frame-rate cap, all in nanoseconds.
PresentPlan make_present_plan(PacingMode mode, uint64_t anchor, uint64_t interval, uint64_t period,
                              uint32_t maxSlots) noexcept;

// Inserted presents a frame needs for this cap, before the adaptive controller trims them.
uint32_t slots_needed(PacingMode mode, uint64_t interval, uint64_t period) noexcept;

// Present period for a frame-rate cap. `capFps` 0 matches the display; a blocking (VSync) present
// mode cannot go faster than the display refresh. 0 when there is nothing to aim for.
uint64_t effective_present_period(uint32_t capFps, uint64_t refreshPeriodNanos, bool vsync) noexcept;

// A slot that would only show once the present after it is already due adds latency without
// motion. The native present is never skipped.
bool should_skip_present(const PresentPlan& plan, uint32_t index, uint64_t nowNanos) noexcept;
} // namespace aurora::gfx
