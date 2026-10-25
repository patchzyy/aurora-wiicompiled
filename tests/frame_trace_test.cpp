#include "gfx/frame_trace.hpp"
#include "gfx/frame_trace_writer.hpp"

#include <aurora/aurora.h>
#include <SDL3/SDL_stdinc.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace aurora {
AuroraConfig g_config{};
} // namespace aurora

namespace {
using aurora::gfx::presentation::FrameTraceWriter;
using aurora::gfx::presentation::TraceSample;

std::filesystem::path unique_directory() {
  static std::atomic_uint64_t counter{0};
  std::random_device random;
  const auto id = std::to_string(random()) + "-" + std::to_string(random()) + "-" +
                  std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
  return std::filesystem::temp_directory_path() /
         (std::u8string(u8"aurora-trace-\u00e9\u65e5\u672c-") + std::u8string(id.begin(), id.end()));
}

std::string utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {value.begin(), value.end()};
}

std::vector<std::vector<std::string>> read_rows(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::vector<std::string>> rows;
  for (std::string line; std::getline(file, line);) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    for (std::string field; std::getline(stream, field, ',');) {
      fields.push_back(field);
    }
    rows.push_back(std::move(fields));
  }
  return rows;
}

TraceSample sample(uint64_t frame) { return {"sample", 1, frame, 2, 3, 4, 5, 6}; }

class FrameTraceTest : public testing::Test {
protected:
  void SetUp() override {
    m_directory = unique_directory();
    std::filesystem::create_directories(m_directory);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(m_directory, error);
  }

  std::filesystem::path m_directory;
};

TEST_F(FrameTraceTest, WritesToUnicodePath) {
  const auto path = m_directory / u8"trace-\u00fc.csv";
  {
    FrameTraceWriter writer(utf8(path).c_str());
    ASSERT_TRUE(writer.enabled());
    writer.record(sample(1));
    writer.record(sample(2));
    writer.stop();
    EXPECT_FALSE(writer.enabled());
  }
  const auto rows = read_rows(path);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0][0], "event");
  EXPECT_EQ(rows[1], (std::vector<std::string>{"sample", "1", "1", "6", "2", "3", "4", "5"}));
  EXPECT_EQ(rows[2][2], "2");
}

TEST_F(FrameTraceTest, OpenFailureDisablesTracing) {
  const auto path = m_directory / "missing" / "trace.csv";
  FrameTraceWriter writer(utf8(path).c_str());
  EXPECT_FALSE(writer.enabled());
  writer.record(sample(1));
  writer.stop();
  writer.stop();
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(FrameTraceTest, CanReadTraceWhileWriterIsOpen) {
  const auto path = m_directory / "live.csv";
  FrameTraceWriter writer(utf8(path).c_str());
  ASSERT_TRUE(writer.enabled());
  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.is_open());
  std::string header;
  ASSERT_TRUE(static_cast<bool>(std::getline(file, header)));
  EXPECT_EQ(header, "event,timestamp_ns,frame_id,slot,duration_ns,deadline_ns,interval_ns,detail");
}

TEST_F(FrameTraceTest, EmptyPathDisablesTracing) {
  FrameTraceWriter writer("");
  EXPECT_FALSE(writer.enabled());
  writer.record(sample(1));
}

TEST_F(FrameTraceTest, CountsDroppedSamples) {
  const auto path = m_directory / "dropped.csv";
  constexpr uint64_t Recorded = FrameTraceWriter::QueueCapacity * 3;
  {
    FrameTraceWriter writer(utf8(path).c_str());
    ASSERT_TRUE(writer.enabled());
    for (uint64_t i = 0; i < Recorded; ++i) {
      writer.record(sample(i + 1));
    }
  }
  uint64_t written = 0;
  uint64_t lost = 0;
  const auto rows = read_rows(path);
  for (size_t i = 1; i < rows.size(); ++i) {
    ASSERT_EQ(rows[i].size(), 8u);
    if (rows[i][0] == "trace_lost") {
      lost += std::stoull(rows[i][7]);
    } else {
      ++written;
    }
  }
  EXPECT_EQ(written + lost, Recorded);
}

TEST_F(FrameTraceTest, LateCallersRacingStopAreDropped) {
  const auto path = m_directory / "race.csv";
  auto writer = std::make_unique<FrameTraceWriter>(utf8(path).c_str());
  ASSERT_TRUE(writer->enabled());

  std::atomic_bool running{true};
  std::atomic_uint64_t afterStop{0};
  std::atomic_bool stopped{false};
  std::vector<std::thread> producers;
  for (int t = 0; t < 4; ++t) {
    producers.emplace_back([&] {
      while (running.load(std::memory_order_acquire)) {
        writer->record(sample(1));
        if (stopped.load(std::memory_order_acquire)) {
          afterStop.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::thread secondStop([&] { writer->stop(); });
  writer->stop();
  secondStop.join();
  stopped.store(true, std::memory_order_release);
  const auto sizeAtStop = std::filesystem::file_size(path);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (afterStop.load(std::memory_order_relaxed) < 10000 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  running.store(false, std::memory_order_release);
  for (auto& producer : producers) {
    producer.join();
  }
  writer.reset();

  EXPECT_GE(afterStop.load(std::memory_order_relaxed), 10000u);
  EXPECT_EQ(std::filesystem::file_size(path), sizeAtStop);
  const auto rows = read_rows(path);
  ASSERT_GE(rows.size(), 1u);
  for (size_t i = 1; i < rows.size(); ++i) {
    ASSERT_EQ(rows[i].size(), 8u);
  }
}

// The global trace is created once per process, so its whole lifecycle is one test.
TEST_F(FrameTraceTest, GlobalTraceLifecycle) {
  using namespace aurora::gfx::presentation;
  const auto path = m_directory / u8"global-\u65e5\u672c.csv";
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MKW_FRAME_TRACE", utf8(path).c_str(), true));
  ASSERT_TRUE(frame_trace_enabled());

  trace_frame("seal", 7);
  set_trace_producer_frame(42);
  trace_producer_timing(100, 30);
  trace_producer_timing(100, 30);
  trace_frame("inherit", 0);
  stop_frame_trace();
  EXPECT_FALSE(frame_trace_enabled());
  trace_frame("late", 9);
  trace_producer_timing(100, 30);
  stop_frame_trace();

  const auto rows = read_rows(path);
  ASSERT_EQ(rows.size(), 5u);
  EXPECT_EQ(rows[1][0], "seal");
  EXPECT_EQ(rows[2], (std::vector<std::string>{"game_busy", rows[2][1], "42", "-1", "70", "0", "100", "30"}));
  EXPECT_EQ(rows[3][0], "game_busy");
  EXPECT_EQ(rows[3][2], "0");
  EXPECT_EQ(rows[4][0], "inherit");
  EXPECT_EQ(rows[4][2], "7");
}
} // namespace
