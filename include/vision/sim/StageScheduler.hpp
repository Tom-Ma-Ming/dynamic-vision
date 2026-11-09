#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sim {

// Deterministic stage clock. Time is the simulator clock, never wall time.
// Equal timestamps run capture, then inference, then feedback, then execution.
// An inference event stays queued until its capture has completed.
enum class Stage { Capture, Inference, Feedback, Execution };

inline const char* name(Stage stage) {
  switch (stage) {
    case Stage::Capture: return "capture";
    case Stage::Inference: return "inference";
    case Stage::Feedback: return "feedback";
    case Stage::Execution: return "execution";
  }
  return "unknown";
}

struct StageEvent {
  Stage stage = Stage::Capture;
  std::int64_t time_ns = 0;
  int rank = 0;
  std::uint64_t seq = 0;
  int frame = 0;
};

class StageScheduler {
 public:
  explicit StageScheduler(std::size_t capacity) : capacity_(capacity) {
    if (!capacity_) throw std::invalid_argument("Stage queue capacity must be positive");
    queues_.resize(4);
    rejected_.fill(0);
    clocks_.fill(-1);
  }

  // rank 0 is popped before rank 1 at the same stage and time. A full queue
  // rejects the new event; nothing already queued is dropped. Negative time,
  // a stage clock moving backwards, and an inference scheduled before its
  // capture are rejected instead of being reordered later.
  bool enqueue(Stage stage, std::int64_t time_ns, int frame, int rank = 0) {
    if (time_ns < 0) throw std::invalid_argument("Stage time cannot be negative");
    if (clocks_[index(stage)] >= 0 && time_ns < clocks_[index(stage)])
      throw std::invalid_argument("Stage time moved backwards");
    if (frame >= 0 && stage == Stage::Inference) {
      const auto capture = scheduled_capture_.find(frame);
      if (capture != scheduled_capture_.end() && time_ns < capture->second)
        throw std::invalid_argument("Inference cannot precede its capture");
    }
    if (frame >= 0 && stage == Stage::Capture) {
      const auto inference = scheduled_inference_.find(frame);
      if (inference != scheduled_inference_.end() && inference->second < time_ns)
        throw std::invalid_argument("Capture cannot be later than its inference");
    }
    auto& queue = queues_[index(stage)];
    if (queue.size() >= capacity_) {
      ++rejected_[index(stage)];
      return false;
    }
    if (frame >= 0 && stage == Stage::Capture && !scheduled_capture_.count(frame) &&
        scheduled_capture_.size() >= capacity_) {
      ++rejected_[index(stage)];
      return false;
    }
    StageEvent event{stage, time_ns, rank, seq_++, frame};
    queue.push_back(event);
    if (frame >= 0 && stage == Stage::Capture) scheduled_capture_[frame] = time_ns;
    if (frame >= 0 && stage == Stage::Inference) scheduled_inference_[frame] = time_ns;
    std::stable_sort(queue.begin(), queue.end(), [](const StageEvent& a, const StageEvent& b) {
      if (a.time_ns != b.time_ns) return a.time_ns < b.time_ns;
      if (a.rank != b.rank) return a.rank < b.rank;
      return a.seq < b.seq;
    });
    return true;
  }

  std::optional<StageEvent> popDue(std::int64_t now) {
    int chosen = -1;
    for (int stage = 0; stage < 4; ++stage) {
      if (queues_[stage].empty() || queues_[stage].front().time_ns > now) continue;
      const auto& event = queues_[stage].front();
      if (event.stage == Stage::Inference && !captured(event.frame)) continue;
      if (chosen < 0 || earlier(event, queues_[chosen].front())) chosen = stage;
    }
    if (chosen < 0) return std::nullopt;
    StageEvent event = queues_[chosen].front();
    queues_[chosen].pop_front();
    if (event.stage == Stage::Capture && event.frame >= 0) {
      captured_[event.frame] = event.time_ns;
      while (captured_.size() > capacity_) captured_.erase(captured_.begin());
    }
    if (event.stage == Stage::Inference && event.frame >= 0) release(event.frame);
    clocks_[index(event.stage)] = event.time_ns;
    return event;
  }

  // Drop capture dependency metadata once its inference completes or is abandoned.
  void release(int frame) {
    scheduled_capture_.erase(frame);
    scheduled_inference_.erase(frame);
    captured_.erase(frame);
  }

  std::size_t retainedFrames() const {
    return scheduled_capture_.size() + scheduled_inference_.size() + captured_.size();
  }

  std::size_t queued(Stage stage) const { return queues_[index(stage)].size(); }
  std::uint64_t rejected(Stage stage) const { return rejected_[index(stage)]; }
  std::int64_t clock(Stage stage) const { return clocks_[index(stage)]; }

 private:
  static int index(Stage stage) { return static_cast<int>(stage); }
  bool captured(int frame) const { return frame >= 0 && captured_.count(frame) != 0; }
  static bool earlier(const StageEvent& a, const StageEvent& b) {
    if (a.time_ns != b.time_ns) return a.time_ns < b.time_ns;
    if (a.stage != b.stage) return static_cast<int>(a.stage) < static_cast<int>(b.stage);
    if (a.rank != b.rank) return a.rank < b.rank;
    return a.seq < b.seq;
  }

  std::size_t capacity_;
  std::uint64_t seq_ = 0;
  std::vector<std::deque<StageEvent>> queues_;
  std::map<int, std::int64_t> scheduled_capture_, scheduled_inference_, captured_;
  std::array<std::uint64_t, 4> rejected_{};
  std::array<std::int64_t, 4> clocks_{};
};

}  // namespace sim
