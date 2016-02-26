// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>
#include <set>
#include <atomic>

#include <glog/logging.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kudu/benchmarks/tpch/line_item_tsv_importer.h"
#include "kudu/benchmarks/tpch/rpc_line_item_dao.h"
#include "kudu/benchmarks/tpch/tpch-schemas.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/util/atomic.h"
#include "kudu/util/env.h"
#include "kudu/util/errno.h"
#include "kudu/util/flags.h"
#include "kudu/util/logging.h"
#include "kudu/util/monotime.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/condition_variable.h"
#include "kudu/util/mutex.h"
#include "kudu/util/countdown_latch.h"

#define LOG_EXPR(expr) do { \
  LOG(INFO) << #expr << " = " << (expr); \
} while (0)

DEFINE_int32(
  yb_load_test_num_iter, 1,
  "Run the entire test this number of times");

DEFINE_string(
  yb_load_test_master_addresses, "localhost",
  "Addresses of masters for the cluster to operate on");

DEFINE_string(
  yb_load_test_table_name, "yb_load_test",
  "Table name to use for YugaByte load testing");

DEFINE_int64(
  yb_load_test_num_rows, 1000000,
  "Number of rows to insert");

DEFINE_int64(
  yb_load_test_progress_report_frequency, 10000,
  "Report progress once per this number of rows");

DEFINE_int64(
  yb_load_test_num_writer_threads, 4,
  "Number of writer threads");

DEFINE_int64(
  yb_load_test_max_num_read_errors, 1000,
  "Maximum number of read errors. The test is aborted after this number of errors.");

DEFINE_bool(
  yb_load_test_verbose, false,
  "Custom verbose log messages for debugging the load test tool");

DEFINE_int32(
  yb_load_test_table_num_replicas, 3,
  "Replication factor for the load test table");


using strings::Substitute;
using std::atomic_long;
using std::atomic_bool;

using namespace kudu::client;
using kudu::client::sp::shared_ptr;
using kudu::Status;
using kudu::ThreadPool;
using kudu::ThreadPoolBuilder;
using kudu::MonoDelta;
using kudu::MemoryOrder;
using kudu::ConditionVariable;
using kudu::Mutex;
using kudu::MutexLock;
using kudu::CountDownLatch;
using kudu::Slice;

void ConfigureSession(KuduSession* session) {
  session->SetFlushMode(KuduSession::FlushMode::MANUAL_FLUSH);
  session->SetTimeoutMillis(60000);
  session->SetExternalConsistencyMode(KuduSession::ExternalConsistencyMode::CLIENT_PROPAGATED);
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

// ------------------------------------------------------------------------------------------------
// MultiThreadedAction
// ------------------------------------------------------------------------------------------------

class MultiThreadedAction {
public:
  MultiThreadedAction(
    const string& description,
    int64 num_keys,
    int num_action_threads,
    int num_extra_threads,
    KuduClient* client,
    KuduTable* table);

  virtual void Start();
  virtual void WaitForCompletion();

protected:

  inline static string GetKeyByIndex(int64 key_index) {
    return strings::Substitute("key$0", key_index);
  }

  inline static string GetValueByIndex(int64 key_index) {
    return strings::Substitute("value$0", key_index);
  }

  virtual void RunActionThread(int actionIndex) = 0;
  virtual void RunStatsThread() = 0;

  string description_;
  const int64 num_keys_;
  const int num_action_threads_;
  KuduClient* client_;
  KuduTable* table_;

  gscoped_ptr<ThreadPool> thread_pool_;
  CountDownLatch running_threads_latch_;
};

MultiThreadedAction::MultiThreadedAction(
    const string& description,
    int64 num_keys,
    int num_action_threads,
    int num_extra_threads,
    KuduClient* client,
    KuduTable* table)
  : description_(description),
    num_keys_(num_keys),
    num_action_threads_(num_action_threads),
    client_(client),
    table_(table),
    running_threads_latch_(num_action_threads) {
  ThreadPoolBuilder(description).set_max_threads(
    num_action_threads_ + num_extra_threads).Build(&thread_pool_);
}

void MultiThreadedAction::Start() {
  LOG(INFO) << "Starting " << num_action_threads_ << " " << description_ << " threads, num_keys = "
  << num_keys_;
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
  MultiThreadedWriter(int64 num_keys, int num_writer_threads, KuduClient* client, KuduTable* table);

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
    int num_writer_threads,
    KuduClient* client,
    KuduTable* table)
  : MultiThreadedAction("writers", num_keys, num_writer_threads, 2, client, table),
    next_key_(0),
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
  shared_ptr<KuduSession> session(client_->NewSession());
  ConfigureSession(session.get());
  while (true) {
    int64 key_index = next_key_++;
    if (key_index >= num_keys_) {
      break;
    }

    gscoped_ptr<KuduInsert> insert(table_->NewInsert());
    string key_str(GetKeyByIndex(key_index));
    string value_str(strings::Substitute("value$0", key_index));
    insert->mutable_row()->SetString("k", key_str.c_str());
    insert->mutable_row()->SetString("v", value_str.c_str());
    Status apply_status = session->Apply(insert.release());
    if (apply_status.ok()) {
      Status flush_status = session->Flush();
      if (flush_status.ok()) {
        inserted_keys_.Insert(key_index);
        if (FLAGS_yb_load_test_verbose) {
          LOG(INFO) << "Successfully inserted key #" << key_index << " at timestamp "
            << client_->GetLatestObservedTimestamp() << " or earlier";
        }
      } else {
        LOG(WARNING) << "Error inserting key '" << key_str << "': Flush() failed";
        failed_keys_.Insert(key_index);
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
  while (running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(1));
    int64 current_key = next_key_.load();
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    LOG(INFO) << "Wrote " << current_key << " rows ("
      << current_key * 1000000.0 / (current_time - start_time) << " writes/sec)"
      << ", contiguous insertion point: " <<
        inserted_up_to_inclusive_.load();
  }
}

void MultiThreadedWriter::RunInsertionTrackerThread() {
  LOG(INFO) << "Insertion tracker thread started";
  int64 current_key = 0;  // the first key to be inserted
  while (running_threads_latch_.count() > 0) {
    while (failed_keys_.Contains(current_key) || inserted_keys_.RemoveIfContains(current_key)) {
      if (FLAGS_yb_load_test_verbose) {
        LOG(INFO) << "Advancing insertion tracker to key #" << current_key;
      }
      inserted_up_to_inclusive_.store(current_key);
      current_key++;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  LOG(INFO) << "Insertion tracker thread stopped";
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedReader
// ------------------------------------------------------------------------------------------------

class MultiThreadedReader : public MultiThreadedAction {
 public:
  MultiThreadedReader(
    int64 num_keys,
    int num_reader_threads,
    KuduClient* client,
    KuduTable* table,
    atomic_long* insertion_point,
    const KeyIndexSet* inserted_keys,
    const KeyIndexSet* failed_keys);
  void Stop();
  void IncrementReadErrorCount();

 protected:
  virtual void RunActionThread(int readerIndex) OVERRIDE;
  virtual void RunStatsThread() OVERRIDE;

 private:
  const atomic_long* insertion_point_;
  const KeyIndexSet* inserted_keys_;
  const KeyIndexSet* failed_keys_;
  atomic_bool stopped_;
  atomic_long num_read_errors_;
};

MultiThreadedReader::MultiThreadedReader(
    int64 num_keys,
    int num_reader_threads,
    KuduClient* client,
    KuduTable* table,
    atomic_long* insertion_point,
    const KeyIndexSet* inserted_keys,
    const KeyIndexSet* failed_keys)
  : MultiThreadedAction("readers", num_keys, num_reader_threads, 1, client, table),
    insertion_point_(insertion_point),
    inserted_keys_(inserted_keys),
    failed_keys_(failed_keys),
    stopped_(false),
    num_read_errors_(0) {
}

void MultiThreadedReader::Stop() {
  stopped_.store(true);
  WaitForCompletion();
}

void MultiThreadedReader::RunActionThread(int readerIndex) {
  LOG(INFO) << "Reader thread " << readerIndex << " started";
  shared_ptr<KuduSession> session(client_->NewSession());
  ConfigureSession(session.get());

  // Wait until at least one row has been inserted (keys are numbered starting from 1).
  while (!stopped_.load() && insertion_point_->load() < 0) {
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  while (!stopped_.load()) {
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
    } while (failed_keys_->Contains(key_index));
    if (FLAGS_yb_load_test_verbose) {
      LOG(INFO) << "Reader thread " << readerIndex << " saw written_up_to="
        << written_up_to << " and picked key #" << key_index;
    }

    uint64_t read_ts = client_->GetLatestObservedTimestamp();
    KuduScanner scanner(table_);
    string key_str(GetKeyByIndex(key_index));
    CHECK_OK(scanner.SetProjectedColumns({ "k", "v" }));
    CHECK_OK(
      scanner.AddConjunctPredicate(
        table_->NewComparisonPredicate(
          "k", KuduPredicate::EQUAL,
          KuduValue::CopyString(key_str))));
    CHECK_OK(scanner.Open());
    if (!scanner.HasMoreRows()) {
      LOG(ERROR) << "No rows found for key #" << key_index << " (read timestamp: "
        << read_ts << ")";
      IncrementReadErrorCount();
      continue;
    }

    vector<KuduScanBatch::RowPtr> rows;
    scanner.NextBatch(&rows);
    if (rows.size() != 1) {
      LOG(ERROR) << "Found an invalid number of rows for key #" << key_index << ": "
        << rows.size() << " (expected to find 1 row), read timestamp: " << read_ts;
      IncrementReadErrorCount();
      continue;
    }

    Slice returned_key, returned_value;
    CHECK_OK(rows.front().GetString("k", &returned_key));
    if (returned_key != key_str) {
      LOG(ERROR) << "Invalid key returned by the read operation: '" << returned_key << "', "
        << "expected: '" << key_str << "', read timestamp: " << read_ts;
      IncrementReadErrorCount();
      continue;
    }

    CHECK_OK(rows.front().GetString("v", &returned_value));
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
}

void MultiThreadedReader::IncrementReadErrorCount() {
  if (++num_read_errors_ >= FLAGS_yb_load_test_max_num_read_errors) {
    LOG(FATAL) << "Reached the maximum number of read errors!";
  }
}

// ------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
    "Usage: yb_load_test_tool --yb_load_test_master_addresses master1:port1,...,masterN:portN"
  );
  kudu::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

  for (int i = 0; i < FLAGS_yb_load_test_num_iter; ++i) {
    shared_ptr<KuduClient> client;
    CHECK_OK(KuduClientBuilder()
      .add_master_server_addr(FLAGS_yb_load_test_master_addresses)
      .default_rpc_timeout(MonoDelta::FromSeconds(600))  // for debugging
      .Build(&client));

    const string table_name(FLAGS_yb_load_test_table_name);

    LOG(INFO) << "Checking if table '" << table_name << "' already exists";
    {
      KuduSchema existing_schema;
      if (client->GetTableSchema(table_name, &existing_schema).ok()) {
        LOG(INFO) << "Table '" << table_name << "' already exists, deleting";
        // Table with the same name already exists, drop it.
        CHECK_OK(client->DeleteTable(table_name));
      } else {
        LOG(INFO) << "Table '" << table_name << "' does not exist yet";
      }
    }

    LOG(INFO) << "Building schema";
    KuduSchemaBuilder schemaBuilder;
    schemaBuilder.AddColumn("k")->PrimaryKey()->Type(KuduColumnSchema::STRING)->NotNull();
    schemaBuilder.AddColumn("v")->Type(KuduColumnSchema::STRING)->NotNull();
    KuduSchema schema;
    CHECK_OK(schemaBuilder.Build(&schema));

    LOG(INFO) << "Creating table";
    gscoped_ptr<KuduTableCreator> table_creator(client->NewTableCreator());
    Status table_creation_status = table_creator->table_name(table_name).schema(&schema)
      .num_replicas(FLAGS_yb_load_test_table_num_replicas).Create();
    if (!table_creation_status.ok()) {
      LOG(INFO) << "Table creation status message: " << table_creation_status.message().ToString();
    }

    if (table_creation_status.message().ToString().find("Table already exists") ==
        std::string::npos) {
      CHECK_OK(table_creation_status);
    }

    shared_ptr<KuduTable> table;
    CHECK_OK(client->OpenTable(table_name, &table));

    LOG(INFO) << "Starting load test";
    MultiThreadedWriter writer(
      FLAGS_yb_load_test_num_rows,
      FLAGS_yb_load_test_num_writer_threads,
      client.get(),
      table.get());

    writer.Start();

    MultiThreadedReader reader(
      FLAGS_yb_load_test_num_rows,
      FLAGS_yb_load_test_num_writer_threads,
      client.get(),
      table.get(),
      writer.InsertionPoint(),
      writer.InsertedKeys(),
      writer.FailedKeys());
    reader.Start();

    writer.WaitForCompletion();

    // The reader will run as long as the writer is running.
    reader.Stop();

    LOG(INFO) << "Test completed (iteration: " << i + 1 << " out of " <<
      FLAGS_yb_load_test_num_iter << ")";
    LOG(INFO) << string(80, '-');
    LOG(INFO) << "";
  }
  return 0;
}
