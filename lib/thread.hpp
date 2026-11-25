#pragma once

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

// Toolchains without std::jthread (notably FreeBSD's base libc++, which has
// <stop_token> but not the full C++20 thread support) get a minimal substitute
// below. Prefer the standard types when available.
#if defined(__has_include)
#  if __has_include(<version>)
#    include <version>
#  elif __has_include(<stop_token>)
#    include <stop_token>
#  endif
#endif

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#  include <stop_token>
#  define AURORA_THREAD_HAS_STD_JTHREAD 1
#endif

#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
#include <atomic>
#include <cstdint>
#include <forward_list>
#include <memory>
#endif

namespace aurora::thread {

enum class Priority { Low, Normal, High };
enum class Affinity { None, SharedCache };

struct Options {
  std::string name;
  Priority priority = Priority::Normal;
  Affinity affinity = Affinity::None;
};

// Applies thread options to the current thread
void set_current(const Options& options) noexcept;

// libc++ on Windows truncates sub-millisecond waits to zero, so its timed condvar waits spin.
void wait_once(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
               std::chrono::nanoseconds timeout) noexcept;

template <typename Rep, typename Period, typename Pred>
bool wait_for(std::condition_variable& cv, std::unique_lock<std::mutex>& lock,
              std::chrono::duration<Rep, Period> timeout, Pred pred) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  while (!pred()) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      return pred();
    }
    wait_once(cv, lock, remaining);
  }
  return true;
}

#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
struct StopState {
  std::atomic_bool requested{false};
  std::mutex mutex;
  std::uint64_t nextCallbackId{1};
  struct Callback {
    std::uint64_t id;
    std::function<void()> fn;
  };
  std::forward_list<Callback> callbacks;
};

class StopToken {
public:
  StopToken() noexcept = default;
  [[nodiscard]] bool stop_possible() const noexcept { return mState != nullptr; }
  [[nodiscard]] bool stop_requested() const noexcept {
    return mState != nullptr && mState->requested.load(std::memory_order_acquire);
  }

private:
  friend class Thread;
  template <typename>
  friend class StopCallback;
  explicit StopToken(std::shared_ptr<StopState> state) noexcept : mState(std::move(state)) {}
  std::shared_ptr<StopState> mState;
};

template <typename Callback>
class StopCallback {
public:
  template <typename F>
  explicit StopCallback(StopToken token, F&& callback) {
    if (!token.mState) {
      return;
    }
    {
      std::lock_guard lock{token.mState->mutex};
      if (!token.mState->requested.load(std::memory_order_acquire)) {
        mState = std::move(token.mState);
        mId = mState->nextCallbackId++;
        mState->callbacks.push_front({mId, std::function<void()>{std::forward<F>(callback)}});
        return;
      }
    }
    // Stop already requested: run outside the mutex so the callback may
    // unregister other callbacks or destroy this token's state.
    std::invoke(std::forward<F>(callback));
  }

  StopCallback(const StopCallback&) = delete;
  StopCallback& operator=(const StopCallback&) = delete;
  StopCallback(StopCallback&& other) noexcept : mState{std::move(other.mState)}, mId{other.mId} {
    other.mId = 0;
  }
  StopCallback& operator=(StopCallback&& other) noexcept {
    if (this != &other) {
      reset();
      mState = std::move(other.mState);
      mId = other.mId;
      other.mId = 0;
    }
    return *this;
  }
  ~StopCallback() { reset(); }

  void reset() noexcept {
    if (!mState) {
      return;
    }
    std::lock_guard lock{mState->mutex};
    mState->callbacks.remove_if(
        [id = mId](const StopState::Callback& callback) { return callback.id == id; });
    mState.reset();
    mId = 0;
  }

private:
  std::shared_ptr<StopState> mState;
  std::uint64_t mId{0};
};

// Aurora's callbacks only wake condition variables. Unlike std::stop_callback,
// reset() does not wait for a callback already running on another thread, so
// callbacks here must not capture shorter-lived state.
template <typename F>
StopCallback(StopToken, F&&) -> StopCallback<std::decay_t<F>>;
#else
using StopToken = std::stop_token;
template <typename Callback>
using StopCallback = std::stop_callback<Callback>;
#endif

class Thread {
public:
  Thread() noexcept = default;

  template <typename Function>
    requires std::invocable<std::decay_t<Function>&, StopToken>
#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
  explicit Thread(Options options, Function&& function)
      : mStopState{std::make_shared<StopState>()},
        mThread{[options = std::move(options), function = std::forward<Function>(function),
                 state = mStopState]() mutable {
          set_current(options);
          std::invoke(function, StopToken{state});
        }} {}
#else
  explicit Thread(Options options, Function&& function)
  : mThread{[options = std::move(options), function = std::forward<Function>(function)](StopToken token) mutable {
    set_current(options);
    std::invoke(function, token);
  }} {}
#endif

  Thread(Thread&&) noexcept = default;
#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
  Thread& operator=(Thread&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (mThread.joinable()) {
      request_stop();
      mThread.join();
    }
    mStopState = std::move(other.mStopState);
    mThread = std::move(other.mThread);
    return *this;
  }
#else
  Thread& operator=(Thread&&) noexcept = default;
#endif
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  ~Thread() {
#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
    // std::jthread joins on destruction; std::thread terminates if joinable.
    request_stop();
    if (mThread.joinable()) {
      mThread.join();
    }
#endif
  }

  [[nodiscard]] bool joinable() const noexcept { return mThread.joinable(); }
  [[nodiscard]] std::thread::id get_id() const noexcept { return mThread.get_id(); }
#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
  [[nodiscard]] StopToken get_stop_token() const noexcept { return StopToken{mStopState}; }
  bool request_stop() noexcept {
    if (mStopState == nullptr || mStopState->requested.exchange(true, std::memory_order_acq_rel)) {
      return false;
    }
    // Splice the callbacks out under the mutex and fire them outside it: a
    // woken worker destroys its StopCallback, which would otherwise deadlock
    // trying to unregister while the mutex is held. No allocation here.
    std::forward_list<StopState::Callback> pending;
    {
      std::lock_guard lock{mStopState->mutex};
      pending.splice_after(pending.before_begin(), mStopState->callbacks);
    }
    for (auto& callback : pending) {
      std::invoke(callback.fn);
    }
    return true;
  }
#else
  [[nodiscard]] StopToken get_stop_token() const noexcept { return mThread.get_stop_token(); }
  bool request_stop() noexcept { return mThread.request_stop(); }
#endif
  void join() { mThread.join(); }
  std::thread::native_handle_type native_handle() { return mThread.native_handle(); }

private:
#if !defined(AURORA_THREAD_HAS_STD_JTHREAD)
  std::shared_ptr<StopState> mStopState;
  std::thread mThread;
#else
  std::jthread mThread;
#endif
};

} // namespace aurora::thread
