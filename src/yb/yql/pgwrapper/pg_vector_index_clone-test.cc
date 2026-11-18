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

#include <algorithm>

#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/consensus/log.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(vector_index_store_payload);
DECLARE_string(vector_index_backend);
DECLARE_uint32(vector_index_tenant_hash_group_and_range_components);
DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::pgwrapper {

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexColocationOnlyTest);

// A clone starts from a snapshot of the source without the source's log, so rows the source's
// vector index holds only in memory when that snapshot is taken must be in the snapshot. See
// issue #34759.
TEST_P(PgVectorIndexColocationOnlyTest, CloneKeepsUnflushedVectorIndexRows) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 16;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Each snapshot op flushes the vector indexes after its checkpoint, when it moves the flushed
  // frontier, so only a snapshot taken after the rows below shows the bug. Create the schedule,
  // and with it the first snapshot, before them.
  ASSERT_OK(CreateSourceSnapshotSchedule());

  // Non-transactional writes leave nothing in the intents DB, whose flush filter would otherwise
  // force a vector index flush.
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  for (size_t i = kNumRows + 1; i <= 2 * kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test (id, embedding) VALUES ($0, '$1')", i, AsString(Vector(i))));
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListTableVectorIndexes(ASSERT_RESULT(FindTableId(kSourceDb, "test")));
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  ASSERT_OK(CloneSourceDbWithExistingSchedule());

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));
}

// After clone, the child's TS-side index_map must list the cloned vector index, not the source
// Restore does not rewrite those IDs for vector indexes.
TEST_P(PgVectorIndexColocationOnlyTest, CloneRemapsVectorIndexMap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 8;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));

  const auto source_table_id = ASSERT_RESULT(FindTableId(kSourceDb, "test"));
  const auto source_index_id = ASSERT_RESULT(FindTableId(kSourceDb, kVectorIndexName));

  ASSERT_OK(CloneSourceDb());

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  const auto clone_index_id = ASSERT_RESULT(FindTableId(kCloneDb, kVectorIndexName));
  ASSERT_NE(clone_table_id, source_table_id);
  ASSERT_NE(clone_index_id, source_index_id);

  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), clone_table_id);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(clone_table_id));
    std::string index_ids;
    for (const auto& [id, _] : *table_info->index_map) {
      if (!index_ids.empty()) {
        index_ids += ", ";
      }
      index_ids += id;
    }
    ASSERT_NE(table_info->index_map->find(clone_index_id), table_info->index_map->end())
        << "cloned tablet " << peer->tablet_id() << " index_map: " << index_ids;
    ASSERT_EQ(table_info->index_map->find(source_index_id), table_info->index_map->end())
        << "cloned tablet " << peer->tablet_id()
        << " still has source index " << source_index_id
        << " index_map: " << index_ids;
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(ASSERT_RESULT(clone_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test")), kNumRows);
  ASSERT_EQ(
      ASSERT_RESULT(clone_conn.FetchRow<int64_t>(Format(
          "SELECT id FROM test ORDER BY $0 LIMIT 1", DistanceToQuery(Vector(1))))),
      1);
}

// The snapshot a clone restores holds the storage of every vector index of the source tablet,
// including indexes created after the clone time. A storage dir is named by index oid, which the
// clone keeps, so a later index of the clone that gets such an oid would load the source index's
// chunks, and DROP DATABASE of the clone would leave the dir behind. See issue #35006.
TEST_P(PgVectorIndexColocationOnlyTest, CloneDropsStorageOfIndexNotInClone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  constexpr size_t kNumRows = 8;
  const std::string kLaterIndexName = "later_vi";

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));

  // No scheduled snapshot after the first one, so the clone takes a new snapshot, which holds the
  // storage of the later index.
  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());
  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, kSourceDb, client::WaitSnapshot::kTrue, 30min, 1h));

  const auto clone_time = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT (extract(epoch FROM clock_timestamp()) * 1000000)::bigint"));
  ASSERT_OK(CreateIndex(conn, kLaterIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto admin_conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(admin_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kCloneDb, kSourceDb, clone_time));

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  // The clone waits only for the leaders to apply the restore.
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), clone_table_id);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    const auto& meta = *peer->tablet_metadata();
    for (const auto& info : meta.GetAllColocatedVectorIndexes()) {
      ASSERT_NE(info->table_name, kLaterIndexName);
    }
    const auto listed_dirs = meta.vector_index_dirs();
    ASSERT_EQ(listed_dirs.size(), 1) << peer->tablet_id();

    const auto prefix =
        docdb::GetStorageDir(BaseName(meta.rocksdb_dir()), docdb::kVectorIndexDirPrefix);
    const auto parent_dir = DirName(meta.rocksdb_dir());
    std::vector<std::string> dirs_on_disk;
    for (const auto& child :
         ASSERT_RESULT(Env::Default()->GetChildren(parent_dir, ExcludeDots::kTrue))) {
      if (child.starts_with(prefix)) {
        dirs_on_disk.push_back(JoinPathSegments(parent_dir, child));
      }
    }
    ASSERT_EQ(dirs_on_disk, listed_dirs) << peer->tablet_id();
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, kNumRows)).size(), kNumRows);
}

// A clone restores the source snapshot onto tablets of a new Raft group, the same as a backup
// restored into another universe, e.g. an xCluster DR target. The restored vector index chunks
// carry OpIds of the source Raft group, far above the restore OpId of the new one. Unless the
// restore brings the vector index flushed OpId back to the restore OpId, bootstrap treats the
// clone's own writes as already flushed into the vector index, and they disappear from it after
// an ungraceful restart. See issue #34763.
TEST_P(PgVectorIndexColocationOnlyTest, CloneKeepsNewRowsAcrossRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr auto kSourceDb = "source_db";
  constexpr auto kCloneDb = "clone_db";
  // Each source row is a separate Raft op, so the source OpIds end up well above the ones the
  // clone writes below reach.
  constexpr size_t kNumSourceRows = 100;
  constexpr int64_t kNumSourceLeaderChanges = 4;
  constexpr size_t kNumCloneTxnRows = 10;
  constexpr size_t kNumCloneRows = 20;
  dimensions_ = 3;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto admin_conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(admin_conn.ExecuteFormat(
      "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));

  auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))$0",
      IsColocated() ? " WITH (COLOCATED = 1)" : " SPLIT INTO 1 TABLETS"));
  ASSERT_OK(CreateIndex(conn));

  auto find_table_id = [this](const std::string& db, const std::string& name) -> Result<TableId> {
    for (const auto& table : VERIFY_RESULT(client_->ListTables())) {
      if (table.has_table() && table.namespace_name() == db && table.table_name() == name) {
        return table.table_id();
      }
    }
    return STATUS_FORMAT(NotFound, "Didn't find table $0.$1", db, name);
  };
  const auto source_table_id = ASSERT_RESULT(find_table_id(kSourceDb, "test"));

  // The vector index flushed OpId is the maximum over its chunks, compared term first, so a
  // restore OpId of a higher term than the source chunks would hide the bug. Raise the source term
  // above any the clone's new Raft group reaches by its restore.
  for (int64_t i = 0; i != kNumSourceLeaderChanges; ++i) {
    auto leaders = ListTableActiveTabletLeadersPeers(cluster_.get(), source_table_id);
    ASSERT_EQ(leaders.size(), 1);
    const auto& leader = leaders.front();
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), source_table_id)) {
      if (peer->permanent_uuid() != leader->permanent_uuid()) {
        ASSERT_OK(TransferLeadership(cluster_.get(), leader->tablet_id(), peer->permanent_uuid()));
        break;
      }
    }
  }

  ASSERT_OK(InsertRows(
      conn, 1, kNumSourceRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), source_table_id)) {
    auto state = ASSERT_RESULT(ASSERT_RESULT(peer->shared_tablet())->GetPersistentState());
    ASSERT_FALSE(state.vector_indexes.empty());
    for (const auto& vector_index : state.vector_indexes) {
      ASSERT_GT(vector_index.op_id.term, kNumSourceLeaderChanges) << state.ToString();
    }
  }

  // Retention must outlive the clone, which takes minutes under sanitizers.
  ASSERT_OK(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, kSourceDb,
      client::WaitSnapshot::kTrue, 10s * kTimeMultiplier, 1h));
  ASSERT_OK(admin_conn.ExecuteFormat("CREATE DATABASE $0 TEMPLATE $1", kCloneDb, kSourceDb));

  const auto clone_table_id = ASSERT_RESULT(find_table_id(kCloneDb, "test"));

  // Rows written to the clone, in a transaction and through the single-shard fast path, stay in
  // the vector index mutable chunk.
  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertRows(clone_conn, kNumSourceRows + 1, kNumSourceRows + kNumCloneTxnRows));
  ASSERT_OK(InsertRows(
      clone_conn, kNumSourceRows + kNumCloneTxnRows + 1, kNumSourceRows + kNumCloneRows,
      /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  constexpr size_t kTotalRows = kNumSourceRows + kNumCloneRows;
  // Ids of the clone rows that a search with the row's own vector does not return.
  auto missing_from_index = [this]() -> Result<std::vector<size_t>> {
    auto conn = VERIFY_RESULT(ConnectToDB(kCloneDb));
    RETURN_NOT_OK(conn.Execute("SET enable_seqscan = off"));
    std::vector<size_t> result;
    for (size_t id = 1; id <= kTotalRows; ++id) {
      auto found = VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT id FROM test ORDER BY $0 LIMIT 1", DistanceToQuery(Vector(id)))));
      if (static_cast<size_t>(found) != id) {
        result.push_back(id);
      }
    }
    return result;
  };
  ASSERT_EQ(ASSERT_RESULT(missing_from_index()), std::vector<size_t>());

  auto log_persistent_state = [this, &clone_table_id](const char* when) -> Status {
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), clone_table_id)) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      LOG(INFO) << "Clone tablet " << peer->tablet_id() << " " << when << ": "
                << VERIFY_RESULT(tablet->GetPersistentState()).ToString();
    }
    return Status::OK();
  };
  ASSERT_OK(log_persistent_state("before restart"));

  DisableFlushOnShutdown(*cluster_, true);
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_OK(log_persistent_state("after restart"));

  auto check_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(check_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test")), kTotalRows);
  ASSERT_EQ(ASSERT_RESULT(missing_from_index()), std::vector<size_t>());

  // The restore must not leave a vector index flushed OpId from the source Raft group.
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), clone_table_id)) {
    auto state = ASSERT_RESULT(ASSERT_RESULT(peer->shared_tablet())->GetPersistentState());
    ASSERT_FALSE(state.vector_indexes.empty());
    for (const auto& vector_index : state.vector_indexes) {
      ASSERT_LE(vector_index.op_id.index, state.regular.index) << state.ToString();
    }
  }
}

// A vector index only takes intent applies committed after its hybrid time. The clone used to
// leave that time invalid on the target tablet, so rows written transactionally into the clone
// never reached its vector index (#34767).
TEST_P(PgVectorIndexColocationOnlyTest, CloneIndexesNewTransactionalWrites) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 8;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(CloneSourceDb());

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));

  // After a restart the index takes the hybrid time from the superblock, so writes keep reaching
  // it.
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, 2 * kNumRows + 1, 3 * kNumRows));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 3 * kNumRows));
}

// CREATE INDEX waits only for the tablet leader's backfill (#34899), so a snapshot can hold a
// replica's vector index with its backfill unfinished, and the clone resumes that backfill. The
// resumed backfill stamps the index table's op id into flushed frontiers, so that op id must not
// come from the source tablet's log: it is ahead of the clone's, and bootstrap would skip the
// clone's writes as already flushed.
TEST_P(PgVectorIndexColocationOnlyTest, CloneResumesIncompleteBackfill) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  // Several chunks, so that a backfill blocked after its first one is left unfinished.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = 8;

  // One write per row puts the source tablet's log well ahead of the clone's.
  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  // Keep a replica down while the index is created, so that its backfill only starts after
  // CREATE INDEX returns. Not the PG tserver, which the connections go through.
  auto* lagging_ts = cluster_->mini_tablet_server(kPgTsIndex == 0 ? 1 : 0);
  const auto lagging_uuid = lagging_ts->server()->permanent_uuid();

  // Also put the source tablet's term past any the clone's new tablets reach, so that its OpIds,
  // which the clone operation and the restored index table op id carry, sort above the clone's
  // own whatever the elections.
  // The leader stays off the replica about to go down, which CREATE INDEX would wait on.
  const auto source_table_id = ASSERT_RESULT(FindTableId(kSourceDb, "test"));
  ASSERT_OK(RaiseTabletTerm(source_table_id, 5, lagging_uuid));
  lagging_ts->Shutdown();
  ASSERT_OK(CreateIndex(conn));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  ASSERT_OK(lagging_ts->RestartStoppedServer(tserver::WaitTabletsBootstrapped::kTrue));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (const auto& index : ListTableVectorIndexes(source_table_id, lagging_uuid)) {
      if (index->metrics().backfill_inserted_entries->value() > 0) {
        return true;
      }
    }
    return false;
  }, 60s * kTimeMultiplier, "Lagging replica backfilled its first batch"));

  ASSERT_OK(CloneSourceDb());
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
      false;

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto indexes = ListTableVectorIndexes(clone_table_id);
    if (indexes.size() != cluster_->num_tablet_servers()) {
      return false;
    }
    for (const auto& index : indexes) {
      if (!VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Clone backfills done"));
  {
    // The clone resumed the unfinished backfill rather than starting over: it inserted some rows,
    // but fewer than the table has. Scoped: an index must not outlive its tablet, which the
    // restart below replaces.
    auto lagging_clone_indexes = ListTableVectorIndexes(clone_table_id, lagging_uuid);
    ASSERT_EQ(lagging_clone_indexes.size(), 1);
    const auto backfilled =
        lagging_clone_indexes.front()->metrics().backfill_inserted_entries->value();
    ASSERT_GT(backfilled, 0);
    ASSERT_LT(backfilled, make_signed(kNumRows));
  }

  // The resumed backfill stamped the index table's op id into the flushed frontiers, so it must not
  // be ahead of the clone's own log.
  const auto clone_index_id = ASSERT_RESULT(FindTableId(kCloneDb, kVectorIndexName));
  for (const auto& peer : ListTableTabletPeers(cluster_.get(), clone_table_id)) {
    auto table_info = ASSERT_RESULT(peer->tablet_metadata()->GetTableInfo(clone_index_id));
    ASSERT_LE(table_info->op_id, peer->log()->GetLatestEntryOpId()) << peer->permanent_uuid();
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, kNumRows + 1, 2 * kNumRows));

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  std::string entries;
  auto status = WaitFor([&]() -> Result<bool> {
    auto indexes = ListTableVectorIndexes(clone_table_id);
    entries.clear();
    bool all_match = indexes.size() == cluster_->num_tablet_servers();
    for (const auto& index : indexes) {
      auto num_entries = VERIFY_RESULT(index->TotalEntries());
      entries += Format("$0: $1\n", index->ToString(), num_entries);
      all_match = all_match && num_entries == 2 * kNumRows;
    }
    return all_match;
  }, 30s * kTimeMultiplier, "Clone indexes hold every row");
  ASSERT_TRUE(status.ok()) << status << "\n" << entries;

  clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));
}

// Snapshot import keeps the snapshot's vector index options, so a snapshot of an index without a
// block-based backend (here: no backend set, as in indexes that predate the backend field) must be
// rejected before the import creates anything.
TEST_P(PgVectorIndexColocationOnlyTest, ImportRejectsDeprecatedBackend) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto schedule_snapshot = ASSERT_RESULT(snapshot_util.WaitScheduleSnapshot(schedule_id));
  auto snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(schedule_snapshot.id()));
  auto snapshots = ASSERT_RESULT(snapshot_util.ListSnapshots(
      snapshot_id, client::ListDeleted::kFalse, client::PrepareForBackup::kTrue));
  ASSERT_EQ(snapshots.size(), 1);
  auto snapshot = snapshots[0];

  size_t num_vector_indexes = 0;
  for (auto& backup_entry : *snapshot.mutable_backup_entries()) {
    auto& entry = *backup_entry.mutable_entry();
    if (entry.type() != master::SysRowEntryType::TABLE) {
      continue;
    }
    auto meta = ASSERT_RESULT(pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data()));
    if (!meta.has_index_info() || !meta.index_info().has_vector_idx_options()) {
      continue;
    }
    auto& options = *meta.mutable_index_info()->mutable_vector_idx_options();
    ASSERT_EQ(options.hnsw().backend(), HnswBackend::YB_HNSW_HNSWLIB);
    options.mutable_hnsw()->clear_backend();
    entry.set_data(meta.SerializeAsString());
    ++num_vector_indexes;
  }
  ASSERT_EQ(num_vector_indexes, 1);

  auto import_result = snapshot_util.StartImportSnapshot(snapshot);
  ASSERT_NOK(import_result);
  ASSERT_TRUE(import_result.status().IsNotSupported()) << import_result.status();
  ASSERT_STR_CONTAINS(import_result.status().ToString(), "no longer supported");
}

// ysql_dump recreates the table from the surviving columns, so DocDB assigns those columns dense
// ids. The ybhnsw index records that dense id. ImportSnapshot puts the source column ids back on
// the master catalog. Restore merges the snapshot superblock, which still has the source
// vector_idx_options, onto the tablet. Search has to use that id: the restored graph was built
// against it.
class PgVectorIndexBackupRestoreTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexBackupRestoreTest);

// A restored backup has none of the source's log, so rows the source's vector index holds only in
// memory when the backup's snapshot is taken must be in the snapshot. See issue #34759.
TEST_P(PgVectorIndexBackupRestoreTest, BackupKeepsUnflushedVectorIndexRows) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Backup and restore go through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_backup_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr int kNumRows = 16;

  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (id int PRIMARY KEY, v vector(3))"));
    ASSERT_OK(conn.Execute("CREATE INDEX v_idx ON t USING ybhnsw (v vector_l2_ops)"));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT i, ARRAY[i, 0, 0]::vector FROM generate_series(1, $0) i",
        kNumRows));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    ASSERT_OK(cluster_->FlushTablets());

    // Non-transactional writes leave nothing in the intents DB, whose flush filter would
    // otherwise force a vector index flush.
    ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
    for (int i = kNumRows + 1; i <= 2 * kNumRows; ++i) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t VALUES ($0, '[$0, 0, 0]')", i));
    }
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto ids = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM t ORDER BY v <-> '[0, 0, 0]' LIMIT $0", 4 * kNumRows)));
  std::ranges::sort(ids);
  std::vector<int32_t> expected(2 * kNumRows);
  std::iota(expected.begin(), expected.end(), 1);
  ASSERT_EQ(ids, expected);
}

TEST_P(PgVectorIndexBackupRestoreTest, IndexAfterDroppedColumn) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Restoring the snapshot superblock goes through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_restore_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr auto kTable = "t";
  constexpr auto kIndex = "v_idx";

  auto column_id = [this, kTable](
      const std::string& db, const std::string& column) -> Result<int32_t> {
    auto table = VERIFY_RESULT(client_->OpenTable(VERIFY_RESULT(GetTableId(db, kTable))));
    const auto& schema = table->schema();
    const auto& columns = schema.columns();
    for (size_t i = 0; i < columns.size(); ++i) {
      if (columns[i].name() == column) {
        return schema.ColumnId(i);
      }
    }
    return STATUS_FORMAT(NotFound, "Column $0 not found in $1.$2", column, db, kTable);
  };

  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id int PRIMARY KEY, embedding vector(3))", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN extra vector(3)", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN extra", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN v vector(3)", kTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 USING ybhnsw (v vector_l2_ops)", kIndex, kTable));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (7, '[1, 0, 0]', '[0, 1, 0]')", kTable));
  }
  const auto source_v_column_id = ASSERT_RESULT(column_id(kSourceDb, "v"));

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  const auto restored_v_column_id = ASSERT_RESULT(column_id(kRestoredDb, "v"));
  ASSERT_EQ(restored_v_column_id, source_v_column_id);
  auto index = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kIndex))));
  ASSERT_TRUE(index->index_info().is_vector_index());
  ASSERT_EQ(index->index_info().vector_idx_options().column_id(), source_v_column_id);

  // The tablet skips opening a vector index whose column id is not in the schema.
  size_t num_restored_indexes = 0;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      if (vector_index->table_id() != index->id()) {
        continue;
      }
      ++num_restored_indexes;
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index->id()));
      ASSERT_EQ(table_info->doc_read_context->vector_idx_options->column_id(), source_v_column_id);
    }
  }
  ASSERT_EQ(num_restored_indexes, 1);

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  // One row is otherwise a sequential scan plus a sort, which never opens the vector index.
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM $0 ORDER BY v <-> '[0, 1, 0]' LIMIT 1", kTable)));
  ASSERT_EQ(rows, (std::vector<int32_t>{7}));
}

// CREATE INDEX on restore takes the backend, store_payload and tenant components from the restore
// cluster's flags, but the restored chunk files were written with the source's. Restore has to
// apply the source options.
TEST_P(PgVectorIndexBackupRestoreTest, IndexAfterBackendAndPayloadFlagsChange) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Restoring the snapshot superblock goes through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_restore_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr auto kTable = "t";
  constexpr auto kIndex = "v_idx";
  constexpr int kNumRows = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 1;
  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (tenant int, id int, v vector(3), PRIMARY KEY (tenant, id))", kTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 USING ybhnsw (v vector_l2_ops)", kIndex, kTable));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT 1, i, ARRAY[i, 0, 0]::vector FROM generate_series(1, $1) i", kTable,
        kNumRows));
  }
  ASSERT_OK(cluster_->FlushTablets());

  auto source_index =
      ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kSourceDb, kIndex))));
  const auto source_options = source_index->index_info().vector_idx_options();
  ASSERT_TRUE(source_options.store_payload());
  ASSERT_EQ(source_options.tenant_hash_group_and_range_components(), 1U);
  ASSERT_EQ(source_options.hnsw().backend(), HnswBackend::YB_HNSW_HNSWLIB);

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "yb_hnsw_usearch";
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  auto check_options = [&source_options](const PgVectorIdxOptionsPB& options) {
    ASSERT_EQ(options.store_payload(), source_options.store_payload());
    ASSERT_EQ(
        options.tenant_hash_group_and_range_components(),
        source_options.tenant_hash_group_and_range_components());
    ASSERT_EQ(options.hnsw().backend(), source_options.hnsw().backend());
    ASSERT_EQ(options.id(), source_options.id());
  };

  auto index = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kIndex))));
  ASSERT_NO_FATALS(check_options(index->index_info().vector_idx_options()));
  auto table = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kTable))));
  const auto* index_in_table = ASSERT_RESULT(table->index_map().FindIndex(index->id()));
  ASSERT_NO_FATALS(check_options(index_in_table->vector_idx_options()));

  size_t num_restored_indexes = 0;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      if (vector_index->table_id() != index->id()) {
        continue;
      }
      ++num_restored_indexes;
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index->id()));
      ASSERT_NO_FATALS(check_options(*table_info->doc_read_context->vector_idx_options));
    }
  }
  ASSERT_EQ(num_restored_indexes, 1);

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM $0 ORDER BY v <-> '[0, 0, 0]' LIMIT 3", kTable)));
  ASSERT_EQ(rows, (std::vector<int32_t>{1, 2, 3}));
}

}  // namespace yb::pgwrapper
