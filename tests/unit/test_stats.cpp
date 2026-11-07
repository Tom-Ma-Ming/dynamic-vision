#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "vision/core/LatencyStats.hpp"

using vision::summarize;

TEST(LatencyStats, EmptySamplesAreZero) {
  const auto summary = summarize({});
  EXPECT_EQ(summary.n, 0u);
  EXPECT_DOUBLE_EQ(summary.p50_ns, 0);
  EXPECT_DOUBLE_EQ(summary.p95_ns, 0);
  EXPECT_DOUBLE_EQ(summary.p99_ns, 0);
  EXPECT_DOUBLE_EQ(summary.max_ns, 0);
}

TEST(LatencyStats, SingleSampleUsesSameValueForEveryPercentile) {
  const auto summary = summarize({7'000'000});
  EXPECT_EQ(summary.n, 1u);
  EXPECT_DOUBLE_EQ(summary.p50_ns, 7'000'000);
  EXPECT_DOUBLE_EQ(summary.p95_ns, 7'000'000);
  EXPECT_DOUBLE_EQ(summary.p99_ns, 7'000'000);
  EXPECT_DOUBLE_EQ(summary.max_ns, 7'000'000);
}

TEST(LatencyStats, NearestRankPercentiles) {
  std::vector<std::int64_t> samples;
  for (int i = 1; i <= 100; ++i) {
    samples.push_back(static_cast<std::int64_t>(i) * 1'000'000);
  }

  const auto summary = summarize(samples);
  EXPECT_EQ(summary.n, 100u);
  EXPECT_DOUBLE_EQ(summary.p50_ns, 50'000'000);
  EXPECT_DOUBLE_EQ(summary.p95_ns, 95'000'000);
  EXPECT_DOUBLE_EQ(summary.p99_ns, 99'000'000);
  EXPECT_DOUBLE_EQ(summary.max_ns, 100'000'000);
}

TEST(LatencyStats, NegativeSamplesAreClampedToZero) {
  const auto summary = summarize({-5, 10, 20});
  EXPECT_EQ(summary.n, 3u);
  EXPECT_DOUBLE_EQ(summary.p50_ns, 10);
  EXPECT_DOUBLE_EQ(summary.max_ns, 20);
}

TEST(LatencyStats, CallerSamplesRemainUnchanged) {
  const std::vector<std::int64_t> samples{30, 10, 20};
  const auto copy = samples;
  (void)summarize(samples);
  EXPECT_EQ(samples, copy);
}
