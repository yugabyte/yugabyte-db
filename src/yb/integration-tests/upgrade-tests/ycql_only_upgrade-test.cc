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

#include "yb/integration-tests/cql_test_util.h"

#include "yb/tablet/tablet_bootstrap_state_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/subprocess.h"

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

  // Dumps every container file of the stopped cluster with the yb-pbc-dump in bin_path, then
  // restarts the cluster.
  Status DumpContainerFiles(const std::string& bin_path) {
    cluster_->Shutdown();
    const auto tool = JoinPathSegments(bin_path, "yb-pbc-dump");
    // Walk does not return the callback's status.
    std::vector<std::string> failures;
    for (auto* daemon : cluster_->daemons()) {
      RETURN_NOT_OK(Env::Default()->Walk(
          daemon->GetRootDir(), Env::PRE_ORDER,
          [&tool, &failures](
              Env::FileType type, const std::string& dir, const std::string& name) -> Status {
            const auto path = JoinPathSegments(dir, name);
            if (type != Env::FILE_TYPE) {
              return Status::OK();
            }
            auto is_container = pb_util::IsPBContainerFile(Env::Default(), path);
            std::string output, error;
            auto status = !is_container.ok() ? is_container.status()
                : *is_container ? Subprocess::Call({tool, path}, &output, &error)
                : Status::OK();
            if (!status.ok()) {
              failures.push_back(Format("$0: $1 $2", path, status, error));
            }
            return Status::OK();
          }));
    }
    SCHECK(failures.empty(), IllegalState, Format("$0 failed: $1", tool, AsString(failures)));
    RETURN_NOT_OK(cluster_->Restart());
    return WaitForClusterToStabilize();
  }

  std::unique_ptr<CppCassandraDriver> MakeCqlDriver() {
    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    return std::make_unique<CppCassandraDriver>(
        hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kFalse);
  }

  client::TableHandle table_;
  int32 row_count_ = 0;
};

namespace {

const std::string kUuidV0 = "f2222222-2222-0222-8222-222222222222";
const std::string kUuidV4 = "9e107d9d-372b-4b6a-8d0a-2f5c7c8e1a3b";
const std::string kUuidV6 = "f1111111-1111-6111-8111-111111111111";
const std::string kUuidV7 = "0b9a2f3c-5d4e-7f60-9a1b-2c3d4e5f6a7b";

using CassCollectionPtr =
    std::unique_ptr<CassCollection, FuncDeleter<CassCollection, &cass_collection_free>>;

Status CheckCass(CassError error) {
  SCHECK_EQ(error, CASS_OK, IllegalState, cass_error_desc(error));
  return Status::OK();
}

Result<CassUuid> ToCassUuid(const std::string& uuid) {
  CassUuid result;
  RETURN_NOT_OK(CheckCass(cass_uuid_from_string(uuid.c_str(), &result)));
  return result;
}

Result<CassCollectionPtr> CassUuidSet(const std::vector<std::string>& uuids) {
  CassCollectionPtr result(cass_collection_new(CASS_COLLECTION_TYPE_SET, uuids.size()));
  for (const auto& uuid : uuids) {
    RETURN_NOT_OK(
        CheckCass(cass_collection_append_uuid(result.get(), VERIFY_RESULT(ToCassUuid(uuid)))));
  }
  return result;
}

// Values of the frozen key columns s, m and n of the table below, with every UUID converted to CQL
// text by uuid_to_cql.
std::vector<std::string> FrozenUuidKey(
    const std::function<std::string(const std::string&)>& uuid_to_cql) {
  const auto v0 = uuid_to_cql(kUuidV0);
  const auto v4 = uuid_to_cql(kUuidV4);
  const auto v6 = uuid_to_cql(kUuidV6);
  const auto v7 = uuid_to_cql(kUuidV7);
  return {
      Format("{$0, $1, $2, $3}", v4, v6, v7, v0),
      Format("{$0: 4, $1: 7}", v4, v7),
      Format("{{$0, $1}, {$2}}", v4, v7, v6)};
}

std::string FrozenUuidKeyCondition(
    const std::function<std::string(const std::string&)>& uuid_to_cql) {
  const auto key = FrozenUuidKey(uuid_to_cql);
  return Format("s = $0 AND m = $1 AND n = $2", key[0], key[1], key[2]);
}

std::string AsIs(const std::string& uuid) {
  return uuid;
}

std::string BlobAsUuid(const std::string& uuid) {
  std::string blob = "0x" + uuid;
  blob.erase(std::remove(blob.begin(), blob.end(), '-'), blob.end());
  return Format("blobasuuid($0)", blob);
}

// Binds the same key as FrozenUuidKey to the first three variables, elements in the same order.
Status BindFrozenUuidKey(CassandraStatement* statement) {
  auto set = VERIFY_RESULT(CassUuidSet({kUuidV4, kUuidV6, kUuidV7, kUuidV0}));
  CassCollectionPtr map(cass_collection_new(CASS_COLLECTION_TYPE_MAP, 2));
  for (const auto& [uuid, value] : {std::pair(kUuidV4, 4), std::pair(kUuidV7, 7)}) {
    RETURN_NOT_OK(
        CheckCass(cass_collection_append_uuid(map.get(), VERIFY_RESULT(ToCassUuid(uuid)))));
    RETURN_NOT_OK(CheckCass(cass_collection_append_int32(map.get(), value)));
  }
  CassCollectionPtr nested(cass_collection_new(CASS_COLLECTION_TYPE_SET, 2));
  for (const auto& inner : {std::vector{kUuidV4, kUuidV7}, std::vector{kUuidV6}}) {
    RETURN_NOT_OK(CheckCass(
        cass_collection_append_collection(nested.get(), VERIFY_RESULT(CassUuidSet(inner)).get())));
  }
  RETURN_NOT_OK(CheckCass(cass_statement_bind_collection(statement->get(), 0, set.get())));
  RETURN_NOT_OK(CheckCass(cass_statement_bind_collection(statement->get(), 1, map.get())));
  return CheckCass(cass_statement_bind_collection(statement->get(), 2, nested.get()));
}

} // namespace

TEST_F(YcqlOnlyUpgradeTest, TestUpgrade) {
  ASSERT_OK(UpgradeClusterToCurrentVersion());

  // upgrade_ysql should be a no-op.
  std::string output;
  ASSERT_OK(cluster_->CallYbAdmin({"upgrade_ysql"}, 10min, &output));

  ASSERT_OK(InsertRowsAndValidate());

  ASSERT_NOK_STR_CONTAINS(StartYsqlMajorCatalogUpgrade(), "YSQL is not enabled");
}

// A version from before #28566 stores the elements of frozen sets and the keys of frozen maps in
// the old UUID order. After the upgrade, the row must be found by its key written as literals, with
// blobasuuid() and as bind variables, and updated and deleted in place.
TEST_F(YcqlOnlyUpgradeTest, FrozenUuidCollectionKey) {
  {
    auto driver = MakeCqlDriver();
    auto session = ASSERT_RESULT(driver->CreateSession());
    ASSERT_OK(session.ExecuteQuery("CREATE KEYSPACE frozen_uuid"));
    ASSERT_OK(session.ExecuteQuery(
        "CREATE TABLE frozen_uuid.t (s frozen<set<uuid>>, m frozen<map<uuid, int>>, "
        "n frozen<set<frozen<set<uuid>>>>, v int, PRIMARY KEY ((s, m, n)))"));
    const auto key = FrozenUuidKey(AsIs);
    ASSERT_OK(session.ExecuteQueryFormat(
        "INSERT INTO frozen_uuid.t (s, m, n, v) VALUES ($0, $1, $2, 1)", key[0], key[1], key[2]));
  }

  ASSERT_OK(UpgradeClusterToCurrentVersion());

  auto driver = MakeCqlDriver();
  auto session = ASSERT_RESULT(driver->CreateSession());
  auto count_rows = [&session] {
    return session.FetchValue<int64_t>("SELECT COUNT(*) FROM frozen_uuid.t");
  };

  for (const auto& uuid_to_cql : {AsIs, BlobAsUuid}) {
    EXPECT_EQ(1, ASSERT_RESULT(session.FetchValue<int32_t>(Format(
        "SELECT v FROM frozen_uuid.t WHERE $0", FrozenUuidKeyCondition(uuid_to_cql)))));
  }
  auto select = ASSERT_RESULT(
      session.Prepare("SELECT v FROM frozen_uuid.t WHERE s = ? AND m = ? AND n = ?"));
  auto select_statement = select.Bind();
  ASSERT_OK(BindFrozenUuidKey(&select_statement));
  ASSERT_OK(session.ExecuteAndProcessOneRow(select_statement, [](const CassandraRow& row) {
    EXPECT_EQ(1, row.Value(0).As<int32_t>());
  }));

  ASSERT_OK(session.ExecuteQueryFormat(
      "UPDATE frozen_uuid.t SET v = 2 WHERE $0", FrozenUuidKeyCondition(AsIs)));
  EXPECT_EQ(1, ASSERT_RESULT(count_rows()));
  EXPECT_EQ(2, ASSERT_RESULT(session.FetchValue<int32_t>("SELECT v FROM frozen_uuid.t")));

  auto remove = ASSERT_RESULT(
      session.Prepare("DELETE FROM frozen_uuid.t WHERE s = ? AND m = ? AND n = ?"));
  auto remove_statement = remove.Bind();
  ASSERT_OK(BindFrozenUuidKey(&remove_statement));
  ASSERT_OK(session.Execute(remove_statement));
  EXPECT_EQ(0, ASSERT_RESULT(count_rows()));
}

TEST_F(YcqlOnlyUpgradeTest, TestRollback) {
  ASSERT_OK(UpgradeClusterToCurrentVersion(/*delay_between_nodes=*/3s, /*auto_finalize=*/false));
  ASSERT_OK(InsertRowsAndValidate());

  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(InsertRowsAndValidate());
}

// Container file headers embed the descriptors of the message type: all types of its .proto file
// and its imports in the old version, only the needed ones in the current version. The yb-pbc-dump
// and the daemons of each version must read the files written by the other one.
TEST_F(YcqlOnlyUpgradeTest, ContainerFiles) {
  ASSERT_OK(DumpContainerFiles(current_version_bin_path()));
  ASSERT_OK(UpgradeClusterToCurrentVersion(/*delay_between_nodes=*/3s, /*auto_finalize=*/false));
  ASSERT_OK(InsertRowsAndValidate());

  // A log rollover writes the bootstrap state file of a tablet that has writes to save.
  std::vector<TabletId> tablet_ids;
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(table_.name(), /*max_tablets=*/0, &tablet_ids, &ranges));
  ASSERT_EQ(tablet_ids.size(), 1);
  for (auto* ts : cluster_->tserver_daemons()) {
    ASSERT_OK(ts->LogGC(tablet_ids, /*rollover=*/true));
    const auto path = tablet::TabletBootstrapStateManager::FilePath(JoinPathSegments(
        ts->GetRootDir(), "yb-data", "tserver", "wals", "table-" + table_->id(),
        "tablet-" + tablet_ids.front()));
    // The old version embeds tens of KB of descriptors in this file.
    ASSERT_OK(WaitFor(
        [&path]() -> Result<bool> {
          return Env::Default()->FileExists(path) &&
                 VERIFY_RESULT(Env::Default()->GetFileSize(path)) < 4_KB;
        },
        30s * kTimeMultiplier, "Bootstrap state file written by the current version"));
  }
  ASSERT_OK(DumpContainerFiles(old_version_bin_path()));

  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(InsertRowsAndValidate());
}

}  // namespace yb
