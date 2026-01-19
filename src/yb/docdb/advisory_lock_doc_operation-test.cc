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

#include "yb/common/transaction-test-util.h"

#include "yb/common/pgsql_protocol.messages.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/pgsql_operation.h"

namespace yb {
namespace docdb {

const uint32_t kDBOid = 1111;

class AdvisoryLockDocOperationTest : public DocDBTestBase {
 public:
  Schema CreateSchema() override {
    SchemaBuilder builder;
    CHECK_OK(builder.AddHashKeyColumn("dbid", DataType::UINT32));
    CHECK_OK(builder.AddKeyColumn("classid", DataType::UINT32));
    CHECK_OK(builder.AddKeyColumn("objid", DataType::UINT32));
    CHECK_OK(builder.AddKeyColumn("objsubid", DataType::UINT32));
    return builder.Build();
  }

  TransactionId StartTransaction() {
    const auto txn_id = TransactionId::GenerateRandom();
    txn_op_context_ = std::make_unique<TransactionOperationContext>(
      txn_id, &txn_status_manager_);
    SetCurrentTransactionId(txn_id);
    return txn_id;
  }

  Status Lock(uint32_t db_oid, uint32_t class_oid, uint32_t objid, uint32_t objsubid,
               PgsqlLockRequestPB::PgsqlAdvisoryLockMode mode, bool wait) {
    auto schema = CreateSchema();
    PgsqlLockRequestPB request;
    PgsqlResponsePB response;
    request.set_lock_mode(mode);
    auto* lock_id = request.mutable_lock_id();
    lock_id->add_lock_partition_column_values()->mutable_value()->set_uint32_value(db_oid);
    lock_id->add_lock_range_column_values()->mutable_value()->set_uint32_value(class_oid);
    lock_id->add_lock_range_column_values()->mutable_value()->set_uint32_value(objid);
    lock_id->add_lock_range_column_values()->mutable_value()->set_uint32_value(objsubid);
    request.set_wait(wait);
    request.set_is_lock(true);

    PgsqlLockOperation op(
        request, txn_op_context_ ? *txn_op_context_ : kNonTransactionalOperationContext);
    RETURN_NOT_OK(op.Init(
        &response, std::make_shared<DocReadContext>(DocReadContext::TEST_Create(schema))));
    auto doc_write_batch = MakeDocWriteBatch();
    ReadRestartData read_restart_data;
    RETURN_NOT_OK(op.Apply({
      .doc_write_batch = &doc_write_batch,
      .read_operation_data = ReadOperationData(),
      .read_restart_data = &read_restart_data,
      .schema_packing_provider = nullptr,
    }));
    return WriteToRocksDB(doc_write_batch, HybridTime::kMax);
  }

 protected:
  TransactionStatusManagerMock txn_status_manager_;
  std::unique_ptr<TransactionOperationContext> txn_op_context_;
};

TEST_F(AdvisoryLockDocOperationTest, ExclusiveLock) {
  auto txn_id = StartTransaction();
  ASSERT_OK(Lock(kDBOid, 2, 3, 4,
                 PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE, true));
  std::unordered_set<std::string> locks;
  DocDBDebugDumpToContainer(&locks);
  LOG(INFO) << CollectionToString(locks);
  ASSERT_TRUE(locks.contains(Format(
      "SubDocKey(DocKey(0x0000, [1111], [2, 3, 4]), []) [kStrongRead, kStrongWrite] "
      "HT<max> -> TransactionId($0) WriteId(0) l",
      txn_id.ToString()))) << CollectionToString(locks);
}

TEST_F(AdvisoryLockDocOperationTest, ShareLock) {
  auto txn_id = StartTransaction();
  ASSERT_OK(Lock(kDBOid, 2, 3, 4,
                 PgsqlLockRequestPB::PG_LOCK_SHARE, true));
  ASSERT_OK(Lock(kDBOid, 4, 5, 6,
                 PgsqlLockRequestPB::PG_LOCK_SHARE, true));
  std::unordered_set<std::string> locks;
  DocDBDebugDumpToContainer(&locks);
  ASSERT_TRUE(locks.contains(Format(
      "SubDocKey(DocKey(0x0000, [1111], [2, 3, 4]), []) [kStrongRead] "
      "HT<max> -> TransactionId($0) WriteId(0) l",
      txn_id.ToString()))) << CollectionToString(locks);
  ASSERT_TRUE(locks.contains(Format(
      "SubDocKey(DocKey(0x0000, [1111], [4, 5, 6]), []) [kStrongRead] "
      "HT<max> -> TransactionId($0) WriteId(1) l",
      txn_id.ToString()))) << CollectionToString(locks);
}

} // namespace docdb
} // namespace yb
