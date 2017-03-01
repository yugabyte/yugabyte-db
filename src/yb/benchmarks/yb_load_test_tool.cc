// Copyright (c) YugaByte, Inc.

#include <queue>
#include <set>
#include <atomic>

#include <glog/logging.h>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>

#include "yb/benchmarks/tpch/line_item_tsv_importer.h"
#include "yb/benchmarks/tpch/rpc_line_item_dao.h"
#include "yb/client/client.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/common/common.pb.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
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

DEFINE_int32(num_replicas, 3, "Replication factor for the load test table");

DEFINE_int32(num_tablets, 16, "Number of tablets to create in the table");

DEFINE_bool(noop_only, false, "Only perform noop requests");

DEFINE_bool(reads_only, false, "Only read the existing rows from the table.");

DEFINE_string(
    target_redis_server_addresses, "",
    "comma separated list of <host:port> addresses of the redis proxy server(s)");

DEFINE_bool(writes_only, false, "Writes a new set of rows into an existing table.");

DEFINE_bool(
    drop_table, false,
    "Whether to drop the table if it already exists. If true, the table is deleted and "
    "then recreated");

DEFINE_bool(use_kv_table, true, "Use key-value table type backed by RocksDB");

DEFINE_int64(value_size_bytes, 16, "Size of each value in a row being inserted");

DEFINE_int32(
    retries_on_empty_read, 0,
    "We can retry up to this many times if we get an empty set of rows on a read "
    "operation");

using strings::Substitute;
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

using strings::Substitute;

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

void CreateTable(const YBTableName &table_name, const shared_ptr<YBClient> &client);
void CreateRedisTable(const YBTableName &table_name, const shared_ptr<YBClient> &client);
void CreateYBTable(const YBTableName &table_name, const shared_ptr<YBClient> &client);

void LaunchYBLoadTest(SessionFactory *session_factory);

shared_ptr<YBClient> CreateYBClient();

void SetupYBTable(const shared_ptr<YBClient> &client);

bool DropTableIfNecessary(const shared_ptr<YBClient> &client, const YBTableName &table_name);

bool YBTableExistsAlready(const shared_ptr<YBClient> &client, const YBTableName &table_name);

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

  bool use_redis_table = !FLAGS_target_redis_server_addresses.empty();

  if (!FLAGS_reads_only)
    LOG(INFO) << "num_keys = " << FLAGS_num_rows;

  for (int i = 0; i < FLAGS_num_iter; ++i) {
    if (!use_redis_table) {
      const YBTableName table_name(FLAGS_table_name);
      shared_ptr<YBClient> client = CreateYBClient();
      SetupYBTable(client);

      shared_ptr<YBTable> table;
      CHECK_OK(client->OpenTable(table_name, &table));
      if (FLAGS_reads_only) {
        SingleThreadedScanner scanner(table.get());
        scanner.CountRows();
      } else if (FLAGS_noop_only) {
        NoopSessionFactory session_factory(client.get(), table.get());
        // Noop operations are done as write operations.
        FLAGS_writes_only = true;
        LaunchYBLoadTest(&session_factory);
      } else {
        YBSessionFactory session_factory(client.get(), table.get());
        LaunchYBLoadTest(&session_factory);
      }
    } else {
      SetupYBTable(CreateYBClient());
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

shared_ptr<YBClient> CreateYBClient() {
  shared_ptr<YBClient> client;
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
  CHECK_OK(client_builder.Build(&client));

  return client;
}

void SetupYBTable(const shared_ptr<YBClient> &client) {
  if (!FLAGS_target_redis_server_addresses.empty()) {
    LOG(INFO) << "Ignoring FLAGS_table_name. Redis proxy expects table name to be .redis";
    FLAGS_table_name = ".redis";
  }
  const YBTableName table_name(FLAGS_table_name);

  if (!YBTableExistsAlready(client, table_name) || DropTableIfNecessary(client, table_name)) {
    CreateTable(table_name, client);
  }
}

vector<const YBPartialRow *> GetSplitsForTable(const YBSchema *schema) {
  // Create the number of partitions based on the split keys.
  vector<const YBPartialRow *> splits;
  for (uint64_t j = 1; j < FLAGS_num_tablets; j++) {
    YBPartialRow *row = schema->NewRow();
    // We divide the interval between 0 and 2**64 into the requested number of intervals.
    string split_key = FormatHexForLoadTestKey(((uint64_t)1 << 62) * 4.0 * j / (FLAGS_num_tablets));
    LOG(INFO) << "split_key #" << j << "=" << split_key;
    CHECK_OK(row->SetBinaryCopy(0, split_key));
    splits.push_back(row);
  }
  return splits;
}

void CreateTable(const YBTableName &table_name, const shared_ptr<YBClient> &client) {
  if (!FLAGS_target_redis_server_addresses.empty()) {
    CreateRedisTable(table_name, client);
  } else {
    CreateYBTable(table_name, client);
  }

  // Workaround for ENG-516. After the table is created, wait a few seconds for leader balancing
  // to settle down before processing IO operations.
  LOG(INFO) << "Sleeping 10 seconds for leader balancing operations to settle.";
  sleep(10);
}

void CreateRedisTable(const YBTableName &table_name, const shared_ptr<YBClient> &client) {
  LOG(INFO) << "Creating table with " << FLAGS_num_tablets << " hash based partitions.";
  gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
  Status table_creation_status = table_creator->table_name(table_name)
                                     .num_tablets(FLAGS_num_tablets)
                                     .num_replicas(FLAGS_num_replicas)
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

void CreateYBTable(const YBTableName &table_name, const shared_ptr<YBClient> &client) {
  LOG(INFO) << "Building schema";
  YBSchemaBuilder schemaBuilder;
  schemaBuilder.AddColumn("k")->PrimaryKey()->Type(yb::BINARY)->NotNull();
  schemaBuilder.AddColumn("v")->Type(yb::BINARY)->NotNull();
  YBSchema schema;
  CHECK_OK(schemaBuilder.Build(&schema));

  vector<const YBPartialRow *> splits = GetSplitsForTable(&schema);

  LOG(INFO) << "Creating table";
  gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
  Status table_creation_status =
      table_creator->table_name(table_name)
          .schema(&schema)
          .split_rows(splits)
          .num_replicas(FLAGS_num_replicas)
          .table_type(
              FLAGS_use_kv_table ? yb::client::YBTableType::YQL_TABLE_TYPE
                                 : yb::client::YBTableType::KUDU_COLUMNAR_TABLE_TYPE)
          .Create();
  if (!table_creation_status.ok()) {
    LOG(INFO) << "Table creation status message: " << table_creation_status.message().ToString();
  }
  if (table_creation_status.message().ToString().find("Table already exists") ==
      std::string::npos) {
    CHECK_OK(table_creation_status);
  }
}

bool YBTableExistsAlready(const shared_ptr<YBClient> &client, const YBTableName &table_name) {
  LOG(INFO) << "Checking if table '" << table_name.ToString() << "' already exists";
  {
    YBSchema existing_schema;
    if (client->GetTableSchema(table_name, &existing_schema).ok()) {
      LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
      return true;
    } else {
      LOG(INFO) << "Table '" << table_name.ToString() << "' does not exist yet";
      return false;
    }
  }
}

bool DropTableIfNecessary(const shared_ptr<YBClient> &client, const YBTableName &table_name) {
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
                               FLAGS_retries_on_empty_read);

    reader.Start();

    writer.WaitForCompletion();

    // The reader will not stop on its own, so we stop it as soon as the writer stops.
    reader.Stop();
    reader.WaitForCompletion();
  }
}
