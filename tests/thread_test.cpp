#include "thread.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <algorithm>
#include <mutex>
#include <bit>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace aurora::thread {
namespace {
TEST(ThreadTest, CacheAffinityKeepsCpuCapacity) {
  EXPECT_FALSE(detail::cache_affinity_has_capacity(0, 0));
  EXPECT_FALSE(detail::cache_affinity_has_capacity(4, 0));
  EXPECT_FALSE(detail::cache_affinity_has_capacity(4, 1));
  EXPECT_FALSE(detail::cache_affinity_has_capacity(4, 3));
  EXPECT_FALSE(detail::cache_affinity_has_capacity(24, 3));
  EXPECT_FALSE(detail::cache_affinity_has_capacity(4, 5));
  EXPECT_TRUE(detail::cache_affinity_has_capacity(1, 1));
  EXPECT_TRUE(detail::cache_affinity_has_capacity(2, 2));
  EXPECT_TRUE(detail::cache_affinity_has_capacity(3, 3));
  EXPECT_TRUE(detail::cache_affinity_has_capacity(4, 4));
  EXPECT_TRUE(detail::cache_affinity_has_capacity(24, 6));
}

#if defined(_WIN32)
TEST(ThreadTest, SharedCachePreservesFourAllowedCpus) {
  DWORD_PTR processMask = 0, systemMask = 0;
  ASSERT_TRUE(GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask));
  if (std::popcount(processMask) < 4) GTEST_SKIP();

  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationCache, nullptr, &bytes);
  ASSERT_GT(bytes, 0u);
  std::vector<unsigned char> buffer(bytes);
  ASSERT_TRUE(GetLogicalProcessorInformationEx(RelationCache,
      reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes));
  PROCESSOR_NUMBER current{};
  GetCurrentProcessorNumberEx(&current);
  unsigned level = 0;
  DWORD_PTR domainMask = 0;
  for (size_t offset = 0; offset < bytes;) {
    const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
    ASSERT_NE(info->Size, 0u);
    const auto& cache = info->Cache;
    if (cache.Level > level && cache.GroupMask.Group == current.Group &&
        (cache.Type == CacheUnified || cache.Type == CacheData)) {
      level = cache.Level;
      domainMask = cache.GroupMask.Mask & processMask;
    }
    offset += info->Size;
  }
  const DWORD_PTR outside = processMask & ~domainMask;
  if (!domainMask || !outside) GTEST_SKIP() << "requires more than one cache domain";
  const auto take = [](DWORD_PTR mask, unsigned count) {
    DWORD_PTR result = 0;
    while (mask && count--) {
      const auto bit = mask & (~mask + 1);
      result |= bit;
      mask &= ~bit;
    }
    return result;
  };
  const auto outsideCount = std::min(3u, static_cast<unsigned>(std::popcount(outside)));
  const auto selected = take(outside, outsideCount) | take(domainMask, 4 - outsideCount);
  ASSERT_EQ(std::popcount(selected), 4);
  std::thread worker([&] {
    ASSERT_NE(SetThreadAffinityMask(GetCurrentThread(), selected), DWORD_PTR{0});
    set_current({.name = "Cache affinity test", .affinity = Affinity::SharedCache});
    EXPECT_GE(available_parallelism(), 4u);
  });
  worker.join();
}
#endif

TEST(ThreadTest, DestructorRequestsStopAndJoins) {
  std::mutex mutex;
  std::condition_variable cv;
  bool started = false;
  bool stopped = false;

  {
    Thread worker{{
                      .name = "Aurora thread test",
                      .priority = Priority::Low,
                  },
                  [&](std::stop_token token) {
                    std::stop_callback wakeOnStop{token, [&] { cv.notify_all(); }};
                    std::unique_lock lock{mutex};
                    started = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return token.stop_requested(); });
                    stopped = true;
                  }};

    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return started; });
  }

  EXPECT_TRUE(stopped);
}
} // namespace
} // namespace aurora::thread
