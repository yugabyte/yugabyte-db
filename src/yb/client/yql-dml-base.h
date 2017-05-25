// Copyright (c) YugaByte, Inc.

#ifndef YB_CLIENT_YQL_DML_BASE_H
#define YB_CLIENT_YQL_DML_BASE_H

#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"
#include "yb/client/callbacks.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/common/yql_rowblock.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/util/async_util.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

using std::string;
using std::vector;
using std::shared_ptr;
using std::unique_ptr;

static const client::YBTableName kTableName("my_keyspace", "yql_client_test_table");

class YqlDmlBase: public YBMiniClusterTestBase<MiniCluster> {
 public:
  YqlDmlBase() {
  }

  virtual void addColumns(YBSchemaBuilder *b) = 0;

  virtual void SetUp() override {
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

    YBSchemaBuilder b;
    YBSchema schema;
    addColumns(&b);
    CHECK_OK(b.Build(&schema));

    unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(kTableName)
        .table_type(YBTableType::YQL_TABLE_TYPE)
        .schema(&schema)
        .num_replicas(3)
        .Create());

    ASSERT_OK(client_->OpenTable(kTableName, &table_));

    schema = table_->schema();
    for (size_t i = 0; i < schema.num_columns(); ++i) {
      column_ids_[schema.Column(i).name()] = schema.ColumnId(i);
    }
  }

  virtual void DoTearDown() override {
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

  shared_ptr<YBqlWriteOp> NewWriteOp(YQLWriteRequestPB::YQLStmtType type) {
    shared_ptr<YBqlWriteOp> op(table_->NewYQLWrite());
    auto *req = op->mutable_request();
    req->set_type(type);
    req->set_client(YQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    return op;
  }

  shared_ptr<YBqlReadOp> NewReadOp() {
    shared_ptr<YBqlReadOp> op(table_->NewYQLRead());
    auto *req = op->mutable_request();
    req->set_client(YQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    return op;
  }

  Status FlushSession(YBSession *session) {
    Synchronizer s;
    YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
    session->FlushAsync(&cb);
    return s.Wait();
  }

  int32_t ColumnId(const string &column_name) {
    return column_ids_[column_name];
  }

  void SetInt32ColumnValue(
      YQLColumnValuePB *column_value, const string &column_name, const int32_t value,
      YBPartialRow *prow = nullptr, int prow_index = -1) {

    column_value->set_column_id(ColumnId(column_name));
    column_value->mutable_expr()->mutable_value()->set_int32_value(value);

    if (prow != nullptr) {
      CHECK_OK(prow->SetInt32(prow_index, value));
    }
  }

  void SetStringColumnValue(
      YQLColumnValuePB *column_value, const string &column_name, const string &value,
      YBPartialRow *prow = nullptr, int prow_index = -1) {

    column_value->set_column_id(ColumnId(column_name));
    column_value->mutable_expr()->mutable_value()->set_string_value(value);

    if (prow != nullptr) {
      CHECK_OK(prow->SetString(prow_index, value));
    }
  }

  // Set a column id without value - for DELETE
  void SetColumn(
      YQLColumnValuePB *column_value, const string &column_name) {
    column_value->set_column_id(ColumnId(column_name));
  }

  // Set a int32 column value comparison.
  // E.g. <column-id> = <int32-value>
  void SetInt32Condition(
      YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
      const int32_t value) {
    condition->add_operands()->set_column_id(ColumnId(column_name));
    condition->set_op(op);
    auto *const val = condition->add_operands()->mutable_value();
    val->set_int32_value(value);
  }

  // Set a string column value comparison.
  // E.g. <column-id> = <string-value>
  void SetStringCondition(
      YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
      const string &value) {
    condition->add_operands()->set_column_id(ColumnId(column_name));
    condition->set_op(op);
    auto *const val = condition->add_operands()->mutable_value();
    val->set_string_value(value);
  }

  // Add a int32 column value comparison under a logical comparison condition.
  // E.g. Add <column-id> = <int32-value> under "... AND <column-id> = <int32-value>".
  void AddInt32Condition(
      YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
      const int32_t value) {
    SetInt32Condition(condition->add_operands()->mutable_condition(), column_name, op, value);
  }

  // Add a string column value comparison under a logical comparison condition.
  // E.g. Add <column-id> = <string-value> under "... AND <column-id> = <string-value>".
  void AddStringCondition(
      YQLConditionPB *const condition, const string &column_name, const YQLOperator op,
      const string &value) {
    SetStringCondition(condition->add_operands()->mutable_condition(), column_name, op, value);
  }

  // Add a simple comparison operation under a logical comparison condition.
  // E.g. Add <EXISTS> under "... AND <EXISTS>".
  void AddCondition(YQLConditionPB *const condition, const YQLOperator op) {
    condition->add_operands()->mutable_condition()->set_op(op);
  }

 protected:
  shared_ptr<YBClient> client_;
  shared_ptr<YBTable> table_;
  unordered_map<string, yb::ColumnId> column_ids_;
};

}  // namespace client
}  // namespace yb

#endif  // YB_CLIENT_YQL_DML_BASE_H
