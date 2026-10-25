#include "frame_trace.hpp"
#include "frame_trace_writer.hpp"

#include "../logging.hpp"
#include "../io.hpp"

#include <SDL3/SDL_stdinc.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <new>
#include <string_view>

namespace aurora::gfx::presentation {
namespace {
Module Log("aurora::gfx::trace");

constexpr std::string_view TraceHeader =
    "event,timestamp_ns,frame_id,slot,duration_ns,deadline_ns,interval_ns,detail\n";

thread_local uint64_t t_traceFrameId = 0;
std::atomic_uint64_t g_traceProducerFrame{0};
std::atomic<FrameTraceWriter*> g_writer{nullptr};

// Same clock as presentation::now_nanos().
uint64_t trace_now() noexcept {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

// Intentionally never freed: threads still running during std::exit may keep tracing.
FrameTraceWriter* create_writer() noexcept {
  const char* path = SDL_getenv("MKW_FRAME_TRACE");
  if (path == nullptr || *path == '\0') {
    return nullptr;
  }
  auto* writer = new (std::nothrow) FrameTraceWriter(path);
  if (writer == nullptr) {
    Log.error("Frame trace disabled: out of memory");
    return nullptr;
  }
  if (!writer->enabled()) {
    delete writer;
    return nullptr;
  }
  g_writer.store(writer, std::memory_order_release);
  if (std::atexit(stop_frame_trace) != 0) {
    Log.warn("Frame trace may lose its tail: could not register exit flush");
  }
  return writer;
}

FrameTraceWriter* writer() noexcept {
  static FrameTraceWriter* const instance = create_writer();
  return instance;
}
} // namespace

FrameTraceWriter::FrameTraceWriter(const char* utf8Path) noexcept {
  if (utf8Path == nullptr || *utf8Path == '\0') {
    return;
  }
  try {
    m_file.open(io::fs_path_from_string(utf8Path), std::ios::binary | std::ios::trunc);
    if (!m_file.is_open()) {
      Log.error("Frame trace disabled: could not open '{}'", utf8Path);
      return;
    }
    m_file.write(TraceHeader.data(), TraceHeader.size()).flush();
    if (!m_file) {
      Log.error("Frame trace disabled: could not write '{}'", utf8Path);
      m_file.close();
      return;
    }
    m_queue.reserve(QueueCapacity);
    m_batch.reserve(QueueCapacity);
    m_writer = std::thread([this] { write_loop(); });
  } catch (const std::exception& e) {
    Log.error("Frame trace disabled: could not start writer: {}", e.what());
    if (m_file.is_open()) m_file.close();
    return;
  }
  m_enabled.store(true, std::memory_order_release);
}

FrameTraceWriter::~FrameTraceWriter() { stop(); }

void FrameTraceWriter::record(const TraceSample& sample) noexcept {
  if (!enabled()) {
    return;
  }
  std::lock_guard lock(m_mutex);
  if (m_stop) {
    return;
  }
  if (m_queue.size() >= QueueCapacity) {
    m_lost.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  m_queue.push_back(sample);
}

void FrameTraceWriter::stop() noexcept {
  std::lock_guard stopLock(m_stopMutex);
  m_enabled.store(false, std::memory_order_release);
  {
    std::lock_guard lock(m_mutex);
    if (m_stop) {
      return;
    }
    m_stop = true;
  }
  m_wake.notify_one();
  if (m_writer.joinable()) {
    m_writer.join();
  }
  if (m_file.is_open()) {
    m_file.close();
    if (!m_file) Log.warn("Frame trace close failed");
  }
}

void FrameTraceWriter::disable(const char* reason) noexcept {
  m_enabled.store(false, std::memory_order_release);
  Log.error("Frame trace disabled: {}", reason);
}

void FrameTraceWriter::write_loop() noexcept {
  fmt::memory_buffer out;
  for (bool stop = false; !stop;) {
    {
      std::unique_lock lock(m_mutex);
      m_wake.wait_for(lock, std::chrono::milliseconds(100), [this] { return m_stop; });
      m_queue.swap(m_batch);
      stop = m_stop;
    }
    out.clear();
    try {
      for (const auto& s : m_batch) {
        fmt::format_to(std::back_inserter(out), "{},{},{},{},{},{},{},{}\n", s.event, s.at, s.frame, s.slot,
                       s.duration, s.deadline, s.interval, s.detail);
      }
      if (const auto lost = m_lost.exchange(0, std::memory_order_relaxed)) {
        fmt::format_to(std::back_inserter(out), "trace_lost,0,0,-1,0,0,0,{}\n", lost);
      }
    } catch (const std::exception& e) {
      disable(e.what());
      return;
    }
    m_batch.clear();
    if (out.size() != 0) {
      m_file.write(out.data(), out.size()).flush();
      if (!m_file) {
        disable("write failed");
        return;
      }
    }
  }
}

bool frame_trace_enabled() noexcept {
  const auto* trace = writer();
  return trace != nullptr && trace->enabled();
}

void set_trace_producer_frame(uint64_t frameId) noexcept {
  g_traceProducerFrame.store(frameId, std::memory_order_release);
}

void trace_frame(const char* event, uint64_t frameId, int slot, uint64_t durationNanos, uint64_t deadlineNanos,
                 uint64_t intervalNanos, uint64_t detail) noexcept {
  auto* trace = writer();
  if (trace == nullptr || !trace->enabled()) {
    return;
  }
  if (frameId != 0) {
    t_traceFrameId = frameId;
  }
  trace->record({event, trace_now(), t_traceFrameId, durationNanos, deadlineNanos, intervalNanos, detail, slot});
}

void trace_producer_timing(uint64_t wallNanos, uint64_t waitNanos) noexcept {
  auto* trace = writer();
  if (trace == nullptr || !trace->enabled()) {
    return;
  }
  const uint64_t frameId = g_traceProducerFrame.exchange(0, std::memory_order_acq_rel);
  trace->record({"game_busy", trace_now(), frameId, wallNanos > waitNanos ? wallNanos - waitNanos : 0, 0, wallNanos,
                 waitNanos, -1});
}

void stop_frame_trace() noexcept {
  if (auto* trace = g_writer.load(std::memory_order_acquire)) {
    trace->stop();
  }
}
} // namespace aurora::gfx::presentation
