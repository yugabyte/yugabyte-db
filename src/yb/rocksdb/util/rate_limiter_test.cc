//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <limits>
#include <string>
#include <gtest/gtest.h>
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/rate_limiter.h"
#include "yb/rocksdb/util/random.h"

#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

class RateLimiterTest : public RocksDBTest {};

TEST_F(RateLimiterTest, StartStop) {
  ASSERT_DEATH(
      std::unique_ptr<RateLimiter> limiter(new GenericRateLimiter(100, 100, 10)),
      "Check failed: refill_bytes_per_period_ > 0");

  std::unique_ptr<RateLimiter> limiter(new GenericRateLimiter(1000, 1000, 10));
}

TEST_F(RateLimiterTest, LargeRequests) {
  // Allow 1000 bytes per second. This gives us 1 byte every micro second, and a request of 1000
  // should take 1s.
  std::unique_ptr<RateLimiter> limiter(new GenericRateLimiter(1000, 1000, 10));

  auto now = yb::CoarseMonoClock::Now();
  limiter->Request(1000, yb::IOPriority::kHigh);
  auto duration_waited = yb::ToMilliseconds(yb::CoarseMonoClock::Now() - now);
  ASSERT_GT(duration_waited, 500);

#if defined(OS_MACOSX)
  // MacOS tests are much slower, so use a larger timeout.
  ASSERT_LT(duration_waited, 10000);
#else
  ASSERT_LT(duration_waited, 1500);
#endif

  ASSERT_EQ(limiter->GetTotalBytesThrough(), 1000);
  ASSERT_EQ(limiter->GetTotalRequests(), 1000);
}

#ifndef OS_MACOSX
TEST_F(RateLimiterTest, Rate) {
  auto* env = Env::Default();
  struct Arg {
    Arg(int32_t _target_rate, int _burst)
        : limiter(new GenericRateLimiter(_target_rate, 100 * 1000, 10)),
          request_size(_target_rate / 10),
          burst(_burst) {}
    std::unique_ptr<RateLimiter> limiter;
    int32_t request_size;
    int burst;
  };

  auto writer = [](void* p) {
    auto* thread_env = Env::Default();
    auto* arg = static_cast<Arg*>(p);
    // Test for 10 seconds
    auto until = thread_env->NowMicros() + 10 * 1000000;
    Random r((uint32_t)(thread_env->NowNanos() %
                        std::numeric_limits<uint32_t>::max()));
    while (thread_env->NowMicros() < until) {
      for (int i = 0; i < static_cast<int>(r.Skewed(arg->burst) + 1); ++i) {
        arg->limiter->Request(r.Uniform(arg->request_size - 1) + 1,
                              yb::IOPriority::kHigh);
      }
      arg->limiter->Request(r.Uniform(arg->request_size - 1) + 1, yb::IOPriority::kLow);
    }
  };

  for (int i = 1; i <= 16; i *= 2) {
    int32_t target = i * 1024 * 10;
    Arg arg(target, i / 4 + 1);
    int64_t old_total_bytes_through = 0;
    for (int iter = 1; iter <= 2; ++iter) {
      // second iteration changes the target dynamically
      if (iter == 2) {
        target *= 2;
        arg.limiter->SetBytesPerSecond(target);
      }
      auto start = env->NowMicros();
      for (int t = 0; t < i; ++t) {
        env->StartThread(writer, &arg);
      }
      env->WaitForJoin();

      auto elapsed = env->NowMicros() - start;
      double rate =
          (arg.limiter->GetTotalBytesThrough() - old_total_bytes_through) *
          1000000.0 / elapsed;
      old_total_bytes_through = arg.limiter->GetTotalBytesThrough();
      fprintf(stderr,
              "request size [1 - %" PRIi32 "], limit %" PRIi32
              " KB/sec, actual rate: %lf KB/sec, elapsed %.2lf seconds\n",
              arg.request_size - 1, target / 1024, rate / 1024,
              elapsed / 1000000.0);

      ASSERT_GE(rate / target, 0.8);
      ASSERT_LE(rate / target, 1.2);
    }
  }
}
#endif

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
