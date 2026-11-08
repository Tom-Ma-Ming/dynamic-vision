#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vision {

// Latency samples are always expressed in nanoseconds on a single monotonic
// clock. Report conversion to milliseconds happens only at serialization time.
struct LatencySummary {
  double p50_ns = 0;
  double p95_ns = 0;
  double p99_ns = 0;
  double max_ns = 0;
  std::size_t n = 0;
};

inline double nsToMs(double ns) { return ns / 1e6; }

// Nearest-rank percentile: index = ceil(p * n), clamped to [1, n].
// The input is copied so callers keep their sample order.
inline LatencySummary summarize(std::vector<std::int64_t> samples_ns) {
  LatencySummary summary;
  summary.n = samples_ns.size();
  if (samples_ns.empty()) return summary;

  for (auto& sample : samples_ns) {
    if (sample < 0) sample = 0;
  }
  std::sort(samples_ns.begin(), samples_ns.end());

  const auto at = [&samples_ns](double percentile) {
    std::size_t rank = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(samples_ns.size())));
    if (rank == 0) rank = 1;
    if (rank > samples_ns.size()) rank = samples_ns.size();
    return static_cast<double>(samples_ns[rank - 1]);
  };

  summary.p50_ns = at(0.50);
  summary.p95_ns = at(0.95);
  summary.p99_ns = at(0.99);
  summary.max_ns = static_cast<double>(samples_ns.back());
  return summary;
}

// Single-writer fixed-memory histogram over the entire run. Quantiles are bin
// upper bounds (at most 1% + 2 ns above nearest rank), capped by exact maximum.
// log1p covers zero and all nonnegative int64 nanosecond values in 4600 bins.
class OnlineLatencyStats {
 public:
  void add(std::int64_t value) {
    const double sample = static_cast<double>(std::max<std::int64_t>(0, value));
    auto index = static_cast<std::size_t>(std::floor(std::log1p(sample) / logBase()));
    index = std::min(index, bins_.size()-1);
    ++bins_[index]; ++count_;
    maximum_ = std::max(maximum_, sample);
  }
  LatencySummary summary() const {
    LatencySummary result;
    result.n = count_; result.max_ns = maximum_;
    if(!count_) return result;
    result.p50_ns = quantile(.50);
    result.p95_ns = quantile(.95);
    result.p99_ns = quantile(.99);
    return result;
  }
 private:
  static double logBase() { static const double value=std::log(1.01); return value; }
  double quantile(double p) const {
    const auto rank=static_cast<std::uint64_t>(std::ceil(p*static_cast<double>(count_)));
    std::uint64_t accumulated=0;
    for(std::size_t i=0;i<bins_.size();++i) {
      accumulated+=bins_[i];
      if(accumulated>=rank)
        return std::min(maximum_, std::ceil(std::expm1((i+1)*logBase())));
    }
    return maximum_;
  }
  std::array<std::uint64_t,4600> bins_{};
  std::size_t count_=0;
  double maximum_=0;
};

}  // namespace vision
