#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace aurora::gfx::presentation {
struct TraceSample {
  const char* event;
  uint64_t at, frame, duration, deadline, interval, detail;
  int slot;
};

class FrameTraceWriter {
public:
  static constexpr size_t QueueCapacity = 8192;

  // Opens a CSV at a UTF-8 path. On failure enabled() stays false and the reason is logged.
  explicit FrameTraceWriter(const char* utf8Path) noexcept;
  ~FrameTraceWriter();
  FrameTraceWriter(const FrameTraceWriter&) = delete;
  FrameTraceWriter& operator=(const FrameTraceWriter&) = delete;

  bool enabled() const noexcept { return m_enabled.load(std::memory_order_acquire); }
  // Never allocates or blocks on I/O. Drops the sample when full or stopped.
  void record(const TraceSample& sample) noexcept;
  // Writes queued samples and closes the file. Safe to call repeatedly and concurrently with record().
  void stop() noexcept;

private:
  void write_loop() noexcept;
  void disable(const char* reason) noexcept;

  std::ofstream m_file;
  std::thread m_writer;
  std::mutex m_stopMutex;
  std::mutex m_mutex;
  std::condition_variable m_wake;
  std::vector<TraceSample> m_queue;
  std::vector<TraceSample> m_batch;
  std::atomic_bool m_enabled{false};
  std::atomic_uint64_t m_lost{0};
  bool m_stop = false;
};
} // namespace aurora::gfx::presentation
