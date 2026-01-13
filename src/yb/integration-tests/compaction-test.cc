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

#include <boost/function.hpp>

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_op.h"

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"

#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/doc_ttl_util.h"
#include "yb/dockv/reader_projection.h"

#include "yb/gutil/ref_counted.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/test_workload.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/util/task_metrics.h"

#include "yb/rpc/messenger.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/full_compaction_manager.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/status_format.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"
#include "yb/util/tsan_util.h"

using namespace std::literals; // NOLINT

DECLARE_bool(TEST_disable_adding_last_compaction_to_tablet_metadata);
DECLARE_bool(TEST_disable_adding_user_frontier_to_sst);
DECLARE_bool(TEST_disable_getting_user_frontier_from_mem_table);
DECLARE_bool(TEST_pause_before_full_compaction);
DECLARE_bool(enable_ondisk_compression);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(file_expiration_ignore_value_ttl);
DECLARE_bool(file_expiration_value_ttl_overrides_table_ttl);
DECLARE_bool(rocksdb_allow_multiple_pending_compactions_for_priority_thread_pool);
DECLARE_bool(rocksdb_determine_compaction_input_at_start);
DECLARE_bool(tablet_enable_ttl_file_filter);
DECLARE_bool(use_priority_thread_pool_for_compactions);
DECLARE_bool(ycql_enable_packed_row);

DECLARE_double(auto_compact_percent_obsolete);

DECLARE_int32(auto_compact_check_interval_sec);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(full_compaction_pool_max_queue_size);
DECLARE_int32(full_compaction_pool_max_threads);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(replication_factor);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(scheduled_full_compaction_frequency_hours);
DECLARE_int32(scheduled_full_compaction_jitter_factor_percentage);
DECLARE_int32(timestamp_history_retention_interval_sec);

DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);

DECLARE_uint32(auto_compact_min_obsolete_keys_found);
DECLARE_uint32(auto_compact_stat_window_seconds);

DECLARE_uint64(post_split_compaction_input_size_threshold_bytes);
DECLARE_uint64(rocksdb_max_file_size_for_compaction);

DECLARE_string(allow_compaction_failures_for_tablet_ids);

namespace yb::tserver {

namespace {

constexpr auto kWaitDelay = 10ms;
constexpr auto kPayloadBytes = 8_KB;
constexpr auto kMemStoreSize = 100_KB;
constexpr auto kDefaultNumTablets = 3;
constexpr auto kNumWriteThreads = 4;
constexpr auto kNumReadThreads = 0;

class RocksDbListener : public rocksdb::EventListener {
 public:
  void OnCompactionCompleted(rocksdb::DB* db,
      const rocksdb::CompactionJobInfo& info) override {
    std::lock_guard lock(mutex_);
    ++num_compactions_completed_[db];
    input_files_in_compactions_completed_[db] += info.stats.num_input_files;
    input_bytes_in_compactions_completed_[db] += info.stats.total_input_bytes;
  }

  size_t GetNumCompactionsCompleted(rocksdb::DB* db) {
    std::lock_guard lock(mutex_);
    return num_compactions_completed_[db];
  }

  uint64_t GetInputFilesInCompactionsCompleted(rocksdb::DB* db) {
    std::lock_guard lock(mutex_);
    return input_files_in_compactions_completed_[db];
  }

  uint64_t GetInputBytesInCompactionsCompleted(rocksdb::DB* db) {
    std::lock_guard lock(mutex_);
    return input_bytes_in_compactions_completed_[db];
  }

  void OnFlushCompleted(rocksdb::DB* db, const rocksdb::FlushJobInfo&) override {
    std::lock_guard lock(mutex_);
    ++num_flushes_completed_[db];
  }

  size_t GetNumFlushesCompleted(rocksdb::DB* db) {
    std::lock_guard lock(mutex_);
    return num_flushes_completed_[db];
  }

  void Reset() {
    std::lock_guard lock(mutex_);
    num_compactions_completed_.clear();
    input_files_in_compactions_completed_.clear();
    input_bytes_in_compactions_completed_.clear();
    num_flushes_completed_.clear();
  }

 private:
  using CountByDbMap = std::unordered_map<const rocksdb::DB *, size_t>;

  std::mutex mutex_;
  CountByDbMap num_compactions_completed_ GUARDED_BY(mutex_);
  CountByDbMap input_files_in_compactions_completed_ GUARDED_BY(mutex_);
  CountByDbMap input_bytes_in_compactions_completed_ GUARDED_BY(mutex_);
  CountByDbMap num_flushes_completed_ GUARDED_BY(mutex_);
};

} // namespace

// This fixture supports YCQL only.
class CompactionTest : public YBTest {
 public:
  CompactionTest() {}

  void SetUp() override {
    YBTest::SetUp();

    ASSERT_OK(clock_->Init());
    rocksdb_listener_ = std::make_shared<RocksDbListener>();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_priority_thread_pool_size) = 2;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

    // Disable scheduled compactions by default so we don't have surprise compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_jitter_factor_percentage) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_check_interval_sec) = 0;

    // Start cluster.
    MiniClusterOptions opts;
    opts.num_tablet_servers = NumTabletServers();
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    // These flags should be set after minicluster start, so it wouldn't override them.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = kMemStoreSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 3;

    AddRocksDBListener(rocksdb_listener_);

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    transaction_manager_ = std::make_unique<client::TransactionManager>(
        client_.get(), clock_, client::LocalTabletFilter());
    transaction_pool_ = std::make_unique<client::TransactionPool>(
        transaction_manager_.get(), nullptr /* metric_entity */);
  }

  void TearDown() override {
    workload_->StopAndJoin();
    // Shutdown client before destroying transaction manager, so we don't have transaction RPCs
    // in progress after transaction manager is destroyed.
    client_->Shutdown();
    cluster_->Shutdown();
    YBTest::TearDown();
  }

  void AddRocksDBListener(std::shared_ptr<rocksdb::EventListener> listener) {
    // Patch tablet options inside tablet manager, will be applied to newly created tablets.
    for (int i = 0 ; i < NumTabletServers(); i++) {
      ANNOTATE_IGNORE_WRITES_BEGIN();
      cluster_->GetTabletManager(i)->TEST_tablet_options()->listeners.push_back(listener);
      ANNOTATE_IGNORE_WRITES_END();
    }
  }

  void SetupWorkload(IsolationLevel isolation_level, int num_tablets = kDefaultNumTablets) {
    workload_.reset(new TestYcqlWorkload(cluster_.get()));
    workload_->set_timeout_allowed(true);
    workload_->set_payload_bytes(kPayloadBytes);
    workload_->set_write_batch_size(1);
    workload_->set_num_write_threads(kNumWriteThreads);
    workload_->set_num_read_threads(kNumReadThreads);
    workload_->set_num_tablets(num_tablets);
    workload_->set_transactional(isolation_level, transaction_pool_.get());
    workload_->set_ttl(TtlSec());
    workload_->set_table_ttl(TableTtlSec());
    workload_->set_sequential_write(sequential_write());
    workload_->Setup();

    const auto workload_table_info =
        ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));
    workload_table_id_ = workload_table_info->id();
  }

 protected:

  // -1 implies no ttl.
  virtual int TtlSec() {
    return -1;
  }

  // -1 implies no table ttl.
  virtual int TableTtlSec() {
    return -1;
  }

  virtual int NumTabletServers() {
    return 1;
  }

  virtual bool sequential_write() {
    return false;
  }

  size_t BytesWritten() {
    return workload_->rows_inserted() * kPayloadBytes;
  }

  Status WriteAtLeast(size_t size_bytes) {
    workload_->Start();
    RETURN_NOT_OK(LoggedWaitFor(
        [this, size_bytes] { return BytesWritten() >= size_bytes; }, 60s,
        Format("Waiting until we've written at least $0 bytes ...", size_bytes), kWaitDelay));
    workload_->StopAndJoin();
    LOG(INFO) << "Wrote " << BytesWritten() << " bytes.";
    return Status::OK();
  }

  Status ReadAtLeastRowsOk(int64_t rows_to_read) {
    workload_->set_num_write_threads(0);
    workload_->set_num_read_threads(4);
    const auto rows_read_start = workload_->rows_read_ok();

    workload_->Start();
    RETURN_NOT_OK(LoggedWaitFor(
        [this, rows_to_read, rows_read_start] {
            return workload_->rows_read_ok() >= rows_to_read + rows_read_start;
        }, 60s,
        Format("Waiting until we've read at least $0 rows ...", rows_to_read), kWaitDelay));
    workload_->StopAndJoin();
    const auto rows_read = workload_->rows_read_ok() - rows_read_start;
    LOG(INFO) << "Read " << rows_read << " rows.";
    workload_->set_num_write_threads(kNumWriteThreads);
    workload_->set_num_read_threads(kNumReadThreads);
    return Status::OK();
  }

  Status WriteAtLeastFilesPerDb(size_t num_files) {
    auto dbs = GetAllRocksDbs(cluster_.get());
    workload_->Start();
    RETURN_NOT_OK(LoggedWaitFor(
        [this, &dbs, num_files] {
            for (auto* db : dbs) {
              if (rocksdb_listener_->GetNumFlushesCompleted(db) < num_files) {
                return false;
              }
            }
            return true;
          }, 60s,
        Format("Waiting until we've written at least $0 files per rocksdb ...", num_files),
        kWaitDelay * kTimeMultiplier));
    workload_->StopAndJoin();
    LOG(INFO) << "Wrote " << BytesWritten() << " bytes.";
    return Status::OK();
  }

  Status WaitForNumCompactionsPerDb(size_t num_compactions) {
    auto dbs = GetAllRocksDbs(cluster_.get());
    RETURN_NOT_OK(LoggedWaitFor(
        [this, &dbs, num_compactions] {
            for (auto* db : dbs) {
              if (rocksdb_listener_->GetNumCompactionsCompleted(db) < num_compactions) {
                return false;
              }
            }
            return true;
          }, 60s,
        Format("Waiting until at least $0 compactions per rocksdb finished...", num_compactions),
        kWaitDelay * kTimeMultiplier));
    return Status::OK();
  }

  Status ChangeTableTTL(const client::YBTableName& table_name, int ttl_sec) {
    RETURN_NOT_OK(client_->TableExists(table_name));
    auto alterer = client_->NewTableAlterer(table_name);
    TableProperties table_properties;
    table_properties.SetDefaultTimeToLive(ttl_sec * MonoTime::kMillisecondsPerSecond);
    alterer->SetTableProperties(table_properties);
    return alterer->Alter();
  }

  Status ExecuteManualCompaction() {
    const auto table_info = VERIFY_RESULT(FindTable(cluster_.get(), workload_->table_name()));
    return workload_->client().CompactTables({table_info->id()}, MonoDelta::FromMinutes(1));
  }

  bool CheckEachDbHasExactlyNumFiles(size_t num_files);
  bool CheckEachDbHasAtLeastNumFiles(size_t num_files);

  void TestCompactionAfterTruncate();
  void TestCompactionWithoutFrontiers(
      const size_t num_without_frontiers,
      const size_t num_with_frontiers,
      const bool trigger_manual_compaction);
  void TestCompactionTaskMetrics(const int num_files, bool manual_compactions);

  Status TriggerAdminCompactions(
      ShouldWait should_wait, rocksdb::SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe =
                                  rocksdb::SkipCorruptDataBlocksUnsafe::kFalse) {
    for (int i = 0; i < NumTabletServers(); i++) {
      auto ts_tablet_manager = cluster_->GetTabletManager(i);
      const auto tablet_peers = ts_tablet_manager->GetTabletPeersWithTableId(workload_table_id_);
      TSTabletManager::TabletPtrs workload_tablet_ptrs;
      for (const auto& tablet_peer : tablet_peers) {
        workload_tablet_ptrs.push_back(tablet_peer->shared_tablet_maybe_null());
      }
      RETURN_NOT_OK(ts_tablet_manager->TriggerAdminCompaction(
          workload_tablet_ptrs,
          AdminCompactionOptions{should_wait, skip_corrupt_data_blocks_unsafe}));
    }
    return Status::OK();
  }

  std::unique_ptr<MiniCluster> cluster_;
  std::unique_ptr<client::YBClient> client_;
  server::ClockPtr clock_{new server::HybridClock()};
  std::unique_ptr<client::TransactionManager> transaction_manager_;
  std::unique_ptr<client::TransactionPool> transaction_pool_;
  std::unique_ptr<TestYcqlWorkload> workload_;
  TableId workload_table_id_;
  std::shared_ptr<RocksDbListener> rocksdb_listener_;
};

void CompactionTest::TestCompactionAfterTruncate() {
  // Write some data before truncate to make sure truncate wouldn't be noop.
  ASSERT_OK(WriteAtLeast(kMemStoreSize * kDefaultNumTablets * 1.2));

  const auto table_info = ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));
  ASSERT_OK(workload_->client().TruncateTable(table_info->id(), true /* wait */));

  rocksdb_listener_->Reset();
  // Write enough to trigger compactions.
  ASSERT_OK(WriteAtLeastFilesPerDb(FLAGS_rocksdb_level0_file_num_compaction_trigger + 1));

  auto dbs = GetAllRocksDbs(cluster_.get());
  ASSERT_OK(LoggedWaitFor(
      [&dbs] {
        for (auto* db : dbs) {
          if (db->GetLiveFilesMetaData().size() >
              implicit_cast<size_t>(FLAGS_rocksdb_level0_file_num_compaction_trigger)) {
            return false;
          }
        }
        return true;
      },
      60s, "Waiting until we have number of SST files not higher than threshold ...", kWaitDelay));
}

void CompactionTest::TestCompactionWithoutFrontiers(
    const size_t num_without_frontiers,
    const size_t num_with_frontiers,
    const bool trigger_manual_compaction) {
  // Write a number of files without frontiers
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_adding_user_frontier_to_sst) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_getting_user_frontier_from_mem_table) = true;
  SetupWorkload(IsolationLevel::SNAPSHOT_ISOLATION);
  ASSERT_OK(WriteAtLeastFilesPerDb(num_without_frontiers));
  // If requested, write a number of files with frontiers second.
  if (num_with_frontiers > 0) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_adding_user_frontier_to_sst) = false;
    rocksdb_listener_->Reset();
    ASSERT_OK(WriteAtLeastFilesPerDb(num_with_frontiers));
  }

  // Trigger manual compaction if requested.
  if (trigger_manual_compaction) {
    ASSERT_OK(ExecuteManualCompaction());
  }
  // Wait for the compaction.
  auto dbs = GetAllRocksDbs(cluster_.get());
  ASSERT_OK(LoggedWaitFor(
      [&dbs, num_without_frontiers, num_with_frontiers] {
        for (auto* db : dbs) {
          if (db->GetLiveFilesMetaData().size() >= num_without_frontiers + num_with_frontiers) {
            return false;
          }
        }
        return true;
      },
      60s, "Waiting until we see fewer SST files than were written initially ...", kWaitDelay));
}

bool CompactionTest::CheckEachDbHasExactlyNumFiles(size_t num_files) {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    if (db->GetCurrentVersionNumSSTFiles() != num_files) {
      return false;
    }
  }
  return true;
}

bool CompactionTest::CheckEachDbHasAtLeastNumFiles(size_t num_files) {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    if (db->GetCurrentVersionNumSSTFiles() < num_files) {
      return false;
    }
  }
  return true;
}

TEST_F(CompactionTest, CompactionAfterTruncate) {
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  TestCompactionAfterTruncate();
}

TEST_F(CompactionTest, CompactionAfterTruncateTransactional) {
  SetupWorkload(IsolationLevel::SNAPSHOT_ISOLATION);
  TestCompactionAfterTruncate();
}

TEST_F(CompactionTest, AutomaticCompactionWithoutAnyUserFrontiers) {
  constexpr int files_without_frontiers = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger)
      = files_without_frontiers;
  // Create all SST files without user frontiers.
  TestCompactionWithoutFrontiers(files_without_frontiers, 0, false);
}

TEST_F(CompactionTest, AutomaticCompactionWithSomeUserFrontiers) {
  constexpr int files_without_frontiers = 1;
  constexpr int files_with_frontiers = 4;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger)
      = files_without_frontiers + files_with_frontiers;
  // Create only one SST file without user frontiers.
  TestCompactionWithoutFrontiers(files_without_frontiers, files_with_frontiers, false);
}

TEST_F(CompactionTest, ManualCompactionWithoutAnyUserFrontiers) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  // Create all SST files without user frontiers.
  TestCompactionWithoutFrontiers(5, 0, true);
}

TEST_F(CompactionTest, ManualCompactionWithSomeUserFrontiers) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  // Create only one SST file without user frontiers.
  TestCompactionWithoutFrontiers(1, 5, true);
}

TEST_F(CompactionTest, ManualCompactionProducesOneFilePerDb) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(10));

  ASSERT_OK(ExecuteManualCompaction());

  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    ASSERT_EQ(1, db->GetCurrentVersionNumSSTFiles());
  }
}

void CompactionTest::TestCompactionTaskMetrics(const int num_files, bool manual_compaction) {
  constexpr auto kNumTablets = 7;

  // Create and instantiate metric entity.
  METRIC_DEFINE_entity(test_entity);
  yb::MetricRegistry registry;
  auto entity = METRIC_ENTITY_test_entity.Instantiate(&registry, "task metrics");

  // Create task metrics for queued, paused, and active tasks.
  ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(test_entity);

  auto priority_thread_pool_metrics =
      std::make_shared<rocksdb::RocksDBPriorityThreadPoolMetrics>(
          ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(entity));

  // Set the priority thread pool metrics for each tserver.
  for (int i = 0 ; i < NumTabletServers(); i++) {
    cluster_->GetTabletManager(i)->TEST_tablet_options()->priority_thread_pool_metrics =
        priority_thread_pool_metrics;
  }

  const auto& active = manual_compaction
      ? priority_thread_pool_metrics->active.full
      : priority_thread_pool_metrics->active.background;
  const auto& nonactive = manual_compaction
      ? priority_thread_pool_metrics->nonactive.full
      : priority_thread_pool_metrics->nonactive.background;

  // Check counters pre-compaction. All should be zero.
  for (const auto& state_metrics : {active, nonactive}) {
    ASSERT_EQ(state_metrics.compaction_tasks_added_->value(), 0);
    ASSERT_EQ(state_metrics.compaction_tasks_removed_->value(), 0);
    ASSERT_EQ(state_metrics.compaction_input_files_added_->value(), 0);
    ASSERT_EQ(state_metrics.compaction_input_files_removed_->value(), 0);
    ASSERT_EQ(state_metrics.compaction_input_bytes_added_->value(), 0);
    ASSERT_EQ(state_metrics.compaction_input_bytes_removed_->value(), 0);
  }

  // Compact, then verify metrics match the original files and sizes.
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL, kNumTablets);
  auto dbs = GetAllRocksDbs(cluster_.get());

  if (manual_compaction) {
    ASSERT_OK(WriteAtLeastFilesPerDb(num_files));
    ASSERT_OK(ExecuteManualCompaction());
  } else {
    // Disable auto compactions in order to run them later at slow rate and have some nonactive
    // compaction tasks for testing nonactive metrics.
    for (auto* db : dbs) {
      ASSERT_OK(db->SetOptions({{"disable_auto_compactions", "true"}}));
    }

    ASSERT_OK(WriteAtLeastFilesPerDb(num_files));

    const auto original_compact_flush_rate_bytes_per_sec =
        FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec;
    const MonoDelta kWaitForNonActiveCompactions = 10s;

    SetCompactFlushRateLimitBytesPerSec(
        cluster_.get(), dbs.front()->GetCurrentVersionSstFilesSize() /
                            (kWaitForNonActiveCompactions.ToSeconds() * 1.5));

    for (auto* db : dbs) {
      ASSERT_OK(db->EnableAutoCompaction({db->DefaultColumnFamily()}));
    }

    const size_t num_nonactive_compactions_expected =
        kNumTablets > FLAGS_priority_thread_pool_size
            ? kNumTablets - FLAGS_priority_thread_pool_size
            : 0;

    ASSERT_OK(LoggedWaitFor(
        [nonactive, num_nonactive_compactions_expected] {
          return nonactive.compaction_tasks_added_->value() >= num_nonactive_compactions_expected;
        },
        kWaitForNonActiveCompactions,
        Format("Waiting for $0 non active compactions", num_nonactive_compactions_expected)));

    ASSERT_EQ(nonactive.compaction_tasks_added_->value(), num_nonactive_compactions_expected);

    SetCompactFlushRateLimitBytesPerSec(cluster_.get(), original_compact_flush_rate_bytes_per_sec);
  }

  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  // Wait until the metrics match the number of completed compactions.
  ASSERT_OK(LoggedWaitFor(
      [this, dbs, active] {
          uint64_t num_completed_compactions = 0;
          for (auto* db : dbs) {
            num_completed_compactions += rocksdb_listener_->GetNumCompactionsCompleted(db);
          }
          return (num_completed_compactions == active.compaction_tasks_removed_->value() &&
              active.compaction_tasks_added_->value() == active.compaction_tasks_removed_->value());
        }, 60s,
        "Waiting until all compactions are completed and metrics match with compaction listener...",
      kWaitDelay * kTimeMultiplier));

  size_t num_completed_compactions = 0;
  uint64_t input_files_compactions = 0;
  uint64_t input_bytes_compactions = 0;
  for (auto* db : dbs) {
    num_completed_compactions += rocksdb_listener_->GetNumCompactionsCompleted(db);
    input_files_compactions += rocksdb_listener_->GetInputFilesInCompactionsCompleted(db);
    input_bytes_compactions += rocksdb_listener_->GetInputBytesInCompactionsCompleted(db);
  }

  // We expect at least one compaction per database.
  ASSERT_GE(num_completed_compactions, dbs.size());
  ASSERT_GT(input_files_compactions, 0);
  ASSERT_GT(input_bytes_compactions, 0);

  // The total number of compactions should match the value recorded by the listener.
  ASSERT_EQ(active.compaction_tasks_added_->value(), num_completed_compactions);
  ASSERT_EQ(active.compaction_input_files_added_->value(), input_files_compactions);
  ASSERT_EQ(active.compaction_input_bytes_added_->value(), input_bytes_compactions);

  // All added/removed metrics should be identical since the compaction has finished.
  for (const auto& state_metrics : {active, nonactive}) {
    ASSERT_EQ(state_metrics.compaction_tasks_added_->value(),
      state_metrics.compaction_tasks_removed_->value());
    ASSERT_EQ(state_metrics.compaction_input_files_added_->value(),
      state_metrics.compaction_input_files_removed_->value());
    ASSERT_EQ(state_metrics.compaction_input_bytes_added_->value(),
      state_metrics.compaction_input_bytes_removed_->value());
  }
}

TEST_F(CompactionTest, BackgroundCompactionTaskMetrics) {
  const int kNumFilesTriggerCompaction = 5;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      kNumFilesTriggerCompaction;

  TestCompactionTaskMetrics(kNumFilesTriggerCompaction, /* manual_compaction */ false);
}

TEST_F(CompactionTest, ManualCompactionTaskMetrics) {
  // Disable automatic compactions
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;

  TestCompactionTaskMetrics(/* num_files */ 5, /* manual_compaction */ true);
}

// Test uses sync points and can only run in debug mode.
#ifndef NDEBUG
TEST_F(CompactionTest, FilesOverMaxSizeWithTableTTLDoNotGetAutoCompacted) {
  yb::SyncPoint::GetInstance()->LoadDependency(
      {{.predecessor = "UniversalCompactionPicker::PickCompaction:SkippingCompaction",
        .successor = "CompactionTest::FilesOverMaxSizeDoNotGetAutoCompacted:WaitNoCompaction"}});
  yb::SyncPoint::GetInstance()->EnableProcessing();

  const int kNumFilesToWrite = 10;
  // Auto compaction will be triggered once 10 files are written.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = kNumFilesToWrite;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 10_KB;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Change the table to have a default time to live.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000));
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));

  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  TEST_SYNC_POINT("CompactionTest::FilesOverMaxSizeDoNotGetAutoCompacted:WaitNoCompaction");

  for (auto* db : dbs) {
    ASSERT_GE(db->GetCurrentVersionNumSSTFiles(), kNumFilesToWrite);
  }

  yb::SyncPoint::GetInstance()->DisableProcessing();
  yb::SyncPoint::GetInstance()->ClearTrace();
}
#endif // NDEBUG

TEST_F(CompactionTest, FilesOverMaxSizeWithTableTTLStillGetManualCompacted) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 10_KB;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Change the table to have a default time to live.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000));
  ASSERT_OK(WriteAtLeastFilesPerDb(10));

  ASSERT_OK(ExecuteManualCompaction());
  ASSERT_OK(WaitForNumCompactionsPerDb(1));

  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    ASSERT_EQ(db->GetCurrentVersionNumSSTFiles(), 1);
  }
}

TEST_F(CompactionTest, YB_DISABLE_TEST_ON_MACOS(MaxFileSizeIgnoredIfNoTableTTL)) {
  const int kNumFilesToWrite = 10;
  // Auto compactions will be triggered every kNumFilesToWrite files written.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = kNumFilesToWrite;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 10_KB;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  ASSERT_OK(WaitForNumCompactionsPerDb(1));

  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    ASSERT_LT(db->GetCurrentVersionNumSSTFiles(), kNumFilesToWrite);
  }
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/26014.
// In case of flakiness on MAC build consider using of YB_DISABLE_TEST_ON_MACOS.
TEST_F(CompactionTest, MaxFileSizeIgnoredIfValueTTLHigherThanTableTTL) {
  constexpr int kValueLargeTTLSec = 5 * 24 * 3600;
  const int kNumFilesToWrite = 10;

  // Auto compactions will be triggered every kNumFilesToWrite files written.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = kNumFilesToWrite;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 10_KB;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  workload_->set_ttl(kValueLargeTTLSec);

  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000));
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  ASSERT_OK(WaitForNumCompactionsPerDb(1));

  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    const auto num_files = db->GetCurrentVersionNumSSTFiles();
    LOG(INFO) << "Num files after compaction: " << num_files;
    ASSERT_LT(num_files, kNumFilesToWrite);
  }
}

TEST_F(CompactionTest, UpdateLastFullCompactionTimeForTableWithoutWrites) {
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  ASSERT_OK(ExecuteManualCompaction());
  const auto table_info = FindTable(cluster_.get(), workload_->table_name());
  ASSERT_OK(table_info);

  for (int i = 0; i < NumTabletServers(); ++i) {
    auto ts_tablet_manager = cluster_->GetTabletManager(i);

    for (const auto& peer : ts_tablet_manager->GetTabletPeers()) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (tablet && peer->tablet_metadata()->table_id() == (*table_info)->id()) {
        ASSERT_NE(tablet->metadata()->last_full_compaction_time(), 0);
      }
    }
  }
}

namespace {
  // Make the queue size twice as big as the number of tablets so that by default, we will
  // not overfill the queue.
  constexpr auto kQueueSize = kDefaultNumTablets * 2;
  constexpr auto kPoolMaxThreads = 1;
}  // namespace

class ScheduledFullCompactionsTest : public CompactionTest {
 public:
  void SetUp() override {
    // Before cluster setup, set the full compaction queue size to be greater than
    // the number of tablets.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_full_compaction_pool_max_queue_size) = kQueueSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_full_compaction_pool_max_threads)
        = kPoolMaxThreads;
    // Set the check interval to 0, to ensure there is no conflict with the background
    // full compaction manager task. Check interval will be manually set to 1 for the
    // stats window tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_check_interval_sec) = 0;

    CompactionTest::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_TEST_disable_adding_last_compaction_to_tablet_metadata) = false;
    // Disable background compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;

    // Set the window size to 1 second. Check interval will be manually set to 1 second
    // in relevant tests (i.e. 1 interval expected).
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_stat_window_seconds) = 1;

    // History retention set to 0 for quicker compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_percent_obsolete) = kPercentObsoleteThreshold;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_min_obsolete_keys_found) = 10000;
  }

 protected:
  Status WaitForTotalNumCompactions(int num_compactions);

  // Used to verify the metadata and next compaction times for all tablets in all tablet
  // managers. Then runs the ScheduleFullCompactions() function in FullCompactionManager.
  // Assumes jitter_factor is 0. expected_last_compact_lower_bound defaults to 0,
  // meaning that the expected next compaction time is now. If any non-special value,
  // this serves as the lower bound for when the last compaction time should be.
  bool CheckNextFullCompactionTimes(
      MonoDelta compaction_frequency,
      HybridTime expected_last_compact_lower_bound = HybridTime(tablet::kNoLastFullCompactionTime));

  // Runs a workload that writes rows and then reads them, then deletes some rows and does it again.
  // A deletion function is provided as a paramenter, as well as the number of SST files expected
  // after the final compaction.
  void AutoCompactionBasedOnStats(
      const std::function<Status()>& delete_fn, const int end_files_expected);

  bool sequential_write() override {
    return true;
  }

  const int kPercentObsoleteThreshold = 50;
};

bool ScheduledFullCompactionsTest::CheckNextFullCompactionTimes(
    MonoDelta compaction_frequency,
    HybridTime expected_last_compact_lower_bound) {
  HybridTime now = clock_->Now();
  for (int i = 0 ; i < NumTabletServers(); i++) {
    auto ts_tablet_manager = cluster_->GetTabletManager(i);
    auto compact_manager = ts_tablet_manager->full_compaction_manager();
    for (auto peer : ts_tablet_manager->GetTabletPeers()) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (!tablet || !tablet->IsEligibleForFullCompaction()) {
        continue;
      }
      // Last full compaction time should be invalid (never compacted).
      auto last_compact_time = HybridTime(tablet->metadata()->last_full_compaction_time());
      auto next_compact_time = compact_manager->TEST_DetermineNextCompactTime(peer, now);
      if (expected_last_compact_lower_bound.is_special()) {
        // If the expected_last_compact_lower_bound is a special value, then it's expected that the
        // last compaction time is 0 and we should compact now.
        if (!last_compact_time.is_special() ||
            next_compact_time != now) {
          LOG(INFO) << "Expected no last compaction metadata, but got "
              << last_compact_time.ToUint64() << " - expected next_compaction_time of "
              << now.ToUint64() << ", got " << next_compact_time.ToUint64();
          return false;
        }
      } else {
        // If the expected_last_compact_lower_bound is any other value, then it's expected that the
        // last compaction time should be greater than or equal to it and the next compaction time
        // should be compaction_frequency from then.
        auto expected_next_compact_time = last_compact_time.AddDelta(compaction_frequency);
        if (last_compact_time < expected_last_compact_lower_bound ||
            next_compact_time != expected_next_compact_time) {
          LOG(INFO) << "Expected last compaction time to be greater than "
              << expected_last_compact_lower_bound.ToUint64() << ", but got "
              << last_compact_time.ToUint64() << " - expected next_compaction time of"
              << expected_next_compact_time.ToUint64() << ", got "
              << next_compact_time.ToUint64();
          return false;
        }
      }
    }
  }
  return true;
}

Status ScheduledFullCompactionsTest::WaitForTotalNumCompactions(int num_compactions) {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  RETURN_NOT_OK(LoggedWaitFor(
    [this, &dbs, num_compactions] {
        int num_compactions_completed = 0;
        for (auto* db : dbs) {
          if (rocksdb_listener_->GetNumCompactionsCompleted(db) > 0) {
            num_compactions_completed++;
          }
        }
        return num_compactions_completed == num_compactions;
      }, 60s, Format("Waiting until exactly $0 total compactions finish...", num_compactions),
    kWaitDelay * kTimeMultiplier));
  return Status::OK();
}

TEST_F(ScheduledFullCompactionsTest, ScheduleWhenExpected) {
  const int kNumFilesToWrite = 10;
  const int32_t kCompactionFrequencyHours = 24;
  const MonoDelta kCompactionFrequency = MonoDelta::FromHours(kCompactionFrequencyHours);
  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    ASSERT_GE(db->GetCurrentVersionNumSSTFiles(), kNumFilesToWrite);
  }
  // Change compaction frequency to enable the FullCompactionManager.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) =
      kCompactionFrequencyHours;

  HybridTime before_first_check = clock_->Now();
  ASSERT_TRUE(CheckNextFullCompactionTimes(kCompactionFrequency));
  compact_manager->ScheduleFullCompactions();

  // Wait until all compactions have finished, then verify they completed.
  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  for (auto* db : dbs) {
    ASSERT_EQ(db->GetCurrentVersionNumSSTFiles(), 1);
  }

  ASSERT_TRUE(CheckNextFullCompactionTimes(
      kCompactionFrequency, before_first_check));
  compact_manager->ScheduleFullCompactions();

  // Manually set the last compaction time for one tablet, and verify that only it gets scheduled.
  // Pick an arbitrary DB to assign an earlier compaction time.
  rocksdb::DB* db_with_early_compaction = dbs[0];
  bool found_tablet_peer = false;
  for (auto peer : ts_tablet_manager->GetTabletPeers()) {
    auto tablet = peer->shared_tablet_maybe_null();
    // Find the tablet peer with the db for early compaction (matching pointers)
    if (tablet && tablet->regular_db() == db_with_early_compaction) {
      auto metadata = tablet->metadata();
      // Previous compaction time set to 30 days prior to now.
      auto now = clock_->Now();
      metadata->set_last_full_compaction_time(
          now.AddDelta(kCompactionFrequency * -1).ToUint64());
      ASSERT_OK(metadata->Flush());
      // Next compaction time should be "now" after the reset
      auto next_compact_time =
          compact_manager->TEST_DetermineNextCompactTime(peer, now);
      ASSERT_GE(next_compact_time, now);
      found_tablet_peer = true;
      break;
    }
  }
  ASSERT_TRUE(found_tablet_peer);

  // Write more files, then schedule full compactions. Only the peer with the reset metadata
  // should be scheduled.
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  compact_manager->ScheduleFullCompactions();
  ASSERT_OK(LoggedWaitFor(
      [this, &dbs] {
          for (auto* db : dbs) {
            if (rocksdb_listener_->GetNumCompactionsCompleted(db) == 1) {
              return true;
            }
          }
          return false;
        }, 60s, "Waiting until at least one compaction finishes on one rocksdb...",
      kWaitDelay * kTimeMultiplier));

  // Verify that exactly one compaction was scheduled.
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 1);
  for (auto* db : dbs) {
    auto num_ssts = db->GetCurrentVersionNumSSTFiles();
    if (db == db_with_early_compaction) {
      // The tablet with an early compaction time should only have 1 file.
      ASSERT_EQ(num_ssts, 1);
    } else {
      // All other tablets should have at least 10 files (number originally written).
      ASSERT_GE(num_ssts, kNumFilesToWrite);
    }
  }
}

TEST_F(ScheduledFullCompactionsTest, WillWaitForPreviousToFinishBeforeScheduling) {
  HybridTime now = clock_->Now();
  const int kNumFilesToWrite = 10;
  const int kCompactionFrequencySecs = 1;

  // Disable background compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));

  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();
  auto ScheduleFullCompactionsEveryNSeconds =
      [compact_manager](const int seconds){
        compact_manager->TEST_DoScheduleFullCompactionsWithManualValues(
          MonoDelta::FromSeconds(seconds), 0);
      };

  // Schedule full compactions using a manual number of seconds as the compaction
  // frequency.
  ScheduleFullCompactionsEveryNSeconds(kCompactionFrequencySecs);

  // Compactions should get scheduled but NOT executed yet.
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), kDefaultNumTablets);
  SleepFor(MonoDelta::FromSeconds(kCompactionFrequencySecs));
  ASSERT_TRUE(CheckEachDbHasAtLeastNumFiles(kNumFilesToWrite));
  now = clock_->Now();
  for (auto peer : ts_tablet_manager->GetTabletPeers()) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    const auto last_compaction_time = HybridTime(tablet->metadata()->last_full_compaction_time());
    ASSERT_TRUE(last_compaction_time.is_special());
    ASSERT_EQ(compact_manager->TEST_DetermineNextCompactTime(peer, now), now);
  }

  // Try to schedule compactions again, with the originals still hanging.
  // No new compactions should be scheduled.
  ScheduleFullCompactionsEveryNSeconds(kCompactionFrequencySecs);
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);

  // Turn off pause before compactions, wait for compactions to finish, and try again.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  ASSERT_TRUE(CheckEachDbHasExactlyNumFiles(1));

  // Write more files, then wait for the compaction frequency amount of time.
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  SleepFor(MonoDelta::FromSeconds(kCompactionFrequencySecs));

  // Try to schedule compactions again; they should succeed.
  ScheduleFullCompactionsEveryNSeconds(kCompactionFrequencySecs);
  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  ASSERT_TRUE(CheckEachDbHasExactlyNumFiles(1));
}

TEST_F(ScheduledFullCompactionsTest, OlderTabletsWillStillScheduleAndCreateMetadata) {
  const int kNumFilesToWrite = 10;
  const int kCompactionFrequencyHours = 24;
  const MonoDelta kCompactionFrequency = MonoDelta::FromHours(kCompactionFrequencyHours);
  // Prevent compaction tablet metadata from being written to mimic older tablets.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_disable_adding_last_compaction_to_tablet_metadata) = true;

  // Write some files and execute a full compaction.
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  ASSERT_OK(ExecuteManualCompaction());
  ASSERT_OK(WaitForNumCompactionsPerDb(1));

  // Re-activate metadata writing so it will be written on the next compaction.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_disable_adding_last_compaction_to_tablet_metadata) = false;
  // Verify that the metadata hasn't been updated, but we still schedule a
  // full compaction for now.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) =
      kCompactionFrequencyHours;
  HybridTime before_first_check = clock_->Now();
  ASSERT_TRUE(CheckNextFullCompactionTimes(kCompactionFrequency));

  rocksdb_listener_->Reset();
  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();
  compact_manager->ScheduleFullCompactions();
  ASSERT_OK(WaitForNumCompactionsPerDb(1));

  // Check that we now have useable metadata (even though the original tablets had none),
  // and that we schedule a compaction for the future.
  ASSERT_TRUE(CheckNextFullCompactionTimes(kCompactionFrequency, before_first_check));
  compact_manager->ScheduleFullCompactions();
}

TEST_F(ScheduledFullCompactionsTest, OldestTabletsAreScheduledFirst) {
  const int kNumFilesToWrite = 10;
  // Turn on the compaction schedule feature.
  const int kCompactionFrequencyHours = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) =
      kCompactionFrequencyHours;

  // Create a table with double the number of tablets as queue size + threads in pool.
  const auto kThreadsPlusQueue = kPoolMaxThreads + kQueueSize;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL, kThreadsPlusQueue * 2);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));

  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();

  // Modify the metadata for each tablet such that all tablets are eligible for compaction,
  // but each has a different last_full_compaction_time.
  // Half of tablets are given a significantly earlier last_full_compaction_time.
  std::vector<tablet::TabletPeerPtr> to_be_compacted;
  std::vector<tablet::TabletPeerPtr> not_to_be_compacted;
  auto now = clock_->Now();
  int i = 0;
  for (auto& peer : ts_tablet_manager->GetTabletPeers()) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet || !tablet->IsEligibleForFullCompaction()) {
      continue;
    }
    auto metadata = tablet->metadata();
    // Since we don't have queue size in thread pool, all compactions are getting scheduled.
    if (true /* i % 2 == 0 */) {
      // Set half of the last compaction times to a week ago (adjusted slightly).
      metadata->set_last_full_compaction_time(
        now.AddDelta(MonoDelta::FromDays(7) * -1)
            .AddDelta(MonoDelta::FromHours(i)).ToUint64());
      to_be_compacted.push_back(peer);
    } else {
      // The other half have compaction times of 2 days ago.
      metadata->set_last_full_compaction_time(
        now.AddDelta(MonoDelta::FromDays(2) * -1)
            .AddDelta(MonoDelta::FromHours(i)).ToUint64());
      not_to_be_compacted.push_back(peer);
    }
    ASSERT_OK(metadata->Flush());
    i++;
  }

  // Force a pause before full compaction to hold compactions in queue.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
  // ScheduleFullCompactions() should execute fine, but will have only scheduled
  // kThreadsPlusQueue compactions.
  compact_manager->ScheduleFullCompactions();
  // ASSERT_EQ(compact_manager->num_scheduled_last_execution(), kThreadsPlusQueue);

  // Try to manually schedule one of the compactions. Should fail.
  // ASSERT_NOK(
  //     ASSERT_RESULT(not_to_be_compacted[0]->shared_tablet())
  //     ->TriggerManualCompactionIfNeeded(rocksdb::CompactionReason::kScheduledFullCompaction));

  // Let the compactions finish.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
  // Let all of the threads finish.
  ASSERT_TRUE(ts_tablet_manager->full_compaction_pool()->WaitFor(60s));
  // ASSERT_OK(WaitForTotalNumCompactions(kThreadsPlusQueue));

  // Verify that the right tablets were compacted.
  for (auto& peer : to_be_compacted) {
    ASSERT_EQ(ASSERT_RESULT(peer->shared_tablet())->GetCurrentVersionNumSSTFiles(), 1);
  }
  for (auto& peer : not_to_be_compacted) {
    ASSERT_GE(
        ASSERT_RESULT(peer->shared_tablet())->GetCurrentVersionNumSSTFiles(), kNumFilesToWrite);
  }

  // Try scheduling compactions again. The rest should be scheduled (but not the
  // tablets that already compacted).
  rocksdb_listener_->Reset();
  compact_manager->ScheduleFullCompactions();
  // ASSERT_EQ(compact_manager->num_scheduled_last_execution(), kThreadsPlusQueue);
  ASSERT_TRUE(ts_tablet_manager->full_compaction_pool()->WaitFor(60s));
  // ASSERT_OK(WaitForTotalNumCompactions(kThreadsPlusQueue));

  for (auto& peer : not_to_be_compacted) {
    ASSERT_EQ(ASSERT_RESULT(peer->shared_tablet())->GetCurrentVersionNumSSTFiles(), 1);
  }
}

void ScheduledFullCompactionsTest::AutoCompactionBasedOnStats(
    const std::function<Status()>& delete_fn, const int end_files_expected) {
  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();
  // Manually set the check interval to 1 second without enabling the full compaction manager
  // thread (for easier test comparisons without worrying about thread timing).
  compact_manager->TEST_SetCheckIntervalSec(1);

  // ScheduleFullCompactions() will trigger statistics collection, and will only compact if all
  // compaction conditions are met.
  // Collect statistics at the beginning and between every step.
  compact_manager->ScheduleFullCompactions();
  const auto table_name = workload_->table_name();
  const auto table_id = ASSERT_RESULT(FindTable(cluster_.get(), table_name))->id();
  const auto tablet_ids = ListTabletIdsForTable(cluster_.get(), table_id);
  ASSERT_GE(tablet_ids.size(), 1);
  const auto tablet_id = *tablet_ids.begin();

  const int kNumFilesToWrite = 10;
  const int kNumRowsToRead = 100;
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  compact_manager->ScheduleFullCompactions();
  // Check total keys after writes, should not see any keys (none read).
  auto stats = compact_manager->TEST_CurrentWindowStats(tablet_id);
  ASSERT_EQ(stats.total, 0);

  // Read random rows and collect the stats again. Expect all rows read to still be live.
  ASSERT_OK(ReadAtLeastRowsOk(kNumRowsToRead));
  compact_manager->ScheduleFullCompactions();
  stats = compact_manager->TEST_CurrentWindowStats(tablet_id);
  ASSERT_EQ(stats.total, workload_->rows_read_ok());
  ASSERT_EQ(stats.obsolete_cutoff, 0);

  // Read rows, then recollect the stats. Expect none obsolete.
  ASSERT_OK(ReadAtLeastRowsOk(kNumRowsToRead));
  // No compactions should be scheduled.
  compact_manager->ScheduleFullCompactions();
  ASSERT_TRUE(CheckEachDbHasAtLeastNumFiles(kNumFilesToWrite));
  stats = compact_manager->TEST_CurrentWindowStats(tablet_id);
  ASSERT_GE(stats.total, kNumRowsToRead);
  ASSERT_EQ(stats.obsolete_cutoff, 0);

  // Execute the delete function to remove enough data that random reads will result in over
  // half of the data read to be removed.
  // Then read from the table again.
  ASSERT_OK(delete_fn());
  ASSERT_OK(ReadAtLeastRowsOk(kNumRowsToRead));

  // Check whether we should delete. We should NOT be able to compact here because we have not
  // seen enough keys.
  compact_manager->ScheduleFullCompactions();
  ASSERT_FALSE(compact_manager->ShouldCompactBasedOnStats(tablet_id));
  ASSERT_EQ(compact_manager->num_scheduled_last_execution(), 0);
  ASSERT_TRUE(CheckEachDbHasAtLeastNumFiles(kNumFilesToWrite));

  stats = compact_manager->TEST_CurrentWindowStats(tablet_id);
  ASSERT_GE(stats.total, kNumRowsToRead);
  ASSERT_EQ(stats.obsolete_cutoff,  workload_->rows_read_empty());
  ASSERT_GT(stats.obsolete_cutoff, stats.total * kPercentObsoleteThreshold / 100);

  // If we reduce the number of keys needed, then we should be able to compact.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_min_obsolete_keys_found) = 10;
  ASSERT_TRUE(compact_manager->ShouldCompactBasedOnStats(tablet_id));

  // Do another round of reads, then try to schedule compactions. This time, a compaction
  // should be scheduled.
  ASSERT_OK(ReadAtLeastRowsOk(kNumRowsToRead));
  rocksdb_listener_->Reset();
  compact_manager->ScheduleFullCompactions();
  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  ASSERT_TRUE(CheckEachDbHasExactlyNumFiles(end_files_expected));
}

TEST_F(ScheduledFullCompactionsTest, AutoCompactionsBasedOnStatsDelete) {
  // Setup the workload to only a single tablet (to simplify stats), and collect the stats.
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL, 1 /* num_tablets */);
  const auto delete_fn = [&]() -> Status {
      const auto num_rows = workload_->rows_inserted();
      const auto rows_to_delete = num_rows * 90 / 100;
      LOG(INFO) << num_rows << " rows written to single tablet. "
          << rows_to_delete << " rows will be deleted";
      // Delete a large number of the rows, starting with id = 0 and going sequentially.
      auto session = client_->NewSession(60s);
      client::TableHandle table;
      RETURN_NOT_OK(table.Open(workload_->table_name(), client_.get()));
      std::vector<client::YBOperationPtr> ops;
      for (int32_t i = 0; i < rows_to_delete; i++) {
        const auto op = table.NewWriteOp(session->arena(), QLWriteRequestPB::QL_STMT_DELETE);
        auto* const req = op->mutable_request();
        QLAddInt32HashValue(req, i);
        ops.push_back(op);
      }
      session->Apply(ops);
      return session->TEST_FlushAndGetOpsErrors().status;
  };

  AutoCompactionBasedOnStats(delete_fn, 1);
}

TEST_F(ScheduledFullCompactionsTest, AutoCompactionsBasedOnStatsTTL) {
  constexpr auto kTTLSec = 5;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL, 1 /* num_tablets */);
  workload_->set_ttl(kTTLSec);

  const auto delete_fn = [&]() -> Status {
    // Wait for TTL seconds to make all rows obsolete.
    LOG(INFO) << "Waiting " << kTTLSec << " seconds for rows to expire.";
    SleepFor(MonoDelta::FromSeconds(kTTLSec));
    return Status::OK();
  };

  // No files should be expected at the end of this scenario, since all data will have expired.
  AutoCompactionBasedOnStats(delete_fn, 0);
}

class CompactionTestWithTTL : public CompactionTest {
 protected:
  int TtlSec() override {
    return kTTLSec;
  }
  const int kTTLSec = 1;
};

TEST_F(CompactionTestWithTTL, YB_DISABLE_TEST_ON_MACOS(CompactionAfterExpiry)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 10;
  // Testing compaction without compaction file filtering for TTL expiration.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  auto dbs = GetAllRocksDbs(cluster_.get(), false);

  // Write enough to be short of triggering compactions.
  ASSERT_OK(WriteAtLeastFilesPerDb(0.8 * FLAGS_rocksdb_level0_file_num_compaction_trigger));
  size_t size_before_compaction = 0;
  for (auto* db : dbs) {
    size_before_compaction += db->GetCurrentVersionSstFilesUncompressedSize();
  }
  LOG(INFO) << "size_before_compaction is " << size_before_compaction;

  LOG(INFO) << "Sleeping";
  SleepFor(MonoDelta::FromSeconds(2 * kTTLSec));

  // Write enough to trigger compactions.
  ASSERT_OK(WriteAtLeastFilesPerDb(FLAGS_rocksdb_level0_file_num_compaction_trigger));

  ASSERT_OK(LoggedWaitFor(
      [&dbs] {
        for (auto* db : dbs) {
          if (db->GetLiveFilesMetaData().size() >
              implicit_cast<size_t>(FLAGS_rocksdb_level0_file_num_compaction_trigger)) {
            return false;
          }
        }
        return true;
      },
      60s, "Waiting until we have number of SST files not higher than threshold ...", kWaitDelay));

  // Assert that the data size is smaller now.
  size_t size_after_compaction = 0;
  for (auto* db : dbs) {
    size_after_compaction += db->GetCurrentVersionSstFilesUncompressedSize();
  }
  LOG(INFO) << "size_after_compaction is " << size_after_compaction;
  EXPECT_LT(size_after_compaction, size_before_compaction);

  SleepFor(MonoDelta::FromSeconds(2 * kTTLSec));

  ASSERT_OK(ExecuteManualCompaction());

  // Assert that the data size is all wiped up now.
  size_t size_after_manual_compaction = 0;
  uint64_t num_sst_files_filtered = 0;
  for (auto* db : dbs) {
    size_after_manual_compaction += db->GetCurrentVersionSstFilesUncompressedSize();
    auto stats = db->GetOptions().statistics;
    num_sst_files_filtered
        += stats->getTickerCount(rocksdb::COMPACTION_FILES_FILTERED);
  }
  LOG(INFO) << "size_after_manual_compaction is " << size_after_manual_compaction;
  EXPECT_EQ(size_after_manual_compaction, 0);
  EXPECT_EQ(num_sst_files_filtered, 0);
}

class CompactionTestWithFileExpiration : public CompactionTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_compact_check_interval_sec) = 0;
    CompactionTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_ignore_value_ttl) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = false;
  }
 protected:
  size_t GetTotalSizeOfDbs();
  uint64_t GetNumFilesInDbs();
  uint64_t CountFilteredSSTFiles();
  uint64_t CountUnfilteredSSTFiles();
  void LogSizeAndFilesInDbs(bool after_compaction);
  void WriteRecordsAllExpire();
  void AssertNoFilesExpired();
  void AssertAllFilesExpired();
  bool CheckAtLeastFileExpirationsPerDb(size_t num_expirations);
  int TableTtlSec() override {
    return kTableTTLSec;
  }
  static constexpr int kTableTTLSec = 1;
};

size_t CompactionTestWithFileExpiration::GetTotalSizeOfDbs() {
  size_t total_size_dbs = 0;
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    total_size_dbs += db->GetCurrentVersionSstFilesUncompressedSize();
  }
  return total_size_dbs;
}

uint64_t CompactionTestWithFileExpiration::GetNumFilesInDbs() {
  uint64_t total_files_dbs = 0;
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    total_files_dbs += db->GetCurrentVersionNumSSTFiles();
  }
  return total_files_dbs;
}

uint64_t CompactionTestWithFileExpiration::CountFilteredSSTFiles() {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  uint64_t num_sst_files_filtered = 0;
  for (auto* db : dbs) {
    auto stats = db->GetOptions().statistics;
    num_sst_files_filtered
        += stats->getTickerCount(rocksdb::COMPACTION_FILES_FILTERED);
  }
  LOG(INFO) << "Number of filtered SST files: " << num_sst_files_filtered;
  return num_sst_files_filtered;
}

uint64_t CompactionTestWithFileExpiration::CountUnfilteredSSTFiles() {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  uint64_t num_sst_files_unfiltered = 0;
  for (auto* db : dbs) {
    auto stats = db->GetOptions().statistics;
    num_sst_files_unfiltered
        += stats->getTickerCount(rocksdb::COMPACTION_FILES_NOT_FILTERED);
  }
  LOG(INFO) << "Number of unfiltered SST files: " << num_sst_files_unfiltered;
  return num_sst_files_unfiltered;
}

void CompactionTestWithFileExpiration::LogSizeAndFilesInDbs(bool after_compaction = false) {
  auto size_before_compaction = GetTotalSizeOfDbs();
  auto files_before_compaction = GetNumFilesInDbs();
  auto descriptor = after_compaction ? "after compaction" : "before compaction";
  LOG(INFO) << "Total size " << descriptor << ": " << size_before_compaction <<
      ", num files: " << files_before_compaction;
}

void CompactionTestWithFileExpiration::AssertAllFilesExpired() {
  auto size_after_manual_compaction = GetTotalSizeOfDbs();
  auto files_after_compaction = GetNumFilesInDbs();
  LOG(INFO) << "Total size after compaction: " << size_after_manual_compaction <<
      ", num files: " << files_after_compaction;
  EXPECT_EQ(size_after_manual_compaction, 0);
  EXPECT_EQ(files_after_compaction, 0);
  ASSERT_GT(CountFilteredSSTFiles(), 0);
}

void CompactionTestWithFileExpiration::AssertNoFilesExpired() {
  auto size_after_manual_compaction = GetTotalSizeOfDbs();
  auto files_after_compaction = GetNumFilesInDbs();
  LOG(INFO) << "Total size after compaction: " << size_after_manual_compaction <<
      ", num files: " << files_after_compaction;
  EXPECT_GT(size_after_manual_compaction, 0);
  EXPECT_GT(files_after_compaction, 0);
  ASSERT_EQ(CountFilteredSSTFiles(), 0);
}

bool CompactionTestWithFileExpiration::CheckAtLeastFileExpirationsPerDb(size_t num_expirations) {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto db : dbs) {
    auto stats = db->GetOptions().statistics;
    if (stats->getTickerCount(rocksdb::COMPACTION_FILES_FILTERED) < num_expirations) {
      return false;
    }
  }
  return true;
}

void CompactionTestWithFileExpiration::WriteRecordsAllExpire() {
  // Disable auto compactions to prevent any files from accidentally expiring early.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  auto size_before_compaction = GetTotalSizeOfDbs();
  auto files_before_compaction = GetNumFilesInDbs();
  LOG(INFO) << "Total size before compaction: " << size_before_compaction <<
      ", num files: " << files_before_compaction;

  LOG(INFO) << "Sleeping long enough to expire all data";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that the data size is all wiped up now.
  EXPECT_EQ(GetTotalSizeOfDbs(), 0);
  EXPECT_EQ(GetNumFilesInDbs(), 0);
}

TEST_F(CompactionTestWithFileExpiration, CompactionNoFileExpiration) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;
  WriteRecordsAllExpire();
  ASSERT_GT(CountUnfilteredSSTFiles(), 0);
  ASSERT_EQ(CountFilteredSSTFiles(), 0);
}

TEST_F(CompactionTestWithFileExpiration, FileExpirationAfterExpiry) {
  WriteRecordsAllExpire();
  ASSERT_GT(CountFilteredSSTFiles(), 0);
}

TEST_F(CompactionTestWithFileExpiration, ValueTTLOverridesTableTTL) {
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Set the value-level TTL to too high to expire.
  workload_->set_ttl(/* ttl_sec = */ 1000);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping long enough to expire all data if TTL were not increased";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that the data is not completely removed
  AssertNoFilesExpired();
}

TEST_F(CompactionTestWithFileExpiration, ValueTTLWillNotOverrideTableTTLWhenTableOnlyFlagSet) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_ignore_value_ttl) = true;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Set the value-level TTL to too high to expire.
  workload_->set_ttl(/* ttl_sec = */ 1000);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping long enough to expire all data (based on table-level TTL)";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that the data is completely removed (i.e. value-level TTL was ignored)
  AssertAllFilesExpired();
}

TEST_F(CompactionTestWithFileExpiration, ValueTTLWillOverrideTableTTLWhenFlagSet) {
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Change the table TTL to a large value that won't expire.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000000));
  // Set the value-level TTL that will expire.
  const auto kValueExpiryTimeSec = 1;
  workload_->set_ttl(kValueExpiryTimeSec);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));

  LOG(INFO) << "Sleeping long enough to expire all data (based on value-level TTL)";
  SleepFor(2s * kValueExpiryTimeSec);

  ASSERT_OK(ExecuteManualCompaction());
  // Add data will be deleted by compaction, but no files should expire after the
  // first compaction (protected by table TTL).
  EXPECT_EQ(GetTotalSizeOfDbs(), 0);
  EXPECT_EQ(GetNumFilesInDbs(), 0);
  ASSERT_EQ(CountFilteredSSTFiles(), 0);

  // Change the file_expiration_value_ttl_overrides_table_ttl flag and create more files.
  // Then, run another compaction and assert that all files have expired.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = true;
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  LogSizeAndFilesInDbs();
  LOG(INFO) << "Sleeping long enough to expire all data (based on value-level TTL)";
  SleepFor(MonoDelta::FromSeconds(2 * kValueExpiryTimeSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that the data is completely removed (i.e. table-level TTL was ignored)
  AssertAllFilesExpired();
}

TEST_F(CompactionTestWithFileExpiration, MixedExpiringAndNonExpiring) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  auto size_before_sleep = GetTotalSizeOfDbs();
  auto files_before_sleep = GetNumFilesInDbs();
  LOG(INFO) << "Total size of " << files_before_sleep <<
      " files that should expire: " << size_before_sleep;

  LOG(INFO) << "Sleeping long enough to expire all data";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  rocksdb_listener_->Reset();
  // Write a file and compact before it expires.
  ASSERT_OK(WriteAtLeastFilesPerDb(1));
  ASSERT_OK(ExecuteManualCompaction());
  // Assert that the data is not completely removed, but some files expired.
  size_t size_after_manual_compaction = GetTotalSizeOfDbs();
  uint64_t files_after_compaction = GetNumFilesInDbs();
  LOG(INFO) << "Total size of " << files_after_compaction << " files after compaction: "
      << size_after_manual_compaction;
  EXPECT_GT(size_after_manual_compaction, 0);
  EXPECT_LT(size_after_manual_compaction, size_before_sleep);
  EXPECT_GT(files_after_compaction, 0);
  EXPECT_LT(files_after_compaction, files_before_sleep);
  ASSERT_GT(CountFilteredSSTFiles(), 0);
}

TEST_F(CompactionTestWithFileExpiration, FileThatNeverExpires) {
  const int kNumFilesToWrite = 10;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping to expire files";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  // Set workload TTL to not expire.
  workload_->set_ttl(dockv::kResetTTL);
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(1));
  ASSERT_OK(ExecuteManualCompaction());

  auto filtered_sst_files = CountFilteredSSTFiles();
  ASSERT_GT(filtered_sst_files, 0);

  // Write 10 more files that would expire if not for the non-expiring file previously written.
  rocksdb_listener_->Reset();
  workload_->set_ttl(/* ttl_sec = */ -1);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));

  LOG(INFO) << "Sleeping to expire files";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));
  ASSERT_OK(ExecuteManualCompaction());

  // Assert that there is still some data remaining, and that we haven't filtered any new files.
  auto size_after_manual_compaction = GetTotalSizeOfDbs();
  auto files_after_compaction = GetNumFilesInDbs();
  LOG(INFO) << "Total size after compaction: " << size_after_manual_compaction <<
      ", num files: " << files_after_compaction;
  EXPECT_GT(size_after_manual_compaction, 0);
  EXPECT_GT(files_after_compaction, 0);
  ASSERT_EQ(filtered_sst_files, CountFilteredSSTFiles());
}

TEST_F(CompactionTestWithFileExpiration, ShouldNotExpireDueToHistoryRetention) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 1000000;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion)";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that there is still data after compaction, and no SST files have been filtered.
  AssertNoFilesExpired();
}

TEST_F(CompactionTestWithFileExpiration, TableTTLChangesWillChangeWhetherFilesExpire) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Change the table TTL to a large value that won't expire.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000000));

  ASSERT_OK(WriteAtLeastFilesPerDb(10));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping for the original table TTL seconds "
      << "(would expire if table TTL weren't changed)";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());

  // Assert the data hasn't changed, as we don't expect any expirations.
  AssertNoFilesExpired();

  // Change the table TTL back to a small value and execute a manual compaction.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), kTableTTLSec));

  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(10));

  LOG(INFO) << "Sleeping for the original table TTL seconds (will now expire rows)";
  SleepFor(MonoDelta::FromSeconds(2 * kTableTTLSec));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert data has expired.
  AssertAllFilesExpired();
}

TEST_F(CompactionTestWithFileExpiration, FewerFilesThanCompactionTriggerCanExpire) {
  // Set the number of files required to trigger compactions too high to initially trigger.
  const int kNumFilesTriggerCompaction = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger)
      = kNumFilesTriggerCompaction;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Write fewer files than are required to trigger an auto compaction.
  // These will be the only files that will be eligible for expiration.
  ASSERT_OK(WriteAtLeastFilesPerDb(1));
  LogSizeAndFilesInDbs();

  LOG(INFO) << "Sleeping for table TTL seconds";
  SleepFor(2s * kTableTTLSec);

  // Write enough files to trigger an automatic compaction.
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesTriggerCompaction));

  LogSizeAndFilesInDbs(true);
  // Verify that at least one file has expired per DB.
  ASSERT_TRUE(CheckAtLeastFileExpirationsPerDb(1));
}

// In the past, we have observed behavior of one disproportionately large file
// being unable to be directly deleted after it expires (and preventing subsequent
// files from also being deleted). This test verifies that large files will not
// prevent expiration.
TEST_F(CompactionTestWithFileExpiration, LargeFileDoesNotPreventExpiration) {
  const int kNumFilesTriggerCompaction = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger)
      = kNumFilesTriggerCompaction;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  // Write a disproportionately large amount of data, then compact into one file.
  ASSERT_OK(WriteAtLeast(1000_KB));
  ASSERT_OK(ExecuteManualCompaction());
  LogSizeAndFilesInDbs();
  ASSERT_TRUE(CheckEachDbHasExactlyNumFiles(1));
  const auto files_compacted_without_expiration = CountUnfilteredSSTFiles();

  // Add a flag to limit file size for compaction, then write several more files.
  // At this point, there will be one large ~1000_KB file, followed by several files
  // ~1_KB large. None of these files will be included in normal compactions
  // (but all are eligible for deletion).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 1_KB;
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesTriggerCompaction));

  LOG(INFO) << "Sleeping for table TTL seconds";
  SleepFor(2s * kTableTTLSec);

  // Write enough files to trigger an auto compaction, even though all are too large
  // to be considered for normal compaction.
  rocksdb_listener_->Reset();
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesTriggerCompaction));

  LogSizeAndFilesInDbs(true);
  // Check that 1 or more files have expired per database.
  ASSERT_TRUE(CheckAtLeastFileExpirationsPerDb(1));
  // Verify that no files have been compacted other than the manual compaction and deletions.
  ASSERT_EQ(CountUnfilteredSSTFiles(), files_compacted_without_expiration);
}

TEST_F(CompactionTestWithFileExpiration, TTLExpiryDisablesScheduledFullCompactions) {
  const HybridTime now = clock_->Now();
  const int kNumFilesToWrite = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = 1_KB;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));

  auto ts_tablet_manager = cluster_->GetTabletManager(0);
  auto compact_manager = ts_tablet_manager->full_compaction_manager();
  // Change the full compaction frequency hours to enable scheduled full compactions.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 30;
  for (auto peer : ts_tablet_manager->GetTabletPeers()) {
    // All tablets will either have a default TTL or have no data - either way, they are
    // not eligible for full compaction. However, the "next compact time" should be ASAP.
    auto next_compact_time = compact_manager->TEST_DetermineNextCompactTime(peer, now);
    ASSERT_EQ(next_compact_time, now);
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_FALSE(tablet->IsEligibleForFullCompaction());
  }
  // Wake the BG compaction thread, no compactions should be scheduled.
  compact_manager->ScheduleFullCompactions();
  ASSERT_TRUE(CheckEachDbHasAtLeastNumFiles(kNumFilesToWrite));

  // Remove table TTL and try again.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 0));
  compact_manager->ScheduleFullCompactions();
  ASSERT_OK(WaitForNumCompactionsPerDb(1));
  ASSERT_TRUE(CheckEachDbHasExactlyNumFiles(1));
}

class FileExpirationWithRF3 : public CompactionTestWithFileExpiration {
 public:
  void SetUp() override {
    CompactionTestWithFileExpiration::SetUp();
  }
 protected:
  bool AllFilesHaveTTLMetadata();
  void WaitUntilAllCommittedOpsApplied(const MonoDelta timeout);
  void ExpirationWhenReplicated(bool withValueTTL);
  int NumTabletServers() override {
    return 3;
  }
  int TtlSec() override {
    return kTTLSec;
  }
  const int kTTLSec = 1;
};

bool FileExpirationWithRF3::AllFilesHaveTTLMetadata() {
  auto dbs = GetAllRocksDbs(cluster_.get(), false);
  for (auto* db : dbs) {
    auto metas = db->GetLiveFilesMetaData();
    for (auto file : metas) {
      const docdb::ConsensusFrontier largest =
          down_cast<docdb::ConsensusFrontier&>(*file.largest.user_frontier);
      auto max_ttl_expiry = largest.max_value_level_ttl_expiration_time();
      // If value is not valid, then it wasn't initialized.
      // If value is kInitial, then the table-level TTL will be used (no value metadata).
      if (!max_ttl_expiry.is_valid() || max_ttl_expiry == HybridTime::kInitial) {
        return false;
      }
    }
  }
  return true;
}

void FileExpirationWithRF3::WaitUntilAllCommittedOpsApplied(const MonoDelta timeout) {
  const auto completion_deadline = MonoTime::Now() + timeout;
  for (auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto consensus_result = peer->GetConsensus();
    if (consensus_result) {
      auto* consensus = consensus_result->get();
      ASSERT_OK(Wait([consensus]() -> Result<bool> {
        return consensus->GetLastAppliedOpId() >= consensus->GetLastCommittedOpId();
      }, completion_deadline, "Waiting for all committed ops to be applied"));
    }
  }
}

void FileExpirationWithRF3::ExpirationWhenReplicated(bool withValueTTL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  if (withValueTTL) {
    // Change the table TTL to a large value that won't expire.
    ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000000));
  } else {
    // Set workload to not have value TTL.
    workload_->set_ttl(/* ttl_sec = */ -1);
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_file_expiration_value_ttl_overrides_table_ttl) = withValueTTL;

  ASSERT_OK(WriteAtLeastFilesPerDb(5));
  WaitUntilAllCommittedOpsApplied(15s);
  ASSERT_EQ(AllFilesHaveTTLMetadata(), withValueTTL);

  LOG(INFO) << "Sleeping to expire files according to value TTL";
  auto timeToSleep = 2 * (withValueTTL ? kTTLSec : kTableTTLSec);
  SleepFor(MonoDelta::FromSeconds(timeToSleep));

  ASSERT_OK(ExecuteManualCompaction());
  // Assert that all data has been deleted, and that we're filtering SST files.
  AssertAllFilesExpired();
}

TEST_F_EX(
    CompactionTestWithFileExpiration, ReplicatedMetadataCanExpireFile, FileExpirationWithRF3) {
  ExpirationWhenReplicated(true);
}

TEST_F_EX(
    CompactionTestWithFileExpiration, ReplicatedNoMetadataUsesTableTTL, FileExpirationWithRF3) {
  ExpirationWhenReplicated(false);
}

TEST_F(CompactionTest, CheckLastRequestTimePersistence) {
  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  auto table_info = ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));

  ASSERT_EQ(table_info->LockForRead()->pb.last_full_compaction_request_time(), 0);

  ASSERT_OK(ExecuteManualCompaction());
  const auto last_request_time = table_info->LockForRead()->pb.last_full_compaction_request_time();
  ASSERT_NE(last_request_time, 0);

  ASSERT_OK(cluster_->RestartSync());
  table_info = ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));
  ASSERT_EQ(table_info->LockForRead()->pb.last_full_compaction_request_time(), last_request_time);

  SleepFor(MonoDelta::FromSeconds(1));
  ASSERT_OK(ExecuteManualCompaction());
  ASSERT_GT(table_info->LockForRead()->pb.last_full_compaction_request_time(), last_request_time);
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/27426. Refer to D44394 for the description.
TEST_F(CompactionTest, BackgroundCompactionDuringPostSplitCompaction) {
  constexpr size_t kNumTablets = 1;
  constexpr size_t kNumFiles = 9;
  constexpr size_t kTrigger = kNumFiles - 2;
  constexpr uint64_t kSstFileSize = 500_KB;
  constexpr uint64_t kThreshold = kSstFileSize * 0.80;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ondisk_compression) = false;

  // Configuring flags to guarantee a background compaction will kick in between post split
  // compaction iterations.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = kSstFileSize;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = kThreshold;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = kTrigger;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_post_split_compaction_input_size_threshold_bytes) = kThreshold;

  // Configuring flags to guarantee background compaction picks SST files at the end of post split
  // compaction and keeps them locked till the compaction is finished.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_determine_compaction_input_at_start) = false;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_rocksdb_allow_multiple_pending_compactions_for_priority_thread_pool) = true;

  // Sanity checks for minimal requirements.
  ASSERT_GT(ANNOTATE_UNPROTECTED_READ(FLAGS_full_compaction_pool_max_threads), 0);
  ASSERT_GT(ANNOTATE_UNPROTECTED_READ(FLAGS_priority_thread_pool_size), 1);

  // Helpers to extract files information.
  auto files_ids = [](auto&& files) {
    return AsString(files, [](auto&& file) { return file.name_id; });
  };
  auto max_file_id = [](auto&& files) {
    return std::ranges::max_element(files, {}, &rocksdb::LiveFileMetaData::name_id)->name_id;
  };
  auto min_file_id = [](auto&& files) {
    return std::ranges::min_element(files, {}, &rocksdb::LiveFileMetaData::name_id)->name_id;
  };

  // Additional RocksDB listener to guarantee compaction flow.
  struct DBListener : public rocksdb::EventListener {
    bool background_compaction_in_progress = false;
    size_t num_post_split_iterations = 0;
    std::mutex mutex;
    std::condition_variable_any compaction_started_cv;

    void OnCompactionStarted() override {
      UniqueLock lock(mutex);

      // Background compaction will be always
      if (num_post_split_iterations == kTrigger && !background_compaction_in_progress) {
        LOG(INFO) << "Background compaction started";
        background_compaction_in_progress = true;

        // Wait for the next post split compaction iteration got triggered or exit on timeout.
        compaction_started_cv.wait_for(
            lock, std::chrono::seconds(30),
            [this] { return num_post_split_iterations != kTrigger; });
      } else {
        ++num_post_split_iterations;
        compaction_started_cv.notify_all();
      }
    }

    void OnCompactionCompleted(rocksdb::DB* db, const rocksdb::CompactionJobInfo& info) override {
      LOG(INFO) << "Compaction completed, reason: " << info.compaction_reason;

      std::lock_guard lock(mutex);
      if (info.is_no_op_compaction) {
        // Sanity check, the only no-op compaction is the post split compaction final iteration.
        ASSERT_EQ(info.compaction_reason, rocksdb::CompactionReason::kPostSplitCompaction);

        LOG(INFO) << "Number of post split compaction iterations: " << num_post_split_iterations;
        num_post_split_iterations = 0; // Resetting to track compactions for the next child.

        // This no op post split compaction iteration happens in any case, let's unblock
        // background compaction to complete it.
        compaction_started_cv.notify_all();
      } else if (info.compaction_reason != rocksdb::CompactionReason::kPostSplitCompaction) {
        background_compaction_in_progress = false;
        EXPECT_EQ(info.compaction_reason, rocksdb::CompactionReason::kUniversalSizeAmplification);
        LOG(INFO) << "Background compaction done";
      }
    }
  };
  auto listener = std::make_shared<DBListener>();
  AddRocksDBListener(listener);

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL, kNumTablets);

  // Change the table to have a default time to live. This is required for the easiest reproing,
  // but the issue may happen even without default TTL.
  ASSERT_OK(ChangeTableTTL(workload_->table_name(), /* ttl_sec = */ 1000));
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFiles));

  // Flush mem tables to have the predictable number of SST files.
  const auto table_info = ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));
  ASSERT_OK(workload_->client().FlushTables({table_info->id()}, MonoDelta::FromMinutes(1)));

  // Remember parent files before split.
  auto dbs = GetAllRocksDbs(cluster_.get(), /* include_intents = */ false);
  ASSERT_EQ(dbs.size(), 1);

  uint64_t parent_max_file_id = 0;
  {
    const auto files = dbs.front()->GetLiveFilesMetaData();
    parent_max_file_id = max_file_id(files);
    LOG(INFO) << "Parent files: " << files_ids(files);
  }

  // Trigger manual tablet split.
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  ASSERT_EQ(peers.size(), kNumTablets);
  const auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet->tablet_id()));

  // Wait until parent tablet got cleaned up.
  ASSERT_OK(LoggedWaitFor(
      [cluster = cluster_.get()]{
        return ListTabletPeers(cluster, ListPeersFilter::kAll).size() == 2;
      }, 60s, "Parent tablet cleanup"));

  // Total number of compactions equals to a sum of number of post split compaction iterations and
  // one background compaction. Number of post split compaction iterations equals to the number of
  // parent files plus one empty iteration to indicate post split compaction completion.
  constexpr size_t kNumParentFiles = kNumFiles + 1; // One more file due to an explicit flush.
  constexpr size_t kNumPostSplitCompactionIterations = kNumParentFiles + 1;
  constexpr size_t kNumBackgroundCompactions = 1;
  constexpr size_t kNumExpectedCompactions =
      kNumPostSplitCompactionIterations + kNumBackgroundCompactions;

  // Postpone status check for logging children files.
  auto status = WaitForNumCompactionsPerDb(kNumExpectedCompactions);

  // Make sure child tablets do not have parent files.
  dbs = GetAllRocksDbs(cluster_.get(), /* include_intents = */ false);
  ASSERT_EQ(dbs.size(), 2);
  for (auto* db : dbs) {
    const auto files = db->GetLiveFilesMetaData();
    LOG(INFO) << "Child files: " << files_ids(files);
    ASSERT_LT(parent_max_file_id, min_file_id(files));
  }

  ASSERT_OK(status);
}

class FullCompactionMonitoringTest : public CompactionTest {
 protected:
  void SetUp() override {
    CompactionTest::SetUp();
    SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);

    // Disable automatic compactions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_scheduled_full_compaction_frequency_hours) = 0;
  }

  void TearDown() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = false;
    CompactionTest::TearDown();
  }

  Status WaitForAllTabletsToHaveFullCompactionState(tablet::FullCompactionState expected_state) {
    std::string description = "Wait for all full compaction states to be " +
                              tablet::FullCompactionState_Name(expected_state);

    return WaitFor(
        [&]() -> Result<bool> {
          for (int i = 0; i < NumTabletServers(); ++i) {
            auto* ts_tablet_manager = cluster_->GetTabletManager(i);
            for (const auto& tablet_peer :
                 ts_tablet_manager->GetTabletPeersWithTableId(workload_table_id_)) {
              const auto actual_state =
                  VERIFY_RESULT(tablet_peer->shared_tablet())->HasActiveFullCompaction()
                      ? tablet::COMPACTING
                      : tablet::IDLE;
              if (expected_state != actual_state) {
                return false;
              }
            }
          }
          return true;
        },
        30s /* timeout */, description);
  }
};

TEST_F(FullCompactionMonitoringTest, IdleStateAfterAdminCompactionCompleted) {
  ASSERT_OK(WaitForAllTabletsToHaveFullCompactionState(tablet::IDLE));
  ASSERT_OK(TriggerAdminCompactions(ShouldWait::kTrue));
  ASSERT_OK(WaitForAllTabletsToHaveFullCompactionState(tablet::IDLE));
}

TEST_F(FullCompactionMonitoringTest, CompactingStateDuringAdminCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
  ASSERT_OK(TriggerAdminCompactions(ShouldWait::kFalse));
  ASSERT_OK(WaitForAllTabletsToHaveFullCompactionState(tablet::COMPACTING));
}

class MasterFullCompactionMonitoringTest : public FullCompactionMonitoringTest {
  void SetUp() override {
    FullCompactionMonitoringTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    test_tablets_ = ASSERT_RESULT(catalog_manager.GetTableInfo(workload_table_id_)->GetTablets());
  }

 protected:
  int NumTabletServers() override { return 3; }

  Status WaitForCompactionStatusesToSatisfy(
      const master::TabletInfos& tablet_infos,
      std::function<bool(const TabletServerId&, const master::FullCompactionStatus&)>
          check,
      const int expected_num_peers = FLAGS_replication_factor) {
    return WaitFor(
        [&]() {
          for (const auto& tablet_info : tablet_infos) {
            const auto replica_locations = tablet_info->GetReplicaLocations();
            if (static_cast<int>(replica_locations->size()) != expected_num_peers) {
              return false;
            }
            for (const auto& pair : *replica_locations) {
              if (!check(pair.first, pair.second.full_compaction_status)) {
                return false;
              }
            }
          }
          return true;
        },
        30s /* timeout */,
        "Wait for the full compaction states on master");
  }

  master::TabletInfos test_tablets_;
};

TEST_F(MasterFullCompactionMonitoringTest, UnknownStateAfterReplicaLocationChange) {
  const auto test_tablet = test_tablets_.front();

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(NumTabletServers() + 1));

  const auto proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  const master::MasterClusterProxy master_proxy(
      proxy_cache_.get(), cluster_->mini_master()->bound_rpc_addr());
  const auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, proxy_cache_.get()));

  const auto new_ts_uuid =
      cluster_->mini_tablet_server(NumTabletServers())->server()->permanent_uuid();
  const auto* new_ts = ts_map.find(new_ts_uuid)->second.get();
  itest::TServerDetails* leader_ts;
  ASSERT_OK(itest::FindTabletLeader(ts_map, test_tablet->id(), 10s /* timeout */, &leader_ts));

  // Wait for all the metrics heartbeats to come in.
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      {test_tablet}, [](const TabletServerId&, const master::FullCompactionStatus& status) {
        return status.full_compaction_state == tablet::IDLE &&
               status.last_full_compaction_time.ToUint64() == 0;
      }));

  // Fail heartbeats of the old tservers so we know the new tserver will send the config change.
  for (int i = 0; i < NumTabletServers(); ++i) {
    cluster_->mini_tablet_server(i)->FailHeartbeats();
  }

  // Cause a config change by adding a new tablet peer to the new tserver.
  ASSERT_OK(itest::AddServer(
      leader_ts, test_tablet->id(), new_ts, consensus::PeerMemberType::PRE_OBSERVER,
      std::nullopt /* cas_config_opid_index */, 10s /* timeout */));

  // Check that the full compaction states on master are all reset to UNKNOWN except for the tserver
  // that sent the config change heartbeat.
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      {test_tablet},
      [&new_ts_uuid](const TabletServerId& tserver_id, const master::FullCompactionStatus& status) {
        if (tserver_id == new_ts_uuid) {
          return status.full_compaction_state == tablet::IDLE &&
                 status.last_full_compaction_time.ToUint64() == 0;
        }
        return status.full_compaction_state == tablet::FULL_COMPACTION_STATE_UNKNOWN;
      },
      FLAGS_replication_factor + 1 /* num_peers */));

  // Re-enable heartbeats to check that the full compaction states on master eventually get
  // updated correctly.
  for (auto& ts : cluster_->mini_tablet_servers()) {
    ts->FailHeartbeats(false);
  }
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      {test_tablet},
      [](const TabletServerId&, const master::FullCompactionStatus& status) {
        return status.full_compaction_state == tablet::IDLE &&
               status.last_full_compaction_time.ToUint64() == 0;
      },
      FLAGS_replication_factor + 1 /* num_peers */));

  // Test the same thing but for removing a tablet peer.
  for (auto& ts : cluster_->mini_tablet_servers()) {
    if (ts->server()->permanent_uuid() != leader_ts->uuid()) {
      ts->FailHeartbeats();
    }
  }
  ASSERT_OK(itest::RemoveServer(
      leader_ts, test_tablet->id(), new_ts, std::nullopt /* cas_config_opid_index */,
      10s /* timeout */));
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      {test_tablet},
      [&leader_ts](const TabletServerId& tserver_id, const master::FullCompactionStatus& status) {
        if (tserver_id == leader_ts->uuid()) {
          return status.full_compaction_state == tablet::IDLE &&
                 status.last_full_compaction_time.ToUint64() == 0;
        }
        return status.full_compaction_state == tablet::FULL_COMPACTION_STATE_UNKNOWN;
      },
      FLAGS_replication_factor /* num_peers */));
}

TEST_F(MasterFullCompactionMonitoringTest, CompactionStateDuringAdminCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_full_compaction) = true;
  ASSERT_OK(TriggerAdminCompactions(ShouldWait::kFalse));
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      test_tablets_, [](const TabletServerId&, const master::FullCompactionStatus& status) {
        return status.full_compaction_state == tablet::COMPACTING &&
               status.last_full_compaction_time.ToUint64() == 0;
      }));
}

TEST_F(MasterFullCompactionMonitoringTest, IdleStateAfterAdminCompactionCompletion) {
  ASSERT_OK(TriggerAdminCompactions(ShouldWait::kTrue));
  ASSERT_OK(WaitForCompactionStatusesToSatisfy(
      test_tablets_, [](const TabletServerId&, const master::FullCompactionStatus& status) {
        return status.full_compaction_state == tablet::IDLE &&
               status.last_full_compaction_time.ToUint64() > 0;
      }));
}

namespace {

Status IterateTabletRows(
    tablet::Tablet* tablet, ColumnIdRep key_column_id,
    std::function<Status(const yb::qlexpr::QLTableRow& row)> callback) {
  const SchemaPtr schema = tablet->metadata()->schema();
  dockv::ReaderProjection projection(*schema);
  auto iter = VERIFY_RESULT(tablet->NewRowIterator(projection));
  qlexpr::QLTableRow row;
  std::unordered_set<int32_t> tablet_keys;
  while (VERIFY_RESULT(iter->FetchNext(&row))) {
    auto key_opt = row.GetValue(key_column_id);
    SCHECK(key_opt.has_value(), InternalError, "Key is not initialized");
    auto key = key_opt->get().int32_value();
    SCHECK(
        tablet_keys.insert(key).second, InternalError,
        Format("Duplicate key $0 in tablet $1", key, tablet->tablet_id()));
    RETURN_NOT_OK(callback(row));
  }
  return Status::OK();
}

size_t RoundUpToBlockSize(size_t size) {
  return (size + FLAGS_db_block_size_bytes - 1) / FLAGS_db_block_size_bytes *
         FLAGS_db_block_size_bytes;
}

} // namespace

TEST_F(CompactionTest, RemoveCorruptDataBlocks) {
  constexpr auto kNumFilesToWrite = 3;
  const size_t kCorruptionOffset = FLAGS_db_block_size_bytes * 2;
  const size_t kCorruptionSize = FLAGS_db_block_size_bytes * 3;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = kCorruptionSize * 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = -1;

  SetupWorkload(IsolationLevel::NON_TRANSACTIONAL);
  ASSERT_OK(WriteAtLeastFilesPerDb(kNumFilesToWrite));
  ASSERT_OK(cluster_->FlushTablets());

  const auto table_info = ASSERT_RESULT(FindTable(cluster_.get(), workload_->table_name()));
  const auto tablets = ASSERT_RESULT(table_info->GetTablets());
  ASSERT_GE(tablets.size(), 1);

  const auto tablet_id = tablets[0]->tablet_id();
  const auto tablet_peers = ASSERT_RESULT(ListTabletActivePeers(cluster_.get(), tablet_id));

  // RF=1
  ASSERT_EQ(tablet_peers.size(), 1);

  const auto tablet_peer = tablet_peers[0];
  const auto shared_tablet = tablet_peer->shared_tablet_maybe_null();

  client::TableHandle table;
  ASSERT_OK(table.Open(workload_->table_name(), client_.get()));
  const auto key_column_id =  table.schema().ColumnId(0);

  std::unordered_map<int32_t, qlexpr::QLTableRow> original_data;
  ASSERT_OK(IterateTabletRows(
      shared_tablet.get(), key_column_id,
      [&original_data, key_column_id](const qlexpr::QLTableRow& row) {
        original_data.emplace(row.GetValue(key_column_id)->get().int32_value(), row);
        return Status::OK();
      }));

  rocksdb::TablePropertiesCollection all_sst_props;
  ASSERT_OK(shared_tablet->regular_db()->GetPropertiesOfAllTables(&all_sst_props));

  const auto tablet_num_keys = original_data.size();

  size_t num_max_corrupt_keys_estimate = 0;
  size_t tablet_num_corrupt_data_blocks_estimate = 0;
  size_t tablet_num_total_data_blocks = 0;

  for (auto& sst_file : shared_tablet->regular_db()->GetLiveFilesMetaData()) {
    const auto base_file_path = sst_file.BaseFilePath();
    const auto data_file_path = sst_file.DataFilePath();
    const auto& sst_props = *all_sst_props[base_file_path];
    tablet_num_total_data_blocks += sst_props.num_data_blocks;
    const double compression_ratio =
        1.0 * (sst_props.raw_key_size + sst_props.raw_value_size) / sst_props.data_size;

    struct CorruptRange {
      size_t offset;
      size_t size;
    };
    for (auto corrupt_range :
         {CorruptRange{.offset = 0, .size = static_cast<size_t>(FLAGS_db_block_size_bytes / 5)},
          CorruptRange{kCorruptionOffset, kCorruptionSize}}) {
      ASSERT_OK(yb::CorruptFile(
          data_file_path, corrupt_range.offset, corrupt_range.size, CorruptionType::kZero));
      tablet_num_corrupt_data_blocks_estimate +=
          RoundUpToBlockSize(corrupt_range.size * compression_ratio) / FLAGS_db_block_size_bytes;
      // RocksDB record started before or ending after corrupt region is likely to be also corrupt.
      num_max_corrupt_keys_estimate += 2;
    }
  }

  LOG(INFO) << "num_total_data_blocks: " << tablet_num_total_data_blocks;

  num_max_corrupt_keys_estimate +=
      tablet_num_keys * tablet_num_corrupt_data_blocks_estimate / tablet_num_total_data_blocks;

  LOG(INFO) << "tablet_num_total_data_blocks: " << tablet_num_total_data_blocks
            << ", tablet_num_keys: " << tablet_num_keys
            << ", tablet_num_corrupt_data_blocks_estimate: "
            << tablet_num_corrupt_data_blocks_estimate
            << ", num_max_corrupt_keys_estimate: " << num_max_corrupt_keys_estimate;

  // Clear block cache.
  cluster_->GetTabletManager(0)->TEST_tablet_options()->block_cache->Evict(
      std::numeric_limits<size_t>::max() / 2);

  ASSERT_OK(SET_FLAG(allow_compaction_failures_for_tablet_ids, tablet_id));

  // Should fail.
  auto status = TriggerAdminCompactions(ShouldWait::kTrue);
  ASSERT_TRUE(status.IsCorruption()) << status;

  // Should success.
  ASSERT_OK(
      TriggerAdminCompactions(ShouldWait::kTrue, rocksdb::SkipCorruptDataBlocksUnsafe::kTrue));
  ASSERT_OK(TriggerAdminCompactions(ShouldWait::kTrue));

  // Check that expected number of rows disappeared.
  // Check that remaining rows have the same values are before corruption.
  size_t num_keys_remained = 0;
  ASSERT_OK(IterateTabletRows(
      shared_tablet.get(), key_column_id,
      [&original_data, &num_keys_remained, key_column_id](const qlexpr::QLTableRow& row) -> Status {
        const auto key = row.GetValue(key_column_id)->get().int32_value();
        auto it = original_data.find(key);
        SCHECK(it != original_data.end(), InternalError, "");
        SCHECK_EQ(row.ToString(), it->second.ToString(), InternalError, "");
        ++num_keys_remained;
        return Status::OK();
      }));

  const auto num_keys_lost = tablet_num_keys - num_keys_remained;
  LOG(INFO) << "num_keys_lost: " << num_keys_lost
            << ", num_max_corrupt_keys_estimate: " << num_max_corrupt_keys_estimate;
  ASSERT_LE(num_keys_lost, num_max_corrupt_keys_estimate);
}

} // namespace yb::tserver
