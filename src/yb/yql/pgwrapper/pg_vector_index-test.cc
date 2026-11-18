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

#include "yb/consensus/consensus.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"

#include "yb/dockv/partition.h"

#include "yb/fs/fs_manager.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/qlexpr/index.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"
#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_PgClientService_Perform);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_enable_pk_routing);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(ysql_enable_cdr_metadata);
DECLARE_bool(TEST_vector_index_exact);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(ysql_client_read_write_timeout_ms);
DECLARE_string(vector_index_backend);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint64(vector_index_max_insert_tasks);
DECLARE_uint64(vector_index_max_merge_tasks);
DECLARE_uint64(vector_index_task_size);

namespace yb::docdb {

extern bool TEST_vector_index_filter_allowed;
extern size_t TEST_vector_index_max_checked_entries;

} // namespace yb::docdb

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::vector_index {

extern MonoDelta TEST_sleep_during_flush;

} // namespace yb::vector_index

namespace yb::pgwrapper {

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexTest);

void PgVectorIndexTest::TestSimple(bool table_exists) {
  docdb::TEST_vector_index_filter_allowed = false;

  auto conn = ASSERT_RESULT(MakeIndex(3, table_exists));

  size_t num_found_peers = 0;
  auto check_tablets = [this, &num_found_peers]() -> Result<bool> {
    num_found_peers = 0;
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->table_type() != TableType::PGSQL_TABLE_TYPE) {
        continue;
      }
      auto& metadata = *tablet->metadata();
      auto tables = metadata.GetAllColocatedTables();
      tablet::TableInfoPtr main_table_info;
      tablet::TableInfoPtr index_table_info;
      size_t num_indexes = 0;
      for (const auto& table_id : tables) {
        auto table_info = VERIFY_RESULT(metadata.GetTableInfo(table_id));
        LOG(INFO) << "Table: " << table_info->ToString();
        if (table_info->table_name == "test") {
          main_table_info = table_info;
        } else if (table_info->index_info) {
          ++num_indexes;
          index_table_info = table_info;
        }
      }
      if (!main_table_info) {
        continue;
      }
      ++num_found_peers;
      if (num_indexes != 1) {
        LOG(INFO) << "Wrong number of indexes " << num_indexes << " at " << tablet->LogPrefix();
        return false;
      }
      auto vector_indexes = tablet->vector_indexes().List();
      auto num_vector_indexes = vector_indexes ? vector_indexes->size() : 0;
      if (num_vector_indexes != 1) {
        LOG(INFO)
            << "Wrong number of vector indexes " << num_vector_indexes << " at "
            << tablet->LogPrefix();
        return false;
      }
      SCHECK_EQ(
        index_table_info->index_info->indexed_table_id(), main_table_info->table_id,
        IllegalState, "Wrong indexed table");
    }
    return true;
  };

  ASSERT_OK(WaitFor(check_tablets, 10s * kTimeMultiplier, "Index created on all tablets"));
  ASSERT_NE(num_found_peers, 0);

  if (!table_exists) {
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));
  }

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test" + IndexQuerySuffix("[1.0, 0.4, 0.3]", 5)));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");

  DumpMemoryUsage();
}

TEST_P(PgVectorIndexTest, Simple) {
  TestSimple();
}

// A row written while a replication origin is set carries a CDR stamp on its packed value, so the
// value no longer begins with the packed row marker. The vector index updater finds the packed row
// by reading that first byte; if it does not decode past the control fields the row never reaches
// the index and an ANN query silently misses it, with nothing to indicate anything was dropped.
//
// Only the packed parameterizations exercise this. Without packing the row is written as loose
// column entries and the updater takes the column-keyed branch, which never reads a row-level
// value; those runs pass either way.
TEST_P(PgVectorIndexTest, StampedRowReachesTheIndex) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  docdb::TEST_vector_index_filter_allowed = false;

  auto conn = ASSERT_RESULT(MakeIndex(3));
  ASSERT_OK(conn.Fetch("SELECT pg_replication_origin_create('cdr_vector_test')"));
  ASSERT_OK(conn.Fetch("SELECT pg_replication_origin_session_setup('cdr_vector_test')"));

  // The source commit time as well as the origin. A write is stamped only when both are known, so
  // attaching the session to an origin and stopping there would leave these rows unstamped and
  // this test asserting nothing.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Fetch(
      "SELECT pg_replication_origin_xact_setup('0/1', '2026-01-01 00:00:00+00'::timestamptz)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test" + IndexQuerySuffix("[1.0, 0.4, 0.3]", 5)));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");
}

TEST_P(PgVectorIndexTest, NotApplied) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  TestSimple();
}

TEST_P(PgVectorIndexTest, Drop) {
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  TestSimple(true);
}

TEST_P(PgVectorIndexTest, DropWithFlush) {
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_during_flush) = 250ms * kTimeMultiplier;
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ThreadHolder threads;
  threads.AddThreadFunctor([this] {
    ASSERT_OK(cluster_->FlushTablets(
        tablet::FlushMode::kAsync, tablet::FlushFlags::kVectorIndexes));
  });
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
}

// Regression test for GH#30640: DROP INDEX on a colocated, partitioned table
// fails with "current transaction is expired or aborted".
TEST_P(PgVectorIndexTest, DropPartitioned) {
  tablet::TEST_fail_on_seq_scan_with_vector_indexes = false;

  auto colocated = IsColocated();
  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  if (colocated) {
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (id int, v1 varchar(100), vec_col vector(2), "
      "PRIMARY KEY (id, v1)) PARTITION BY LIST(v1)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_a PARTITION OF test FOR VALUES IN ('cat a')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_b PARTITION OF test FOR VALUES IN ('cat b')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_default PARTITION OF test DEFAULT"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO test (id, v1, vec_col) "
      "SELECT i, "
      "  CASE WHEN i % 3 = 0 THEN 'cat a' "
      "       WHEN i % 3 = 1 THEN 'cat b' "
      "       ELSE 'cat c' END, "
      "  ('[' || (i % 10)::text || ',' || ((i + 1) % 10)::text || ']')::vector "
      "FROM generate_series(1, 100) AS s(i)"));

  ASSERT_OK(conn.Execute(
      "CREATE INDEX vi_part ON test USING ybhnsw (vec_col vector_l2_ops)"));

  ASSERT_OK(conn.Execute("DROP INDEX vi_part"));
}

void PgVectorIndexTest::TestManyRows(AddFilter add_filter, Backfill backfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows, backfill));
  ASSERT_NO_FATALS(VerifyRows(conn, add_filter, ExpectedRows(add_filter ? 2 : 5), /* limit= */ 5));
  if (add_filter) {
    ASSERT_NO_FATALS(VerifyRead(conn, /* limit= */ 1, add_filter));
  }
}

TEST_P(PgVectorIndexTest, Split) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Give some time for split to happen.
  std::this_thread::sleep_for(2s * kTimeMultiplier);

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, ManyRows) {
  TestManyRows(AddFilter::kFalse);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfill) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_insert_tasks) = 1;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_GT(indexes.size(), 0);

  // Each replica backfills its own data, but CREATE INDEX only waits for the leader. So wait for
  // every replica's backfill to finish before checking the progress metric, otherwise followers
  // could still be mid-backfill.
  for (const auto& index : indexes) {
    ASSERT_OK(WaitFor(
        [&index] { return index->BackfillDone(); }, 60s * kTimeMultiplier, "Backfill done"));
  }

  // backfill_inserted_entries is a table-level metric, so all tablets of the index living on the
  // same tserver share one counter. Sum the distinct counters and the per-tablet entry counts:
  // without restarts every entry is backfilled exactly once, so the two totals must match.
  std::unordered_set<Counter*> counted;
  size_t total_backfilled = 0;
  size_t total_entries = 0;
  for (const auto& index : indexes) {
    total_entries += ASSERT_RESULT(index->TotalEntries());
    const auto& counter = index->metrics().backfill_inserted_entries;
    if (counted.insert(counter.get()).second) {
      total_backfilled += counter->value();
    }
  }
  LOG(INFO) << "Backfilled entries: " << total_backfilled << ", total entries: " << total_entries;
  ASSERT_GT(total_backfilled, 0);
  ASSERT_EQ(total_backfilled, total_entries);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfillAndRestart) {
  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);
  size_t tablet_entries = 0;
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    auto current_entries = ASSERT_RESULT(index->TotalEntries());
    LOG(INFO) << index->ToString() << ": Total entries: " << current_entries;
    if (tablet_entries) {
      ASSERT_EQ(tablet_entries, current_entries);
    } else {
      tablet_entries = current_entries;
    }
  }
}

TEST_P(PgVectorIndexTest, ManyRowsWithFilter) {
  TestManyRows(AddFilter::kTrue);
}

TEST_P(PgVectorIndexTest, ManyReads) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_reads) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;

  constexpr size_t kNumRows = 64;
  constexpr size_t kNumReads = 16;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  TestThreadHolder threads;
  for (size_t i = 1; i <= kNumReads; ++i) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load()) {
        auto id = RandomUniformInt<size_t>(1, kNumRows);
        auto vector = Vector(id);
        auto rows = ASSERT_RESULT(conn.FetchAllAsString(
            "SELECT * FROM test" + IndexQuerySuffix(vector, 1)));
        ASSERT_EQ(rows, ExpectedRow(id));
      }
    });
  }

  threads.WaitAndStop(5s);
}

// Drives concurrent index writes and searches against a single mutable chunk to reproduce (and,
// with the fix, guard against) the data race where the lock-free search traversal reads node state
// a concurrent insert is still publishing. The race was in the usearch backend, which is no longer
// tested; for hnswlib this doubles as a concurrency stress test.
TEST_P(PgVectorIndexTest, ConcurrentInsertAndSearch) {
  // Under sanitizers, a single writer and reader is enough to expose (or, with the fix, clear) the
  // search-vs-insert race, and the run is kept short because sanitizers are much slower. The vector
  // index insert/search concurrency is left at its default, which is already capped under
  // sanitizers (see SanitizerCappedConcurrency); forcing it higher made the sustained, CPU-heavy
  // insert/search traffic starve the cluster's raft heartbeats into cascading RPC timeouts and grow
  // the usearch search-context memory until the Postgres backends were OOM-killed -- neither being
  // the race this test guards against.
  const size_t kWriters = RegularBuildVsSanitizers<size_t>(4, 1);
  const size_t kReaders = RegularBuildVsSanitizers<size_t>(8, 1);
  const auto kRunTime = RegularBuildVsSanitizers<std::chrono::seconds>(180s, 30s);
  constexpr int64_t kBatchSize = 50;

  // The base fixture forces exact (brute force) search, which serializes inserts behind a mutex in
  // IndexWrapperBase::Insert and never traverses the HNSW graph -- both hide the race. Run against
  // the real index instead, with one insert task per vector so a single mutable chunk receives many
  // concurrent writes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_task_size) = 1;
  // Keep everything in a single tablet (hence a single mutable chunk) so writers and readers hit
  // the same index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 64));
  ASSERT_OK(CreateIndex(conn));

  // Seed a few rows so concurrent readers always have a non-empty index to traverse.
  ASSERT_OK(InsertBatch(conn, 1, 100));

  std::atomic<int64_t> next_id{1000};
  TestThreadHolder threads;

  // Cap the number of inserted rows. For the pure hnswlib backends a chunk's in-memory graph is
  // never released after it is flushed to disk (DoSaveToFile keeps the original index) and each
  // chunk additionally reserves its estimated size in the block cache, so tracked memory grows with
  // every inserted vector, reaching the 6.8GB soft limit somewhere near 1M rows. Past that, writes
  // are rejected and retried for the whole 10 minute YSQL client timeout, so an insert outlives
  // kRunTime and wedges the thread holder's JoinAll until the test times out.
  //
  // Reaching the cap makes the writers exit, which sets the holder's stop flag. In a regular build
  // hnswlib does not reach it within kRunTime, so kRunTime bounds the insert-vs-search window and
  // the cap only bounds memory: at 300K the hnswlib peak stays around 1.9GB, well clear of the soft
  // limit.
  constexpr int64_t kMaxId = 300000;

  // Writers: keep inserting multi-row batches until the row cap is reached. With task_size=1 every
  // batch fans out into many concurrent add() calls on the chunk's index. Ids come from a shared
  // counter so they stay unique across writers; once a chunk fills it rolls into a new mutable one.
  for (size_t w = 0; w != kWriters; ++w) {
    threads.AddThreadFunctor([this, &next_id, &stop_flag = threads.stop_flag()] {
      auto write_conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire) &&
             next_id.load(std::memory_order_relaxed) < kMaxId) {
        ASSERT_OK(InsertBatch(
            write_conn, next_id.fetch_add(kBatchSize, std::memory_order_relaxed), kBatchSize));
      }
    });
  }

  // Readers: continuously search the (mutable) chunk that writers are filling. Returned rows are
  // not validated; the test checks that concurrent search neither crashes nor fails.
  for (size_t r = 0; r != kReaders; ++r) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto read_conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire)) {
        auto query = Vector(RandomUniformInt<int64_t>(1, 1000));
        ASSERT_RESULT(read_conn.FetchAllAsString(
            "SELECT id FROM test" + IndexQuerySuffix(query, 10)));
      }
    });
  }

  threads.WaitAndStop(kRunTime);
}

// The test parks a compaction task in that window, drops the table to tear the tablet down (freeing
// the VectorLSM), then resumes the task so it dereferences the freed object in CompactionTask::Run
// on the priority thread pool -- matching the issue's stack. A raw heap use-after-free only faults
// reliably under a sanitizer, so the access is made observable in any build by a check inside
// VectorLSM::LastSerialNo() (which the resumed task calls): the destructor clears
// options_.thread_pool, turning the use-after-free into a deterministic CHECK failure. Under ASAN
// the same access additionally reports a heap-use-after-free.
//
// Covers https://github.com/yugabyte/yugabyte-db/issues/30554.
TEST_P(PgVectorIndexTest, CompactionDuringShutdown) {
  // Disable background compactions so the only task reaching the sync point is the manual
  // compaction scheduled below.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  CountDownLatch task_in_window{1};
  CountDownLatch lsm_freed{1};
  std::atomic<bool> fired{false};

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::CompactionTask::Run:AfterCompleted",
      [&fired, &task_in_window, &lsm_freed](void*) {
        if (fired.exchange(true)) {
          return;  // Only orchestrate the first compaction task.
        }
        // The task has deregistered itself (so shutdown can complete and free the VectorLSM) but is
        // about to dereference it again. Let the tablet be torn down, then resume into the
        // use-after-free.
        task_in_window.CountDown();
        ASSERT_TRUE(lsm_freed.WaitFor(60s * kTimeMultiplier));
      });
  sync_point->EnableProcessing();

  // Schedule an asynchronous compaction and keep only a weak reference to the index, so the tablet
  // owns the only strong references and tearing the tablet down frees the VectorLSM.
  std::weak_ptr<docdb::DocVectorIndex> weak_index;
  {
    auto indexes = ListVectorIndexes(cluster_.get());
    ASSERT_FALSE(indexes.empty()) << "No tablet with a vector index found";
    weak_index = indexes.front();
    ASSERT_OK(indexes.front()->Compact());
  }
  ASSERT_FALSE(weak_index.expired());

  ASSERT_TRUE(task_in_window.WaitFor(60s * kTimeMultiplier))
      << "Compaction task did not reach the post-completion window";

  // Drop the table on another thread to tear down the tablet (and its VectorLSM) while the
  // compaction task is parked mid-run. The drop runs off the main thread so we can drive the task
  // even when tablet teardown blocks waiting for the task (which is what a correct fix does).
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.Execute("DROP TABLE test"));
  });

  // Tearing the tablet down must free the VectorLSM out from under the parked compaction task --
  // that is the bug. Require it: otherwise the use-after-free window was not hit and the resumed
  // task below would run against a live object and pass silently.
  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 30s * kTimeMultiplier,
                    "vector index destruction"));

  // Resume the parked task; on the buggy code it now dereferences the freed VectorLSM.
  lsm_freed.CountDown();

  // Give the task a moment to surface the use-after-free before the test tears down the cluster.
  SleepFor(2s * kTimeMultiplier);
  threads.WaitAndStop(60s * kTimeMultiplier);
}

// RocksDB flushes/compactions and vector index compactions all run on a single, process-global
// priority thread pool (TSTabletManager::VectorIndexCompactionToken routes vector index compactions
// to docdb::GetGlobalPriorityThreadPool, the same pool used for RocksDB flushes when
// use_priority_thread_pool_for_flushes is on -- which it is for mini-cluster tests). A vector index
// compaction that occupies a pool worker for its entire (potentially minute-long) duration without
// yielding to higher priority work starves memtable flushes, including flush-on-shutdown, which
// then hangs tablet teardown. Pin the pool to a single worker so one compaction occupies the whole
// pool, making the starvation deterministic.
class PgVectorIndexCompactionPoolTest : public PgVectorIndexTest {
 protected:
  // Keep rocksdb auto compactions from competing for the single pool worker.
  int GetFileNumCompactionTrigger() override {
    return 10000;
  }

  void SetUp() override {
    // The global priority thread pool size is read once, when the pool is first created during
    // cluster startup, so it must be set before the base SetUp brings the cluster up.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_priority_thread_pool_size) = 1;
    PgVectorIndexTest::SetUp();
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexCompactionPoolTest);

// Shutting a tserver down must not hang when another tserver is running a vector index compaction.
// Reproduces the teardown deadlock from https://github.com/yugabyte/yugabyte-db/issues/31321: the
// shutting-down tserver flushes its memtables on shutdown, and that flush is a task on the shared,
// single-worker priority pool -- the same pool a vector index compaction on a *different* tserver
// is occupying. If the compaction never yields the worker, the flush-on-shutdown is starved and the
// shutdown hangs. A correct fix makes the compaction honor the suspender so the higher priority
// flush preempts it.
TEST_P(PgVectorIndexCompactionPoolTest, ShutdownNotBlockedByCompaction) {
  // Single-threaded merge so the compaction does its work directly on the pool worker it occupies
  // (rather than offloading the inserts to the merge thread pool) -- that worker is the one the
  // flush-on-shutdown also needs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_merge_tasks) = 0;
  // We trigger the one compaction manually; no background compactions competing for the worker.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Run against the real index so a compaction actually merges HNSW graphs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  constexpr int64_t kBatchSize = 50;
  constexpr int64_t kBatches = 5;

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 64));
  ASSERT_OK(CreateIndex(conn));

  // Build several immutable chunks (one per flushed batch) so a manual full compaction has a real
  // merge loop to run.
  for (int64_t b = 0; b != kBatches; ++b) {
    ASSERT_OK(InsertBatch(conn, 1 + b * kBatchSize, kBatchSize));
    ASSERT_OK(cluster_->FlushTablets());
  }
  // Leave an unflushed memtable on the tablet leader so it actually has to flush on shutdown (the
  // flush that gets starved). The leader -- where these writes land -- is the tserver we shut down.
  ASSERT_OK(InsertBatch(conn, 1 + kBatches * kBatchSize, kBatchSize));

  // Pick the vector tablet leader (it holds the unflushed writes above, so its shutdown must flush)
  // and a follower (we run the pool-hogging compaction there so it keeps holding the worker while
  // the leader shuts down).
  auto leader_peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(leader_peers.empty()) << "No vector tablet leader found";
  auto leader_uuid = leader_peers.front()->permanent_uuid();

  auto follower_indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
  ASSERT_FALSE(follower_indexes.empty()) << "No vector tablet follower found";
  auto follower_index = follower_indexes.front();

  size_t shutdown_idx = cluster_->num_tablet_servers();
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    if (cluster_->mini_tablet_server(i)->server()->permanent_uuid() == leader_uuid) {
      shutdown_idx = i;
      break;
    }
  }
  ASSERT_LT(shutdown_idx, cluster_->num_tablet_servers());

  // Slow every merge step so the compaction stays in its merge loop -- occupying the single pool
  // worker -- for the whole window below. SleepFor (not a hard block) so the loop keeps reaching
  // the suspend point a correct fix relies on. Announce on the first step so we know it is held.
  CountDownLatch compaction_running{1};
  std::atomic<bool> slow_merge{true};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::DoMerge:Checkpoint", [&](void*) {
    compaction_running.CountDown();
    if (slow_merge.load(std::memory_order_acquire)) {
      SleepFor(1s * kTimeMultiplier);
    }
  });
  sync_point->EnableProcessing();

  // Compact() schedules the compaction asynchronously on the priority pool.
  ASSERT_OK(follower_index->Compact());
  ASSERT_TRUE(compaction_running.WaitFor(60s * kTimeMultiplier))
      << "Vector index compaction did not start merging";

  // Shut the leader down on a background thread; it must not block on its flush-on-shutdown.
  CountDownLatch shutdown_done{1};
  TestThreadHolder threads;
  threads.AddThreadFunctor([this, shutdown_idx, &shutdown_done] {
    cluster_->mini_tablet_server(shutdown_idx)->Shutdown();
    shutdown_done.CountDown();
  });

  auto shut_down = shutdown_done.WaitFor(30s * kTimeMultiplier);

  // Release the merge so a hung shutdown can complete (and its thread join) for cleanup; the small
  // chunks finish merging almost immediately once not slowed, freeing the pool worker for the
  // starved flush.
  slow_merge.store(false, std::memory_order_release);
  sync_point->DisableProcessing();
  threads.WaitAndStop(120s * kTimeMultiplier);

  ASSERT_TRUE(shut_down)
      << "tserver shutdown hung: a vector index compaction starved the flush-on-shutdown";
}

TEST_P(PgVectorIndexTest, DeleteAndUpdate) {
  constexpr size_t kNumRows = 64;
  const std::string kDistantVector = "[100, 500, 9000]";
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2", kDistantVector));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 20"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES(1000, NULL)"));

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(3),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));

  std::vector<std::string> expected_filtered = {
    BuildRow(2, kDistantVector),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, expected_filtered));
}

// The search merges rows from intents with the index results. A row without a vector there must
// be skipped, and must hide its old vector, which is still in the index.
void PgVectorIndexTest::TestNullVectorsInIntents(bool commit) {
  constexpr size_t kNumRows = 64;
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  // So the search finds the filled rows in the index, not in intents.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  if (commit) {
    // Keep the committed rows in intents.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  }

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1000, NULL), (1001, NULL)"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 3"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  if (commit) {
    ASSERT_OK(conn.CommitTransaction());
  }

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(2),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, {ExpectedRow(2)}));
}

TEST_P(PgVectorIndexTest, NullVectorsInIntents) {
  TestNullVectorsInIntents(/* commit= */ false);
}

TEST_P(PgVectorIndexTest, NullVectorsInNotAppliedIntents) {
  TestNullVectorsInIntents(/* commit= */ true);
}

// In skip prefix locks mode a serializable write takes a strong intent on the table's top level
// key. The search must not take it for a row with intents, neither in the writing transaction nor
// after the transaction committed but before it was applied (GH#36749). Only a colocated table's
// scan reaches this key, and skip prefix locks mode requires packed rows. The row packing version
// and the vector index engine don't matter, so these tests run with the defaults only.
class PgVectorIndexSkipPrefixLocksTest : public PgVectorIndexTestBase {
 protected:
  bool IsColocated() const override {
    return true;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV2;
  }

  void TestSearchAfterSerializableWrite(bool commit);
};

void PgVectorIndexSkipPrefixLocksTest::TestSearchAfterSerializableWrite(bool commit) {
  constexpr size_t kNumRows = 64;
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";
  const std::string kCloserVector = "[0.0625, 0.125, 0.1875]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  // So the search finds the filled rows in the index, not in intents.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  if (commit) {
    // Keep the committed rows in intents.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  }

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (1000, '$0')", kCloserVector));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  if (commit) {
    ASSERT_OK(conn.CommitTransaction());
  }

  std::vector<std::string> expected = {
    BuildRow(1000, kCloserVector),
    BuildRow(10, kCloseVector),
    ExpectedRow(2),
    ExpectedRow(3),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, {ExpectedRow(2)}));
}

TEST_F(PgVectorIndexSkipPrefixLocksTest, SearchAfterSerializableWrite) {
  TestSearchAfterSerializableWrite(/* commit= */ false);
}

TEST_F(PgVectorIndexSkipPrefixLocksTest, SearchAfterCommittedNotAppliedSerializableWrite) {
  TestSearchAfterSerializableWrite(/* commit= */ true);
}

TEST_P(PgVectorIndexTest, IndexCheck) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, 500, 9000]' WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 3"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1000, NULL)"));
  // The check covers only the applied rows.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  size_t leader_rows = 0;
  size_t num_follower_checks = 0;
  for (const auto& check : ASSERT_RESULT(VerifyVectorIndexes())) {
    ASSERT_EQ(check.result().inconsistencies_size(), 0) << check.ShortDebugString();
    if (check.role() == PeerRole::LEADER) {
      leader_rows += check.result().num_rows();
    } else {
      ++num_follower_checks;
    }
  }
  // Rows 1 and 3 and the inserted row have no vector.
  ASSERT_EQ(leader_rows, kNumRows - 2);
  ASSERT_GT(num_follower_checks, 0);
}

void PgVectorIndexTest::TestRandom() {
  constexpr size_t kLimit = 10;
  constexpr size_t kDimensionMultiplier = RegularBuildVsDebugVsSanitizers(96, 4, 4);
  constexpr size_t kNumRows = RegularBuildVsDebugVsSanitizers(1000, 1000, 100);
  constexpr int kNumIterations = RegularBuildVsDebugVsSanitizers(2000, 20, 10);

  real_dimensions_ = 16;
  dimensions_ = real_dimensions_ * kDimensionMultiplier;

  unum::usearch::metric_punned_t metric(
      dimensions_, UsearchMetricKind(), unum::usearch::scalar_kind_t::f32_k);

  std::vector<const HdrHistogram*> read_histograms;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    read_histograms.push_back(
      cluster_->mini_tablet_server(i)->metric_entity().FindOrCreateMetric<Histogram>(
          &METRIC_handler_latency_yb_tserver_TabletServerService_Read)->underlying());
  }

  auto conn = ASSERT_RESULT(MakeIndexAndFillRandom(kNumRows));

  size_t sum_missing = 0;
  std::vector<size_t> counts;

  uint64_t total_read_time = 0;

  for (int i = 0; i != kNumIterations;) {
    auto query_vector = RandomVector();

    uint64_t read_sum_before = SumHistograms(read_histograms);

    auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(
        "SELECT id FROM test" + IndexQuerySuffix(query_vector, kLimit)));

    total_read_time += SumHistograms(read_histograms) - read_sum_before;
    std::vector<int64_t> expected(vectors_.size());
    std::generate(expected.begin(), expected.end(), [n{0LL}]() mutable { return n++; });
    std::sort(
        expected.begin(), expected.end(),
        [&metric, &query_vector, &vectors = vectors_](size_t li, size_t ri) {
      const auto& lhs = vectors[li];
      const auto& rhs = vectors[ri];
      return metric(VectorToBytePtr(query_vector), VectorToBytePtr(lhs)) <
             metric(VectorToBytePtr(query_vector), VectorToBytePtr(rhs));
    });
    size_t ep = 0;
    for (int64_t id : rows) {
      while (ep < expected.size() && id != expected[ep]) {
        ++ep;
      }
      if (ep == expected.size()) {
        break;
      }
      ++ep;
    }
    size_t missing = ep - kLimit;
    if (missing > counts.size()) {
      LOG(INFO)
          << "New max: " << missing << ", fetched: " << AsString(rows) << ", expected: "
          << AsString(boost::make_iterator_range(
                 expected.begin(), expected.begin() + kLimit + missing));
    }
    if (ep == expected.size()) {
      // It means that vectors were fetched in a different order.
      // Since it is infrequent case we could just retry this iteration.
      continue;
    }
    counts.resize(std::max(counts.size(), missing + 1));
    ++counts[missing];
    sum_missing += missing;
    if (++i >= kNumIterations && sum_missing * 50 <= kLimit * kNumIterations) {
      break;
    }
  }
  LOG(INFO)
      << "Counts: " << AsString(counts)
      << ", recall: " << 1.0 - sum_missing * 1.0 / (kLimit * kNumIterations)
      << ", average read time: " << (total_read_time / kNumIterations) << " us";
}

TEST_P(PgVectorIndexTest, Random) {
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomInnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomCosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestRandom();
}

void PgVectorIndexTest::TestMetric(const std::string& expected) {
  auto kDimensions = 2;
  auto conn = ASSERT_RESULT(MakeIndex(kDimensions));

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[0.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[5.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (3, '[9.0, 0.0]')"));

  const auto query_vector = "[2.0, 1.0]";
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT id FROM test" + IndexQuerySuffix(query_vector, 5)));
  ASSERT_EQ(result, expected);
}

TEST_P(PgVectorIndexTest, L2) {
  distance_kind_ = vector_index::DistanceKind::kL2Squared;
  TestMetric("1; 2; 3");
}

TEST_P(PgVectorIndexTest, InnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestMetric("3; 2; 1");
}

TEST_P(PgVectorIndexTest, Cosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestMetric("2; 3; 1");
}

TEST_P(PgVectorIndexTest, EfSearch) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;

  constexpr size_t kNumRows = 1000;
  constexpr int kIterations = 10;
  constexpr int kSmallEf = 1;
  constexpr int kBigEf = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, 1, AddFilter::kFalse));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  for (int i = 0; i != kIterations; ++i) {
    for (int ef : {kSmallEf, kBigEf}) {
      ASSERT_OK(conn.ExecuteFormat("SET hnsw.ef_search = $0", ef));
      ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_max_checked_entries) = ef * 100;
      auto valid = RowsMatch(conn, "", ExpectedRows(1));
      ASSERT_TRUE(valid || ef == kSmallEf);
    }
  }
}

TEST_P(PgVectorIndexTest, Paging) {
  constexpr int kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE fk (id INT PRIMARY KEY)"));

  auto keys = {42, 3, 21, 10, 30};

  std::vector<int> available_keys;
  for (auto key : keys) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO fk VALUES ($0)", key));
    available_keys.push_back(key);
    std::ranges::sort(available_keys);
    std::vector<std::string> expected;
    for (auto k : available_keys) {
      expected.push_back(Format("$0, $1, $0", k, AsString(Vector(k))));
    }
    for (int limit = 1; limit <= kNumRows; limit *= 2) {
      SCOPED_TRACE(Format("limit: $0", limit));
      ASSERT_NO_FATALS(VerifyRows(
          conn, "INNER JOIN fk as f ON t.id = f.id", expected, limit));
    }
  }
}

TEST_P(PgVectorIndexTest, PagingWithFunction) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateEchoFunction(conn));
  ASSERT_OK(conn.Execute(
      "INSERT INTO test SELECT i, vector('[1.0, 1.0, 1.' || lpad(i::text, 5, '0') || ']') "
      "FROM generate_series (1, 300) AS i"));
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test WHERE id >= echo_int(0) "
      "ORDER BY embedding <-> '[1.0, 1.0, 1.0]'::vector LIMIT 100;"));
  LOG(INFO) << "Result: " << result;
}

TEST_P(PgVectorIndexTest, Options) {
  auto conn = ASSERT_RESULT(MakeTable());
  std::unordered_map<TabletId, std::unordered_set<TableId>> checked_indexes;
  std::vector<std::string> option_names = {"m", "m0", "ef_construction"};
  // We need unique values for used params. Since m and ef has different allowed intervals,
  // use different counters for them. counters[0] for m/m0 and counters[1] for ef_construction.
  std::array<size_t, 2> counters = {32, 64};
  for (int i = 0; i != 1 << option_names.size(); ++i) {
    std::string expected_options;
    {
      std::string options;
      size_t prev_value = 0;
      for (size_t j = 0; j != option_names.size(); ++j) {
        if (!expected_options.empty()) {
          expected_options += " ";
        }
        size_t value;
        if ((i & (1 << j))) {
          value = ++counters[j >= 2];
          if (!options.empty()) {
            options += ", ";
          }
          options += Format("$0 = $1", option_names[j], value);
        } else {
          switch (j) {
            case 0:
              value = 32; // Default value for m
              break;
            case 1:
              // When not specified m0 uses value of m.
              value = prev_value;
              break;
            case 2:
              value = 200; // Default value for ef
              break;
            default:
              ASSERT_LT(j, 4U) << "Unexpected number of options";
              value = 0;
              break;
          }
        }
        expected_options += Format("$0: $1", option_names[j], value);
        prev_value = value;
      }
      switch (Engine()) {
        case VectorIndexEngine::kYbHnswHnswlib:
          expected_options += " backend: YB_HNSW_HNSWLIB";
          break;
      }
      if (!options.empty()) {
        options = " WITH (" + options + ")";
      }
      auto query = "CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)" + options;
      LOG(INFO) << "Query: " << query;
      ASSERT_OK(conn.Execute(query));
    }
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    std::unordered_map<TabletId, size_t> checked_tablets;
    for (const auto& peer : peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto vector_indexes = tablet->vector_indexes().List();
      if (checked_tablets[peer->tablet_id()] != 0) {
        continue;
      }
      LOG(INFO) << "Vector indexes: " << AsString(vector_indexes);
      auto& tablet_indexes = checked_indexes[peer->tablet_id()];
      size_t num_new_indexes = 0;
      for (const auto& vector_index : *vector_indexes) {
        if (!tablet_indexes.insert(vector_index->table_id()).second) {
          continue;
        }
        ++num_new_indexes;
        auto doc_read_context = ASSERT_RESULT(
            tablet->metadata()->GetTableInfo(vector_index->table_id()))->doc_read_context;
        const auto& hnsw_options = doc_read_context->vector_idx_options->hnsw();
        LOG(INFO)
            << "Vector index: " << AsString(vector_index) << ", options: "
            << AsString(hnsw_options);
        ASSERT_EQ(AsString(hnsw_options), expected_options);
      }
      checked_tablets[peer->tablet_id()] = num_new_indexes;
    }
    for (const auto& [tablet_id, num_new_indexes] : checked_tablets) {
      ASSERT_EQ(num_new_indexes, 1) << "Wrong number of new indexes for: " << tablet_id;
    }
  }
}

TEST_P(PgVectorIndexTest, Backup) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(cluster_->StartYbControllerServers());

  TestManyRows(AddFilter::kFalse);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateIndex(conn, "vi_cos", vector_index::DistanceKind::kCosine));

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, "ysql." + DbName()));

  // Restore the backup.
  const std::string kRestoreDb = "restored_db";
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, "ysql." + kRestoreDb));

  auto restore_conn = ASSERT_RESULT(ConnectToDB(kRestoreDb));
  VerifyRead(restore_conn, 10, AddFilter::kFalse);
}

TEST_P(PgVectorIndexTest, ReverseColumnOrder) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v2 vector(1)"));
  ASSERT_OK(CreateIndex(conn, "vi_2", std::nullopt, "v2"));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(CreateIndex(conn, "vi_3", std::nullopt, "v2"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1, 2, 3]', '[4]')"));
}

// Routing does not depend on the vector index engine or the row packing, so these tests run with
// the defaults only.
class PgVectorIndexPkRoutingTest : public PgVectorIndexTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV2;
  }

  static constexpr int kNumTenants = 20;
  static constexpr int kRowsPerTenant = 10;
  static constexpr int kNumTablets = 4;

  struct QueryStats {
    std::vector<int32_t> ids;
    // Partitions searched over all fetch rounds of the query, and the number of rounds.
    int64_t partitions = 0;
    uint64_t rounds = 0;
  };

  Result<QueryStats> Query(PGConn& conn, const std::string& query) {
    auto [sum_before, count_before] = PartitionsQueried();
    QueryStats result;
    result.ids = VERIFY_RESULT(conn.FetchRows<int32_t>(query));
    auto [sum_after, count_after] = PartitionsQueried();
    result.partitions = sum_after - sum_before;
    result.rounds = count_after - count_before;
    return result;
  }

  // Embedding of row (tenant, id) is [tenant, id, 0], so the rows of a tenant ordered by distance
  // to [tenant, 0, 0] are ordered by id.
  Status CreateAndFill(PGConn& conn, const std::string& table, const std::string& key_columns,
                       const std::string& primary_key, const std::string& split) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 ($1, embedding vector(3), PRIMARY KEY ($2)) $3",
        table, key_columns, primary_key, split));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE INDEX ON $0 USING ybhnsw (embedding vector_l2_ops)", table));
    return conn.ExecuteFormat(
        "INSERT INTO $0 SELECT t, i, vector('[' || t || ', ' || i || ', 0]') "
        "FROM generate_series(0, $1) t, generate_series(0, $2) i",
        table, kNumTenants - 1, kRowsPerTenant - 1);
  }

  static std::string KnnQuery(
      const std::string& table, const std::string& where, int tenant, int limit) {
    return Format(
        "/*+IndexScan($0 $0_embedding_idx)*/ SELECT id FROM $0 WHERE $1 "
        "ORDER BY embedding <-> '[$2, 0, 0]' LIMIT $3",
        table, where, tenant, limit);
  }

  static std::vector<int32_t> Ids(int32_t first, int32_t count) {
    std::vector<int32_t> result(count);
    std::iota(result.begin(), result.end(), first);
    return result;
  }
};

TEST_F(PgVectorIndexPkRoutingTest, HashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "th", "tenant INT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("th", Format("tenant = $0", tenant), tenant, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // Without a filter on the key, and with routing disabled, every partition is searched.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "id < 3", 4, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = false;
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4", 4, 5)));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = true;
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // A non-integer cross-type equality isn't routed, but still returns the right rows.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4.0", 4, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // With several equalities on the key, the one with the fewest values bounds the search.
  for (const auto* where : {"tenant = 3 AND tenant IN (3, 4)", "tenant IN (3, 4) AND tenant = 3"}) {
    SCOPED_TRACE(where);
    stats = ASSERT_RESULT(Query(conn, KnnQuery("th", where, 3, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // IN list: one partition per distinct partition of the tenants.
  auto hashes = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT yb_hash_code(t) FROM generate_series(0, $0) t ORDER BY t", kNumTenants - 1)));
  std::vector<size_t> partition_of_tenant;
  for (auto hash : hashes) {
    partition_of_tenant.push_back(ASSERT_RESULT(HashPartition("th", hash)));
  }
  int other_tenant = 1;
  while (partition_of_tenant[other_tenant] == partition_of_tenant[0]) {
    ++other_tenant;
    ASSERT_LT(other_tenant, kNumTenants);
  }
  // Fetch every row of both tenants, so a missing partition would lose rows.
  stats = ASSERT_RESULT(Query(
      conn, KnnQuery("th", Format("tenant IN (0, $0)", other_tenant), 0, 2 * kRowsPerTenant)));
  std::ranges::sort(stats.ids);
  std::vector<int32_t> expected_ids;
  for (int id = 0; id != kRowsPerTenant; ++id) {
    expected_ids.insert(expected_ids.end(), {id, id});
  }
  ASSERT_EQ(stats.ids, expected_ids);
  ASSERT_EQ(stats.partitions, 2);

  // Prepared statement with a generic plan: the parameter is bound at execution.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(int, vector) AS /*+IndexScan(th)*/ SELECT id FROM th WHERE tenant = $1 "
      "ORDER BY embedding <-> $2 LIMIT 4"));
  for (int tenant : {3, 7}) {
    auto stats = ASSERT_RESULT(Query(
        conn, Format("EXECUTE knn($0, '[$0, 0, 0]')", tenant)));
    ASSERT_EQ(stats.ids, Ids(0, 4));
    ASSERT_EQ(stats.partitions, 1);
  }
  ASSERT_OK(conn.Execute("RESET plan_cache_mode"));

  // Nested loop: the inner scan is routed again on each rescan.
  stats = ASSERT_RESULT(Query(conn,
      "/*+NestLoop(t th) Leading((t th)) IndexScan(th)*/ "
      "SELECT x.id FROM (VALUES (2), (9)) t(tenant), LATERAL ("
      "  SELECT id FROM th WHERE th.tenant = t.tenant "
      "  ORDER BY embedding <-> '[0, 0, 0]' LIMIT 2) x"));
  ASSERT_EQ(stats.ids, (std::vector<int32_t>{0, 1, 0, 1}));
  ASSERT_EQ(stats.partitions, 2);

  // The join rejects the first rows returned by the vector index, so Postgres fetches more rounds.
  // The pruned partitions must not keep the query waiting for them.
  ASSERT_OK(conn.Execute("CREATE TABLE keep (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO keep VALUES (6), (7), (8)"));
  stats = ASSERT_RESULT(Query(conn,
      "SELECT th.id FROM th INNER JOIN keep AS k ON th.id = k.id WHERE th.tenant = 5 "
      "ORDER BY th.embedding <-> '[5, 0, 0]' LIMIT 3"));
  ASSERT_EQ(stats.ids, Ids(6, 3));
  ASSERT_GE(stats.rounds, 2);
  ASSERT_LE(stats.partitions, static_cast<int64_t>(stats.rounds));
}

TEST_F(PgVectorIndexPkRoutingTest, MultiColumnHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tm", "tenant INT, id INT", "(tenant, id) HASH",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3 AND id = 2", 3, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{2});
  ASSERT_EQ(stats.partitions, 1);

  // Only some of the hash columns: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3", 3, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, TextHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tt", "tenant TEXT COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  // Expressions with a non-C collation are not pushed down, so their key equalities can't be
  // routed. Their results must still be correct.
  ASSERT_OK(CreateAndFill(
      conn, "tt_icu", "tenant TEXT COLLATE \"en-US-x-icu\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt", Format("tenant = '$0'", tenant), tenant, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);

    // PG filters these rows itself, but the vector prefetch is sized by the LIMIT,
    // so a small LIMIT pages through several rounds. A LIMIT covering every row keeps it
    // to one round.
    stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt_icu", Format("tenant = '$0'", tenant), tenant,
                       kNumTenants * kRowsPerTenant)));
    ASSERT_EQ(stats.ids, Ids(0, kRowsPerTenant));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // varchar has no equality operator of its own: the column is compared with text's operator, and
  // the value can be text or varchar.
  ASSERT_OK(CreateAndFill(
      conn, "tv", "tenant VARCHAR(20) COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  for (const auto* value : {"'7'", "'7'::varchar", "'7'::text", "'7'::varchar(5)"}) {
    SCOPED_TRACE(value);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", Format("tenant = $0", value), 7, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);
  }
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", "tenant IN ('7', '7')", 7, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, 1);
}

// Integer literals are int4 and parameters can be int8, so equalities on integer keys are often
// cross-type (e.g. int8 = int4). Those are routed after converting the value to the column type.
TEST_F(PgVectorIndexPkRoutingTest, IntegerCrossTypeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tb", "tenant BIGINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  ASSERT_OK(CreateAndFill(
      conn, "ts", "tenant SMALLINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (const auto* where : {"tenant = 6", "6 = tenant", "tenant = 6::smallint", "tenant IN (6)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tb", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }
  for (const auto* where : {"tenant = 6", "tenant = 6::bigint", "tenant IN (6::bigint)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // A value outside the smallint range matches no row, so the column isn't routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant = 70000", 6, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{});
  ASSERT_EQ(stats.partitions, kNumTablets);
  stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant IN (6, 70000)", 6, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // A bigint parameter against a smallint column, with a generic plan.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(bigint, vector) AS /*+IndexScan(ts ts_embedding_idx)*/ SELECT id FROM ts "
      "WHERE tenant = $1 ORDER BY embedding <-> $2 LIMIT 4"));
  stats = ASSERT_RESULT(Query(conn, "EXECUTE knn(9, '[9, 0, 0]')"));
  ASSERT_EQ(stats.ids, Ids(0, 4));
  ASSERT_EQ(stats.partitions, 1);
}

TEST_F(PgVectorIndexPkRoutingTest, FloatKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tf", "tenant FLOAT8, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  // -0 equals 0 but has a different key encoding, so float keys are never routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tf", "tenant = '-0'", 0, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, RangeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  // Partitions: [, 5), [5, (10, 5)), [(10, 5), 15), [15, ).
  ASSERT_OK(CreateAndFill(
      conn, "tr", "tenant INT, id INT", "tenant ASC, id ASC",
      "SPLIT AT VALUES ((5), (10, 5), (15))"));

  for (auto [tenant, partitions] : std::initializer_list<std::pair<int, int64_t>>{
           // The split point (5) sorts after the prefix of tenant 5, so both of its neighbours
           // are searched.
           {2, 1}, {5, 2}, {7, 1}, {10, 2}, {12, 1}, {15, 2}, {19, 1}}) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tr", Format("tenant = $0", tenant), tenant, 7)));
    ASSERT_EQ(stats.ids, Ids(0, 7));
    ASSERT_EQ(stats.partitions, partitions);
  }

  // A prefix of two range columns.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "tenant = 10 AND id = 7", 10, 3)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{7});
  ASSERT_EQ(stats.partitions, 1);

  // No equality on the leading range column: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "id = 7", 10, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

// A stuck vector index insert blocks the apply of a write sent by a local call. The Perform that
// sent the write should still finish at its deadline, not when the insert is unblocked.
void PgVectorIndexTest::TestPerformDeadlineWithStuckLocalWrite(UseSharedMemory use_shared_memory) {
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto* pg_ts = cluster_->mini_tablet_server(kPgTsIndex);
  const auto pg_ts_uuid = pg_ts->fs_manager().uuid();
  ASSERT_OK(WaitFor([this, &table_id, &pg_ts_uuid]() -> Result<bool> {
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
      if (VERIFY_RESULT(peer->GetConsensus())->GetLeaderStatus() !=
              consensus::LeaderStatus::LEADER_AND_READY) {
        continue;
      }
      if (peer->permanent_uuid() == pg_ts_uuid) {
        return true;
      }
      WARN_NOT_OK(StepDown(peer, pg_ts_uuid, ForceStepDown::kTrue), "StepDown failed");
    }
    return false;
  }, 60s * kTimeMultiplier, "Tablet leader on postgres tserver"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = use_shared_memory;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) = 2000 * kTimeMultiplier;
  ASSERT_OK(RestartPostgres());
  conn = ASSERT_RESULT(Connect());
  // Updates the meta cache with the new leader.
  ASSERT_OK(InsertRows(conn, 1, 1, /* keep_vectors= */ false, NonTransactionalWrites::kTrue));

  auto leader_indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(leader_indexes.size(), 1);
  const auto leader_index_path = leader_indexes.front()->path();

  CountDownLatch insert_parked{1};
  CountDownLatch resume_insert{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::Insert:BeforeAllocateTasks", [&](void* arg) {
    if (*static_cast<const std::string*>(arg) != leader_index_path) {
      return;
    }
    insert_parked.CountDown();
    ASSERT_TRUE(resume_insert.WaitFor(60s * kTimeMultiplier));
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &resume_insert] {
    resume_insert.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  // Autocommit single row insert is a non-transactional write, it inserts the vector on apply.
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  ASSERT_NOK(conn.ExecuteFormat("INSERT INTO test VALUES (2, '$0')", AsString(Vector(2))));
  ASSERT_EQ(insert_parked.count(), 0);

  if (!use_shared_memory) {
    ASSERT_OK(WaitFor([pg_ts]() -> Result<bool> {
      rpc::DumpRunningRpcsRequestPB req;
      rpc::DumpRunningRpcsResponsePB resp;
      RETURN_NOT_OK(pg_ts->server()->messenger()->DumpRunningRpcs(req, &resp));
      for (const auto& connection : resp.inbound_connections()) {
        for (const auto& call : connection.calls_in_flight()) {
          if (call.header().remote_method().method_name() == "Perform") {
            return false;
          }
        }
      }
      return true;
    }, 5s * kTimeMultiplier, "Perform finished while the insert is stuck"));
  }

  resume_insert.CountDown();
  if (use_shared_memory) {
    // The tserver posts the response of the timed out Perform only after the insert finishes.
    // Until then the exchange is busy and Performs fall back to RPC.
    SingleMetricWatcher rpc_performs(
        *pg_ts->server(), METRIC_handler_latency_yb_tserver_PgClientService_Perform);
    ASSERT_OK(WaitFor([&conn, &rpc_performs]() -> Result<bool> {
      return VERIFY_RESULT(rpc_performs.Delta([&conn] {
        return ResultToStatus(conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));
      })) == 0;
    }, 10s * kTimeMultiplier, "Performs over shared memory"));
  }
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (3, '$0')", AsString(Vector(3))));
  // The timed out write is applied once unblocked.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 2")), 2);
}

TEST_P(PgVectorIndexTest, PerformDeadlineWithStuckLocalWrite) {
  TestPerformDeadlineWithStuckLocalWrite(UseSharedMemory::kFalse);
}

TEST_P(PgVectorIndexTest, PerformDeadlineWithStuckLocalWriteSharedMemory) {
  TestPerformDeadlineWithStuckLocalWrite(UseSharedMemory::kTrue);
}

// ExecScan clears plan_limit when the scan has a qual. A ybhnsw scan keeps the LIMIT
// separately and must prefetch that many rows when Postgres evaluates the WHERE (#37005).
// Reading the whole table is what overflows the RPC on a large vector column.
class PgVectorIndexLimitTest : public PgVectorIndexTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV2;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  // Rows the items vector index placed in search responses since the tablet opened.
  Result<uint64_t> VectorRowsReturned() {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName("items"));
    uint64_t sum = 0;
    for (const auto& peer : ListTableActiveTabletLeadersPeers(cluster_.get(), table_id)) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      auto indexes = tablet->vector_indexes().List();
      if (!indexes) {
        continue;
      }
      for (const auto& index : *indexes) {
        sum += index->metrics().result_size->TotalSum();
      }
    }
    return sum;
  }
};

TEST_F(PgVectorIndexLimitTest, FilteredTopKUsesLimit) {
  constexpr int kNumRows = 400;
  constexpr int kLimit = 4;
  // Embeddings are [id, 0, 0], so distance to the origin is the id order.
  const auto query_vector = "'[0, 0, 0]'";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE items (id int PRIMARY KEY, content text, metadata json, tenant text, "
      "embedding vector(3)) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE INDEX ON items USING ybhnsw (embedding vector_l2_ops) WITH ($0)",
      kCheapIndexBuildOptions));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO items SELECT g, 'chunk ' || g, "
      "json_build_object('category', (ARRAY['news','blog','doc','faq'])[1 + (g % 4)], "
      "'year', 2010 + (g % 15)), "
      "'t' || (g % 20), vector('[' || g || ', 0, 0]') "
      "FROM generate_series(1, $0) g",
      kNumRows));
  ASSERT_OK(CreateEchoFunction(conn));

  const auto index_query = [&](const std::string& where) {
    return Format(
        "/*+IndexScan(items items_embedding_idx)*/ SELECT id FROM items $0 "
        "ORDER BY embedding <-> $1 LIMIT $2",
        where, query_vector, kLimit);
  };

  auto expect = [&](const std::string& name, const std::string& query,
                    const std::vector<int32_t>& expected, uint64_t max_rows) -> Status {
    const auto before = VERIFY_RESULT(VectorRowsReturned());
    auto ids = VERIFY_RESULT(conn.FetchRows<int32_t>(query));
    SCHECK_EQ(AsString(ids), AsString(expected), IllegalState, name);
    const auto read = VERIFY_RESULT(VectorRowsReturned()) - before;
    // The broken scan prefetches every row of the table into one response.
    SCHECK_LT(read, static_cast<uint64_t>(kNumRows), IllegalState,
              Format("$0 read $1 rows", name, read));
    SCHECK_LE(read, max_rows, IllegalState, Format("$0 read $1 rows", name, read));
    SCHECK_GE(read, expected.size(), IllegalState, Format("$0 read $1 rows", name, read));
    return Status::OK();
  };

  auto expect_plan = [&](const std::string& query, bool postgres_filter) -> Status {
    auto plan = VERIFY_RESULT(conn.FetchAllAsString("EXPLAIN (COSTS OFF) " + query));
    SCHECK_NE(plan.find("Index Scan"), std::string::npos, IllegalState, plan);
    if (postgres_filter) {
      SCHECK_EQ(plan.find("Storage Filter:"), std::string::npos, IllegalState, plan);
      SCHECK_NE(plan.find("Filter:"), std::string::npos, IllegalState, plan);
    } else if (query.find("WHERE") != std::string::npos) {
      SCHECK_NE(plan.find("Storage Filter:"), std::string::npos, IllegalState, plan);
    }
    return Status::OK();
  };

  const std::vector<int32_t> nearest = {1, 2, 3, 4};
  // One prefetch of LIMIT, plus a second page the scan may open before it stops.
  const uint64_t kOnePage = 2 * kLimit;
  // A qual Postgres evaluates. Every row passes, so the scan stops after the nearest page.
  const auto always_true = index_query("WHERE id >= echo_int(0)");
  ASSERT_OK(expect_plan(always_true, true));
  ASSERT_OK(expect("volatile filter", always_true, nearest, kOnePage));

  const auto no_filter = index_query("");
  ASSERT_OK(expect_plan(no_filter, false));
  ASSERT_OK(expect("no filter", no_filter, nearest, kOnePage));

  const auto distance = index_query(Format("WHERE embedding <-> $0 < 1000000", query_vector));
  ASSERT_OK(expect_plan(distance, true));
  ASSERT_OK(expect("distance threshold", distance, nearest, kOnePage));

  // category cycles news, blog, doc, faq. The doc rows are 2, 6, 10, 14.
  const auto json_filter = index_query(
      "WHERE metadata::jsonb @@ '$.category == \"doc\"'");
  ASSERT_OK(expect_plan(json_filter, true));
  ASSERT_OK(expect("json filter", json_filter, {2, 6, 10, 14}, 32));

  // year is 2010 + (id % 15). The first ids at or above 2020 are 10..13.
  const auto cast_filter = index_query("WHERE (metadata->>'year')::int >= 2020");
  ASSERT_OK(expect_plan(cast_filter, true));
  ASSERT_OK(expect("cast filter", cast_filter, {10, 11, 12, 13}, 32));

  // Pushed to DocDB, so plan_limit already survived. tenant 't3' is id % 20 == 3.
  const auto pushed = index_query("WHERE tenant = 't3'");
  ASSERT_OK(expect_plan(pushed, false));
  ASSERT_OK(expect("pushed filter", pushed, {3, 23, 43, 63}, 160));

  // OFFSET has to page: the prefetch is the LIMIT, and the scan keeps fetching.
  const auto offset_query = Format(
      "/*+IndexScan(items items_embedding_idx)*/ SELECT id FROM items "
      "ORDER BY embedding <-> $0 LIMIT $1 OFFSET 2",
      query_vector, kLimit);
  ASSERT_OK(expect("offset", offset_query, {3, 4, 5, 6}, 24));

  ASSERT_OK(conn.Execute("CREATE ROLE limit_reader"));
  ASSERT_OK(conn.Execute("GRANT SELECT ON items TO limit_reader"));
  ASSERT_OK(conn.Execute("ALTER TABLE items ENABLE ROW LEVEL SECURITY"));
  ASSERT_OK(conn.Execute(
      "CREATE POLICY items_tenant ON items USING (tenant = current_setting('app.tenant'))"));
  ASSERT_OK(conn.Execute("SET app.tenant = 't3'"));
  ASSERT_OK(conn.Execute("SET ROLE limit_reader"));
  const auto rls = index_query("");
  ASSERT_OK(expect_plan(rls, true));
  ASSERT_OK(expect("row level security", rls, {3, 23, 43, 63}, 160));
  ASSERT_OK(conn.Execute("RESET ROLE"));

  // Same top-k without the vector index.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("SET enable_indexscan = off"));
  ASSERT_OK(conn.Execute("SET enable_indexonlyscan = off"));
  const auto seq_query = Format(
      "SELECT id FROM items ORDER BY embedding <-> $0 LIMIT $1", query_vector, kLimit);
  const auto before = ASSERT_RESULT(VectorRowsReturned());
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRows<int32_t>(seq_query)), nearest);
  ASSERT_EQ(ASSERT_RESULT(VectorRowsReturned()), before);
  ASSERT_OK(conn.Execute("RESET enable_indexscan"));
  ASSERT_OK(conn.Execute("RESET enable_indexonlyscan"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
}

class PgVectorIndexBackendFlagTest : public YBTest {};

TEST_F(PgVectorIndexBackendFlagTest, RejectsRemovedBackends) {
  for (const auto* backend : {"usearch", "hnswlib", "yb_hnsw"}) {
    ASSERT_NOK(SET_FLAG(vector_index_backend, backend));
  }
  for (const auto* backend : {"yb_hnsw_usearch", "yb_hnsw_hnswlib"}) {
    ASSERT_OK(SET_FLAG(vector_index_backend, backend));
  }
}

}  // namespace yb::pgwrapper
