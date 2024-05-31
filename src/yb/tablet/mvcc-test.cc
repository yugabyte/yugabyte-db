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

#include <sstream>

#include "yb/util/logging.h"

#include "yb/gutil/casts.h"

#include "yb/server/logical_clock.h"

#include "yb/tablet/mvcc.h"

#include "yb/util/atomic.h"
#include "yb/util/enums.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using namespace std::literals;
using std::vector;

using yb::server::LogicalClock;

namespace yb {
namespace tablet {

class MvccTest : public YBTest {
 public:
  MvccTest()
      : clock_(server::LogicalClock::CreateStartingAt(HybridTime::kInitial)),
        manager_(std::string(), clock_.get()) {
  }

 protected:
  void RunRandomizedTest(bool use_ht_lease);

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
  vector<HybridTime> hts(kTotalEntries);
  for (int i = 0; i != kTotalEntries; ++i) {
    hts[i] = manager_.AddLeaderPending(OpId(1, i));
  }
  for (int i = 0; i != kTotalEntries; ++i) {
    manager_.Replicated(hts[i], OpId(1, i));
    ASSERT_EQ(hts[i], manager_.LastReplicatedHybridTime());
  }
}

TEST_F(MvccTest, SafeHybridTimeToReadAt) {
  std::ostringstream mvcc_op_trace_stream;
  manager_.TEST_DumpTrace(&mvcc_op_trace_stream);
  ASSERT_STR_CONTAINS(mvcc_op_trace_stream.str(), "No MVCC operations");
  mvcc_op_trace_stream.clear();

  constexpr uint64_t kLease = 10;
  constexpr uint64_t kDelta = 10;
  auto time = AddLogical(clock_->Now(), kLease);
  FixedHybridTimeLease ht_lease {
    .time = time,
    .lease = time,
  };
  clock_->Update(AddLogical(ht_lease.lease, kDelta));
  ASSERT_EQ(ht_lease.lease, manager_.SafeTime(ht_lease));

  HybridTime ht1 = clock_->Now();
  manager_.AddFollowerPending(ht1, OpId(1, 1));
  ASSERT_EQ(ht1.Decremented(), manager_.SafeTime(FixedHybridTimeLease()));

  HybridTime ht2 = manager_.AddLeaderPending(OpId(1, 2));
  ASSERT_EQ(ht1.Decremented(), manager_.SafeTime(FixedHybridTimeLease()));

  manager_.Replicated(ht1, OpId(1, 1));
  ASSERT_EQ(ht2.Decremented(), manager_.SafeTime(FixedHybridTimeLease()));

  manager_.Replicated(ht2, OpId(1, 2));
  time = clock_->Now();
  ht_lease = {
    .time = time,
    .lease = time,
  };
  ASSERT_EQ(time, manager_.SafeTime(ht_lease));

  manager_.TEST_DumpTrace(&mvcc_op_trace_stream);
  const auto mvcc_trace = mvcc_op_trace_stream.str();
  ASSERT_STR_CONTAINS(mvcc_trace, "1. SafeTime");
  ASSERT_STR_CONTAINS(mvcc_trace, "2. AddFollowerPending");
  ASSERT_STR_CONTAINS(mvcc_trace, "8. Replicated");
  ASSERT_STR_CONTAINS(mvcc_trace, "9. SafeTime");
}

TEST_F(MvccTest, Abort) {
  constexpr size_t kTotalEntries = 10;
  vector<HybridTime> hts(kTotalEntries);
  for (int i = 0; i != kTotalEntries; ++i) {
    hts[i] = manager_.AddLeaderPending(OpId(1, i));
  }
  size_t begin = 0;
  size_t end = hts.size();
  for (size_t i = 0; i < hts.size(); ++i) {
    if (i & 1) {
      ASSERT_EQ(hts[begin].Decremented(), manager_.SafeTime(FixedHybridTimeLease()));
      manager_.Replicated(hts[begin], OpId(1, begin));
      ++begin;
    } else {
      --end;
      manager_.Aborted(hts[end], OpId(1, end));
    }
  }
  auto now = clock_->Now();
  ASSERT_EQ(now, manager_.SafeTime({
    .time = now,
    .lease = now,
  }));
}

void MvccTest::RunRandomizedTest(bool use_ht_lease) {
  constexpr size_t kTotalOperations = 20000;
  enum class OpType { kAdd, kReplicated, kAborted };

  struct Op {
    OpType type;
    HybridTime ht;
    OpId op_id;

    Op CopyAndChangeType(OpType new_type) const {
      Op result = *this;
      result.type = new_type;
      return result;
    }
  };

  std::map<HybridTime, size_t> queue;
  vector<Op> alive;
  size_t counts[] = { 0, 0, 0 };

  std::atomic<bool> stopped { false };

  const auto get_count = [&counts](OpType op) { return counts[to_underlying(op)]; };
  LogicalClock* const logical_clock = down_cast<LogicalClock*>(clock_.get());

  std::atomic<uint64_t> max_ht_lease{0};
  std::atomic<bool> is_leader{true};

  const auto ht_lease_provider =
      [use_ht_lease, logical_clock, &max_ht_lease]() -> FixedHybridTimeLease {
        if (!use_ht_lease) {
          return FixedHybridTimeLease();
        }
        auto now = logical_clock->Peek();
        auto ht_lease = now.AddMicroseconds(RandomUniformInt(0, 50));

        // Update the maximum HT lease that we gave to any caller.
        UpdateAtomicMax(&max_ht_lease, ht_lease.ToUint64());

        return {
          .time = now,
          .lease = ht_lease,
        };
      };

  // This thread will keep getting the safe time in the background.
  std::thread safetime_query_thread([this, &stopped, &ht_lease_provider, &is_leader]() {
    while (!stopped.load(std::memory_order_acquire)) {
      if (is_leader.load(std::memory_order_acquire)) {
        manager_.SafeTime(HybridTime::kMin, CoarseTimePoint::max(), ht_lease_provider());
      } else {
        manager_.SafeTimeForFollower(HybridTime::kMin, CoarseTimePoint::max());
      }
      std::this_thread::yield();
    }
  });

  auto se = ScopeExit([&stopped, &safetime_query_thread] {
    stopped = true;
    safetime_query_thread.join();
  });

  vector<Op> ops;
  ops.reserve(kTotalOperations);

  const ssize_t kTargetConcurrency = 50;

  int op_idx = 0;
  for (size_t i = 0; i < kTotalOperations || !alive.empty(); ++i) {
    ssize_t rnd;
    if (kTotalOperations - i <= alive.size()) {
      // We have (kTotalOperations - i) operations left to do, so let's finish operations that are
      // already in progress.
      rnd = kTargetConcurrency + RandomUniformInt(0, 1);
    } else {
      // If alive.size() < kTargetConcurrency, we'll be starting new operations with probability of
      // 1 - alive.size() / (2 * kTargetConcurrency), starting at almost 100% and approaching 50%
      // as alive.size() reaches kTargetConcurrency.
      //
      // If alive.size() >= kTargetConcurrency: we keep starting new operations in half of the
      // cases, and finishing existing ones in half the cases.
      rnd = RandomUniformInt(-kTargetConcurrency, kTargetConcurrency - 1) +
          std::min<ssize_t>(kTargetConcurrency, alive.size());
    }
    if (rnd < kTargetConcurrency) {
      // Start a new operation.
      OpId op_id(1, ++op_idx);
      HybridTime ht = manager_.AddLeaderPending(op_id);
      alive.push_back(Op {.type = OpType::kAdd, .ht = ht, .op_id = op_id});
      queue.emplace(alive.back().ht, alive.size() - 1);
      ops.push_back(alive.back());
    } else {
      size_t idx;
      if (rnd & 1) {
        // Finish replication for the next operation.
        idx = queue.begin()->second;
        ops.push_back(alive[idx].CopyAndChangeType(OpType::kReplicated));
        manager_.Replicated(alive[idx].ht, alive[idx].op_id);
      } else {
        // Abort the last operation that is alive.
        idx = queue.rbegin()->second;
        ops.push_back(alive[idx].CopyAndChangeType(OpType::kAborted));
        manager_.Aborted(alive[idx].ht, alive[idx].op_id);
      }
      queue.erase(alive[idx].ht);
      alive[idx] = alive.back();
      alive.pop_back();
      if (idx != alive.size()) {
        ASSERT_EQ(queue[alive[idx].ht], alive.size());
        queue[alive[idx].ht] = idx;
      }
    }
    ++counts[to_underlying(ops.back().type)];

    HybridTime safe_time;
    if (alive.empty()) {
      auto time_before = clock_->Now();
      safe_time = manager_.SafeTime(ht_lease_provider());
      auto time_after = clock_->Now();
      ASSERT_GE(safe_time.ToUint64(), time_before.ToUint64());
      ASSERT_LE(safe_time.ToUint64(), time_after.ToUint64());
    } else {
      auto min = queue.begin()->first;
      safe_time = manager_.SafeTime(ht_lease_provider());
      ASSERT_EQ(min.Decremented(), safe_time);
    }
    if (use_ht_lease) {
      ASSERT_LE(safe_time.ToUint64(), max_ht_lease.load(std::memory_order_acquire));
    }
  }
  LOG(INFO) << "Adds: " << get_count(OpType::kAdd)
            << ", replicates: " << get_count(OpType::kReplicated)
            << ", aborts: " << get_count(OpType::kAborted);
  const size_t replicated_and_aborted =
      get_count(OpType::kReplicated) + get_count(OpType::kAborted);
  ASSERT_EQ(kTotalOperations, get_count(OpType::kAdd) + replicated_and_aborted);
  ASSERT_EQ(get_count(OpType::kAdd), replicated_and_aborted);

  // Replay the recorded operations as if we are a follower receiving these operations from the
  // leader.
  is_leader = false;
  uint64_t shift = std::max(max_ht_lease + 1, clock_->Now().ToUint64() + 1);
  LOG(INFO) << "Shifting hybrid times by " << shift << " units and replaying in follower mode";
  auto start = std::chrono::steady_clock::now();
  for (auto& op : ops) {
    op.ht = HybridTime(op.ht.ToUint64() + shift);
    switch (op.type) {
      case OpType::kAdd:
        manager_.AddFollowerPending(op.ht, op.op_id);
        break;
      case OpType::kReplicated:
        manager_.Replicated(op.ht, op.op_id);
        break;
      case OpType::kAborted:
        manager_.Aborted(op.ht, op.op_id);
        break;
    }
  }
  auto end = std::chrono::steady_clock::now();
  LOG(INFO) << "Passed: " << yb::ToString(end - start);
}

TEST_F(MvccTest, RandomWithoutHTLease) {
  RunRandomizedTest(false);
}

TEST_F(MvccTest, RandomWithHTLease) {
  RunRandomizedTest(true);
}

TEST_F(MvccTest, WaitForSafeTime) {
  constexpr uint64_t kLease = 10;
  constexpr uint64_t kDelta = 10;
  auto limit = AddLogical(clock_->Now(), kLease);
  clock_->Update(AddLogical(limit, kDelta));
  HybridTime ht1 = clock_->Now();
  manager_.AddFollowerPending(ht1, OpId(1, 1));
  HybridTime ht2 = manager_.AddLeaderPending(OpId(1, 2));
  std::atomic<bool> t1_done(false);
  std::thread t1([this, ht2, &t1_done] {
    manager_.SafeTime(ht2.Decremented(), CoarseTimePoint::max(), FixedHybridTimeLease());
    t1_done = true;
  });
  std::atomic<bool> t2_done(false);
  std::thread t2([this, ht2, &t2_done] {
    manager_.SafeTime(AddLogical(ht2, 1), CoarseTimePoint::max(), FixedHybridTimeLease());
    t2_done = true;
  });
  std::this_thread::sleep_for(100ms);
  ASSERT_FALSE(t1_done.load());
  ASSERT_FALSE(t2_done.load());

  manager_.Replicated(ht1, OpId(1, 1));
  std::this_thread::sleep_for(100ms);
  ASSERT_TRUE(t1_done.load());
  ASSERT_FALSE(t2_done.load());

  manager_.Replicated(ht2, OpId(1, 2));
  std::this_thread::sleep_for(100ms);
  ASSERT_TRUE(t1_done.load());
  ASSERT_TRUE(t2_done.load());

  t1.join();
  t2.join();

  HybridTime ht3 = manager_.AddLeaderPending(OpId(1, 3));
  ASSERT_FALSE(manager_.SafeTime(ht3, CoarseMonoClock::now() + 100ms, FixedHybridTimeLease()));
}

} // namespace tablet
} // namespace yb
