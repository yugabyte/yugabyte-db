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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Tests for the tablet-level tiered-storage migration driver (Tablet::SetTierMigrationTarget,
// StartTierMigrationPass and AlterTabletTier). The data-movement primitive,
// DB::ScheduleDBPathMove, has its own coverage in yb/rocksdb/db/db_sst_test.cc; these tests cover
// the orchestration on top of it: one pass at a time, how each per-file outcome is accounted for,
// and shutdown mid-pass.
// Deciding that a tablet has converged (kDone) is the background reconciler's job, so every pass
// here ends kInProgress.

#include <future>
#include <unordered_set>

#include "yb/common/ql_protocol_util.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

namespace yb::tablet {

class TierMigrationTest : public YBTabletTest {
 public:
  TierMigrationTest() : YBTabletTest(GetSimpleTestSchema()) {}

  static constexpr uint32_t kHomePathId = 0;
  static constexpr uint32_t kTargetPathId = 1;
  static constexpr uint32_t kDecoyPathId = 2;

  // Deliberately does not call YBTabletTest::SetUp() (which opens the tablet immediately): the
  // tier_paths configured below must be in place before OpenRocksDB reads them at Open() time.
  void SetUp() override {
    YBTest::SetUp();
    CreateTestTablet();

    auto* meta = tablet()->metadata();
    meta->TEST_SetTierPaths({
        TierPathInfo{.path_id = kHomePathId, .tier = "ssd", .path = meta->rocksdb_dir()},
        TierPathInfo{.path_id = kTargetPathId, .tier = "hdd", .path = GetTestPath("hdd_tier")},
        TierPathInfo{.path_id = kDecoyPathId, .tier = "nvme", .path = GetTestPath("decoy_tier")},
    });

    ASSERT_OK(harness()->Open());
    writer_ = std::make_unique<LocalTabletWriter>(tablet());
  }

  void TearDown() override {
    // Sync points are a process-wide singleton; leaving state enabled/registered here would leak
    // into whichever test runs next in this binary.
    yb::SyncPoint::GetInstance()->DisableProcessing();
    yb::SyncPoint::GetInstance()->ClearAllCallBacks();
    yb::SyncPoint::GetInstance()->ClearTrace();
    YBTabletTest::TearDown();
  }

  // Writes one row and flushes it into a brand new SST, returning that SST's file number.
  uint64_t WriteFileWithKey(int32_t key) {
    QLWriteRequestPB req;
    QLAddInt32HashValue(&req, key);
    QLAddInt32ColumnValue(&req, kFirstColumnId + 1, key);
    EXPECT_OK(writer_->Write(&req));
    EXPECT_OK(tablet()->Flush(FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
    for (const auto& file : tablet()->regular_db()->GetLiveFilesMetaData()) {
      if (known_files_.insert(file.name_id).second) {
        return file.name_id;
      }
    }
    ADD_FAILURE() << "Flush did not produce a new SST for key " << key;
    return 0;
  }

  // Polls GetTierInfo() until the in-flight pass has finished (or times out).
  Result<TabletTierInfo> WaitForPassToFinish() {
    TabletTierInfo info;
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      info = VERIFY_RESULT(tablet()->GetTierInfo());
      return !info.migration.pass_in_flight;
    }, MonoDelta::FromSeconds(10), "tier migration pass to finish"));
    return info;
  }

  static uint32_t SstCountOnPath(const TabletTierInfo& info, uint32_t path_id) {
    for (const auto& stats : info.tier_paths) {
      if (stats.path_id == path_id) {
        return stats.sst_count;
      }
    }
    ADD_FAILURE() << "No tier_paths entry for path_id " << path_id;
    return 0;
  }

 protected:
  std::unique_ptr<LocalTabletWriter> writer_;
  std::unordered_set<uint64_t> known_files_;
};

// A tablet whose live SSTs are all under the target already schedules nothing; the pass is over
// before AlterTabletTier returns.
TEST_F(TierMigrationTest, ConvergedTabletSchedulesNothing) {
  WriteFileWithKey(1);

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("ssd", kHomePathId));
  ASSERT_FALSE(status.pass_in_flight);
  ASSERT_EQ(status.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(status.files_total, 0u);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "ssd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kHomePathId);

  status = ASSERT_RESULT(tablet()->StartTierMigrationPass());
  ASSERT_FALSE(status.pass_in_flight);
  ASSERT_EQ(status.files_total, 0u);
}

// Re-asserting an unchanged target must still re-point the regular DB at it. The superblock is
// what the "unchanged" check compares against, so if the DB's target_path_id ever drifted from it
// (a SetOptions failure after the superblock write, or interleaved calls), re-running the same
// AlterTabletTier is the operator's only repair.
TEST_F(TierMigrationTest, ReassertingTargetRepointsDb) {
  WriteFileWithKey(1);
  ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_RESULT(WaitForPassToFinish());

  // Drift the DB away from the persisted intent behind the tablet's back; the next flush follows
  // the DB, not the superblock.
  ASSERT_OK(tablet()->regular_db()->SetOptions(
      {{"target_path_id", std::to_string(kDecoyPathId)}}));
  WriteFileWithKey(2);
  auto info = ASSERT_RESULT(tablet()->GetTierInfo());
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);

  // Same intent as before. The pass it starts moves the stray file back, but the point of the
  // test is the flush after it: that must land on the target again.
  ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_RESULT(WaitForPassToFinish());
  WriteFileWithKey(3);
  info = ASSERT_RESULT(tablet()->GetTierInfo());
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 3u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 0u);
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 0u);
}

// A hard error keeps the tablet IN_PROGRESS and counts passes without progress; the first pass
// that makes progress resets the count.
TEST_F(TierMigrationTest, HardFailureKeepsInProgressAndCountsPasses) {
  WriteFileWithKey(1);

  // Make the target directory unusable: replace it with a regular file so the copy cannot open
  // its destination.
  const auto target_dir = GetTestPath("hdd_tier");
  ASSERT_OK(env_->DeleteDir(target_dir));
  ASSERT_OK(WriteStringToFile(env_.get(), "", target_dir));

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(info.migration.files_failed, 1u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 1u);
  ASSERT_NOK(info.migration.last_error);

  ASSERT_OK(tablet()->StartTierMigrationPass());
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.consecutive_failed_passes, 2u);

  ASSERT_OK(env_->DeleteFile(target_dir));
  ASSERT_OK(env_->CreateDir(target_dir));
  ASSERT_OK(tablet()->StartTierMigrationPass());
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 0u);
  ASSERT_OK(info.migration.last_error);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);
}

#ifndef NDEBUG
// The tests below hold a move at the DEBUG_ONLY_TEST_SYNC_POINT before its copy, or race a
// candidate away at Tablet::ScheduleTierMigrationFiles. Both compile to nothing in release builds.

// A second AlterTabletTier while a pass is in flight must not start another pass, but it is not
// an error either: it reports the pass that is already running.
TEST_F(TierMigrationTest, SecondCallWhilePassInFlightReportsProgress) {
  WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::SecondCallWhilePassInFlightReportsProgress:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto first = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_TRUE(first.pass_in_flight);
  ASSERT_EQ(first.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(first.files_total, 1u);

  // The pass's only move is blocked mid-copy, so it is still in flight.
  auto second = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_TRUE(second.pass_in_flight);
  ASSERT_EQ(second.files_total, 1u);

  // StartTierMigrationPass itself is the one that refuses.
  auto pass = tablet()->StartTierMigrationPass();
  ASSERT_NOK(pass);
  ASSERT_TRUE(pass.status().IsServiceUnavailable()) << pass.status();

  TEST_SYNC_POINT("TierMigrationTest::SecondCallWhilePassInFlightReportsProgress:Release");
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_OK(info.migration.last_error);
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);
}

// A file busy with a compaction that was already in flight cannot be moved. That is not a
// failure: it is reported as deferred, and the tablet stays IN_PROGRESS for the next pass.
TEST_F(TierMigrationTest, BusyFileIsDeferredNotFailed) {
  const uint64_t file_number = WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::BusyFileIsDeferredNotFailed:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Stands in for an in-flight compaction: claims the file (being_compacted = true) and blocks
  // mid-copy, so the pass's own attempt to move it hits Aborted.
  std::promise<Status> hold_done;
  ASSERT_OK(tablet()->regular_db()->ScheduleDBPathMove(
      file_number, kDecoyPathId, [&](const Status& s) { hold_done.set_value(s); }));

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);

  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(info.migration.files_deferred, 1u);
  ASSERT_EQ(info.migration.files_moved, 0u);
  ASSERT_EQ(info.migration.files_failed, 0u);
  ASSERT_EQ(info.migration.obsoleted, 0u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 0u);

  // The pass must not wait on the compaction it deferred to: it finished above with the holder
  // still blocked mid-copy.
  TEST_SYNC_POINT("TierMigrationTest::BusyFileIsDeferredNotFailed:Release");
  ASSERT_OK(hold_done.get_future().get());
}

// A candidate that disappears between being collected and being scheduled (e.g. a manual
// compaction rewrote it away) must not be treated as a failure.
TEST_F(TierMigrationTest, ObsoletedFileDoesNotFailMigration) {
  const uint64_t file_number = WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Right as the pass is about to schedule the candidate it collected, race it away for real (to
  // a tier the pass doesn't care about), so the pass's own scheduling attempt comes back NotFound.
  yb::SyncPoint::GetInstance()->SetCallBack("Tablet::ScheduleTierMigrationFiles", [&](void*) {
    std::promise<Status> racer_done;
    ASSERT_OK(tablet()->regular_db()->ScheduleDBPathMove(
        file_number, kDecoyPathId, [&](const Status& s) { racer_done.set_value(s); }));
    ASSERT_OK(racer_done.get_future().get());
  });

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);

  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_failed, 0u);
  ASSERT_EQ(info.migration.obsoleted, 1u);
  ASSERT_EQ(info.migration.files_moved, 0u);
  ASSERT_EQ(info.migration.files_deferred, 0u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
}

// Changing the target while a pass is in flight is accepted immediately (the intent is
// persisted), the running pass finishes toward its original target, and only the next pass
// converges the tablet on the new one.
TEST_F(TierMigrationTest, TargetChangeDuringPassAppliedByNextPass) {
  WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::TargetChangeDuringPassAppliedByNextPass:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto first = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_TRUE(first.pass_in_flight);

  auto second = ASSERT_RESULT(tablet()->AlterTabletTier("nvme", kDecoyPathId));
  ASSERT_TRUE(second.pass_in_flight);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "nvme");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kDecoyPathId);

  TEST_SYNC_POINT("TierMigrationTest::TargetChangeDuringPassAppliedByNextPass:Release");
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  // The held pass moved the file to hdd, which is no longer the target.
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);

  ASSERT_OK(tablet()->StartTierMigrationPass());
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 0u);
}

// Shutdown initiated with a move in flight must complete rather than hang or crash: RocksDB's own
// shutdown aborts queued moves, and their callbacks land in FinishTierMigrationPass while the
// tablet is draining the pending-op counter the pass's ScopedRWOperation holds.
TEST_F(TierMigrationTest, ShutdownDuringMigrationCompletesCleanly) {
  WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::ShutdownDuringMigrationCompletesCleanly:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    tablet()->StartShutdown(DisableFlushOnShutdown::kTrue, AbortOps::kTrue);
  });

  // Not required for correctness, just makes it likely the shutdown thread is genuinely blocked
  // (rather than not-yet-started) when the hold below is released.
  SleepFor(MonoDelta::FromMilliseconds(50));

  TEST_SYNC_POINT("TierMigrationTest::ShutdownDuringMigrationCompletesCleanly:Release");
  threads.JoinAll();

  tablet()->CompleteShutdown();
}

#endif  // NDEBUG

}  // namespace yb::tablet
