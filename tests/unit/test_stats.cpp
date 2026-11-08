#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>
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

TEST(OnlineLatencyStats, EmptyNegativeAndInt64Limits) {
  vision::OnlineLatencyStats stats;
  EXPECT_EQ(stats.summary().n,0u);
  EXPECT_EQ(stats.summary().p99_ns,0);
  stats.add(-4);
  EXPECT_EQ(stats.summary().p99_ns,0);
  stats.add(std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(stats.summary().max_ns,static_cast<double>(std::numeric_limits<std::int64_t>::max()));
  EXPECT_TRUE(std::isfinite(stats.summary().p99_ns));
}

TEST(OnlineLatencyStats, MillionSamplesBoundedMemoryAndRankError) {
  static_assert(sizeof(vision::OnlineLatencyStats)<40000,"fixed telemetry memory budget");
  vision::OnlineLatencyStats stats;
  std::vector<std::int64_t> values;
  std::mt19937 random(71);
  for(int i=0;i<1000000;++i) {
    // Both fast operations and rare multi-second stalls must remain represented.
    const std::int64_t value=i%100==0 ? 4000000000LL+random() : random()%100000000;
    values.push_back(value);stats.add(value);
  }
  const auto exact=summarize(values), result=stats.summary();
  EXPECT_EQ(result.n,exact.n);
  EXPECT_EQ(result.max_ns,exact.max_ns);
  for(const auto pair : {std::pair<double,double>{result.p50_ns,exact.p50_ns},
                        {result.p95_ns,exact.p95_ns},{result.p99_ns,exact.p99_ns}}) {
    EXPECT_GE(pair.first,pair.second);
    EXPECT_LE(pair.first,pair.second*1.01+2);
  }
}
