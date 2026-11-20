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

#include <sys/stat.h>

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"
#include "yb/docdb/docdb_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(TEST_disable_apply_committed_transactions);
DECLARE_bool(TEST_disable_wal_retention_time);
DECLARE_bool(TEST_fail_vector_index_backfill);
DECLARE_bool(TEST_skip_process_apply);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int32(TEST_delay_init_tablet_peer_ms);
DECLARE_int32(TEST_pause_and_skip_apply_intents_task_loop_ms);
DECLARE_uint32(vector_index_backfill_retry_delay_ms);
DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;

} // namespace yb::tablet

namespace yb::pgwrapper {

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexTest);

void PgVectorIndexTest::TestRestart(
    tablet::FlushFlags flush_flags, NonTransactionalWrites non_transactional_writes) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  if (non_transactional_writes) {
    // Retained retryable requests pin the WAL and force replaying recent segments regardless of
    // flushed OpIds. The timeout is read at tablet bootstrap, so set it before the table exists.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 0;
    // Let WAL GC remove every segment that no storage needs.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;
  }
  auto conn = ASSERT_RESULT(MakeIndex());
  if (non_transactional_writes) {
    // Give vector indexes a flushed OpId above the first WAL entry, so bootstrap has to pick the
    // replay start from it. These vectors are far from the verified ones.
    ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 2 + 8));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  } else {
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
    if (!peers.empty()) {
      peers.front()->shared_tablet_maybe_null()->TEST_SleepBeforeApplyIntents(
          5s * kTimeMultiplier);
    }
  }
  ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));
  if (non_transactional_writes) {
    // Close the WAL segment holding the writes and add more data to a new one. An open segment is
    // replayed as a whole, while a closed one is replayed only from the lowest flushed OpId, so
    // this also covers the choice of the replay start.
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    for (const auto& peer : peers) {
      ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
    }
    ASSERT_OK(InsertRows(
        conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false, non_transactional_writes));
    // Followers may not have applied the last rows yet; the check below runs on every replica.
    ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));

    // Check that the vector indexes lag the regular DB, so the restart below exercises bootstrap
    // replay of plain writes into vector indexes only.
    for (const auto& peer : peers) {
      auto tablet = ASSERT_RESULT(peer->shared_tablet());
      auto state = ASSERT_RESULT(tablet->GetPersistentState());
      ASSERT_FALSE(state.vector_indexes.empty());
      for (const auto& vector_index : state.vector_indexes) {
        ASSERT_LT(vector_index.op_id.index, state.regular.index) << AsString(state);
      }
    }

    // The closed segment is flushed to the regular DB only, so WAL GC must keep it for the
    // vector indexes.
    std::vector<size_t> num_segments;
    for (const auto& peer : peers) {
      num_segments.push_back(peer->log()->num_segments());
    }
    ASSERT_OK(cluster_->CleanTabletLogs());
    for (size_t i = 0; i != peers.size(); ++i) {
      ASSERT_EQ(peers[i]->log()->num_segments(), num_segments[i]) << peers[i]->tablet_id();
    }
  }
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, Restart) {
  TestRestart(tablet::FlushFlags::kAllDbs);
}

TEST_P(PgVectorIndexTest, Bootstrap) {
  TestRestart(tablet::FlushFlags::kRegular);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedIntentsDB) {
  TestRestart(tablet::FlushFlags::kIntents);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedVectorIndexes) {
  TestRestart(tablet::FlushFlags::kVectorIndexes);
}

// Plain non-transactional writes flushed to the regular DB but not to the vector index must be
// replayed into the vector index after an ungraceful restart. See issue #32797.
TEST_P(PgVectorIndexTest, BootstrapNonTransactionalWrites) {
  TestRestart(tablet::FlushFlags::kRegular, NonTransactionalWrites::kTrue);
}

// A vector index whose backfill has not flushed anything before an ungraceful restart has no
// flushed OpId, so bootstrap reads the whole retained WAL. Plain writes made before the index
// existed must not be replayed into it, their schema packing could be GCed already (GH#35124).
TEST_P(PgVectorIndexTest, BootstrapUnfinishedBackfillAfterSchemaPackingGC) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone) {
    GTEST_SKIP() << "Schema packings are used by packed rows only";
  }

  num_pre_split_tablets_ = 1;
  // The table has no vector index yet, so nothing else keeps it from being split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(
      conn, 1, kNumRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));

  // Repack the rows with a later schema version, so packing GC drops the one used by the writes
  // above, while the WAL still retains them.
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN extra"));
  // A replica that was not running yet when its tablet elected a leader catches up only on a later
  // heartbeat. The compaction below must repack its rows too, and the peer listings must see it.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // Compaction repacks only rows below the history cutoff.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(
      cluster_.get(), "test", ListPeersFilter::kAll));
  for (const auto& peer : peers) {
    ASSERT_OK(WaitFor([&peer, &table_id]() -> Result<bool> {
      auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
      return !table_info->doc_read_context->schema_packing_storage.HasVersionBelow(
          table_info->schema_version);
    }, 30s * kTimeMultiplier, "Old schema packing GC"));
  }
  const auto num_peers = peers.size();
  peers.clear();

  // Park the backfills until the restart below shuts a follower down. CREATE INDEX waits for the
  // leader backfill only, which that shutdown releases.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdown", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  // Keep the PG tserver up, so the connections survive the restart.
  const auto pg_ts_uuid = cluster_->mini_tablet_server(kPgTsIndex)->server()->permanent_uuid();
  tserver::MiniTabletServer* target_ts = nullptr;
  {
    auto followers = ASSERT_RESULT(ListTabletPeersForTableName(
        cluster_.get(), "test", ListPeersFilter::kNonLeaders));
    auto it = std::ranges::find_if(followers, [&pg_ts_uuid](const auto& peer) {
      return peer->permanent_uuid() != pg_ts_uuid;
    });
    ASSERT_NE(it, followers.end());
    const auto& target_peer = *it;
    ASSERT_OK(WaitFor([&target_peer] {
      auto tablet = target_peer->shared_tablet_maybe_null();
      return tablet && tablet->vector_indexes().TEST_HasIndexes();
    }, 60s * kTimeMultiplier, "Vector index registered on target follower"));
    // A colocated tablet flushes its superblock lazily, e.g. on WAL rollover, so the index could
    // be missing from it at the restart.
    ASSERT_OK(target_peer->tablet_metadata()->Flush());
    target_ts = cluster_->find_tablet_server(target_peer->permanent_uuid());
  }

  DisableFlushOnShutdown(*cluster_, true);
  target_ts->Shutdown();
  ASSERT_OK(target_ts->Start());
  ASSERT_OK(target_ts->WaitStarted());

  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(num_peers, "Backfill done after restart"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The check reads at the safe time, which on the restarted follower stays below the history
  // cutoff of the compaction above until the leader propagates a later safe time.
  std::string inconsistencies;
  ASSERT_OK(WaitFor([this, &inconsistencies]() -> Result<bool> {
    auto result = VectorIndexInconsistencies();
    if (!result.ok() && result.status().IsSnapshotTooOld()) {
      return false;
    }
    inconsistencies = VERIFY_RESULT(std::move(result));
    return true;
  }, 30s * kTimeMultiplier, "Vector index check"));
  ASSERT_EQ(inconsistencies, "");
}

// A transaction that writes its intents before the vector index is added and commits after it is
// invisible to the backfill, so the index gets its rows from the apply only. Bootstrap must replay
// that apply into the index, while skipping the intent writes that precede the index.
TEST_P(PgVectorIndexTest, BootstrapTransactionCommittedAfterIndexCreation) {
  constexpr int64_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(txn_conn, 1, kNumRows));
  // Send the buffered intents to the tablet before the index is created.
  ASSERT_RESULT(txn_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));

  ASSERT_OK(CreateIndex(conn));
  // CREATE INDEX waits for the leader backfill only. A replica that was not running yet when its
  // tablet elected a leader could backfill after applying the commit below, and then flush the
  // committed rows to the index with the backfill.
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));
  ASSERT_OK(txn_conn.CommitTransaction());
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  // The index got the rows from the apply only, so they are not flushed to it.
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto state = ASSERT_RESULT(tablet->GetPersistentState());
    ASSERT_EQ(state.vector_indexes.size(), 1);
    ASSERT_LT(state.vector_indexes[0].op_id.index, state.regular.index) << AsString(state);
  }

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A large transaction committed before CREATE INDEX is applied in chunks after the commit.
// The apply finishing while the backfill is reading must not leave its rows out of the index.
// See #36909.
TEST_P(PgVectorIndexTest, BackfillLargeTransactionAppliedAfterCommit) {
  constexpr int64_t kNumRows = 100;
  constexpr int kApplyBatchRecords = 10;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = kApplyBatchRecords;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kApplyBatchRecords;

  auto conn = ASSERT_RESULT(MakeTable());

  // Let the APPLY op write the first chunk only, and hold the rest in the apply intents task.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms) = 10;
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(conn, 1, kNumRows));
  ASSERT_OK(conn.CommitTransaction());

  auto peers = ASSERT_RESULT(WaitForTableNumPeers(
      cluster_.get(), "test", cluster_->num_tablet_servers(), 30s * kTimeMultiplier));
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_OK(WaitFor([&tablet]() -> Result<bool> {
      return VERIFY_RESULT(tablet->TEST_CountDBRecords(docdb::StorageDbType::kRegular)) > 0;
    }, 30s * kTimeMultiplier, "First chunk applied"));
  }

  // Park every backfill between its first and second batch.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  TestThreadHolder threads;
  // Declared after threads, so a failed assertion releases the backfills before joining them.
  auto unblock_backfills = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
        false;
  });
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });
  ASSERT_OK(WaitForVectorIndexBackfillsStarted(cluster_->num_tablet_servers()));

  // The apply finishes before the backfills read on.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_and_skip_apply_intents_task_loop_ms) = 0;
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = false;
  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));

  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows) << index->ToString();
  }
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A transaction committed before CREATE INDEX may be applied while a backfill batch is read.
// That must not leave its rows out of the index. See #36909.
TEST_P(PgVectorIndexTest, BackfillTransactionAppliedDuringRead) {
  constexpr int64_t kNumRows = 100;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());

  // The coordinator sends APPLY again after transaction_resend_applying_interval_usec.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_apply_committed_transactions) = true;
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(conn, 1, kNumRows));
  ASSERT_OK(conn.CommitTransaction());

  // Park every backfill right after it opened the reader of its first batch.
  std::atomic<size_t> num_parked{0};
  CountDownLatch release_backfills{1};
  TestThreadHolder threads;
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("TabletVectorIndexes::Backfill:ReaderOpened", [&](void*) {
    ++num_parked;
    release_backfills.Wait();
  });
  sync_point->EnableProcessing();
  // Declared after threads, so a failed assertion releases the backfills before joining them.
  auto sync_point_cleanup = ScopeExit([sync_point, &release_backfills] {
    release_backfills.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });
  ASSERT_OK(WaitFor([this, &num_parked] {
    return num_parked.load() >= cluster_->num_tablet_servers();
  }, 30s * kTimeMultiplier, "Backfills parked"));

  // The apply writes all rows at once. Their intents stay until the backfills release the reader.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_apply_committed_transactions) = false;
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_OK(WaitFor([&tablet]() -> Result<bool> {
      return VERIFY_RESULT(tablet->TEST_CountDBRecords(docdb::StorageDbType::kRegular)) > 0;
    }, 30s * kTimeMultiplier, "Transaction applied"));
  }

  release_backfills.CountDown();
  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows) << index->ToString();
  }
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A vector index backfill must not keep the transactions that finish on its tablet in the
// transaction participant until it ends: that holds back their cleanup and their intents.
// See #36909.
TEST_P(PgVectorIndexTest, BackfillReleasesFinishedTransactionsBetweenBatches) {
  constexpr int64_t kNumRows = 100;
  constexpr size_t kBatchSize = 10;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kBatchSize;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertBatch(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Its intents are written before the index is created, so its commit during the backfill does not
  // run into the schema version that CREATE INDEX changes.
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(txn_conn, kNumRows + 1, 1));
  // Send the buffered intents to the tablet.
  ASSERT_RESULT(txn_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));

  // Park every backfill between its first and second batch.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  TestThreadHolder threads;
  // Declared after threads, so a failed assertion releases the backfills before joining them.
  auto unblock_backfills = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
        false;
  });
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });
  ASSERT_OK(WaitForVectorIndexBackfillsStarted(cluster_->num_tablet_servers()));
  ASSERT_OK(txn_conn.CommitTransaction());

  // The parked backfills hold no request, so the committed transaction leaves the participant.
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());
  ASSERT_OK(WaitFor([&peers]() -> Result<bool> {
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->transaction_participant()->GetNumRunningTransactions() != 0) {
        return false;
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Finished transactions removed"));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = false;
  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows + 1) << index->ToString();
  }
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A backfill reads at backfill_ht, so compactions must keep the history at backfill_ht from the
// moment the index is added until its backfill is done, also before the backfill task starts.
TEST_P(PgVectorIndexTest, CompactionRetainsHistoryForPendingBackfill) {
  constexpr int64_t kNumRows = 64;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertBatch(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Park every backfill before it reads anything.
  std::atomic<size_t> num_parked{0};
  CountDownLatch release_backfills{1};
  TestThreadHolder threads;
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("TabletVectorIndexes::Backfill:Start", [&](void*) {
    ++num_parked;
    release_backfills.Wait();
  });
  sync_point->EnableProcessing();
  // Declared after threads, so a failed assertion releases the backfills before joining them.
  auto sync_point_cleanup = ScopeExit([sync_point, &release_backfills] {
    release_backfills.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });
  ASSERT_OK(WaitFor([this, &num_parked] {
    return num_parked.load() >= cluster_->num_tablet_servers();
  }, 30s * kTimeMultiplier, "Backfills parked"));

  // Without the pending backfills, this compaction would move the history cutoff past backfill_ht.
  // An intents DB flush would wait for the vector index.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  release_backfills.CountDown();
  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows) << index->ToString();
  }

  // Finished backfills no longer hold the history.
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"))) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_EQ(tablet->vector_indexes().AllowedHistoryCutoff(), HybridTime::kMax);
  }
}

// A vector index storage the tablet does not list, as a restore leaves on a crash, is deleted at
// tserver startup. See issue #35895.
TEST_P(PgVectorIndexTest, StartupDeletesStorageOfUnlistedIndex) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  auto* env = Env::Default();
  std::vector<std::string> unlisted_dirs;
  std::vector<std::string> listed_dirs;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    const auto& meta = *peer->tablet_metadata();
    auto dirs = meta.vector_index_dirs();
    ASSERT_FALSE(dirs.empty()) << peer->tablet_id();
    listed_dirs.insert(listed_dirs.end(), dirs.begin(), dirs.end());
    auto dir = docdb::GetStorageDir(meta.rocksdb_dir(), docdb::kVectorIndexDirPrefix + "99999");
    ASSERT_OK(env->CreateDir(dir));
    ASSERT_OK(WriteStringToFile(env, "chunk", JoinPathSegments(dir, "1.yb_hnsw")));
    unlisted_dirs.push_back(std::move(dir));
  }
  ASSERT_FALSE(unlisted_dirs.empty());

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  for (const auto& dir : unlisted_dirs) {
    ASSERT_FALSE(env->DirExists(dir)) << dir;
  }
  for (const auto& dir : listed_dirs) {
    ASSERT_TRUE(env->DirExists(dir)) << dir;
  }
}

// DROP destroys only the vector index storages the tablet lists, so it leaves one a restore left,
// e.g. on a build before #35006. The tserver deletes it at startup. See issue #35895.
TEST_P(PgVectorIndexTest, StartupDeletesStorageOfDroppedTablet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto* env = Env::Default();
  std::vector<std::string> rocksdb_dirs;
  std::vector<std::string> unlisted_dirs;
  {
    auto conn = ASSERT_RESULT(MakeIndexAndFill(16));
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      const auto& meta = *peer->tablet_metadata();
      rocksdb_dirs.push_back(meta.rocksdb_dir());
      auto dir =
          docdb::GetStorageDir(meta.rocksdb_dir(), docdb::kVectorIndexDirPrefix + "99999");
      ASSERT_OK(env->CreateDir(dir));
      ASSERT_OK(WriteStringToFile(env, "chunk", JoinPathSegments(dir, "1.yb_hnsw")));
      unlisted_dirs.push_back(std::move(dir));
    }
    ASSERT_FALSE(unlisted_dirs.empty());
    // Dropping a colocated table keeps its tablet.
    if (!IsColocated()) {
      ASSERT_OK(conn.Execute("DROP TABLE test"));
    }
  }
  if (IsColocated()) {
    auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(conn.Execute("DROP DATABASE colocated_db"));
  }

  ASSERT_OK(WaitFor([env, &rocksdb_dirs]() {
    return std::none_of(rocksdb_dirs.begin(), rocksdb_dirs.end(), [env](const auto& dir) {
      return env->DirExists(dir);
    });
  }, 30s * kTimeMultiplier, "Tablet data deleted"));
  for (const auto& dir : unlisted_dirs) {
    ASSERT_TRUE(env->DirExists(dir)) << dir;
  }

  ASSERT_OK(RestartCluster());
  for (const auto& dir : unlisted_dirs) {
    ASSERT_FALSE(env->DirExists(dir)) << dir;
  }
}

// Tablet::AddTableInMemory creates the storage of a new vector index before the superblock that
// lists the index is flushed. Rolling the superblocks back to before the index simulates a crash in
// that window: bootstrap replays the index back, so the startup sweep must keep its storage.
TEST_P(PgVectorIndexTest, StartupKeepsStorageOfIndexReplayedByBootstrap) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  // Flushed before the index, so bootstrap replays from before its ADD_TABLE.
  ASSERT_OK(cluster_->FlushTablets());

  std::unordered_map<std::string, tablet::RaftGroupReplicaSuperBlockPB> superblocks;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    const auto& meta = *peer->tablet_metadata();
    auto path = ASSERT_RESULT(meta.fs_manager()->GetRaftGroupMetadataPath(peer->tablet_id()));
    ASSERT_OK(meta.ReadSuperBlockFromDisk(&superblocks[path]));
  }

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));

  // The inode tells a kept storage dir from one deleted and created again by bootstrap.
  auto dir_inode = [](const std::string& dir) -> Result<ino_t> {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
      return STATUS_FROM_ERRNO(Format("stat $0", dir), errno);
    }
    return st.st_ino;
  };
  std::vector<std::pair<std::string, ino_t>> storages;
  std::vector<std::string> rolled_back_paths;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    const auto& meta = *peer->tablet_metadata();
    rolled_back_paths.push_back(
        ASSERT_RESULT(meta.fs_manager()->GetRaftGroupMetadataPath(peer->tablet_id())));
    for (const auto& dir : meta.vector_index_dirs()) {
      storages.emplace_back(dir, ASSERT_RESULT(dir_inode(dir)));
    }
  }
  ASSERT_EQ(storages.size(), cluster_->num_tablet_servers());

  DisableFlushOnShutdown(*cluster_, true);
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->mini_tablet_server(i)->Shutdown();
  }
  for (const auto& path : rolled_back_paths) {
    ASSERT_OK(pb_util::WritePBContainerToPath(
        Env::Default(), path, superblocks.at(path), pb_util::OVERWRITE, pb_util::SYNC));
  }
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Start());
  }

  ASSERT_EQ(ListTabletPeersWithVectorIndexes(cluster_.get()).size(), rolled_back_paths.size());
  for (const auto& [dir, inode] : storages) {
    ASSERT_EQ(ASSERT_RESULT(dir_inode(dir)), inode) << dir;
  }
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, RemoteBootstrap) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto* mts = cluster_->mini_tablet_server(2);
  mts->Shutdown();
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(cluster_->FlushTablets());
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
    ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
  }

  // Need at least one committed entry after segment to GC this segment.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));

  ASSERT_OK(cluster_->CleanTabletLogs());

  ASSERT_OK(mts->Start());
  ASSERT_OK(WaitFor([this, table_id, mts]() -> Result<bool> {
    auto peers = ListTableActiveTabletPeers(cluster_.get(), table_id);
    tablet::TabletPeerPtr leader;
    for (const auto& peer : peers) {
      bool is_leader =
          VERIFY_RESULT(peer->GetConsensus())->GetLeaderStatus() ==
              consensus::LeaderStatus::LEADER_AND_READY;
      if (peer->permanent_uuid() != mts->fs_manager().uuid()) {
        if (is_leader) {
          leader = peer;
        }
        continue;
      }
      if (is_leader) {
        return true;
      }
    }
    if (leader) {
      LOG(INFO) << "Step down: " << leader->permanent_uuid();
      WARN_NOT_OK(StepDown(leader, mts->fs_manager().uuid(), ForceStepDown::kTrue),
                  "StepDown failed");
    }
    return false;
  }, 60s * kTimeMultiplier, "Wait desired leader"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// Bootstrap replays the writes that a vector index has not flushed into it, so packing GC must
// keep their schema packings after the regular DB repacks the rows (GH#35466).
void PgVectorIndexTest::TestBootstrapAfterSchemaChange(
    NullVectorBeforeSchemaChange null_vector, NonTransactionalWrites non_transactional_writes) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone) {
    GTEST_SKIP() << "Schema packings are used by packed rows only";
  }

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Backfill done"));
  if (null_vector) {
    // A row with a NULL vector adds no entry to the index, while the vectors written after the
    // schema change keep only the later schema version in the index.
    ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, NULL)", kNumRows * 2));
    ASSERT_OK(conn.Execute("RESET yb_disable_transactional_writes"));
  } else {
    ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  }

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN extra"));
  if (null_vector) {
    ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  // A follower that applies the rows after the flush below misses the compaction that repacks them.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // Compaction repacks only rows below the history cutoff. An intents DB flush would wait for the
  // vector index, and force its flush.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  // Packing GC runs before the compaction completes.
  auto check_old_packings = [this, &table_id](bool expected) -> Status {
    for (const auto& peer : VERIFY_RESULT(ListTabletPeersForTableName(
             cluster_.get(), "test", ListPeersFilter::kAll))) {
      auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
      const auto& packings = table_info->doc_read_context->schema_packing_storage;
      SCHECK_EQ(
          packings.HasVersionBelow(table_info->schema_version), expected, IllegalState,
          Format("$0 packings: $1", peer->tablet_id(), packings.VersionsToString()));
    }
    return Status::OK();
  };
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(
           cluster_.get(), "test", ListPeersFilter::kAll))) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto state = ASSERT_RESULT(tablet->GetPersistentState());
    ASSERT_EQ(state.vector_indexes.size(), 1);
    ASSERT_LT(state.vector_indexes[0].op_id.index, state.regular.index) << AsString(state);
  }
  ASSERT_OK(check_old_packings(true));

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The old packings go once the vector index flushes the writes. The replay also left their schema
  // version in the frontier of the regular DB memtable, which is not flushed without entries.
  // So every replica, followers included, must apply the row before the flush.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows + 1, /* keep_vectors = */ false, non_transactional_writes));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // The check reads a follower at its safe time. After the restart, it stays at the last replayed
  // write, below the history cutoff of the compaction above, until the follower applies a new write
  // or gets a safe time from the leader.
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());
  ASSERT_OK(check_old_packings(false));
}

TEST_P(PgVectorIndexTest, BootstrapLaggingVectorIndexAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kFalse, NonTransactionalWrites::kTrue);
}

TEST_P(PgVectorIndexTest, BootstrapTransactionalWritesAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kFalse, NonTransactionalWrites::kFalse);
}

TEST_P(PgVectorIndexTest, BootstrapNullVectorAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kTrue, NonTransactionalWrites::kTrue);
}

// Bootstrap replays an op into a lagging vector index whatever tables it writes. In a colocated
// tablet a plain write or a transaction could write only another table, whose old schema packings
// the index does not keep (GH#35466).
TEST_P(PgVectorIndexTest, BootstrapAfterOtherTableSchemaChange) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone || !IsColocated()) {
    GTEST_SKIP() << "Needs packed rows of another table in the same tablet";
  }

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(conn.Execute("CREATE TABLE other (id BIGINT PRIMARY KEY, v INT)"));
  const auto num_peers = ASSERT_RESULT(ListTabletPeersForTableName(
      cluster_.get(), "test", ListPeersFilter::kAll)).size();
  ASSERT_OK(WaitForVectorIndexBackfills(num_peers, "Backfill done"));
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  ASSERT_OK(conn.Execute("INSERT INTO other VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("RESET yb_disable_transactional_writes"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO other VALUES (2, 2)"));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(conn.Execute("ALTER TABLE other ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE other DROP COLUMN extra"));
  // Keep the index lagging the regular DB.
  ASSERT_OK(InsertRows(
      conn, 1, kNumRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Compaction repacks only rows below the history cutoff. An intents DB flush would wait for the
  // vector index, and force its flush.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  for (int32_t id : {1, 2}) {
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int32_t>(Format("SELECT v FROM other WHERE id = $0", id))), id);
  }
}

// Reproduces the SIGSEGV in MvccManager::SafeTimeForFollower during tablet bootstrap.
//
// On bootstrap, a tablet that has a vector index runs the index backfill from
// Tablet::OpenKeyValueTablet -> TabletVectorIndexes::DoCreateIndex -> ScheduleBackfill, i.e. before
// TabletPeer::InitTabletPeer publishes tablet_. The backfill reads the table; when it hits a
// provisional record it resolves the transaction status (DecodeStrongWriteIntent ->
// RequestStatusAt). If that transaction is aborted, the async status response
// (RunningTransaction::DoStatusReceived) enqueues a remove and walks the remove queue
// (ProcessRemoveQueueUnlocked -> SafeTimeForTransactionParticipant). The participant is not
// closing, so the CheckClosing() guard from [#31374] does not apply, and tablet_ is not yet
// assigned, so the raw access dereferences a null tablet. RF>1 is required so the transaction
// status coordinator is available to answer the status request while the tablet reopens.
TEST_P(PgVectorIndexTest, StatusResolutionDuringBootstrapBackfill) {
  num_pre_split_tablets_ = 1;

  // Hold the initial vector index backfill until the tablet starts shutting down, so it never
  // finishes before we shut the target tserver down. On the restart below the predecessor is
  // already cleared, so the backfill runs immediately.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdown", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 1));

  // The uncommitted-transaction connection lives on the PG tserver (kPgTsIndex), so shut a
  // different tserver down to keep that connection alive for the abort below. With RF3 the target
  // tserver hosts a replica of the single test tablet.
  const size_t target_idx = kPgTsIndex == 0 ? 1 : 0;
  const auto target_uuid = cluster_->mini_tablet_server(target_idx)->server()->permanent_uuid();

  // Populate via an uncommitted transaction, leaving provisional records (intents) on the tablet.
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(txn_conn, /* start_id= */ 1, /* count= */ 16));
  // Intentionally not committed.

  // Create the vector index. Its backfill blocks at the sync point above and CREATE INDEX blocks
  // until backfill completes, so run it on a background thread; it is expected to fail when we shut
  // the target tserver down.
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    WARN_NOT_OK(CreateIndex(index_conn), "CreateIndex interrupted by shutdown");
  });

  // Wait until the vector index is registered on the tserver we are going to shut down (by then it
  // has also replicated the uncommitted transaction's intents).
  ASSERT_OK(WaitFor([this, &target_uuid]() -> Result<bool> {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kAll));
    for (const auto& peer : peers) {
      if (peer->permanent_uuid() != target_uuid) {
        continue;
      }
      auto tablet = peer->shared_tablet_maybe_null();
      if (tablet && tablet->vector_indexes().TEST_HasIndexes()) {
        return true;
      }
    }
    return false;
  }, 60s * kTimeMultiplier, "Vector index registered on target tserver"));

  // Shut the target tserver down; its blocked backfill is released (and interrupted) by the tablet
  // shutdown.
  cluster_->mini_tablet_server(target_idx)->Shutdown();

  // Abort the transaction; the coordinator (available on the surviving tservers) marks it ABORTED.
  ASSERT_OK(txn_conn.RollbackTransaction());

  // Delay InitTabletPeer so that, if the backfill were (incorrectly) launched during bootstrap, its
  // aborted-status response would walk the remove queue while tablet_ is still null.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_init_tablet_peer_ms) = 5000 * kTimeMultiplier;

  ASSERT_OK(cluster_->mini_tablet_server(target_idx)->Start());
  ASSERT_OK(cluster_->mini_tablet_server(target_idx)->WaitStarted());

  // Reaching here without the tserver crashing means the fix holds.
  threads.Stop();
}

// A truncate pauses the tablet's read/write operation counters while it replaces the storages, so a
// vector index backfill running at that moment fails to acquire its scoped operation and aborts
// with TryAgain. Backfills were launched only from Tablet::Start(), so nothing resumed the aborted
// one: the index stayed not backfilled until the tserver restarted, blocking tablet splits and
// never reaching the master through vector_index_finished_backfills (GH#33102). In a debug build
// the abort also hit a DFATAL.
TEST_P(PgVectorIndexTest, BackfillInterruptedByTruncate) {
  constexpr size_t kNumRows = 64;

  if (IsColocated()) {
    // A colocated table shares its tablet with the rest of the database, so TRUNCATE tombstones the
    // table instead of replacing the storages.
    GTEST_SKIP() << "Truncate does not replace tablet storages of a colocated table";
  }

  // Park the backfills until the truncate below has paused the blocking operations, so that reading
  // the indexed table fails with TryAgain. The dependency is satisfied by that first pause, so the
  // retried backfill runs without parking.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdownStorages:BlockingPaused", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  // Retry rapidly, so that retries land all over the truncate: while it still holds the pause, and
  // in the window where it has torn the vector indexes down and not re-opened them yet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backfill_retry_delay_ms) = 10 * kTimeMultiplier;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  // The backfill is parked, so CREATE INDEX does not return until the truncate below releases it.
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  ASSERT_OK(WaitFor([this] {
    return !ListVectorIndexes(cluster_.get()).empty();
  }, 60s * kTimeMultiplier, "Vector index created on tablets"));

  // Truncate through the client rather than through YSQL: a YSQL TRUNCATE either rewrites the table
  // instead of truncating the tablet, or conflicts with the catalog version bump of the CREATE
  // INDEX still running above.
  ASSERT_OK(client_->TruncateTable(ASSERT_RESULT(GetTableIDFromTableName("test"))));

  // CREATE INDEX returns once the backfill is reported to the master, so by now every peer has the
  // index and the expected count is stable.
  threads.JoinAll();
  const auto num_indexes = ListVectorIndexes(cluster_.get()).size();
  ASSERT_GT(num_indexes, 0);

  ASSERT_OK(WaitForVectorIndexBackfills(num_indexes, "Backfill done after truncate"));
}

// A backfill holds its blocking operation from batch to batch, so a truncate waits for it.
// The truncate disables new blocking operations, so the next batch fails with TryAgain, which
// releases the truncate, and the backfill is retried after it.
TEST_P(PgVectorIndexTest, TruncateBetweenBackfillBatches) {
  constexpr int64_t kNumRows = 100;
  constexpr int64_t kBatchSize = 10;

  if (IsColocated()) {
    GTEST_SKIP() << "Truncate does not replace tablet storages of a colocated table";
  }

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kBatchSize;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backfill_retry_delay_ms) = 10 * kTimeMultiplier;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertBatch(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), cluster_->num_tablet_servers());

  // Park every backfill between its first and second batch.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  TestThreadHolder threads;
  // Declared after threads, so a failed assertion releases the backfills before joining them.
  auto unblock_backfills = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
        false;
  });
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });
  ASSERT_OK(WaitForVectorIndexBackfillsStarted(cluster_->num_tablet_servers()));

  // See BackfillInterruptedByTruncate for why this does not truncate through YSQL.
  threads.AddThreadFunctor([this, &table_id] {
    ASSERT_OK(client_->TruncateTable(table_id));
  });

  // Tablet::StartShutdownStorages:BlockingPaused is reached only once the backfills released their
  // blocking operations, so poll for the disabled ones instead.
  ASSERT_OK(WaitFor([&peers]() -> Result<bool> {
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      auto op = tablet->CreateScopedRWOperationBlockingRocksDbShutdownStart(CoarseTimePoint::min());
      if (op.ok()) {
        return false;
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Truncate disabled blocking operations"));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = false;
  threads.JoinAll();
  const auto num_indexes = ListVectorIndexes(cluster_.get()).size();
  ASSERT_GT(num_indexes, 0);
  ASSERT_OK(WaitForVectorIndexBackfills(num_indexes, "Backfill done after truncate"));
  // Only the first batch was inserted: the rest of the rows were truncated before the backfill read
  // them. That batch is gone with the rows.
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_EQ(index->metrics().backfill_inserted_entries->value(), kBatchSize) << index->ToString();
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), 0) << index->ToString();
  }
}

// When the backfill fails with something other than TryAgain/ShutdownInProgress, CREATE INDEX
// should fail with that error instead of hanging.
TEST_P(PgVectorIndexTest, CreateIndexFailsOnFailedBackfill) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  // So the test fails instead of hanging if the error isn't reported.
  ASSERT_OK(conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(60s * kTimeMultiplier)));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_vector_index_backfill) = true;
  ASSERT_NOK_STR_CONTAINS(CreateIndex(conn), "Injected vector index backfill failure");

  // The failed CREATE INDEX got rolled back, so this should work now.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_vector_index_backfill) = false;
  ASSERT_OK(CreateIndex(conn));
}

// VectorLSM::Insert counts its tasks on the mutable chunk before allocating them in the insert
// registry. When the allocation failed because the registry was already shut down, the count
// stayed elevated, so a chunk that a flush had meanwhile handed to the save path never saved and
// the shutdown waited for it forever (GH#34199). Only an index removal can shut the registry down
// under a running insert: a tablet shutdown drains the operations first.
TEST_P(PgVectorIndexTest, RemoveIndexDuringBackfillInsert) {
  constexpr size_t kNumRows = 64;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  // Park a follower's backfill insert before it allocates its tasks. CREATE INDEX waits for the
  // leader's backfill only, so the leader proceeds and the DROP INDEX below can run. The follower
  // is picked once the index is registered on the tablets, so every insert first waits for that
  // choice.
  std::string parked_dir;
  CountDownLatch follower_picked{1};
  CountDownLatch insert_parked{1};
  CountDownLatch resume_insert{1};
  CountDownLatch registries_stopped{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::Insert:BeforeAllocateTasks", [&](void* arg) {
    ASSERT_TRUE(follower_picked.WaitFor(60s * kTimeMultiplier));
    if (*static_cast<const std::string*>(arg) != parked_dir) {
      return;
    }
    insert_parked.CountDown();
    ASSERT_TRUE(resume_insert.WaitFor(60s * kTimeMultiplier));
  });
  sync_point->SetCallBack("VectorLSM::CompleteShutdown:RegistriesStopped", [&](void* arg) {
    if (*static_cast<const std::string*>(arg) == parked_dir) {
      registries_stopped.CountDown();
    }
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  // Keep only a weak reference to the parked index: the removal destroys it once its shutdown
  // completes, which is what the test waits for.
  std::weak_ptr<docdb::DocVectorIndex> weak_index;
  ASSERT_OK(WaitFor([this, &weak_index, &parked_dir] {
    auto indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
    if (indexes.empty()) {
      return false;
    }
    weak_index = indexes.front();
    parked_dir = indexes.front()->path();
    return true;
  }, 60s * kTimeMultiplier, "Vector index registered on a follower"));
  follower_picked.CountDown();

  ASSERT_TRUE(insert_parked.WaitFor(60s * kTimeMultiplier)) << "Backfill insert did not park";
  threads.JoinAll();

  // Let the other replicas finish their backfills, so the removal below overtakes the parked insert
  // only.
  ASSERT_OK(WaitFor([this, &parked_dir]() -> Result<bool> {
    for (const auto& index : ListVectorIndexes(cluster_.get())) {
      if (index->path() != parked_dir && !VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Other replicas backfilled"));

  // The removal shuts the insert registry down, then waits for all chunks to save. Resume the
  // insert only after that, so its allocation fails. The drop runs off the main thread in case it
  // waits for the parked replica.
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.ExecuteFormat("DROP INDEX $0", kVectorIndexName));
  });
  ASSERT_TRUE(registries_stopped.WaitFor(60s * kTimeMultiplier))
      << "Index removal did not reach the registry shutdown";
  resume_insert.CountDown();

  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 30s * kTimeMultiplier,
                    "Index removal hung waiting for the chunk of the failed insert"));
  threads.JoinAll();
}

////////////////////////////////////////////////////////
// PgVectorIndexStorageSetCapacityTest
////////////////////////////////////////////////////////

// GH#33923: docdb::StorageSet used to be a std::bitset<64> where bit 0 is the regular DB and bit
// 1 + i is vector index i, so a tablet could address only 63 vector indexes: the writers and the
// bootstrap threw std::out_of_range for the 64th one and aborted the tserver.
// Colocated tables share one tablet, so one index per table is enough to get there; a
// non-colocated table has to carry all the indexes itself, one per vector column.
class PgVectorIndexStorageSetCapacityTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  // One more than the 63 vector indexes the old StorageSet could address.
  static constexpr size_t kNumIndexes = 64;
  static constexpr int64_t kNumRows = 2;

  static std::string TableName(size_t idx) {
    return Format("test_$0", idx);
  }

  static std::string ColumnName(size_t idx) {
    return Format("embedding_$0", idx);
  }

  std::string TableForIndex(size_t idx) {
    return TableName(IsColocated() ? idx : 0);
  }

  std::string ColumnForIndex(size_t idx) {
    return ColumnName(IsColocated() ? 0 : idx);
  }

  Status CreateVectorIndex(PGConn& conn, size_t idx) {
    return conn.ExecuteFormat(
        "CREATE INDEX vi_$0 ON $1 USING ybhnsw ($2 $3) WITH ($4)",
        idx, TableForIndex(idx), ColumnForIndex(idx), pgwrapper::VectorOpsName(distance_kind_),
        kCheapIndexBuildOptions);
  }

  Result<PGConn> MakeTabletWithIndexes() {
    dimensions_ = 3;
    auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    if (IsColocated()) {
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0 COLOCATION = true", DbName()));
      conn = VERIFY_RESULT(Connect());
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));

    if (IsColocated()) {
      for (size_t i = 0; i != kNumIndexes; ++i) {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0 (id bigserial PRIMARY KEY, $1 vector($2)) WITH (COLOCATED = 1)",
            TableForIndex(i), ColumnForIndex(i), dimensions_));
      }
    } else {
      std::string columns;
      for (size_t i = 0; i != kNumIndexes; ++i) {
        columns += Format(", $0 vector($1)", ColumnForIndex(i), dimensions_);
      }
      RETURN_NOT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0 (id bigserial PRIMARY KEY$1) SPLIT INTO 1 TABLETS",
          TableName(0), columns));
    }

    for (size_t i = 0; i != kNumIndexes; ++i) {
      RETURN_NOT_OK(CreateVectorIndex(conn, i));
    }
    return conn;
  }

  // An explicit transaction, so that the write is applied through the intents path and is replayed
  // from the WAL with a computed StorageSet. Every vector column is populated, because the apply
  // skips null columns before it reaches the index in the last slot.
  Status InsertRowsInTransaction(PGConn& conn) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    for (int64_t id = 1; id <= kNumRows; ++id) {
      if (IsColocated()) {
        for (size_t i = 0; i != kNumIndexes; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat(
              "INSERT INTO $0 VALUES ($1, '$2')", TableForIndex(i), id, AsString(Vector(id))));
        }
      } else {
        std::string values;
        for (size_t i = 0; i != kNumIndexes; ++i) {
          values += Format(", '$0'", AsString(Vector(id)));
        }
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 VALUES ($1$2)", TableName(0), id, values));
      }
    }
    return conn.CommitTransaction();
  }

  Result<size_t> NumVectorIndexesOnTablet() {
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected exactly one tablet with vector indexes");
    auto tablet = VERIFY_RESULT(peers.front()->shared_tablet());
    auto list = tablet->vector_indexes().List();
    return list ? list->size() : 0;
  }

  void VerifyAllIndexesReturnAllRows() {
    std::string expected;
    for (int64_t id = 1; id <= kNumRows; ++id) {
      if (!expected.empty()) {
        expected += "; ";
      }
      expected += std::to_string(id);
    }

    auto conn = ASSERT_RESULT(Connect());
    for (size_t i = 0; i != kNumIndexes; ++i) {
      // TEST_fail_on_seq_scan_with_vector_indexes is on, so the read has to go through the index.
      auto rows = ASSERT_RESULT(conn.FetchAllAsString(Format(
          "SELECT id FROM $0 ORDER BY $1 $2 '$3' LIMIT $4",
          TableForIndex(i), ColumnForIndex(i), pgwrapper::DistanceOpName(distance_kind_),
          AsString(Vector(1)), kNumRows)));
      ASSERT_EQ(rows, expected) << "Vector index " << i;
    }
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexStorageSetCapacityTest);

// The live apply of a transactional write to a tablet with 64 vector indexes, followed by a restart
// that replays the WAL.
TEST_P(PgVectorIndexStorageSetCapacityTest, ApplyWith64VectorIndexes) {
  auto conn = ASSERT_RESULT(MakeTabletWithIndexes());
  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_OK(InsertRowsInTransaction(conn));

  // No flush here and none on shutdown, so the WAL holds the only copy of the writes.
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());

  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_NO_FATALS(VerifyAllIndexesReturnAllRows());
}

// The bootstrap half on its own. TEST_skip_process_apply skips the live apply, so the APPLYING
// record is applied only by the WAL replay, which uses a StorageSet computed by
// ComputeApplyToStorages rather than StorageSet::All(). Replay runs on every tablet open, so a
// crash here means the tablet never comes back.
TEST_P(PgVectorIndexStorageSetCapacityTest, BootstrapReplayWith64VectorIndexes) {
  auto conn = ASSERT_RESULT(MakeTabletWithIndexes());
  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_process_apply) = true;
  ASSERT_OK(InsertRowsInTransaction(conn));

  DisableFlushOnShutdown(*cluster_, true);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_process_apply) = false;
  ASSERT_OK(RestartCluster());

  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_NO_FATALS(VerifyAllIndexesReturnAllRows());
}

}  // namespace yb::pgwrapper
