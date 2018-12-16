//
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
//

#include "yb/client/ql-dml-test-base.h"

#include "yb/client/client.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb {
namespace client {

const client::YBTableName kTableName("my_keyspace", "ql_client_test_table");
const std::string KeyValueTableTest::kKeyColumn = "key";
const std::string KeyValueTableTest::kValueColumn = "value";

namespace {

QLWriteRequestPB::QLStmtType GetQlStatementType(const WriteOpType op_type) {
  switch (op_type) {
    case WriteOpType::INSERT:
      return QLWriteRequestPB::QL_STMT_INSERT;
    case WriteOpType::UPDATE:
      return QLWriteRequestPB::QL_STMT_UPDATE;
    case WriteOpType::DELETE:
      return QLWriteRequestPB::QL_STMT_DELETE;
  }
  FATAL_INVALID_ENUM_VALUE(WriteOpType, op_type);
}

} // namespace

void QLDmlTestBase::SetUp() {
  HybridTime::TEST_SetPrettyToString(true);

  YBMiniClusterTestBase::SetUp();

  // Start minicluster and wait for tablet servers to connect to master.
  MiniClusterOptions opts;
  opts.num_tablet_servers = 3;
  cluster_.reset(new MiniCluster(env_.get(), opts));
  ASSERT_OK(cluster_->Start());

  ASSERT_OK(CreateClient());

  // Create test table
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name()));
}

Status QLDmlTestBase::CreateClient() {
  // Connect to the cluster.
  return cluster_->CreateClient(&client_);
}

void QLDmlTestBase::DoTearDown() {
  // If we enable this, it will break FLAGS_mini_cluster_reuse_data
  //
  // This DeleteTable clean up seems to cause a crash because the delete may not succeed
  // immediately and is retried after the master is restarted (see ENG-663). So disable it for
  // now.
  //
  // if (table_) {
  //   ASSERT_OK(client_->DeleteTable(kTableName));
  // }
  if (cluster_) {
    cluster_->Shutdown();
    cluster_.reset();
  }
  YBMiniClusterTestBase::DoTearDown();
}

void KeyValueTableTest::CreateTable(Transactional transactional) {
  YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(INT32);
  if (transactional) {
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    builder.SetTableProperties(table_properties);
  }

  ASSERT_OK(table_.Create(kTableName, CalcNumTablets(3), client_.get(), &builder));
}

Result<shared_ptr<YBqlWriteOp>> KeyValueTableTest::WriteRow(
    const YBSessionPtr& session, int32_t key, int32_t value,
    const WriteOpType op_type, Flush flush) {
  VLOG(4) << "Calling WriteRow key=" << key << " value=" << value << " op_type="
          << yb::ToString(op_type);
  const QLWriteRequestPB::QLStmtType stmt_type = GetQlStatementType(op_type);
  const auto op = table_.NewWriteOp(stmt_type);
  auto* const req = op->mutable_request();
  QLAddInt32HashValue(req, key);
  if (op_type != WriteOpType::DELETE) {
    table_.AddInt32ColumnValue(req, kValueColumn, value);
  }
  RETURN_NOT_OK(session->Apply(op));
  if (flush) {
    RETURN_NOT_OK(session->Flush());
    if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
      return STATUS_FORMAT(QLError, "Error writing row: $0", op->response().error_message());
    }
  }
  return op;
}

Result<shared_ptr<YBqlWriteOp>> KeyValueTableTest::DeleteRow(
    const YBSessionPtr& session, int32_t key) {
  return WriteRow(session, key, 0 /* value */, WriteOpType::DELETE);
}

Result<shared_ptr<YBqlWriteOp>> KeyValueTableTest::UpdateRow(
    const YBSessionPtr& session, int32_t key, int32_t value) {
  return WriteRow(session, key, value, WriteOpType::UPDATE);
}

Result<int32_t> KeyValueTableTest::SelectRow(
    const YBSessionPtr& session, int32_t key, const std::string& column) {
  const shared_ptr<YBqlReadOp> op = table_.NewReadOp();
  auto* const req = op->mutable_request();
  QLAddInt32HashValue(req, key);
  table_.AddColumns({column}, req);
  auto status = session->ApplyAndFlush(op);
  if (status.IsIOError()) {
    for (const auto& error : session->GetPendingErrors()) {
      LOG(WARNING) << "Error: " << error->status() << ", op: " << error->failed_op().ToString();
    }
  }
  RETURN_NOT_OK(status);
  if (op->response().status() != QLResponsePB::YQL_STATUS_OK) {
    return STATUS(QLError,
                  op->response().error_message(),
                  Slice(),
                  static_cast<int64_t>(ql::QLStatusToErrorCode(op->response().status())));
  }
  auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
  if (rowblock->row_count() == 0) {
    return STATUS_FORMAT(NotFound, "Row not found for key $0", key);
  }
  return rowblock->row(0).column(0).int32_value();
}

YBSessionPtr KeyValueTableTest::CreateSession(const YBTransactionPtr& transaction) {
  auto session = std::make_shared<YBSession>(client_);
  if (transaction) {
    session->SetTransaction(transaction);
  }
  session->SetTimeout(NonTsanVsTsan(15s, 60s));
  return session;
}

} // namespace client
} // namespace yb
