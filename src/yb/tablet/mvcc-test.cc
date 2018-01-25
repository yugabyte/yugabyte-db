// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include <gtest/gtest.h>
#include <glog/logging.h>

#include "yb/server/logical_clock.h"
#include "yb/tablet/mvcc.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {
namespace tablet {

class MvccTest : public YBTest {
 public:
  MvccTest()
      : clock_(server::LogicalClock::CreateStartingAt(HybridTime::kInitial)),
        manager_(std::string(), clock_.get()) {
  }

 protected:
  void AdvanceClock(HybridTime time) {
    while (clock_->Now() < time) {}
  }

  server::ClockPtr clock_;
  MvccManager manager_;
};

namespace {

HybridTime AddLogical(HybridTime input, uint64_t delta) {
  HybridTime result;
  EXPECT_OK(result.FromUint64(input.ToUint64() + delta));
  return result;
}

}

TEST_F(MvccTest, Basic) {
  constexpr size_t kTotalEntries = 10;
  std::vector<HybridTime> hts(kTotalEntries);
  for (auto& ht : hts) {
    manager_.AddPending(&ht);
  }
  for (const auto& ht : hts) {
    manager_.Replicated(ht);
    ASSERT_EQ(ht, manager_.LastReplicatedHybridTime());
  }
}

TEST_F(MvccTest, SafeHybridTimeToReadAt) {
  constexpr uint64_t kLease = 10;
  constexpr uint64_t kDelta = 10;
  auto limit = AddLogical(clock_->Now(), kLease);
  AdvanceClock(AddLogical(limit, kDelta));
  ASSERT_EQ(limit, manager_.SafeTime(limit));
  HybridTime ht1 = clock_->Now();
  manager_.AddPending(&ht1);
  ASSERT_EQ(ht1.Decremented(), manager_.SafeTime());
  HybridTime ht2;
  manager_.AddPending(&ht2);
  ASSERT_EQ(ht1.Decremented(), manager_.SafeTime());
  manager_.Replicated(ht1);
  ASSERT_EQ(ht2.Decremented(), manager_.SafeTime());
  manager_.Replicated(ht2);
  auto now = clock_->Now();
  ASSERT_EQ(now, manager_.SafeTime(now));
}

TEST_F(MvccTest, Abort) {
  constexpr size_t kTotalEntries = 10;
  std::vector<HybridTime> hts(kTotalEntries);
  for (auto& ht : hts) {
    manager_.AddPending(&ht);
  }
  for (size_t i = 1; i < hts.size(); i += 2) {
    manager_.Aborted(hts[i]);
  }
  for (size_t i = 0; i < hts.size(); i += 2) {
    ASSERT_EQ(hts[i].Decremented(), manager_.SafeTime());
    manager_.Replicated(hts[i]);
  }
  auto now = clock_->Now();
  ASSERT_EQ(now, manager_.SafeTime(now));
}

TEST_F(MvccTest, Random) {
  constexpr size_t kTotalOperations = 10000;
  enum class Op { kAdd, kReplicated, kAborted };

  std::map<HybridTime, size_t> queue;
  std::vector<HybridTime> alive;
  size_t counts[] = { 0, 0, 0 };

  std::vector<std::pair<Op, HybridTime>> ops;
  ops.reserve(kTotalOperations);
  for (size_t i = 0; (i < kTotalOperations) || !alive.empty(); ++i) {
    int rnd;
    if (kTotalOperations <= alive.size() + i + 1) {
      rnd = 50 + RandomUniformInt(0, 1);
    } else {
      rnd = RandomUniformInt(-50, 49) + std::min<int>(50, alive.size());
    }
    if (rnd < 50) {
      alive.push_back(clock_->Now());
      queue.emplace(alive.back(), alive.size() - 1);
      ops.emplace_back(Op::kAdd, alive.back());
      manager_.AddPending(&alive.back());
    } else {
      size_t idx;
      if (rnd & 1) {
        idx = queue.begin()->second;
        ops.emplace_back(Op::kReplicated, alive[idx]);
        manager_.Replicated(alive[idx]);
      } else {
        idx = RandomUniformInt<size_t>(0, alive.size() - 1);
        ops.emplace_back(Op::kAborted, alive[idx]);
        manager_.Aborted(alive[idx]);
      }
      queue.erase(alive[idx]);
      alive[idx] = alive.back();
      alive.pop_back();
      if (idx != alive.size()) {
        ASSERT_EQ(queue[alive[idx]], alive.size());
        queue[alive[idx]] = idx;
      }
    }
    ++counts[static_cast<size_t>(ops.back().first)];
    ops.back().second = AddLogical(ops.back().second, kTotalOperations);
    if (alive.empty()) {
      auto now = clock_->Now();
      ASSERT_EQ(now, manager_.SafeTime(now));
    } else {
      auto min = queue.begin()->first;
      ASSERT_EQ(min.Decremented(), manager_.SafeTime());
    }
  }
  LOG(INFO) << "Adds: " << counts[static_cast<size_t>(Op::kAdd)]
            << ", replicates: " << counts[static_cast<size_t>(Op::kReplicated)]
            << ", aborts: " << counts[static_cast<size_t>(Op::kAborted)];
  ASSERT_EQ((kTotalOperations + 1) / 2 * 2,
            counts[static_cast<size_t>(Op::kAdd)] +
                counts[static_cast<size_t>(Op::kReplicated)] +
                counts[static_cast<size_t>(Op::kAborted)]);
  ASSERT_EQ(counts[static_cast<size_t>(Op::kAdd)],
            counts[static_cast<size_t>(Op::kReplicated)] +
                counts[static_cast<size_t>(Op::kAborted)]);
  auto start = std::chrono::steady_clock::now();
  for (auto& op : ops) {
    switch(op.first) {
      case Op::kAdd:
        manager_.AddPending(&op.second);
        break;
      case Op::kReplicated:
        manager_.Replicated(op.second);
        break;
      case Op::kAborted:
        manager_.Aborted(op.second);
        break;
    }
  }
  auto end = std::chrono::steady_clock::now();
  LOG(INFO) << "Passed: " << yb::ToString(end - start);
}

TEST_F(MvccTest, WaitSafeTimestampToReadAt) {
  constexpr uint64_t kLease = 10;
  constexpr uint64_t kDelta = 10;
  auto limit = AddLogical(clock_->Now(), kLease);
  AdvanceClock(AddLogical(limit, kDelta));
  HybridTime ht1 = clock_->Now();
  manager_.AddPending(&ht1);
  HybridTime ht2;
  manager_.AddPending(&ht2);
  std::atomic<bool> t1_done(false);
  std::thread t1([this, ht2, &t1_done] {
    manager_.SafeTime(ht2.Decremented(), MonoTime::kMax, HybridTime::kMax);
    t1_done = true;
  });
  std::atomic<bool> t2_done(false);
  std::thread t2([this, ht2, &t2_done] {
    manager_.SafeTime(AddLogical(ht2, 1), MonoTime::kMax, HybridTime::kMax);
    t2_done = true;
  });
  std::this_thread::sleep_for(100ms);
  ASSERT_FALSE(t1_done.load());
  ASSERT_FALSE(t2_done.load());

  manager_.Replicated(ht1);
  std::this_thread::sleep_for(100ms);
  ASSERT_TRUE(t1_done.load());
  ASSERT_FALSE(t2_done.load());

  manager_.Replicated(ht2);
  std::this_thread::sleep_for(100ms);
  ASSERT_TRUE(t1_done.load());
  ASSERT_TRUE(t2_done.load());

  t1.join();
  t2.join();

  HybridTime ht3;
  manager_.AddPending(&ht3);
  ASSERT_FALSE(manager_.SafeTime(ht3, MonoTime::Now() + 100ms, HybridTime::kMax));
}

} // namespace tablet
} // namespace yb
