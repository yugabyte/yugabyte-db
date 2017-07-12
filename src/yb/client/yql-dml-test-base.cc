//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/yql-dml-test-base.h"

DECLARE_bool(mini_cluster_reuse_data);

namespace yb {
namespace client {

const client::YBTableName kTableName("my_keyspace", "yql_client_test_table");

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
        .Create());
  }

  EXPECT_OK(client->OpenTable(table_name, &table_));

  schema = table_->schema();
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    EXPECT_TRUE(
       column_ids_.emplace(schema.Column(i).name(), yb::ColumnId(schema.ColumnId(i))).second);
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

std::shared_ptr<YBqlWriteOp> TableHandle::NewWriteOp(YQLWriteRequestPB::YQLStmtType type) {
  auto op = std::make_shared<YBqlWriteOp>(table_);
  auto *req = SetupRequest(op);
  req->set_type(type);
  return op;
}

std::shared_ptr<YBqlReadOp> TableHandle::NewReadOp() {
  shared_ptr<YBqlReadOp> op(table_->NewYQLRead());
  SetupRequest(op);
  return op;
}

void TableHandle::SetInt32ColumnValue(
    YQLColumnValuePB *column_value, const string &column_name, const int32_t value,
    YBPartialRow *prow, int prow_index) {

  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_int32_value(value);

  if (prow != nullptr) {
    ASSERT_OK(prow->SetInt32(prow_index, value));
  }
}

void TableHandle::SetStringColumnValue(
    YQLColumnValuePB *column_value, const string &column_name, const string &value,
    YBPartialRow *prow, int prow_index) {

  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_string_value(value);

  if (prow != nullptr) {
    ASSERT_OK(prow->SetString(prow_index, value));
  }
}

void TableHandle::SetColumn(YQLColumnValuePB *column_value, const string &column_name) {
  column_value->set_column_id(ColumnId(column_name));
}

void TableHandle::SetInt32Condition(
    YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
    const int32_t value) {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto *const val = condition->add_operands()->mutable_value();
  val->set_int32_value(value);
}

void TableHandle::SetStringCondition(
    YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
    const string &value) {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto *const val = condition->add_operands()->mutable_value();
  val->set_string_value(value);
}

void TableHandle::AddInt32Condition(
    YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
    const int32_t value) {
  SetInt32Condition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddStringCondition(
    YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
    const string &value) {
  SetStringCondition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddCondition(YQLConditionPB *const condition, const YQLOperator op) {
  condition->add_operands()->mutable_condition()->set_op(op);
}

void TableHandle::AddColumns(const std::vector<std::string>& columns, YQLReadRequestPB* req) {
  for (const auto column : columns) {
    auto id = ColumnId(column);
    req->add_column_ids(id);
    req->mutable_column_refs()->add_ids(id);
  }
}

Status FlushSession(YBSession *session) {
  Synchronizer s;
  YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
  session->FlushAsync(&cb);
  return s.Wait();
}

void YqlDmlTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  // Start minicluster and wait for tablet servers to connect to master.
  MiniClusterOptions opts;
  opts.num_tablet_servers = 3;
  cluster_.reset(new MiniCluster(env_.get(), opts));
  ASSERT_OK(cluster_->Start());

  // Connect to the cluster.
  ASSERT_OK(YBClientBuilder()
      .add_master_server_addr(yb::ToString(cluster_->mini_master()->bound_rpc_addr()))
      .Build(&client_));

  // Create test table
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name()));
}

void YqlDmlTestBase::DoTearDown() {
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
