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

#include <string>

#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {
namespace pgwrapper {

constexpr auto kTableName = "test";
constexpr auto kDatabaseName = "yugabyte";

YB_DEFINE_ENUM(StorageFormat, (Regular)(PackedRowsV1)(PackedRowsV2));

class PgAddColumnDefaultTest : public LibPqTestBase,
                               public ::testing::WithParamInterface<StorageFormat> {
 public:
    void SetUp() override {
      LibPqTestBase::SetUp();
      conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
    }
    void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
      options->extra_tserver_flags.push_back("--timestamp_history_retention_interval_sec=0");
      if (GetParam() == StorageFormat::Regular) {
        options->extra_tserver_flags.push_back("--ysql_enable_packed_row=false");
      } else {
        // Add flags to enable packed row feature.
        options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
        options->extra_tserver_flags.push_back("--ysql_enable_packed_row=true");
        if (GetParam() == StorageFormat::PackedRowsV2) {
          options->extra_tserver_flags.push_back(
              "--allowed_preview_flags_csv=ysql_use_packed_row_v2");
          options->extra_tserver_flags.push_back("--ysql_use_packed_row_v2=true");
        }
      }
    }
    std::unique_ptr<PGConn> conn_;
    TestThreadHolder thread_holder_;
};

// Test compaction after updates are performed on columns with missing default values.
TEST_P(PgAddColumnDefaultTest, AddColumnDefaultCompactionAfterUpdate) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (t int PRIMARY KEY)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 4))", kTableName));
  // Add columns, some with default value.
  ASSERT_OK(conn_->ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN c1 text DEFAULT 'default', ADD COLUMN dummy int,"
      " ADD COLUMN c2 int DEFAULT 5",
      kTableName));
  // Verify the data.
  auto res = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      Format("SELECT count(*) FROM $0 WHERE c1 = 'default' AND c2 = 5", kTableName)));
  ASSERT_EQ(res, 4);
  // Update some rows.
  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET c1 = 'not default' WHERE t = 1", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET c2 = 6 WHERE t = 2", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET c2 = null WHERE t = 3", kTableName));
  // Drop the dummy column.
    ASSERT_OK(conn_->ExecuteFormat("ALTER TABLE $0 DROP COLUMN dummy", kTableName));
  const auto table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));
  // Compact the table.
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      3 /* deadline (seconds) */,
      true /* is_compaction */));
  // Verify the data after compaction.
  auto rows = ASSERT_RESULT((conn_->FetchRows<int32_t, std::string, std::optional<int32_t>>(
      Format("SELECT * FROM $0 ORDER BY t", kTableName))));
  ASSERT_EQ(
      rows,
      (decltype(rows){
         {1, "not default", 5},
         {2, "default", 6},
         {3, "default", std::nullopt},
         {4, "default", 5},
       }));
}

// Test COPY FROM after a ALTER TABLE ... ADD COLUMN ... DEFAULT operation.
TEST_P(PgAddColumnDefaultTest, AddColumnDefaultCopy) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (t int PRIMARY KEY)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 3))", kTableName));
  ASSERT_OK(conn_->ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN c1 text DEFAULT 'default'", kTableName));
  // COPY new rows into table.
  // Insert the following rows:
  // (4), (5), (6, null), (7, null), (8, "not default"), (9, "not default")
  ASSERT_OK(conn_->CopyBegin(Format("COPY $0(t) FROM STDIN WITH BINARY", kTableName)));
  for (int i = 4; i <= 5; ++i) {
    conn_->CopyStartRow(1 /* number of columns */);
    conn_->CopyPutInt32(i);
  }
  ASSERT_OK(conn_->CopyEnd());
  ASSERT_OK(conn_->CopyBegin(Format("COPY $0 FROM STDIN WITH BINARY", kTableName)));
  for (int i = 6; i <= 9; ++i) {
    conn_->CopyStartRow(2 /* number of columns */);
    conn_->CopyPutInt32(i);
    conn_->CopyPutString(i < 8 ? "" : "not default");
  }
  ASSERT_OK(conn_->CopyEnd());

  auto table_content_checker = [&conn = this->conn_]() -> Status {
    auto rows = VERIFY_RESULT((conn->FetchRows<int32_t, std::string>(
        Format("SELECT * FROM $0 ORDER BY t", kTableName))));
    constexpr auto kDefault = "default";
    constexpr auto kNotDefault = "not default";
    constexpr auto kEmpty = "";
    SCHECK_EQ(rows,
              (decltype(rows){
                {1, kDefault}, {2, kDefault}, {3, kDefault}, {4, kDefault}, {5, kDefault},
                {6, kEmpty}, {7, kEmpty},
                {8, kNotDefault}, {9, kNotDefault}}),
              IllegalState,
              "Unexpected value");
    return Status::OK();
  };

  // Verify the data.
  ASSERT_OK(table_content_checker());

  const auto table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));
  // Compact the table.
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      3 /* deadline (seconds) */,
      true /* is_compaction */));
  // Verify the data after compaction.
  ASSERT_OK(table_content_checker());
}

INSTANTIATE_TEST_CASE_P(
    AddColumnDefaultTest, PgAddColumnDefaultTest,
    ::testing::Values(StorageFormat::Regular, StorageFormat::PackedRowsV1,
        StorageFormat::PackedRowsV2));

class PgAddColumnDefaultConcurrencyTest : public PgAddColumnDefaultTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    PgAddColumnDefaultTest::UpdateMiniClusterOptions(opts);
    // This test verifies behavior without table-level locking and transactional DDL.
    // Both features are disabled to concurrent inserts during ALTER TABLE.
    opts->extra_tserver_flags.emplace_back("--enable_object_locking_for_table_locks=false");
    opts->extra_tserver_flags.emplace_back("--ysql_yb_ddl_transaction_block_enabled=false");
  }
};

// Test concurrently inserted rows during an ALTER TABLE ... ADD COLUMN ... DEFAULT operation
// use the missing default value for the new column.
TEST_P(PgAddColumnDefaultConcurrencyTest, AddColumnDefaultConcurrency) {
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (t int PRIMARY KEY)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 3))", kTableName));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_alter_table", "alter_schema"));

  thread_holder_.AddThreadFunctor([this] {
    LOG(INFO) << "Begin alter table thread";
    PGConn alter_conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
    Status status = alter_conn.ExecuteFormat(
        "ALTER TABLE $0 ADD COLUMN c1 varchar(10) DEFAULT 'default'", kTableName);
  });

  // Concurrently insert new rows while the alter table is executing. These rows should use the
  // missing default value.
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(4, 6))", kTableName));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_alter_table", "completion"));
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    const auto table_id = VERIFY_RESULT(GetTableIdByTableName(
        client.get(), kDatabaseName, kTableName));
    std::shared_ptr<client::YBTableInfo> table_info = std::make_shared<client::YBTableInfo>();
    Synchronizer sync;
    RETURN_NOT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    RETURN_NOT_OK(sync.Wait());
    return table_info->schema.columns().size() == 2;
  }, MonoDelta::FromSeconds(60), "Wait for schema to match"));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(7, 9))", kTableName));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_alter_table", ""));
  thread_holder_.JoinAll();

  // Explicitly insert null values into the new column.
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(10, 12), null)", kTableName));
  // Verify that we can read the correct values for the new column.
  auto res = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      Format("SELECT count(*) FROM $0 WHERE c1 = 'default'", kTableName)));
  ASSERT_EQ(res, 9);
  const auto table_id = ASSERT_RESULT(GetTableIdByTableName(
      client.get(), kDatabaseName, kTableName));
  // Compact the table.
  ASSERT_OK(client->FlushTables(
      {table_id},
      false /* add_indexes */,
      3 /* deadline (seconds) */,
      true /* is_compaction */));
  // Verify that we can read the correct values for the new column after compaction.
  res = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      Format("SELECT count(*) FROM $0 WHERE c1 = 'default'", kTableName)));
  ASSERT_EQ(res, 9);
}

INSTANTIATE_TEST_CASE_P(
    AddColumnDefaultTest, PgAddColumnDefaultConcurrencyTest,
    ::testing::Values(StorageFormat::Regular, StorageFormat::PackedRowsV1,
        StorageFormat::PackedRowsV2));
} // namespace pgwrapper
} // namespace yb
