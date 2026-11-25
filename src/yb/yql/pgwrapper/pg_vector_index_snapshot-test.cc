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

#include "yb/client/client-test-util.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/yb_table_name.h"

#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(TEST_skip_flush_deactivated_vector_indexes);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(TEST_skip_vector_index_stamp_after_restore_reopen);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(TEST_sleep_before_restore_checkpoint_resumes_ops_ms);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint64(vector_index_task_size);

namespace yb::tablet {

extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::pgwrapper {

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexTest);
MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexColocationOnlyTest);

TEST_P(PgVectorIndexTest, SnapshotSchedule) {
  constexpr size_t kNumRows = 128;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, {}, 10));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(
      schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// An in-place restore of a plain snapshot keeps vector indexes created after the snapshot. They
// have no counterpart in the snapshot superblock and must be backfilled from the restored rows.
// See issue #35832.
TEST_P(PgVectorIndexTest, SnapshotRestoreIndexCreatedAfterSnapshot) {
  constexpr size_t kNumRows = 128;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  client::YBTableName db_name(YQL_DATABASE_PGSQL);
  db_name.set_namespace_id(ASSERT_RESULT(client::GetNamespaceIdByNamespaceName(
      client_.get(), DbName())));
  db_name.set_namespace_name(DbName());
  auto snapshot_id = ASSERT_RESULT(snapshot_util.CreateSnapshot(db_name));

  // Delete the rows first, so the indexes below are built empty and correct results after the
  // restore can only come from the post-restore backfill.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;

  ASSERT_OK(CreateIndex(conn, "vi_after"));
  ASSERT_OK(CreateIndex(conn, "vi_dropped"));
  ASSERT_OK(conn.Execute("DROP INDEX vi_dropped"));

  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    for (const auto& index : ListVectorIndexes(cluster_.get())) {
      if (!index->IsDeactivated() && !VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Wait vector index backfills"));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  // Leave only the index created after the snapshot to serve the query.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kVectorIndexName));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// A restore to before a DROP COLUMN of the vector column must not leave the column recorded as
// deleted, otherwise compaction past the drop discards its reverse mapping entries.
// See issue #35814.
TEST_P(PgVectorIndexTest, SnapshotScheduleRestoreBeforeDropColumn) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  // Waits until the tablet metadata of every replica has, or lacks, deleted columns.
  auto wait_deleted_cols = [this, &table_id](bool deleted, const std::string& description) {
    return WaitFor([this, &table_id, deleted]() -> Result<bool> {
      for (const auto& peer : VERIFY_RESULT(ListTabletPeersForTableName(
               cluster_.get(), "test", ListPeersFilter::kAll))) {
        auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
        if (table_info->deleted_cols.empty() == deleted) {
          return false;
        }
      }
      return true;
    }, 30s * kTimeMultiplier, description);
  };

  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN embedding"));
  ASSERT_OK(wait_deleted_cols(true, "Drop applied"));

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, hybrid_time));
  ASSERT_OK(wait_deleted_cols(false, "Restored column no longer deleted"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The schedule holds the history cutoff at its last snapshot, which must pass the drop.
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, cluster_->mini_master(0)->Now()));
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->CompactTablets());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A restored vector index that needs a compaction right away must not start it before the
// restore resumes read/write operations. The compaction would fail with TryAgain, the index would
// keep that failure, and the restore apply or the next write would crash the tserver. See DB-23803.
TEST_P(PgVectorIndexTest, CompactionAfterSnapshotRestore) {
  constexpr size_t kBatch = 8;

  num_pre_split_tablets_ = 1;
  // Accumulate enough chunks for a compaction, but let it start only from the restore.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  size_t num_rows = 0;
  for (int i = 0; i != GetFileNumCompactionTrigger(); ++i) {
    ASSERT_OK(InsertRows(conn, num_rows + 1, num_rows + kBatch));
    num_rows += kBatch;
    // A lagging follower would put several batches into one chunk and have nothing to compact.
    ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
    ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes));
  }

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  std::atomic<size_t> num_compacted{0};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::DoCompact:ChunksUpdated", [&num_compacted](void*) {
    ++num_compacted;
  });
  sync_point->EnableProcessing();
  auto se = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = true;
  // Hold the pause long enough for a compaction enabled under it to reach its first read.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_before_restore_checkpoint_resumes_ops_ms) =
      1000 * kTimeMultiplier;
  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_before_restore_checkpoint_resumes_ops_ms) = 0;

  // One compaction per tablet replica.
  const auto num_indexes = ListVectorIndexes(cluster_.get()).size();
  ASSERT_OK(LoggedWaitFor(
      [&num_compacted, num_indexes] { return num_compacted >= num_indexes; },
      30s * kTimeMultiplier, "Restored indexes compacted"));

  ASSERT_NO_FATALS(VerifyRead(conn, kBatch, AddFilter::kFalse));
  ASSERT_OK(InsertRows(conn, num_rows + 1, num_rows + kBatch));
  ASSERT_NO_FATALS(VerifyRead(conn, kBatch, AddFilter::kFalse));
}

void PgVectorIndexTest::TestSnapshotRestoreNonTransactionalWrites(
    CrashAfterRestoreReopen crash_after_reopen) {
  constexpr size_t kNumRows = 16;

  // One tablet, so every replica's vector index holds all the rows.
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  // These rows take the single-shard fast path, so they feed the vector indexes at apply time.
  // Flushing the regular DB only keeps the indexes behind it, which is what makes bootstrap
  // consider the operations for replay into them.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false,
      NonTransactionalWrites::kTrue));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(schedule_id, hybrid_time));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_vector_index_stamp_after_restore_reopen) =
      crash_after_reopen.get();
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_vector_index_stamp_after_restore_reopen) = false;

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows) << index->ToString();
  }
}

// A restore replaces the vector index storage together with the regular DB. The vector indexes
// must get the restore op id like the regular DB, otherwise bootstrap starts replay at their older
// flushed OpId and re-inserts the rolled back writes into them. See issue #32797.
TEST_P(PgVectorIndexTest, SnapshotRestoreNonTransactionalWrites) {
  TestSnapshotRestoreNonTransactionalWrites(CrashAfterRestoreReopen::kFalse);
}

// Once the regular DB has the restore op id, bootstrap does not replay the restore. A crash right
// after the restore reopens the storages must find the vector indexes at the restore op id too,
// not at the snapshot's OpIds. See issue #35077.
TEST_P(PgVectorIndexTest, SnapshotRestoreCrashAfterReopen) {
  TestSnapshotRestoreNonTransactionalWrites(CrashAfterRestoreReopen::kTrue);
}

// Rows a vector index holds only in memory when a snapshot is taken must survive a restore served
// from that snapshot. See issue #34759.
TEST_P(PgVectorIndexColocationOnlyTest, SnapshotRestoreKeepsUnflushedVectorIndexRows) {
  constexpr size_t kNumRows = 16;

  // One tablet, so that every replica's vector index holds unflushed rows below.
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Only the snapshot taken at creation precedes the restore point below, so the restore has to
  // take a new snapshot while the rows below are still in the vector index's memory.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  // Non-transactional writes leave nothing in the intents DB, whose flush filter would otherwise
  // force a vector index flush.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false,
      NonTransactionalWrites::kTrue));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 2, AddFilter::kFalse));
}

// A vector index dropped while a snapshot schedule retains it stays on the tablets, hidden, until
// the retention ends. A restore can only bring it back as of a time before the drop, so writes
// after the drop must not be added to it. See issue #34329.
TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexRetainedBySnapshotSchedule) {
  constexpr size_t kNumRows = 32;
  const std::string kDroppedIndexName = "dropped_vi";

  // The check counts chunk files, which a compaction would merge.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Without its vector index the table becomes splittable under the suite's quick-split setup.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kDroppedIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  // Storage dir of each vector index replica, keyed by whether it belongs to the dropped index.
  std::map<bool, std::vector<std::string>> index_dirs;
  TableId dropped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      const bool dropped = table_info->table_name == kDroppedIndexName;
      if (dropped) {
        dropped_index_id = vector_index->table_id();
      }
      index_dirs[dropped].push_back(vector_index->path());
    }
  }
  ASSERT_FALSE(dropped_index_id.empty());
  ASSERT_EQ(index_dirs[false].size(), index_dirs[true].size());

  auto count_chunks = [](const std::string& dir) -> Result<size_t> {
    auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
    return std::ranges::count_if(children, [](const std::string& name) {
      return name.starts_with("vectorindex_");
    });
  };
  std::unordered_map<std::string, size_t> chunks_before;
  for (const auto& [_, dirs] : index_dirs) {
    for (const auto& dir : dirs) {
      chunks_before[dir] = ASSERT_RESULT(count_chunks(dir));
      ASSERT_GT(chunks_before[dir], 0) << dir;
    }
  }

  ASSERT_OK(conn.Execute("DROP INDEX " + kDroppedIndexName));

  // Wait until the master has hidden the index and finished any tablet RPCs that come with that.
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto dropped_table = catalog_manager.GetTableInfo(dropped_index_id);
  ASSERT_NE(dropped_table, nullptr);
  ASSERT_OK(WaitFor(
      [&dropped_table] {
        return dropped_table->LockForRead()->is_hidden_but_not_deleting() &&
               !dropped_table->HasTasks();
      },
      30s * kTimeMultiplier, "Dropped index hidden"));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  for (const auto& dir : index_dirs[false]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    EXPECT_GT(chunks_after, chunks_before[dir]) << dir;
  }
  // The schedule still retains the dropped index, so its storage must stay as it was.
  for (const auto& dir : index_dirs[true]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    EXPECT_EQ(chunks_after, chunks_before[dir]) << dir;
  }

  // Verification skips the dropped index, which lacks the rows written after the drop.
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// Rows applied to a vector index before DROP INDEX but not yet flushed by it must survive a tserver
// restart that happens after the drop reached the tablets: a restore to before the drop can be
// served from a snapshot taken after the restart, which checkpoints the retained index as bootstrap
// left it. With skip_deactivation_flush the deactivated index keeps those rows in memory until the
// restart, so they come back only through bootstrap replay, and only the flush on tablet start
// persists them. See issue #34329.
void PgVectorIndexColocationOnlyTest::TestDroppedIndexKeepsUnflushedRows(
    bool skip_deactivation_flush) {
  constexpr size_t kNumRows = 16;

  // Without its vector index the table becomes splittable under the suite's quick-split setup,
  // and the restore would wait for those splits.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Only the snapshot taken at creation precedes the restore point below, so the restore has to
  // take a new snapshot, after the restart.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  // Flush only the regular DB, so these rows reach the vector index after a restart only through
  // bootstrap replay.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  std::vector<std::pair<tablet::TabletPeerPtr, TableId>> peers_with_indexed_table;
  TableId index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto vector_indexes = tablet->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);
    index_id = vector_indexes->front()->table_id();
    auto index_table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index_id));
    peers_with_indexed_table.emplace_back(peer, index_table_info->index_info->indexed_table_id());
  }
  ASSERT_FALSE(peers_with_indexed_table.empty());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_flush_deactivated_vector_indexes) =
      skip_deactivation_flush;
  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));

  // The drop removes the index from the indexed table's index map on every replica.
  ASSERT_OK(WaitFor(
      [&peers_with_indexed_table, &index_id]() -> Result<bool> {
        for (const auto& [peer, indexed_table_id] : peers_with_indexed_table) {
          auto tablet = VERIFY_RESULT(peer->shared_tablet());
          auto index_map = VERIFY_RESULT(tablet->metadata()->index_map(indexed_table_id));
          if (index_map->contains(index_id)) {
            return false;
          }
        }
        return true;
      },
      30s * kTimeMultiplier, "Index removed from index maps"));

  // The hybrid time after which each replica hosting the index stops adding rows to it.
  auto deactivated_hts = [this, &index_id]() -> Result<std::vector<HybridTime>> {
    std::vector<HybridTime> result;
    for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (!tablet) {
        continue;
      }
      auto table_info = tablet->metadata()->GetTableInfo(index_id);
      if (table_info.ok()) {
        result.push_back((*table_info)->vector_index_deactivated_ht);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  auto deactivated_before_restart = ASSERT_RESULT(deactivated_hts());
  ASSERT_EQ(deactivated_before_restart.size(), peers_with_indexed_table.size());
  for (const auto& ht : deactivated_before_restart) {
    ASSERT_TRUE(ht.is_valid());
    ASSERT_GT(ht, restore_at);
  }

  DisableFlushOnShutdown(*cluster_, true);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_flush_deactivated_vector_indexes) = false;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_EQ(ASSERT_RESULT(deactivated_hts()), deactivated_before_restart);

  // Replicas of the index hosted by the tablets.
  auto index_replicas = [this, &index_id]() {
    std::vector<docdb::DocVectorIndexPtr> result;
    for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto list = tablet ? tablet->vector_indexes().List() : tablet::VectorIndexList();
      if (!list) {
        continue;
      }
      for (const auto& index : *list) {
        if (index->table_id() == index_id) {
          result.push_back(index);
        }
      }
    }
    return result;
  };

  // The deactivated index gets no new rows that would flush it, so the tablet flushes it on start.
  ASSERT_OK(WaitFor(
      [&index_replicas] {
        return std::ranges::all_of(index_replicas(), [](const auto& index) {
          return index->GetFlushAbility() == storage::FlushAbility::kNoNewData;
        });
      },
      30s * kTimeMultiplier, "Deactivated index flushed"));

  // Every replica of the retained index holds all the rows of its tablet written before the drop.
  size_t total_entries = 0;
  for (const auto& index : index_replicas()) {
    total_entries += ASSERT_RESULT(index->TotalEntries());
  }
  ASSERT_EQ(total_entries, kNumRows * 2 * cluster_->num_tablet_servers());

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 2, AddFilter::kFalse));

  // The restored index takes new rows again.
  ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 3, AddFilter::kFalse));

  // The restore brings the index back into the index maps, which reactivates it.
  for (const auto& ht : ASSERT_RESULT(deactivated_hts())) {
    ASSERT_FALSE(ht.is_valid()) << ht;
  }
}

TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexKeepsUnflushedRowsAcrossRestart) {
  TestDroppedIndexKeepsUnflushedRows(/* skip_deactivation_flush= */ false);
}

TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexKeepsUnflushedRowsAcrossRestartReplay) {
  TestDroppedIndexKeepsUnflushedRows(/* skip_deactivation_flush= */ true);
}

// A deactivated index takes no new rows, so nothing advances its flushed OpId. Bootstrap replays
// from the lowest flushed OpId across all storages, so a frozen one would make every restart replay
// all the retained WAL for as long as the schedule keeps the index.
TEST_P(PgVectorIndexColocationOnlyTest, DeactivatedIndexDoesNotHoldBackBootstrap) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kNumRowsAfterDrop = 128;
  const std::string kDroppedIndexName = "dropped_vi";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kDroppedIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  TableId dropped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      if (table_info->table_name == kDroppedIndexName) {
        dropped_index_id = vector_index->table_id();
      }
    }
  }
  ASSERT_FALSE(dropped_index_id.empty());

  ASSERT_OK(conn.Execute("DROP INDEX " + kDroppedIndexName));

  // Each replica of the dropped index, with its tablet peer.
  auto dropped_replicas = [this, &dropped_index_id]() -> Result<
      std::vector<std::pair<tablet::TabletPeerPtr, docdb::DocVectorIndexPtr>>> {
    std::vector<std::pair<tablet::TabletPeerPtr, docdb::DocVectorIndexPtr>> result;
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      for (const auto& vector_index : *tablet->vector_indexes().List()) {
        if (vector_index->table_id() == dropped_index_id) {
          result.emplace_back(peer, vector_index);
        }
      }
    }
    return result;
  };
  ASSERT_OK(WaitFor(
      [&dropped_replicas]() -> Result<bool> {
        auto replicas = VERIFY_RESULT(dropped_replicas());
        return !replicas.empty() &&
               std::ranges::all_of(replicas, [](const auto& replica) {
                 return replica.second->IsDeactivated();
               });
      },
      30s * kTimeMultiplier, "Dropped index deactivated"));
  // Every tablet of the table hosts the index on every tablet server.
  const auto num_dropped_replicas = ASSERT_RESULT(dropped_replicas()).size();
  ASSERT_GT(num_dropped_replicas, 0);
  ASSERT_EQ(num_dropped_replicas % cluster_->num_tablet_servers(), 0);

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows + kNumRowsAfterDrop));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // The dropped index's flushed OpId keeps up with the regular DB's, so it does not lower the
  // OpId bootstrap replays from.
  auto wait_dropped_index_caught_up =
      [&dropped_replicas, num_dropped_replicas](const std::string& description) {
    return WaitFor(
        [&dropped_replicas, num_dropped_replicas]() -> Result<bool> {
          auto replicas = VERIFY_RESULT(dropped_replicas());
          if (replicas.size() != num_dropped_replicas) {
            LOG(INFO) << "Found " << replicas.size() << " replicas of the dropped index, expected "
                      << num_dropped_replicas;
            return false;
          }
          for (const auto& [peer, index] : replicas) {
            auto tablet = VERIFY_RESULT(peer->shared_tablet());
            auto regular = VERIFY_RESULT(tablet->GetPersistentState()).regular;
            // Without a flushed regular DB OpId there is nothing to compare against.
            SCHECK(regular.is_valid_not_empty(), IllegalState,
                   Format("$0no flushed regular DB OpId", tablet->LogPrefix()));
            auto frontier = index->GetFlushedFrontier();
            auto index_op_id = frontier ? frontier->op_id() : OpId();
            if (index_op_id < regular) {
              LOG(INFO) << tablet->LogPrefix() << "Dropped index flushed OpId " << index_op_id
                        << " behind the regular DB's " << regular;
              return false;
            }
          }
          return true;
        },
        30s * kTimeMultiplier, description);
  };
  ASSERT_OK(wait_dropped_index_caught_up("Dropped index flushed OpId caught up"));

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_OK(wait_dropped_index_caught_up("Dropped index flushed OpId caught up after restart"));
}

// A vector index flush that fails, before or after it seals the mutable chunk, fails the tablet
// flush and the checkpoint. See #35156.
TEST_P(PgVectorIndexColocationOnlyTest, FlushFailureFailsCheckpoint) {
  constexpr size_t kRowsPerRound = 16;

  auto conn = ASSERT_RESULT(MakeIndex());

  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());

  auto* sync_point = SyncPoint::GetInstance();
  auto cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  const std::string kInjectedError = "Injected vector index flush failure";
  // The intents DB flush filter also flushes the vector indexes, from a RocksDB thread, and its
  // failure sticks to the intents DB. So the failure of Flush itself is injected only on the test
  // thread, where Tablet::Flush and the checkpoint flush the indexes. A failed chunk save leaves
  // the index failed, and the intents DB with it, so that round goes last.
  const auto test_thread = std::this_thread::get_id();
  size_t num_rows = 0;
  for (const auto& [point, test_thread_only] : {
           std::pair{"VectorLSM::Flush:Status", true},
           std::pair{"VectorLSM::DoSaveChunk:Status", false}}) {
    SCOPED_TRACE(point);
    // Saves the rows of earlier rounds, so no save of them can hit the injection before this
    // round's rows are written: a failed index rejects new rows.
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      auto peer_tablet = ASSERT_RESULT(peer->shared_tablet());
      ASSERT_OK(peer_tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
    }

    sync_point->ClearAllCallBacks();
    sync_point->SetCallBack(point, [test_thread, test_thread_only, kInjectedError](void* arg) {
      if (!test_thread_only || std::this_thread::get_id() == test_thread) {
        *static_cast<Status*>(arg) = STATUS(IOError, kInjectedError);
      }
    });
    sync_point->EnableProcessing();

    // Written after the injection is enabled, so a background flush cannot save them first and
    // leave Tablet::Flush nothing to fail on.
    ASSERT_OK(InsertRows(conn, num_rows + 1, num_rows + kRowsPerRound));
    num_rows += kRowsPerRound;
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

    ASSERT_NOK_STR_CONTAINS(
        tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly), kInjectedError);
    ASSERT_NOK_STR_CONTAINS(
        tablet->snapshots().CreateCheckpoint(GetTestPath(Format("checkpoint_$0", num_rows))),
        kInjectedError);

    sync_point->DisableProcessing();
  }
}

// A caller holding only a non-blocking operation, e.g. DoCleanupIntentFiles, can flush while tablet
// shutdown is stopping the vector indexes. The flush must fail, since the unflushed vectors still
// need the intents that caller deletes next.
TEST_P(PgVectorIndexColocationOnlyTest, FlushFailsWhileVectorIndexesShutDown) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 16));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());

  tablet->vector_indexes().StartShutdown();
  auto status = tablet->Flush(
      tablet::FlushMode::kSync,
      tablet::FlushFlags::kRegular | tablet::FlushFlags::kVectorIndexes |
          tablet::FlushFlags::kNoScopedOperation,
      rocksdb::FlushReason::kTestOnly);
  ASSERT_TRUE(status.IsShutdownInProgress()) << status;
}

// A restore to before a vector index was created removes the index from the indexed table's index
// map, while the snapshot schedule can keep the hidden index on the tablets. No restore can bring
// it back, since that would be a forward restore, so it must not take new rows either.
// The master keeps the index on a distributed table's tablets only when they are already retained
// by the schedule, e.g. after another index was dropped under it.
TEST_P(PgVectorIndexColocationOnlyTest, IndexRestoredAwayRetainedBySnapshotSchedule) {
  constexpr size_t kNumRows = 32;
  const std::string kRestoredAwayIndexName = "restored_away_vi";
  const std::string kEarlierDroppedIndexName = "earlier_dropped_vi";

  // The check counts chunk files, which a compaction would merge.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kEarlierDroppedIndexName, vector_index::DistanceKind::kCosine));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Ids of the vector index tables hosted by the tablets, by name.
  auto index_ids_by_name = [this]() -> Result<std::unordered_map<std::string, TableId>> {
    std::unordered_map<std::string, TableId> result;
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      for (const auto& vector_index : *tablet->vector_indexes().List()) {
        auto table_info = VERIFY_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
        result.emplace(table_info->table_name, vector_index->table_id());
      }
    }
    return result;
  };
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto wait_hidden = [&catalog_manager](const TableId& table_id) -> Status {
    auto table = catalog_manager.GetTableInfo(table_id);
    SCHECK_NOTNULL(table);
    return WaitFor(
        [&table] { return table->LockForRead()->is_hidden() && !table->HasTasks(); },
        30s * kTimeMultiplier, Format("$0 hidden", table->name()));
  };
  auto earlier_dropped_index_id = ASSERT_RESULT(index_ids_by_name()).at(kEarlierDroppedIndexName);

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));
  ASSERT_OK(conn.Execute("DROP INDEX " + kEarlierDroppedIndexName));
  ASSERT_OK(wait_hidden(earlier_dropped_index_id));
  auto restore_at = cluster_->mini_master(0)->Now();

  ASSERT_OK(CreateIndex(conn, kRestoredAwayIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Storage dir of each replica of the kept index (false) and the restored away index (true).
  std::map<bool, std::vector<std::string>> index_dirs;
  TableId restored_away_index_id;
  // CREATE INDEX can return before a follower applies the change metadata op that adds the index.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        index_dirs.clear();
        for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
          auto tablet = VERIFY_RESULT(peer->shared_tablet());
          for (const auto& vector_index : *tablet->vector_indexes().List()) {
            auto table_info =
                VERIFY_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
            if (table_info->table_name == kEarlierDroppedIndexName) {
              continue;
            }
            const bool restored_away = table_info->table_name == kRestoredAwayIndexName;
            if (restored_away) {
              restored_away_index_id = vector_index->table_id();
            }
            index_dirs[restored_away].push_back(vector_index->path());
          }
        }
        if (index_dirs[false].size() != index_dirs[true].size()) {
          LOG(INFO) << "Found " << index_dirs[true].size() << " replicas of "
                    << kRestoredAwayIndexName << ", expected " << index_dirs[false].size();
          return false;
        }
        return true;
      },
      30s * kTimeMultiplier, "All replicas host the restored away index"));
  ASSERT_FALSE(restored_away_index_id.empty());

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_OK(wait_hidden(restored_away_index_id));
  // Followers apply the restore, which replaces the index storage dirs, after it returns.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));

  // The restore replaced the storages, so count the chunks it left. A directory the master already
  // removed from the tablets counts as unchanged.
  auto count_chunks = [](const std::string& dir) -> Result<std::optional<size_t>> {
    if (!Env::Default()->FileExists(dir)) {
      return std::nullopt;
    }
    auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
    return std::ranges::count_if(children, [](const std::string& name) {
      return name.starts_with("vectorindex_");
    });
  };
  std::unordered_map<std::string, std::optional<size_t>> chunks_before;
  for (const auto& [_, dirs] : index_dirs) {
    for (const auto& dir : dirs) {
      chunks_before[dir] = ASSERT_RESULT(count_chunks(dir));
    }
  }

  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  for (const auto& dir : index_dirs[false]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    ASSERT_TRUE(chunks_after.has_value()) << dir;
    EXPECT_GT(*chunks_after, chunks_before[dir].value_or(0)) << dir;
  }
  for (const auto& dir : index_dirs[true]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    LOG(INFO) << "Restored away index dir " << dir << " chunks before: "
              << AsString(chunks_before[dir]) << ", after: " << AsString(chunks_after);
    EXPECT_EQ(chunks_after, chunks_before[dir]) << dir;
  }
}

// The bootstrap variant of #33276: restoring past the ADD COLUMN that introduced the vector column
// leaves the tablet heap schema without the column, while the vector index stays registered on the
// tablet and has lost its chunks (the restored checkpoint predates the index). The next bootstrap
// launches a backfill for the chunkless index, and the backfill projects the vector column out of
// the live schema, so DoCreateIndex must skip such an index at tablet open instead of letting the
// backfill read out of bounds.
TEST_P(PgVectorIndexColocationOnlyTest, SnapshotScheduleRestoreBeforeVectorColumn) {
  constexpr size_t kNumRows = 16;

  // The suite's quick-split setup lets the indexed table split before the index exists, and the
  // restore then reverts to the pre-split parent, which never hosted the index at all.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // The read below runs against a table that has no vector index in the catalog, so it is a plain
  // scan while the tablets may still host the index.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  // The table starts without the vector column, so MakeTable does not fit here.
  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  std::string create_suffix;
  if (IsColocated()) {
    create_suffix = " WITH (COLOCATED = 1)";
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  } else {
    create_suffix = " SPLIT INTO 1 TABLETS";
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test (id bigserial PRIMARY KEY)$0", create_suffix));

  // The rows must predate the restore target: they are what the backfill scans after the restore.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0)", i));
  }
  ASSERT_OK(conn.CommitTransaction());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN embedding vector(3)"));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '[$0, 0, 0]' WHERE id = $0", i));
  }
  ASSERT_OK(CreateIndex(conn));

  // The restore drops the index from the catalog, and CleanupHiddenObjects then unregisters it
  // from the tablets, typically before the restart below gets to bootstrap. The removal is a
  // replicated ChangeMetadata operation, so blocking the RPC on the master is what keeps the
  // restarted tserver from replaying it during bootstrap.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"SnapshotScheduleRestoreBeforeVectorColumn::Bootstrapped",
       "AsyncRemoveTableFromTablet::SendRequest"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, hybrid_time));

  // Bootstrap is what launches backfills for indexes that have no chunks. Restart a tserver that
  // does not host PG, to keep the connection above usable; the master has a task thread parked on
  // the sync point above, so it has to stay up.
  auto* target_ts = cluster_->mini_tablet_server(kPgTsIndex == 0 ? 1 : 0);
  ASSERT_OK(target_ts->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  TEST_SYNC_POINT("SnapshotScheduleRestoreBeforeVectorColumn::Bootstrapped");

  ASSERT_EQ(
      kNumRows,
      make_unsigned(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test"))));
}

// A restore to before the ADD COLUMN of a vector column, from a snapshot taken after the index on
// that column was created, brings back the index storage while the restored schema lacks the
// column, so the tablets skip the index. Removing the index from the tablets must still destroy
// that storage: nothing knows about it once the index is gone from the tablet metadata (#35063).
// With with_kept_index, another vector index, which the restore keeps, must keep its storage.
// Without it, the skipped index is the only vector index of the tablet.
void PgVectorIndexColocationOnlyTest::TestSkippedIndexStorageDestroyedOnRemove(
    bool with_kept_index) {
  constexpr size_t kNumRows = 16;
  const std::string kKeptIndexName = "kept_vi";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  std::string create_suffix;
  if (IsColocated()) {
    create_suffix = " WITH (COLOCATED = 1)";
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  } else {
    create_suffix = " SPLIT INTO 1 TABLETS";
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, kept_embedding vector(3))$0", create_suffix));
  if (with_kept_index) {
    ASSERT_OK(CreateIndex(conn, kKeptIndexName, std::nullopt, "kept_embedding"));
  }

  // The interval is long enough for the restore to find no snapshot covering restore_at, so it
  // takes a new one, which includes the index storage.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));
  auto restore_at = cluster_->mini_master(0)->Now();

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN embedding vector(3)"));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test (id, kept_embedding, embedding) VALUES ($0, '[$0, 0, 0]', '[$0, 0, 0]')",
        i));
  }
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  // Storage dirs of the replicas of each index, by index name.
  std::unordered_map<std::string, std::vector<std::string>> index_dirs;
  TableId kept_index_id;
  TableId skipped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info =
          ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      if (table_info->table_name == kKeptIndexName) {
        kept_index_id = vector_index->table_id();
      } else {
        skipped_index_id = vector_index->table_id();
      }
      index_dirs[table_info->table_name].push_back(vector_index->path());
    }
  }
  const auto& kept_dirs = index_dirs[kKeptIndexName];
  const auto& skipped_dirs = index_dirs[kVectorIndexName];
  ASSERT_FALSE(skipped_dirs.empty());
  ASSERT_EQ(kept_dirs.size(), with_kept_index ? skipped_dirs.size() : 0);

  // Depending on whether the snapshot taken by the restore retains it, the master removes the
  // hidden index from the tablets right after the restore or once the schedule is deleted below.
  // Hold the removal to check the state the restore leaves.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"SkippedIndexStorageDestroyedOnRemove::Restored",
       "AsyncRemoveTableFromTablet::SendRequest"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), kept_dirs.size());
  for (const auto& peer : peers) {
    auto vector_indexes = ASSERT_RESULT(peer->shared_tablet())->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);
    ASSERT_EQ(vector_indexes->front()->table_id(), kept_index_id);
  }
  for (const auto& dir : skipped_dirs) {
    ASSERT_TRUE(Env::Default()->FileExists(dir)) << dir;
  }
  TEST_SYNC_POINT("SkippedIndexStorageDestroyedOnRemove::Restored");

  ASSERT_OK(snapshot_util.DeleteSchedule(schedule_id));
  ASSERT_OK(WaitFor(
      [this, &skipped_index_id]() -> Result<bool> {
        for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
          if (peer->tablet_metadata()->GetTableInfo(skipped_index_id).ok()) {
            return false;
          }
        }
        return true;
      },
      60s * kTimeMultiplier, "Index removed from the tablets"));
  // The removal deletes the storage only after it flushes the metadata without the index.
  ASSERT_OK(WaitFor(
      [&skipped_dirs] {
        auto* env = Env::Default();
        return std::ranges::none_of(skipped_dirs, [env](const auto& dir) {
          return env->FileExists(dir) || docdb::IsVectorIndexStorageMarkedRemoved(*env, dir);
        });
      },
      30s * kTimeMultiplier, "Skipped index storage and its removal mark deleted"));
  for (const auto& dir : kept_dirs) {
    EXPECT_TRUE(Env::Default()->FileExists(dir)) << dir;
  }
}

TEST_P(PgVectorIndexColocationOnlyTest, SkippedIndexStorageDestroyedOnRemove) {
  TestSkippedIndexStorageDestroyedOnRemove(/* with_kept_index= */ false);
}

TEST_P(PgVectorIndexColocationOnlyTest, SkippedIndexStorageDestroyedOnRemoveWithKeptIndex) {
  TestSkippedIndexStorageDestroyedOnRemove(/* with_kept_index= */ true);
}

// Cancelling CREATE INDEX removes the index while its backfill is still inserting. Neither the
// cancel nor the removal may wait for the rest of the backfill: the removal is applied by Raft, so
// waiting there blocks the tablet and the heartbeats of its tservers (GH#38073). The removal does
// not wait for the destroy of the index storage either, which waits for the queued inserts to skip
// their vectors.
TEST_P(PgVectorIndexColocationOnlyTest, CancelCreateIndexDuringBackfill) {
  constexpr size_t kNumRows = 2000;
  // With one insert thread and one vector per task, each replica needs kNumRows * kInsertDelay to
  // backfill, far longer than kRemoveTimeout.
  constexpr auto kInsertDelay = 50ms;
  const auto kRemoveTimeout = 20s * kTimeMultiplier;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_task_size) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test SELECT g, ('[' || g || ',' || g || ',' || g || ']')::vector "
      "FROM generate_series(1, $0) g", kNumRows));

  CountDownLatch destroys_held{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSMInsertTask::DoInsert:Entry", [kInsertDelay](void*) {
    std::this_thread::sleep_for(kInsertDelay);
  });
  sync_point->SetCallBack(
      "VectorLSM::CompleteShutdown:RegistriesStopped", [&destroys_held](void*) {
    destroys_held.Wait();
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &destroys_held] {
    destroys_held.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  std::atomic<int32_t> backend_pid{0};
  TestThreadHolder threads;
  threads.AddThreadFunctor([this, &backend_pid] {
    auto index_conn = ASSERT_RESULT(Connect());
    backend_pid = ASSERT_RESULT(index_conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
    ASSERT_NOK_STR_CONTAINS(CreateIndex(index_conn), "canceling statement due to user request");
  });
  ASSERT_OK(WaitForVectorIndexBackfillsStarted(cluster_->num_tablet_servers()));
  const auto index_table_id = ListVectorIndexes(cluster_.get()).front()->table_id();
  std::vector<std::string> storage_dirs;
  for (const auto& vector_index : ListVectorIndexes(cluster_.get())) {
    storage_dirs.push_back(vector_index->path());
  }
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  ASSERT_EQ(storage_dirs.size(), peers.size());

  const auto start = MonoTime::Now();
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
      Format("SELECT pg_cancel_backend($0)", backend_pid.load()))));
  // The rollback of the cancelled CREATE INDEX removes the index from the tablets.
  threads.JoinAll();
  ASSERT_OK(WaitFor([&peers, &index_table_id] {
    for (const auto& peer : peers) {
      if (peer->tablet_metadata()->GetTableInfo(index_table_id).ok()) {
        return false;
      }
    }
    return true;
  }, kRemoveTimeout, "Index removed from tablets"));
  const auto elapsed = MonoTime::Now() - start;
  LOG(INFO) << "Index removed in " << elapsed;
  ASSERT_LT(elapsed, MonoDelta(kRemoveTimeout));

  destroys_held.CountDown();
  auto* env = Env::Default();
  ASSERT_OK(WaitFor([env, &storage_dirs] {
    for (const auto& dir : storage_dirs) {
      if (env->DirExists(dir) || docdb::IsVectorIndexStorageMarkedRemoved(*env, dir)) {
        return false;
      }
    }
    return true;
  }, kRemoveTimeout, "Index storages and their removal marks deleted"));
  LOG(INFO) << "Index storages deleted in " << MonoTime::Now() - start;

  sync_point->ClearAllCallBacks();
  ASSERT_OK(CreateIndex(conn));
}

// The removal of an index may delete its storage only once the tablet metadata no longer lists the
// index: after a crash in between, bootstrap would open the index from its partly deleted storage
// and fail the tablet.
TEST_P(PgVectorIndexColocationOnlyTest, RemoveIndexDeletesStorageAfterMetadataFlush) {
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(100));
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  const auto index_table_id = ListVectorIndexes(cluster_.get()).front()->table_id();

  std::mutex mutex;
  // Storage dir of the index, by the metadata of each replica not flushed without the index yet.
  std::unordered_map<tablet::RaftGroupMetadata*, std::string> storage_dirs;
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    storage_dirs[peer->tablet_metadata().get()] =
        tablet->vector_indexes().IndexForTable(index_table_id)->path();
  }
  ASSERT_EQ(storage_dirs.size(), peers.size());
  std::atomic<size_t> checked{0};
  std::atomic<size_t> deleted_before_flush{0};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "RaftGroupMetadata::Flush",
      [&mutex, &storage_dirs, &index_table_id, &checked, &deleted_before_flush](void* arg) {
    auto* metadata = static_cast<tablet::RaftGroupMetadata*>(arg);
    std::string dir;
    {
      std::lock_guard lock(mutex);
      auto it = storage_dirs.find(metadata);
      if (it == storage_dirs.end() || metadata->HasTable(index_table_id)) {
        return;
      }
      dir = it->second;
      storage_dirs.erase(it);
    }
    // Gives a destroy scheduled before the flush the time to delete the storage.
    auto status = WaitFor(
        [&dir] { return !Env::Default()->DirExists(dir); }, 2s * kTimeMultiplier,
        "Storage deleted before the metadata flush");
    if (status.ok()) {
      LOG(INFO) << "Deleted before the metadata flush: " << dir;
      ++deleted_before_flush;
    }
    ++checked;
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(WaitFor([&checked, &peers] {
    return checked.load() == peers.size();
  }, 30s * kTimeMultiplier, "Metadata flushes without the index checked"));
  ASSERT_EQ(deleted_before_flush.load(), 0);
}

// Re-adding an index while the destroy of its earlier instance is pending, as a PITR restore can do
// (see TabletVectorIndexes::DoCreateIndex), must wait for the destroy, which would otherwise delete
// the storage the re-added index uses. The tablet metadata must not list the index meanwhile: a
// flush of it followed by a crash would leave a listed index with partly deleted storage.
TEST_P(PgVectorIndexColocationOnlyTest, ReAddIndexWaitsForPendingDestroy) {
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(100));
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  const auto index_table_id = ListVectorIndexes(cluster_.get()).front()->table_id();
  const auto storage_dir = tablet->vector_indexes().IndexForTable(index_table_id)->path();
  auto index_table = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index_table_id));
  tablet::TableInfoPB index_table_pb;
  index_table->ToPB(&index_table_pb);

  CountDownLatch destroys_held{1};
  std::atomic<bool> re_add_waiting{false};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::CompleteShutdown:RegistriesStopped", [&destroys_held](void*) {
    destroys_held.Wait();
  });
  sync_point->SetCallBack(
      "TabletVectorIndexes::PrepareAddIndex", [&re_add_waiting](void*) {
    re_add_waiting = true;
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &destroys_held] {
    destroys_held.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(WaitFor([&tablet, &index_table_id] {
    return !tablet->metadata()->GetTableInfo(index_table_id).ok();
  }, 30s * kTimeMultiplier, "Index removed from the tablet"));

  std::atomic<bool> created{false};
  TestThreadHolder threads;
  // As the apply of a re-sent AddTableToTablet does.
  threads.AddThreadFunctor([&tablet, &index_table_pb, &index_table, &created] {
    ASSERT_OK(tablet->AddTable(
        index_table_pb, tablet->metadata()->TEST_LastAppliedChangeMetadataOperationOpId(),
        index_table->hybrid_time));
    created = true;
  });
  // Before the thread is joined.
  auto release_destroys = ScopeExit([&destroys_held] { destroys_held.CountDown(); });
  ASSERT_OK(WaitFor(
      [&re_add_waiting] { return re_add_waiting.load(); }, 30s * kTimeMultiplier,
      "Re-add waits for the destroy"));
  ASSERT_NOK(WaitFor([&tablet, &index_table_id, &created] {
    return created.load() || tablet->metadata()->HasTable(index_table_id);
  }, 1s * kTimeMultiplier, "Re-add done or index listed"));

  destroys_held.CountDown();
  threads.JoinAll();
  ASSERT_TRUE(created.load());
  ASSERT_NE(tablet->vector_indexes().IndexForTable(index_table_id), nullptr);
  ASSERT_TRUE(Env::Default()->DirExists(storage_dir)) << storage_dir;
}

// A crash after the removal of an index flushed the tablet metadata, but before the destroy deleted
// the storage, leaves the storage whole under its name. A replayed re-add of the index must not
// reuse it: it holds the chunks and the backfill state of the removed index.
TEST_P(PgVectorIndexColocationOnlyTest, ReAddIndexDeletesStorageLeftByRemoval) {
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(100));
  ASSERT_OK(cluster_->FlushTablets());
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  const auto index_table_id = ListVectorIndexes(cluster_.get()).front()->table_id();
  const auto storage_dir = tablet->vector_indexes().IndexForTable(index_table_id)->path();
  auto index_table = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index_table_id));
  tablet::TableInfoPB index_table_pb;
  index_table->ToPB(&index_table_pb);

  CountDownLatch destroys_held{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::CompleteShutdown:RegistriesStopped", [&destroys_held](void*) {
    destroys_held.Wait();
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &destroys_held] {
    destroys_held.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(WaitFor([&tablet, &index_table_id] {
    return !tablet->metadata()->GetTableInfo(index_table_id).ok();
  }, 30s * kTimeMultiplier, "Index removed from the tablet"));

  // The storage as the crash leaves it.
  auto* env = Env::Default();
  ASSERT_TRUE(docdb::IsVectorIndexStorageMarkedRemoved(*env, storage_dir)) << storage_dir;
  const auto left_dir = storage_dir + ".left";
  ASSERT_OK(env->CreateDir(left_dir));
  for (const auto& file : ASSERT_RESULT(env->GetChildren(storage_dir, ExcludeDots::kTrue))) {
    ASSERT_OK(env_util::CopyFile(
        env, JoinPathSegments(storage_dir, file), JoinPathSegments(left_dir, file)));
  }
  destroys_held.CountDown();
  ASSERT_OK(WaitFor(
      [env, &storage_dir] { return !docdb::IsVectorIndexStorageMarkedRemoved(*env, storage_dir); },
      30s * kTimeMultiplier, "Storage destroyed"));
  ASSERT_FALSE(env->DirExists(storage_dir)) << storage_dir;
  ASSERT_OK(env->RenameFile(left_dir, storage_dir));
  ASSERT_OK(docdb::MarkRemovedVectorIndexStorage(*env, storage_dir));
  const auto stale_file = JoinPathSegments(storage_dir, "stale");
  ASSERT_OK(WriteStringToFile(env, "stale", stale_file));

  // As the replay of a re-sent AddTableToTablet does.
  ASSERT_OK(tablet->AddTable(
      index_table_pb, tablet->metadata()->TEST_LastAppliedChangeMetadataOperationOpId(),
      index_table->hybrid_time));
  ASSERT_FALSE(env->FileExists(stale_file)) << stale_file;
  ASSERT_FALSE(docdb::IsVectorIndexStorageMarkedRemoved(*env, storage_dir)) << storage_dir;
  ASSERT_NE(tablet->vector_indexes().IndexForTable(index_table_id), nullptr);
}

// A crash after the removal of an index is applied, but before the tablet metadata is flushed
// without the index, makes bootstrap replay the removal, which schedules the destroy of the
// storage. The startup sweep of unlisted storages must not delete the storage under that destroy.
TEST_P(PgVectorIndexColocationOnlyTest, StartupWaitsForDestroyReplayedByBootstrap) {
  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(100));
  ASSERT_OK(cluster_->FlushTablets());

  // The superblocks and the storages as the crash leaves them.
  auto* env = Env::Default();
  std::unordered_map<std::string, tablet::RaftGroupReplicaSuperBlockPB> superblocks;
  std::vector<std::string> storage_dirs;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    const auto& meta = *peer->tablet_metadata();
    auto path = ASSERT_RESULT(meta.fs_manager()->GetRaftGroupMetadataPath(peer->tablet_id()));
    ASSERT_OK(meta.ReadSuperBlockFromDisk(&superblocks[path]));
    for (const auto& dir : meta.vector_index_dirs()) {
      const auto copy_dir = dir + ".copy";
      ASSERT_OK(env->CreateDir(copy_dir));
      for (const auto& file : ASSERT_RESULT(env->GetChildren(dir, ExcludeDots::kTrue))) {
        ASSERT_OK(env_util::CopyFile(
            env, JoinPathSegments(dir, file), JoinPathSegments(copy_dir, file)));
      }
      storage_dirs.push_back(dir);
    }
  }
  ASSERT_EQ(storage_dirs.size(), cluster_->num_tablet_servers());
  auto storages_destroyed = [env, &storage_dirs] {
    return std::ranges::none_of(storage_dirs, [env](const auto& dir) {
      return env->DirExists(dir) || docdb::IsVectorIndexStorageMarkedRemoved(*env, dir);
    });
  };

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(WaitFor(storages_destroyed, 30s * kTimeMultiplier, "Index storages destroyed"));

  DisableFlushOnShutdown(*cluster_, true);
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->mini_tablet_server(i)->Shutdown();
  }
  for (const auto& [path, superblock] : superblocks) {
    ASSERT_OK(pb_util::WritePBContainerToPath(
        env, path, superblock, pb_util::OVERWRITE, pb_util::SYNC));
  }
  for (const auto& dir : storage_dirs) {
    ASSERT_OK(env->RenameFile(dir + ".copy", dir));
  }

  std::atomic<size_t> destroys_held{0};
  CountDownLatch release_destroys{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::CompleteShutdown:RegistriesStopped", [&destroys_held, &release_destroys](void*) {
    ++destroys_held;
    release_destroys.Wait();
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &release_destroys] {
    release_destroys.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  TestThreadHolder threads;
  // Each in its own thread, since Start waits for the tablets to open.
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    threads.AddThreadFunctor([this, i] {
      ASSERT_OK(cluster_->mini_tablet_server(i)->Start());
    });
  }
  // Before the threads are joined.
  auto release = ScopeExit([&release_destroys] { release_destroys.CountDown(); });
  ASSERT_OK(WaitFor(
      [&destroys_held, &storage_dirs] { return destroys_held.load() == storage_dirs.size(); },
      60s * kTimeMultiplier, "Bootstrap replays the removal"));
  ASSERT_NOK(WaitFor(
      [env, &storage_dirs] {
        return std::ranges::any_of(
            storage_dirs, [env](const auto& dir) { return !env->DirExists(dir); });
      },
      3s * kTimeMultiplier, "Storage deleted under its pending destroy"));

  release_destroys.CountDown();
  threads.JoinAll();
  ASSERT_OK(WaitFor(storages_destroyed, 30s * kTimeMultiplier, "Index storages destroyed"));
}

// A restore can put back the storage of an index whose removal left the mark, e.g. when its
// destroy failed. The restore must delete the mark, or a re-sent AddTableToTablet would delete the
// restored storage.
TEST_P(PgVectorIndexColocationOnlyTest, SnapshotRestoreDeletesRemovalMark) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  auto* env = Env::Default();
  std::vector<std::string> storage_dirs;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    for (auto& dir : peer->tablet_metadata()->vector_index_dirs()) {
      ASSERT_OK(docdb::MarkRemovedVectorIndexStorage(*env, dir));
      storage_dirs.push_back(std::move(dir));
    }
  }
  ASSERT_FALSE(storage_dirs.empty());

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));
  // The restore can be done before the followers apply it.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));

  for (const auto& dir : storage_dirs) {
    ASSERT_TRUE(env->DirExists(dir)) << dir;
    ASSERT_FALSE(docdb::IsVectorIndexStorageMarkedRemoved(*env, dir)) << dir;
  }
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

}  // namespace yb::pgwrapper
