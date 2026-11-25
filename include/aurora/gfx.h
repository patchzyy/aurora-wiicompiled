#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stddef.h"
#include "stdint.h"
#endif

#if !defined(NDEBUG) && !defined(AURORA_GFX_DEBUG_GROUPS)
#define AURORA_GFX_DEBUG_GROUPS
#endif

void push_debug_group(const char* label);
void pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
  uint32_t presentedFrameCount;
  uint32_t interpolatedFrameCount;
} AuroraStats;

typedef struct {
  uint64_t totalPresentCount;
  uint32_t sampleCount;
  double framesPerSecond;
  double averageFrameTimeMs;
  double p95FrameTimeMs;
  double jitterMs;
  // framesPerSecond with duplicated presentation slots scaled out: the rate of frames that
  // carried new motion.
  double effectiveFramesPerSecond;
} AuroraPresentTiming;

const AuroraStats* aurora_get_stats();
float aurora_get_fps();
void aurora_get_present_timing(AuroraPresentTiming* timing);

// Interpolation health: the per-frame fields describe the last frame, the counters accumulate
// since it was configured.
typedef struct {
  uint32_t targetFps;
  uint32_t targetSamples;
  uint32_t activeSamples;
  uint32_t candidates;
  uint32_t matchable;
  uint32_t matches;
  uint32_t eligible;
  uint32_t replaySafe;
  uint64_t framesSealed;
  uint64_t framesLowMatch;
  uint64_t framesReplayUnsafe;
  uint64_t slotReductions;
  uint64_t lateSealDrops;
} AuroraFrameInterpolationDiagnostics;

void aurora_get_frame_interpolation_diagnostics(AuroraFrameInterpolationDiagnostics* diagnostics);

// Inserted presents between consecutive 60 Hz logical frames. Guest simulation and VI timing are
// unchanged. mode: 0 off, 1 smooth (interpolate; the native frame shows last), 2 responsive (the
// native frame shows at once, then extrapolated frames follow). capFps caps the present rate; 0
// matches the display, and a non-tearing present mode never goes past the display refresh.
#define AURORA_FRAME_PACING_OFF 0u
#define AURORA_FRAME_PACING_SMOOTH 1u
#define AURORA_FRAME_PACING_RESPONSIVE 2u
void aurora_set_frame_pacing(uint32_t mode, uint32_t capFps);
void aurora_get_frame_pacing(uint32_t* mode, uint32_t* capFps);
// Present rate the pacing currently aims for; 0 when off or the display refresh is unknown.
double aurora_get_effective_present_fps();
// Older interface: a target above 60 means smooth pacing capped at that rate, anything else off.
// The getter returns the cap while pacing is on (0 for "match display" or off).
void aurora_set_frame_interpolation_fps(uint32_t targetFps);
uint32_t aurora_get_frame_interpolation_fps();

// When enabled, draws whose pipeline is still compiling are skipped instead of stalling.
void aurora_set_skip_unready_pipelines(bool enabled);
bool aurora_get_skip_unready_pipelines();
uint32_t aurora_get_queued_pipeline_count();

// Controls whether display copies bypass the vertical copy filter.
void aurora_set_disable_copy_filter(bool disabled);
bool aurora_get_disable_copy_filter();

// Guest-RAM write tracking. `generation` changes whenever guest RAM covering a host range was
// written (or returns AURORA_GUEST_WRITE_UNTRACKED); `notify` reports writes aurora made itself.
#define AURORA_GUEST_WRITE_UNTRACKED UINT64_MAX
typedef uint64_t (*AuroraGuestWriteGenerationCallback)(const void* hostPtr, size_t size);
typedef void (*AuroraGuestWriteNotifyCallback)(const void* hostPtr, size_t size);
void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                  AuroraGuestWriteNotifyCallback notify);

void aurora_enable_vsync(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
