// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <chrono>
#include <cmath>
#include <random>

#include "yb/util/net/rate_limiter.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"

#include <gtest/gtest.h>

DECLARE_int32(rate_limiter_min_rate);

namespace yb {

using namespace std::chrono_literals;

constexpr uint64_t kRate = 1_KB;

namespace {
uint64_t GetDifference(uint64_t n1, uint64_t n2) {
  return n1 > n2 ? n1 - n2 : n2 - n1;
}
}

TEST(RateLimiter, TestRate) {
  for (uint64_t rate : {1_KB, 512ul, 1ul, 0ul, 48ul, 4_KB, 1_MB, 1_GB}) {
    RateLimiter rate_limiter([rate]() { return rate; });
    auto min_rate = std::max(rate, static_cast<uint64_t>(FLAGS_rate_limiter_min_rate));
    ASSERT_EQ(rate_limiter.GetMaxSizeForNextTransmission(),
              min_rate * rate_limiter.time_slot_ms_ / MonoTime::kMillisecondsPerSecond);
  }
}

TEST(RateLimiter, TestUpdateDataSize) {
  RateLimiter rate_limiter([]() { return kRate; });
  rate_limiter.Init();
  SleepFor(3s);
  auto start = MonoTime::Now();
  // This method should sleep about 1 second to make the rate equivalent to 1024 bytes/sec.
  rate_limiter.UpdateDataSizeAndMaybeSleep(4 * kRate);
  auto elapsed = MonoTime::Now().GetDeltaSince(start);
  auto time_diff_ms = std::abs(elapsed.ToMilliseconds() - MonoTime::kMillisecondsPerSecond);
  ASSERT_LE(time_diff_ms, 100);
  auto max_allowed_diff = kRate * 5 / 100;
  auto diff = GetDifference(rate_limiter.GetRate(), kRate);
  ASSERT_LE(diff, max_allowed_diff);
}

TEST(RateLimiter, TestSendRequest) {
  RateLimiter rate_limiter([]() { return kRate; });
  auto start = MonoTime::Now();
  auto status = rate_limiter.SendOrReceiveData(
      // Send or receive function.
      []() {
        SleepFor(3s);
        return Status::OK();
      },
      // Returns the total amount of data sent or received.
      []() { return 4 * kRate; });
  ASSERT_OK(status);
  auto elapsed = MonoTime::Now().GetDeltaSince(start);
  // The elapsed time should be ~4s (4 * kRate bytes trasmitted at kRate bytes/sec).
  auto time_diff_ms =
      std::abs(elapsed.ToMilliseconds() - 4 * MonoTime::kMillisecondsPerSecond);
  ASSERT_LE(time_diff_ms, 100);
  // Check that diff in rate is not more than 5%.
  auto max_allowed_diff = kRate * 5 / 100;
  auto diff = GetDifference(rate_limiter.GetRate(), kRate);
  ASSERT_LE(diff, max_allowed_diff);
}

TEST(RateLimiter, TestSendRequestWithMultipleRates) {
  vector<uint64_t> rates;
  uint64_t rates_sum = 0;

  RateLimiter rate_limiter([&rates, &rates_sum]() {
    // This is the rate updater function.
    auto nconnections = RandomUniformInt(1, 20);
    auto rate = kRate * 1_KB / nconnections;
    rates.push_back(rate);
    rates_sum += rate;
    return rate;
  });

  constexpr int kIterations = 100;
  constexpr int kIterationsPerSecond = 10;
  auto start = MonoTime::Now();
  for (int i = 0; i < kIterations; i++) {
    auto status = rate_limiter.SendOrReceiveData(
        // Send or receive function.
        []() {
          // Sleep fo 100ms.
          SleepFor(
              MonoDelta::FromMilliseconds(MonoTime::kMillisecondsPerSecond / kIterationsPerSecond));
          return Status::OK();
        },
        // Returns the total amount of data sent or received.
        [&rates]() {
          // The number of bytes sent or received is always equal to the rate / 10, which means that
          // it should take 100ms to send or receive the data. But since our function sleeps for
          // 100ms, the rate_limiter object should not sleep.
          return rates.back() / kIterationsPerSecond;
        });
    ASSERT_OK(status);
  }

  auto end = MonoTime::Now();
  auto elapsed = end.GetDeltaSince(start);

  // The total time should be ~10s
  // ((kIterations * MonoTime::kMillisecondsPerSecond / 10) milliseconds because each call to the
  // send/receive function slept for (MonoTime::kMillisecondsPerSecond / 10) ms) since rate limiter
  // should have never had additional sleeps. The only sleeps that should have happened are the ones
  // in our send/receive function.
  auto expected_time_ms = kIterations * MonoTime::kMillisecondsPerSecond / 10;
  auto time_diff_ms = std::abs(std::abs(elapsed.ToMilliseconds()) - expected_time_ms);
  ASSERT_LE(time_diff_ms, expected_time_ms * 5 / 100);

  auto expected_avg_rate = rates_sum / rates.size();
  // Check that diff in rate is not more than 5%.
  auto max_allowed_diff = expected_avg_rate * 5 / 100;

  LOG(INFO) << "Expected average rate: " << expected_avg_rate;
  LOG(INFO) << "Rate limiter rate: " << rate_limiter.GetRate();
  auto diff = GetDifference(rate_limiter.GetRate(), expected_avg_rate);
  ASSERT_LE(diff, max_allowed_diff);
}

TEST(RateLimiter, TestFastSendRequestWithMultipleRates) {
  vector<uint64_t> rates;
  uint64_t rates_sum = 0;

  RateLimiter rate_limiter([&rates, &rates_sum]() {
    auto nconnections = RandomUniformInt(1, 20);
    auto rate = kRate * 1_KB / nconnections;
    rates.push_back(rate);
    rates_sum += rate;
    return rate;
  });

  constexpr int kIterations = 100;
  constexpr int kIterationsPerSecond = 10;
  auto start = MonoTime::Now();
  for (int i = 0; i < kIterations; i++) {
    auto status = rate_limiter.SendOrReceiveData(
        // Send or receive function. This time there is no sleep, so the sleep will be called by
        // the RateLimiter object.
        []() {
          return Status::OK();
        },
        // The number of bytes sent or received is always equal to the rate / 10, which means that
        // the rate limiter object should sleep for 100ms.
        [&rates]() {
          return rates.back() / kIterationsPerSecond;
        });
    ASSERT_OK(status);
  }
  auto end = MonoTime::Now();
  auto elapsed = end.GetDeltaSince(start);

  // The total time should be ~10s
  // ((kIterations * MonoTime::kMillisecondsPerSecond / 10) milliseconds) since the RateLimiter
  // object should have slept for 100ms for every call to SendOrReceiveData.
  auto expected_time_ms = kIterations * MonoTime::kMillisecondsPerSecond / 10;
  auto time_diff_ms = std::abs(std::abs(elapsed.ToMilliseconds()) - expected_time_ms);
  // Verify that the difference in elapsed time is within 5% of the expected time.
  ASSERT_LE(time_diff_ms, expected_time_ms * 5 / 100);

  auto expected_avg_rate = rates_sum / rates.size();
  // Check that diff in rate is not more than 5%.
  auto max_allowed_diff = expected_avg_rate * 5 / 100;

  LOG(INFO) << "Expected average rate: " << expected_avg_rate;
  LOG(INFO) << "Rate limiter rate: " << rate_limiter.GetRate();
  auto diff = GetDifference(rate_limiter.GetRate(), expected_avg_rate);
  LOG(INFO) << "diff: " << diff;
  ASSERT_LE(diff, max_allowed_diff);
}

TEST(RateLimiter, TestInactiveRateLimiter) {
  RateLimiter active_rate_limiter([]() { return kRate; });
  RateLimiter inactive_rate_limiter;
  ASSERT_TRUE(active_rate_limiter.active());
  ASSERT_FALSE(inactive_rate_limiter.active());

  vector<MonoDelta> elapsed_times;
  for (auto* rate_limiter : {&active_rate_limiter, &inactive_rate_limiter}) {
    auto start = MonoTime::Now();
    for (int i = 0; i < 10; i++) {
      auto status = rate_limiter->SendOrReceiveData([]() { return Status::OK(); },
                                                    []() { return kRate * 11 / 10; });
    }
    auto end = MonoTime::Now();
    elapsed_times.emplace_back(end.GetDeltaSince(start));
    LOG(INFO) << "Elapsed time is: " << elapsed_times.back().ToMilliseconds();
  }
  ASSERT_LT(elapsed_times[1].ToMilliseconds(), elapsed_times[0].ToMilliseconds() / 100);
}

} // namespace yb
