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

  LOG(INFO) << "Building schema";
  KuduSchemaBuilder schemaBuilder;
  schemaBuilder.AddColumn("k")->PrimaryKey()->Type(KuduColumnSchema::STRING)->NotNull();
  schemaBuilder.AddColumn("v")->Type(KuduColumnSchema::STRING)->NotNull();
  KuduSchema schema;
  CHECK_OK(schemaBuilder.Build(&schema));

  LOG(INFO) << "Creating table";
  gscoped_ptr<KuduTableCreator> table_creator(client->NewTableCreator());
  const string kTableName = "yb_test";
  Status table_creation_status = table_creator->table_name(kTableName).schema(&schema).Create();
  LOG(INFO) << "Table creation status message: " << table_creation_status.message().ToString();

  // "Table already exists" is OK.
  // TODO: drop and re-create table.
  if (table_creation_status.message().ToString().find("Table already exists") ==
      std::string::npos) {
    CHECK_OK(table_creation_status);
  }

  shared_ptr<KuduTable> table;
  CHECK_OK(client->OpenTable(kTableName, &table));

  for (int i = 0; i < 1000; ++i) {
    gscoped_ptr<KuduInsert> insert(table->NewInsert());
    insert->mutable_row()->SetString("k", "key" + std::to_string(i));
    insert->mutable_row()->SetString("v", "value" + std::to_string(i));
    CHECK_OK(session->Apply(insert.release()));
    CHECK_OK(session->Flush());
  }

  CHECK_OK(session->Close());
  LOG(INFO) << "Test completed";
  return 0;
}
