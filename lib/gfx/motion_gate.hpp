#pragma once

#include <cmath>

// When a transform may be blended between two frames (weight 0..1) or predicted past the current
// one (weight above 1). Prediction is held to steadier motion: it shows something that has not
// happened yet, so a sudden change overshoots instead of settling.
namespace aurora::gfx {
// Largest scale change per frame prediction follows (10%).
constexpr float kPredictableScaleRatio = 1.1f;
// cos(half of 23 degrees): the largest turn per frame prediction follows.
constexpr float kPredictableQuaternionDot = 0.98f;

// One axis of a transform's scale, or a projection's scale term. A sign change is a mirror (the
// look-back camera), which never blends.
inline bool scale_blendable(float previous, float current, float weight) noexcept {
  if (!std::isfinite(previous) || !std::isfinite(current) || previous == 0.0f || current == 0.0f ||
      (previous < 0.0f) != (current < 0.0f)) {
    return false;
  }
  if (weight <= 1.0f) {
    return true;
  }
  const float ratio = std::abs(current / previous);
  return ratio <= kPredictableScaleRatio && ratio >= 1.0f / kPredictableScaleRatio;
}

// `quaternionDot`: |dot| of the two rotations' quaternions (cos of half the turn).
inline bool rotation_blendable(float quaternionDot, float weight) noexcept {
  return weight <= 1.0f || quaternionDot >= kPredictableQuaternionDot;
}
// Matrix palettes blend entry by entry, so a turn changes the result's size: a 180 degree turn
// blended halfway collapses it and predicted past it stretches it. `minimumAxisCosine` is the
// smallest cosine between matching axes (columns) of the two matrices: blending follows turns up to
// 90 degrees, prediction up to about 23.
constexpr float kPredictableAxisCosine = 0.92f;

inline bool axes_blendable(float minimumAxisCosine, float weight) noexcept {
  if (!std::isfinite(minimumAxisCosine) || minimumAxisCosine < 0.0f) {
    return false;
  }
  return weight <= 1.0f || minimumAxisCosine >= kPredictableAxisCosine;
}
} // namespace aurora::gfx
