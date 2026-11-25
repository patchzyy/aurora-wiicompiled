#include "gfx/present_plan.hpp"

#include <gtest/gtest.h>

namespace {
using aurora::gfx::effective_present_period;
using aurora::gfx::make_present_plan;
using aurora::gfx::MaxPresentPlanSlots;
using aurora::gfx::PacingMode;
using aurora::gfx::PresentPlan;
using aurora::gfx::should_skip_present;
using aurora::gfx::slots_needed;

// The runtime's VI interval for 60 Hz video modes.
constexpr uint64_t kInterval = 16'666'000;
constexpr uint64_t kAnchor = 1'000'000'000;

constexpr uint64_t period_for(uint32_t fps) { return 1'000'000'000ull / fps; }

void expect_native_only(const PresentPlan& plan) {
  ASSERT_EQ(plan.count, 1u);
  EXPECT_EQ(plan.presents[0].slot, -1);
  EXPECT_EQ(plan.presents[0].timeNanos, 0u);
  EXPECT_FLOAT_EQ(plan.presents[0].weight, 1.0f);
}

TEST(PresentPlan, SmoothAt180SpacesTwoSlotsEvenlyAndShowsNativeLast) {
  const auto plan = make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(180), 5);
  ASSERT_EQ(plan.count, 3u);
  EXPECT_EQ(plan.presents[0].timeNanos, kAnchor);
  EXPECT_EQ(plan.presents[1].timeNanos, kAnchor + kInterval / 3);
  EXPECT_EQ(plan.presents[2].timeNanos, kAnchor + kInterval * 2 / 3);
  EXPECT_NEAR(plan.presents[0].weight, 1.0f / 3.0f, 1e-4f);
  EXPECT_NEAR(plan.presents[1].weight, 2.0f / 3.0f, 1e-4f);
  EXPECT_EQ(plan.presents[0].slot, 0);
  EXPECT_EQ(plan.presents[1].slot, 1);
  EXPECT_EQ(plan.presents[2].slot, -1);
  EXPECT_FLOAT_EQ(plan.presents[2].weight, 1.0f);
}

TEST(PresentPlan, SmoothAt144KeepsShownTimeLinearInPresentTime) {
  const uint64_t period = period_for(144);
  const auto plan = make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period, 5);
  ASSERT_EQ(plan.count, 3u);
  const float step = static_cast<float>(period) / static_cast<float>(kInterval);
  EXPECT_EQ(plan.presents[1].timeNanos, kAnchor + period);
  EXPECT_EQ(plan.presents[2].timeNanos, kAnchor + period * 2);
  EXPECT_NEAR(plan.presents[0].weight, 1.0f - 2.0f * step, 1e-4f);
  EXPECT_NEAR(plan.presents[1].weight, 1.0f - step, 1e-4f);
  EXPECT_EQ(plan.presents[2].slot, -1);
}

TEST(PresentPlan, ResponsiveAt180ShowsNativeAtAnchorThenPredicts) {
  const auto plan =
      make_present_plan(PacingMode::Responsive, kAnchor, kInterval, period_for(180), 5);
  ASSERT_EQ(plan.count, 3u);
  EXPECT_EQ(plan.presents[0].slot, -1);
  EXPECT_EQ(plan.presents[0].timeNanos, kAnchor);
  EXPECT_FLOAT_EQ(plan.presents[0].weight, 1.0f);
  EXPECT_EQ(plan.presents[1].slot, 0);
  EXPECT_EQ(plan.presents[2].slot, 1);
  EXPECT_NEAR(plan.presents[1].weight, 4.0f / 3.0f, 1e-4f);
  EXPECT_NEAR(plan.presents[2].weight, 5.0f / 3.0f, 1e-4f);
  EXPECT_EQ(plan.presents[2].timeNanos, kAnchor + kInterval * 2 / 3);
}

TEST(PresentPlan, FewerAvailableSlotsWidenThePeriod) {
  const auto smooth = make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(240), 1);
  ASSERT_EQ(smooth.count, 2u);
  EXPECT_NEAR(smooth.presents[0].weight, 0.5f, 1e-4f);
  EXPECT_EQ(smooth.presents[1].timeNanos, kAnchor + kInterval / 2);
  EXPECT_EQ(smooth.presents[1].slot, -1);

  const auto responsive =
      make_present_plan(PacingMode::Responsive, kAnchor, kInterval, period_for(240), 1);
  ASSERT_EQ(responsive.count, 2u);
  EXPECT_NEAR(responsive.presents[1].weight, 1.5f, 1e-4f);
}

TEST(PresentPlan, NativeOnlyWhenNothingFitsBetweenFrames) {
  expect_native_only(make_present_plan(PacingMode::Off, kAnchor, kInterval, period_for(240), 5));
  expect_native_only(make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(60), 5));
  expect_native_only(make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(50), 5));
  expect_native_only(make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(180), 0));
  expect_native_only(make_present_plan(PacingMode::Smooth, 0, kInterval, period_for(180), 5));
  expect_native_only(make_present_plan(PacingMode::Responsive, kAnchor, 0, period_for(180), 5));
}

TEST(PresentPlan, SlotsNeededPerCap) {
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(120)), 1u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(144)), 2u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(165)), 2u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(180)), 2u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(200)), 3u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(240)), 3u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(360)), 5u);
  EXPECT_EQ(slots_needed(PacingMode::Responsive, kInterval, period_for(144)), 2u);
  EXPECT_EQ(slots_needed(PacingMode::Off, kInterval, period_for(360)), 0u);
  EXPECT_EQ(slots_needed(PacingMode::Smooth, kInterval, period_for(1000)), MaxPresentPlanSlots);
}

TEST(PresentPlan, LateFrameSkipsOverdueSlotsButNeverTheNative) {
  const auto plan = make_present_plan(PacingMode::Smooth, kAnchor, kInterval, period_for(240), 5);
  ASSERT_EQ(plan.count, 4u);
  const uint64_t muchLater = kAnchor + kInterval * 2;
  for (uint32_t i = 0; i < plan.count; ++i) {
    EXPECT_EQ(should_skip_present(plan, i, muchLater), plan.presents[i].slot != -1) << i;
  }
  // On time, nothing is skipped.
  for (uint32_t i = 0; i < plan.count; ++i) {
    EXPECT_FALSE(should_skip_present(plan, i, kAnchor)) << i;
  }
  // Responsive: the native comes first and is kept, a slot whose successor is due is dropped.
  const auto predict =
      make_present_plan(PacingMode::Responsive, kAnchor, kInterval, period_for(180), 5);
  EXPECT_FALSE(should_skip_present(predict, 0, muchLater));
  EXPECT_TRUE(should_skip_present(predict, 1, predict.presents[2].timeNanos));
  EXPECT_FALSE(should_skip_present(predict, 2, muchLater));
}
TEST(PresentPlan, EffectivePeriodFollowsCapDisplayAndVsync) {
  const uint64_t refresh144 = period_for(144);
  // 0 matches the display.
  EXPECT_EQ(effective_present_period(0, refresh144, true), refresh144);
  EXPECT_EQ(effective_present_period(0, refresh144, false), refresh144);
  // VSync cannot show more than the display refreshes; without it the cap stands.
  EXPECT_EQ(effective_present_period(240, refresh144, true), refresh144);
  EXPECT_EQ(effective_present_period(240, refresh144, false), period_for(240));
  EXPECT_EQ(effective_present_period(120, refresh144, true), period_for(120));
  // An unknown refresh leaves "match display" with nothing to aim for.
  EXPECT_EQ(effective_present_period(0, 0, true), 0u);
  EXPECT_EQ(effective_present_period(165, 0, true), period_for(165));
}
} // namespace

#include "gfx/motion_gate.hpp"

namespace {
using aurora::gfx::rotation_blendable;
using aurora::gfx::scale_blendable;

TEST(MotionGate, MirroringNeverBlends) {
  // Looking back can mirror the camera: blending through it squashes, predicting past it stretches.
  EXPECT_FALSE(scale_blendable(-1.0f, 1.0f, 0.5f));
  EXPECT_FALSE(scale_blendable(1.0f, -1.0f, 1.5f));
  EXPECT_TRUE(scale_blendable(-1.0f, -1.0f, 1.5f));
}

TEST(MotionGate, PredictionNeedsSteadyScale) {
  EXPECT_TRUE(scale_blendable(1.0f, 1.3f, 0.5f));    // interpolation may follow a zoom
  EXPECT_FALSE(scale_blendable(1.0f, 1.3f, 1.5f));   // prediction would overshoot it
  EXPECT_TRUE(scale_blendable(1.0f, 1.05f, 1.5f));
  EXPECT_FALSE(scale_blendable(1.0f, 0.0f, 0.5f));
}

TEST(MotionGate, PredictionNeedsSmallTurns) {
  // Quaternion dot = cos(half the turn). 10 degrees per frame predicts, 60 does not.
  EXPECT_TRUE(rotation_blendable(0.99619f, 1.5f));
  EXPECT_FALSE(rotation_blendable(0.86603f, 1.5f));
  EXPECT_TRUE(rotation_blendable(0.86603f, 0.5f));
}
} // namespace

namespace {
using aurora::gfx::axes_blendable;

TEST(MotionGate, PaletteMatricesNeedAlignedAxes) {
  // Smallest cosine between matching matrix axes. A 180 degree turn (cos -1) never blends; blending
  // follows up to 90 degrees, prediction only about 23.
  EXPECT_FALSE(axes_blendable(-1.0f, 0.5f));
  EXPECT_TRUE(axes_blendable(0.5f, 0.5f));
  EXPECT_FALSE(axes_blendable(0.5f, 1.5f));
  EXPECT_TRUE(axes_blendable(0.95f, 1.5f));
}
} // namespace
