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

// Tests that measure how the intents DB memtable drives flushes of the intents DB and the regular
// DB of a tablet.
//
// Intents of a committed transaction are removed with SingleDelete. When the intent is still in the
// active memtable, MemTable::Erase unlinks it from the skiplist, but the arena memory is not
// returned. MemTable::ShouldFlushNow uses arena usage, so the intents memtable fills up at the rate
// of intent bytes written, even though almost nothing is left to write to the SST file.
//
// Each intents DB flush requires the regular DB to be flushed up to the same op id
// (Tablet::IntentsDbFlushFilter). If the regular DB does not flush on its own within
// intents_flush_max_delay_ms, the regular DB flush is forced with reason kIntentsApply.

#include <atomic>
#include <map>
#include <regex>
#include <thread>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/listener.h"
#include "yb/rocksdb/table_properties.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server_options.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int32(intents_flush_max_delay_ms);
DECLARE_int32(memstore_size_mb);
DECLARE_int32(num_raft_ops_to_force_idle_intents_db_to_flush);

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

constexpr int kValueSize = 100;

struct FlushRecord {
  size_t order = 0;
  bool intents = false;
  int job_id = -1;
  bool started = false;
  bool completed = false;
  std::string reason;
  // From the flush_started event log line.
  int64_t num_memtables = -1;
  int64_t memtable_bytes = -1;
  int64_t memtable_entries = -1;
  // From the SST file written by the flush.
  uint64_t sst_entries = 0;
  uint64_t sst_raw_bytes = 0;
  uint64_t sst_data_bytes = 0;
  // Data, index and filter blocks.
  uint64_t sst_file_bytes = 0;

  std::string ToString() const {
    return Format(
        "$0 job=$1 reason=$2 num_memtables=$3 memtable_bytes=$4 memtable_entries=$5 "
        "sst_entries=$6 sst_raw_bytes=$7 sst_data_bytes=$8 sst_file_bytes=$9",
        intents ? "[I]" : "[R]", job_id, reason, num_memtables, memtable_bytes, memtable_entries,
        sst_entries, sst_raw_bytes, sst_data_bytes, sst_file_bytes);
  }
};

// Records flushes of the regular DB and intents DB of one tablet.
// - Memtable stats come from the flush_started event log line (glog sink).
// - SST stats come from EventListener::OnFlushCompleted.
// Both are joined on (db type, RocksDB job id).
// A flush job postponed by IntentsDbFlushFilter calls OnFlushCompleted without writing a file and
// without logging flush_started. Such jobs are excluded.
class FlushRecorder : public rocksdb::EventListener, public google::LogSink {
 public:
  struct CompactionRecord {
    bool intents = false;
    size_t input_files = 0;
    uint64_t input_bytes = 0;
    uint64_t output_bytes = 0;
  };

  // Starts recording flushes and compactions of the given tablet. Drops all previous records.
  void SetTabletId(const TabletId& tablet_id) {
    std::lock_guard lock(mutex_);
    tablet_id_ = tablet_id;
    records_.clear();
    compactions_.clear();
  }

  void OnCompactionCompleted(rocksdb::DB*, const rocksdb::CompactionJobInfo& info) override {
    if (info.input_files.empty()) {
      return;
    }
    std::lock_guard lock(mutex_);
    const auto& path = info.input_files.front();
    if (tablet_id_.empty() || path.find("tablet-" + tablet_id_) == std::string::npos) {
      return;
    }
    compactions_.push_back(CompactionRecord {
      .intents = path.find(".intents/") != std::string::npos,
      .input_files = info.input_files.size(),
      .input_bytes = info.stats.total_input_bytes,
      .output_bytes = info.stats.total_output_bytes,
    });
  }

  std::vector<CompactionRecord> Compactions() const {
    std::lock_guard lock(mutex_);
    return compactions_;
  }

  void OnFlushCompleted(rocksdb::DB*, const rocksdb::FlushJobInfo& info) override {
    std::lock_guard lock(mutex_);
    if (tablet_id_.empty() || info.file_path.find("tablet-" + tablet_id_) == std::string::npos) {
      return;
    }
    const bool intents = info.file_path.find(".intents/") != std::string::npos;
    auto& record = GetRecordUnlocked(intents, info.job_id);
    record.completed = true;
    record.reason = AsString(info.flush_reason);
    record.sst_entries = info.table_properties.num_entries;
    record.sst_raw_bytes =
        info.table_properties.raw_key_size + info.table_properties.raw_value_size;
    record.sst_data_bytes = info.table_properties.data_size;
    record.sst_file_bytes =
        info.table_properties.data_size + info.table_properties.data_index_size +
        info.table_properties.filter_size + info.table_properties.filter_index_size;
  }

  void send(
      google::LogSeverity severity, const char* full_filename, const char* base_filename,
      int line, const struct ::tm* tm_time, const char* message, size_t message_len) override {
    std::string_view msg(message, message_len);
    if (msg.find("flush_started") == std::string_view::npos) {
      return;
    }
    std::string str(msg);
    std::lock_guard lock(mutex_);
    if (tablet_id_.empty() || str.find(tablet_id_) == std::string::npos) {
      return;
    }
    auto job_id = ExtractInt(str, "job");
    if (job_id < 0) {
      return;
    }
    const bool intents = str.find("[I]") != std::string::npos;
    auto& record = GetRecordUnlocked(intents, static_cast<int>(job_id));
    record.started = true;
    record.num_memtables = ExtractInt(str, "num_memtables");
    record.memtable_bytes = ExtractInt(str, "memory_usage");
    record.memtable_entries = ExtractInt(str, "num_entries");
  }

  // Returns flushes that wrote an SST file, in order of first observation.
  std::vector<FlushRecord> Flushes() const {
    std::vector<FlushRecord> result;
    std::lock_guard lock(mutex_);
    for (const auto& [key, record] : records_) {
      if (record.started && record.completed) {
        result.push_back(record);
      }
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
      return lhs.order < rhs.order;
    });
    return result;
  }

  size_t CountFlushes(bool intents, const std::string& reason = std::string()) const {
    size_t result = 0;
    for (const auto& record : Flushes()) {
      result += record.intents == intents && (reason.empty() || record.reason == reason);
    }
    return result;
  }

 private:
  static int64_t ExtractInt(const std::string& str, const std::string& name) {
    std::smatch match;
    std::regex re("\"" + name + "\": (\\d+)");
    if (!std::regex_search(str, match, re)) {
      return -1;
    }
    return std::stoll(match[1].str());
  }

  FlushRecord& GetRecordUnlocked(bool intents, int job_id) {
    auto [it, inserted] = records_.try_emplace(std::make_pair(intents, job_id));
    if (inserted) {
      it->second.order = next_order_++;
      it->second.intents = intents;
      it->second.job_id = job_id;
    }
    return it->second;
  }

  mutable std::mutex mutex_;
  TabletId tablet_id_;
  std::map<std::pair<bool, int>, FlushRecord> records_;
  std::vector<CompactionRecord> compactions_;
  size_t next_order_ = 0;
};

struct FlushSummary {
  size_t intents_flushes = 0;
  size_t regular_flushes = 0;
  std::map<std::string, size_t> intents_reasons;
  std::map<std::string, size_t> regular_reasons;
};

FlushSummary Summarize(const std::vector<FlushRecord>& records) {
  FlushSummary summary;
  for (const auto& record : records) {
    LOG(INFO) << "Flush: " << record.ToString();
    if (record.intents) {
      ++summary.intents_flushes;
      ++summary.intents_reasons[record.reason];
    } else {
      ++summary.regular_flushes;
      ++summary.regular_reasons[record.reason];
    }
  }
  LOG(INFO) << "Intents flushes: " << summary.intents_flushes
            << ", reasons: " << AsString(summary.intents_reasons);
  LOG(INFO) << "Regular flushes: " << summary.regular_flushes
            << ", reasons: " << AsString(summary.regular_reasons);
  return summary;
}

} // namespace

class PgIntentsMemtableFlushTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    // Keep each flushed SST visible and avoid compaction interference.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = DisableCompactions();
    google::AddLogSink(recorder_.get());
    PgMiniTestBase::SetUp();
    // MiniCluster::StartAsync sets memstore_size_mb to 16. Tablets created after SetUp use the
    // value read here.
    write_buffer_size_ = FLAGS_memstore_size_mb * 1_MB;
    LOG(INFO) << "Write buffer size: " << write_buffer_size_;
  }

  void DoTearDown() override {
    google::RemoveLogSink(recorder_.get());
    PgMiniTestBase::DoTearDown();
  }

  virtual bool DisableCompactions() {
    return true;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  std::vector<tserver::TabletServerOptions> ExtraTServerOptions() override {
    auto opts = EXPECT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
    opts.listeners.push_back(recorder_);
    return {opts};
  }

  Status CreateTable(PGConn* conn, const std::string& primary_key = "k") {
    // A range sharded table starts with one tablet.
    const bool range = primary_key.find("ASC") != std::string::npos;
    RETURN_NOT_OK(conn->ExecuteFormat(
        "CREATE TABLE t (k INT, v TEXT, PRIMARY KEY ($0))$1", primary_key,
        range ? "" : " SPLIT INTO 1 TABLETS"));
    auto peers = VERIFY_RESULT(ListTabletPeersForTableName(cluster_.get(), "t"));
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected one tablet peer");
    tablet_peer_ = peers[0];
    recorder_->SetTabletId(tablet_peer_->tablet_id());
    LOG(INFO) << "Tablet id: " << tablet_peer_->tablet_id();
    return Status::OK();
  }

  // Distributed transaction: writes intents, then applies and removes them on commit.
  Status InsertInTxn(PGConn* conn, int first_key, int num_rows) {
    RETURN_NOT_OK(conn->Execute("BEGIN"));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "INSERT INTO t SELECT i, repeat('x', $2) FROM generate_series($0, $1) AS i",
        first_key, first_key + num_rows - 1, kValueSize));
    return conn->Execute("COMMIT");
  }

  // Single row autocommit insert: fast path, writes the regular DB only.
  Status InsertFastPath(PGConn* conn, int key) {
    return conn->ExecuteFormat("INSERT INTO t VALUES ($0, repeat('x', $1))", key, kValueSize);
  }

  // Wait for postponed intents flushes and the forced regular flushes to complete.
  void WaitForPendingFlushes() {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_intents_flush_max_delay_ms) + 3s);
  }

  std::shared_ptr<FlushRecorder> recorder_ = std::make_shared<FlushRecorder>();
  tablet::TabletPeerPtr tablet_peer_;
  int64_t write_buffer_size_ = 0;
};

// Transactions that commit quickly remove their intents from the active intents memtable with
// MemTable::Erase. The intents memtable still reaches the write buffer size, and the intents SST
// written by its flush is nearly empty. Each intents flush also flushes the regular DB before the
// regular memtable is full.
TEST_F(PgIntentsMemtableFlushTest, FastCommitIntentsFlushNearlyEmpty) {
  constexpr size_t kMinIntentsFlushes = 3;
  constexpr int kRowsPerTxn = 10;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));

  int key = 0;
  const auto deadline = CoarseMonoClock::now() + 300s * kTimeMultiplier;
  while (recorder_->CountFlushes(/* intents= */ true, "kMemtableSizeLimit") < kMinIntentsFlushes) {
    ASSERT_LT(CoarseMonoClock::now(), deadline) << "Too few intents flushes, rows: " << key;
    ASSERT_OK(InsertInTxn(&conn, key, kRowsPerTxn));
    key += kRowsPerTxn;
  }
  WaitForPendingFlushes();

  const auto records = recorder_->Flushes();
  auto summary = Summarize(records);
  LOG(INFO) << "Rows: " << key << ", txns: " << key / kRowsPerTxn
            << ", write buffer size: " << write_buffer_size_;

  size_t intents_size_limit_flushes = 0;
  for (const auto& record : records) {
    if (record.intents && record.reason == "kMemtableSizeLimit") {
      ++intents_size_limit_flushes;
      // The intents memtable was full.
      ASSERT_GE(record.memtable_bytes, write_buffer_size_ * 9 / 10) << record.ToString();
      // Almost all entries were erased before the flush.
      ASSERT_LT(record.sst_raw_bytes, write_buffer_size_ / 100) << record.ToString();
      ASSERT_LT(record.sst_entries * 100, implicit_cast<uint64_t>(record.memtable_entries))
          << record.ToString();
    }
    if (!record.intents && record.reason == "kIntentsApply") {
      // The regular memtable was not full when the intents DB forced the flush.
      ASSERT_LT(record.memtable_bytes, write_buffer_size_ * 9 / 10) << record.ToString();
    }
  }
  ASSERT_GE(intents_size_limit_flushes, kMinIntentsFlushes);

  // The regular memtable never fills up on its own: every regular flush is forced by the intents
  // DB, with reason kIntentsApply (IntentsDbFlushFilter) or kIntentFilesCleanup.
  ASSERT_EQ(summary.regular_reasons["kMemtableSizeLimit"], 0U);
  ASSERT_GE(summary.regular_reasons["kIntentsApply"], kMinIntentsFlushes - 1);
}

// The intents memtable is flushed with reason kIdleIntents when the tablet has applied
// num_raft_ops_to_force_idle_intents_db_to_flush raft ops since the last intents write.
// The intents memtable is not empty even when all its intents were erased, so it is flushed and the
// regular DB must be flushed first. A tablet with mostly fast path writes and occasional
// transactions flushes the regular DB every ~num_raft_ops_to_force_idle_intents_db_to_flush raft
// ops, regardless of the regular memtable size.
TEST_F(PgIntentsMemtableFlushTest, IdleIntentsForceRegularFlush) {
  constexpr size_t kCycles = 4;
  const int kFastPathRowsPerCycle = FLAGS_num_raft_ops_to_force_idle_intents_db_to_flush * 6 / 5;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(&conn));

  int key = 0;
  for (size_t cycle = 0; cycle != kCycles; ++cycle) {
    ASSERT_OK(InsertInTxn(&conn, key, /* num_rows= */ 1));
    ++key;
    for (int i = 0; i != kFastPathRowsPerCycle; ++i) {
      ASSERT_OK(InsertFastPath(&conn, key));
      ++key;
    }
    // No more writes until the intents DB forces the regular flush.
    ASSERT_OK(WaitFor(
        [this, cycle] {
          return recorder_->CountFlushes(/* intents= */ false, "kIntentsApply") > cycle;
        },
        15s * kTimeMultiplier, Format("Regular flush forced by intents, cycle $0", cycle)));
    LOG(INFO) << "Cycle " << cycle << " done, rows: " << key;
  }
  WaitForPendingFlushes();

  const auto records = recorder_->Flushes();
  auto summary = Summarize(records);
  LOG(INFO) << "Rows: " << key << ", write buffer size: " << write_buffer_size_;

  ASSERT_GE(summary.intents_reasons["kIdleIntents"], kCycles);
  ASSERT_GE(summary.regular_reasons["kIntentsApply"], kCycles);
  ASSERT_EQ(summary.regular_reasons["kMemtableSizeLimit"], 0U);
  for (const auto& record : records) {
    if (!record.intents && record.reason == "kIntentsApply") {
      // One cycle of rows, far below the write buffer size.
      ASSERT_LT(record.memtable_bytes, write_buffer_size_ / 10) << record.ToString();
    }
  }
}

YB_DEFINE_ENUM(WriteMode, (kTxnInsert)(kTxnUpdate)(kFastPathInsert)(kFastPathUpdate));

struct WriteModeStats {
  WriteMode mode;
  int rows = 0;
  MonoDelta elapsed;
  size_t regular_flushes = 0;
  size_t regular_forced_flushes = 0;
  // Sum of regular memtable bytes at flushes with reason kIntentsApply.
  int64_t regular_intents_apply_memtable_bytes = 0;
  size_t regular_intents_apply_flushes = 0;
  // Rows written to SSTs by regular flushes.
  uint64_t regular_flushed_rows = 0;
  uint64_t regular_flushed_bytes = 0;
  size_t intents_flushes = 0;
  uint64_t intents_flushed_bytes = 0;
  size_t regular_compactions = 0;
  uint64_t regular_compaction_input_bytes = 0;
  uint64_t regular_compaction_output_bytes = 0;
  size_t intents_compactions = 0;
  size_t live_regular_files = 0;
  size_t live_intents_files = 0;
  // Live regular SST files, sampled every second while writing.
  double avg_sampled_regular_files = 0;
  size_t max_sampled_regular_files = 0;

  uint64_t RowsPerRegularFlush() const {
    return regular_flushes ? regular_flushed_rows / regular_flushes : 0;
  }

  // (flushed bytes + compaction output bytes) / flushed bytes.
  double WriteAmplification() const {
    return regular_flushed_bytes
        ? static_cast<double>(regular_flushed_bytes + regular_compaction_output_bytes) /
              regular_flushed_bytes
        : 0;
  }

  int64_t AvgRegularMemtableBytesAtIntentsApply() const {
    return regular_intents_apply_flushes
        ? regular_intents_apply_memtable_bytes /
              static_cast<int64_t>(regular_intents_apply_flushes)
        : 0;
  }

  std::string ToString() const {
    return Format(
        "$0 rows=$1 elapsed=$2 regular_flushes=$3 (forced by intents: $4) "
        "rows_per_regular_flush=$5 avg_regular_memtable_bytes_at_kIntentsApply=$6 "
        "regular_flushed_bytes=$7 intents_flushes=$8 intents_flushed_bytes=$9 "
        "regular_compactions=$10 regular_compaction_input_bytes=$11 "
        "regular_compaction_output_bytes=$12 write_amplification=$13 intents_compactions=$14 "
        "live_regular_files=$15 live_intents_files=$16 avg_sampled_regular_files=$17 "
        "max_sampled_regular_files=$18",
        mode, rows, elapsed, regular_flushes, regular_forced_flushes, RowsPerRegularFlush(),
        AvgRegularMemtableBytesAtIntentsApply(), regular_flushed_bytes, intents_flushes,
        intents_flushed_bytes, regular_compactions, regular_compaction_input_bytes,
        regular_compaction_output_bytes, WriteAmplification(), intents_compactions,
        live_regular_files, live_intents_files, avg_sampled_regular_files,
        max_sampled_regular_files);
  }
};

// SQL expression for a value of kValueSize bytes.
// - fill_only: repeat(fill), compresses to a few percent.
// - otherwise: md5 hex digits derived from the key, compresses poorly.
std::string ValueSql(const std::string& key_sql, char fill, bool fill_only) {
  if (fill_only) {
    return Format("repeat('$0', $1)", std::string(1, fill), kValueSize);
  }
  return Format(
      "substr(md5($0::text || '$1a') || md5($0::text || '$1b') || md5($0::text || '$1c') || "
      "md5($0::text || '$1d'), 1, $2)",
      key_sql, std::string(1, fill), kValueSize);
}

class PgIntentsMemtableFlushCompactionTest : public PgIntentsMemtableFlushTest {
 protected:
  bool DisableCompactions() override {
    return false;
  }

  Result<WriteModeStats> RunWriteMode(
      WriteMode mode, int num_rows, int rows_per_stmt, bool fill_only) {
    const bool txn = mode == WriteMode::kTxnInsert || mode == WriteMode::kTxnUpdate;
    const bool update = mode == WriteMode::kTxnUpdate || mode == WriteMode::kFastPathUpdate;

    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("DROP TABLE IF EXISTS t"));
    // Range primary key, so that the update statements scan only the updated rows.
    RETURN_NOT_OK(CreateTable(&conn, "k ASC"));

    if (update) {
      // Load rows without intents, then compact them into one file before measuring.
      constexpr int kLoadBatch = 100000;
      RETURN_NOT_OK(conn.Execute("SET yb_disable_transactional_writes = true"));
      for (int first_key = 0; first_key < num_rows; first_key += kLoadBatch) {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO t SELECT i, $2 FROM generate_series($0, $1) AS i",
            first_key, std::min(first_key + kLoadBatch, num_rows) - 1,
            ValueSql("i", 'x', fill_only)));
      }
      RETURN_NOT_OK(conn.Execute("SET yb_disable_transactional_writes = false"));
      RETURN_NOT_OK(cluster_->CompactTablets());
      recorder_->SetTabletId(tablet_peer_->tablet_id());
    }

    auto tablet = VERIFY_RESULT(tablet_peer_->shared_tablet());
    std::atomic<bool> stop_sampler{false};
    size_t num_samples = 0;
    size_t sum_samples = 0;
    size_t max_sample = 0;
    std::thread sampler([&] {
      std::vector<rocksdb::LiveFileMetaData> files;
      while (!stop_sampler.load()) {
        files.clear();
        tablet->regular_db()->GetLiveFilesMetaData(&files);
        ++num_samples;
        sum_samples += files.size();
        max_sample = std::max(max_sample, files.size());
        SleepFor(1s);
      }
    });
    CancelableScopeExit stop_sampler_scope([&] {
      stop_sampler = true;
      sampler.join();
    });

    const auto start = MonoTime::Now();
    if (mode == WriteMode::kFastPathUpdate) {
      // yb_disable_transactional_writes does not apply to multi-row UPDATE, so use single row
      // autocommit updates (single shard fast path), from several connections for speed.
      constexpr int kNumThreads = 4;
      std::vector<PGConn> conns;
      for (int i = 0; i != kNumThreads; ++i) {
        conns.push_back(VERIFY_RESULT(Connect()));
      }
      std::vector<Status> statuses(kNumThreads);
      std::vector<std::thread> threads;
      for (int i = 0; i != kNumThreads; ++i) {
        threads.emplace_back([&conns, &statuses, i, num_rows, fill_only] {
          for (int key = i; key < num_rows; key += kNumThreads) {
            auto status = conns[i].ExecuteFormat(
                "UPDATE t SET v = $1 WHERE k = $0", key, ValueSql(AsString(key), 'y', fill_only));
            if (!status.ok()) {
              statuses[i] = status;
              return;
            }
          }
        });
      }
      for (auto& thread : threads) {
        thread.join();
      }
      for (const auto& status : statuses) {
        RETURN_NOT_OK(status);
      }
    } else if (!txn) {
      // Multi-row insert that skips the intents DB, same as single shard (fast path) writes.
      RETURN_NOT_OK(conn.Execute("SET yb_disable_transactional_writes = true"));
    }

    for (int first_key = 0;
         mode != WriteMode::kFastPathUpdate && first_key < num_rows;
         first_key += rows_per_stmt) {
      if (txn) {
        RETURN_NOT_OK(conn.Execute("BEGIN"));
      }
      if (update) {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "UPDATE t SET v = $2 WHERE k >= $0 AND k < $1",
            first_key, first_key + rows_per_stmt, ValueSql("k", 'y', fill_only)));
      } else {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO t SELECT i, $2 FROM generate_series($0, $1) AS i",
            first_key, first_key + rows_per_stmt - 1, ValueSql("i", 'x', fill_only)));
      }
      if (txn) {
        RETURN_NOT_OK(conn.Execute("COMMIT"));
      }
    }
    WriteModeStats stats {
      .mode = mode,
      .rows = num_rows,
      .elapsed = MonoTime::Now() - start,
    };
    WaitForPendingFlushes();
    stop_sampler_scope.Cancel();
    stop_sampler = true;
    sampler.join();
    stats.avg_sampled_regular_files =
        num_samples ? static_cast<double>(sum_samples) / num_samples : 0;
    stats.max_sampled_regular_files = max_sample;

    for (const auto& record : recorder_->Flushes()) {
      LOG(INFO) << mode << " flush: " << record.ToString();
      if (record.intents) {
        ++stats.intents_flushes;
        stats.intents_flushed_bytes += record.sst_file_bytes;
      } else {
        ++stats.regular_flushes;
        stats.regular_flushed_rows += record.sst_entries;
        stats.regular_flushed_bytes += record.sst_file_bytes;
        if (record.reason == "kIntentsApply" || record.reason == "kIntentFilesCleanup") {
          ++stats.regular_forced_flushes;
        }
        if (record.reason == "kIntentsApply") {
          ++stats.regular_intents_apply_flushes;
          stats.regular_intents_apply_memtable_bytes += record.memtable_bytes;
        }
      }
    }
    for (const auto& compaction : recorder_->Compactions()) {
      if (compaction.intents) {
        ++stats.intents_compactions;
      } else {
        ++stats.regular_compactions;
        stats.regular_compaction_input_bytes += compaction.input_bytes;
        stats.regular_compaction_output_bytes += compaction.output_bytes;
      }
    }
    std::vector<rocksdb::LiveFileMetaData> files;
    tablet->regular_db()->GetLiveFilesMetaData(&files);
    stats.live_regular_files = files.size();
    files.clear();
    tablet->intents_db()->GetLiveFilesMetaData(&files);
    stats.live_intents_files = files.size();

    LOG(INFO) << "Write mode stats: " << stats.ToString();
    return stats;
  }

  // Restores flags that the test harness changes and that affect flushes to their default values.
  // Applies to tablets created after the call.
  // - memstore_size_mb: MiniCluster sets 16.
  // - use_priority_thread_pool_for_flushes: YBMiniClusterTestBase sets true.
  // - never_fsync: YBTest sets true.
  void RestoreFlushFlagDefaults() {
    for (const auto* name :
         {"memstore_size_mb", "use_priority_thread_pool_for_flushes", "never_fsync"}) {
      auto info = google::GetCommandLineFlagInfoOrDie(name);
      ASSERT_FALSE(google::SetCommandLineOption(name, info.default_value.c_str()).empty());
      LOG(INFO) << "Restored " << name << " to default " << info.default_value;
    }
    write_buffer_size_ = FLAGS_memstore_size_mb * 1_MB;
  }

  Result<std::map<WriteMode, WriteModeStats>> CompareWriteModes(
      int num_rows, int rows_per_stmt, bool fill_only) {
    std::map<WriteMode, WriteModeStats> stats;
    for (auto mode : WriteModeList()) {
      stats[mode] = VERIFY_RESULT(RunWriteMode(mode, num_rows, rows_per_stmt, fill_only));
    }
    LOG(INFO) << "Write buffer size: " << write_buffer_size_ << ", rows: " << num_rows
              << ", rows per statement: " << rows_per_stmt << ", value size: " << kValueSize
              << ", fill only values: " << fill_only;
    for (const auto& [mode, mode_stats] : stats) {
      LOG(INFO) << "Summary: " << mode_stats.ToString();
    }
    return stats;
  }
};

// Writes the same number of rows with each write mode, and compares the number of regular DB
// flushes and compactions. In transactional modes, the regular DB is flushed when the intents
// memtable fills up, before the regular memtable is full.
TEST_F(PgIntentsMemtableFlushCompactionTest, CompareWriteModes) {
  auto stats = ASSERT_RESULT(CompareWriteModes(
      /* num_rows= */ 300000, /* rows_per_stmt= */ 10, /* fill_only= */ true));

  ASSERT_GT(stats[WriteMode::kTxnInsert].regular_flushes,
            stats[WriteMode::kFastPathInsert].regular_flushes);
  ASSERT_GT(stats[WriteMode::kTxnUpdate].regular_flushes,
            stats[WriteMode::kFastPathUpdate].regular_flushes);
  ASSERT_EQ(stats[WriteMode::kFastPathInsert].intents_flushes, 0U);
  ASSERT_EQ(stats[WriteMode::kFastPathUpdate].intents_flushes, 0U);
}

// Long version of CompareWriteModes, one test per write mode, with default flush settings
// (128 MB memtables), values that do not compress well and 20x more rows, so the regular DB grows
// past rocksdb_universal_compaction_always_include_size_threshold and compactions run repeatedly.
// Each mode runs for 5-20 minutes.
class PgIntentsMemtableFlushSteadyStateTest : public PgIntentsMemtableFlushCompactionTest,
                                              public ::testing::WithParamInterface<WriteMode> {
 protected:
  void SetUp() override {
    // Block cache defaults to 50% of the memory limit (8 GB in tests). Limit it to fit on small
    // hosts. Block cache size does not affect flush or compaction triggers.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_size_bytes) = 512_MB;
    PgIntentsMemtableFlushCompactionTest::SetUp();
  }
};

TEST_P(PgIntentsMemtableFlushSteadyStateTest, DISABLED_WriteMode) {
  ASSERT_NO_FATALS(RestoreFlushFlagDefaults());
  auto stats = ASSERT_RESULT(RunWriteMode(
      GetParam(), /* num_rows= */ 6000000, /* rows_per_stmt= */ 10, /* fill_only= */ false));
  LOG(INFO) << "Summary: " << stats.ToString();
  const bool txn = GetParam() == WriteMode::kTxnInsert || GetParam() == WriteMode::kTxnUpdate;
  ASSERT_EQ(stats.intents_flushes > 0, txn);
}

INSTANTIATE_TEST_SUITE_P(
    , PgIntentsMemtableFlushSteadyStateTest, ::testing::Values(
        WriteMode::kTxnInsert, WriteMode::kTxnUpdate, WriteMode::kFastPathInsert,
        WriteMode::kFastPathUpdate),
    [](const auto& info) { return AsString(info.param); });

} // namespace yb::pgwrapper
