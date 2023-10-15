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

#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/transaction_error.h"

#include "yb/master/master_backup.proxy.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"

using namespace std::literals;
using yb::master::SysSnapshotEntryPB;

DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_int32(TEST_inject_status_resolver_complete_delay_ms);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_int32(unresponsive_ts_rpc_timeout_ms);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_bool(allow_encryption_at_rest);

namespace yb {
namespace client {

using ImportedSnapshotData = google::protobuf::RepeatedPtrField<
    master::ImportSnapshotMetaResponsePB::TableMetaPB>;

class BackupTxnTest : public TransactionTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_encryption_at_rest) = false;
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
    mini_cluster_opt_.num_masters = 3;
    TransactionTestBase::SetUp();
    snapshot_util_ = std::make_unique<SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }

  void DoBeforeTearDown() override {
    if (!testing::Test::HasFailure()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
      ASSERT_OK(cluster_->RestartSync());
    }

    TransactionTestBase::DoBeforeTearDown();
  }

  Status WaitAllSnapshotsDeleted() {
    RETURN_NOT_OK(snapshot_util_->WaitAllSnapshotsDeleted());
    // Check if deleted in DocDB.
    return WaitFor([this]() -> Result<bool> {
      auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
      for (const auto& peer : peers) {
        auto db = peer->tablet()->regular_db();
        if (!db) {
          continue;
        }
        auto dir = tablet::TabletSnapshots::SnapshotsDirName(db->GetName());
        auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
        if (!children.empty()) {
          LOG(INFO) << peer->LogPrefix() << "Children: " << AsString(children);
          return false;
        }
      }
      return true;
    }, kWaitTimeout * kTimeMultiplier, "Delete on tablets");
  }

  Result<bool> IsSnapshotImportDone(const ImportedSnapshotData& data) {
    for (const auto& table : data) {
      RETURN_NOT_OK(client_->OpenTable(table.table_ids().new_id()));
    }

    return true;
  }

  void TestDeleteTable(bool restart_masters);
  void TestDeleteSnapshot(bool compact_and_restart);

  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
};

TEST_F(BackupTxnTest, Simple) {
  SetAtomicFlag(
      std::chrono::duration_cast<std::chrono::microseconds>(1s).count() * kTimeMultiplier,
      &FLAGS_max_clock_skew_usec);
  ASSERT_NO_FATALS(WriteData());

  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->StartSnapshot(table_));

  bool has_pending = false;
  ASSERT_OK(WaitFor([this, &snapshot_id, &has_pending]() -> Result<bool> {
    if (!VERIFY_RESULT(snapshot_util_->IsSnapshotDone(snapshot_id))) {
      has_pending = true;
      return false;
    }
    return true;
  }, 10s, "Snapshot done"));

  ASSERT_TRUE(has_pending);

  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(1, WriteOpType::UPDATE));

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id));

  ASSERT_NO_FATALS(VerifyData(/* num_transactions=*/ 1, WriteOpType::INSERT));
}

TEST_F(BackupTxnTest, PointInTimeRestore) {
  ASSERT_NO_FATALS(WriteData());
  auto hybrid_time = cluster_->mini_tablet_server(0)->server()->Clock()->Now();
  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));

  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyData(/* num_transactions=*/ 1, WriteOpType::INSERT));
}

// This test writes a lot of update to the same key.
// Then takes snapshot and restores it to time before the write.
// So we test how filtering iterator works in case where a lot of record should be skipped.
TEST_F(BackupTxnTest, PointInTimeBigSkipRestore) {
  constexpr int kNumWrites = RegularBuildVsSanitizers(100000, 100);
  constexpr int kKey = 123;

  std::vector<std::future<FlushStatus>> futures;
  auto session = CreateSession();
  ASSERT_OK(WriteRow(session, kKey, 0));
  auto hybrid_time = cluster_->mini_tablet_server(0)->server()->Clock()->Now();
  for (int r = 1; r <= kNumWrites; ++r) {
    ASSERT_OK(WriteRow(session, kKey, r, WriteOpType::INSERT, client::Flush::kFalse));
    futures.push_back(session->FlushFuture());
  }

  int good = 0;
  for (auto& future : futures) {
    if (future.get().status.ok()) {
      ++good;
    }
  }

  LOG(INFO) << "Total good: " << good;

  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time));

  auto value = ASSERT_RESULT(SelectRow(session, kKey));
  ASSERT_EQ(value, 0);
}

// Restore to the time before current history cutoff.
TEST_F(BackupTxnTest, PointInTimeRestoreBeforeHistoryCutoff) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  ASSERT_NO_FATALS(WriteData());
  auto hybrid_time = cluster_->mini_tablet_server(0)->server()->Clock()->Now();
  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));

  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  ASSERT_OK(WaitFor([this, hybrid_time]() -> Result<bool> {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (const auto& peer : peers) {
      // History cutoff is not moved for tablets w/o writes after current history cutoff.
      // So just ignore such tablets.
      if (peer->tablet()->mvcc_manager()->LastReplicatedHybridTime() < hybrid_time) {
        continue;
      }
      // Check that history cutoff is after hybrid_time.
      auto read_operation = tablet::ScopedReadOperation::Create(
          peer->tablet(), tablet::RequireLease::kTrue, ReadHybridTime::SingleTime(hybrid_time));
      if (read_operation.ok()) {
        auto policy = peer->tablet()->RetentionPolicy();
        LOG(INFO) << "Pending history cutoff, tablet: " << peer->tablet_id()
                  << ", current: "
                  << policy->GetRetentionDirective().history_cutoff
                                                    .primary_cutoff_ht
                  << ", desired: " << hybrid_time;
        return false;
      }
      if (!read_operation.status().IsSnapshotTooOld()) {
        return read_operation.status();
      }
    }
    return true;
  }, kWaitTimeout, "History retention move past hybrid time"));

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyData(/* num_transactions=*/ 1, WriteOpType::INSERT));
}

TEST_F(BackupTxnTest, Persistence) {
  LOG(INFO) << "Write data";

  ASSERT_NO_FATALS(WriteData());

  LOG(INFO) << "Create snapshot";

  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));

  LOG(INFO) << "First restart";

  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Restart());
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  LOG(INFO) << "Create namespace";

  // Create namespace and flush, to avoid replaying logs in the master tablet containing the
  // CREATE_ON_MASTER operation for the snapshot.
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name() + "_Test",
                                                kTableName.namespace_type()));

  LOG(INFO) << "Flush";

  auto tablet_peer = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->tablet_peer();
  ASSERT_OK(tablet_peer->tablet()->Flush(tablet::FlushMode::kSync));

  LOG(INFO) << "Second restart";

  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Restart());

  LOG(INFO) << "Verify";

  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));
}

void BackupTxnTest::TestDeleteSnapshot(bool compact_and_restart) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));
  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));
  if (compact_and_restart) {
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      ASSERT_OK(cluster_->mini_master(i)->tablet_peer()->tablet()->ForceManualRocksDBCompact());
    }
    ASSERT_OK(cluster_->RestartSync());
  }
  ASSERT_OK(snapshot_util_->WaitAllSnapshotsDeleted());

  SetAtomicFlag(1000, &FLAGS_snapshot_coordinator_cleanup_delay_ms);

  ASSERT_OK(snapshot_util_->WaitAllSnapshotsCleaned());
}

TEST_F(BackupTxnTest, Delete) {
  TestDeleteSnapshot(/* compact_and_restart= */ false);
}

TEST_F(BackupTxnTest, DeleteWithCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 0;

  TestDeleteSnapshot(/* compact_and_restart= */ true);
}

TEST_F(BackupTxnTest, CleanupAfterRestart) {
  SetAtomicFlag(300000, &FLAGS_snapshot_coordinator_cleanup_delay_ms);

  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));
  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));
  ASSERT_OK(snapshot_util_->WaitAllSnapshotsDeleted());

  ASSERT_FALSE(ASSERT_RESULT(snapshot_util_->ListSnapshots()).empty());

  SetAtomicFlag(1000, &FLAGS_snapshot_coordinator_cleanup_delay_ms);
  ASSERT_OK(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Restart());

  ASSERT_OK(snapshot_util_->WaitAllSnapshotsCleaned());
}

TEST_F(BackupTxnTest, ImportMeta) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::COMPLETE,
                                           table_.table()->GetPartitionCount()));

  auto snapshots = ASSERT_RESULT(snapshot_util_->ListSnapshots(
      snapshot_id, ListDeleted::kFalse, PrepareForBackup::kTrue));
  ASSERT_EQ(snapshots.size(), 1);

  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));
  ASSERT_OK(snapshot_util_->WaitAllSnapshotsDeleted());
  ASSERT_OK(client_->DeleteTable(kTableName));
  ASSERT_OK(client_->DeleteNamespace(kTableName.namespace_name()));

  auto import_data = ASSERT_RESULT(snapshot_util_->StartImportSnapshot(snapshots[0]));

  ASSERT_OK(WaitFor([this, import_data] {
    return IsSnapshotImportDone(import_data);
  }, kWaitTimeout * kTimeMultiplier, "Complete import snapshot"));

  ASSERT_OK(table_.Open(kTableName, client_.get()));

  ASSERT_NO_FATALS(WriteData());
}

TEST_F(BackupTxnTest, Retry) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_unresponsive_ts_rpc_timeout_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 1000;

  ASSERT_NO_FATALS(WriteData());

  ShutdownAllTServers(cluster_.get());

  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->StartSnapshot(table_));

  std::this_thread::sleep_for(FLAGS_unresponsive_ts_rpc_timeout_ms * 1ms + 1s);

  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::CREATING,
                                           table_.table()->GetPartitionCount()));

  ASSERT_OK(StartAllTServers(cluster_.get()));

  ASSERT_OK(snapshot_util_->WaitSnapshotDone(snapshot_id, 15s));

  ASSERT_NO_FATALS(VerifyData());

  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(WriteOpType::UPDATE));

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id));

  ASSERT_NO_FATALS(VerifyData());
}

TEST_F(BackupTxnTest, Failure) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;

  ASSERT_NO_FATALS(WriteData());

  ShutdownAllTServers(cluster_.get());

  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->StartSnapshot(table_));

  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::CREATING,
                                           table_.table()->GetPartitionCount()));

  ShutdownAllMasters(cluster_.get());

  ASSERT_OK(StartAllTServers(cluster_.get()));

  // Wait 2 rounds to be sure that very recent history cutoff committed.
  std::this_thread::sleep_for(FLAGS_raft_heartbeat_interval_ms * 2ms * kTimeMultiplier);

  ASSERT_OK(StartAllMasters(cluster_.get()));

  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, SysSnapshotEntryPB::FAILED, 30s));
}

TEST_F(BackupTxnTest, Restart) {
  FLAGS_timestamp_history_retention_interval_sec =
      std::chrono::duration_cast<std::chrono::seconds>(kWaitTimeout).count() *
      kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;

  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));

  ShutdownAllMasters(cluster_.get());

  // Wait 2 rounds to be sure that very recent history cutoff committed.
  std::this_thread::sleep_for((FLAGS_timestamp_history_retention_interval_sec + 1) * 1s);

  ASSERT_OK(StartAllMasters(cluster_.get()));

  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, SysSnapshotEntryPB::COMPLETE, 1s));
}

TEST_F(BackupTxnTest, CompleteAndBounceMaster) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));

  std::this_thread::sleep_for(1s);

  ASSERT_OK(client_->DeleteTable(kTableName));

  auto leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  leader->Shutdown();

  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, SysSnapshotEntryPB::COMPLETE, 1s));

  ASSERT_OK(leader->Start());
}

TEST_F(BackupTxnTest, FlushSysCatalogAndDelete) {
  ASSERT_NO_FATALS(WriteData());
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));

  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    auto tablet_peer = cluster_->mini_master(i)->tablet_peer();
    ASSERT_OK(tablet_peer->tablet()->Flush(tablet::FlushMode::kSync));
  }

  ShutdownAllTServers(cluster_.get());
  ASSERT_OK(snapshot_util_->DeleteSnapshot(snapshot_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
  ShutdownAllMasters(cluster_.get());

  LOG(INFO) << "Start masters";

  ASSERT_OK(StartAllMasters(cluster_.get()));
  ASSERT_OK(StartAllTServers(cluster_.get()));

  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, SysSnapshotEntryPB::DELETED, 30s));
}

// Workload writes same value across all keys in a txn, using sevaral txns in concurrently.
// Checks that after restore all keys/tablets report same value.
TEST_F(BackupTxnTest, Consistency) {
  constexpr int kThreads = 5;
  constexpr int kKeys = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_status_resolver_complete_delay_ms) = 100;

  TestThreadHolder thread_holder;
  std::atomic<int> value(0);

  for (int i = 0; i != kThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), &value] {
      auto session = CreateSession();
      while (!stop.load(std::memory_order_acquire)) {
        auto txn = CreateTransaction();
        session->SetTransaction(txn);
        auto v = value.fetch_add(1, std::memory_order_acq_rel);
        for (int j = 0; j != kKeys; ++j) {
          ASSERT_OK(WriteRow(session, j, v, WriteOpType::INSERT, Flush::kFalse));
        }
        auto status = session->TEST_Flush();
        if (status.ok()) {
          status = txn->CommitFuture().get();
        }
        if (!status.ok()) {
          TransactionError txn_error(status);
          ASSERT_TRUE(txn_error == TransactionErrorCode::kConflict ||
                      txn_error == TransactionErrorCode::kAborted ||
                      status.IsCombined()) << status;
        } else {
          LOG(INFO) << "Committed: " << txn->id() << ", written: " << v;
        }
      }
    });
  }

  while (value.load(std::memory_order_acquire) < 100) {
    std::this_thread::sleep_for(5ms);
  }

  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_));

  thread_holder.Stop();

  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id));

  auto session = CreateSession();
  int restored_value = -1;
  for (int j = 0; j != kKeys; ++j) {
    auto current_value = ASSERT_RESULT(SelectRow(session, j));
    LOG(INFO) << "Key: " << j << ", value: " << current_value;
    if (restored_value == -1) {
      restored_value = current_value;
    } else {
      ASSERT_EQ(restored_value, current_value);
    }
  }

  LOG(INFO) << "Value: " << restored_value;
}

void BackupTxnTest::TestDeleteTable(bool restart_masters) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_unresponsive_ts_rpc_timeout_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 2500 * kTimeMultiplier;

  ASSERT_NO_FATALS(WriteData());

  ShutdownAllTServers(cluster_.get());
  TxnSnapshotId snapshot_id = ASSERT_RESULT(snapshot_util_->StartSnapshot(table_));

  std::this_thread::sleep_for(FLAGS_unresponsive_ts_rpc_timeout_ms * 1ms + 1s);
  ASSERT_OK(snapshot_util_->VerifySnapshot(snapshot_id, SysSnapshotEntryPB::CREATING,
                                           table_.table()->GetPartitionCount()));

  ASSERT_OK(client_->DeleteTable(kTableName, false));

  if (restart_masters) {
    ShutdownAllMasters(cluster_.get());
  }

  ASSERT_OK(StartAllTServers(cluster_.get()));

  if (restart_masters) {
    ASSERT_OK(StartAllMasters(cluster_.get()));
    ASSERT_OK(WaitUntilMasterHasLeader(cluster_.get(), 5s));
  }

  ASSERT_OK(snapshot_util_->WaitSnapshotInState(snapshot_id, SysSnapshotEntryPB::FAILED,
                                                5s * kTimeMultiplier));
}

TEST_F(BackupTxnTest, DeleteTable) {
  TestDeleteTable(/* restart_masters= */ false);
}

TEST_F(BackupTxnTest, DeleteTableWithMastersRestart) {
  TestDeleteTable(/* restart_masters= */ true);
}

} // namespace client
} // namespace yb
