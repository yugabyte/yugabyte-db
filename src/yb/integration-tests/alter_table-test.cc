// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>
#include <future>
#include <map>
#include <string>
#include <utility>

#include "yb/util/flags.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/consensus/log.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/faststring.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/metrics.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(enable_data_block_fsync);
DECLARE_bool(enable_maintenance_manager);
DECLARE_bool(enable_ysql);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(use_hybrid_clock);
DECLARE_int32(ht_lease_duration_ms);
DECLARE_int32(replication_factor);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(catalog_manager_report_batch_size);
DECLARE_string(TEST_block_alter_table);
DECLARE_bool(TEST_fail_alter_table_after_commit);
DECLARE_bool(TEST_fail_alter_table_sys_catalog_write);

METRIC_DECLARE_counter(sys_catalog_peer_write_count);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTableType;
using std::shared_ptr;
using std::map;
using std::pair;
using std::vector;
using std::string;
using std::max;
using tablet::TabletPeer;

MATCHER_P(EqualsCode, expected_status, "") {
  return arg.code() == expected_status.code();
}

class AlterTableTest : public YBMiniClusterTestBase<MiniCluster>,
                       public ::testing::WithParamInterface<int> {
 public:
  AlterTableTest()
    : stop_threads_(false),
      inserted_idx_(0) {

    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("c1")->Type(DataType::INT32)->NotNull();
    CHECK_OK(b.Build(&schema_));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_data_block_fsync) = false; // Keep unit tests fast.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_hybrid_clock) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ht_lease_duration_ms) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = false;
    ANNOTATE_BENIGN_RACE(&FLAGS_enable_maintenance_manager,
                         "safe to change at runtime");
  }

  void SetUp() override {
    // Make heartbeats faster to speed test runtime.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_report_batch_size) = GetParam();

    YBMiniClusterTestBase::SetUp();

    MiniClusterOptions opts;
    opts.num_tablet_servers = num_replicas();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = num_replicas();
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(cluster_->WaitForTabletServerCount(num_replicas()));

    client_ = CHECK_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
        .default_admin_operation_timeout(MonoDelta::FromSeconds(60))
        .Build());

    CHECK_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                 kTableName.namespace_type()));

    // Add a table, make sure it reports itself.
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    CHECK_OK(table_creator->table_name(kTableName)
             .schema(&schema_)
             .table_type(YBTableType::YQL_TABLE_TYPE)
             .num_tablets(1)
             .Create());

    if (num_replicas() == 1) {
      tablet_peer_ = LookupTabletPeer();
    }
    LOG(INFO) << "Tablet successfully located";
  }

  void DoTearDown() override {
    client_.reset();
    tablet_peer_.reset();
    cluster_->Shutdown();
  }

  std::shared_ptr<TabletPeer> LookupTabletPeer() {
    auto peers = cluster_->mini_tablet_server(0)->server()->tablet_manager()->GetTabletPeers();
    return peers[0];
  }

  void ShutdownTS(int idx = 0) {
    // Drop the tablet_peer_ reference since the tablet peer becomes invalid once
    // we shut down the server. Additionally, if we hold onto the reference,
    // we'll end up calling the destructor from the test code instead of the
    // normal location, which can cause crashes, etc.
    tablet_peer_.reset();
    if (cluster_->mini_tablet_server(idx)->server() != nullptr) {
      cluster_->mini_tablet_server(idx)->Shutdown();
    }
  }

  void RestartTabletServer(int idx = 0) {
    tablet_peer_.reset();
    if (cluster_->mini_tablet_server(idx)->server()) {
      ASSERT_OK(cluster_->mini_tablet_server(idx)->Restart());
    } else {
      ASSERT_OK(cluster_->mini_tablet_server(idx)->Start(tserver::WaitTabletsBootstrapped::kFalse));
    }

    ASSERT_OK(cluster_->mini_tablet_server(idx)->WaitStarted());
    if (idx == 0) {
      tablet_peer_ = LookupTabletPeer();
    }
  }

  Status WaitAlterTableCompletion(const YBTableName& table_name, int attempts) {
    int wait_time = 1000;
    for (int i = 0; i < attempts; ++i) {
      bool in_progress;
      string table_id;
      RETURN_NOT_OK(client_->IsAlterTableInProgress(table_name, table_id, &in_progress));
      if (!in_progress) {
        return Status::OK();
      }

      SleepFor(MonoDelta::FromMicroseconds(wait_time));
      wait_time = std::min(wait_time * 5 / 4, 1000000);
    }

    return STATUS(TimedOut, "AlterTable not completed within the timeout");
  }

  Status AddNewI32Column(const YBTableName& table_name,
                         const string& column_name) {
    return AddNewI32Column(table_name, column_name, MonoDelta::FromSeconds(60));
  }

  Status AddNewI32Column(const YBTableName& table_name,
                         const string& column_name,
                         const MonoDelta& timeout) {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(table_name));
    table_alterer->AddColumn(column_name)->Type(DataType::INT32)->NotNull();
    return table_alterer->timeout(timeout)->Alter();
  }

  enum VerifyPattern {
    C1_MATCHES_INDEX,
    C1_IS_DEADBEEF,
    C1_DOESNT_EXIST
  };

  void VerifyRows(int start_row, int num_rows, VerifyPattern pattern);

  void InsertRows(int start_row, int num_rows);

  void UpdateRow(int32_t row_key, const map<string, int32_t>& updates);

  std::vector<std::string> ScanToStrings();

  void WriteThread(QLWriteRequestPB::QLStmtType type);
  void ScannerThread();

  Status CreateTable(const YBTableName& table_name) {
    RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(table_name.namespace_name(),
                                                      table_name.namespace_type()));

    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    return table_creator->table_name(table_name)
        .schema(&schema_)
        .num_tablets(10)
        .Create();
  }

  int64_t GetSysCatalogWrites() {
    auto GetSysCatalogMetric = [&](CounterPrototype& prototype) -> int64_t {
      auto metrics = cluster_->mini_master()->sys_catalog().GetMetricEntity();
      return prototype.Instantiate(metrics)->value();
    };
    return GetSysCatalogMetric(METRIC_sys_catalog_peer_write_count);
  }

  Result<bool> TableHasColumn(const YBTableName& table_name, const string& column_name) {
    YBSchema schema;
    dockv::PartitionSchema partition_schema;
    RETURN_NOT_OK(client_->GetTableSchema(table_name, &schema, &partition_schema));
    return std::ranges::any_of(schema.columns(), [&](const auto& column) {
      return column.name() == column_name;
    });
  }

  Result<bool> TableNameResolves(const YBTableName& table_name) {
    YBSchema schema;
    dockv::PartitionSchema partition_schema;
    Status s = client_->GetTableSchema(table_name, &schema, &partition_schema);
    if (s.ok()) {
      return true;
    }
    if (s.IsNotFound()) {
      return false;
    }
    return s;
  }

 protected:
  virtual int num_replicas() const { return 1; }

  static const YBTableName kTableName;

  std::unique_ptr<YBClient> client_;

  YBSchema schema_;

  std::shared_ptr<TabletPeer> tablet_peer_;

  AtomicBool stop_threads_;

  // The index of the last row inserted by InserterThread.
  // UpdaterThread uses this to figure out which rows can be
  // safely updated.
  AtomicInt<int32_t> inserted_idx_;
};

// Subclass which creates three servers and a replicated cluster.
class ReplicatedAlterTableTest : public AlterTableTest {
 protected:
  virtual int num_replicas() const override { return 3; }
};

// Subclass for the tests that target the master-side windows of an alter, from the name
// reservation through the sys-catalog write and the in-memory commit. The tablet-report batch
// size that parameterizes AlterTableTest does not affect these tests, so they run at a
// single value.
class AlterTableSysCatalogWriteTest : public AlterTableTest {};

const YBTableName AlterTableTest::kTableName(YQL_DATABASE_CQL, "my_keyspace", "fake-table");

INSTANTIATE_TEST_CASE_P(BatchSize, AlterTableTest, ::testing::Values(1, 10));
INSTANTIATE_TEST_CASE_P(BatchSize, ReplicatedAlterTableTest, ::testing::Values(1, 10));
INSTANTIATE_TEST_CASE_P(
    SingleBatchSize, AlterTableSysCatalogWriteTest, ::testing::Values(1));

// Simple test to verify that the "alter table" command sent and executed
// on the TS handling the tablet of the altered table.
// TODO: create and verify multiple tablets when the client will support that.
TEST_P(AlterTableTest, TestTabletReports) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());
  ASSERT_OK(AddNewI32Column(kTableName, "new-i32"));
  ASSERT_EQ(1, tablet->metadata()->primary_table_schema_version());
}

// Verify that adding an existing column will return an "already present" error
TEST_P(AlterTableTest, TestAddExistingColumn) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());

  {
    Status s = AddNewI32Column(kTableName, "c1");
    ASSERT_TRUE(s.IsAlreadyPresent());
    ASSERT_STR_CONTAINS(s.ToString(), "The column already exists: c1");
  }

  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());
}

// Adding a nullable column with no default value should be equivalent
// to a NULL default.
TEST_P(AlterTableTest, TestAddNullableColumnWithoutDefault) {
  InsertRows(0, 1);
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    table_alterer->AddColumn("new")->Type(DataType::INT32);
    ASSERT_OK(table_alterer->Alter());
  }

  InsertRows(1, 1);

  vector<string> rows = ScanToStrings();
  EXPECT_EQ(2, rows.size());
  EXPECT_EQ("{ int32:0, int32:0, null }", rows[0]);
  EXPECT_EQ("{ int32:16777216, int32:1, null }", rows[1]);
}

// Verify that, if a tablet server is down when an alter command is issued,
// it will eventually receive the command when it restarts.
TEST_P(AlterTableTest, TestAlterOnTSRestart) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());

  ShutdownTS();

  // Send the Alter request
  {
    Status s = AddNewI32Column(kTableName, "new-32", MonoDelta::FromMilliseconds(500));
    ASSERT_TRUE(s.IsTimedOut());
  }

  LOG(INFO) << "Original " << schema_.ToString();
  // Verify that the Schema is the new one.
  YBSchema schema;
  dockv::PartitionSchema partition_schema;
  bool alter_in_progress = false;
  string table_id;
  ASSERT_OK(client_->GetTableSchema(kTableName, &schema, &partition_schema));
  LOG(INFO) << "Got " << schema.ToString();
  ASSERT_EQ(3, schema.num_columns()); // New schema.

  ASSERT_OK(client_->IsAlterTableInProgress(kTableName, table_id, &alter_in_progress));
  ASSERT_TRUE(alter_in_progress);

  // Restart the TS and wait for the new schema
  RestartTabletServer();
  ASSERT_OK(WaitAlterTableCompletion(kTableName, 50));
  tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(1, tablet->metadata()->primary_table_schema_version());
}

// Verify that nothing is left behind on cluster shutdown with pending async tasks
TEST_P(AlterTableTest, TestShutdownWithPendingTasks) {
  DontVerifyClusterBeforeNextTearDown();
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());

  ShutdownTS();

  // Send the Alter request
  {
    Status s = AddNewI32Column(kTableName, "new-i32", MonoDelta::FromMilliseconds(500));
    ASSERT_TRUE(s.IsTimedOut());
  }
}

// Verify that the new schema is applied/reported even when
// the TS is going down with the alter operation in progress.
// On TS restart the master should:
//  - get the new schema state, and mark the alter as complete
//  - get the old schema state, and ask the TS again to perform the alter.
TEST_P(AlterTableTest, TestRestartTSDuringAlter) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping slow test";
    return;
  }

  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(0, tablet->metadata()->primary_table_schema_version());

  Status s = AddNewI32Column(kTableName, "new-i32", MonoDelta::FromMilliseconds(1));
  ASSERT_TRUE(s.IsTimedOut());

  // Restart the TS while alter is running
  for (int i = 0; i < 3; i++) {
    SleepFor(MonoDelta::FromMicroseconds(500));
    RestartTabletServer();
  }

  // Wait for the new schema
  ASSERT_OK(WaitAlterTableCompletion(kTableName, 50));
  ASSERT_EQ(1, tablet->metadata()->primary_table_schema_version());
}

TEST_P(AlterTableTest, TestGetSchemaAfterAlterTable) {
  ASSERT_OK(AddNewI32Column(kTableName, "new-i32"));

  YBSchema s;
  dockv::PartitionSchema partition_schema;
  ASSERT_OK(client_->GetTableSchema(kTableName, &s, &partition_schema));
}

void AlterTableTest::InsertRows(int start_row, int num_rows) {
  auto session = client_->NewSession(15s);
  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));
  std::vector<std::shared_ptr<client::YBqlOp>> ops;

  // Insert a bunch of rows with the current schema
  for (int i = start_row; i < start_row + num_rows; i++) {
    auto op = table.NewInsertOp(session->arena());
    // Endian-swap the key so that we spew inserts randomly
    // instead of just a sequential write pattern. This way
    // compactions may actually be triggered.
    int32_t key = bswap_32(i);
    auto req = op->mutable_request();
    QLAddInt32HashValue(req, key);

    if (table.schema().num_columns() > 1) {
      table.AddInt32ColumnValue(req, table.schema().columns()[1].name(), i);
    }

    ops.push_back(op);
    session->Apply(op);

    if (ops.size() >= 50) {
      FlushSessionOrDie(session, ops);
      ops.clear();
    }
  }

  FlushSessionOrDie(session, ops);
}

void AlterTableTest::UpdateRow(int32_t row_key,
                               const map<string, int32_t>& updates) {
  auto session = client_->NewSession(15s);

  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));

  auto update = table.NewUpdateOp(session->arena());
  int32_t key = bswap_32(row_key); // endian swap to match 'InsertRows'
  QLAddInt32HashValue(update->mutable_request(), key);
  for (const auto& e : updates) {
    table.AddInt32ColumnValue(update->mutable_request(), e.first, e.second);
  }
  session->Apply(update);
  FlushSessionOrDie(session);
}

std::vector<string> AlterTableTest::ScanToStrings() {
  return ScanTableToStrings(kTableName, client_.get());
}

// Verify that the 'num_rows' starting with 'start_row' fit the given pattern.
// Note that the 'start_row' here is not a row key, but the pre-transformation row
// key (InsertRows swaps endianness so that we random-write instead of sequential-write)
void AlterTableTest::VerifyRows(int start_row, int num_rows, VerifyPattern pattern) {
  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));

  int verified = 0;
  for (const auto& row : client::TableRange(table)) {
    int32_t key = row.column(0).int32_value();
    int32_t row_idx = bswap_32(key);
    if (row_idx < start_row || row_idx >= start_row + num_rows) {
      // Outside the range we're verifying
      continue;
    }
    verified++;

    switch (pattern) {
      case C1_MATCHES_INDEX:
        ASSERT_EQ(row_idx, row.column(1).int32_value());
        break;
      case C1_IS_DEADBEEF:
        ASSERT_TRUE(row.column(1).IsNull());
        break;
      case C1_DOESNT_EXIST:
        continue;
      default:
        ASSERT_TRUE(false) << "Invalid pattern: " << pattern;
        break;
    }
  }
  ASSERT_EQ(verified, num_rows);
}

// Test inserting/updating some data, dropping a column, and adding a new one
// with the same name. Data should not "reappear" from the old column.
//
// This is a regression test for KUDU-461.
TEST_P(AlterTableTest, TestDropAndAddNewColumn) {
  // Reduce flush threshold so that we get both on-disk data
  // for the alter as well as in-MRS data.
  // This also increases chances of a race.
  const int kNumRows = AllowSlowTests() ? 100000 : 1000;
  InsertRows(0, kNumRows);

  LOG(INFO) << "Verifying initial pattern";
  VerifyRows(0, kNumRows, C1_MATCHES_INDEX);

  LOG(INFO) << "Dropping and adding back c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  ASSERT_OK(AddNewI32Column(kTableName, "c1"));

  LOG(INFO) << "Verifying that the new default shows up";
  VerifyRows(0, kNumRows, C1_IS_DEADBEEF);
}

TEST_P(AlterTableTest, DISABLED_TestCompactionAfterDrop) {
  LOG(INFO) << "Inserting rows";
  InsertRows(0, 3);

  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  std::string docdb_dump = tablet->TEST_DocDBDumpStr();
  // DocDB should not be empty right now.
  ASSERT_NE(0, docdb_dump.length());

  LOG(INFO) << "Dropping c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  LOG(INFO) << "Forcing compaction";
  CHECK_OK(tablet->ForceManualRocksDBCompact());

  docdb_dump = tablet->TEST_DocDBDumpStr();

  LOG(INFO) << "Checking that docdb is empty";
  ASSERT_EQ("", docdb_dump);

  ASSERT_OK(cluster_->RestartSync());
  tablet_peer_ = LookupTabletPeer();
}

// This tests the scenario where the log entries immediately after last RocksDB flush are for a
// different schema than the one that was last flushed to the superblock.
TEST_P(AlterTableTest, TestLogSchemaReplay) {
  ASSERT_OK(AddNewI32Column(kTableName, "c2"));
  InsertRows(0, 2);
  UpdateRow(1, { {"c1", 0} });

  LOG(INFO) << "Flushing RocksDB";
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  UpdateRow(0, { {"c1", 1}, {"c2", 10001} });

  LOG(INFO) << "Dropping c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  UpdateRow(1, { {"c2", 10002} });

  auto rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, int32:10001 }", rows[0]);
  ASSERT_EQ("{ int32:16777216, int32:10002 }", rows[1]);

  google::FlagSaver flag_saver;
  // Restart without flushing RocksDB
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
  LOG(INFO) << "Restarting tablet";
  ASSERT_NO_FATALS(RestartTabletServer());

  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, int32:10001 }", rows[0]);
  ASSERT_EQ("{ int32:16777216, int32:10002 }", rows[1]);
}

// Tests that a renamed table can still be altered. This is a regression test, we used to not carry
// over column ids after a table rename.
TEST_P(AlterTableTest, TestRenameTableAndAdd) {
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  YBTableName new_name(kTableName.namespace_type(), kTableName.namespace_name(), "someothername");
  ASSERT_OK(table_alterer->RenameTo(new_name)
            ->Alter());

  // After a successful rename the old name must not resolve. The commit erased it.
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));

  ASSERT_OK(AddNewI32Column(new_name, "new"));
}

// An ALTER TABLE whose sys-catalog write fails must surface an error instead of wedging the
// master. If AlterTable held CatalogManager::mutex_ across the write, the failure-path rollback
// would re-acquire that same non-recursive mutex and self-deadlock, so the write must run with
// mutex_ released.
//
// This is the rename variant. The new name is reserved under mutex_ before the write. After the
// injected failure the reservation must be rolled back, and the old name must still resolve
// because only a commit erases it.
TEST_P(AlterTableSysCatalogWriteTest, TestRenameSysCatalogWriteFailureDoesNotDeadlock) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_sys_catalog_write) = true;

  YBTableName new_name(kTableName.namespace_type(), kTableName.namespace_name(), "renamed-table");
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  Status s = table_alterer->RenameTo(new_name)->timeout(MonoDelta::FromSeconds(30))->Alter();
  // The injected write failure must surface as an error instead of hanging.
  ASSERT_NOK(s);

  // Confirm mutex_ is not wedged. A subsequent alter acquires the same mutex and must complete.
  // The old name must still resolve because a failure never erases it, and the reserved new name
  // must be rolled back.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_sys_catalog_write) = false;
  ASSERT_OK(AddNewI32Column(kTableName, "added-after-rename-failure"));

  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // The rolled-back name is free for a fresh table.
  ASSERT_OK(CreateTable(new_name));
}

// The non-rename variant of the sys-catalog write-failure test above. A column add reserves no
// name, so its failure path must not touch the by-name map. A rollback that erases a name anyway
// hits the table's live entry and drops its mapping. The test verifies that the table stays
// reachable by its original name after an injected write failure.
TEST_P(AlterTableSysCatalogWriteTest, TestAddColumnSysCatalogWriteFailureKeepsNameMapping) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_sys_catalog_write) = true;

  Status s = AddNewI32Column(kTableName, "doomed-column", MonoDelta::FromSeconds(30));
  ASSERT_NOK(s);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_sys_catalog_write) = false;

  // The live mapping must be intact. A name lookup and a subsequent alter both succeed.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_OK(AddNewI32Column(kTableName, "added-after-add-failure"));
}

// The window tests below park an alter mid-flight with FLAGS_TEST_block_alter_table and drive
// concurrent client operations through the open window. "sys_catalog_write" parks the alter
// after it has released CatalogManager::mutex_ and before it starts the sys-catalog write,
// holding the table's COW write lock. That is the exact state a concurrent RPC observes while a
// real write is in flight. "post_name_reservation" parks a rename after it has reserved its new
// name and released mutex_, before it takes the COW write lock. That is the state a concurrent
// RPC observes between the reservation and the lock, when the rename holds no lock at all.

// Catalog operations must not block while an alter's sys-catalog write is in flight, because
// mutex_ is released across the write. While the alter is parked, a schema read of the altering
// table returns its committed schema without the new column, a listing shows the table once, and
// a CREATE of an unrelated table completes. Each of these operations acquires mutex_, so if an
// alter ever again held mutex_ across the write, they would block behind the parked alter and
// this test would time out.
TEST_P(AlterTableSysCatalogWriteTest, TestCatalogOpsProceedWhileAlterWritesSysCatalog) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "sys_catalog_write";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto alter_future = std::async(std::launch::async, [&] {
    return AddNewI32Column(kTableName, "column-added-during-park");
  });
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  ASSERT_FALSE(ASSERT_RESULT(TableHasColumn(kTableName, "column-added-during-park")));

  auto tables = ASSERT_RESULT(client_->ListTables());
  ASSERT_EQ(1, std::ranges::count_if(tables, [](const YBTableName& name) {
    return name.table_name() == kTableName.table_name();
  }));

  YBTableName other_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "created-during-park");
  ASSERT_OK(CreateTable(other_name));

  // The alter is still parked, so the operations above overlapped its write window instead of
  // running after it completed.
  ASSERT_EQ(alter_future.wait_for(0s), std::future_status::timeout);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  ASSERT_OK(alter_future.get());
  ASSERT_TRUE(ASSERT_RESULT(TableHasColumn(kTableName, "column-added-during-park")));
}

// A YCQL rename reserves the new name in the by-name map before mutex_ is released and erases
// the old name only after the in-memory commit. While the rename is parked before its write,
// both names resolve to the same table, a CREATE targeting either name is rejected with
// AlreadyPresent, and a listing shows the table exactly once, under its committed old name.
// After the rename commits, the old name is free and a CREATE reusing it succeeds.
TEST_P(AlterTableSysCatalogWriteTest, TestRenameWindowBothNamesResolve) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "renamed-during-park");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "sys_catalog_write";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    return table_alterer->RenameTo(new_name)->Alter();
  });
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  // The reservation that blocks CREATE collisions is an ordinary by-name map entry, and name
  // lookups read through it, so the new name resolves before the rename commits. If the rename
  // fails, the rollback removes the reservation and the new name stops resolving.
  auto info_by_old_name = ASSERT_RESULT(client_->GetYBTableInfo(kTableName));
  auto info_by_new_name = ASSERT_RESULT(client_->GetYBTableInfo(new_name));
  ASSERT_EQ(info_by_old_name.table_id, info_by_new_name.table_id);

  // A lookup through the reservation returns committed table state. The returned identifier
  // carries the committed old name, not the uncommitted new one.
  ASSERT_EQ(kTableName.table_name(), info_by_new_name.table_name.table_name());

  // The listing iterates the by-id map and reports committed names, so the renaming table shows
  // up exactly once, under the old name.
  auto tables = ASSERT_RESULT(client_->ListTables());
  ASSERT_EQ(1, std::ranges::count_if(tables, [](const YBTableName& name) {
    return name.table_name() == kTableName.table_name();
  }));
  ASSERT_EQ(0, std::ranges::count_if(tables, [&](const YBTableName& name) {
    return name.table_name() == new_name.table_name();
  }));

  ASSERT_THAT(CreateTable(new_name), EqualsCode(STATUS(AlreadyPresent, "")));
  ASSERT_THAT(CreateTable(kTableName), EqualsCode(STATUS(AlreadyPresent, "")));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  ASSERT_OK(rename_future.get());

  // The commit erased the old name and the table stays reachable under the new one, so the old
  // name is free for a fresh table.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(new_name)));
  ASSERT_OK(CreateTable(kTableName));
}

// A DROP that arrives through the old name while the rename is parked resolves the table and
// then waits on its COW write lock. The drop must not complete while the rename holds that
// lock, and once the rename commits the drop deletes the renamed table, so afterwards neither
// name resolves. The rename's post-commit erase removes the old name and the drop removes the
// new name, so this interleaving leaves nothing behind in the by-name map.
//
// TODO: The delete path resolves the name before it takes any lock and never
// revalidates it, so the drop deletes the renamed table through its stale old name. A fix that
// revalidates the name once the delete holds the table lock would reject the stale old name,
// and the assertions below should flip with it.
TEST_P(AlterTableSysCatalogWriteTest, TestDropByOldNameWaitsForRenameCommit) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "renamed-before-drop");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "sys_catalog_write";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");
  StringWaiterLogSink drop_started_sink("Servicing DeleteTable request");

  auto rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    // The drop deletes the table as soon as the rename commits, so waiting for the new schema to
    // reach the tablets cannot succeed. Return once the master has committed the rename.
    return table_alterer->RenameTo(new_name)->wait(false)->Alter();
  });
  std::future<Status> drop_future;  // Assigned once the rename is parked.
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  drop_future = std::async(std::launch::async, [&] {
    return client_->DeleteTable(kTableName);
  });

  // Once the master logs the drop it resolves the old name within the same handler, with mutex_
  // free, and then waits on the COW write lock that the parked rename holds. Give it a moment to
  // prove it stays blocked.
  ASSERT_OK(drop_started_sink.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(drop_future.wait_for(1s * kTimeMultiplier), std::future_status::timeout);

  // The drop waits on the COW write lock without holding mutex_. An unrelated CREATE acquires
  // mutex_ and completes while the drop stays blocked.
  YBTableName probe_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "created-during-blocked-drop");
  ASSERT_OK(CreateTable(probe_name));
  ASSERT_EQ(drop_future.wait_for(0s), std::future_status::timeout);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  ASSERT_OK(rename_future.get());
  ASSERT_OK(drop_future.get());

  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // A leftover entry in the by-name map would fail a CREATE with AlreadyPresent. Both creates
  // succeeding proves the rename erased the old name and the drop erased the new one.
  ASSERT_OK(CreateTable(kTableName));
  ASSERT_OK(CreateTable(new_name));
}

// Two renames of the same table can be in flight at once. The first rename holds the table's
// COW write lock across its sys-catalog write. The second rename reserves its own new name
// under mutex_ and then waits for the COW lock with no lock held, so catalog operations proceed
// while it waits and its reserved name resolves. Once the first rename commits, the second
// rename applies on top of it. Each rename erases the name that was committed when it acquired
// the lock, so every name the table passed through frees up for reuse.
TEST_P(AlterTableSysCatalogWriteTest, TestQueuedRenameWaitsWithoutBlockingCatalogOps) {
  YBTableName first_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "renamed-once");
  YBTableName second_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "renamed-twice");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "sys_catalog_write";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto first_rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    // The second rename changes the name again before the tablets report, so polling for this
    // alter to finish by name cannot succeed. Return once the master has committed the rename.
    return table_alterer->RenameTo(first_name)->wait(false)->Alter();
  });
  std::future<Status> second_rename_future;  // Assigned once the first rename is parked.
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  second_rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    return table_alterer->RenameTo(second_name)->wait(false)->Alter();
  });

  // The second rename reserves its new name under mutex_ and then waits for the COW write lock
  // that the parked rename holds. The reservation resolving proves the second rename is past
  // its mutex_ scope. If the second rename held mutex_ while waiting for the COW lock, this
  // schema read would block behind it and the wait would time out.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return TableNameResolves(second_name);
  }, 30s * kTimeMultiplier, "second rename reserves its new name"));

  // The committed name and both reservations point at the same table.
  auto info_committed = ASSERT_RESULT(client_->GetYBTableInfo(kTableName));
  auto info_first = ASSERT_RESULT(client_->GetYBTableInfo(first_name));
  auto info_second = ASSERT_RESULT(client_->GetYBTableInfo(second_name));
  ASSERT_EQ(info_committed.table_id, info_first.table_id);
  ASSERT_EQ(info_committed.table_id, info_second.table_id);

  // Catalog operations proceed while the second rename waits. A listing shows the table exactly
  // once, under its committed name, and a CREATE of an unrelated table completes. Both acquire
  // mutex_, so they would block if either in-flight rename held it.
  auto tables = ASSERT_RESULT(client_->ListTables());
  ASSERT_EQ(1, std::ranges::count_if(tables, [](const YBTableName& name) {
    return name.table_name() == kTableName.table_name();
  }));
  YBTableName other_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "created-during-queued-rename");
  ASSERT_OK(CreateTable(other_name));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  ASSERT_OK(first_rename_future.get());
  ASSERT_OK(second_rename_future.get());

  // The first rename erased the original name. The second rename erased the first new name,
  // which was the committed name when it acquired the lock. Only the final name resolves, and
  // both earlier names are free for fresh tables. A rename that erased the name its request
  // carried instead of the committed name would leave the first new name behind.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(second_name)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(first_name)));
  ASSERT_OK(CreateTable(kTableName));
  ASSERT_OK(CreateTable(first_name));

  // The renamed table is fully usable. This alter waits for the tablets to apply the schema, so
  // the cluster is settled before teardown.
  ASSERT_OK(AddNewI32Column(second_name, "added-after-queued-renames"));
}

// An alter that carries both a rename and a failing option must not leak the reserved new name.
// wal_retention_secs cannot be combined with other changes, so a request that carries both a
// rename and wal_retention_secs fails validation after the new name is reserved in the by-name
// map. The failure must erase the reservation. A leaked entry would keep the new name resolving
// to this table and would fail any CREATE targeting the name with AlreadyPresent until a leader
// change rebuilds the map.
TEST_P(AlterTableSysCatalogWriteTest, TestFailedRenameDoesNotLeakReservedName) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "never-renamed-to");

  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  Status s = table_alterer->RenameTo(new_name)->SetWalRetentionSecs(3600)->Alter();
  ASSERT_THAT(s, EqualsCode(STATUS(InvalidArgument, "")));

  // The old name stays live and the reservation is rolled back.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // The rolled-back name is free for a fresh table.
  ASSERT_OK(CreateTable(new_name));
}

// The alter-step variant of the leak test above. The rename target is free, so the reservation
// succeeds, and the invalid alter step fails the request after it, inside the schema-change
// application rather than in option validation. The failure must erase the reservation.
TEST_P(AlterTableSysCatalogWriteTest, TestFailedAlterStepReleasesReservedName) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "never-renamed-either");

  // Dropping a column that does not exist fails schema-change application.
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  table_alterer->DropColumn("no-such-column");
  Status s = table_alterer->RenameTo(new_name)->Alter();
  ASSERT_THAT(s, EqualsCode(STATUS(NotFound, "")));

  // The old name stays live and the reservation is rolled back.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // The rolled-back name is free for a fresh table.
  ASSERT_OK(CreateTable(new_name));
}

// A rename checks its target name for collisions and reserves it before the table's COW write
// lock is taken, so the collision check runs before the request's alter steps are validated. A
// request that combines a rename to a taken name with an invalid alter step reports the
// collision. This pins that order deliberately. The collision cannot be checked later than the
// reservation, and the reservation precedes the COW lock so that mutex_ and that lock are never
// held together.
TEST_P(AlterTableSysCatalogWriteTest, TestRenameCollisionCheckedBeforeAlterSteps) {
  YBTableName taken_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "taken-name");
  ASSERT_OK(CreateTable(taken_name));

  // Dropping a column that does not exist is an invalid step. The collision must win.
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  table_alterer->DropColumn("no-such-column");
  Status s = table_alterer->RenameTo(taken_name)->Alter();
  ASSERT_THAT(s, EqualsCode(STATUS(AlreadyPresent, "")));

  // Nothing changed. Each name still resolves to its own table.
  auto info_original = ASSERT_RESULT(client_->GetYBTableInfo(kTableName));
  auto info_taken = ASSERT_RESULT(client_->GetYBTableInfo(taken_name));
  ASSERT_NE(info_original.table_id, info_taken.table_id);
}

// A rename whose target is the table's own current name collides with the table itself in the
// by-name map. The collision is reported before any reservation is made, so the failure path
// has nothing to erase and must not touch the live entry. A rollback that erased the entry
// anyway would drop the table's only name mapping.
TEST_P(AlterTableSysCatalogWriteTest, TestRenameToSameNameKeepsTableReachable) {
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  Status s = table_alterer->RenameTo(kTableName)->Alter();
  ASSERT_THAT(s, EqualsCode(STATUS(AlreadyPresent, "")));

  // The table's one name mapping is intact. A lookup and a subsequent alter both succeed.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_OK(AddNewI32Column(kTableName, "added-after-self-rename"));
}

// The name reservation rejects a concurrent rename the same way it rejects a concurrent
// CREATE. While one table's rename is parked before its sys-catalog write, holding the
// reservation, a rename of a second table to the same name fails with AlreadyPresent against
// the reservation. The check runs in the second rename's own mutex_ scope, so it completes
// while the first rename is parked. Once the first rename commits, the name belongs to the
// renamed table and its vacated old name is free.
TEST_P(AlterTableSysCatalogWriteTest, TestRenameToReservedNameRejected) {
  YBTableName contested_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "contested-name");
  YBTableName other_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "other-renamer");
  ASSERT_OK(CreateTable(other_name));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "sys_catalog_write";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    return table_alterer->RenameTo(contested_name)->Alter();
  });
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  // The second rename fails against the reservation, before it reaches its own park point, so
  // this call returns while the first rename is still parked.
  std::unique_ptr<YBTableAlterer> other_alterer(client_->NewTableAlterer(other_name));
  Status s = other_alterer->RenameTo(contested_name)->Alter();
  ASSERT_THAT(s, EqualsCode(STATUS(AlreadyPresent, "")));

  // The failed rename changed nothing. The other table stays reachable by its own name, and the
  // contested name still resolves to the renaming table.
  auto info_other = ASSERT_RESULT(client_->GetYBTableInfo(other_name));
  auto info_contested = ASSERT_RESULT(client_->GetYBTableInfo(contested_name));
  auto info_renaming = ASSERT_RESULT(client_->GetYBTableInfo(kTableName));
  ASSERT_EQ(info_contested.table_id, info_renaming.table_id);
  ASSERT_NE(info_contested.table_id, info_other.table_id);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  ASSERT_OK(rename_future.get());

  // The parked rename committed. The contested name belongs to the renamed table and the old
  // name is free for a fresh table.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(contested_name)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_OK(CreateTable(kTableName));
}

// A rename holds no lock between reserving its new name and taking the table's COW write lock.
// A DROP of the table through its committed old name in that window runs to completion, because
// nothing blocks it, and it erases only the committed name. The reservation stays behind,
// pointing at the deleted table. The rename then finds the table deleted and fails, and the
// failure must erase the reservation. A reservation left behind would fail every CREATE of that
// name with AlreadyPresent even though the table is gone.
TEST_P(AlterTableSysCatalogWriteTest, TestDropByOldNameDuringReservationWindow) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "reserved-then-dropped");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "post_name_reservation";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    return table_alterer->RenameTo(new_name)->Alter();
  });
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  // The parked rename holds neither mutex_ nor the table's COW write lock, so the drop runs to
  // completion while the rename is parked.
  ASSERT_OK(client_->DeleteTable(kTableName));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  // The rename finds the table deleted when it takes the lock.
  ASSERT_THAT(rename_future.get(), EqualsCode(STATUS(NotFound, "")));

  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // The drop erased the committed name and the rename's failure erased the reservation. Both
  // names are free for fresh tables.
  ASSERT_OK(CreateTable(kTableName));
  ASSERT_OK(CreateTable(new_name));
}

// The reservation-window variant where the DROP arrives through the reserved new name instead
// of the committed old name. The delete path resolves the name before it takes any lock and
// never revalidates it, so the drop resolves the reservation to the renaming table and deletes
// it. The drop erases only the committed name, the rename fails against the deleted table, and
// the failure must erase the reservation.
//
// TODO: A delete-path fix that revalidates the resolved name against the committed
// name once the delete holds the table lock would reject a drop through an uncommitted reserved
// name, and the drop assertions below should flip with it.
TEST_P(AlterTableSysCatalogWriteTest, TestDropByReservedNameDuringReservationWindow) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "reserved-name-dropped");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "post_name_reservation";
  StringWaiterLogSink parked_sink("Blocking AlterTableWithBatchTracker");

  auto rename_future = std::async(std::launch::async, [&] {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    return table_alterer->RenameTo(new_name)->Alter();
  });
  auto release_alter = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  });
  ASSERT_OK(parked_sink.WaitFor(30s * kTimeMultiplier));

  // The reserved name resolves to the renaming table, and the parked rename holds no lock, so
  // the drop resolves through the reservation and runs to completion.
  ASSERT_OK(client_->DeleteTable(new_name));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_alter_table) = "";
  // The rename finds the table deleted when it takes the lock.
  ASSERT_THAT(rename_future.get(), EqualsCode(STATUS(NotFound, "")));

  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(new_name)));

  // The drop erased the committed name and the rename's failure erased the reservation. Both
  // names are free for fresh tables.
  ASSERT_OK(CreateTable(kTableName));
  ASSERT_OK(CreateTable(new_name));
}

// An injected failure right after the in-memory commit exercises the alter's late failure path.
// The rename is committed at that point, so the failure must not roll it back. The reservation
// guard was dismissed at commit and the old name was already erased, so the new name keeps
// resolving, the old name is free, and the error reaches the client. A guard that outlived the
// commit would erase the table's live name.
TEST_P(AlterTableSysCatalogWriteTest, TestFailureAfterCommitKeepsRename) {
  YBTableName new_name(
      kTableName.namespace_type(), kTableName.namespace_name(), "renamed-then-failed");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_after_commit) = true;
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  Status s = table_alterer->RenameTo(new_name)->Alter();
  ASSERT_NOK(s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_table_after_commit) = false;

  // The rename committed before the injected failure. Only the new name resolves.
  ASSERT_TRUE(ASSERT_RESULT(TableNameResolves(new_name)));
  ASSERT_FALSE(ASSERT_RESULT(TableNameResolves(kTableName)));

  // The commit erased the old name rather than leaking it, so it is free for a fresh table.
  ASSERT_OK(CreateTable(kTableName));

  // The renamed table accepts further schema changes. This alter waits for the tablets to apply
  // the schema, so the cluster is settled before teardown.
  ASSERT_OK(AddNewI32Column(new_name, "added-after-injected-failure"));
}

// Test restarting a tablet server several times after various
// schema changes.
// This is a regression test for KUDU-462.
TEST_P(AlterTableTest, TestBootstrapAfterAlters) {
  ASSERT_OK(AddNewI32Column(kTableName, "c2"));
  InsertRows(0, 1);
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  InsertRows(1, 1);

  UpdateRow(0, { {"c1", 10001} });
  UpdateRow(1, { {"c1", 10002} });

  auto rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, int32:10001, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, int32:10002, null }", rows[1]);

  LOG(INFO) << "Dropping c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, null }", rows[1]);

  // Test that restart doesn't fail when trying to replay updates or inserts
  // with the dropped column.
  ASSERT_NO_FATALS(RestartTabletServer());

  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, null }", rows[1]);

  // Add back a column called 'c2', but should not materialize old data.
  ASSERT_OK(AddNewI32Column(kTableName, "c1"));
  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, null, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, null, null }", rows[1]);

  ASSERT_NO_FATALS(RestartTabletServer());
  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, null, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, null, null }", rows[1]);
}

TEST_P(AlterTableTest, TestAlterWalRetentionSecs) {
  InsertRows(1, 1000);
  int kWalRetentionSecs = RandomUniformBool()
      ? FLAGS_log_min_seconds_to_retain / 2
      : FLAGS_log_min_seconds_to_retain * 2;

  LOG(INFO) << "Modifying wal retention time to " << kWalRetentionSecs;
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));

  ASSERT_OK(table_alterer->SetWalRetentionSecs(kWalRetentionSecs)->Alter());

  int expected_wal_retention_secs = max(FLAGS_log_min_seconds_to_retain, kWalRetentionSecs);

  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_EQ(kWalRetentionSecs, tablet->metadata()->wal_retention_secs());
  ASSERT_EQ(expected_wal_retention_secs, tablet_peer_->log()->wal_retention_secs());

  // Test that the wal retention time gets set correctly in the metadata and in the log objects.
  ASSERT_NO_FATALS(RestartTabletServer());

  ASSERT_EQ(kWalRetentionSecs, tablet->metadata()->wal_retention_secs());
  ASSERT_EQ(expected_wal_retention_secs, tablet_peer_->log()->wal_retention_secs());
}

TEST_P(AlterTableTest, TestCompactAfterUpdatingRemovedColumn) {
  // Disable maintenance manager, since we manually flush/compact
  // in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_maintenance_manager) = false;

  vector<string> rows;

  ASSERT_OK(AddNewI32Column(kTableName, "c2"));
  InsertRows(0, 1);
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  InsertRows(1, 1);
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, int32:0, null }", rows[0]);
  ASSERT_EQ("{ int32:16777216, int32:1, null }", rows[1]);

  // Add a delta for c1.
  UpdateRow(0, { {"c1", 54321} });

  // Drop c1.
  LOG(INFO) << "Dropping c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  rows = ScanToStrings();
  ASSERT_EQ(2, rows.size());
  ASSERT_EQ("{ int32:0, null }", rows[0]);
}

typedef std::vector<std::shared_ptr<client::YBqlOp>> Ops;

std::pair<bool, int> AnalyzeResponse(const Ops& ops) {
  std::pair<bool, int> result = { false, 0 };
  for (const auto& op : ops) {
    if (op->response().status() == QLResponsePB::YQL_STATUS_OK) {
      ++result.second;
    } else {
      if (QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH == op->response().status()) {
        result.first = true;
      }
    }
  }
  return result;
}

// Thread which inserts rows into the table.
// After each batch of rows is inserted, inserted_idx_ is updated
// to communicate how much data has been written (and should now be
// updateable)
void AlterTableTest::WriteThread(QLWriteRequestPB::QLStmtType type) {
  auto session = client_->NewSession(15s);

  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));
  Ops ops;
  int32_t processed = 0;
  int32_t i = 0;
  Random rng(1);
  for (;;) {
    bool should_stop = stop_threads_.Load();
    if (!should_stop) {
      auto op = table.NewWriteOp(session->arena(), type);
      auto req = op->mutable_request();
      // Endian-swap the key so that we spew inserts randomly
      // instead of just a sequential write pattern. This way
      // compactions may actually be triggered.

      if (type == QLWriteRequestPB::QL_STMT_INSERT) {
        int32_t key = bswap_32(i++);
        QLAddInt32HashValue(req, key);
        table.AddInt32ColumnValue(req, table.schema().columns()[1].name(), i);
      } else {
        int32_t max = inserted_idx_.Load();
        if (max == 0) {
          // Inserter hasn't inserted anything yet, so we have nothing to update.
          SleepFor(MonoDelta::FromMicroseconds(100));
          continue;
        }
        // Endian-swap the key to match the way the insert generates keys.
        int32_t key = bswap_32(rng.Uniform(max-1));
        QLAddInt32HashValue(req, key);
        table.AddInt32ColumnValue(req, table.schema().columns()[1].name(), i);
      }

      ops.push_back(op);
      session->Apply(op);
    }

    if (should_stop || ops.size() >= 10) {
      Status s = session->TEST_Flush();
      ASSERT_TRUE(s.ok() || s.IsBusy() || s.IsIOError());
      auto result = AnalyzeResponse(ops);
      ops.clear();
      processed += result.second;
      if (type == QLWriteRequestPB::QL_STMT_INSERT) {
        inserted_idx_.Store(processed);
        i = processed;
      }
      if (result.first) {
        ASSERT_OK(table.Open(kTableName, client_.get()));
      }
    }

    if (should_stop) {
      break;
    }
  }

  LOG(INFO) << "Processed: " << processed << " of type " << QLWriteRequestPB::QLStmtType_Name(type);
  ASSERT_GT(processed, 0);
}

// Thread which loops reading data from the table.
// No verification is performed.
void AlterTableTest::ScannerThread() {
  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));
  while (!stop_threads_.Load()) {
    int inserted_at_scanner_start = inserted_idx_.Load();
    client::TableIteratorOptions options;
    bool failed = false;
    options.error_handler = [&failed](const Status& status) {
      LOG(WARNING) << "Scan failed: " << status;
      failed = true;
    };
    size_t count = boost::size(client::TableRange(table, options));
    if (failed) {
      continue;
    }

    LOG(INFO) << "Scanner saw " << count << " rows";
    // We may have gotten more rows than we expected, because inserts
    // kept going while we set up the scan. But, we should never get
    // fewer.
    ASSERT_GE(count, inserted_at_scanner_start)
      << "We didn't get as many rows as expected";
  }
}

// Test altering a table while also sending a lot of writes,
// checking for races between the two.
TEST_P(AlterTableTest, TestAlterUnderWriteLoad) {
  scoped_refptr<Thread> writer;
  CHECK_OK(Thread::Create(
      "test", "inserter",
      std::bind(&AlterTableTest::WriteThread, this, QLWriteRequestPB::QL_STMT_INSERT), &writer));

  scoped_refptr<Thread> updater;
  CHECK_OK(Thread::Create(
      "test", "updater",
      std::bind(&AlterTableTest::WriteThread, this, QLWriteRequestPB::QL_STMT_UPDATE), &updater));

  scoped_refptr<Thread> scanner;
  CHECK_OK(
      Thread::Create("test", "scanner", std::bind(&AlterTableTest::ScannerThread, this), &scanner));

  // Add columns until we reach 10.
  for (int i = 2; i < 10; i++) {
    MonoDelta delay;
    if (AllowSlowTests()) {
      // In slow test mode, let more writes accumulate in between
      // alters, so that we get enough writes to cause flushes,
      // compactions, etc.
      delay = MonoDelta::FromSeconds(3);
    } else {
      delay = MonoDelta::FromMilliseconds(100);
    }
    SleepFor(delay);

    ASSERT_OK(AddNewI32Column(kTableName, strings::Substitute("c$0", i)));
  }

  stop_threads_.Store(true);
  writer->Join();
  updater->Join();
  scanner->Join();
}

TEST_P(AlterTableTest, TestInsertAfterAlterTable) {
  YBTableName kSplitTableName(YQL_DATABASE_CQL, "my_keyspace", "split-table");

  // Create a new table with 10 tablets.
  //
  // With more tablets, there's a greater chance that the TS will heartbeat
  // after some but not all tablets have finished altering.
  ASSERT_OK(CreateTable(kSplitTableName));

  // Add a column, and immediately try to insert a row including that
  // new column.
  ASSERT_OK(AddNewI32Column(kSplitTableName, "new-i32"));
  client::TableHandle table;
  ASSERT_OK(table.Open(kSplitTableName, client_.get()));
  auto session = client_->NewSession(15s);
  auto insert = table.NewInsertOp(session->arena());
  auto req = insert->mutable_request();
  QLAddInt32HashValue(req, 1);
  table.AddInt32ColumnValue(req, "c1", 1);
  table.AddInt32ColumnValue(req, "new-i32", 1);
  session->Apply(insert);
  auto flush_status = session->TEST_FlushAndGetOpsErrors();
  const auto& s = flush_status.status;
  if (!s.ok()) {
    ASSERT_EQ(1, flush_status.errors.size());
    ASSERT_OK(flush_status.errors[0]->status()); // will fail
  }
}

// Issue a bunch of alter tables in quick succession. Regression for a bug
// seen in an earlier implementation of "alter table" where these could
// conflict with each other.
TEST_P(AlterTableTest, TestMultipleAlters) {
  YBTableName kSplitTableName(YQL_DATABASE_CQL, "my_keyspace", "split-table");
  const size_t kNumNewCols = 10;

  // With more tablets, there's a greater chance that the TS will heartbeat
  // after some but not all tablets have finished altering.
  ASSERT_OK(CreateTable(kSplitTableName));

  // Issue a bunch of new alters without waiting for them to finish.
  for (size_t i = 0; i < kNumNewCols; i++) {
    std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kSplitTableName));
    table_alterer->AddColumn(strings::Substitute("new_col$0", i))
                 ->Type(DataType::INT32)->NotNull();
    ASSERT_OK(table_alterer->wait(false)->Alter());
  }

  // Now wait. This should block on all of them.
  ASSERT_OK(WaitAlterTableCompletion(kSplitTableName, 50));

  // All new columns should be present.
  YBSchema new_schema;
  dockv::PartitionSchema partition_schema;
  ASSERT_OK(client_->GetTableSchema(kSplitTableName, &new_schema, &partition_schema));
  ASSERT_EQ(kNumNewCols + schema_.num_columns(), new_schema.num_columns());
}

TEST_P(ReplicatedAlterTableTest, TestReplicatedAlter) {
  const int kNumRows = 100;
  InsertRows(0, kNumRows);

  LOG(INFO) << "Verifying initial pattern";
  VerifyRows(0, kNumRows, C1_MATCHES_INDEX);

  LOG(INFO) << "Dropping and adding back c1";
  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());

  ASSERT_OK(AddNewI32Column(kTableName, "c1"));

  bool alter_in_progress;
  string table_id;
  ASSERT_OK(client_->IsAlterTableInProgress(kTableName, table_id, &alter_in_progress));
  ASSERT_FALSE(alter_in_progress);

  LOG(INFO) << "Verifying that the new default shows up";
  VerifyRows(0, kNumRows, C1_IS_DEADBEEF);
}

TEST_P(ReplicatedAlterTableTest, TestAlterOneTSDown) {
  const int kNumRows = 100;
  InsertRows(0, kNumRows);

  LOG(INFO) << "Verifying initial pattern";
  VerifyRows(0, kNumRows, C1_MATCHES_INDEX);

  ShutdownTS(0);
  // Now operating with 2 out of 3 servers.  Alter should still work because it's quorum-based.

  std::unique_ptr<YBTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
  ASSERT_OK(table_alterer->DropColumn("c1")->Alter());
  ASSERT_OK(AddNewI32Column(kTableName, "c1"));
  ASSERT_OK(AddNewI32Column(kTableName, "new_col"));

  bool alter_in_progress;
  string table_id;
  ASSERT_OK(client_->IsAlterTableInProgress(kTableName, table_id, &alter_in_progress));
  ASSERT_FALSE(alter_in_progress);

  LOG(INFO) << "Verifying that the new default shows up";
  VerifyRows(0, kNumRows, C1_IS_DEADBEEF);

  RestartTabletServer(0);
}

}  // namespace yb
