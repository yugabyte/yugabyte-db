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
#include <future>

#include "yb/docdb/doc_vector_index.h"

#include "yb/fs/fs_manager.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/db_impl.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_flusher.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/power_loss_env.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

METRIC_DECLARE_gauge_uint64(tablet_flush_active);

DECLARE_bool(allow_encryption_at_rest);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(vector_index_store_payload);
DECLARE_bool(TEST_simulate_abrupt_server_restart);
DECLARE_int32(bytes_durable_wal_write_mb);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(interval_durable_wal_write_ms);
DECLARE_int32(max_nexts_to_avoid_seek);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_uint64(TEST_inject_sleep_before_applying_intents_ms);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int64(db_block_size_bytes);
DECLARE_bool(vector_index_allow_parallel_compactions);
DECLARE_int32(vector_index_files_number_compaction_trigger);
DECLARE_uint32(vector_index_num_compactions_limit);
DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::vector_index {

extern MonoDelta TEST_sleep_after_saving_chunk;

} // namespace yb::vector_index

namespace yb::pgwrapper {

////////////////////////////////////////////////////////
// PgVectorIndexSingleServerTest
////////////////////////////////////////////////////////

class PgVectorIndexSingleServerTest
    : public PgVectorIndexTestParamsDecorator<PgVectorIndexSingleServerTestBase> {
 protected:
  void TestSearchWithConcurrentUpdates(AddFilter add_filter, StorePayload store_payload);
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexSingleServerTest);

// The vector index reverse mapping records are stored in the regular RocksDB before user data
// records, below the middle split key lower bound. The index backfill writes only reverse mapping
// records, so the subsequent flush produces an SST file without user data records.
// The middle split key calculation should skip such a file instead of failing, otherwise the
// tablet cannot be split until this file is compacted away.
TEST_P(PgVectorIndexSingleServerTest, ManualSplitWithReverseMappingOnlySstFile) {
  if (IsColocated()) {
    GTEST_SKIP() << "Tablet split is not supported for colocated tables";
  }

  constexpr size_t kNumRows = 100;

  // Small data blocks so rows span multiple blocks; otherwise GetMiddleKey's single-block
  // fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

  // Reverse mappings are written by the backfill only when they are not owned by the table.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Flush user data records into a separate SST file before the vector index exists.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Create the vector index, its backfill writes reverse mapping records into the regular
  // RocksDB. Flush them into an SST file consisting of such records only.
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());

  // Sanity check the setup: there is an SST file with reverse mapping records only.
  size_t num_reverse_mapping_only_files = 0;
  for (const auto& file : tablet->regular_db()->GetLiveFilesMetaData()) {
    if (!file.largest.key.empty() &&
        file.largest.key[0] < dockv::kMinRegularDbTableRowFirstByte) {
      ++num_reverse_mapping_only_files;
    }
  }
  ASSERT_EQ(num_reverse_mapping_only_files, 1);

  // The middle split key calculation should skip the reverse mapping only file.
  ASSERT_RESULT(tablet->GetSplitKeys(cluster_->GetSplitFactor()));

  // Make sure the tablet split completes.
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), parent_tablet_id));

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);
}

// Graceful restart without explicit flushes. The index is created on an empty table, so all
// vectors live in the vector LSM mutable chunk at shutdown time. See issue #32691.
TEST_P(PgVectorIndexSingleServerTest, GracefulRestart) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// A tablet whose vector index chunk cannot be loaded must fail to open. Otherwise the first write
// applied to it FATALs the tserver, which then crash-loops replaying that write. See issue #36620.
TEST_P(PgVectorIndexSingleServerTest, UnloadableChunkFailsTablet) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 64));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));

  auto* env = Env::Default();
  std::vector<std::string> chunk_files;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    for (const auto& dir : peer->tablet_metadata()->vector_index_dirs()) {
      for (const auto& file : ASSERT_RESULT(env->GetChildren(dir))) {
        if (file.ends_with(".yb_hnsw")) {
          chunk_files.push_back(JoinPathSegments(dir, file));
        }
      }
    }
  }
  ASSERT_FALSE(chunk_files.empty());

  auto* ts = cluster_->mini_tablet_server(0);
  // A footer of only its CRC and size; the CRC of empty data is 0.
  std::string footer_only(4096, '\0');
  footer_only[footer_only.size() - sizeof(uint64_t)] = sizeof(uint32_t) + sizeof(uint64_t);
  // Empty and zero-filled, with footer size 0, are what an unsynced chunk could look like after a
  // power loss.
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"empty", ""}, {"zero-filled", std::string(4096, '\0')}, {"footer only", footer_only}};
  for (const auto& [name, content] : cases) {
    SCOPED_TRACE(name);
    ts->Shutdown();
    for (const auto& file : chunk_files) {
      ASSERT_OK(WriteStringToFile(env, content, file));
    }
    ASSERT_OK(ts->Start(tserver::WaitTabletsBootstrapped::kFalse));
    auto status = ts->WaitStarted();
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(), "Failed to load");
  }
}

class PgVectorIndexPowerLossTest : public PgVectorIndexSingleServerTest {
 protected:
  void SetUp() override {
    // Otherwise tablets use an encrypted env that bypasses power_loss_env_.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_encryption_at_rest) = false;
    PgVectorIndexSingleServerTest::SetUp();
  }

  void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    options->ts_env = &power_loss_env_;
  }

  PowerLossEnv power_loss_env_;
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexPowerLossTest);

// The fsynced vector index manifest lists the chunks of a flush, so their files must survive a
// power loss right after it. See issue #36620.
TEST_P(PgVectorIndexPowerLossTest, FlushedChunkSurvives) {
  constexpr size_t kQueryLimit = 5;
  auto is_chunk_file = [](const std::string& path) {
    return BaseName(path).starts_with("vectorindex_");
  };

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 64));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  // Otherwise chunk files bypass power_loss_env_ and the scenario is not exercised.
  ASSERT_GT(power_loss_env_.NumTrackedFiles(is_chunk_file), 0U);

  DisableFlushOnShutdown(*cluster_, true);
  auto* ts = cluster_->mini_tablet_server(0);
  ts->Shutdown();
  // Drop only the chunk data, FlushAheadOfUnsyncedWal covers the WAL.
  ASSERT_OK(power_loss_env_.DropUnsyncedData(is_chunk_file));

  ASSERT_OK(ts->Start());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// A power loss drops the WAL tail that was not fsynced, along with the unflushed regular DB rows
// written by it.
// A vector index flush done before the power loss must not have persisted vectors of those rows.
// See issue #36244.
TEST_P(PgVectorIndexPowerLossTest, FlushAheadOfUnsyncedWal) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;
  auto is_wal_segment = [](const std::string& path) {
    return FsManager::IsWalSegmentFileName(BaseName(path));
  };

  // Fsync WAL only when forced to: segment allocation, rollover or close.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_interval_durable_wal_write_ms) = 3600 * 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_bytes_durable_wal_write_mb) = 1024;

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(
      conn, 1, kNumRows, /* keep_vectors= */ false, NonTransactionalWrites::kTrue));
  // Otherwise the WAL tail is already durable and the scenario is not exercised.
  ASSERT_GT(power_loss_env_.UnsyncedBytes(is_wal_segment), 0U);

  // A regular DB flush would fsync the WAL itself, so flush the vector indexes only.
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes));

  // Crash: neither the shutdown flush nor the WAL close syncs the WAL.
  DisableFlushOnShutdown(*cluster_, true);
  auto* ts = cluster_->mini_tablet_server(0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = true;
  ts->Shutdown();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_abrupt_server_restart) = false;
  ASSERT_OK(power_loss_env_.DropUnsyncedData(is_wal_segment));

  ASSERT_OK(ts->Start());
  conn = ASSERT_RESULT(Connect());
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  auto num_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
  ASSERT_EQ(num_rows, static_cast<int64_t>(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// Reproduces a failure seen in pgvector stress tests: an ANN query whose read time precedes
// a concurrent DELETE fails with "Vector not found". PgsqlVectorFilter checks reverse
// mappings at the statement read time and accepts the deleted row, while
// DocVectorIndexImpl::Search resolves vector ids to ybctids at ReadHybridTime::Max() and
// observes the tombstone written by the DELETE.
TEST_P(PgVectorIndexSingleServerTest, SnapshotReadWithConcurrentDelete) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto read_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  // Pin the transaction read time before the delete happens.
  ASSERT_RESULT(read_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));

  // Tombstones the reverse mapping for row 2 and marks the tablet as having vector deletions,
  // so the vector filter is active for the read below.
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));

  // Row 2 was deleted after the read time, so it should still be visible to the query.
  ASSERT_NO_FATALS(VerifyRows(read_conn, AddFilter::kFalse, ExpectedRows(kQueryLimit)));
  ASSERT_OK(read_conn.CommitTransaction());
}

// Reproduces the "Vector not found" flake from PgVectorIndexTest.SnapshotSchedule by interleaving a
// committed DELETE's reverse-mapping apply with a vector search: sync points gate the apply to run
// after the filter reads the live mapping (HandleApplying waits for Search:AfterFilter) and let the
// query resolve only after it commits (Search:BeforeResolve waits for ApplyIntentsDone). A single
// tablet server keeps each sync point mapped to one tablet.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentDeleteApplyDuringSearch) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 10;

  // Keep the filter active (could_have_missing_entries == false) so Search resolves on the filter's
  // reader -- the path the fix exercises. In production this holds when the tablet has deletions;
  // the racing delete's apply is gated below, so force it on to keep the test deterministic.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      // Hold the DELETE's apply at HandleApplying until the filter has read the live mapping.
      {"DocVectorIndexImpl::Search:AfterFilter", "TransactionParticipant::HandleApplying"},
      // Resolve only after the apply commits (its tombstone is visible to a new reader once
      // ApplyIntents' WriteToRocksDB returns).
      {"TransactionParticipant::ApplyIntentsDone", "DocVectorIndexImpl::Search:BeforeResolve"},
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] { sync_point->DisableProcessing(); });

  // Commit the racing delete; its apply is gated at HandleApplying, so COMMIT returns first and the
  // query below reads after the commit.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Same connection: read-your-writes puts the read time at/after the commit. Without fast next
  // disabled for the reverse mapping reader this could fail with "Vector not found"; with it,
  // resolution stays on the filter's snapshot and row 1 is excluded when its ybctid is fetched.
  const auto query = "SELECT id FROM test AS t" + IndexQuerySuffix("[0.0, 0.0, 0.0]", kQueryLimit);
  auto ids = ASSERT_RESULT(conn.FetchRows<int64_t>(query));

  // Row 1 is deleted; the query must return the remaining nearest rows without erroring.
  ASSERT_FALSE(ids.empty());
  for (auto id : ids) {
    ASSERT_NE(id, 1);
  }
}

// Stress version of ConcurrentDeleteApplyDuringSearch. Instead of pinning one interleaving with
// sync points, it repeatedly deletes the nearest neighbors while searches run, widening the race
// window. The filter may accept a live reverse mapping, but before resolving its ybctid, the search
// may observe the delete tombstone and fail with "Vector not found". This inconsistency occurs when
// DBIter::FastNext exposes records written after the reverse mapping iterator's snapshot; a high
// max_nexts_to_avoid_seek makes this path easy to hit. With FastNext disabled for reverse mapping
// reads, filtering and resolution stay on the same snapshot. Each query must return a full page
// and exclude rows deleted before it started.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentDeleteApplyDuringSearchStress) {
  constexpr int64_t kNumRows = 200;
  constexpr size_t kQueryLimit = 10;
  // Keep at least 2 * kQueryLimit rows live so every query can fill a full page.
  constexpr int64_t kMaxDeletes = kNumRows - 2 * kQueryLimit;
  static constexpr uint64_t kApplyDelayMs = 100;
  const size_t kNumQueries = RegularBuildVsSanitizers<size_t>(15, 8);

  // Keep the filter active from the first query, before any delete has applied.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;
  // Make forward fetches step to the target instead of seeking, so freshly applied tombstones are
  // visible to the resolution reader (see the comment above).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_nexts_to_avoid_seek) = 100000;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  // Delay applies so deletes committed shortly before a query stay unapplied when its filter runs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_sleep_before_applying_intents_ms) = kApplyDelayMs;

  auto* sync_point = SyncPoint::GetInstance();
  // Sleep long enough for every apply pending at the filter read to land before resolution.
  sync_point->SetCallBack("DocVectorIndexImpl::Search:BeforeResolve", [](void*) {
    std::this_thread::sleep_for(3ms * kApplyDelayMs * kTimeMultiplier);
  });
  sync_point->EnableProcessing();

  std::atomic<int64_t> deleted{0};
  TestThreadHolder threads;
  threads.AddThreadFunctor([this, &deleted, &stop_flag = threads.stop_flag()] {
    auto delete_conn = ASSERT_RESULT(Connect());
    for (int64_t id = 1; id <= kMaxDeletes && !stop_flag.load(std::memory_order_acquire); ++id) {
      // Explicit transaction: a fast-path delete writes its reverse-mapping tombstone inline
      // instead of at apply time, so it cannot race with a search.
      ASSERT_OK(delete_conn.Execute("BEGIN"));
      ASSERT_OK(delete_conn.ExecuteFormat("DELETE FROM test WHERE id = $0", id));
      ASSERT_OK(delete_conn.Execute("COMMIT"));
      deleted.store(id, std::memory_order_release);
      std::this_thread::sleep_for(30ms * kTimeMultiplier);
    }
  });

  const auto query = "SELECT id FROM test AS t" + IndexQuerySuffix("[0.0, 0.0, 0.0]", kQueryLimit);
  for (size_t i = 0; i != kNumQueries; ++i) {
    auto deleted_before = deleted.load(std::memory_order_acquire);
    auto ids = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
    // Rows 1..deleted_before are deleted at the query read time and at least 2 * kQueryLimit rows
    // stay live, so a full page of rows above deleted_before is expected.
    ASSERT_EQ(ids.size(), kQueryLimit);
    for (auto id : ids) {
      ASSERT_GT(id, deleted_before);
    }
  }
  threads.Stop();
}

// A writer keeps moving the vectors of a few rows between Vector(id) and -Vector(id), while
// searchers read a row's vector and search near Vector(id) in one statement. Whenever the row is at
// Vector(id), the search has to return it. The search resolves candidate vectors through the
// reverse mapping, or fetches their rows when there is a filter or the index stores the ybctid as
// the payload. Either could see an update in the uncertainty window, i.e. after the read time, and
// leave out the vector without restarting the read.
//
// Covers https://github.com/yugabyte/yugabyte-db/issues/36618.
void PgVectorIndexSingleServerTest::TestSearchWithConcurrentUpdates(
    AddFilter add_filter, StorePayload store_payload) {
  constexpr int64_t kNumRows = 1000;
  constexpr int64_t kNumHotRows = 20;
  constexpr size_t kRowsPerTransaction = 5;
  constexpr size_t kNumSearchers = 4;
  const auto kRunTime = RegularBuildVsSanitizers<std::chrono::seconds>(15s, 10s);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = store_payload;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  TestThreadHolder threads;
  threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
    auto write_conn = ASSERT_RESULT(Connect());
    std::vector<int64_t> ids(kNumHotRows);
    std::iota(ids.begin(), ids.end(), 1);
    while (!stop_flag.load(std::memory_order_acquire)) {
      std::shuffle(ids.begin(), ids.end(), ThreadLocalRandom());
      ASSERT_OK(write_conn.Execute("BEGIN"));
      for (size_t i = 0; i != kRowsPerTransaction; ++i) {
        auto id = ids[i];
        auto vector = Vector(id);
        if (RandomUniformBool()) {
          for (auto& coordinate : vector) {
            coordinate = -coordinate;
          }
        }
        ASSERT_OK(write_conn.ExecuteFormat(
            "UPDATE test SET embedding = '$0' WHERE id = $1", AsString(vector), id));
      }
      ASSERT_OK(write_conn.Execute("COMMIT"));
    }
  });

  // id + 0 keeps the always true filter from becoming a key bound, so the search is done with a
  // DocDB filter, i.e. PgsqlVectorFilter fetches the row of every candidate vector.
  const std::string where = add_filter ? "WHERE id + 0 >= 0" : "";
  std::atomic<size_t> num_searches{0};
  std::atomic<size_t> num_misses{0};
  for (size_t i = 0; i != kNumSearchers; ++i) {
    threads.AddThreadFunctor(
        [this, &where, &num_searches, &num_misses, &stop_flag = threads.stop_flag()] {
      auto read_conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire)) {
        auto id = RandomUniformInt<int64_t>(1, kNumHotRows);
        auto near = AsString(Vector(id));
        auto result = read_conn.FetchRow<bool, int64_t>(Format(
            "SELECT (SELECT embedding = '$1' FROM test WHERE id = $0), "
            "(SELECT id FROM test $2 $3)",
            id, near, where, IndexQuerySuffix(near, 1)));
        if (!result.ok()) {
          ASSERT_TRUE(IsRetryable(result.status())) << result.status();
          continue;
        }
        ++num_searches;
        auto [is_near, found_id] = *result;
        if (is_near && found_id != id) {
          LOG(WARNING) << "Row " << id << " is at " << near << ", but search returned "
                       << found_id;
          ++num_misses;
        }
      }
    });
  }

  threads.WaitAndStop(kRunTime);
  LOG(INFO) << "Searches: " << num_searches << ", misses: " << num_misses;
  ASSERT_GT(num_searches, 0);
  ASSERT_EQ(num_misses, 0);
}

TEST_P(PgVectorIndexSingleServerTest, SearchWithConcurrentUpdates) {
  TestSearchWithConcurrentUpdates(AddFilter::kFalse, StorePayload::kFalse);
}

TEST_P(PgVectorIndexSingleServerTest, FilteredSearchWithConcurrentUpdates) {
  TestSearchWithConcurrentUpdates(AddFilter::kTrue, StorePayload::kFalse);
}

TEST_P(PgVectorIndexSingleServerTest, SearchWithConcurrentUpdatesStorePayload) {
  TestSearchWithConcurrentUpdates(AddFilter::kFalse, StorePayload::kTrue);
}

TEST_P(PgVectorIndexSingleServerTest, FilteredSearchWithConcurrentUpdatesStorePayload) {
  TestSearchWithConcurrentUpdates(AddFilter::kTrue, StorePayload::kTrue);
}

TEST_P(PgVectorIndexSingleServerTest, OnDiskSize) {
  // Make the heartbeat compute (and read OnDiskSize) on every heartbeat, and
  // heartbeat often.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);

  // Hold the writer right after it stores chunk->file, before it takes
  // VectorLSM::mutex_, so a heartbeat OnDiskSize read overlaps the
  // unsynchronized store and ThreadSanitizer reliably observes the race.
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_after_saving_chunk) = 200ms * kTimeMultiplier;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  // Vectors are added to the index asynchronously with respect to the SQL writes, and only flushed
  // chunks contribute to the on-disk size. Wait until every vector has been applied and indexed,
  // then flush so the index chunks are persisted and counted.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets(
      tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes));

  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    // The vector index size is reported via the on-disk size info (which feeds the UI) and must be
    // included in the active on-disk size.
    auto info = peer->GetOnDiskSizeInfo();
    ASSERT_GT(info.vector_index_disk_size, 0);
    ASSERT_GE(info.active_on_disk_size, info.vector_index_disk_size);
  }
}

// A manual (e.g. post-split) compaction can be requested just before a tablet is torn down. The
// requesting thread passes the RUNNING_STATUS() check in ScheduleManualCompaction() while the LSM
// is still running, then -- inside RegisterManualCompaction() -- it may block waiting for ongoing
// background compactions. Shutdown aborts those background tasks, which wakes the manual thread.
// VectorLSM shutdown only waits for compaction_tasks_ to become empty and does not account for the
// in-flight manual registration, so CompleteShutdown() can finish before the manual thread inserts
// its task. The manual thread then registers and submits a CompactionTask on a now shutting-down /
// destroyed VectorLSM; the task lingers in the shared priority thread pool and later runs against
// the freed object, matching the crash in CompactionTask::Run -> Completed -> Deregister.
//
// The interleaving is forced deterministically with sync point dependencies:
//   1) shutdown begins only after the manual compaction has passed the running check;
//   2) the manual compaction inserts its task only after shutdown has reached the compaction wait;
//   3) a task wrongly submitted during shutdown runs only after the VectorLSM has been destroyed.
// A correct fix observes the shutdown and refuses to register (Compact() returns a shutdown error);
// the buggy code registers a task that runs against the freed VectorLSM, which the guard in
// VectorLSM::LastSerialNo() turns into a deterministic failure (and a heap-use-after-free under
// ASAN). Uses a single tablet server so each sync point maps to a single VectorLSM instance.
//
// Covers https://github.com/yugabyte/yugabyte-db/issues/30554.
TEST_P(PgVectorIndexSingleServerTest, ManualCompactionDuringShutdown) {
  // Background compactions off: the only task we orchestrate is the manual compaction below.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      // Shutdown may begin only after the manual compaction passed the running check, so it does
      // not bail out early in ScheduleManualCompaction().
      {"VectorLSM::RegisterManualCompaction:Start", "VectorLSM::StartShutdown:Begin"},
      // The manual compaction inserts its task only after shutdown has started and reached the
      // compaction wait -- the lost-update window.
      {"VectorLSM::CompleteShutdown:Waiting", "VectorLSM::RegisterManualCompaction:BeforeRegister"},
      // A task wrongly submitted during shutdown runs only after the VectorLSM has been destroyed,
      // making the use-after-free deterministic.
      {"VectorLSM::~VectorLSM:Destroyed", "VectorLSM::CompactionTask::Run:Start"},
  });
  sync_point->EnableProcessing();

  // Hand the only external strong reference to the compaction thread; keep a weak ref to observe
  // destruction.
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty()) << "No tablet with a vector index found";
  std::shared_ptr<docdb::DocVectorIndex> index = indexes.front();
  indexes.clear();
  std::weak_ptr<docdb::DocVectorIndex> weak_index = index;

  TestThreadHolder threads;
  Status compact_status;
  threads.AddThreadFunctor([&compact_status, index] {
    compact_status = index->Compact();
  });
  index.reset();  // Main keeps only the weak ref.

  // Tear the table down so the VectorLSM shuts down. StartShutdown waits (via the dependency above)
  // until the manual compaction is running, so launch order does not matter.
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.Execute("DROP TABLE test"));
  });

  threads.WaitAndStop(120s * kTimeMultiplier);

  // A correct fix refuses to register a manual compaction once the LSM is shutting down. On the
  // buggy code a task is registered (status OK) and runs against the freed VectorLSM, tripping the
  // LastSerialNo() guard before we even get here.
  ASSERT_NOK(compact_status) << "Manual compaction was registered on a shutting-down VectorLSM";
  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 60s * kTimeMultiplier,
                    "vector index destruction"));
}

// Two compactions may run on the same VectorLSM in parallel when allowed by
// vector_index_allow_parallel_compactions and the per-tserver vector_index_num_compactions_limit.
// DoCompact used to update immutable_chunks_ by the position remembered at pick time, so when
// the compaction with the lower position (the manual one, always a whole prefix) finished first,
// the shifted positions made the other compaction erase wrong chunks, losing their vectors from
// search results.
//
// Forced interleaving (sync point callbacks keyed on the compaction type): the manual compaction
// picks the whole prefix and parks after merging, before the manifest update; four flushed
// chunks trigger a background compaction, which picks them and parks too; two more chunks are
// flushed to keep the stale erase in bounds; the manual compaction updates the chunks, then the
// background one does. On correct code all rows stay readable.
TEST_P(PgVectorIndexSingleServerTest, ParallelCompactions) {
  using vector_index::CompactionType;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_allow_parallel_compactions) = true;
  // The shared compaction token captures the value at creation, so set before creating the index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_num_compactions_limit) = 2;
  // Chunks locked by the running manual compaction do not count towards the trigger.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_files_number_compaction_trigger) = 4;

  constexpr size_t kBatch = 8;

  auto conn = ASSERT_RESULT(MakeIndex());

  size_t num_rows = 0;
  auto add_chunk = [this, &conn, &num_rows]() -> Status {
    RETURN_NOT_OK(InsertRows(conn, num_rows + 1, num_rows + kBatch));
    num_rows += kBatch;
    RETURN_NOT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
    return cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes);
  };

  // The manual compaction prefix: the empty creation chunk and one data chunk.
  ASSERT_OK(add_chunk());

  std::atomic<bool> manual_parked{false};
  std::atomic<bool> manual_updated{false};
  std::atomic<bool> background_parked{false};
  std::atomic<bool> background_updated{false};
  std::atomic<bool> release_manual{false};
  std::atomic<bool> release_background{false};

  auto wait_flag = [](std::atomic<bool>& flag, const std::string& description) {
    return LoggedWaitFor(
        [&flag]() -> Result<bool> { return flag.load(); }, 20s * kTimeMultiplier, description);
  };

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::DoCompact:Merged", [&](void* arg) {
        auto manual = *static_cast<CompactionType*>(arg) == CompactionType::kManual;
        (manual ? manual_parked : background_parked) = true;
        auto& release = manual ? release_manual : release_background;
        while (!release) {
          std::this_thread::sleep_for(10ms);
        }
      });
  sync_point->SetCallBack(
      "VectorLSM::DoCompact:ChunksUpdated", [&](void* arg) {
        auto manual = *static_cast<CompactionType*>(arg) == CompactionType::kManual;
        (manual ? manual_updated : background_updated) = true;
      });
  sync_point->EnableProcessing();

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(indexes.size(), 1);
  ASSERT_OK(indexes.front()->Compact());
  ASSERT_OK(wait_flag(manual_parked, "Manual compaction parked after merge"));

  // Enough small chunks for the size ratio picker, so a background compaction picks them all.
  for (int i = 0; i != 4; ++i) {
    ASSERT_OK(add_chunk());
  }
  auto overlap_status = wait_flag(background_parked, "Background compaction parked");
  LOG(INFO) << "Compactions overlap: " << overlap_status;

  // Keep the background scope away from the last chunk so its stale erase stays in bounds.
  ASSERT_OK(add_chunk());
  ASSERT_OK(add_chunk());

  release_manual = true;
  ASSERT_OK(wait_flag(manual_updated, "Manual compaction chunks update done"));

  release_background = true;
  if (overlap_status.ok()) {
    ASSERT_OK(wait_flag(background_updated, "Background compaction chunks update done"));
  }
  sync_point->DisableProcessing();
  sync_point->ClearAllCallBacks();

  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, ExpectedRows(num_rows)));
}

// Reproduces a data race between the in-place update of the tablet's vector index list in
// TabletVectorIndexes::DoCreateIndex and a lock-free reader iterating that same list -- the read
// that the post-split vector index compaction performs via VectorIndexList::WaitForCompaction().
//
// The reader and writer are coordinated only through relaxed atomics: introducing a
// mutex/latch/acquire-release would add a happens-before edge between the racing read and write and
// hide the bug. The missing synchronization (a relaxed use_count() load driving an in-place
// mutation) is exactly the defect under test. On buggy code TSAN reports a data race on the shared
// std::vector buffer; the copy-on-write fix keeps a published buffer immutable, so the reader's
// snapshot is never mutated in place.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentCreateIndexAndListRace) {
  auto conn = ASSERT_RESULT(MakeIndex());  // Table + first vector index: the list holds one entry.

  // Locate the single tablet that owns the vector index.
  tablet::TabletPtr tablet;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto candidate = peer->shared_tablet_maybe_null();
    if (!candidate) {
      continue;
    }
    auto list = candidate->vector_indexes().List();
    if (list && !list->empty()) {
      tablet = candidate;
      break;
    }
  }
  ASSERT_TRUE(tablet) << "No tablet with a vector index found";

  std::atomic<bool> reader_has_snapshot{false};
  std::atomic<bool> writer_parked{false};
  std::atomic<bool> reader_done{false};

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "TabletVectorIndexes::DoCreateIndex:BeforeListUpdate",
      [&writer_parked, &reader_done](void*) {
        // Let the reader perform its racing read now, then wait until it drops the snapshot so that
        // use_count() falls back to 1 and DoCreateIndex takes the in-place branch.
        writer_parked.store(true, std::memory_order_relaxed);
        while (!reader_done.load(std::memory_order_relaxed)) {}
      });
  sync_point->EnableProcessing();

  TestThreadHolder threads;
  threads.AddThreadFunctor(
      [&tablet, &reader_has_snapshot, &writer_parked, &reader_done] {
        // Snapshot the list under a brief shared lock (bumps use_count to 2), same as the
        // compaction path, and release the lock before signalling so the parked writer cannot
        // deadlock.
        auto list = tablet->vector_indexes().List();
        ASSERT_TRUE(list && !list->empty());
        reader_has_snapshot.store(true, std::memory_order_relaxed);

        // Wait until the writer is parked right before the in-place list update, so the racing read
        // is fresh, then iterate the buffer exactly as WaitForCompaction() does.
        while (!writer_parked.load(std::memory_order_relaxed)) {}
        for (const auto& index : *list) {
          ASSERT_TRUE(index != nullptr);  // Racing read of the shared vector buffer.
        }

        list = {};  // Drop the snapshot: use_count() falls back to 1.
        reader_done.store(true, std::memory_order_relaxed);
      });

  // Ensure the reader released the shared lock before the writer takes the exclusive lock: the
  // writer parks while holding vector_indexes_mutex_, so a reader still inside List() would
  // deadlock.
  while (!reader_has_snapshot.load(std::memory_order_relaxed)) {}

  // Writer: create a second vector index. DoCreateIndex takes the in-place branch and mutates the
  // shared buffer that the reader is still holding.
  ASSERT_OK(CreateIndex(conn, "another"));

  threads.WaitAndStop(60s * kTimeMultiplier);
  sync_point->DisableProcessing();
}

// Reproduces issue #32211: shutting a tablet down (e.g. on tombstone) must not deadlock when an
// intents-DB write is parked in a RocksDB write stall that never clears. RemoveIntents runs on the
// TabletPeer strand, and TabletPeer::CompleteShutdown drains that strand before it reaches the
// point that signals the tablet's RocksDB instances to shut down. So a writer parked in
// DBImpl::DelayWrite (which only escapes the stall once RocksDB is told it is shutting down) is
// never released, and the strand drain -- hence the whole shutdown -- hangs forever. The fix
// signals RocksDB shutdown early, in Tablet::StartShutdown, before the strand is drained.
TEST_P(PgVectorIndexSingleServerTest, ShutdownDuringIntentsWriteStall) {
  // DROP TABLE tears the tablet down only in the distributed (non-colocated) layout; in a colocated
  // database the table shares a tablet that stays alive, so the shutdown path under test is not
  // exercised.
  if (IsColocated()) {
    GTEST_SKIP() << "DROP TABLE does not tear the tablet down in colocated mode";
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 16;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get(), 30s * kTimeMultiplier));

  // Locate the tablet peer holding the vector index; its provisional records (intents) DB is the
  // one we stall.
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), 1) << "Expected exactly one tablet peer with a vector index";
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto* intents_db = down_cast<rocksdb::DBImpl*>(tablet->intents_db());

  CountDownLatch intents_write_stalled{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "DBImpl::DelayWrite:Wait",
      [&intents_write_stalled](void*) { intents_write_stalled.CountDown(); });
  sync_point->EnableProcessing();

  // Write provisional intents in a distributed transaction, then force a never-clearing write stall
  // on the intents DB. Committing applies the transaction and schedules RemoveIntents, which writes
  // to the intents DB and parks in DelayWrite. The stall is owned by the DB, so it is cleaned up
  // when the tablet is torn down -- no external token to release.
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(txn_conn.Execute(
      "INSERT INTO test VALUES (1001, '[1,2,3]'), (1002, '[4,5,6]'), (1003, '[7,8,9]')"));
  // Force the buffered provisional intents to flush to the tablet before we stop writes (a primary
  // key read keeps the vector-index seq-scan guard happy).
  ASSERT_RESULT(txn_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1001"));

  intents_db->TEST_StopWrites();

  ASSERT_OK(txn_conn.CommitTransaction());

  ASSERT_TRUE(intents_write_stalled.WaitFor(60s * kTimeMultiplier))
      << "No intents write parked in the write stall";

  // Tear the tablet down. On the buggy code this hangs forever in TabletPeer::CompleteShutdown,
  // waiting for the parked RemoveIntents to drain from the strand, and the test times out. On the
  // fixed code Tablet::StartShutdown signals RocksDB shutdown before the strand drain, releasing
  // the stalled write, so the drop completes.
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgVectorIndexSingleServerTest, FailedVectorFlushRetiresIntentsAndAdmission) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  auto conn = ASSERT_RESULT(MakeIndex());
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), 1);
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs,
                          rocksdb::FlushReason::kTestOnly));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(conn, 1, 16));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  // Remove regular RocksDB from the dependency so the filter can only be waiting for vector data.
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular,
                          rocksdb::FlushReason::kTestOnly));
  auto indexes = tablet->vector_indexes().List();
  ASSERT_EQ(indexes->size(), 1);
  auto index = indexes->front();
  auto* intents_db = tablet->intents_db();
  ASSERT_NE(intents_db, nullptr);
  auto flushed_frontier = [](auto* storage) {
    auto frontier = storage->GetFlushedFrontier();
    return frontier ? frontier->ToString() : std::string();
  };
  const auto intents_frontier = flushed_frontier(intents_db);
  const auto vector_frontier = flushed_frontier(index.get());

  CountDownLatch saving(1), release_save(1), intents_blocked(1), shutdown_done(1);
  CountDownLatch vector_retiring(1), release_vector(1), intents_failed(1), release_intents(1);
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("VectorLSM::DoSaveChunk:Status", [&](void* arg) {
    saving.CountDown();
    release_save.Wait();
    *static_cast<Status*>(arg) = STATUS(IOError, "injected vector dependency failure");
  });
  sync->SetCallBack("Tablet::IntentsDbFlushFilter:Blocked", [&](void* arg) {
    if (arg == tablet.get()) {
      intents_blocked.CountDown();
    }
  });
  sync->SetCallBack("VectorLSM::SaveChunk:BeforeRetirement", [&](void*) {
    vector_retiring.CountDown();
    release_vector.Wait();
  });
  sync->SetCallBack("DBImpl::WaitAfterBackgroundError", [&](void* arg) {
    if (arg == intents_db) {
      intents_failed.CountDown();
      release_intents.Wait();
    }
  });
  sync->EnableProcessing();
  auto* server = cluster_->mini_tablet_server(0)->server();
  auto active = server->metric_entity()->FindOrNull<AtomicGauge<uint64_t>>(
      METRIC_tablet_flush_active);
  auto& flusher = server->tablet_manager()->tablet_flusher();
  auto result = std::make_shared<std::promise<std::pair<Status, TabletId>>>();
  auto future = result->get_future();
  bool admitted = false;
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release_save.CountDown();
    release_vector.CountDown();
    release_intents.CountDown();
    if (admitted && future.valid()) {
      future.wait();
    }
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  tserver::FlushTabletsRequestPB request;
  request.set_operation(tserver::FlushTabletsRequestPB::FLUSH);
  ASSERT_OK(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 30s * kTimeMultiplier,
      [result](const Status& status, const TabletId& id) { result->set_value({status, id}); }));
  admitted = true;
  ASSERT_TRUE(saving.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_blocked.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(flushed_frontier(intents_db), intents_frontier);
  release_save.CountDown();
  ASSERT_TRUE(vector_retiring.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_failed.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_db->WaitForFlush().IsIOError());
  // The vector save task has not retired yet, so the batch still holds its admission.
  ASSERT_EQ(active->value(), 1);
  ASSERT_EQ(future.wait_for(20ms), std::future_status::timeout);
  ASSERT_TRUE(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 5s,
      [](const Status&, const TabletId&) {}).IsServiceUnavailable());

  // Once the vector task retires, the batch completes even though the intents DB's failed job is
  // still parked in its error cleanup: RocksDB drains that itself, and waiting here would also
  // cover every later automatic flush.
  release_vector.CountDown();
  ASSERT_TRUE(index->WaitForFlush().IsIOError());
  ASSERT_EQ(future.wait_for(30s * kTimeMultiplier), std::future_status::ready);
  auto [status, failed_tablet_id] = future.get();
  ASSERT_OK(WaitFor([&] { return active->value() == 0; }, 5s * kTimeMultiplier,
                    "Admission released before error cleanup"));
  release_intents.CountDown();
  ASSERT_TRUE(status.IsIOError()) << status;
  ASSERT_EQ(failed_tablet_id, tablet->tablet_id());
  ASSERT_EQ(active->value(), 0);
  ASSERT_EQ(flushed_frontier(intents_db), intents_frontier);
  ASSERT_EQ(flushed_frontier(index.get()), vector_frontier);

  threads.AddThreadFunctor([&] {
    cluster_->mini_tablet_server(0)->Shutdown();
    shutdown_done.CountDown();
  });
  ASSERT_TRUE(shutdown_done.WaitFor(30s * kTimeMultiplier));
  threads.JoinAll();
}

TEST_P(PgVectorIndexSingleServerTest, ReverseMappingCleanup) {
  // Set number of files for background compaction explicitly.
  constexpr auto kRetentionIntervalSec = 4;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_timestamp_history_retention_interval_sec) = kRetentionIntervalSec;

  auto conn = ASSERT_RESULT(MakeIndex());

  // Get tablet and corresponding rocksdb dir.
  auto table_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(table_peers.size(), 1);
  auto tablet = ASSERT_RESULT(table_peers.front()->shared_tablet());
  const auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
  auto* db = tablet->regular_db();
  auto* db_impl = down_cast<rocksdb::DBImpl*>(db);

  // Setup helpers.
  auto flush_tablet_and_wait = [&tablet, db_impl, cluster = cluster_.get()](
      const std::string& description) -> Status {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster, 10s));
    RETURN_NOT_OK(tablet->Flush(
        tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs, rocksdb::FlushReason::kTestOnly));

    // Wait for the files are really being flushed.
    SleepFor(MonoDelta::FromMilliseconds(200));
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumRunningFlushes() == 0;
    }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  auto compact_tablet = [&tablet] {
    return tablet->ForceManualRocksDBCompact(docdb::SkipFlush::kTrue);
  };

  auto wait_for_compaction_done = [db_impl](const std::string& description) -> Status {
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumBackgroundCompactionsScheduled() == 0 &&
             db_impl->TEST_NumTotalRunningCompactions() == 0;
      }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  TestKVFormatter formatter;
  auto run_sst_dump = [this, db, &formatter] -> Status {
    formatter.Clear();
    for (const auto& live_file : db->GetLiveFilesMetaData()) {
      RETURN_NOT_OK(RunSstDump(formatter, live_file.BaseFilePath()));
    }
    return Status::OK();
  };

  // Initial insert.
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, /* end_row = */ 5));
  ASSERT_OK(flush_tablet_and_wait("Initial flush"));

  // Make some changes to a next SST file.
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));

  // Force a chunk flush while the compaction owns the manifest; such a chunk used to be lost
  // from the manifest, hanging the sync flush below. Wait for vector inserts first so the flush
  // has a chunk to save; otherwise the compaction would wait on the second dependency forever.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto vector_indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(vector_indexes.size(), 1);

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"VectorLSM::DoCompact:ManifestAcquired", "VectorLSM::DoSaveChunk:BeforeManifestCheck"},
      {"VectorLSM::DoSaveChunk:ManifestWriteSkipped", "VectorLSM::DoCompact:BeforeManifestUpdate"},
  });
  sync_point->EnableProcessing();

  // Compact only schedules; the compaction races with the flush below.
  ASSERT_OK(vector_indexes.front()->Compact());

  ASSERT_OK(flush_tablet_and_wait("Flush for inital updates"));

  ASSERT_OK(vector_indexes.front()->WaitForCompaction());
  sync_point->DisableProcessing();

  // Wait less than retention period and make sure no tombstoned reverse mapping records deleted.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec / 4.0));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("First compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_2_vector_1 -> DEL
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_1 -> DEL
      )#",
      formatter.FormatVectorsMeta());

  // Wait enough time to make sure tombstoned records are deleted during full compactions if
  // they are outside retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("Second compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());
  const size_t oldest_file = db->GetLiveFilesMetaData().front().name_id;
  LOG(INFO) << "Oldest file number [" << oldest_file << "] to be excluded from compaction";

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
      )#",
      formatter.FormatVectorsMeta());

  // Let's produce more files.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[11, 21, 31]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 6, /* end_row = */ 7));
  ASSERT_OK(flush_tablet_and_wait("Flush after update"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 3"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 6"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[12, 22, 32]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 8, /* end_row = */ 8));
  ASSERT_OK(flush_tablet_and_wait("Flush after deletes"));

  // Keep the number of files to understand how many additional files should be produced to let
  // a background compaction automatically happen. It is expected to have 3 files at this point,
  // one - after comaction plus two - after two flushes, but in some cases an additional flush of
  // an unknown nature may happen, producing one more file. That does not break the logic, but it's
  // good to make additional research to understand where that flush comes from.
  constexpr size_t kNumFilesExpected = 3;
  const size_t num_files = db->GetLiveFilesMetaData().size();
  if (num_files == kNumFilesExpected) {
    LOG(INFO) << "Current number of files: " << num_files;
  } else {
    LOG(WARNING) << "Current number of files: " << num_files << ", expected: " << kNumFilesExpected;
    ASSERT_GE(num_files, kNumFilesExpected);
  }

  // Make sure everything expected is seen after the flush.
  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_3 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_4_vector_3 -> DEL
          ybctid_8_vector_1 -> ybctid_8
      )#",
      formatter.FormatVectorsMeta());

  // Make deletes pass the retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec + 1));
  LOG(INFO) << "Passed enough time to make tombstones be outside retention period";

  // Update exclude SST file functor to exclude exactly the oldest file, to simulate
  // background compaction for N-1 latest files.
  auto excluder = std::make_shared<rocksdb::CompactionFileExcluder>(
      [oldest_file](const rocksdb::FileMetaData& file){
        bool need_exclude = oldest_file == file.fd.GetNumber();
        LOG(INFO) << (need_exclude ? "Excluding" : "Keeping") << " file: " << file.fd.ToString();
        return need_exclude;
      });
  db_impl->TEST_SetExcludeFromCompaction(excluder);

  // Need to add more files to trigger background compaction, adding 1 for the excluded file.
  const size_t need_files = GetFileNumCompactionTrigger() - num_files + 1;
  LOG(INFO) << "Need " << need_files << " files to trigger background compaction";
  std::stringstream expected_tail;
  for (size_t i = 0; i < need_files; ++i) {
    const size_t key_idx = 9 + i;
    ASSERT_OK(InsertRows(conn, /* start_row = */ key_idx, /* end_row = */ key_idx));
    ASSERT_OK(flush_tablet_and_wait(Format("Flush $0 done", i)));
    expected_tail << Format("ybctid_$0_vector_1 -> ybctid_$0\n", key_idx);
    LOG(INFO) << "Flushed data to " << i << " out of " << need_files << " files";
  }

  // Give some time for background compaction to start.
  SleepFor(MonoDelta::FromSeconds(1));
  LOG(INFO) << "Background compaction should have been started";
  ASSERT_OK(wait_for_compaction_done("Final compaction"));
  ASSERT_EQ(2, db->GetLiveFilesMetaData().size());

  // Check the final state in SST files.
  ASSERT_OK(run_sst_dump());
  auto output = formatter.FormatVectorsMeta();
  LOG(INFO) << "Parsed SST dump output:\n" << output;

  // Data from excluded file
  // ybctid_1_vector_1 -> ybctid_1
  // ybctid_3_vector_1 -> ybctid_3
  // ybctid_5_vector_1 -> ybctid_5
  // ybctid_4_vector_2 -> ybctid_4
  //
  // Other old files:
  // ybctid_4_vector_3 -> ybctid_4  => outside retention, should be filtered due to newer value
  // ybctid_4_vector_2 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_6_vector_1 -> ybctid_6  => outside retention, should be filtered due to delete
  // ybctid_7_vector_1 -> ybctid_7  => visible
  // ybctid_3_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_6_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_4 -> ybctid_4  => visible
  // ybctid_4_vector_3 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_8_vector_1 -> ybctid_8  => outside retention, but should be kept as updates too fresh.
  //
  // Fresh files: => fresh data, within retention period.
  // ybctid_9_vector_1 -> ybctid_9
  // ...
  // ybctid_N_vector_3 -> ybctid_N
  ASSERT_STR_EQ_VERBOSE_TRIMMED(util::TrimWhitespaceFromEveryLine(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_4_vector_3 -> DEL
          ybctid_8_vector_1 -> ybctid_8
      )#" + expected_tail.str()),
      output);
}

////////////////////////////////////////////////////////
// PgVectorIndexSmallBlockCacheTest
////////////////////////////////////////////////////////

// Reproduces #32357: a vector index backed by usearch/hnswlib allocates its graph state on the
// heap, accounted under the per-tablet "vector_indexes" MemTracker. This consumption is additive
// on top of whatever the RocksDB block cache already holds, so a server whose block cache is full
// can be pushed over the soft/hard memory limit purely by building a vector index.
//
// The test sizes a small block cache, fills and reads a regular (non-vector) table to populate the
// cache to its cap, then builds a vector index smaller than the cache. With the fix each chunk is
// registered in the block cache as reserved (unloadable) space, so building the index evicts cached
// blocks and the cache releases approximately the index footprint. Without the fix the cache is
// untouched and the index simply grows total memory -- the regression this reproduces.
//
// Parameterized by vector index engine; colocation and packing are fixed since they do not affect
// the heap footprint or its block cache reservation. All engines build their mutable chunk with
// usearch/hnswlib on the heap (the yb_hnsw block-cache format is only the saved/loaded form, which
// this test never reaches because it keeps the index mutable), so the reservation applies to all.

// Varies only the vector index engine; colocation and packing are pinned.
using PgVectorIndexEngineOnlyParam = VectorIndexEngine;

template <>
struct TestParamTraits<PgVectorIndexEngineOnlyParam> {
  using ParamType = PgVectorIndexEngineOnlyParam;

  static bool IsColocated(const ParamType&) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return param;
  }

  static PackingMode GetPackingMode(const ParamType&) {
    return PackingMode::kNone;
  }

  static auto TestParamGenerator() {
    return testing::ValuesIn(kVectorIndexEngineArray);
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      // Drop the leading "k" of the enum value name, e.g. "kYbHnswHnswlib" -> "YbHnswHnswlib".
      return ToString(param_info.param).substr(1);
    };
  }
};

class PgVectorIndexSmallBlockCacheTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexEngineOnlyParam> {
  using Base = PgVectorIndexTestParamsDecoratorBase<
      PgVectorIndexSingleServerTestBase, PgVectorIndexEngineOnlyParam>;

 protected:
  // Small block cache so it fills quickly and so a single vector index comfortably exceeds the
  // space a chunk reserves. The index footprint we build below stays under this, so the cache is
  // not driven empty -- it releases approximately the index footprint and keeps the rest.
  static constexpr int64_t kBlockCacheBytes = 80_MB;

  void SetUp() override {
    // db_block_cache_size_bytes is read once when the shared block cache is built during cluster
    // startup, so unlike the other flags this test tweaks it cannot be set from the test body.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_size_bytes) = kBlockCacheBytes;
    Base::SetUp();
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexSmallBlockCacheTest);

TEST_P(PgVectorIndexSmallBlockCacheTest, IndexReservesBlockCacheSpace) {
  constexpr size_t kVectorDimensions = 768;
  // Width of the filler column. The block cache holds uncompressed data blocks, so total filler
  // bytes must exceed kBlockCacheBytes for the cache to fill to its cap once read back. Wider rows
  // mean fewer of them for the same byte volume, which keeps the insert (per-row bound) fast; the
  // column uses PLAIN storage so PG keeps the value inline instead of TOASTing/compressing it.
  constexpr int kFillerRowBytes = 4000;
  // Filler bytes (rows * kFillerRowBytes) must exceed kBlockCacheBytes so the cache fills to cap.
  constexpr auto kFillerRows = static_cast<size_t>(kBlockCacheBytes * 11 / 10 / kFillerRowBytes);
  // Keep the resulting index footprint comfortably below kBlockCacheBytes so the cache releases
  // approximately the index size rather than being driven empty (which would cap the release).
  constexpr auto kVectorRows = RegularBuildVsDebugVsSanitizers<int64_t>(8000, 8000, 6000);
  constexpr size_t kFillerChunkRows = 5000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kVectorRows;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());

  // 1. Regular table without a vector index. Fill it, compact it to disk, then read it back so the
  // block cache fills with its data blocks.
  ASSERT_OK(conn.Execute("CREATE TABLE filler (id bigserial PRIMARY KEY, payload text)"));
  // PLAIN storage keeps the wide payload inline and uncompressed, so reading it actually fills the
  // block cache (a TOASTed/compressed value would occupy far less cache than its logical size).
  ASSERT_OK(conn.Execute("ALTER TABLE filler ALTER COLUMN payload SET STORAGE PLAIN"));
  for (size_t start = 0; start < kFillerRows; start += kFillerChunkRows) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO filler (payload) SELECT repeat('x', $0) FROM generate_series(1, $1)",
        kFillerRowBytes, std::min(kFillerChunkRows, kFillerRows - start)));
  }
  ASSERT_OK(cluster_->CompactTablets());

  // Read the whole table to populate the block cache up to its cap.
  ASSERT_OK(conn.FetchRow<int64_t>("SELECT count(length(payload)) FROM filler"));
  const auto cache_before = BlockCacheConsumption(cluster_.get());
  ASSERT_GT(cache_before, kBlockCacheBytes * 80 / 100)
      << "Block cache was not filled to at least 80% of its capacity; adjust filler sizing.";

  // Cheap build parameters: this test measures memory, not recall, and a large ef_construction
  // would make the build too slow to fit the time budget.
  auto index_conn = ASSERT_RESULT(MakeIndex(
      kVectorDimensions, /* table_exists= */ false, "ef_construction = 50, m = 16, m0 = 32"));

  // Insert the vectors and wait until they have all been added to the index (the build is async, so
  // the graph keeps growing after the inserts return).
  constexpr int64_t kBatchRows = 1000;
  for (int64_t start = 1; start <= kVectorRows; start += kBatchRows) {
    ASSERT_OK(InsertBatch(index_conn, start, std::min(kBatchRows, kVectorRows - start + 1)));
  }
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // The fix registers each vector index chunk in the block cache as reserved (unloadable) space, so
  // building the index evicts cached blocks instead of growing total memory. Without the fix the
  // cache is untouched (release ~ 0), which is exactly the regression #32357 describes.
  const auto index_size = VectorIndexPeakConsumption(cluster_.get());
  const auto cache_after = BlockCacheConsumption(cluster_.get());
  const auto released = cache_before - cache_after;
  const auto free_space_before = kBlockCacheBytes - cache_before;
  LOG(INFO) << "Block cache released " << released << " bytes (before=" << cache_before
            << ", after=" << cache_after << ", free before=" << free_space_before
            << ") for a vector index of " << index_size << " bytes";

  ASSERT_GT(index_size, 0);
  // The cache must be full enough that the index cannot simply fit in the unused space -- otherwise
  // building it would not need to evict anything and the test would not exercise the reservation.
  ASSERT_LT(free_space_before, index_size / 2)
      << "Block cache had too much free space before the index build; increase the filler so the "
         "index build is forced to evict.";
  // Building the index reserves index_size bytes in the cache. Up to free_space_before of that fits
  // in the unused capacity; the remainder must be reclaimed by evicting cached blocks. Require at
  // least half of that remainder to be freed -- a deliberately loose bound that absorbs the
  // difference between the heap build peak and the more compact on-disk form that ends up resident,
  // plus estimate-vs-actual reservation rounding, while still failing hard when nothing is freed.
  ASSERT_GE(released, (index_size - free_space_before) / 2)
      << "Block cache did not release space for the vector index; the index footprint is not being "
         "accounted within the block cache budget.";
}

}  // namespace yb::pgwrapper
