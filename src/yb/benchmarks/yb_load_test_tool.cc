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

#include <queue>
#include <set>
#include <atomic>

#include <glog/logging.h>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/common/partition.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/redis/redisserver/redis_parser.h"
#include "yb/common/common.pb.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master_util.h"
#include "yb/util/atomic.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/threadpool.h"

#include "yb/integration-tests/load_generator.h"

DEFINE_int32(rpc_timeout_sec, 30, "Timeout for RPC calls, in seconds");

DEFINE_int32(num_iter, 1, "Run the entire test this number of times");

DEFINE_string(load_test_master_addresses,
              "",
              "Addresses of masters for the cluster to operate on");

DEFINE_string(load_test_master_endpoint,
              "",
              "REST endpoint from which the master addresses can be obtained");

DEFINE_string(table_name, "yb_load_test", "Table name to use for YugaByte load testing");

DEFINE_int64(num_rows, 50000, "Number of rows to insert");

DEFINE_int64(num_noops, 100000000, "Number of noop requests to execute.");

DEFINE_int32(num_writer_threads, 4, "Number of writer threads");

DEFINE_int32(num_reader_threads, 4, "Number of reader threads (used for read or noop requests).");

DEFINE_int64(max_num_write_errors,
             1000,
             "Maximum number of write errors. The test is aborted after this number of errors.");

DEFINE_int64(max_num_read_errors,
             1000,
             "Maximum number of read errors. The test is aborted after this number of errors.");

DEFINE_int64(max_noop_errors,
             1000,
             "Maximum number of noop errors. The test is aborted after this number of errors.");

DEFINE_int32(num_tablets, 16, "Number of tablets to create in the table");

DEFINE_bool(noop_only, false, "Only perform noop requests");

DEFINE_bool(reads_only, false, "Only read the existing rows from the table.");

DEFINE_string(
    target_redis_server_addresses, "",
    "comma separated list of <host:port> addresses of the redis proxy server(s)");

DEFINE_bool(create_redis_table_and_exit, false, "If true, create the redis table and exit.");

DEFINE_bool(writes_only, false, "Writes a new set of rows into an existing table.");

DEFINE_bool(
    drop_table, false,
    "Whether to drop the table if it already exists. If true, the table is deleted and "
    "then recreated");

DEFINE_int64(value_size_bytes, 16, "Size of each value in a row being inserted");

DEFINE_bool(
    stop_on_empty_read, true,
    "Stop reading if we get an empty set of rows on a read operation");

using std::atomic_long;
using std::atomic_bool;

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBColumnSchema;
using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;
using yb::client::YBTable;
using yb::client::YBTableCreator;
using yb::client::YBTableName;
using std::shared_ptr;
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
using yb::YQLDatabase;

using yb::load_generator::KeyIndexSet;
using yb::load_generator::SessionFactory;
using yb::load_generator::NoopSessionFactory;
using yb::load_generator::YBSessionFactory;
using yb::load_generator::RedisNoopSessionFactory;
using yb::load_generator::RedisSessionFactory;
using yb::load_generator::MultiThreadedReader;
using yb::load_generator::MultiThreadedWriter;
using yb::load_generator::SingleThreadedScanner;
using yb::load_generator::FormatHexForLoadTestKey;

// ------------------------------------------------------------------------------------------------

void CreateTable(const YBTableName &table_name, YBClient* client);
void CreateRedisTable(const YBTableName &table_name, YBClient* client);
void CreateYBTable(const YBTableName &table_name, YBClient* client);

void LaunchYBLoadTest(SessionFactory *session_factory);

std::unique_ptr<YBClient> CreateYBClient();

void SetupYBTable(YBClient* client);

bool DropTableIfNecessary(YBClient* client, const YBTableName &table_name);

bool YBTableExistsAlready(YBClient* client, const YBTableName &table_name);

int main(int argc, char *argv[]) {
  gflags::SetUsageMessage(
      "Usage:\n"
      "    load_test_tool --load_test_master_endpoint http://<metamaster rest endpoint>\n"
      "    load_test_tool --load_test_master_addresses master1:port1,...,masterN:portN\n"
      "    load_test_tool --target_redis_server_addresses proxy1:port1,...,proxyN:portN");
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);

  if (FLAGS_reads_only && FLAGS_writes_only) {
    LOG(FATAL) << "Reads only and Writes only options cannot be set together.";
  }

  if (FLAGS_drop_table && (FLAGS_reads_only || FLAGS_writes_only)) {
    LOG(FATAL) << "If reads only or writes only option is set, then we cannot drop the table";
  }

  bool use_redis_table =
      !FLAGS_target_redis_server_addresses.empty() || FLAGS_create_redis_table_and_exit;

  if (!FLAGS_reads_only)
    LOG(INFO) << "num_keys = " << FLAGS_num_rows;

  for (int i = 0; i < FLAGS_num_iter; ++i) {
    auto client = CreateYBClient();
    if (!use_redis_table) {
      const YBTableName table_name(yb::YQL_DATABASE_CQL, "my_keyspace", FLAGS_table_name);
      SetupYBTable(client.get());

      yb::client::TableHandle table;
      CHECK_OK(table.Open(table_name, client.get()));
      if (FLAGS_reads_only) {
        SingleThreadedScanner scanner(&table);
        scanner.CountRows();
      } else if (FLAGS_noop_only) {
        NoopSessionFactory session_factory(client.get(), &table);
        // Noop operations are done as write operations.
        FLAGS_writes_only = true;
        LaunchYBLoadTest(&session_factory);
      } else {
        YBSessionFactory session_factory(client.get(), &table);
        LaunchYBLoadTest(&session_factory);
      }
    } else {
      SetupYBTable(client.get());
      if (FLAGS_create_redis_table_and_exit) {
        LOG(INFO) << "Done creating redis table";
        return 0;
      }
      if (FLAGS_noop_only) {
        RedisNoopSessionFactory session_factory(FLAGS_target_redis_server_addresses);
        LaunchYBLoadTest(&session_factory);
      } else {
        RedisSessionFactory session_factory(FLAGS_target_redis_server_addresses);
        LaunchYBLoadTest(&session_factory);
      }
    }

    LOG(INFO) << "Test completed (iteration: " << i + 1 << " out of " << FLAGS_num_iter << ")";
    LOG(INFO) << string(80, '-');
    LOG(INFO) << "";
  }
  return 0;
}

std::unique_ptr<YBClient> CreateYBClient() {
  YBClientBuilder client_builder;
  client_builder.default_rpc_timeout(MonoDelta::FromSeconds(FLAGS_rpc_timeout_sec));
  if (!FLAGS_load_test_master_addresses.empty() && !FLAGS_load_test_master_endpoint.empty()) {
    LOG(FATAL) << "Specify either 'load_test_master_addresses' or 'load_test_master_endpoint'";
  }
  if (!FLAGS_load_test_master_addresses.empty()) {
    client_builder.add_master_server_addr(FLAGS_load_test_master_addresses);
  } else if (!FLAGS_load_test_master_endpoint.empty()) {
    client_builder.add_master_server_endpoint(FLAGS_load_test_master_endpoint);
  }
  return CHECK_RESULT(client_builder.Build());
}

void SetupYBTable(YBClient* client) {
  string keyspace = "my_keyspace";
  if (!FLAGS_target_redis_server_addresses.empty() || FLAGS_create_redis_table_and_exit) {
    LOG(INFO) << "Ignoring FLAGS_table_name. Redis proxy expects table name to be "
              << yb::common::kRedisKeyspaceName << '.' << yb::common::kRedisTableName;
    FLAGS_table_name = yb::common::kRedisTableName;
    keyspace = yb::common::kRedisKeyspaceName;
  }
  const YBTableName table_name(
      yb::master::GetDefaultDatabaseType(keyspace), keyspace, FLAGS_table_name);
  CHECK_OK(client->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                              YQLDatabase::YQL_DATABASE_REDIS));

  if (!YBTableExistsAlready(client, table_name) || DropTableIfNecessary(client, table_name)) {
    CreateTable(table_name, client);
  }
}

void CreateTable(const YBTableName &table_name, YBClient* client) {
  if (!FLAGS_target_redis_server_addresses.empty() || FLAGS_create_redis_table_and_exit) {
    CreateRedisTable(table_name, client);
  } else {
    CreateYBTable(table_name, client);
  }

  // Workaround for ENG-516. After the table is created, wait a few seconds for leader balancing
  // to settle down before processing IO operations.
  LOG(INFO) << "Sleeping 10 seconds for leader balancing operations to settle.";
  sleep(10);
}

void CreateRedisTable(const YBTableName &table_name, YBClient* client) {
  LOG(INFO) << "Creating table with " << FLAGS_num_tablets << " hash based partitions.";
  std::unique_ptr<YBTableCreator> table_creator(client->NewTableCreator());
  Status table_creation_status = table_creator->table_name(table_name)
                                     .num_tablets(FLAGS_num_tablets)
                                     .table_type(yb::client::YBTableType::REDIS_TABLE_TYPE)
                                     .Create();
  if (!table_creation_status.ok()) {
    LOG(INFO) << "Table creation status message: " << table_creation_status.message().ToString();
  }
  if (table_creation_status.message().ToString().find("Table already exists") ==
      std::string::npos) {
    CHECK_OK(table_creation_status);
  }
}

void CreateYBTable(const YBTableName &table_name, YBClient* client) {
  LOG(INFO) << "Building schema";
  YBSchemaBuilder schemaBuilder;
  schemaBuilder.AddColumn("k")->PrimaryKey()->Type(yb::BINARY)->NotNull();
  schemaBuilder.AddColumn("v")->Type(yb::BINARY)->NotNull();
  YBSchema schema;
  CHECK_OK(schemaBuilder.Build(&schema));

  LOG(INFO) << "Creating table";
  std::unique_ptr<YBTableCreator> table_creator(client->NewTableCreator());
  Status table_creation_status =
      table_creator->table_name(table_name)
          .schema(&schema)
          .table_type(yb::client::YBTableType::YQL_TABLE_TYPE)
          .Create();
  if (!table_creation_status.ok()) {
    LOG(INFO) << "Table creation status message: " << table_creation_status.message().ToString();
  }
  if (table_creation_status.message().ToString().find("Table already exists") ==
      std::string::npos) {
    CHECK_OK(table_creation_status);
  }
}

bool YBTableExistsAlready(YBClient* client, const YBTableName &table_name) {
  LOG(INFO) << "Checking if table '" << table_name.ToString() << "' already exists";
  {
    YBSchema existing_schema;
    yb::PartitionSchema partition_schema;
    if (client->GetTableSchema(table_name, &existing_schema, &partition_schema).ok()) {
      LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
      return true;
    } else {
      LOG(INFO) << "Table '" << table_name.ToString() << "' does not exist yet";
      return false;
    }
  }
}

bool DropTableIfNecessary(YBClient* client, const YBTableName &table_name) {
  if (FLAGS_drop_table) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' already exists, deleting";
    // Table with the same name already exists, drop it.
    CHECK_OK(client->DeleteTable(table_name));
    return true;
  }
  return false;
}

void LaunchYBLoadTest(SessionFactory *session_factory) {
  LOG(INFO) << "Starting load test";
  atomic_bool stop_flag(false);
  if (FLAGS_writes_only) {
    // Adds more keys starting from next index after scanned index
    MultiThreadedWriter writer(
        FLAGS_num_rows, 0, FLAGS_num_writer_threads, session_factory, &stop_flag,
        FLAGS_value_size_bytes, FLAGS_max_num_write_errors);

    writer.Start();
    writer.WaitForCompletion();
  } else {
    MultiThreadedWriter writer(
        FLAGS_num_rows, 0, FLAGS_num_writer_threads, session_factory, &stop_flag,
        FLAGS_value_size_bytes, FLAGS_max_num_write_errors);

    writer.Start();
    MultiThreadedReader reader(FLAGS_num_rows, FLAGS_num_reader_threads, session_factory,
                               writer.InsertionPoint(), writer.InsertedKeys(), writer.FailedKeys(),
                               &stop_flag, FLAGS_value_size_bytes, FLAGS_max_num_read_errors,
                               FLAGS_stop_on_empty_read);

    reader.Start();

    writer.WaitForCompletion();

    // The reader will not stop on its own, so we stop it as soon as the writer stops.
    reader.Stop();
    reader.WaitForCompletion();
  }
}
