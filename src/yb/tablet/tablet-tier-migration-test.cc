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
// Deciding that a tablet has converged (kDone) or that retrying is pointless (kFailed) is the
// background reconciler's job, exercised here through Tablet::RunTierReconcilePass; the tserver
// thread that drives it is covered in yb/tserver/ts_tablet_manager-test.cc.

#include <atomic>
#include <future>
#include <unordered_set>

#include "yb/common/ql_protocol_util.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/monotime.h"
#include "yb/util/status_format.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(enable_tiered_storage);

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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tiered_storage) = true;
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
  Result<TabletTierInfo> WaitForPassToFinish() { return WaitForPassToFinish(tablet()); }

  Result<TabletTierInfo> WaitForPassToFinish(const TabletPtr& t) {
    TabletTierInfo info;
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      info = VERIFY_RESULT(t->GetTierInfo());
      return !info.migration.pass_in_flight;
    }, MonoDelta::FromSeconds(10), "tier migration pass to finish"));
    return info;
  }

  // Creates a split child of the test tablet (hard links of its SSTs under the child's own home
  // directory) with hdd and nvme directories of its own, and opens it. Its post-split compaction
  // has not run, as on a tserver right after a split.
  Result<TabletPtr> CreateAndOpenSplitChild(const std::string& id_suffix = "child") {
    const auto split_hash_code = std::numeric_limits<docdb::DocKeyHash>::max() / 2;
    auto partition = *tablet()->metadata()->partition();
    partition.set_partition_key_end(
        dockv::PartitionSchema::EncodeMultiColumnHashValue(split_hash_code));
    dockv::KeyBytes encoded_doc_key;
    dockv::DocKeyEncoderAfterTableIdStep(&encoded_doc_key).Hash(
        split_hash_code, dockv::KeyEntryValues());
    docdb::KeyBounds key_bounds;
    key_bounds.upper = encoded_doc_key;

    const auto child_id = Format("$0-$1", tablet()->tablet_id(), id_suffix);
    auto meta = VERIFY_RESULT(tablet()->CreateSplitChildTablet(
        child_id, partition, key_bounds, OpId{}, HybridTime{}));
    // TabletTestHarness::OpenTablet reloads the superblock, so the tier paths must be persisted.
    meta->TEST_SetTierPaths({
        TierPathInfo{.path_id = kHomePathId, .tier = "ssd", .path = meta->rocksdb_dir()},
        TierPathInfo{.path_id = kTargetPathId, .tier = "hdd",
                     .path = GetTestPath("hdd_tier_" + id_suffix)},
        TierPathInfo{.path_id = kDecoyPathId, .tier = "nvme",
                     .path = GetTestPath("decoy_tier_" + id_suffix)},
    });
    RETURN_NOT_OK(meta->Flush());
    return harness()->OpenTablet(child_id);
  }

  // ScheduleDBPathMove only enqueues; the task claims the file (being_compacted) when a pool
  // worker runs it. A test whose holder must win the file has to wait for that claim before it
  // schedules the competing move, or the competitor can claim first and park at the sync point
  // meant for the holder, and the pass it belongs to never finishes.
  Status WaitForFileClaimed(uint64_t file_number) {
    return WaitFor([&]() -> Result<bool> {
      for (const auto& file : tablet()->regular_db()->GetLiveFilesMetaData()) {
        if (file.name_id == file_number) {
          return file.being_compacted;
        }
      }
      return STATUS_FORMAT(NotFound, "File $0 is not live", file_number);
    }, MonoDelta::FromSeconds(10), Format("holder to claim file $0", file_number));
  }

  // File number of the tablet's single live SST. A DB path move writes the copy under a new file
  // number, so a number captured before a move is stale afterwards.
  uint64_t OnlyLiveFileNumber() {
    const auto files = tablet()->regular_db()->GetLiveFilesMetaData();
    EXPECT_EQ(files.size(), 1u);
    return files.empty() ? 0 : files.front().name_id;
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
  // So does the bare target write, even for the same target: nothing rewrites it under a pass.
  auto write = tablet()->SetTierMigrationTarget("hdd", kTargetPathId);
  ASSERT_TRUE(write.IsServiceUnavailable()) << write;

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
  ASSERT_OK(WaitForFileClaimed(file_number));

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

// A different target while a pass is in flight is refused, and neither the superblock nor the
// DB's target_path_id changes: the moves cannot be cancelled, so the pass has to finish toward
// the target it started with, and that must stay the tablet's target until it does. (The same
// target is not an error: SecondCallWhilePassInFlightReportsProgress.) Once the pass has ended
// the change goes through.
TEST_F(TierMigrationTest, TargetChangeDuringPassIsRefused) {
  WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::TargetChangeDuringPassIsRefused:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto first = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_TRUE(first.pass_in_flight);

  auto second = tablet()->AlterTabletTier("nvme", kDecoyPathId);
  ASSERT_NOK(second);
  ASSERT_TRUE(second.status().IsServiceUnavailable()) << second.status();
  // The bare target write is refused the same way.
  auto write = tablet()->SetTierMigrationTarget("nvme", kDecoyPathId);
  ASSERT_TRUE(write.IsServiceUnavailable()) << write;
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "hdd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);
  ASSERT_TRUE(tablet()->tier_migration_status().pass_in_flight);
  // The DB still flushes to hdd, not to the refused target.
  WriteFileWithKey(2);

  TEST_SYNC_POINT("TierMigrationTest::TargetChangeDuringPassIsRefused:Release");
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 2u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 0u);

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("nvme", kDecoyPathId));
  ASSERT_EQ(status.files_total, 2u);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "nvme");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kDecoyPathId);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 2u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 2u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 0u);
}

// Two requests with different targets, neither finding a pass in flight: the second's write lands
// after the first's, before the first starts its pass, and the second's pass claims the tablet.
// The first must not report that pass as its own success; its target is no longer the persisted
// one.
TEST_F(TierMigrationTest, TargetReplacedBeforePassStartIsRefused) {
  WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::TargetReplacedBeforePassStartIsRefused:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  // Slips AlterTabletTier(nvme) in once, after the hdd request has persisted its target. The nvme
  // request itself passes the same sync point, so the flag keeps it from recursing.
  bool slip_in = true;
  Status second_status;
  bool second_pass_in_flight = false;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "Tablet::AlterTabletTier:BeforeStartPass",
      [this, &slip_in, &second_status, &second_pass_in_flight](void*) {
        if (slip_in) {
          slip_in = false;
          auto result = tablet()->AlterTabletTier("nvme", kDecoyPathId);
          second_status = result.ok() ? Status::OK() : result.status();
          second_pass_in_flight = result.ok() && result->pass_in_flight;
        }
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto first = tablet()->AlterTabletTier("hdd", kTargetPathId);
  ASSERT_NOK(first);
  ASSERT_TRUE(first.status().IsServiceUnavailable()) << first.status();
  ASSERT_OK(second_status);
  ASSERT_TRUE(second_pass_in_flight);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "nvme");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kDecoyPathId);

  TEST_SYNC_POINT("TierMigrationTest::TargetReplacedBeforePassStartIsRefused:Release");
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 0u);
}

// The refusal is what keeps a pass for one target from booking its outcome against another: a
// hard-failing pass still finishes toward its own target, and its failure count and error stay
// with that target. The new target then starts with a clean record.
TEST_F(TierMigrationTest, TargetChangeDuringFailingPassIsRefused) {
  WriteFileWithKey(1);

  // Make the target directory unusable, as HardFailureKeepsInProgressAndCountsPasses does.
  const auto target_dir = GetTestPath("hdd_tier");
  ASSERT_OK(env_->DeleteDir(target_dir));
  ASSERT_OK(WriteStringToFile(env_.get(), "", target_dir));

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::TargetChangeDuringFailingPassIsRefused:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto first = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_TRUE(first.pass_in_flight);

  auto second = tablet()->AlterTabletTier("nvme", kDecoyPathId);
  ASSERT_NOK(second);
  ASSERT_TRUE(second.status().IsServiceUnavailable()) << second.status();
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "hdd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);

  TEST_SYNC_POINT("TierMigrationTest::TargetChangeDuringFailingPassIsRefused:Release");
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.target_tier, "hdd");
  ASSERT_EQ(info.target_tier_path_id, kTargetPathId);
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(info.migration.files_failed, 1u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 1u);
  ASSERT_NOK(info.migration.last_error);
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 1u);

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("nvme", kDecoyPathId));
  ASSERT_EQ(status.consecutive_failed_passes, 0u);
  ASSERT_OK(status.last_error);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 0u);
  ASSERT_OK(info.migration.last_error);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
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

// A file busy with a compaction is deferred, not failed, and the next reconciler pass moves it.
TEST_F(TierMigrationTest, ReconcilePassRetriesDeferredFile) {
  const uint64_t file_number = WriteFileWithKey(1);
  ASSERT_OK(tablet()->SetTierMigrationTarget("hdd", kTargetPathId));

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::ReconcilePassRetriesDeferredFile:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Stands in for an in-flight compaction: claims the file and blocks mid-copy.
  std::promise<Status> hold_done;
  ASSERT_OK(tablet()->regular_db()->ScheduleDBPathMove(
      file_number, kDecoyPathId, [&](const Status& s) { hold_done.set_value(s); }));
  ASSERT_OK(WaitForFileClaimed(file_number));

  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.scheduled, 1u);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_deferred, 1u);
  ASSERT_EQ(info.migration.files_moved, 0u);
  ASSERT_EQ(info.migration.consecutive_failed_passes, 0u);
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);

  TEST_SYNC_POINT("TierMigrationTest::ReconcilePassRetriesDeferredFile:Release");
  ASSERT_OK(hold_done.get_future().get());

  // The holder left the file on the decoy tier; the next pass moves it where it belongs.
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.scheduled, 1u);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
}

// A pass claims the tablet before it scans, so a different target that arrives between its scan
// and its verdict is refused like one during the moves. Otherwise the pass would schedule moves
// from a scan of the wrong directory, or, having found nothing there, declare the tablet
// converged on a tier it was never scanned for.
TEST_F(TierMigrationTest, TargetChangeDuringScanIsRefused) {
  WriteFileWithKey(1);
  ASSERT_OK(tablet()->SetTierMigrationTarget("hdd", kTargetPathId));

  // Sends the change from inside the next pass, right after its scan.
  bool send_change = false;
  Status change_status;
  yb::SyncPoint::GetInstance()->SetCallBack(
      "Tablet::RunTierMigrationPass:AfterScan", [this, &send_change, &change_status](void*) {
        if (send_change) {
          send_change = false;
          auto result = tablet()->AlterTabletTier("nvme", kDecoyPathId);
          change_status = result.ok() ? Status::OK() : result.status();
        }
      });
  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Scheduling path: the scan finds the file on home, and the pass still moves it to hdd.
  send_change = true;
  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_FALSE(send_change);
  ASSERT_TRUE(change_status.IsServiceUnavailable()) << change_status;
  ASSERT_EQ(result.scheduled, 1u);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "hdd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);

  // Verdict path: the scan finds nothing outside hdd, so the pass declares kDone, for hdd.
  send_change = true;
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_FALSE(send_change);
  ASSERT_TRUE(change_status.IsServiceUnavailable()) << change_status;
  ASSERT_EQ(result.candidates, 0u);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "hdd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);

  // With no pass in flight the change goes through, and the reconciler converges on nvme.
  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("nvme", kDecoyPathId));
  ASSERT_EQ(status.files_total, 1u);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 0u);
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
}

// A pass that leaves a busy file behind does not wake the reconciler: the file is waiting on its
// compaction, and the reconciler's interval is the right time to retry it.
TEST_F(TierMigrationTest, DeferredFileDoesNotWakeReconciler) {
  std::atomic<int> wakes{0};
  tablet()->TEST_SetTierMigrationHooks(std::make_shared<TierMigrationHooks>(TierMigrationHooks{
      .files_moved = nullptr,
      .files_failed = nullptr,
      .wake_reconciler = [&wakes] { ++wakes; },
  }));
  const uint64_t file_number = WriteFileWithKey(1);

  yb::SyncPoint::GetInstance()->LoadDependency(
      {{"TierMigrationTest::DeferredFileDoesNotWakeReconciler:Release",
        "DBImpl::ExecuteDBPathMoveCompaction:BeforeCopy"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  // Stands in for an in-flight compaction: claims the file and blocks mid-copy.
  std::promise<Status> hold_done;
  ASSERT_OK(tablet()->regular_db()->ScheduleDBPathMove(
      file_number, kDecoyPathId, [&](const Status& s) { hold_done.set_value(s); }));
  ASSERT_OK(WaitForFileClaimed(file_number));

  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_deferred, 1u);
  SleepFor(MonoDelta::FromMilliseconds(100));  // a wake would follow the status update
  ASSERT_EQ(wakes.load(), 0);

  TEST_SYNC_POINT("TierMigrationTest::DeferredFileDoesNotWakeReconciler:Release");
  ASSERT_OK(hold_done.get_future().get());
}

#endif  // NDEBUG

// Only a reconciler pass declares convergence, and only from a scan of its own: the pass that
// moved the last file leaves the tablet kInProgress, the next one finds nothing and marks it
// kDone. A file that later shows up outside the target directory puts it back to kInProgress
// until it has been moved too.
TEST_F(TierMigrationTest, ReconcilePassMarksConvergedTabletDone) {
  WriteFileWithKey(1);
  ASSERT_OK(tablet()->SetTierMigrationTarget("hdd", kTargetPathId));

  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.candidates, 1u);
  ASSERT_EQ(result.scheduled, 1u);
  ASSERT_FALSE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kInProgress);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.candidates, 0u);
  ASSERT_EQ(result.scheduled, 0u);
  ASSERT_TRUE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  ASSERT_FALSE(result.status.pass_in_flight);

  // An idle pass changes nothing.
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_FALSE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  ASSERT_EQ(tablet()->tier_migration_status().state, TierMigrationStatus::State::kDone);

  // A straggler lands outside the target directory (stands in for a flush or compaction that was
  // already running when the target switched).
  std::promise<Status> straggler_done;
  ASSERT_OK(tablet()->regular_db()->ScheduleDBPathMove(
      OnlyLiveFileNumber(), kDecoyPathId, [&](const Status& s) { straggler_done.set_value(s); }));
  ASSERT_OK(straggler_done.get_future().get());

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.scheduled, 1u);
  ASSERT_TRUE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kInProgress);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kDecoyPathId), 0u);

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
}

// Hard failures are retried only so many times: at the threshold the reconciler marks the tablet
// kFailed and stops scheduling passes for it. AlterTabletTier is the manual retry.
TEST_F(TierMigrationTest, ReconcilePassMarksFailedAtThresholdUntilNextAlter) {
  WriteFileWithKey(1);

  // Make the target directory unusable, as HardFailureKeepsInProgressAndCountsPasses does.
  const auto target_dir = GetTestPath("hdd_tier");
  ASSERT_OK(env_->DeleteDir(target_dir));
  ASSERT_OK(WriteStringToFile(env_.get(), "", target_dir));
  ASSERT_OK(tablet()->SetTierMigrationTarget("hdd", kTargetPathId));

  const TierReconcileOptions options{.failed_pass_threshold = 2};
  for (uint32_t pass = 1; pass <= 2; ++pass) {
    auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(options));
    ASSERT_EQ(result.scheduled, 1u);
    auto info = ASSERT_RESULT(WaitForPassToFinish());
    ASSERT_EQ(info.migration.files_failed, 1u);
    ASSERT_EQ(info.migration.consecutive_failed_passes, pass);
    ASSERT_EQ(info.migration.state, TierMigrationStatus::State::kInProgress);
  }

  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(options));
  ASSERT_EQ(result.candidates, 1u);
  ASSERT_EQ(result.scheduled, 0u);
  ASSERT_TRUE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kFailed);
  ASSERT_FALSE(result.status.pass_in_flight);
  ASSERT_NOK(result.status.last_error);
  // The last pass's tallies survive for GetTierInfo.
  ASSERT_EQ(result.status.files_failed, 1u);
  ASSERT_EQ(result.status.consecutive_failed_passes, 2u);

  // Skipped from now on, without even a scan, though the file is still on the wrong disk.
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(options));
  ASSERT_EQ(result.candidates, 0u);
  ASSERT_EQ(result.scheduled, 0u);
  ASSERT_FALSE(result.state_changed);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kFailed);

  // Repair the disk. A fresh AlterTabletTier resets the state and moves the file; the reconciler
  // then declares the tablet done.
  ASSERT_OK(env_->DeleteFile(target_dir));
  ASSERT_OK(env_->CreateDir(target_dir));
  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(status.consecutive_failed_passes, 0u);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(options));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
}

// Restart mid-migration: the intent is in the superblock, the files are still on the old disk and
// the in-memory status is gone. The first reconciler pass after the reopen picks up where things
// stand, without a new AlterTabletTier.
TEST_F(TierMigrationTest, ReconcilePassConvergesReopenedTablet) {
  WriteFileWithKey(1);
  WriteFileWithKey(2);
  // Intent only, as if the process died before the first pass ran.
  ASSERT_OK(tablet()->SetTierMigrationTarget("hdd", kTargetPathId));

  tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  tablet()->CompleteShutdown();
  // The writer holds a ref to the old tablet; drop it so the reopen fully replaces the tablet.
  writer_.reset();
  ASSERT_NO_FATALS(TabletReOpen());

  ASSERT_EQ(tablet()->metadata()->target_storage_tier(), "hdd");
  ASSERT_EQ(tablet()->metadata()->target_tier_path_id(), kTargetPathId);
  ASSERT_EQ(tablet()->tier_migration_status().state, TierMigrationStatus::State::kNone);
  auto info = ASSERT_RESULT(tablet()->GetTierInfo());
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 2u);

  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.scheduled, 2u);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kInProgress);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 2u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 2u);
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 0u);

  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  ASSERT_EQ(ASSERT_RESULT(tablet()->GetTierInfo()).current_tier, "hdd");
}

// A split child inherits its parent's SSTs by hard link, on whatever disk they were on. Its
// post-split compaction only compacts the files numbered below the bound the split recorded, and
// a move gives its copy a fresh, higher number, so a parent file moved before that compaction runs
// would never be compacted away. Until it has run, both the reconciler and AlterTabletTier leave
// the child alone; afterwards the reconciler converges it.
TEST_F(TierMigrationTest, SplitChildWaitsForPostSplitCompaction) {
  WriteFileWithKey(1);
  auto child = ASSERT_RESULT(CreateAndOpenSplitChild());
  ASSERT_FALSE(child->metadata()->rocksdb_parent_data_compacted());
  auto info = ASSERT_RESULT(child->GetTierInfo());
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 1u);

  ASSERT_OK(child->SetTierMigrationTarget("hdd", kTargetPathId));
  auto result = ASSERT_RESULT(child->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_TRUE(result.awaiting_post_split_compaction);
  ASSERT_EQ(result.candidates, 0u);
  ASSERT_EQ(result.scheduled, 0u);
  ASSERT_FALSE(result.status.pass_in_flight);
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kInProgress);
  ASSERT_EQ(result.status.consecutive_failed_passes, 0u);

  // The manual path waits too.
  auto status = ASSERT_RESULT(child->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_FALSE(status.pass_in_flight);
  ASSERT_EQ(status.state, TierMigrationStatus::State::kInProgress);
  info = ASSERT_RESULT(child->GetTierInfo());
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 0u);

  // Stands in for the post-split compaction having run.
  child->metadata()->set_rocksdb_parent_data_compacted(true);
  result = ASSERT_RESULT(child->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_FALSE(result.awaiting_post_split_compaction);
  ASSERT_EQ(result.scheduled, 1u);
  info = ASSERT_RESULT(WaitForPassToFinish(child));
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_EQ(SstCountOnPath(info, kTargetPathId), 1u);
  ASSERT_EQ(SstCountOnPath(info, kHomePathId), 0u);
  result = ASSERT_RESULT(child->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  // The parent's own link to the file is untouched.
  ASSERT_EQ(SstCountOnPath(ASSERT_RESULT(tablet()->GetTierInfo()), kHomePathId), 1u);
}

// Only a pass that AlterTabletTier started, and only one that ends with nothing left to retry,
// wakes the reconciler: that is when the verdict should not wait for the interval. A failed file
// waits for the interval, and the reconciler's own passes never wake it.
TEST_F(TierMigrationTest, OnlyCleanManualPassWakesReconciler) {
  std::atomic<int> wakes{0};
  tablet()->TEST_SetTierMigrationHooks(std::make_shared<TierMigrationHooks>(TierMigrationHooks{
      .files_moved = nullptr,
      .files_failed = nullptr,
      .wake_reconciler = [&wakes] { ++wakes; },
  }));
  auto wait_for_wakes = [&wakes](int expected) {
    return WaitFor([&wakes, expected]() { return wakes.load() == expected; },
                   MonoDelta::FromSeconds(10), Format("$0 reconciler wake(s)", expected));
  };
  WriteFileWithKey(1);

  // Clean pass: the file moves, then one wake, after pass_in_flight is cleared.
  auto status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_EQ(status.files_total, 1u);
  auto info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  ASSERT_OK(wait_for_wakes(1));

  // The reconciler's own passes never wake it, whatever they find.
  auto result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.status.state, TierMigrationStatus::State::kDone);
  ASSERT_EQ(wakes.load(), 1);

  // Already converged: AlterTabletTier schedules nothing and wakes at once, for the verdict.
  status = ASSERT_RESULT(tablet()->AlterTabletTier("hdd", kTargetPathId));
  ASSERT_FALSE(status.pass_in_flight);
  ASSERT_EQ(wakes.load(), 2);

  // Hard failure: no wake; the interval retries it.
  const auto decoy_dir = GetTestPath("decoy_tier");
  ASSERT_OK(env_->DeleteDir(decoy_dir));
  ASSERT_OK(WriteStringToFile(env_.get(), "", decoy_dir));
  status = ASSERT_RESULT(tablet()->AlterTabletTier("nvme", kDecoyPathId));
  ASSERT_EQ(status.files_total, 1u);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_failed, 1u);
  SleepFor(MonoDelta::FromMilliseconds(100));  // a wake would follow the status update
  ASSERT_EQ(wakes.load(), 2);

  // A reconciler pass that moves the file does not wake either.
  ASSERT_OK(env_->DeleteFile(decoy_dir));
  ASSERT_OK(env_->CreateDir(decoy_dir));
  result = ASSERT_RESULT(tablet()->RunTierReconcilePass(TierReconcileOptions()));
  ASSERT_EQ(result.scheduled, 1u);
  info = ASSERT_RESULT(WaitForPassToFinish());
  ASSERT_EQ(info.migration.files_moved, 1u);
  SleepFor(MonoDelta::FromMilliseconds(100));
  ASSERT_EQ(wakes.load(), 2);
}

}  // namespace yb::tablet
