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

#include "yb/consensus/consensus.h"

#include "yb/integration-tests/cql_test_base.h"
#include "yb/integration-tests/load_generator.h"

using namespace std::literals;  // NOLINT

DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(max_queued_split_candidates);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_force_split_threshold_bytes);

DECLARE_double(TEST_simulate_lookup_partition_list_mismatch_probability);
DECLARE_bool(TEST_disable_split_tablet_candidate_processing);

namespace yb {

class CqlTabletSplitTest : public CqlTestBase {
  void SetUp() override {
    FLAGS_yb_num_shards_per_tserver = 1;
    FLAGS_enable_automatic_tablet_splitting = true;
    FLAGS_tablet_split_low_phase_size_threshold_bytes = 0;
    FLAGS_tablet_split_high_phase_size_threshold_bytes = 0;
    FLAGS_max_queued_split_candidates = 10;
    FLAGS_tablet_split_low_phase_shard_count_per_node = 0;
    FLAGS_tablet_split_high_phase_shard_count_per_node = 0;
    FLAGS_tablet_force_split_threshold_bytes = 30_KB;
    FLAGS_db_write_buffer_size = FLAGS_tablet_force_split_threshold_bytes / 4;
    FLAGS_db_block_size_bytes = 2_KB;
    FLAGS_db_filter_block_size_bytes = 2_KB;
    FLAGS_db_index_block_size_bytes = 2_KB;
    CqlTestBase::SetUp();
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

TEST_F(CqlTabletSplitTest, SecondaryIndex) {
  const auto kNumRows = std::numeric_limits<int64_t>::max();
  const auto kWriterThreads = 2;
  const auto kReaderThreads = 4;
  const auto kValueSizeBytes = 128;
  const auto kMaxWriteErrors = 100;
  const auto kMaxReadErrors = 100;
  const auto kNumSplits = 10;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  ASSERT_OK(
      session.ExecuteQuery("CREATE TABLE t (k varchar PRIMARY KEY, v varchar) WITH transactions = "
                           "{ 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery(
      "CREATE INDEX t_by_value ON t(v) WITH transactions = { 'enabled' : true }"));

  const auto start_num_active_tablets = GetNumActiveTablets(cluster_.get());
  LOG(INFO) << "Number of active tablets at workload start: " << start_num_active_tablets;

  CqlSecondaryIndexSessionFactory load_session_factory(driver_.get());
  std::atomic<bool> stop_requested{false};

  load_generator::MultiThreadedWriter writer(
      kNumRows, /* start_key = */ 0, kWriterThreads, &load_session_factory, &stop_requested,
      kValueSizeBytes, kMaxWriteErrors);
  load_generator::MultiThreadedReader reader(
      kNumRows, kReaderThreads, &load_session_factory, writer.InsertionPoint(),
      writer.InsertedKeys(), writer.FailedKeys(), &stop_requested,
      kValueSizeBytes, kMaxReadErrors);

  LOG(INFO) << "Started workload";
  writer.Start();
  reader.Start();
  FLAGS_TEST_simulate_lookup_partition_list_mismatch_probability = 0.5;
  size_t num_active_tablets = start_num_active_tablets;
  while (writer.IsRunning() && num_active_tablets < start_num_active_tablets + kNumSplits) {
    num_active_tablets = GetNumActiveTablets(cluster_.get());
    LOG(INFO) << "Number of active tablets: " << num_active_tablets;
    std::this_thread::sleep_for(3s);
  }
  writer.Stop();
  reader.Stop();
  writer.WaitForCompletion();
  reader.WaitForCompletion();

  LOG(INFO) << "Workload complete, num_writes: " << writer.num_writes()
            << ", num_write_errors: " << writer.num_write_errors()
            << ", num_reads: " << reader.num_reads()
            << ", num_read_errors:" << reader.num_read_errors();
  ASSERT_EQ(reader.read_status_stopped(), load_generator::ReadStatus::kOk)
      << " reader stopped due to: " << AsString(reader.read_status_stopped());
  ASSERT_LE(writer.num_write_errors(), kMaxWriteErrors);

  // Disable splitting and wait for pending splits to finish before shutdown.
  // TODO(tsplit): remove this workaround after https://github.com/yugabyte/yugabyte-db/issues/8222
  // is fixed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_split_tablet_candidate_processing) = true;
  std::this_thread::sleep_for(FLAGS_heartbeat_interval_ms * 1ms);
  const auto splits_completion_deadline = MonoTime::Now() + 15s * kTimeMultiplier;
  for (auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto consensus = peer->shared_consensus();
    if (consensus) {
      ASSERT_OK(Wait([consensus]() -> Result<bool> {
        return consensus->GetLastAppliedOpId() >= consensus->GetLastCommittedOpId();
      }, splits_completion_deadline, "Complete pending tablet splits"));
    }
  }

  LOG(INFO) << "Number of active tablets: " << GetNumActiveTablets(cluster_.get());
}

}  // namespace yb
