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

#include <rapidjson/document.h>

#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/encryption/encryption_util.h"

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/master/master_client.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/env.h"
#include "yb/util/random_util.h"

namespace yb {

namespace {

const MonoDelta kRpcTimeout = 5s * kTimeMultiplier;

} // namespace

// The old release writes 16-byte data keys under a 32-byte universe key. The upgrade must not
// change that: the universe stays on AES-128 until the operator rotates to a 48-byte universe key
// and opts in to 32-byte data keys. Rolling back must leave every combination readable by the old
// release, which has always accepted those sizes.
class EncryptionUpgradeTest : public UpgradeTestBase {
 public:
  EncryptionUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}

  void SetUp() override {
    UpgradeTestBase::SetUp();
    if (Test::IsSkipped()) {
      return;
    }

    ASSERT_OK(StartClusterInOldVersion());
    ASSERT_OK(AddAndRotateUniverseKey(encryption::ENCRYPTION_ALGORITHM_AES_128_CTR));
    ASSERT_OK(CreateTable());
    ASSERT_OK(InsertRowsAndFlush());
  }

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    UpgradeTestBase::SetUpOptions(opts);
    opts.enable_ysql = false;
  }

  // Adds a universe key as strong as the data keys of algorithm and rotates to it.
  Status AddAndRotateUniverseKey(encryption::EncryptionAlgorithmPB algorithm) {
    const auto key_id = RandomHumanReadableString(16);
    const auto key_path = GetTestPath(key_id);
    auto key = RandomBytes(VERIFY_RESULT(encryption::UniverseKeySize(algorithm)));
    RETURN_NOT_OK(WriteStringToFile(Env::Default(), Slice(key.data(), key.size()), key_path));
    RETURN_NOT_OK(cluster_->CallYbAdmin({"add_universe_key_to_all_masters", key_id, key_path}));
    RETURN_NOT_OK(WaitFor(
        [this, &key_id]() -> Result<bool> {
          return cluster_->CallYbAdmin({"all_masters_have_universe_key_in_memory", key_id}).ok();
        },
        30s * kTimeMultiplier, "All masters have the universe key"));
    RETURN_NOT_OK(cluster_->CallYbAdmin({"rotate_universe_key_in_memory", key_id}));
    // Tservers pick up the new key with the registry on their next heartbeat.
    SleepFor(3s * kTimeMultiplier);
    return Status::OK();
  }

  Status CreateTable() {
    client::YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("c0")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    RETURN_NOT_OK(b.Build(&schema));

    client::YBTableName table_name(YQL_DATABASE_CQL, "namespace_name", "table_name");
    RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(
        table_name.namespace_name(), table_name.namespace_type()));
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    RETURN_NOT_OK(table_creator->table_name(table_name)
                      .schema(&schema)
                      .table_type(client::YBTableType::YQL_TABLE_TYPE)
                      .num_tablets(3)
                      .Create());
    return table_.Open(table_name, client_.get());
  }

  // Writes rows and flushes them to SST files, so that both WAL segments and SST files are written
  // with the data keys of the running release.
  Status InsertRowsAndFlush() {
    auto session = client_->NewSession(kRpcTimeout);
    std::vector<client::YBOperationPtr> ops;
    for (auto i = 0; i < 100; i++) {
      auto op = table_.NewInsertOp(session->arena());
      QLAddInt32HashValue(op->mutable_request(), row_count_++);
      ops.push_back(std::move(op));
    }
    RETURN_NOT_OK(session->TEST_ApplyAndFlush(ops));
    RETURN_NOT_OK(client_->FlushTables({table_->id()}));
    return ValidateRows();
  }

  // The old release's FlushTables registers an empty, never-found flush request when the master
  // leader has no tablet replica locations yet, and flushes only replicas it knows of. So wait for
  // every tserver to report its replicas.
  Status WaitForTabletLocations() {
    return WaitFor(
        [this]() -> Result<bool> {
          google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
          RETURN_NOT_OK(client_->GetTabletsFromTableId(table_->id(), 0, &tablets));
          for (const auto& tablet : tablets) {
            if (tablet.stale() || static_cast<size_t>(tablet.replicas_size()) !=
                                      cluster_->num_tablet_servers()) {
              return false;
            }
          }
          return !tablets.empty();
        },
        30s * kTimeMultiplier, "Master leader has tablet replica locations");
  }

  Status ValidateRows() {
    SCHECK_EQ(client::CountTableRows(table_), row_count_, IllegalState, "Row count mismatch");
    return Status::OK();
  }

  // Number of encrypted SST files with the given data-key and universe-key sizes, in bits, across
  // all tservers. Only the current release serves the report.
  Result<uint64_t> CountSstFiles(uint32_t data_key_bits, uint32_t universe_key_bits) {
    uint64_t result = 0;
    for (auto* ts : cluster_->tserver_daemons()) {
      faststring body;
      RETURN_NOT_OK(EasyCurl().FetchURL(
          Format("http://$0/api/v1/encryption-key-report",
                 HostPort(ts->bind_host(), ts->http_port())),
          &body));
      rapidjson::Document doc;
      SCHECK(!doc.Parse(body.char_data(), body.length()).HasParseError(), IllegalState,
             Format("Failed to parse report: $0", body.ToString()));
      for (const auto& group : doc["report"]["groups"].GetArray()) {
        if (group["encrypted"].GetBool() &&
            std::string(group["file_type"].GetString()) == "Sst" &&
            group["data_key_bits"].GetUint() == data_key_bits &&
            group["universe_key_bits"].GetUint() == universe_key_bits) {
          result += group["files"].GetUint64();
        }
      }
    }
    return result;
  }

  client::TableHandle table_;
  int32 row_count_ = 0;
};

TEST_F(EncryptionUpgradeTest, UpgradeToAes256AndRollback) {
  ASSERT_OK(UpgradeClusterToCurrentVersion(/*delay_between_nodes=*/3s, /*auto_finalize=*/false));
  ASSERT_OK(ValidateRows());

  // The upgraded universe keeps writing AES-128 data keys.
  ASSERT_OK(InsertRowsAndFlush());
  ASSERT_GT(ASSERT_RESULT(CountSstFiles(128, 128)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountSstFiles(256, 128)), 0);

  // Rotating to a 48-byte universe key changes only the wrapping layer.
  ASSERT_OK(AddAndRotateUniverseKey(encryption::ENCRYPTION_ALGORITHM_AES_256_CTR));
  ASSERT_OK(InsertRowsAndFlush());
  ASSERT_GT(ASSERT_RESULT(CountSstFiles(128, 256)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountSstFiles(256, 256)), 0);

  // After the operator opts in, new files are AES-256 at both layers.
  ASSERT_OK(cluster_->CallYbAdmin({"set_encryption_algorithm", "AES_256_CTR"}));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_OK(InsertRowsAndFlush());
  ASSERT_GT(ASSERT_RESULT(CountSstFiles(256, 256)), 0);

  // The old release reads every combination of key sizes back: tablet bootstrap replays the WAL,
  // the row count reads every SST file, and compaction rewrites them all.
  ASSERT_OK(RollbackClusterToOldVersion());
  ASSERT_OK(ValidateRows());
  ASSERT_OK(WaitForTabletLocations());
  ASSERT_OK(client_->CompactTables({table_->id()}));
  ASSERT_OK(ValidateRows());
  ASSERT_OK(InsertRowsAndFlush());
}

}  // namespace yb
