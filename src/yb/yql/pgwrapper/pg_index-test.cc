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

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/bind_internal.h"

#include "yb/integration-tests/mini_cluster_base.h"

#include "yb/util/async_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using std::string;

namespace yb {
namespace pgwrapper {

namespace {

constexpr auto kDatabaseName = "yugabyte";
constexpr auto kIndexName = "ginidx";
constexpr auto kTableName = "gintab";

} // namespace

class PgIndexTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    PgMiniTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

 protected:
  std::unique_ptr<PGConn> conn_;
};

// Test creating a ybgin index on an array whose element type is unsupported for primary key.
TEST_F(PgIndexTest, UnsupportedArrayElementType) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (a tsvector[])", kTableName));
  auto status = conn_->ExecuteFormat("CREATE INDEX $0 ON $1 USING ybgin (a)",
                                     kIndexName, kTableName);

  // Make sure that the index isn't created on PG side.
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("INDEX on column of type 'TSVECTOR' not yet supported") != std::string::npos)
      << status;

  // Make sure that the index isn't created on YB side.
  // First, check the table to make sure schema version isn't incremented.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto table_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, kTableName));
  auto table_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  ASSERT_EQ(table_info->schema.version(), 0);
  // Second, check that the index doesn't exist.
  auto result = GetTableIdByTableName(client.get(), kDatabaseName, kIndexName);
  ASSERT_NOK(result);
}

// Test SPLIT option.
TEST_F(PgIndexTest, SplitOption) {
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (v tsvector)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ('ab bc'), ('cd ef gh')", kTableName));

  // Hash splitting shouldn't work since the default partitioning scheme is range.
  auto status = conn_->ExecuteFormat("CREATE INDEX ON $0 USING ybgin (v) SPLIT INTO 4 TABLETS",
                                     kTableName);
  ASSERT_NOK(status);
  auto msg = status.message().ToBuffer();
  ASSERT_TRUE(msg.find("HASH columns must be present to split by number of tablets")
              != std::string::npos) << status;

  // Range splitting should work.
  ASSERT_OK(conn_->ExecuteFormat("CREATE INDEX $0 ON $1 USING ybgin (v)"
                                 " SPLIT AT VALUES (('bar'), ('foo'))",
                                 kIndexName, kTableName));
  // Check that partitions were actually created.
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  auto index_id = ASSERT_RESULT(GetTableIdByTableName(client.get(), kDatabaseName, kIndexName));
  auto index_info = std::make_shared<client::YBTableInfo>();
  Synchronizer sync;
  ASSERT_OK(client->GetTableSchemaById(index_id, index_info, sync.AsStatusCallback()));
  ASSERT_OK(sync.Wait());
  PartitionSchemaPB pb;
  index_info->partition_schema.ToPB(&pb);
  LOG(INFO) << "Index partition schema: " << pb.DebugString();
  ASSERT_EQ(pb.range_schema().splits_size(), 2);
  // Check SELECT.
  {
    auto query = Format("SELECT count(*) FROM $0 where v @@ 'bc'", kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
    auto value = ASSERT_RESULT(conn_->FetchValue<PGUint64>(query));
    ASSERT_EQ(value, 1);
  }
  {
    const auto query = Format("SELECT unnest.lexeme FROM $0, LATERAL unnest(v) where v @@ 'bc'",
                              kTableName);
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
    const auto values = ASSERT_RESULT(conn_->FetchRows<std::string>(query));
    ASSERT_EQ(values, (decltype(values){"ab", "bc"}));
  }

  // Hash partitioning is currently not possible, so we can't test hash splitting.
}

TEST_F(PgIndexTest, NullKey) {
  ASSERT_OK(conn_->Execute("CREATE TABLE usc_asc(k int, v int)"));
  ASSERT_OK(conn_->Execute("CREATE UNIQUE INDEX ON usc_asc(v ASC NULLS FIRST)"));
  ASSERT_OK(conn_->Execute(
      "INSERT INTO usc_asc VALUES (44, NULL),(22, 20),(33, 30),(11, 10),(44, NULL)"));
  auto rows = ASSERT_RESULT((conn_->FetchRows<int32_t, std::optional<int32_t>>(
      "SELECT * FROM usc_asc ORDER BY v DESC NULLS LAST")));
  ASSERT_EQ(rows,
            (decltype(rows){{33, 30}, {22, 20}, {11, 10}, {44, std::nullopt}, {44, std::nullopt}}));
}

} // namespace pgwrapper
} // namespace yb
