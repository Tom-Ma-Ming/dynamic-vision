#pragma once

#include <algorithm>
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

}  // namespace vision
