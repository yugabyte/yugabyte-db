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

#include <gtest/gtest.h>

#include "yb/consensus/consensus.h"
#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

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
DECLARE_int32(process_split_tablet_candidates_interval_msec);
DECLARE_int32(max_queued_split_candidates);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_force_split_threshold_bytes);

DECLARE_double(TEST_simulate_lookup_partition_list_mismatch_probability);
DECLARE_bool(TEST_disable_split_tablet_candidate_processing);

namespace yb {

namespace {

size_t GetNumActiveTablets(MiniCluster* cluster) {
  return ListTabletPeers(
             cluster,
             [](const std::shared_ptr<tablet::TabletPeer>& peer) -> bool {
               const auto tablet_meta = peer->tablet_metadata();
               const auto consensus = peer->shared_consensus();
               return tablet_meta && consensus &&
                      tablet_meta->table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE &&
                      tablet_meta->tablet_data_state() !=
                          tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED &&
                      consensus->GetLeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
             })
      .size();
}

} // namespace

class CqlTabletSplitTest : public CqlTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    FLAGS_yb_num_shards_per_tserver = 1;
    FLAGS_enable_automatic_tablet_splitting = true;

    // Setting this very low will just cause to include metrics in every heartbeat, no overhead on
    // setting it lower than FLAGS_heartbeat_interval_ms.
    FLAGS_tserver_heartbeat_metrics_interval_ms = 1;
    // Split as soon as we get tablet to split on master.
    FLAGS_process_split_tablet_candidates_interval_msec = 1;
    FLAGS_heartbeat_interval_ms = 1000;

    FLAGS_tablet_split_low_phase_size_threshold_bytes = 0;
    FLAGS_tablet_split_high_phase_size_threshold_bytes = 0;
    FLAGS_max_queued_split_candidates = 10;
    FLAGS_tablet_split_low_phase_shard_count_per_node = 0;
    FLAGS_tablet_split_high_phase_shard_count_per_node = 0;
    FLAGS_tablet_force_split_threshold_bytes = 64_KB;
    FLAGS_db_write_buffer_size = FLAGS_tablet_force_split_threshold_bytes;
    FLAGS_db_block_size_bytes = 2_KB;
    FLAGS_db_filter_block_size_bytes = 2_KB;
    FLAGS_db_index_block_size_bytes = 2_KB;
    CqlTestBase::SetUp();
  }

  void WaitUntilAllCommittedOpsApplied(const MonoDelta timeout) {
    const auto splits_completion_deadline = MonoTime::Now() + timeout;
    for (auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto consensus = peer->shared_consensus();
      if (consensus) {
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
    LOG(INFO) << "Number of active tablets: " << GetNumActiveTablets(cluster_.get());
  }

  void DoTearDown() override {
    // TODO(tsplit): remove this workaround after
    // https://github.com/yugabyte/yugabyte-db/issues/8222 is fixed.
    StopSplitsAndWait();
    CqlTestBase::DoTearDown();
  }

  void StartSecondaryIndexTest();
  void CompleteSecondaryIndexTest(int num_splits, MonoDelta timeout);

  int writer_threads_ = 2;
  int reader_threads_ = 4;
  int value_size_bytes_ = 128;
  int max_write_errors_ = 100;
  int max_read_errors_ = 100;
  CassandraSession session_;
  std::atomic<bool> stop_requested_{false};
  std::unique_ptr<load_generator::SessionFactory> load_session_factory_;
  std::unique_ptr<load_generator::MultiThreadedWriter> writer_;
  std::unique_ptr<load_generator::MultiThreadedReader> reader_;
  int start_num_active_tablets_;
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
  prepared_insert_ = CHECK_RESULT(session_.Prepare("INSERT INTO t (k, v) VALUES (?, ?)"));
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
  prepared_select_ = CHECK_RESULT(session_.Prepare("SELECT k, v FROM t WHERE v = ?"));
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
  ASSERT_OK(session_.ExecuteQuery(
      "CREATE TABLE t (k varchar PRIMARY KEY, v varchar) WITH transactions = "
      "{ 'enabled' : true }"));
  ASSERT_OK(session_.ExecuteQuery(
      "CREATE INDEX t_by_value ON t(v) WITH transactions = { 'enabled' : true }"));

  start_num_active_tablets_ = GetNumActiveTablets(cluster_.get());
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
  const auto num_wait_for_active_tablets = start_num_active_tablets_ + num_splits;
  size_t num_active_tablets;

  ASSERT_OK(LoggedWaitFor(
      [&]() {
        num_active_tablets = GetNumActiveTablets(cluster_.get());
        YB_LOG_EVERY_N_SECS(INFO, 5) << "Number of active tablets: " << num_active_tablets;
        if (!writer_->IsRunning()) {
          return true;
        }
        if (num_active_tablets > num_wait_for_active_tablets) {
          return true;
        }
        return false;
      },
      timeout,
      Format("Waiting for $0 active tablets or writer stopped", num_wait_for_active_tablets)));
  LOG(INFO) << "Number of active tablets: " << num_active_tablets;

  writer_->Stop();
  reader_->Stop();
  writer_->WaitForCompletion();
  reader_->WaitForCompletion();

  LOG(INFO) << "Workload complete, num_writes: " << writer_->num_writes()
            << ", num_write_errors: " << writer_->num_write_errors()
            << ", num_reads: " << reader_->num_reads()
            << ", num_read_errors:" << reader_->num_read_errors();
  ASSERT_EQ(reader_->read_status_stopped(), load_generator::ReadStatus::kOk)
      << " reader stopped due to: " << AsString(reader_->read_status_stopped());
  ASSERT_LE(writer_->num_write_errors(), max_write_errors_);
}

TEST_F(CqlTabletSplitTest, SecondaryIndex) {
  const auto kNumSplits = 10;

  ASSERT_NO_FATALS(StartSecondaryIndexTest());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_lookup_partition_list_mismatch_probability) = 0.5;
  ASSERT_NO_FATALS(CompleteSecondaryIndexTest(kNumSplits, 300s * kTimeMultiplier));
}

TEST_F(CqlTabletSplitTest, SecondaryIndexWithDrop) {
  const auto kNumSplits = 3;
  const auto kNumTestIters = 2;

  for (auto iter = 1; iter <= kNumTestIters; ++iter) {
    LOG(INFO) << "Iteration: " << iter;
    ASSERT_NO_FATALS(StartSecondaryIndexTest());
    ASSERT_NO_FATALS(CompleteSecondaryIndexTest(kNumSplits, 300s * kTimeMultiplier));

    // TODO(tsplit): Remove this workaround after
    // https://github.com/yugabyte/yugabyte-db/issues/8034 is fixed.
    {
      StopSplitsAndWait();
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    }

    LOG(INFO) << "Iteration: " << iter << " deleting test table";
    ASSERT_OK(session_.ExecuteQuery("DROP TABLE t"));
    LOG(INFO) << "Iteration: " << iter << " deleted test table";
  }
}

class CqlTabletSplitTestBatchTimeseries : public CqlTestBase<ExternalMiniCluster> {
 protected:
  void SetUpFlags() override {
    mini_cluster_opt_.extra_master_flags.push_back(Format("--yb_num_shards_per_tserver=$0", 1));
    mini_cluster_opt_.extra_master_flags.push_back(Format(
        "--tablet_split_size_threshold_bytes=$0", 300_KB));
    mini_cluster_opt_.extra_tserver_flags.push_back(Format("--db_write_buffer_size=$0", 100_KB));
  }
};

struct DataSource {
  std::string metric_id;
  uint64_t data_emit_start_ts = 1;
  uint64_t last_emitted_ts = -1;
};

TEST_F(CqlTabletSplitTestBatchTimeseries, Run) {
  const auto kWriterThreads = 4;
  const auto kReaderThreads = 4;
  const auto kMinMetricsCount = 10000;
  const auto kMaxMetricsCount = 20000;
  const auto kReadBatchSize = 100;
  const auto kWriteBatchSize = 500;
  const auto kReadBackDeltaTime = 180;

  const auto kMaxWriteErrors = 100;
  const auto kMaxReadErrors = 100;

  const auto kTableTtlSeconds = MonoDelta(24h).ToSeconds();

  const auto kRunTime = 200s;

  const auto kTableName = "batch_timeseries_test";

  std::atomic_int num_reads(0);
  std::atomic_int num_writes(0);
  std::atomic_int num_read_errors(0);
  std::atomic_int num_write_errors(0);

  Random r(/* seed */ 29383);
  const auto num_metrics = r.Uniform(kMaxMetricsCount - kMinMetricsCount) + kMinMetricsCount;
  std::vector<DataSource> data_sources;
  for (int i = 0; i < num_metrics; ++i) {
    DataSource source{Format("metric-$0", i)};
    data_sources.push_back(source);
  }

  auto session = CHECK_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(
      session.ExecuteQuery(Format(
          "CREATE TABLE $0 ("
            "metric_id varchar, "
            "ts bigint, "
            "value varchar, "
            "primary key (metric_id, ts)) "
            "WITH default_time_to_live = $1", kTableName, kTableTtlSeconds)));

  auto prepared_write = ASSERT_RESULT(session.Prepare(Format(
      "INSERT INTO $0 (metric_id, ts, value) VALUES (?, ?, ?)", kTableName)));

  auto prepared_read = ASSERT_RESULT(session.Prepare(Format(
      "SELECT * from $0 WHERE metric_id = ? AND ts > ? AND ts < ? ORDER BY ts DESC LIMIT ?",
      kTableName)));

  std::mutex random_mutex;
  auto get_random_source = [&r, &random_mutex, &data_sources]() {
    std::lock_guard<decltype(random_mutex)> lock(random_mutex);
    auto index = r.Uniform(data_sources.size());
    return data_sources.at(index);
  };

  auto get_value = [&r, &random_mutex](int ts) {
    std::lock_guard<decltype(random_mutex)> lock(random_mutex);
    return Format("0", r.Next32() + ts);
  };

  TestThreadHolder io_threads;
  auto reader = [&, &stop = io_threads.stop_flag()]() -> void {
    while (!stop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(1s * kTimeMultiplier);
      auto source = get_random_source();
      if (source.data_emit_start_ts > source.last_emitted_ts) {
        continue;
      }
      const int64_t end_ts = source.last_emitted_ts + 1;
      const int64_t start_ts = end_ts - kReadBackDeltaTime;

      auto stmt = prepared_read.Bind();
      stmt.Bind(0, source.metric_id);
      stmt.Bind(1, start_ts);
      stmt.Bind(2, end_ts);
      stmt.Bind(3, kReadBatchSize);
      auto status = session.Execute(stmt);
      if (!status.ok()) {
        LOG(INFO) << "Read failed: " << AsString(status);
        num_read_errors++;
      } else {
        num_reads += 1;
        YB_LOG_EVERY_N_SECS(INFO, 5) << "Completed " << num_reads << " reads.";
      }
    }
  };

  auto writer = [&, &stop = io_threads.stop_flag()]() -> void {
    while (!stop.load(std::memory_order_acquire)) {
      auto source = get_random_source();

      if (source.last_emitted_ts == -1) {
        source.last_emitted_ts = source.data_emit_start_ts;
      }
      int64_t ts = source.last_emitted_ts;
      CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);

      for (int i = 0; i < kWriteBatchSize; ++i) {
        auto stmt = prepared_write.Bind();
        stmt.Bind(0, source.metric_id);
        stmt.Bind(1, ts);
        stmt.Bind(2, Format("$0", get_value(ts)));
        batch.Add(&stmt);
        ts++;
      }

      auto status = session.ExecuteBatch(batch);
      if (!status.ok()) {
        LOG(INFO) << "Write failed: " << AsString(status);
        num_write_errors++;
      } else {
        num_writes += kWriteBatchSize;
        YB_LOG_EVERY_N_SECS(INFO, 5) << "Completed " << num_writes << " writes.";
        source.last_emitted_ts = ts;
      }
      std::this_thread::sleep_for(1s * kTimeMultiplier);
    }
  };

  for (int i = 0; i < kReaderThreads; ++i) {
    io_threads.AddThreadFunctor(reader);
  }
  for (int i = 0; i < kWriterThreads; ++i) {
    io_threads.AddThreadFunctor(writer);
  }

  auto stop_time = CoarseMonoClock::Now() + kRunTime;
  while (
      CoarseMonoClock::Now() < stop_time &&
      num_read_errors < kMaxReadErrors &&
      num_write_errors < kMaxWriteErrors) {
    std::this_thread::sleep_for(1s);
  }

  io_threads.Stop();
  EXPECT_LE(num_read_errors, kMaxReadErrors);
  EXPECT_LE(num_write_errors, kMaxWriteErrors);
}

}  // namespace yb
