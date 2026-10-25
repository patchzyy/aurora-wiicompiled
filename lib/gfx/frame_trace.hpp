#pragma once

#include <cstdint>

namespace aurora::gfx::presentation {
// Opt in with MKW_FRAME_TRACE=<csv path>. Timestamps use the presentation clock.
// Disk I/O runs on a separate thread; a full queue drops samples instead of waiting for disk.
// Event names must be string literals: they are written after the caller returns.
bool frame_trace_enabled() noexcept;
// Marks the frame the next producer timing report belongs to.
void set_trace_producer_frame(uint64_t frameId) noexcept;
// frameId == 0 inherits the last explicitly traced frame on this thread.
void trace_frame(const char* event, uint64_t frameId, int slot = -1, uint64_t durationNanos = 0,
                 uint64_t deadlineNanos = 0, uint64_t intervalNanos = 0, uint64_t detail = 0) noexcept;
// Records game_busy for the frame set since the previous report, or frame 0 if none was.
void trace_producer_timing(uint64_t wallNanos, uint64_t waitNanos) noexcept;
// Flushes and closes the trace. Idempotent; later trace calls are dropped.
void stop_frame_trace() noexcept;
} // namespace aurora::gfx::presentation
