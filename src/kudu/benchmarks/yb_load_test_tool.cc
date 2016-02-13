// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>

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

using namespace kudu::client;
using kudu::client::sp::shared_ptr;
using kudu::Status;

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
    "Usage: yb_load_test_tool --yb_load_test_master_addresses master1:port1,...,masterN:portN"
  );
  kudu::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

  shared_ptr<KuduClient> client;
  CHECK_OK(KuduClientBuilder()
     .add_master_server_addr(FLAGS_yb_load_test_master_addresses)
     .Build(&client));
  shared_ptr<KuduSession> session(client->NewSession());
  session->SetFlushMode(KuduSession::FlushMode::MANUAL_FLUSH);
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

  CHECK_OK(session->Close());
  LOG(INFO) << "Test completed";
  return 0;
}
