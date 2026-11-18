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

#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"

#include "yb/docdb/doc_vector_index.h"

#include "yb/dockv/doc_key.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

METRIC_DECLARE_event_stats(vector_index_partitions_queried);

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(tasks_tracker_num_tasks);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_include_into_post_split_compaction);
DECLARE_bool(vector_index_delete_list_after_rows);
DECLARE_bool(vector_index_skip_filter_check);
DECLARE_bool(vector_index_store_payload);
DECLARE_bool(TEST_vector_index_corrupt_inserted_payloads);
DECLARE_bool(TEST_vector_index_corrupt_inserted_vectors);
DECLARE_bool(TEST_vector_index_duplicate_inserted_vectors);
DECLARE_bool(TEST_vector_index_exact);
DECLARE_bool(TEST_vector_index_skip_insert);
DECLARE_bool(TEST_vector_index_skip_reverse_mapping_write);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_uint64(vector_index_backfill_single_chunk_size_bytes);
DECLARE_int64(db_block_size_bytes);
DECLARE_uint64(post_split_compaction_input_size_threshold_bytes);
DECLARE_uint32(vector_index_tenant_hash_group_and_range_components);
DECLARE_uint64(vector_index_initial_chunk_memstore_size_mb);
DECLARE_uint64(vector_index_initial_chunk_size);
DECLARE_uint64(vector_index_max_extra_tied_results);
DECLARE_uint64(vector_index_max_merge_tasks);

namespace yb::docdb {

extern bool TEST_vector_index_clear_result_entries_once;
extern bool TEST_vector_index_filter_allowed;

} // namespace yb::docdb

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;
extern std::optional<bool> TEST_vector_index_skip_reverse_mapping_backfill;

} // namespace yb::tablet

namespace yb::pgwrapper {

class PgVectorIndexUtilTest : public PgVectorIndexSingleServerDumpTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }

  void TestFastPathVectorDeletion(const std::string& statement);
  void TestUnappliedVectorUpdates();
  void TestBackfillBatches(uint64_t single_chunk_size_bytes);
};

TEST_F(PgVectorIndexUtilTest, BackfillSkipsReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  auto before_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // Table owned vector reverse mapping populating reuses insert logic, which updates write_id
  // component for the hybrid time. That's why the output is ordered by the timestamp.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
      )#",
      before_backfill);

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto after_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(before_backfill, after_backfill);
}

TEST_F(PgVectorIndexUtilTest, BackfillWritesReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  auto before_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", before_backfill);

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto after_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // The order is deterministic and stable, but follows [HT, str(hash, id)] ordering, where
  // HT is the backfill time and is the same for all entries for this particular backfill case.
  // The difference from the insertion logic is that backfill does not increment the write_id
  // component for the hybrid time, and that's how each entry has the same timestamp.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_1 -> ybctid_4
          ybctid_2_vector_1 -> ybctid_2
          ybctid_1_vector_1 -> ybctid_1
      )#",
      after_backfill);
}

// Backfill inserts rows in batches of a default mutable chunk's worth of vectors. For
// high-dimension vectors the chunk memory limit bounds that well below
// vector_index_initial_chunk_size, so the rows arrive in several batches. With
// single_chunk_size_bytes set, backfill first counts the rows to size one chunk for all of them,
// then restarts the scan and inserts them into that chunk in the same batches.
void PgVectorIndexUtilTest::TestBackfillBatches(uint64_t single_chunk_size_bytes) {
  constexpr size_t kDimensions = 1024;
  constexpr size_t kNumRows = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backfill_single_chunk_size_bytes) =
      single_chunk_size_bytes;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_memstore_size_mb) = 1;
  // The batch sizes below must come from a single tablet's backfill.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable(kDimensions));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  std::mutex mutex;
  std::vector<size_t> chunk_sizes;
  std::vector<size_t> batch_sizes;
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "TabletVectorIndexes::Backfill:ChunkSize",
      [&mutex, &chunk_sizes](void* arg) {
        std::lock_guard lock(mutex);
        chunk_sizes.push_back(*static_cast<size_t*>(arg));
      });
  sync_point->SetCallBack(
      "VectorIndexBackfillHelper::Flush:NumEntries",
      [&mutex, &batch_sizes](void* arg) {
        std::lock_guard lock(mutex);
        batch_sizes.push_back(*static_cast<size_t*>(arg));
      });
  sync_point->EnableProcessing();
  auto se = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  std::lock_guard lock(mutex);
  LOG(INFO) << "Backfill chunk sizes: " << AsString(chunk_sizes)
            << ", batch sizes: " << AsString(batch_sizes);
  // The budget fits far more than kNumRows vectors, so the counting pass sizes the chunk for
  // exactly the rows in the table.
  ASSERT_EQ(chunk_sizes, std::vector<size_t>{single_chunk_size_bytes ? kNumRows : 0});
  ASSERT_GT(batch_sizes.size(), 1);
  // Every batch but the last is full.
  for (size_t i = 1; i + 1 < batch_sizes.size(); ++i) {
    ASSERT_EQ(batch_sizes[i], batch_sizes.front());
  }
  ASSERT_LE(batch_sizes.back(), batch_sizes.front());
  // The scan restarted after the counting pass: every row is inserted exactly once.
  ASSERT_EQ(std::accumulate(batch_sizes.begin(), batch_sizes.end(), size_t{0}), kNumRows);
}

TEST_F(PgVectorIndexUtilTest, BackfillBatchesByChunkMemoryLimit) {
  TestBackfillBatches(/* single_chunk_size_bytes= */ 0);
}

TEST_F(PgVectorIndexUtilTest, BackfillSingleChunkBatchesByChunkMemoryLimit) {
  TestBackfillBatches(/* single_chunk_size_bytes= */ 1ULL << 30);
}

TEST_F(PgVectorIndexUtilTest, SearchSkipsTombstonedReverseMapping) {
  constexpr size_t kNumRows = 50;
  constexpr size_t kQueryLimit = 75;

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // Tombstone reverse mappings for the first batch.
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM test WHERE id <= $0", kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  const auto kQuery = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kQueryLimit);

  ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_clear_result_entries_once) = true;

  // Only rows 51..100 have live reverse mappings.
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(kQuery));
  ASSERT_EQ(rows.size(), kNumRows);

  ASSERT_FALSE(ANNOTATE_UNPROTECTED_READ(docdb::TEST_vector_index_clear_result_entries_once));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/31322.
TEST_F(PgVectorIndexUtilTest, NumTopVectorsToRemoveExceedsResultEntries) {
  constexpr size_t kNumRows = 50;
  constexpr size_t kQueryLimit = 75;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  // Needs legacy ownership: pre-index inserts must not write reverse mappings so that skipping
  // backfill leaves rows 1..kNumRows unresolvable during search.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, kNumRows));

  // Skip reverse mapping backfill to make sure there are no reverse mapping entries for the rows.
  // The search will drop these results with vector_index_skip_filter_check enabled, so the first
  // page will resolve fewer than kQueryLimit rows while could_have_more_data stays true, forcing
  // a second fetch with num_top_vectors_to_remove_ > 0.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = true;
  ASSERT_OK(CreateIndex(conn));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = false;
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // Insert the second half of the rows with the reverse mapping entries.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  const std::string kQuery = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kQueryLimit);

  // Will be triggered only on the second page of the query simulating reverse-mapping misses.
  ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_clear_result_entries_once) = true;

  // Only half of the inserted rows (51..100) have reverse mappings and hence could be fetched.
  // The test fails at this query with the stack trace from the ticket without the fix.
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(kQuery));
  ASSERT_EQ(rows.size(), kNumRows);

  // Make sure the test hit the test path that clears result entries.
  ASSERT_FALSE(ANNOTATE_UNPROTECTED_READ(docdb::TEST_vector_index_clear_result_entries_once));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/37003.
// The prefetch size is the scan's limit, so WITH TIES needs more than one page. A page returns the
// whole tie at its end, so the next page, which skips rows by count, does not cut it. Ties past
// vector_index_max_extra_tied_results are cut, then the rows of the tie after the cut are left out,
// but no row is returned twice.
TEST_F(PgVectorIndexUtilTest, PagedSearchWithTies) {
  constexpr int kNumGroups = 10;
  constexpr int kGroupSize = 8;
  constexpr size_t kMaxExtraTiedResults = 2;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn));

  // All rows of group g share the vector [10 * g, 0, 0].
  std::vector<std::vector<int64_t>> groups(kNumGroups);
  int64_t id = 0;
  for (int g = 0; g != kNumGroups; ++g) {
    for (int i = 0; i != kGroupSize; ++i) {
      groups[g].push_back(++id);
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, '[$1, 0, 0]')", id, 10 * g));
    }
  }
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto rounds = [this] {
    uint64_t result = 0;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      result += cluster_->mini_tablet_server(i)->metric_entity()
          .FindOrCreateMetric<EventStats>(&METRIC_vector_index_partitions_queried)->underlying()
          ->TotalCount();
    }
    return result;
  };

  auto check = [&](const std::string& stage) -> Status {
    const auto max_extra_tied_results = FLAGS_vector_index_max_extra_tied_results;
    for (int g = 0; g != kNumGroups; ++g) {
      auto rounds_before = rounds();
      auto rows = VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
          "SELECT id FROM test ORDER BY embedding $0 '[$1, 0, 0]' FETCH FIRST 1 ROWS WITH TIES",
          pgwrapper::DistanceOpName(distance_kind_), 10 * g)));
      auto context = Format("$0, group $1: $2", stage, g, AsString(rows));
      // The search must have paged for the test to cover the issue.
      SCHECK_GT(rounds() - rounds_before, 1U, IllegalState, context);
      std::ranges::sort(rows);
      SCHECK(std::ranges::adjacent_find(rows) == rows.end(), IllegalState,
             "Duplicate rows, " + context);
      SCHECK(std::ranges::includes(groups[g], rows), IllegalState,
             "Rows of another group, " + context);
      const size_t expected_size = std::min<size_t>(kGroupSize, 1 + max_extra_tied_results);
      SCHECK_EQ(rows.size(), expected_size, IllegalState, context);
    }
    return Status::OK();
  };

  // Before the flush the mutable hnswlib chunk serves the search, after the compaction YbHnsw does.
  ASSERT_OK(check("Before flush"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_extra_tied_results) = kMaxExtraTiedResults;
  ASSERT_OK(check("Before flush, ties cut"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_extra_tied_results) = 1024;
  ASSERT_OK(cluster_->CompactTablets());
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
  ASSERT_OK(check("After compaction"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_extra_tied_results) = kMaxExtraTiedResults;
  ASSERT_OK(check("After compaction, ties cut"));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/37003.
// The join rejects the rows at the query point. With vector_index_max_extra_tied_results = 0 the
// pages cut the tie there, so each page drops the tied rows the re-run search returns again. A
// tablet that is still dropping ties must not let another tablet's farther row out before its own
// nearer one.
TEST_F(PgVectorIndexUtilTest, PagedSearchWithTiesAcrossTablets) {
  constexpr int kNumClusters = 20;
  constexpr int kNumTies = 20;
  constexpr int kNumFar = 10;

  num_pre_split_tablets_ = 2;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(conn.Execute("CREATE TABLE keep (id BIGINT PRIMARY KEY)"));

  // Cluster c has kNumTies rows at its center [100 * c, 0, 0], the nearest row to the center after
  // them at distance 10, and kNumFar rows at distance 20.
  std::vector<int64_t> nearest(kNumClusters);
  int64_t id = 0;
  for (int c = 0; c != kNumClusters; ++c) {
    for (int i = 0; i != kNumTies; ++i) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, '[$1, 0, 0]')", ++id, 100 * c));
    }
    nearest[c] = ++id;
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, '[$1, 10, 0]')", id, 100 * c));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO keep VALUES ($0)", id));
    for (int i = 0; i != kNumFar; ++i) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, '[$1, 0, 20]')", ++id, 100 * c));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO keep VALUES ($0)", id));
    }
  }
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // A batched nested loop or a hash join would sort the joined rows by distance again.
  ASSERT_OK(conn.Execute("SET yb_bnl_batch_size = 1"));
  ASSERT_OK(conn.Execute("SET enable_hashjoin = off"));
  ASSERT_OK(conn.Execute("SET enable_mergejoin = off"));

  auto rounds = [this] {
    return cluster_->mini_tablet_server(0)->metric_entity()
        .FindOrCreateMetric<EventStats>(&METRIC_vector_index_partitions_queried)->underlying()
        ->TotalCount();
  };

  for (auto max_extra_tied_results : {uint64_t{1024}, uint64_t{0}}) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_extra_tied_results) = max_extra_tied_results;
    for (int c = 0; c != kNumClusters; ++c) {
      auto query = Format(
          "SELECT test.id FROM test INNER JOIN keep ON test.id = keep.id "
          "ORDER BY test.embedding $0 '[$1, 0, 0]' LIMIT 1",
          pgwrapper::DistanceOpName(distance_kind_), 100 * c);
      auto rounds_before = rounds();
      auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
      auto context = Format("Cluster $0, max extra tied results $1", c, max_extra_tied_results);
      ASSERT_GT(rounds() - rounds_before, 1U) << context;
      ASSERT_EQ(rows, std::vector<int64_t>{nearest[c]}) << context;
    }
  }
}

// Rows 1..20 hold [i, 2i, 3i]: row 20 is the nearest of them to kQueryA and row 1 to kQueryB. Each
// test moves some vector to a query point and away from it in one transaction, so a search there
// must return the nearest of rows 1..20.
constexpr auto kQueryA = "[100, 100, 100]";
constexpr auto kQueryB = "[-100, 100, -100]";

// Covers https://github.com/yugabyte/yugabyte-db/issues/34312.
TEST_F(PgVectorIndexUtilTest, SameTxnReplacedVector) {
  constexpr int64_t kNumRows = 20;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  // A new row whose vector is replaced in the transaction that inserted it.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (1000, '$0')", kQueryA));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[-100, -100, -100]' WHERE id = 1000"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // A committed row updated twice in one transaction, the middle vector must not stay.
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2000, '[0, 0, -500]')"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2000", kQueryB));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, -100, 100]' WHERE id = 2000"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  for (const auto& [query, expected] :
           {std::pair(kQueryA, kNumRows), std::pair(kQueryB, int64_t{1})}) {
    auto id = ASSERT_RESULT(
        conn.FetchRow<int64_t>("SELECT id FROM test" + IndexQuerySuffix(query, 1)));
    EXPECT_EQ(id, expected) << "Query: " << query;
  }
}

// A search that finds the vector of a deleted row gets no row for it, which breaks its paging:
// https://github.com/yugabyte/yugabyte-db/issues/34737.
// Covers https://github.com/yugabyte/yugabyte-db/issues/34312.
TEST_F(PgVectorIndexUtilTest, SameTxnDeletedVector) {
  constexpr int64_t kNumRows = 20;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  // A new row deleted in the transaction that inserted it.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (1000, '$0')", kQueryA));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1000"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // A committed row whose vector is replaced, then deleted in one transaction.
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2000, '[0, 0, -500]')"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2000", kQueryB));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2000"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Without the fix, a LIMIT 1 search repeats the same page until the statement timeout.
  ASSERT_OK(conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(10s * kTimeMultiplier)));
  using Ids = std::vector<int64_t>;
  for (const auto& [query, expected] : {
           std::pair(kQueryA, Ids{20, 19, 18, 17, 16}), std::pair(kQueryB, Ids{1, 2, 3, 4, 5})}) {
    for (size_t limit : {expected.size(), 1UL}) {
      auto ids = ASSERT_RESULT(
          conn.FetchRows<int64_t>("SELECT id FROM test" + IndexQuerySuffix(query, limit)));
      EXPECT_EQ(ids, Ids(expected.begin(), expected.begin() + limit)) << "Query: " << query;
    }
  }
}

// A tablet that restarts with an uncommitted transaction in its intents DB scans the reverse index
// of that transaction backwards for its last strong intent. The delete-list record of a DELETE sits
// between the strong and the weak intents of its batch. An UPDATE also takes a row lock, a strong
// intent written after the delete list.
TEST_F(PgVectorIndexUtilTest, LoadTransactionWithDeleteList) {
  auto conn = ASSERT_RESULT(MakeIndexAndFill(20));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(cluster_->FlushTablets());

  StringWaiterLogSink decode_failure_log("Failed to decode intent while loading transaction");
  ASSERT_OK(RestartAndWaitTransactionsLoaded());
  ASSERT_FALSE(decode_failure_log.IsEventOccurred());
}

// A transaction that spans the promotion of vector_index_delete_list_after_rows has delete lists
// with both key encodings.
TEST_F(PgVectorIndexUtilTest, DeleteListsWithBothEncodings) {
  constexpr int64_t kNumRows = 20;
  constexpr auto kQueryC = "[0, 0, -500]";
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  // A committed row updated twice in one transaction: the old key tombstones the committed vector,
  // the new key tombstones the vector of the first update.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (2000, '$0')", kQueryC));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_delete_list_after_rows) = false;
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2000", kQueryB));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_delete_list_after_rows) = true;
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, -100, 100]' WHERE id = 2000"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  for (const auto& query : {kQueryB, kQueryC}) {
    auto id = ASSERT_RESULT(
        conn.FetchRow<int64_t>("SELECT id FROM test" + IndexQuerySuffix(query, 1)));
    EXPECT_EQ(id, 1) << "Query: " << query;
  }

  // A pending transaction with both encodings, loaded by a restarted tablet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_delete_list_after_rows) = false;
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_delete_list_after_rows) = true;
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(cluster_->FlushTablets());

  StringWaiterLogSink decode_failure_log("Failed to decode intent while loading transaction");
  ASSERT_OK(RestartAndWaitTransactionsLoaded());
  ASSERT_FALSE(decode_failure_log.IsEventOccurred());
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/30262.
TEST_F(PgVectorIndexUtilTest, ReverseMappingPostSplitCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      std::numeric_limits<int32>::max();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_post_split_compaction_input_size_threshold_bytes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 10;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  auto parent_tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto* parent_db = parent_tablet->regular_db();

  TestKVFormatter formatter;
  auto run_sst_dump = [this, &formatter](rocksdb::DB* db, bool clean_vectors = false) -> Status {
    formatter.Clear(clean_vectors);
    for (const auto& live_file : db->GetLiveFilesMetaData()) {
      RETURN_NOT_OK(RunSstDump(formatter, live_file.BaseFilePath()));
    }
    return Status::OK();
  };

  ASSERT_OK(run_sst_dump(parent_db, /*clean_vectors=*/true));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_8_vector_1 -> ybctid_8
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
      )#",
      formatter.FormatVectorsMeta());

  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(run_sst_dump(parent_db, /* clean_vectors = */ true));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_8_vector_1 -> ybctid_8
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
          ybctid_2_vector_2 -> ybctid_2
          ybctid_2_vector_1 -> DEL
      )#",
      formatter.FormatVectorsMeta());

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), parent_tablet_id));

  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  std::erase_if(peers, [&](const auto& peer) {
    return peer->tablet_id() == parent_tablet_id;
  });
  ASSERT_EQ(peers.size(), 2);
  std::ranges::sort(peers, [](const auto& peer1, const auto& peer2) {
    const auto& part1 = peer1->tablet_metadata()->partition()->partition_key_start();
    const auto& part2 = peer2->tablet_metadata()->partition()->partition_key_start();
    return part1 < part2;
  });

  auto child0_tablet = ASSERT_RESULT(peers[0]->shared_tablet());
  ASSERT_OK(run_sst_dump(child0_tablet->regular_db()));
  const auto child0_dump = formatter.FormatVectorsMeta();
  LOG(INFO) << "Tablet " << peers[0]->tablet_id() << " reverse mapping dump:\n" << child0_dump;

  auto child1_tablet = ASSERT_RESULT(peers[1]->shared_tablet());
  ASSERT_OK(run_sst_dump(child1_tablet->regular_db()));
  const auto child1_dump = formatter.FormatVectorsMeta();
  LOG(INFO) << "Tablet " << peers[1]->tablet_id() << " reverse mapping dump:\n" << child1_dump;

  const bool child0_has_row2 = child0_dump.find("ybctid_2") != std::string::npos;
  const bool child1_has_row2 = child1_dump.find("ybctid_2") != std::string::npos;
  ASSERT_NE(child0_has_row2, child1_has_row2);

  const auto& without_row2_dump = child0_has_row2 ? child1_dump : child0_dump;
  const auto& with_row2_dump = child0_has_row2 ? child0_dump : child1_dump;

  // The child that does not own row 2 must not retain any ybctid_2 reverse mapping records
  // after post-split compaction (neither the bare tombstone nor the stale mapping entry).
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_7_vector_1 -> ybctid_7
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
      )#",
      without_row2_dump);

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_6_vector_1 -> ybctid_6
          ybctid_8_vector_1 -> ybctid_8
          ybctid_2_vector_2 -> ybctid_2
          ybctid_2_vector_1 -> DEL
      )#",
      with_row2_dump);
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/35923.
TEST_F(PgVectorIndexUtilTest, PayloadSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  // Keep the chunks inherited from the parent, so the children hold the vectors of the sibling.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_include_into_post_split_compaction) = false;

  constexpr size_t kNumRows = 10;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), parent_tablet_id));

  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  std::erase_if(peers, [&](const auto& peer) {
    return peer->tablet_id() == parent_tablet_id;
  });
  ASSERT_EQ(peers.size(), 2);

  // [0, 0, 0] in the pgvector binary format: dimensions, an unused int16, coordinates.
  std::string query_vector("\x03\x00\x00\x00", 4);
  query_vector.append(3 * sizeof(float), '\0');

  size_t num_found = 0;
  std::vector<docdb::DocVectorIndexPtr> indexes;
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto tablet_indexes = tablet->vector_indexes().List();
    ASSERT_TRUE(tablet_indexes);
    ASSERT_EQ(tablet_indexes->size(), 1);
    const auto& index = tablet_indexes->front();
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows);

    auto reader = ASSERT_RESULT(index->context().CreateReverseMappingReader(
        ReadHybridTime::Max(), /* statistics= */ nullptr));
    auto result = ASSERT_RESULT(index->Search(
        query_vector, {.max_num_results = kNumRows, .ef = kNumRows},
        /* could_have_missing_entries= */ true, *reader));
    for (const auto& entry : result.entries) {
      ASSERT_TRUE(tablet->key_bounds().IsWithinBounds(entry.key.AsSlice()))
          << "Tablet " << peer->tablet_id() << " found sibling row "
          << entry.key.AsSlice().ToDebugHexString();
    }
    num_found += result.entries.size();
    indexes.push_back(index);
  }
  ASSERT_EQ(num_found, kNumRows);

  size_t num_entries = 0;
  for (const auto& index : indexes) {
    ASSERT_OK(index->Compact());
    ASSERT_OK(index->WaitForCompaction());
    num_entries += ASSERT_RESULT(index->TotalEntries());
  }
  ASSERT_EQ(num_entries, kNumRows);
}

TEST_F(PgVectorIndexUtilTest, SstDump) {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // The entires order is different from what sst_dump really prints, it's required to re-sort
  // to be able to compare the expected results.
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
      output);
}

// With vector_index_store_payload enabled the vector index writes no reverse mapping entries
// at all, neither on insert nor on delete.
TEST_F(PgVectorIndexUtilTest, SstDumpStoredYbctid) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));

  // Neither the deleted vector of row 2 nor the vector replaced by the update of row 4 add a
  // reverse mapping entry.
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  // Search resolves rows from the ybctids stored in the vector index and skips both the deleted
  // row and the vector left behind by the update, because it fetches the row they point to.
  const auto query = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kNumRows);
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
  std::ranges::sort(rows);
  ASSERT_EQ(AsString(rows), "[1, 3, 4, 5]");
}

// Every kind of inconsistency the check reports, injected through the test flags which skip or
// corrupt the vector index and reverse mapping writes.
TEST_F(PgVectorIndexUtilTest, IndexCheck) {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");

  // The vector of the row is not inserted into the index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = true;
  ASSERT_OK(InsertRows(conn, 6, 6));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = false;

  // The inserted row gets no reverse mapping entry and the deleted row keeps its entry.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_reverse_mapping_write) = true;
  ASSERT_OK(InsertRows(conn, 7, 7));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_reverse_mapping_write) = false;

  // The index stores a vector which differs from the row vector.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 8, 8));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = false;

  // The index stores two copies of the row vector.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 9, 9));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = false;

  ASSERT_EQ(
      ASSERT_RESULT(VectorIndexInconsistencies()),
      "DUPLICATE_VECTOR 9, MISSING_REVERSE_MAPPING 7, MISSING_VECTOR 6, "
      "SPURIOUS_REVERSE_MAPPING 1, VECTOR_MISMATCH 8");

  auto run_tool = [this](const std::vector<std::string>& args) -> Result<std::string> {
    auto command = ToStringVector(
        GetToolPath("yb-ts-cli"), "--server_address",
        AsString(cluster_->GetTServerBoundRpcAddr(0)), "verify_vector_indexes");
    command.insert(command.end(), args.begin(), args.end());
    std::string output;
    SCHECK(!Subprocess::Call(command, &output).ok(), IllegalState, "yb-ts-cli succeeded");
    LOG(INFO) << "yb-ts-cli output:\n" << output;
    return output;
  };

  // The tool prints every inconsistency and fails.
  auto output = ASSERT_RESULT(run_tool({}));
  for (const auto* kind : {"DUPLICATE_VECTOR", "MISSING_REVERSE_MAPPING", "MISSING_VECTOR",
                           "SPURIOUS_REVERSE_MAPPING", "VECTOR_MISMATCH"}) {
    ASSERT_STR_CONTAINS(output, Format("  $0: vector ", kind));
  }

  // An unknown index is reported as not checked.
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  output = ASSERT_RESULT(run_tool({peers.front()->tablet_id(), "unknown_index"}));
  ASSERT_STR_CONTAINS(output, "not checked: Not found");
}

// An index which stores payloads has no reverse mapping, so deleted and replaced vectors are
// invisible rather than inconsistent, and only the vectors of the rows are checked.
TEST_F(PgVectorIndexUtilTest, IndexCheckStoredPayload) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = true;
  ASSERT_OK(InsertRows(conn, 6, 6));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 7, 7));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 8, 8));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_payloads) = true;
  ASSERT_OK(InsertRows(conn, 9, 9));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_payloads) = false;

  ASSERT_EQ(
      ASSERT_RESULT(VectorIndexInconsistencies()),
      "DUPLICATE_VECTOR 8, MISSING_VECTOR 6, PAYLOAD_MISMATCH 9, VECTOR_MISMATCH 7");
}

// The reverse mapping decision is fixed for the table, so an index created after the gflag is
// turned off still stores payloads and the table keeps writing no reverse mapping entries.
TEST_F(PgVectorIndexUtilTest, StoredYbctidFixedForTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = false;
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// An index which stores payloads has no reverse mapping entries, so compaction cannot detect
// deleted vectors and keeps them in the index. Search skips them anyway, because the rows their
// stored ybctids point to are gone.
TEST_F(PgVectorIndexUtilTest, SearchSkipsDeletedVectorsWithStoredYbctid) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  // The range DELETE below is planned as a seq scan.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 10;
  constexpr size_t kNumDeletedRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto vector_indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(vector_indexes.size(), 1);
  auto& index = *vector_indexes.front();
  ASSERT_OK(index.Flush());
  ASSERT_OK(index.WaitForFlush());

  ASSERT_OK(conn.ExecuteFormat("DELETE FROM test WHERE id <= $0", kNumDeletedRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // TODO(vector_index): the deleted vectors are expected to be removed by compaction once
  // deletions are delivered to the vector index, see VectorMergeFilter::Filter.
  ASSERT_OK(index.Compact());
  ASSERT_OK(index.WaitForCompaction());
  ASSERT_EQ(ASSERT_RESULT(index.TotalEntries()), kNumRows);

  const auto query = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kNumRows);
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
  std::ranges::sort(rows);
  ASSERT_EQ(AsString(rows), "[6, 7, 8, 9, 10]");
}

// Master caps the tenant components of a new index below the hash group and range components of
// its table, so a tenant never covers the whole primary key, and the vectors keep their tenant in
// the payload. A search without a tenant reaches every tenant in memory, in flushed chunks and in
// the chunk compacted from them.
TEST_F(PgVectorIndexUtilTest, TenantComponents) {
  // Exact search scans all vectors, ignoring tenants.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 3;

  constexpr int kNumTenants = 3;
  constexpr int kRowsPerBatch = 6;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (tenant INT, id BIGINT, embedding vector(3), "
      "PRIMARY KEY ((tenant) HASH, id))"));
  ASSERT_OK(conn.Execute("CREATE TABLE single (id INT PRIMARY KEY, embedding vector(3))"));
  for (const auto* table : {"test", "single"}) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0_idx ON $0 USING ybhnsw (embedding vector_l2_ops)", table));
  }
  auto tenant_components = [this](const std::string& index_name) -> Result<uint32_t> {
    auto index = VERIFY_RESULT(client_->OpenTable(
        VERIFY_RESULT(GetTableIDFromTableName(index_name))));
    return index->index_info().vector_idx_options().tenant_hash_group_and_range_components();
  };
  // The tenant of PRIMARY KEY ((tenant) HASH, id) is its hash group.
  ASSERT_EQ(ASSERT_RESULT(tenant_components("test_idx")), 1U);
  // Every row of a single component key would be its own tenant.
  ASSERT_EQ(ASSERT_RESULT(tenant_components("single_idx")), 0U);

  // Consecutive ids belong to different tenants.
  std::vector<int64_t> ids;
  for (int batch = 0; batch != 2; ++batch) {
    const auto first_id = batch * kRowsPerBatch + 1;
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test SELECT i % $0, i, ARRAY[i, 0, 0]::vector FROM generate_series($1, $2) i",
        kNumTenants, first_id, first_id + kRowsPerBatch - 1));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    for (int i = 0; i != kRowsPerBatch; ++i) {
      ids.push_back(first_id + i);
    }
    ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, ids.size())), AsString(ids));
    ASSERT_OK(cluster_->FlushTablets());
    ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, ids.size())), AsString(ids));
  }
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_OK(index->Compact());
    ASSERT_OK(index->WaitForCompaction());
  }
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, ids.size())), AsString(ids));
}

// A table which writes no reverse mapping still marks its tablets as having vector deletions when
// a row is deleted or its vector is replaced. Until then a search does not fetch the rows of its
// candidates, after that it does, so the vector left behind by an update is not returned.
TEST_F(PgVectorIndexUtilTest, StoredPayloadTracksVectorDeletions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto query = [this, &conn] { return NearestIds(conn, kNumRows); };

  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));
  docdb::TEST_vector_index_filter_allowed = false;
  ASSERT_EQ(ASSERT_RESULT(query()), "[1, 2, 3, 4, 5]");
  docdb::TEST_vector_index_filter_allowed = true;

  // The old vector of row 1 stays in the index next to the new one.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, 200, 300]' WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 3, 4, 5, 1]");

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 4, 5, 1]");

  // The tablet keeps knowing about the deletions after a restart, whether it replays them from the
  // log or reads them from the flushed frontier.
  for (auto flush : {false, true}) {
    SCOPED_TRACE(Format("flush: $0", flush));
    if (flush) {
      ASSERT_OK(cluster_->FlushTablets());
    }
    DisableFlushOnShutdown(*cluster_, !flush);
    ASSERT_OK(RestartCluster());
    conn = ASSERT_RESULT(Connect());
    ASSERT_EQ(ASSERT_RESULT(query()), "[2, 4, 5, 1]");
    ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  }
}

// A table without vector indexes takes the single-shard fast path for an autocommit single-row
// write, which is applied without intents.
void PgVectorIndexUtilTest::TestFastPathVectorDeletion(const std::string& statement) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 5));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));

  // So that a write taking the distributed path does not mark the tablet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(conn.Execute(statement));
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
}

TEST_F(PgVectorIndexUtilTest, StoredPayloadFastPathUpdate) {
  TestFastPathVectorDeletion("UPDATE test SET embedding = '[100, 200, 300]' WHERE id = 1");
}

TEST_F(PgVectorIndexUtilTest, StoredPayloadFastPathDelete) {
  TestFastPathVectorDeletion("DELETE FROM test WHERE id = 3");
}

// A restore to a time uses a later snapshot, whose vector index keeps the vectors replaced after
// that time, while the regular DB is rolled back. The tablet keeps checking rows after a restart.
TEST_F(PgVectorIndexUtilTest, StoredPayloadRestoreToTime) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto restore_at = cluster_->mini_master(0)->Now();
  // Closer to the query vector than any row, so the vector left in the index comes first.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[0.5, 1, 1.5]' WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, cluster_->mini_master(0)->Now()));
  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  // The query waits for the tablet leader, which HasVectorDeletion requires.
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, kNumRows)), "[1, 2, 3, 4, 5]");
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
}

// A committed transaction is visible through its intents until it is applied, while the index
// still holds the old vectors of the rows it updated. A later transaction applied first leaves the
// intents of the earlier one with a stale vector.
void PgVectorIndexUtilTest::TestUnappliedVectorUpdates() {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto query = [this, &conn] { return NearestIds(conn, kNumRows); };

  // Ignored applies are resent after transaction_resend_applying_interval_usec.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(UpdateInTransaction(conn, 1, "[100, 200, 300]"));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 3, 4, 5, 1]");

  ASSERT_OK(UpdateInTransaction(conn, 2, "[200, 400, 600]"));
  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
  ASSERT_OK(UpdateInTransaction(conn, 2, "[4.5, 9, 13.5]"));
  // Set by the apply of the last update.
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> { return HasVectorDeletion(); }, 10s * kTimeMultiplier,
      "Last update applied"));
  ASSERT_EQ(ASSERT_RESULT(query()), "[3, 4, 2, 5, 1]");

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[3, 4, 2, 5, 1]");
}

TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdates) {
  TestUnappliedVectorUpdates();
}

TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdatesStoredPayload) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  TestUnappliedVectorUpdates();
}

// The search does not filter vectors, so the index entries of the rows with intents are left in
// its results.
TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdateSkipFilterCheck) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(UpdateInTransaction(conn, 1, "[100, 200, 300]"));
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, kNumRows + 1)), "[2, 3, 4, 5, 1]");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
}

// Verifies table-owned V1 reverse-mapping values dump as SubDocKey(..., [ColumnId(...)]).
TEST_F(PgVectorIndexUtilTest, ReverseMappingDumpFormatV1) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 1));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  const auto lines = ASSERT_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), "test"));
  size_t v1_meta_values = 0;
  for (const auto& line : lines) {
    if (line.find("MetaKey(VectorId(") == std::string::npos) {
      continue;
    }
    // Legacy: MetaKey(...) -> DocKey(...)
    // V1:     MetaKey(...) -> SubDocKey(DocKey(...), [ColumnId(...)])
    ASSERT_NE(line.find("SubDocKey("), std::string::npos) << line;
    ASSERT_NE(line.find("ColumnId("), std::string::npos) << line;
    ++v1_meta_values;
  }
  ASSERT_EQ(v1_meta_values, 1);
}

// The merge keeps a vector only when it finds its reverse mapping, so whether a merge can drop
// the vectors of a write depends on how the reverse mappings are stored. The engine, the packing
// mode and colocation do not change when the reverse mappings of a write become visible.
class PgVectorIndexMergeTest : public PgVectorIndexUtilTest,
                               public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    PgVectorIndexUtilTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = GetParam();
  }
};

INSTANTIATE_TEST_SUITE_P(
    , PgVectorIndexMergeTest, testing::Bool(),
    [](const testing::TestParamInfo<bool>& param_info) -> std::string {
      return param_info.param ? "TableOwned" : "LegacyOwned";
    });

// Applying a write hands the row's vectors to the VectorLSM before RocksDB publishes the batch
// that carries the row and its reverse mapping. The merge filter reads reverse mappings from the
// regular DB at a snapshot taken when the merge starts, so a merge that runs while such a batch is
// still open cannot see the mappings of the vectors it merges and discards them for good, although
// the rows do land in the table. A flush from another thread, e.g. the forced flush of an index
// lagging the log tail, can save the mutable chunk while the batch is open. Here the write is
// parked at the sync point hit right after its vector is handed to the insert thread pool, the
// mutable chunk is flushed and all saved chunks are merged, and only then the write is released.
// Expected: the row is found both by primary key and by vector search (GH #34246).
TEST_P(PgVectorIndexMergeTest, MergeDuringUnpublishedWrite) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Merge on the compaction thread itself, so the merge does not hit the insert registry sync
  // point used to park the write.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_merge_tasks) = 0;

  constexpr size_t kNumRows = 8;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());
  // The fill is applied in the background, so its inserts could otherwise hit the sync points
  // below instead of the parked write.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(indexes.size(), 1);
  auto index = indexes.front();

  CountDownLatch parked(1);
  CountDownLatch resume(1);
  std::atomic<bool> armed{true};
  // The insert task is held as well, so it completes only after the flush below: the completion
  // of the last task of an immutable chunk saves the chunk, unless the chunk is held by the write.
  CountDownLatch resume_task(1);
  std::atomic<bool> task_armed{true};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSMInsertRegistryBase::ExecuteTasks:Enqueued", [&](void*) {
    if (!armed.exchange(false)) {
      return;
    }
    parked.CountDown();
    resume.Wait();
  });
  sync_point->SetCallBack("VectorLSMInsertTask::Run:Begin", [&](void*) {
    if (task_armed.exchange(false)) {
      resume_task.Wait();
    }
  });
  sync_point->EnableProcessing();

  const int64_t kRowId = kNumRows + 1;
  TestThreadHolder threads;
  // Declared after threads, so a failed assertion releases the parked threads before they are
  // joined.
  auto release_parked = ScopeExit([&resume, &resume_task, sync_point] {
    resume_task.CountDown();
    resume.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  threads.AddThreadFunctor([this, kRowId] {
    auto insert_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(insert_conn.ExecuteFormat(
        "INSERT INTO test VALUES ($0, '$1')", kRowId, AsString(Vector(kRowId))));
  });
  ASSERT_TRUE(parked.WaitFor(60s * kTimeMultiplier));

  // The write is open: the vector is in the mutable chunk, the row and its reverse mapping are in
  // an unpublished RocksDB batch. WaitForFlush would wait for the write, so wait for the insert
  // task instead.
  ASSERT_OK(index->Flush());
  resume_task.CountDown();
  ASSERT_OK(WaitFor(
      [&index] { return !index->TEST_HasBackgroundInserts(); }, 60s * kTimeMultiplier,
      "Wait for vector inserts"));
  ASSERT_OK(index->Compact());
  ASSERT_OK(index->WaitForCompaction());

  resume.CountDown();
  threads.WaitAndStop(60s * kTimeMultiplier);
  ASSERT_OK(index->WaitForFlush());

  auto id = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT id FROM test WHERE id = $0", kRowId)));
  ASSERT_EQ(id, kRowId);
  VerifyRows(conn, "", ExpectedRows(kNumRows + 1));
}

TEST_F(PgVectorIndexUtilTest, DeleteTabletDirs) {
  constexpr size_t kNumRows = 10;
  num_pre_split_tablets_ = 2; // To have test both types of delete_state.
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows, AddFilter::kFalse));

  auto tablet_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(tablet_peers.size(), 2);

  for (auto delete_state : { tablet::TABLET_DATA_DELETED, tablet::TABLET_DATA_TOMBSTONED }) {
    ASSERT_FALSE(tablet_peers.empty());
    auto tablet = ASSERT_RESULT(tablet_peers.back()->shared_tablet());
    auto vector_indexes = tablet->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);

    const auto vector_index_dir = vector_indexes->front()->path();
    const auto table_dir = DirName(vector_index_dir);
    const auto tablet_id = tablet->tablet_id();

    // Sanity check to make sure table dir contains vector index.
    ASSERT_TRUE(vector_index_dir.starts_with(table_dir));

    auto content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs before tablet $0 deletion: $1", tablet_id, AsString(content));

    ASSERT_OK(cluster_->mini_tablet_server(0)->DeleteTablet(tablet_id, delete_state));

    // Make sure all tablet directories have been deleted (including vector index dir).
    content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs after tablet $0 deletion: $1", tablet_id, AsString(content));
    const auto num_tablet_dirs = std::ranges::count_if(
          content, [&tablet_id](const std::string& subdir) { return subdir.contains(tablet_id); });
    ASSERT_EQ(num_tablet_dirs, 0);

    tablet_peers.pop_back();
  }
}

// DROP DATABASE deletes the tablets of indexed tables before it gets to their indexes. A vector
// index lives on its indexed table's tablets, so the background cleanup may free those tablets in
// between. Processing the index afterwards must not fail the drop (#34249).
TEST_F(PgVectorIndexUtilTest, DropDatabaseAfterIndexedTableCleanedUp) {
  // Finished tasks kept for the UI hold their tablets, which keeps tablets of dropped tables
  // alive. In a busy cluster they are evicted quickly, so do not keep them at all. The tracker
  // is sized when the master starts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tasks_tracker_num_tasks) = 0;
  ASSERT_OK(RestartMaster());

  constexpr auto kDbName = "vector_db";
  constexpr size_t kNumTables = 2;

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDbName));
  {
    auto db_conn = ASSERT_RESULT(ConnectToDB(kDbName));
    ASSERT_OK(db_conn.Execute("CREATE EXTENSION vector"));
    for (size_t i = 0; i != kNumTables; ++i) {
      ASSERT_OK(db_conn.ExecuteFormat(
          "CREATE TABLE t$0 (id bigserial PRIMARY KEY, embedding vector(3)) SPLIT INTO 1 TABLETS",
          i));
      ASSERT_OK(db_conn.ExecuteFormat(
          "CREATE INDEX ON t$0 USING ybhnsw (embedding $1)", i,
          pgwrapper::VectorOpsName(distance_kind_)));
    }
  }

  std::vector<TableId> table_ids;
  for (size_t i = 0; i != kNumTables; ++i) {
    table_ids.push_back(ASSERT_RESULT(GetTableIDFromTableName(Format("t$0", i))));
  }

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  std::atomic<size_t> num_started_tables{0};
  std::atomic<bool> table_cleaned_up{false};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "CatalogManager::DeleteOrHideTabletsAndSendRequests::Start", [&](void*) {
        // Tables go first, so here the first table's tablets are deleted and the second's are not.
        // Hold the drop until the background cleanup removes the first table and its tablets.
        if (num_started_tables.fetch_add(1) != 1) {
          return;
        }
        table_cleaned_up = WaitFor(
            [&]() -> Result<bool> {
              return std::ranges::any_of(table_ids, [&catalog_manager](const auto& table_id) {
                return catalog_manager.GetTableInfo(table_id) == nullptr;
              });
            },
            30s * kTimeMultiplier, "Indexed table removed by the background cleanup").ok();
      });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDbName));
  ASSERT_TRUE(table_cleaned_up);
}

TEST_F(PgVectorIndexUtilTest, AutomaticTabletSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = true;

  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(WaitForTabletSplit(table_id));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

class PgVectorIndexSplitTest : public PgVectorIndexUtilTest {
 protected:
  void SetUp() override {
    // Delete split parents quickly.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    PgVectorIndexUtilTest::SetUp();
  }
};

// GH#34286: CREATE INDEX hung forever when a tablet that had already finished its backfill was
// split and deleted while another tablet was still backfilling.
TEST_F(PgVectorIndexSplitTest, SplitBackfilledTabletDuringCreateIndex) {
  constexpr size_t kChunkSize = 100;
  // With db_block_size_bytes = 256 this is enough for the small tablet to have a split key. It is
  // below kChunkSize, so the small tablet backfills all its rows in one batch and is not blocked.
  constexpr size_t kSmallTabletRows = 40;
  // More than one batch, so the large tablet blocks after its first batch.
  constexpr size_t kNumRows = kSmallTabletRows + 2 * kChunkSize;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kChunkSize;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigint, embedding vector(3), PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES (($0))", kSmallTabletRows));
  dimensions_ = 3;
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 2);
  auto small_tablet_it = std::ranges::find_if(peers, [](const auto& peer) {
    return peer->tablet_metadata()->partition()->partition_key_start().empty();
  });
  ASSERT_NE(small_tablet_it, peers.end());
  const auto small_tablet_id = (*small_tablet_it)->tablet_id();

  // Keep the large tablet backfilling until the split parent is deleted.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  auto unblock_backfill = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
        false;
  });

  // Split only after the readiness poll has looked up the tablets, so its cache has the parent.
  std::atomic<bool> poll_looked_up_tablets{false};
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "PgClientService::PollVectorIndexReady:TabletsLookedUp",
      [&poll_looked_up_tablets](void*) { poll_looked_up_tablets = true; });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    // Fail instead of hanging the test if CREATE INDEX never returns.
    ASSERT_OK(index_conn.ExecuteFormat(
        "SET statement_timeout = $0", ToMilliseconds(120s * kTimeMultiplier)));
    ASSERT_OK(CreateIndex(index_conn));
  });

  ASSERT_OK(WaitFor(
      [&poll_looked_up_tablets] { return poll_looked_up_tablets.load(); },
      30s * kTimeMultiplier, "Readiness poll looked up tablets"));

  auto small_tablet_backfill_done = [this, &small_tablet_id]() -> Result<bool> {
    auto small_tablet_peers = VERIFY_RESULT(ListTabletActivePeers(cluster_.get(), small_tablet_id));
    if (small_tablet_peers.empty()) {
      return false;
    }
    for (const auto& peer : small_tablet_peers) {
      auto indexes = VERIFY_RESULT(peer->shared_tablet())->vector_indexes().List();
      if (!indexes || indexes->empty()) {
        return false;
      }
      for (const auto& index : *indexes) {
        if (!VERIFY_RESULT(index->BackfillDone())) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor(
      small_tablet_backfill_done, 30s * kTimeMultiplier, "Small tablet backfill done"));

  ASSERT_OK(InvokeSplitTabletRpc(cluster_.get(), small_tablet_id));
  ASSERT_OK(WaitFor([this, &small_tablet_id]() -> Result<bool> {
    auto all_peers = VERIFY_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
    return std::ranges::none_of(all_peers, [&small_tablet_id](const auto& peer) {
      return peer->tablet_id() == small_tablet_id;
    });
  }, 60s * kTimeMultiplier, "Split parent deleted"));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
      false;
  threads.JoinAll();

  auto query_vector = Vector(RandomUniformInt<size_t>(0, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_EQ(num_found, kNumRows);
}

}  // namespace yb::pgwrapper
