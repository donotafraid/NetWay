// tests/util/Cxx17Compat.h
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

// C++17 版 std::latch（简化：只支持 count_down / wait）
#if defined(__cpp_lib_latch) && __cpp_lib_latch >= 201907L
#include <latch>
using std::latch;
#else
class latch {
  std::ptrdiff_t n_;
  mutable std::mutex m_;
  mutable std::condition_variable cv_;

public:
  explicit latch(std::ptrdiff_t n) : n_(n) {}
  latch(const latch &) = delete;
  latch &operator=(const latch &) = delete;
  void count_down(std::ptrdiff_t u = 1) {
    std::lock_guard lk(m_);
    if ((n_ -= u) == 0)
      cv_.notify_all();
  }
  void wait() const {
    std::unique_lock lk(m_);
    cv_.wait(lk, [&] { return n_ == 0; });
  }
  bool try_wait() const {
    std::lock_guard lk(m_);
    return n_ == 0;
  }
};
#endif

// C++17 版 std::jthread（RAII join，不实现 stop_token）
class JThread {
public:
  JThread() noexcept = default;

  template <class F, class... Args>
  explicit JThread(F &&f, Args &&...args)
      : t_(std::forward<F>(f), std::forward<Args>(args)...) {}

  ~JThread() {
    if (t_.joinable())
      t_.join();
  }

  JThread(JThread &&other) noexcept : t_(std::move(other.t_)) {}

  JThread &operator=(JThread &&other) noexcept {
    if (this != &other) {
      if (t_.joinable())
        t_.join();
      t_ = std::move(other.t_);
    }
    return *this;
  }

  JThread(const JThread &) = delete;
  JThread &operator=(const JThread &) = delete;

  bool joinable() const noexcept { return t_.joinable(); }
  void join() {
    if (t_.joinable())
      t_.join();
  }

private:
    std::thread t_;
};