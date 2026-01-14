// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

namespace yb {

const MonoDelta kRpcTimeout = 5s * kTimeMultiplier;

// Test upgrade and rollback with a simple workload with updates and selects.
class YcqlOnlyUpgradeTest : public UpgradeTestBase {
 public:
  YcqlOnlyUpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}

  void SetUp() override {
    UpgradeTestBase::SetUp();
    if (Test::IsSkipped()) {
      return;
    }

    ASSERT_OK(StartClusterInOldVersion());
    ASSERT_OK(CreateTable());
    ASSERT_OK(InsertRowsAndValidate());
  }

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    UpgradeTestBase::SetUpOptions(opts);
    opts.enable_ysql = false;
  }

  Status CreateTable() {
    client::YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    RETURN_NOT_OK(b.Build(&schema));

    client::YBTableName table_name(YQL_DATABASE_CQL, "namespace_name", "table_name");
    RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(
        table_name.namespace_name(), table_name.namespace_type()));

    // Add a table, make sure it reports itself.
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    RETURN_NOT_OK(table_creator->table_name(table_name)
                      .schema(&schema)
                      .table_type(client::YBTableType::YQL_TABLE_TYPE)
                      .num_tablets(1)
                      .Create());

    RETURN_NOT_OK(table_.Open(table_name, client_.get()));

    return Status::OK();
  }

  Status InsertRows() {
    auto session = client_->NewSession(kRpcTimeout);
    std::vector<client::YBOperationPtr> ops;

    for (auto i = 0; i < 10; i++) {
      auto op = table_.NewInsertOp(session->arena());
      int32_t key = row_count_++;
      QLAddInt32HashValue(op->mutable_request(), key);
      ops.push_back(std::move(op));
    }

    return session->TEST_ApplyAndFlush(ops);
  }

  Status ValidateRows() {
    SCHECK_EQ(client::CountTableRows(table_), row_count_, IllegalState, "Row count mismatch");
    return Status::OK();
  }

  Status InsertRowsAndValidate() {
    RETURN_NOT_OK(InsertRows());
    return ValidateRows();
  }

  client::TableHandle table_;
  int32 row_count_ = 0;
};

TEST_F(YcqlOnlyUpgradeTest, TestUpgrade) {
  ASSERT_OK(UpgradeClusterToCurrentVersion());

  // upgrade_ysql should be a no-op.
  std::string output;
  ASSERT_OK(cluster_->CallYbAdmin({"upgrade_ysql"}, 10min, &output));

  ASSERT_OK(InsertRowsAndValidate());

  ASSERT_NOK_STR_CONTAINS(StartYsqlMajorCatalogUpgrade(), "YSQL is not enabled");
}

TEST_F(YcqlOnlyUpgradeTest, TestRollback) {
  ASSERT_OK(UpgradeClusterToCurrentVersion(/*delay_between_nodes=*/3s, /*auto_finalize=*/false));
  ASSERT_OK(InsertRowsAndValidate());

  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(InsertRowsAndValidate());
}

}  // namespace yb
