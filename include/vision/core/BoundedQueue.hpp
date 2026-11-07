#pragma once

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace vision {

// Producer/consumer hand-off policy.
// Blocking: push waits for a free slot; no frame is ever dropped.
// Latest:   push never waits; when full, the oldest pending frame is replaced.
enum class QueueMode { Blocking, Latest };

inline const char* queueModeName(QueueMode mode) {
  return mode == QueueMode::Latest ? "latest" : "blocking";
}

// Bounded, close-aware queue. The consumer's in-flight item is never touched:
// only items still waiting in the queue can be replaced by Latest mode.
template <class T>
class BoundedQueue {
 public:
  BoundedQueue(std::size_t capacity, QueueMode mode)
      : capacity_(capacity), mode_(mode) {
    if (capacity_ == 0) {
      throw std::invalid_argument("BoundedQueue capacity must be positive");
    }
  }

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  // Returns false only when the queue is (or becomes) closed before the value
  // can be accepted. Latest mode replaces the oldest pending value instead of
  // blocking.
  bool push(T value) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (mode_ == QueueMode::Blocking) {
      not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
      if (closed_) return false;
    } else {
      if (closed_) return false;
      if (queue_.size() >= capacity_) {
        queue_.pop_front();
        ++replaced_;
      }
    }
    queue_.push_back(std::move(value));
    ++pushed_;
    if (queue_.size() > peak_) peak_ = queue_.size();
    not_empty_.notify_one();
    return true;
  }

  // Blocks until an item is available or the queue is closed. A closed queue
  // still drains: already queued items are returned before nullopt.
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    return takeLocked();
  }

  // Non-blocking variant used by tests and by callers that already know an item
  // is pending.
  std::optional<T> tryPop() {
    std::lock_guard<std::mutex> lock(mutex_);
    return takeLocked();
  }

  // Idempotent. Wakes both producers and consumers; queued items stay
  // retrievable until drained.
  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  // Returns false when close interrupts pacing; never consumes queued data.
  bool waitUntil(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    return !not_full_.wait_until(lock, deadline, [this] { return closed_; });
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  std::size_t capacity() const { return capacity_; }
  QueueMode mode() const { return mode_; }

  std::size_t pushed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pushed_;
  }
  std::size_t popped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return popped_;
  }
  std::size_t replaced() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replaced_;
  }
  // Alias kept for the latency/queue report.
  std::size_t dropped() const { return replaced(); }
  std::size_t peak() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
  }
  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

 private:
  std::optional<T> takeLocked() {
    if (queue_.empty()) return std::nullopt;
    T value = std::move(queue_.front());
    queue_.pop_front();
    ++popped_;
    not_full_.notify_one();
    return value;
  }

  const std::size_t capacity_;
  const QueueMode mode_;
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<T> queue_;
  bool closed_ = false;
  std::size_t pushed_ = 0;
  std::size_t popped_ = 0;
  std::size_t replaced_ = 0;
  std::size_t peak_ = 0;
};

}  // namespace vision
