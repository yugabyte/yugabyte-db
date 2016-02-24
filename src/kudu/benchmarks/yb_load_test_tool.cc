// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>
#include <set>

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

using strings::Substitute;

using namespace kudu::client;
using kudu::client::sp::shared_ptr;
using kudu::Status;
using kudu::AtomicInt;
using kudu::ThreadPool;
using kudu::ThreadPoolBuilder;
using kudu::MonoDelta;
using kudu::MemoryOrder;
using kudu::ConditionVariable;
using kudu::Mutex;
using kudu::MutexLock;
using kudu::CountDownLatch;

void ConfigureSession(KuduSession* session) {
  session->SetFlushMode(KuduSession::FlushMode::MANUAL_FLUSH);
  session->SetTimeoutMillis(60000);
}

class KeyIndexSet {
 public:
  int NumElements() {
    MutexLock l(mutex_);
    return set_.size();
  }

  void Insert(int64 key) {
    MutexLock l(mutex_);
    set_.insert(key);
  }

  bool Contains(int64 key) {
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
 private:
  set<int64> set_;
  Mutex mutex_;
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
  void WaitForCompletion();

protected:
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

void MultiThreadedAction::WaitForCompletion() {
  thread_pool_->Wait();
}

void MultiThreadedAction::Start() {
  LOG(INFO) << "Starting " << num_action_threads_ << " " << description_ << " threads, num_keys = "
  << num_keys_;
  thread_pool_->SubmitFunc(boost::bind(&MultiThreadedAction::RunStatsThread, this));
  for (int i = 0; i < num_action_threads_; i++) {
    thread_pool_->SubmitFunc(boost::bind(&MultiThreadedAction::RunActionThread, this, i));
  }
}

// ------------------------------------------------------------------------------------------------
// MultiThreadedWriter
// ------------------------------------------------------------------------------------------------

class MultiThreadedWriter : public MultiThreadedAction {
 public:
  // client and table are managed by the caller and their lifetime should be a superset of this
  // object's lifetime.
  MultiThreadedWriter(int64 num_keys, int num_writer_threads, KuduClient* client, KuduTable* table);

  virtual void Start() OVERRIDE;

 private:
  virtual void RunActionThread(int writerIndex);
  virtual void RunStatsThread();
  void RunInsertionTrackerThread();

  // This is the current key to be inserted by any thread. Each thread does an atomic get and
  // increment operation and inserts the current value.
  AtomicInt<int64> current_key_;

  KeyIndexSet inserted_keys_;
  KeyIndexSet failed_keys_;

//  Mutex failed_keys_lock_;
//  set<int64> failed_keys_;
//
//  Mutex inserted_keys_lock_;
//  set<int64> inserted_keys_;

  Mutex insertion_progress_lock_;
  ConditionVariable insertion_progress_condition_;
};

MultiThreadedWriter::MultiThreadedWriter(
    int64 num_keys,
    int num_writer_threads,
    KuduClient* client,
    KuduTable* table)
  : MultiThreadedAction("writers", num_keys, num_writer_threads, 2, client, table),
    current_key_(0),
    insertion_progress_condition_(&insertion_progress_lock_) {
}

void MultiThreadedWriter::Start() {
  MultiThreadedAction::Start();
  thread_pool_->SubmitFunc(boost::bind(&MultiThreadedWriter::RunInsertionTrackerThread, this));
}

void MultiThreadedWriter::RunActionThread(int writerIndex) {
  LOG(INFO) << "Writer thread " << writerIndex << " started";
  shared_ptr<KuduSession> session(client_->NewSession());
  ConfigureSession(session.get());
  while (true) {
    int64 key_index = current_key_.Increment(MemoryOrder::kMemOrderBarrier);
    if (key_index > num_keys_) {
      break;
    }

    gscoped_ptr<KuduInsert> insert(table_->NewInsert());
    string key(strings::Substitute("key$0", key_index));
    string value(strings::Substitute("value$0", key_index));
    insert->mutable_row()->SetString("k", key.c_str());
    insert->mutable_row()->SetString("v", value.c_str());
    Status apply_status = session->Apply(insert.release());
    if (apply_status.ok()) {
      Status flush_status = session->Flush();
      if (flush_status.ok()) {
        inserted_keys_.Insert(key_index);
      } else {
        LOG(WARNING) << "Error inserting key '" << key << "': Flush() failed";
        failed_keys_.Insert(key_index);
      }
    } else {
      LOG(WARNING) << "Error inserting key '" << key << "': Apply() failed";
      failed_keys_.Insert(key_index);
    }

    insertion_progress_condition_.Signal();
  }
  session->Close();
  LOG(INFO) << "Writer thread " << writerIndex << " finished";
  running_threads_latch_.CountDown();
}

void MultiThreadedWriter::RunStatsThread() {
  MicrosecondsInt64 start_time = GetMonoTimeMicros();
  while (running_threads_latch_.count() > 0) {
    running_threads_latch_.WaitFor(MonoDelta::FromSeconds(1));
    int64 current_key = current_key_.Load(MemoryOrder::kMemOrderAcquire);
    MicrosecondsInt64 current_time = GetMonoTimeMicros();
    LOG(INFO) << "Wrote " << current_key << " rows (" <<
      current_key * 1000000.0 / (current_time - start_time) << " rows/sec)";
  }
}

void MultiThreadedWriter::RunInsertionTrackerThread() {
  MutexLock l(insertion_progress_lock_);
  int64 current_key = 0;
  while (running_threads_latch_.count() > 0) {
    insertion_progress_condition_.Wait();
    if (failed_keys_.Contains(current_key) || inserted_keys_.RemoveIfContains(current_key)) {

      current_key++;
    }
  }
}

// ------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
    "Usage: yb_load_test_tool --yb_load_test_master_addresses master1:port1,...,masterN:portN"
  );
  kudu::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

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
  Status table_creation_status = table_creator->table_name(table_name).schema(&schema).Create();
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

  writer.WaitForCompletion();

  LOG(INFO) << "Test completed";
  return 0;
}
