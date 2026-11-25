#include "present_plan.hpp"

#include <algorithm>
#include <cmath>

namespace aurora::gfx {
namespace {
// A cap within 2% of a whole multiple of the native rate (180 fps over a 59.94-60 Hz game) is
// treated as exactly that multiple, so the presents of every frame fall on one even grid.
constexpr double kSnapTolerance = 0.02;

uint64_t snapped_period(uint64_t interval, uint64_t period) noexcept {
  const double ratio = static_cast<double>(interval) / static_cast<double>(period);
  const double whole = std::round(ratio);
  if (whole >= 1.0 && std::abs(ratio - whole) <= kSnapTolerance) {
    return interval / static_cast<uint64_t>(whole);
  }
  return period;
}

uint32_t slots_for_period(PacingMode mode, uint64_t interval, uint64_t period) noexcept {
  if (mode == PacingMode::Smooth) {
    // Presents that start inside one interval, the native one included.
    const double ratio = static_cast<double>(interval) / static_cast<double>(period);
    const auto presents = static_cast<uint32_t>(std::ceil(ratio - 1e-6));
    return presents > 0 ? presents - 1 : 0;
  }
  // Predicted presents after the native one. One that would land within a quarter period of the
  // next native frame only adds a sliver of a frame.
  uint32_t slots = 0;
  while (static_cast<uint64_t>(slots + 1) * period < interval - period / 4) {
    ++slots;
  }
  return slots;
}

PresentPlan native_only() noexcept {
  PresentPlan plan;
  plan.presents[0] = {};
  plan.count = 1;
  return plan;
}
} // namespace

uint32_t slots_needed(PacingMode mode, uint64_t interval, uint64_t period) noexcept {
  if (mode == PacingMode::Off || interval == 0 || period == 0) {
    return 0;
  }
  period = snapped_period(interval, period);
  if (period >= interval) {
    return 0;
  }
  return std::min(slots_for_period(mode, interval, period), MaxPresentPlanSlots);
}

PresentPlan make_present_plan(PacingMode mode, uint64_t anchor, uint64_t interval, uint64_t period,
                              uint32_t maxSlots) noexcept {
  const uint32_t limit = std::min(maxSlots, MaxPresentPlanSlots);
  uint32_t slots = anchor == 0 ? 0 : slots_needed(mode, interval, period);
  if (slots == 0 || limit == 0) {
    return native_only();
  }
  period = snapped_period(interval, period);
  if (slots > limit) {
    // Fewer slots than the cap wants: spread the ones there are evenly over the interval.
    slots = limit;
    period = interval / (limit + 1);
  }

  PresentPlan plan;
  const auto step = [&](uint32_t presents) {
    return static_cast<float>(static_cast<double>(presents) * static_cast<double>(period) /
                              static_cast<double>(interval));
  };
  if (mode == PacingMode::Smooth) {
    // Shown game time stays linear in present time: each present moves `period` further along,
    // and the last one lands on the native frame.
    for (uint32_t j = 0; j <= slots; ++j) {
      const uint64_t time = anchor + period * j;
      plan.presents[j] = j == slots ? PlannedPresent{time, 1.0f, -1}
                                    : PlannedPresent{time, 1.0f - step(slots - j), static_cast<int32_t>(j)};
    }
  } else {
    plan.presents[0] = {anchor, 1.0f, -1};
    for (uint32_t j = 1; j <= slots; ++j) {
      plan.presents[j] = {anchor + period * j, 1.0f + step(j), static_cast<int32_t>(j - 1)};
    }
  }
  plan.count = slots + 1;
  return plan;
}

uint64_t effective_present_period(uint32_t capFps, uint64_t refreshPeriodNanos, bool vsync) noexcept {
  if (capFps == 0) {
    return refreshPeriodNanos;
  }
  const uint64_t capPeriod = 1'000'000'000ull / capFps;
  return vsync ? std::max(capPeriod, refreshPeriodNanos) : capPeriod;
}

bool should_skip_present(const PresentPlan& plan, uint32_t index, uint64_t nowNanos) noexcept {
  if (index >= plan.count) {
    return true;
  }
  if (plan.presents[index].slot < 0 || index + 1 >= plan.count) {
    return false;
  }
  const uint64_t next = plan.presents[index + 1].timeNanos;
  return next != 0 && nowNanos >= next;
}
} // namespace aurora::gfx
