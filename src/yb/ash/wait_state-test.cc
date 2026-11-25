// Copyright (c) YugabyteDB, Inc.
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

#include "yb/ash/wait_state.h"

#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include "yb/common/common.pb.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

namespace yb {

using yb::ash::AshAuxInfo;
using yb::ash::AshMetadata;
using yb::ash::kMaxWaitStateTrackerNumShards;
using yb::ash::WaitStateInfo;
using yb::ash::WaitStateInfoPtr;
using yb::ash::WaitStateTracker;

class WaitStateTest : public YBTest {};

HostPort RandomHostPort() {
  return HostPort(
      Format("host-$0", RandomUniformInt<uint16_t>(0, 10)), RandomUniformInt<uint16_t>());
}

AshMetadata GenerateRandomMetadata() {
  return AshMetadata{
      .root_request_id{Uuid::Generate()},
      .top_level_node_id{Uuid::Generate()},
      .query_id = RandomUniformInt<uint64_t>(),
      .plan_id = RandomUniformInt<uint64_t>(),
      .pid = RandomUniformInt<pid_t>(),
      .database_id = RandomUniformInt<uint32_t>(),
      .user_id = RandomUniformInt<uint32_t>(),
      .rpc_request_id = RandomUniformInt<int64_t>(),
      .client_host_port = RandomHostPort()};
}

void testToAndFromPB() {
  AshMetadata meta1 = GenerateRandomMetadata();
  AshMetadataPB pb;
  meta1.ToPB(&pb);
  ASSERT_EQ(pb.root_request_id().size(), kUuidSize);
  ASSERT_EQ(pb.top_level_node_id().size(), kUuidSize);
  AshMetadata meta2 = AshMetadata::FromPB(pb);
  ASSERT_EQ(meta1.root_request_id, meta2.root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, meta2.top_level_node_id);
  ASSERT_EQ(meta1.query_id, meta2.query_id);
  ASSERT_EQ(meta1.plan_id, meta2.plan_id);
  ASSERT_EQ(meta1.pid, meta2.pid);
  ASSERT_EQ(meta1.database_id, meta2.database_id);
  ASSERT_EQ(meta1.user_id, meta2.user_id);
  ASSERT_EQ(meta1.rpc_request_id, meta2.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, meta2.client_host_port);
}

TEST(WaitStateTest, TestToAndFromPB) {
  testToAndFromPB();
}

TEST(WaitStateTest, TestUpdate) {
  AshMetadata meta1 = GenerateRandomMetadata();
  const AshMetadata meta1_copy = meta1;
  // Update 4 fields, rest unset.
  AshMetadataPB pb1;
  auto pb1_root_request_id = Uuid::Generate();
  pb1_root_request_id.ToBytes(pb1.mutable_root_request_id());
  pb1.set_query_id(RandomUniformInt<uint64_t>());
  pb1.set_plan_id(RandomUniformInt<uint64_t>());
  pb1.set_pid(RandomUniformInt<pid_t>());
  HostPortToPB(RandomHostPort(), pb1.mutable_client_host_port());
  meta1.UpdateFrom(AshMetadata::FromPB(pb1));
  ASSERT_EQ(meta1.root_request_id, pb1_root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, meta1_copy.top_level_node_id);
  ASSERT_EQ(meta1.query_id, pb1.query_id());
  ASSERT_EQ(meta1.plan_id, pb1.plan_id());
  ASSERT_EQ(meta1.pid, pb1.pid());
  ASSERT_EQ(meta1.database_id, meta1_copy.database_id);
  ASSERT_EQ(meta1.user_id, meta1_copy.user_id);
  ASSERT_EQ(meta1.rpc_request_id, meta1_copy.rpc_request_id);
  ASSERT_EQ(meta1.client_host_port, HostPortFromPB(pb1.client_host_port()));

  meta1 = meta1_copy;
  // Update 2 other fields, rest unset.
  AshMetadataPB pb2;
  auto pb2_top_level_node_id = Uuid::Generate();
  pb2_top_level_node_id.ToBytes(pb2.mutable_top_level_node_id());
  pb2.set_rpc_request_id(RandomUniformInt<int64_t>());
  meta1.UpdateFrom(AshMetadata::FromPB(pb2));
  ASSERT_EQ(meta1.root_request_id, meta1_copy.root_request_id);
  ASSERT_EQ(meta1.top_level_node_id, pb2_top_level_node_id);
  ASSERT_EQ(meta1.query_id, meta1_copy.query_id);
  ASSERT_EQ(meta1.plan_id, meta1_copy.plan_id);
  ASSERT_EQ(meta1.pid, meta1_copy.pid);
  ASSERT_EQ(meta1.user_id, meta1_copy.user_id);
  ASSERT_EQ(meta1.rpc_request_id, pb2.rpc_request_id());
  ASSERT_EQ(meta1.client_host_port, meta1_copy.client_host_port);
}

void TestTrackUntrack(WaitStateTracker* tracker) {
  ASSERT_TRUE(tracker->GetWaitStates().empty());

  // Untracking something that was never tracked, and tracking a null pointer, are both no-ops.
  auto untracked = std::make_shared<WaitStateInfo>();
  tracker->Untrack(untracked);
  tracker->Track(nullptr);
  ASSERT_TRUE(tracker->GetWaitStates().empty());

  constexpr size_t kNumEntries = 100;
  std::vector<WaitStateInfoPtr> entries;
  for (size_t i = 0; i < kNumEntries; ++i) {
    entries.push_back(std::make_shared<WaitStateInfo>());
    tracker->Track(entries.back());
  }
  ASSERT_EQ(tracker->GetWaitStates().size(), kNumEntries);

  // Tracking the same entry again should not duplicate it.
  tracker->Track(entries.front());
  ASSERT_EQ(tracker->GetWaitStates().size(), kNumEntries);

  // The snapshot should contain exactly what was tracked.
  std::unordered_set<WaitStateInfoPtr> expected(entries.begin(), entries.end());
  for (const auto& entry : tracker->GetWaitStates()) {
    ASSERT_EQ(expected.erase(entry), 1) << "Unexpected entry in snapshot";
  }
  ASSERT_TRUE(expected.empty());

  for (const auto& entry : entries) {
    tracker->Untrack(entry);
  }
  ASSERT_TRUE(tracker->GetWaitStates().empty());
}

TEST(WaitStateTest, TestTrackerTrackUntrack) {
  // A single shard degenerates to the unsharded behavior; more shards spread the entries out.
  // 10 is deliberately not a power of two, since an operator override need not be one.
  for (size_t num_shards :
       {size_t(1), size_t(4), size_t(8), size_t(10), size_t(kMaxWaitStateTrackerNumShards)}) {
    WaitStateTracker sharded_tracker(num_shards);
    TestTrackUntrack(&sharded_tracker);
  }
}

TEST(WaitStateTest, TestShardedTrackerSpreadsEntries) {
  constexpr size_t kNumShards = 8;
  constexpr size_t kNumEntries = 100 * kNumShards;

  WaitStateTracker tracker(kNumShards);
  std::vector<WaitStateInfoPtr> entries;
  for (size_t i = 0; i < kNumEntries; ++i) {
    entries.push_back(std::make_shared<WaitStateInfo>());
    tracker.Track(entries.back());
  }
  ASSERT_EQ(tracker.GetWaitStates().size(), kNumEntries);

  // Hashing the pointer should reach every shard. Using the raw address does not: make_shared
  // allocations are evenly spaced, so the low bits are nearly constant and whole shards go unused.
  ASSERT_EQ(tracker.TEST_NumNonEmptyShards(), kNumShards);
}

TEST(WaitStateTest, TestShardedTrackerConcurrency) {
  constexpr size_t kNumThreads = 8;
  constexpr size_t kNumIterations = 2000;
  WaitStateTracker tracker(4);

  // Entries that stay tracked for the duration of the test, so the concurrent GetWaitStates()
  // snapshots have something stable to observe.
  std::vector<WaitStateInfoPtr> pinned;
  for (size_t i = 0; i < kNumThreads; ++i) {
    pinned.push_back(std::make_shared<WaitStateInfo>());
    tracker.Track(pinned.back());
  }

  std::vector<std::thread> threads;
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&tracker, &pinned]() {
      for (size_t iter = 0; iter < kNumIterations; ++iter) {
        auto entry = std::make_shared<WaitStateInfo>();
        tracker.Track(entry);
        ASSERT_GE(tracker.GetWaitStates().size(), pinned.size());
        tracker.Untrack(entry);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  // Only the pinned entries should be left behind.
  ASSERT_EQ(tracker.GetWaitStates().size(), pinned.size());
  for (const auto& entry : pinned) {
    tracker.Untrack(entry);
  }
  ASSERT_TRUE(tracker.GetWaitStates().empty());
}

}  // namespace yb
