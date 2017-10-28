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

DECLARE_bool(mini_cluster_reuse_data);

namespace yb {
namespace client {

const client::YBTableName kTableName("my_keyspace", "ql_client_test_table");

void TableHandle::Create(const YBTableName& table_name,
                         YBClient* client,
                         YBSchemaBuilder* builder) {
  YBSchema schema;
  EXPECT_OK(builder->Build(&schema));

  if (!FLAGS_mini_cluster_reuse_data) {
    std::unique_ptr<YBTableCreator> table_creator(client->NewTableCreator());
    EXPECT_OK(table_creator->table_name(table_name)
        .table_type(YBTableType::YQL_TABLE_TYPE)
        .schema(&schema)
        .num_replicas(3)
        .num_tablets(CalcNumTablets(3))
        .Create());
  }

  EXPECT_OK(client->OpenTable(table_name, &table_));

  schema = table_->schema();
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    yb::ColumnId col_id = yb::ColumnId(schema.ColumnId(i));
    EXPECT_TRUE(column_ids_.emplace(schema.Column(i).name(), col_id).second);
    EXPECT_TRUE(column_types_.emplace(col_id, schema.Column(i).type()).second);
  }
}

namespace {

template<class T>
auto SetupRequest(const T& op) {
  auto* req = op->mutable_request();
  req->set_client(YQL_CLIENT_CQL);
  req->set_request_id(0);
  req->set_query_id(reinterpret_cast<int64_t>(op.get()));
  req->set_schema_version(0);
  return req;
}

} // namespace

std::shared_ptr<YBqlWriteOp> TableHandle::NewWriteOp(QLWriteRequestPB::QLStmtType type) {
  auto op = std::make_shared<YBqlWriteOp>(table_);
  auto *req = SetupRequest(op);
  req->set_type(type);
  return op;
}

std::shared_ptr<YBqlReadOp> TableHandle::NewReadOp() {
  shared_ptr<YBqlReadOp> op(table_->NewQLRead());
  SetupRequest(op);
  return op;
}

void TableHandle::SetInt32ColumnValue(
    QLColumnValuePB *column_value, const string &column_name, const int32_t value) {
  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_int32_value(value);
}

void TableHandle::SetStringColumnValue(
    QLColumnValuePB *column_value, const string &column_name, const string &value) {
  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_string_value(value);
}

void TableHandle::SetInt32Expression(QLExpressionPB *expr, const int32_t value) {
  expr->mutable_value()->set_int32_value(value);
}

void TableHandle::SetStringExpression(QLExpressionPB *expr, const string &value) {
  expr->mutable_value()->set_string_value(value);
}

void TableHandle::SetColumn(QLColumnValuePB *column_value, const string &column_name) {
  column_value->set_column_id(ColumnId(column_name));
}

void TableHandle::SetInt32Condition(
    QLConditionPB *const condition, const string &column_name, const QLOperator op,
    const int32_t value) {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto *const val = condition->add_operands()->mutable_value();
  val->set_int32_value(value);
}

void TableHandle::SetStringCondition(
    QLConditionPB *const condition, const string &column_name, const QLOperator op,
    const string &value) {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto *const val = condition->add_operands()->mutable_value();
  val->set_string_value(value);
}

void TableHandle::AddInt32Condition(
    QLConditionPB *const condition, const string &column_name, const QLOperator op,
    const int32_t value) {
  SetInt32Condition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddStringCondition(
    QLConditionPB *const condition, const string &column_name, const QLOperator op,
    const string &value) {
  SetStringCondition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddCondition(QLConditionPB *const condition, const QLOperator op) {
  condition->add_operands()->mutable_condition()->set_op(op);
}

void TableHandle::AddColumns(const std::vector<std::string>& columns, QLReadRequestPB* req) {
  QLRSRowDescPB *rsrow_desc = req->mutable_rsrow_desc();
  for (const auto column : columns) {
    auto id = ColumnId(column);
    req->add_selected_exprs()->set_column_id(id);
    req->mutable_column_refs()->add_ids(id);

    QLRSColDescPB *rscol_desc = rsrow_desc->add_rscol_descs();
    rscol_desc->set_name(column);
    column_types_[column_ids_[column]]->ToQLTypePB(rscol_desc->mutable_ql_type());
  }
}

Status FlushSession(YBSession *session) {
  Synchronizer s;
  YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
  session->FlushAsync(&cb);
  return s.Wait();
}

void QLDmlTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  // Start minicluster and wait for tablet servers to connect to master.
  MiniClusterOptions opts;
  opts.num_tablet_servers = 3;
  cluster_.reset(new MiniCluster(env_.get(), opts));
  ASSERT_OK(cluster_->Start());

  // Connect to the cluster.
  ASSERT_OK(cluster_->CreateClient(nullptr, &client_));

  // Create test table
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name()));
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

} // namespace client
} // namespace yb
