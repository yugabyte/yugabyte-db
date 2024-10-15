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

#include <atomic>
#include <thread>

#include <boost/range/adaptors.hpp>
#include <gtest/gtest.h>

#include "yb/client/table_info.h"

#include "yb/consensus/consensus.h"

#include "yb/gutil/strings/join.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

using std::string;

using namespace std::literals;  // NOLINT

DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_force_split_threshold_bytes);

DECLARE_double(TEST_simulate_lookup_partition_list_mismatch_probability);
DECLARE_bool(TEST_reject_delete_not_serving_tablet_rpc);
DECLARE_bool(use_priority_thread_pool_for_flushes);

namespace yb {

namespace {

Result<size_t> GetNumActiveTablets(
    MiniCluster* cluster, const client::YBTableName& table_name, const MonoDelta&,
    const RequireTabletsRunning require_tablets_running) {
  master::GetTableLocationsResponsePB resp;
  RETURN_NOT_OK(itest::GetTableLocations(
      cluster, table_name, require_tablets_running, &resp));
  if (VLOG_IS_ON(4)) {
    for (const auto& tablet : resp.tablet_locations()) {
      VLOG_WITH_FUNC(4) << "tablet_id: " << tablet.tablet_id();
    }
  }
  return resp.tablet_locations_size();
}

Result<size_t> GetNumActiveTablets(
    ExternalMiniCluster* cluster, const client::YBTableName& table_name, const MonoDelta& timeout,
    const RequireTabletsRunning require_tablets_running) {
  master::GetTableLocationsResponsePB resp;
  RETURN_NOT_OK(itest::GetTableLocations(
      cluster, table_name, timeout, require_tablets_running, &resp));
  return resp.tablet_locations_size();
}

// If should_stop_func() returns true we are stopping writes and don't wait any more.
// This can be used to abort the test in case of too much read/write errors accumulated.
template <class MiniClusterType>
Status WaitForActiveTablets(
    MiniClusterType* cluster, const client::YBTableName& table_name,
    const size_t num_expected_active_tablets, const MonoDelta& timeout,
    std::atomic<bool>* stop_writes, std::function<bool()> should_stop_func) {
  const auto deadline = CoarseMonoClock::Now() + timeout;
  size_t num_active_tablets = 0;
  while (CoarseMonoClock::Now() < deadline && !should_stop_func()) {
    // When we get num_wait_for_active_tablets active tablets (not necessarily running), we stop
    // writes to avoid creating too many post-split tablets and overloading local cluster.
    // After stopping writes, we continue reads and wait for num_wait_for_active_tablets active
    // running tablets.
    auto num_active_tablets_res = GetNumActiveTablets(
        cluster, table_name, 10s * kTimeMultiplier,
        RequireTabletsRunning(stop_writes->load(std::memory_order_acquire)));
    if (num_active_tablets_res.ok()) {
      num_active_tablets = *num_active_tablets_res;
      YB_LOG_EVERY_N_SECS(INFO, 3) << "Number of active tablets: " << num_active_tablets;
      if (num_active_tablets >= num_expected_active_tablets) {
        if (!stop_writes->exchange(true)) {
          LOG(INFO) << "Stopping writes";
        }
        break;
      }
    }
    SleepFor(500ms);
  }
  LOG(INFO) << "Number of active tablets: " << num_active_tablets;
  if (CoarseMonoClock::Now() > deadline) {
    return STATUS_FORMAT(
        TimedOut, "Timed out waiting for $0 active tablets, only got $1",
        num_expected_active_tablets, num_active_tablets);
  }
  if (num_active_tablets < num_expected_active_tablets) {
    return STATUS_FORMAT(
        IllegalState, "Expected $0 active tablets, only got $1", num_expected_active_tablets,
        num_active_tablets);
  }
  return Status::OK();
}

} // namespace

const auto kSecondaryIndexTestTableName =
    client::YBTableName(YQL_DATABASE_CQL, kCqlTestKeyspace, "cql_test_table");

class CqlTabletSplitTest : public CqlTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_num_shards_per_tserver) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

    // Setting this very low will just cause to include metrics in every heartbeat, no overhead on
    // setting it lower than FLAGS_heartbeat_interval_ms.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 1000;

    // Reduce cleanup waiting time, so tests are completed faster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 64_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) =
        FLAGS_tablet_force_split_threshold_bytes;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_filter_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_index_block_size_bytes) = 2_KB;
    CqlTestBase::SetUp();

    // We want to test default behaviour here without overrides done by
    // YBMiniClusterTestBase::SetUp.
    // See https://github.com/yugabyte/yugabyte-db/issues/8935#issuecomment-1142223006
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_priority_thread_pool_for_flushes) =
        saved_use_priority_thread_pool_for_flushes_;
  }

  void WaitUntilAllCommittedOpsApplied(const MonoDelta timeout) {
    const auto splits_completion_deadline = MonoTime::Now() + timeout;
    for (auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto consensus_result = peer->GetConsensus();
      if (consensus_result) {
        auto consensus = consensus_result->get();
        ASSERT_OK(Wait([consensus]() -> Result<bool> {
          return consensus->GetLastAppliedOpId() >= consensus->GetLastCommittedOpId();
        }, splits_completion_deadline, "Waiting for all committed ops to be applied"));
      }
    }
  }

  // Disable splitting and wait for pending splits to complete.
  void StopSplitsAndWait() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // Give time to leaders for applying split ops that has been already scheduled.
    std::this_thread::sleep_for(1s * kTimeMultiplier);
    // Wait until followers also apply those split ops.
    ASSERT_NO_FATALS(WaitUntilAllCommittedOpsApplied(15s * kTimeMultiplier));
    LOG(INFO) << "Number of active tablets: " << GetNumActiveTablets(
          cluster_.get(), kSecondaryIndexTestTableName, 60s * kTimeMultiplier,
          RequireTabletsRunning::kTrue);
  }

  void DoTearDown() override {
    // TODO(tsplit): remove this workaround after
    // https://github.com/yugabyte/yugabyte-db/issues/8222 is fixed.
    StopSplitsAndWait();
    CqlTestBase::DoTearDown();
  }

  void StartSecondaryIndexTest();
  void CompleteSecondaryIndexTest(int num_splits, MonoDelta timeout);

  int writer_threads_ = RegularBuildVsSanitizers(2, 1);
  int reader_threads_ = RegularBuildVsSanitizers(4, 2);
  int value_size_bytes_ = 1024;
  size_t max_write_errors_ = 100;
  size_t max_read_errors_ = 100;
  CassandraSession session_;
  std::atomic<bool> stop_requested_{false};
  std::unique_ptr<load_generator::SessionFactory> load_session_factory_;
  std::unique_ptr<load_generator::MultiThreadedWriter> writer_;
  std::unique_ptr<load_generator::MultiThreadedReader> reader_;
  size_t start_num_active_tablets_;
};

class CqlTabletSplitTestMultiMaster : public CqlTabletSplitTest {
 public:
  int num_masters() override {
    return 3;
  }
};

class CqlSecondaryIndexWriter : public load_generator::SingleThreadedWriter {
 public:
  CqlSecondaryIndexWriter(
      load_generator::MultiThreadedWriter* writer, int writer_index, CppCassandraDriver* driver)
      : SingleThreadedWriter(writer, writer_index), driver_(driver) {}

 private:
  CppCassandraDriver* driver_;
  CassandraSession session_;
  CassandraPrepared prepared_insert_;

  void ConfigureSession() override;
  void CloseSession() override;
  bool Write(int64_t key_index, const string& key_str, const string& value_str) override;
  void HandleInsertionFailure(int64_t key_index, const string& key_str) override;
};

void CqlSecondaryIndexWriter::ConfigureSession() {
  session_ = CHECK_RESULT(EstablishSession(driver_));
  prepared_insert_ = CHECK_RESULT(session_.Prepare(Format(
      "INSERT INTO $0 (k, v) VALUES (?, ?)", kSecondaryIndexTestTableName.table_name())));
}

void CqlSecondaryIndexWriter::CloseSession() {
  session_.Reset();
}

bool CqlSecondaryIndexWriter::Write(
    int64_t key_index, const string& key_str, const string& value_str) {
  auto stmt = prepared_insert_.Bind();
  stmt.Bind(0, key_str);
  stmt.Bind(1, value_str);
  auto status = session_.Execute(stmt);
  if (!status.ok()) {
    LOG(INFO) << "Insert failed: " << AsString(status);
    return false;
  }
  return true;
}

void CqlSecondaryIndexWriter::HandleInsertionFailure(int64_t key_index, const string& key_str) {
}

class CqlSecondaryIndexReader : public load_generator::SingleThreadedReader {
 public:
  CqlSecondaryIndexReader(
      load_generator::MultiThreadedReader* writer, int writer_index, CppCassandraDriver* driver)
      : SingleThreadedReader(writer, writer_index), driver_(driver) {}

 private:
  CppCassandraDriver* driver_;
  CassandraSession session_;
  CassandraPrepared prepared_select_;

  void ConfigureSession() override;
  void CloseSession() override;
  load_generator::ReadStatus PerformRead(
      int64_t key_index, const string& key_str, const string& expected_value) override;
};

void CqlSecondaryIndexReader::ConfigureSession() {
  session_ = CHECK_RESULT(EstablishSession(driver_));
  prepared_select_ = CHECK_RESULT(session_.Prepare(
      Format("SELECT k, v FROM $0 WHERE v = ?", kSecondaryIndexTestTableName.table_name())));
}

void CqlSecondaryIndexReader::CloseSession() {
  session_.Reset();
}

load_generator::ReadStatus CqlSecondaryIndexReader::PerformRead(
      int64_t key_index, const string& key_str, const string& expected_value) {
  auto stmt = prepared_select_.Bind();
  stmt.Bind(0, expected_value);
  auto result = session_.ExecuteWithResult(stmt);
  if (!result.ok()) {
    LOG(WARNING) << "Select failed: " << AsString(result.status());
    return load_generator::ReadStatus::kOtherError;
  }
  auto iter = result->CreateIterator();
  auto values_formatter = [&] {
    return Format(
        "for v: '$0', expected key: '$1', key_index: $2", expected_value, key_str, key_index);
  };
  if (!iter.Next()) {
    LOG(ERROR) << "No rows found " << values_formatter();
    return load_generator::ReadStatus::kNoRows;
  }
  auto row = iter.Row();
  const auto k = row.Value(0).ToString();
  if (k != key_str) {
    LOG(ERROR) << "Invalid k " << values_formatter() << " got k: " << k;
    return load_generator::ReadStatus::kInvalidRead;
  }
  if (iter.Next()) {
    return load_generator::ReadStatus::kExtraRows;
    LOG(ERROR) << "More than 1 row found " << values_formatter();
    do {
      LOG(ERROR) << "k: " << iter.Row().Value(0).ToString();
    } while (iter.Next());
  }
  return load_generator::ReadStatus::kOk;
}

class CqlSecondaryIndexSessionFactory : public load_generator::SessionFactory {
 public:
  explicit CqlSecondaryIndexSessionFactory(CppCassandraDriver* driver) : driver_(driver) {}

  std::string ClientId() override { return "CQL secondary index test client"; }

  load_generator::SingleThreadedWriter* GetWriter(
      load_generator::MultiThreadedWriter* writer, int idx) override {
    return new CqlSecondaryIndexWriter(writer, idx, driver_);
  }

  load_generator::SingleThreadedReader* GetReader(
      load_generator::MultiThreadedReader* reader, int idx) override {
    return new CqlSecondaryIndexReader(reader, idx, driver_);
  }

 protected:
  CppCassandraDriver* driver_;
};

void CqlTabletSplitTest::StartSecondaryIndexTest() {
  const auto kNumRows = std::numeric_limits<int64_t>::max();

  session_ = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(session_.ExecuteQuery(Format(
      "CREATE TABLE $0 (k varchar PRIMARY KEY, v varchar) WITH transactions = "
      "{ 'enabled' : true }",
      kSecondaryIndexTestTableName.table_name())));
  ASSERT_OK(session_.ExecuteQuery(Format(
      "CREATE INDEX $0_by_value ON $0(v) WITH transactions = { 'enabled' : true }",
      kSecondaryIndexTestTableName.table_name())));

  start_num_active_tablets_ = ASSERT_RESULT(GetNumActiveTablets(
      cluster_.get(), kSecondaryIndexTestTableName, 60s * kTimeMultiplier,
      RequireTabletsRunning::kTrue));
  LOG(INFO) << "Number of active tablets at workload start: " << start_num_active_tablets_;

  load_session_factory_ = std::make_unique<CqlSecondaryIndexSessionFactory>(driver_.get());
  stop_requested_ = false;

  writer_ = std::make_unique<load_generator::MultiThreadedWriter>(
      kNumRows, /* start_key = */ 0, writer_threads_, load_session_factory_.get(), &stop_requested_,
      value_size_bytes_, max_write_errors_);
  reader_ = std::make_unique<load_generator::MultiThreadedReader>(
      kNumRows, reader_threads_, load_session_factory_.get(), writer_->InsertionPoint(),
      writer_->InsertedKeys(), writer_->FailedKeys(), &stop_requested_, value_size_bytes_,
      max_read_errors_);

  LOG(INFO) << "Started workload";
  writer_->Start();
  reader_->Start();
}

void CqlTabletSplitTest::CompleteSecondaryIndexTest(const int num_splits, const MonoDelta timeout) {
  auto s = WaitForActiveTablets(
      cluster_.get(), kSecondaryIndexTestTableName, start_num_active_tablets_ + num_splits, timeout,
      &stop_requested_, [&]() {
        return writer_->num_write_errors() > max_write_errors_ ||
               reader_->num_read_errors() > max_read_errors_;
      });

  writer_->Stop();
  reader_->Stop();
  writer_->WaitForCompletion();
  reader_->WaitForCompletion();

  LOG(INFO) << "Workload complete, num_writes: " << writer_->num_writes()
            << ", num_write_errors: " << writer_->num_write_errors()
            << ", num_reads: " << reader_->num_reads()
            << ", num_read_errors: " << reader_->num_read_errors();
  ASSERT_EQ(reader_->read_status_stopped(), load_generator::ReadStatus::kOk)
      << " reader stopped due to: " << AsString(reader_->read_status_stopped());
  ASSERT_LE(reader_->num_read_errors(), max_read_errors_);
  ASSERT_LE(writer_->num_write_errors(), max_write_errors_);
  ASSERT_OK(s);
}

TEST_F(CqlTabletSplitTest, SecondaryIndex) {
  const auto kNumSplits = RegularBuildVsSanitizers(10, 3);

  ASSERT_NO_FATALS(StartSecondaryIndexTest());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_lookup_partition_list_mismatch_probability) = 0.5;
  ASSERT_NO_FATALS(CompleteSecondaryIndexTest(kNumSplits, 300s * kTimeMultiplier));
}

TEST_F_EX(CqlTabletSplitTest, SecondaryIndexWithDrop, CqlTabletSplitTestMultiMaster) {
  const auto kNumSplits = 3;
  const auto kNumTestIters = 2;

#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency(
      {{"CatalogManager::DeleteNotServingTablet:Reject",
        "CqlTabletSplitTest::SecondaryIndexWithDrop:WaitForReject"}});
#endif // NDEBUG

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  for (auto iter = 1; iter <= kNumTestIters; ++iter) {
    LOG(INFO) << "Iteration " << iter;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_reject_delete_not_serving_tablet_rpc) = true;
#ifndef NDEBUG
    SyncPoint::GetInstance()->EnableProcessing();
#endif // NDEBUG

    ASSERT_NO_FATALS(StartSecondaryIndexTest());
    const auto table_info =
        ASSERT_RESULT(client->GetYBTableInfo(kSecondaryIndexTestTableName));
    ASSERT_NO_FATALS(CompleteSecondaryIndexTest(kNumSplits, 300s * kTimeMultiplier));

    TEST_SYNC_POINT("CqlTabletSplitTest::SecondaryIndexWithDrop:WaitForReject");

    if (iter > 1) {
      // Test tracking split tablets in case of leader master failover.
      const auto leader_master_idx = cluster_->LeaderMasterIdx();
      const auto sys_catalog_tablet_peer_leader =
          cluster_->mini_master(leader_master_idx)->tablet_peer();
      const auto sys_catalog_tablet_peer_follower =
          cluster_->mini_master((leader_master_idx + 1) % cluster_->num_masters())->tablet_peer();
      LOG(INFO) << "Iteration " << iter << ": stepping down master leader";
      ASSERT_OK(StepDown(
          sys_catalog_tablet_peer_leader, sys_catalog_tablet_peer_follower->permanent_uuid(),
          ForceStepDown::kFalse));
      LOG(INFO) << "Iteration " << iter << ": stepping down master leader - done";
    }

    LOG(INFO) << "Iteration " << iter << ": deleting test table";
    ASSERT_OK(session_.ExecuteQuery("DROP TABLE " + kSecondaryIndexTestTableName.table_name()));
    LOG(INFO) << "Iteration " << iter << ": deleted test table";

    // Make sure all table tablets deleted on all tservers.
    auto peer_to_str = [](const tablet::TabletPeerPtr& peer) { return peer->LogPrefix(); };
    std::vector<tablet::TabletPeerPtr> tablet_peers;
    auto s = LoggedWaitFor([&]() -> Result<bool> {
      tablet_peers = ListTableTabletPeers(cluster_.get(), table_info.table_id);
      return tablet_peers.size() == 0;
    }, 10s * kTimeMultiplier, "Waiting for tablets to be deleted");
    ASSERT_TRUE(s.ok()) << AsString(s) + ": expected tablets to be deleted, but following left:\n"
                        << JoinStrings(
                               tablet_peers | boost::adaptors::transformed(peer_to_str), "\n");

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_reject_delete_not_serving_tablet_rpc) = false;
#ifndef NDEBUG
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG
  }
}

class CqlTabletSplitTestExt : public CqlTestBase<ExternalMiniCluster> {
 protected:
  void SetUpFlags() override {
    const int64 kSplitThreshold = 64_KB;

    std::vector<std::string> common_flags;
    common_flags.push_back("--yb_num_shards_per_tserver=1");

    auto& master_flags = mini_cluster_opt_.extra_master_flags;
    master_flags.push_back(
        Format("--replication_factor=$0", std::min(num_tablet_servers(), 3)));
    master_flags.push_back("--enable_automatic_tablet_splitting=true");
    master_flags.push_back("--tablet_split_low_phase_size_threshold_bytes=0");
    master_flags.push_back("--tablet_split_high_phase_size_threshold_bytes=0");
    master_flags.push_back("--tablet_split_low_phase_shard_count_per_node=0");
    master_flags.push_back("--tablet_split_high_phase_shard_count_per_node=0");
    master_flags.push_back(Format("--tablet_force_split_threshold_bytes=$0", kSplitThreshold));

    auto& tserver_flags = mini_cluster_opt_.extra_tserver_flags;
    tserver_flags.push_back(Format("--db_write_buffer_size=$0", kSplitThreshold));
    tserver_flags.push_back("--rocksdb_max_write_buffer_number=2");
    // Lower SST block size to have multiple entries in index and be able to detect a split key.
    tserver_flags.push_back(Format(
        "--db_block_size_bytes=$0", std::min(FLAGS_db_block_size_bytes, kSplitThreshold / 8)));
    tserver_flags.push_back("--cleanup_split_tablets_interval_sec=5");

    for (auto& flag : common_flags) {
      master_flags.push_back(flag);
      tserver_flags.push_back(flag);
    }
  }
};

class CqlTabletSplitTestExtRf1 : public CqlTabletSplitTestExt {
 public:
  int num_tablet_servers() override { return 1; }
};

struct BatchTimeseriesDataSource {
  explicit BatchTimeseriesDataSource(const std::string& metric_id_) : metric_id(metric_id_) {}

  const std::string metric_id;
  const int64_t data_emit_start_ts = 1;
  std::atomic<int64_t> last_emitted_ts{-1};
};

Status RunBatchTimeSeriesTest(
    ExternalMiniCluster* cluster, CppCassandraDriver* driver, const int num_splits,
    const MonoDelta timeout) {
  const auto kWriterThreads = 4;
  const auto kReaderThreads = 4;
  const auto kMinMetricsCount = 10000;
  const auto kMaxMetricsCount = 20000;
  const auto kReadBatchSize = 100;
  const auto kWriteBatchSize = 500;
  const auto kReadBackDeltaTime = 100;
  const auto kValueSize = 100;

  const auto kMaxWriteErrors = 100;
  const auto kMaxReadErrors = 100;

  const auto kTableTtlSeconds = MonoDelta(24h).ToSeconds();

  const client::YBTableName kTableName(YQL_DATABASE_CQL, "test", "batch_timeseries_test");

  std::atomic_int num_reads(0);
  std::atomic_int num_writes(0);
  std::atomic_int num_read_errors(0);
  std::atomic_int num_write_errors(0);

  std::mt19937_64 rng(/* seed */ 29383);
  const auto num_metrics = RandomUniformInt<>(kMinMetricsCount, kMaxMetricsCount - 1, &rng);
  std::vector<std::unique_ptr<BatchTimeseriesDataSource>> data_sources;
  for (int i = 0; i < num_metrics; ++i) {
    data_sources.emplace_back(std::make_unique<BatchTimeseriesDataSource>(Format("metric-$0", i)));
  }

  auto session = CHECK_RESULT(EstablishSession(driver));

  RETURN_NOT_OK(
      session.ExecuteQuery(Format(
          "CREATE TABLE $0 ("
            "metric_id varchar, "
            "ts bigint, "
            "value varchar, "
            "primary key (metric_id, ts)) "
            "WITH default_time_to_live = $1", kTableName.table_name(), kTableTtlSeconds)));

  const auto start_num_active_tablets = VERIFY_RESULT(GetNumActiveTablets(
      cluster, kTableName, 60s * kTimeMultiplier, RequireTabletsRunning::kTrue));
  LOG(INFO) << "Number of active tablets at workload start: " << start_num_active_tablets;

  auto prepared_write = VERIFY_RESULT(session.Prepare(Format(
      "INSERT INTO $0 (metric_id, ts, value) VALUES (?, ?, ?)", kTableName.table_name())));

  auto prepared_read = VERIFY_RESULT(session.Prepare(Format(
      "SELECT * from $0 WHERE metric_id = ? AND ts > ? AND ts < ? ORDER BY ts DESC LIMIT ?",
      kTableName.table_name())));

  std::mutex random_mutex;
  auto get_random_source = [&rng, &random_mutex, &data_sources]() -> BatchTimeseriesDataSource* {
    std::lock_guard lock(random_mutex);
    return RandomElement(data_sources, &rng).get();
  };

  auto get_value = [](int64_t ts, std::string* value) {
    value->clear();
    value->append(AsString(ts));
    const auto suffix_size = value->size() >= kValueSize ? 0 : kValueSize - value->size();
    while (value->size() < kValueSize) {
      value->append(Uint16ToHexString(RandomUniformInt<uint16_t>()));
    }
    if (suffix_size > 0) {
      value->resize(kValueSize);
    }
    return value;
  };

  TestThreadHolder io_threads;
  auto reader = [&, &stop = io_threads.stop_flag()]() -> void {
    while (!stop.load(std::memory_order_acquire)) {
      auto& source = *CHECK_NOTNULL(get_random_source());
      if (source.last_emitted_ts < source.data_emit_start_ts) {
        continue;
      }
      const int64_t end_ts = source.last_emitted_ts + 1;
      const int64_t start_ts = std::max(end_ts - kReadBackDeltaTime, source.data_emit_start_ts);

      auto stmt = prepared_read.Bind();
      stmt.Bind(0, source.metric_id);
      stmt.Bind(1, start_ts);
      stmt.Bind(2, end_ts);
      stmt.Bind(3, kReadBatchSize);
      auto status = session.Execute(stmt);
      if (!status.ok()) {
        YB_LOG_EVERY_N_SECS(INFO, 1) << "Read failed: " << AsString(status);
        num_read_errors++;
      } else {
        num_reads += 1;
      }
      YB_LOG_EVERY_N_SECS(INFO, 5)
          << "Completed " << num_reads << " reads, read errors: " << num_read_errors;
    }
  };

  std::atomic<bool> stop_writes{false};
  auto writer = [&, &stop = io_threads.stop_flag()]() -> void {
    std::string value;
    // Reserve more bytes, because we fill by 2-byte blocks and might overfill and then
    // truncate, but don't want reallocation on growth.
    value.reserve(kValueSize + 1);
    while (!stop.load(std::memory_order_acquire) && !stop_writes.load(std::memory_order_acquire)) {
      auto& source = *CHECK_NOTNULL(get_random_source());

      if (source.last_emitted_ts == -1) {
        source.last_emitted_ts = source.data_emit_start_ts;
      }
      auto ts = source.last_emitted_ts.load(std::memory_order_acquire);
      CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);

      for (int i = 0; i < kWriteBatchSize; ++i) {
        auto stmt = prepared_write.Bind();
        stmt.Bind(0, source.metric_id);
        stmt.Bind(1, ts);
        get_value(ts, &value);
        stmt.Bind(2, value);
        batch.Add(&stmt);
        ts++;
      }

      auto status = session.ExecuteBatch(batch);
      if (!status.ok()) {
        YB_LOG_EVERY_N_SECS(INFO, 1) << "Write failed: " << AsString(status);
        num_write_errors++;
      } else {
        num_writes += kWriteBatchSize;
        source.last_emitted_ts = ts;
      }
      YB_LOG_EVERY_N_SECS(INFO, 5)
          << "Completed " << num_writes << " writes, num_write_errors: " << num_write_errors;
    }
  };

  for (int i = 0; i < kReaderThreads; ++i) {
    io_threads.AddThreadFunctor(reader);
  }
  for (int i = 0; i < kWriterThreads; ++i) {
    io_threads.AddThreadFunctor(writer);
  }

  auto s = WaitForActiveTablets(
      cluster, kTableName, start_num_active_tablets + num_splits, timeout, &stop_writes,
      [&]() {
        return num_read_errors > kMaxReadErrors || num_write_errors > kMaxWriteErrors;
      });
  if (s.IsTimedOut()) {
    // Produce a core dump for investigation.
    for (auto* daemon : cluster->daemons()) {
      ERROR_NOT_OK(daemon->Kill(SIGSEGV), "Failed to crash process: ");
    }
  }

  io_threads.Stop();
  LOG(INFO) << "num_reads: " << num_reads;
  LOG(INFO) << "num_writes: " << num_writes;
  LOG(INFO) << "num_read_errors: " << num_read_errors;
  LOG(INFO) << "num_write_errors: " << num_write_errors;
  EXPECT_LE(num_read_errors, kMaxReadErrors);
  EXPECT_LE(num_write_errors, kMaxWriteErrors);
  return s;
}

TEST_F_EX(CqlTabletSplitTest, BatchTimeseries, CqlTabletSplitTestExt) {
  // TODO(#10498) - Set this back to 20 once outstanding_tablet_split_limit is set back to a higher
  // value.
  const auto kNumSplits = 4;
  ASSERT_OK(
      RunBatchTimeSeriesTest(cluster_.get(), driver_.get(), kNumSplits, 300s * kTimeMultiplier));
}

TEST_F_EX(CqlTabletSplitTest, BatchTimeseriesRf1, CqlTabletSplitTestExtRf1) {
  // TODO(#10498) - Set this back to 20 once outstanding_tablet_split_limit is set back to a higher
  // value.
  const auto kNumSplits = 4;
  ASSERT_OK(
      RunBatchTimeSeriesTest(cluster_.get(), driver_.get(), kNumSplits, 300s * kTimeMultiplier));
}

}  // namespace yb
