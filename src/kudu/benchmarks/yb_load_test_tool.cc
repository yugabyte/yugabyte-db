// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <queue>

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

// ------------------------------------------------------------------------------------------------
// MultiThreadedWriter
// ------------------------------------------------------------------------------------------------

class MultiThreadedWriter {
public:
  // client and table are managed by the caller and their lifetime should be a superset of this
  // object's lifetime.
  MultiThreadedWriter(int64 num_keys, int num_threads, KuduClient* client, KuduTable* table);
  void Start();
  void WaitForCompletion();

private:
  void RunWriterThread(int writerIndex);

  // This is the current key to be inserted by any thread. Each thread does an atomic get and
  // increment operation and inserts the current value.
  AtomicInt<int64> current_key_;

  const int64 num_keys_;

  const int num_threads_;

  std::queue<int64> inserted_keys_;
  boost::mutex inserted_keys_lock_;

  std::queue<int64> failed_keys_;
  boost::mutex failed_keys_lock_;

  gscoped_ptr<ThreadPool> thread_pool_;
  KuduClient* client_;
  KuduTable* table_;
};

MultiThreadedWriter::MultiThreadedWriter(
    int64 num_keys,
    int num_threads,
    KuduClient* client,
    KuduTable* table)
  : current_key_(0),
    num_keys_(num_keys),
    num_threads_(num_threads),
    client_(client),
    table_(table) {
  ThreadPoolBuilder("writers").set_max_threads(num_threads_).Build(&thread_pool_);
}

void MultiThreadedWriter::Start() {
  for (int i = 0; i < num_threads_; i++) {
//    scoped_refptr<Thread> thread_ptr;
//    CHECK_OK(Thread::Create(
//      "Load test writers",
//      Substitute("writer thread #$0", i),
//      &MultiThreadedWriter::RunWriterThread,
//      this,
//      i,
//      &thread_ptr
//    ));
//    writer_threads_.push_back(thread_ptr.get());

    thread_pool_->SubmitFunc(boost::bind(&MultiThreadedWriter::RunWriterThread, this, i));
  }
}

void MultiThreadedWriter::RunWriterThread(int writerIndex) {
  LOG(INFO) << "Writer thread " << writerIndex << " started";
  shared_ptr<KuduSession> session(client_->NewSession());
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
    // This should auto-flush.
    session->Apply(insert.release());
  }
  LOG(INFO) << "Writer thread " << writerIndex << " finished";
}

void MultiThreadedWriter::WaitForCompletion() {
  thread_pool_->Wait();
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
  shared_ptr<KuduSession> session(client->NewSession());
  session->SetFlushMode(KuduSession::FlushMode::AUTO_FLUSH_SYNC);
  session->SetTimeoutMillis(60000);

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

  if (false) {
    for (int64 i = 0; i < FLAGS_yb_load_test_num_rows; ++i) {
      gscoped_ptr<KuduInsert> insert(table->NewInsert());
      string key(strings::Substitute("key$0", i));
      string value(strings::Substitute("value$0", i));
      insert->mutable_row()->SetString("k", key.c_str());
      insert->mutable_row()->SetString("v", value.c_str());
      VLOG(1) << "Insertion: " + insert->ToString();
      CHECK_OK(session->Apply(insert.release()));
      VLOG(1) << "Flushing pending writes at i = " << i;
      CHECK_OK(session->Flush());
      if (i % FLAGS_yb_load_test_progress_report_frequency == 0) {
        LOG(INFO) << "i = " << i;
      }
    }
  }

  CHECK_OK(session->Close());
  LOG(INFO) << "Test completed";
  return 0;
}
