// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>
#include <set>
#include <atomic>

#include "yb/benchmarks/tpch/line_item_tsv_importer.h"
#include "yb/benchmarks/tpch/rpc_line_item_dao.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/util/atomic.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/threadpool.h"
#include "yb/common/common.pb.h"

DEFINE_int32(
  load_test_num_iter, 1,
  "Run the entire test this number of times");

DEFINE_string(
  load_test_master_addresses, "localhost",
  "Addresses of masters for the cluster to operate on");

DEFINE_string(
  load_test_table_name, "yb_load_test",
  "Table name to use for YugaByte load testing");

DEFINE_int64(
  load_test_num_rows, 50000,
  "Number of rows to insert");

DEFINE_int32(
  load_test_num_writer_threads, 4,
  "Number of writer threads");

DEFINE_int64(
  load_test_max_num_write_errors, 1000,
  "Maximum number of write errors. The test is aborted after this number of errors.");

DEFINE_int64(
  load_test_max_num_read_errors, 1000,
  "Maximum number of read errors. The test is aborted after this number of errors.");

DEFINE_bool(
  load_test_verbose, false,
  "Custom verbose log messages for debugging the load test tool");

DEFINE_int32(
  load_test_table_num_replicas, 3,
  "Replication factor for the load test table");

DEFINE_int32(
  insertion_tracker_delay_ms, 50,
  "The internval at which the \"insertion tracker thread\" wakes up in milliseconds");

DEFINE_int32(
  load_test_num_tablets, 16,
  "Number of tablets to create in the table");

DEFINE_bool(
    load_test_reads_only, false,
    "Only read the existing rows from the table.");

DEFINE_bool(
    load_test_writes_only, false,
    "Writes a new set of rows into an existing table.");

DEFINE_bool(
    create_table, true,
    "Whether the table should be created. Its made false when either reads_only/writes_only is "
    "true. If value is true, existing table will be deleted and recreated.");

DEFINE_bool(
    use_kv_table, false,
    "Use key-value table type backed by RocksDB");

using strings::Substitute;
using std::atomic_long;
using std::atomic_bool;

using namespace yb::client;
using yb::client::sp::shared_ptr;
using yb::Status;
using yb::ThreadPool;
using yb::ThreadPoolBuilder;
using yb::MonoDelta;
using yb::MemoryOrder;
using yb::ConditionVariable;
using yb::Mutex;
using yb::MutexLock;
using yb::CountDownLatch;
using yb::Slice;
using yb::YBPartialRow;
using yb::TableType;
using strings::Substitute;

void ConfigureSession(YBSession* session) {
  session->SetFlushMode(YBSession::FlushMode::MANUAL_FLUSH);
  session->SetTimeoutMillis(60000);
  session->SetExternalConsistencyMode(YBSession::ExternalConsistencyMode::CLIENT_PROPAGATED);
}

class KeyIndexSet {
 public:
  int NumElements() const {
    MutexLock l(mutex_);
    return set_.size();
  }

  void Insert(int64 key) {
    MutexLock l(mutex_);
    set_.insert(key);
  }

  bool Contains(int64 key) const {
    MutexLock l(mutex_);
    return set_.find(key) != set_.end();
  }

  bool RemoveIfContains(int64 key) {
    MutexLock l(mutex_);
    set<int64>::iterator it = set_.find(key);
    if (it == set_.end()) {
      return false;
    } else {
      set_.erase(it);
      return true;
    }
  }

  int64 GetRandomKey() const {
    MutexLock l(mutex_);
    // The set iterator does not support indexing, so we probabilistically choose a random element
    // by iterating the set.
    int n = set_.size();
    for (int64 x : set_) {
      if (rand() % n == 0) return x;
      --n;  // Decrement the number of remaining elements we are considering.
    }
    // This will only happen if the set is empty.
    return -1;
  }

 private:
  set<int64> set_;
  mutable Mutex mutex_;

  friend ostream& operator <<(ostream& out, const KeyIndexSet &key_index_set);
};

ostream& operator <<(ostream& out, const KeyIndexSet &key_index_set) {
  MutexLock l(key_index_set.mutex_);
  out << "[";
  bool first = true;
  for (auto key : key_index_set.set_) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << key;
  }
  out << "]";
  return out;
}

string FormatHex(uint64_t x) {
  char buf[64];
  snprintf(buf, sizeof(buf) - 1, "%016zx", x);
  return buf;
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedAction
// ------------------------------------------------------------------------------------------------

class MultiThreadedAction {
public:
  MultiThreadedAction(
    const string& description,
    int64 num_keys,
    int64 start_key,
    int num_action_threads,
    int num_extra_threads,
    YBClient* client,
    YBTable* table,
    atomic_bool* stop_flag);

  virtual void Start();
  virtual void WaitForCompletion();

  void Stop() { stop_flag_->store(true); }
  bool IsStopped() { return stop_flag_->load(); }

protected:

  inline static string GetKeyByIndex(int64 key_index) {
    string key_index_str(Substitute("key$0", key_index));
    return Substitute("$0_$1", FormatHex(std::hash<string>()(key_index_str)), key_index_str);
  }

  inline static string GetValueByIndex(int64 key_index) {
    return strings::Substitute("value$0", key_index);
  }

  virtual void RunActionThread(int actionIndex) = 0;
  virtual void RunStatsThread() = 0;

  string description_;
  const int64 num_keys_; // Total number of keys in the table after successful end of this action
  const int64 start_key_; // First insertion key index of the write action
  const int num_action_threads_;
  YBClient* client_;
  YBTable* table_;

  gscoped_ptr<ThreadPool> thread_pool_;
  CountDownLatch running_threads_latch_;

  atomic_bool* stop_flag_;
};

MultiThreadedAction::MultiThreadedAction(
    const string& description,
    int64 num_keys,
    int64 start_key,
    int num_action_threads,
    int num_extra_threads,
    YBClient* client,
    YBTable* table,
    atomic_bool* stop_flag)
  : description_(description),
    num_keys_(num_keys),
    start_key_(start_key),
    num_action_threads_(num_action_threads),
    client_(client),
    table_(table),
    running_threads_latch_(num_action_threads),
    stop_flag_(stop_flag) {
  ThreadPoolBuilder(description).set_max_threads(
    num_action_threads_ + num_extra_threads).Build(&thread_pool_);
}

void MultiThreadedAction::Start() {
  LOG(INFO) << "Starting " << num_action_threads_ << " " << description_ << " threads";
  thread_pool_->SubmitFunc(boost::bind(&MultiThreadedAction::RunStatsThread, this));
  for (int i = 0; i < num_action_threads_; i++) {
    thread_pool_->SubmitFunc(boost::bind(&MultiThreadedAction::RunActionThread, this, i));
  }
}

void MultiThreadedAction::WaitForCompletion() {
  thread_pool_->Wait();
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedWriter
// ------------------------------------------------------------------------------------------------

class MultiThreadedWriter : public MultiThreadedAction {
 public:
  virtual void WaitForCompletion() override;

// client and table are managed by the caller and their lifetime should be a superset of this
  // object's lifetime.
  MultiThreadedWriter(
    int64 num_keys,
    int64 start_key,
    int num_writer_threads,
    YBClient* client,
    YBTable* table,
    atomic_bool* stop_flag);

  virtual void Start() OVERRIDE;
  atomic_long* InsertionPoint() { return &inserted_up_to_inclusive_; }
  const KeyIndexSet* InsertedKeys() const { return &inserted_keys_; }
  const KeyIndexSet* FailedKeys() const { return &failed_keys_; }

 private:
  virtual void RunActionThread(int writerIndex);
  virtual void RunStatsThread();
  void RunInsertionTrackerThread();

  // This is the current key to be inserted by any thread. Each thread does an atomic get and
  // increment operation and inserts the current value.
  atomic_long next_key_;
  atomic_long inserted_up_to_inclusive_;

  KeyIndexSet inserted_keys_;
  KeyIndexSet failed_keys_;
};

MultiThreadedWriter::MultiThreadedWriter(
    int64 num_keys,
    int64 start_key,
    int num_writer_threads,
    YBClient* client,
    YBTable* table,
    atomic_bool* stop_flag)
  : MultiThreadedAction("writers", num_keys, start_key, num_writer_threads, 2, client, table,
                        stop_flag),
    next_key_(start_key),
    inserted_up_to_inclusive_(-1) {
}

void MultiThreadedWriter::Start() {
  MultiThreadedAction::Start();
  thread_pool_->SubmitFunc(boost::bind(&MultiThreadedWriter::RunInsertionTrackerThread, this));
}

void MultiThreadedWriter::WaitForCompletion() {
  MultiThreadedAction::WaitForCompletion();
  LOG(INFO) << "Inserted up to and including " << inserted_up_to_inclusive_.load();
}

void MultiThreadedWriter::RunActionThread(int writerIndex) {
  LOG(INFO) << "Writer thread " << writerIndex << " started";
  shared_ptr<YBSession> session(client_->NewSession());
  ConfigureSession(session.get());
  while (!IsStopped()) {
    int64 key_index = next_key_++;
    if (key_index >= num_keys_) {
      break;
    }

    gscoped_ptr<YBInsert> insert(table_->NewInsert());
    string key_str(GetKeyByIndex(key_index));
    string value_str(Substitute("value$0", key_index));
    insert->mutable_row()->SetBinary("k", key_str.c_str());
    insert->mutable_row()->SetBinary("v", value_str.c_str());
    Status apply_status = session->Apply(insert.release());
    if (apply_status.ok()) {
      Status flush_status = session->Flush();
      if (flush_status.ok()) {
        inserted_keys_.Insert(key_index);
        if (FLAGS_load_test_verbose) {
          LOG(INFO) << "Successfully inserted key #" << key_index << " at timestamp "
            << client_->GetLatestObservedTimestamp() << " or earlier";
        }
      } else {
        LOG(WARNING) << "Error inserting key '" << key_str << "': Flush() failed";
        failed_keys_.Insert(key_index);
        if (failed_keys_.NumElements() >= FLAGS_load_test_max_num_write_errors) {
          LOG(ERROR) <<
            "Reached the maximum number of write errors " <<
            FLAGS_load_test_max_num_write_errors << ", stopping the test.";
          Stop();
          break;
        }
      }
    } else {
      LOG(WARNING) << "Error inserting key '" << key_str << "': Apply() failed";
      failed_keys_.Insert(key_index);
    }
  }
  session->Close();
  LOG(INFO) << "Writer thread " << writerIndex << " finished";
  running_threads_latch_.CountDown();
}

void MultiThreadedWriter::RunStatsThread() {
  MicrosecondsInt64 start_time = GetMonoTimeMicros();
  while (!IsStopped() && running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(1));
    int64 current_key = next_key_.load();
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    LOG(INFO) << "Wrote " << current_key - start_key_ << " rows ("
      << current_key * 1000000.0 / (current_time - start_time) << " writes/sec)"
      << ", contiguous insertion point: " << inserted_up_to_inclusive_.load()
      << ", write errors: " << failed_keys_.NumElements();
  }
}

void MultiThreadedWriter::RunInsertionTrackerThread() {
  LOG(INFO) << "Insertion tracker thread started";
  int64 current_key = 0;  // the first key to be inserted
  while (!IsStopped() && running_threads_latch_.count() > 0) {
    while (failed_keys_.Contains(current_key) || inserted_keys_.RemoveIfContains(current_key)) {
      if (FLAGS_load_test_verbose) {
        LOG(INFO) << "Advancing insertion tracker to key #" << current_key;
      }
      inserted_up_to_inclusive_.store(current_key);
      current_key++;
    }
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_insertion_tracker_delay_ms));
  }
  LOG(INFO) << "Insertion tracker thread stopped";
}

// ------------------------------------------------------------------------------------------------
// SingleThreadedScanner
// ------------------------------------------------------------------------------------------------
// TODO: Make MultiThreaded.
class SingleThreadedScanner {
 public:
  SingleThreadedScanner(YBTable* table) {
    table_ = table;
    num_rows_ = 0;
  }

  int64 CountRows();

 private:
  int64   num_rows_;
  YBTable* table_;
};

int64 SingleThreadedScanner::CountRows() {
  YBScanner scanner(table_);
  CHECK_OK(scanner.Open());
  vector<YBRowResult> results;
  while (scanner.HasMoreRows()) {
    CHECK_OK(scanner.NextBatch(&results));
    num_rows_ += results.size();
    results.clear();
  }

  LOG(INFO) << " num read rows = " << num_rows_;
  return num_rows_;
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedReader
// ------------------------------------------------------------------------------------------------
class MultiThreadedReader : public MultiThreadedAction {
 public:
  MultiThreadedReader(
    int64 num_keys,
    int num_reader_threads,
    YBClient* client,
    YBTable* table,
    atomic_long* insertion_point,
    const KeyIndexSet* inserted_keys,
    const KeyIndexSet* failed_keys,
    atomic_bool* stop_flag);
  void IncrementReadErrorCount();

 protected:
  virtual void RunActionThread(int readerIndex) OVERRIDE;
  virtual void RunStatsThread() OVERRIDE;

 private:
  const atomic_long* insertion_point_;
  const KeyIndexSet* inserted_keys_;
  const KeyIndexSet* failed_keys_;
  atomic_long num_read_errors_;
  atomic_long num_reads_;
};

MultiThreadedReader::MultiThreadedReader(
    int64 num_keys,
    int num_reader_threads,
    YBClient* client,
    YBTable* table,
    atomic_long* insertion_point,
    const KeyIndexSet* inserted_keys,
    const KeyIndexSet* failed_keys,
    atomic_bool* stop_flag)
  : MultiThreadedAction("readers", num_keys, 0, num_reader_threads, 1, client, table, stop_flag),
    insertion_point_(insertion_point),
    inserted_keys_(inserted_keys),
    failed_keys_(failed_keys),
    num_read_errors_(0),
    num_reads_(0) {
}

void MultiThreadedReader::RunActionThread(int readerIndex) {
  LOG(INFO) << "Reader thread " << readerIndex << " started";
  shared_ptr<YBSession> session(client_->NewSession());
  ConfigureSession(session.get());

  // Wait until at least one row has been inserted (keys are numbered starting from 1).
  while (!IsStopped() && insertion_point_->load() < 0) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  while (!IsStopped()) {
    int64 key_index;
    int64 written_up_to = insertion_point_->load();
    do {
      switch (rand() % 3) {
        case 0:
          // Read the latest value that the insertion tracker knows we've written up to.
          key_index = written_up_to;
          break;
        case 1:
          // Read one of the keys that have been successfully inserted but have not been processed
          // by the insertion tracker thread yet.
          key_index = inserted_keys_->GetRandomKey();
          if (key_index == -1) {
            // The set is empty.
            key_index = written_up_to;
          }
          break;

        default:
          // We're assuming the total number of keys is < RAND_MAX (~2 billion) here.
          key_index = rand() % (written_up_to + 1);
          break;
      }
      // Ensure we don't try to read a key for which a write failed.
    } while (failed_keys_->Contains(key_index) && !IsStopped());
    if (FLAGS_load_test_verbose) {
      LOG(INFO) << "Reader thread " << readerIndex << " saw written_up_to="
        << written_up_to << " and picked key #" << key_index;
    }

    uint64_t read_ts = client_->GetLatestObservedTimestamp();
    YBScanner scanner(table_);
    scanner.SetSelection(YBClient::ReplicaSelection::LEADER_ONLY);
    string key_str(GetKeyByIndex(key_index));
    CHECK_OK(scanner.SetProjectedColumns({ "k", "v" }));
    CHECK_OK(
      scanner.AddConjunctPredicate(
        table_->NewComparisonPredicate(
          "k", YBPredicate::EQUAL,
          YBValue::CopyString(key_str))));
    CHECK_OK(scanner.Open());
    num_reads_++;
    if (!scanner.HasMoreRows()) {
      LOG(ERROR) << "No rows found for key #" << key_index << " (read timestamp: "
        << read_ts << ")";
      IncrementReadErrorCount();
      continue;
    }

    vector<YBScanBatch::RowPtr> rows;
    scanner.NextBatch(&rows);
    if (rows.size() != 1) {
      LOG(ERROR) << "Found an invalid number of rows for key #" << key_index << ": "
        << rows.size() << " (expected to find 1 row), read timestamp: " << read_ts;
      IncrementReadErrorCount();
      continue;
    }

    Slice returned_key, returned_value;
    CHECK_OK(rows.front().GetBinary("k", &returned_key));
    if (returned_key != key_str) {
      LOG(ERROR) << "Invalid key returned by the read operation: '" << returned_key << "', "
        << "expected: '" << key_str << "', read timestamp: " << read_ts;
      IncrementReadErrorCount();
      continue;
    }

    CHECK_OK(rows.front().GetBinary("v", &returned_value));
    string expected_value(GetValueByIndex(key_index));
    if (returned_value != expected_value) {
      LOG(ERROR) << "Invalid value returned by the read operation for key '" << key_str << "': "
        << "'" << returned_value << "', expected: '" << expected_value << "', read timestamp: "
        << read_ts;
      IncrementReadErrorCount();
      continue;
    }
  }

  session->Close();
  LOG(INFO) << "Reader thread " << readerIndex << " finished";
  running_threads_latch_.CountDown();
}

void MultiThreadedReader::RunStatsThread() {
  MicrosecondsInt64 start_time = GetMonoTimeMicros();
  while (!IsStopped() && running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(1));
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    long num_rows_read = num_reads_.load();
    LOG(INFO) << "Read " << num_rows_read << " rows ("
      << num_rows_read * 1000000.0 / (current_time - start_time) << " reads/sec)"
      << ", read errors: " << num_read_errors_.load();
  }
}

void MultiThreadedReader::IncrementReadErrorCount() {
  if (++num_read_errors_ >= FLAGS_load_test_max_num_read_errors) {
    LOG(ERROR) << "Reached the maximum number of read errors!";
    Stop();
  }
}

// ------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
    "Usage: load_test_tool --load_test_master_addresses master1:port1,...,masterN:portN"
  );
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);

  if (!FLAGS_load_test_reads_only)
    LOG(INFO) << "num_keys = " << FLAGS_load_test_num_rows;

  for (int i = 0; i < FLAGS_load_test_num_iter; ++i) {
    shared_ptr<YBClient> client;
    CHECK_OK(YBClientBuilder()
      .add_master_server_addr(FLAGS_load_test_master_addresses)
      .default_rpc_timeout(MonoDelta::FromSeconds(600))  // for debugging
      .Build(&client));

    const string table_name(FLAGS_load_test_table_name);

    if (FLAGS_load_test_reads_only || FLAGS_load_test_writes_only) {
      FLAGS_create_table = false;
    }

    if (FLAGS_load_test_reads_only && FLAGS_load_test_writes_only) {
      LOG(FATAL) << "Reads only and Writes only options cannot be set together.";
      return 0;
    }

    if (!FLAGS_load_test_reads_only && !FLAGS_load_test_writes_only && !FLAGS_create_table) {
      LOG(FATAL) << "If reads only or writes only option is not set, then table create should be "
        "allowed.";
      return 0;
    }

    LOG(INFO) << "Checking if table '" << table_name << "' already exists";
    {
      YBSchema existing_schema;
      if (client->GetTableSchema(table_name, &existing_schema).ok()) {
        if (FLAGS_create_table) {
          LOG(INFO) << "Table '" << table_name << "' already exists, deleting";
          // Table with the same name already exists, drop it.
          CHECK_OK(client->DeleteTable(table_name));
        }
      } else {
        LOG(INFO) << "Table '" << table_name << "' does not exist yet";

        if (!FLAGS_create_table) {
          LOG(FATAL) << "Exiting as the table was not asked to be created.";
          return 0;
        }
      }
    }

    if (FLAGS_create_table) {
      LOG(INFO) << "Building schema";
      YBSchemaBuilder schemaBuilder;
      schemaBuilder.AddColumn("k")->PrimaryKey()->Type(YBColumnSchema::BINARY)->NotNull();
      schemaBuilder.AddColumn("v")->Type(YBColumnSchema::BINARY)->NotNull();
      YBSchema schema;
      CHECK_OK(schemaBuilder.Build(&schema));

      // Create the number of partitions based on the split keys.
      vector<const YBPartialRow *> splits;
      for (uint64_t j = 1; j < FLAGS_load_test_num_tablets; j++) {
        YBPartialRow *row = schema.NewRow();
        // We divide the interval between 0 and 2**64 into the requested number of intervals.
        string split_key = FormatHex(
          ((uint64_t) 1 << 62) * 4.0 * j / (FLAGS_load_test_num_tablets));
        LOG(INFO) << "split_key #" << j << "=" << split_key;
        CHECK_OK(row->SetBinaryCopy(0, split_key));
        splits.push_back(row);
      }

      LOG(INFO) << "Creating table";
      gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
      Status table_creation_status = table_creator->table_name(table_name).schema(&schema)
        .split_rows(splits)
        .num_replicas(FLAGS_load_test_table_num_replicas)
        .table_type(
          FLAGS_use_kv_table ?
            YBTableType::KEY_VALUE_TABLE_TYPE :
            YBTableType::KUDU_COLUMNAR_TABLE_TYPE)
        .Create();
      if (!table_creation_status.ok()) {
        LOG(INFO) << "Table creation status message: " <<
        table_creation_status.message().ToString();
      }
      if (table_creation_status.message().ToString().find("Table already exists") ==
          std::string::npos) {
        CHECK_OK(table_creation_status);
      }
    }

    shared_ptr<YBTable> table;
    CHECK_OK(client->OpenTable(table_name, &table));

    LOG(INFO) << "Starting load test";
    atomic_bool stop_flag(false);
    if (FLAGS_load_test_reads_only) {
      SingleThreadedScanner scanner(table.get());

      scanner.CountRows();
    } else if (FLAGS_load_test_writes_only) {
      SingleThreadedScanner scanner(table.get());
      int64 num = scanner.CountRows();

      // Adds more keys starting from next index after scanned index
      MultiThreadedWriter writer(
        FLAGS_load_test_num_rows + num + 1, num + 1,
        FLAGS_load_test_num_writer_threads,
        client.get(),
        table.get(),
        &stop_flag);

      writer.Start();
      writer.WaitForCompletion();
    } else {
      MultiThreadedWriter writer(
        FLAGS_load_test_num_rows, 0,
        FLAGS_load_test_num_writer_threads,
        client.get(),
        table.get(),
        &stop_flag);

      writer.Start();
      MultiThreadedReader reader(
        FLAGS_load_test_num_rows,
        FLAGS_load_test_num_writer_threads,
        client.get(),
        table.get(),
        writer.InsertionPoint(),
        writer.InsertedKeys(),
        writer.FailedKeys(),
        &stop_flag);

      reader.Start();

      writer.WaitForCompletion();

      // The reader will not stop on its own, so we stop it as soon as the writer stops.
      reader.Stop();
      reader.WaitForCompletion();
    }

    LOG(INFO) << "Test completed (iteration: " << i + 1 << " out of " <<
      FLAGS_load_test_num_iter << ")";
    LOG(INFO) << string(80, '-');
    LOG(INFO) << "";
  }
  return 0;
}
