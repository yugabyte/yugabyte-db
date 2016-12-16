// Copyright (c) YugaByte, Inc.

#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/yb_op.h"
#include "yb/client/callbacks.h"
#include "yb/common/ysql_protocol.pb.h"
#include "yb/common/ysql_rowblock.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/util/async_util.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

using std::string;
using std::vector;
using std::shared_ptr;
using std::unique_ptr;

static const char kTableName[] = "ysql_client_test_table";

class YsqlDmlTest : public YBTest {
 public:
  YsqlDmlTest() {
  }

  virtual void SetUp() override {
    YBTest::SetUp();

    // Start minicluster and wait for tablet servers to connect to master.
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    // Connect to the cluster.
    ASSERT_OK(YBClientBuilder()
                  .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr().ToString())
                  .Build(&client_));

    // Create test table
    // create table t (h1 int, h2 varchar, r1 int, r2 varchar, c1 int, c2 varchar,
    //     primary key ((h1, h2), r1, r2));
    YBSchemaBuilder b;
    YBSchema schema;
    b.AddColumn("h1")->Type(YBColumnSchema::INT32)->NotNull();
    b.AddColumn("h2")->Type(YBColumnSchema::STRING)->NotNull();
    b.AddColumn("r1")->Type(YBColumnSchema::INT32)->NotNull();
    b.AddColumn("r2")->Type(YBColumnSchema::STRING)->NotNull();
    b.AddColumn("c1")->Type(YBColumnSchema::INT32);
    b.AddColumn("c2")->Type(YBColumnSchema::STRING);
    b.SetPrimaryKey({"h1", "h2", "r1", "r2"});
    CHECK_OK(b.Build(&schema));

    shared_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(kTableName)
                  .table_type(YBTableType::YSQL_TABLE_TYPE)
                  .schema(&schema)
                  .num_replicas(3)
                  .add_hash_partitions({"h1", "h2"}, 2)
                  .Create());

    ASSERT_OK(client_->OpenTable(kTableName, &table_));
  }

  virtual void TearDown() override {
    if (table_) {
      ASSERT_OK(client_->DeleteTable(kTableName));
    }
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }
    YBTest::TearDown();
  }

  shared_ptr<YBSqlWriteOp> NewWriteOp(YSQLWriteRequestPB::YSQLStmtType type) {
    shared_ptr<YBSqlWriteOp> op(table_->NewYSQLWrite());
    auto* req = op->mutable_request();
    req->set_type(type);
    req->set_client(YSQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    req->set_hash_code(0);
    return op;
  }

  shared_ptr<YBSqlReadOp> NewReadOp() {
    shared_ptr<YBSqlReadOp> op(table_->NewYSQLRead());
    auto* req = op->mutable_request();
    req->set_client(YSQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    req->set_hash_code(0);
    return op;
  }

  Status FlushSession(YBSession* session) {
    Synchronizer s;
    YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
    session->FlushAsync(&cb);
    return s.Wait();
  }

  int32_t ColumnId(size_t column_idx) {
    return table_->schema().ColumnId(column_idx);
  }

  void SetInt32ColumnValue(
      YSQLColumnValuePB* column_value, const size_t column_idx, const int32_t value) {
    column_value->set_column_id(ColumnId(column_idx));
    column_value->mutable_value()->set_datatype(INT32);
    column_value->mutable_value()->set_int32_value(value);
  }

  void SetStringColumnValue(
      YSQLColumnValuePB* column_value, const size_t column_idx, const string& value) {
    column_value->set_column_id(ColumnId(column_idx));
    column_value->mutable_value()->set_datatype(STRING);
    column_value->mutable_value()->set_string_value(value);
  }

  // Set a column id without value - for DELETE
  void SetColumn(
      YSQLColumnValuePB* column_value, const size_t column_idx) {
    column_value->set_column_id(ColumnId(column_idx));
  }

  void SetInt32Relation(
      YSQLReadRequestPB::YSQLRelationPB* relation, const size_t column_idx, const YSQLOperator op,
      const int32_t value) {
    relation->set_range_column_id(ColumnId(column_idx));
    relation->set_op(op);
    relation->mutable_value()->set_datatype(INT32);
    relation->mutable_value()->set_int32_value(value);
  }

  void SetStringRelation(
      YSQLReadRequestPB::YSQLRelationPB* relation, const size_t column_idx, const YSQLOperator op,
      const string& value) {
    relation->set_range_column_id(ColumnId(column_idx));
    relation->set_op(op);
    relation->mutable_value()->set_datatype(STRING);
    relation->mutable_value()->set_string_value(value);
  }

 protected:
  shared_ptr<MiniCluster> cluster_;
  shared_ptr<YBClient> client_;
  shared_ptr<YBTable> table_;
};

TEST_F(YsqlDmlTest, TestInsertUpdateAndSelect) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetInt32ColumnValue(req->add_column_values(), 4, 3);
    SetStringColumnValue(req->add_column_values(), 5, "c");
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(0));
    req->add_column_ids(ColumnId(1));
    req->add_column_ids(ColumnId(2));
    req->add_column_ids(ColumnId(3));
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.column(0).int32_value(), 1);
    EXPECT_EQ(row.column(1).string_value(), "a");
    EXPECT_EQ(row.column(2).int32_value(), 2);
    EXPECT_EQ(row.column(3).string_value(), "b");
    EXPECT_EQ(row.column(4).int32_value(), 3);
    EXPECT_EQ(row.column(5).string_value(), "c");
  }

  {
    // update t set c1 = 4, c2 = 'd' where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_UPDATE);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetInt32ColumnValue(req->add_column_values(), 4, 4);
    SetStringColumnValue(req->add_column_values(), 5, "d");
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    // Flush manually and async
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session = client_->NewSession(true /* read_only */);
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    CHECK_OK(session->Apply(op));
    CHECK_OK(FlushSession(session.get()));

    // Expect 4, 'd' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.int32_value(0), 4);
    EXPECT_EQ(row.string_value(1), "d");
  }
}

TEST_F(YsqlDmlTest, TestInsertMultipleRows) {
  {
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    YSQLWriteRequestPB* req;

    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    shared_ptr<YBSqlWriteOp> op1 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op1->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetInt32ColumnValue(req->add_column_values(), 4, 3);
    SetStringColumnValue(req->add_column_values(), 5, "c");
    CHECK_OK(session->Apply(op1));

    // insert into t values (1, 'a', 2, 'd', 4, 'e');
    shared_ptr<YBSqlWriteOp> op2 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op2->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "d");
    SetInt32ColumnValue(req->add_column_values(), 4, 4);
    SetStringColumnValue(req->add_column_values(), 5, "e");
    CHECK_OK(session->Apply(op2));
    CHECK_OK(FlushSession(session.get()));

    Synchronizer s;
    YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
    session->FlushAsync(&cb);
    CHECK_OK(s.Wait());

    EXPECT_EQ(op1->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    EXPECT_EQ(op2->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(0));
    req->add_column_ids(ColumnId(1));
    req->add_column_ids(ColumnId(2));
    req->add_column_ids(ColumnId(3));
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    {
      shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
      CHECK_OK(session->Apply(op));
    }

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    {
      unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
      EXPECT_EQ(rowblock->row_count(), 1);
      const auto& row = rowblock->row(0);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2);
      EXPECT_EQ(row.string_value(3), "b");
      EXPECT_EQ(row.int32_value(4), 3);
      EXPECT_EQ(row.string_value(5), "c");
    }

    // Reuse op and update where clause to:
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'd';
    SetStringRelation(req->mutable_relations(1), 3, YSQL_OP_EQUAL, "d");
    {
      shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
      CHECK_OK(session->Apply(op));
    }

    // Expect 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    {
      unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
      EXPECT_EQ(rowblock->row_count(), 1);
      const auto& row = rowblock->row(0);
      EXPECT_EQ(row.column(0).int32_value(), 1);
      EXPECT_EQ(row.column(1).string_value(), "a");
      EXPECT_EQ(row.column(2).int32_value(), 2);
      EXPECT_EQ(row.column(3).string_value(), "d");
      EXPECT_EQ(row.column(4).int32_value(), 4);
      EXPECT_EQ(row.column(5).string_value(), "e");
    }
  }
}

TEST_F(YsqlDmlTest, TestUpsert) {
  {
    // update t set c1 = 3 where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlWriteOp> op(table_->NewYSQLWrite());
    auto* req = op->mutable_request();
    req->set_type(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req->set_client(YSQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    req->set_hash_code(0);
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetInt32ColumnValue(req->add_column_values(), 4, 3);
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(0));
    req->add_column_ids(ColumnId(1));
    req->add_column_ids(ColumnId(2));
    req->add_column_ids(ColumnId(3));
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, null returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.column(0).int32_value(), 1);
    EXPECT_EQ(row.column(1).string_value(), "a");
    EXPECT_EQ(row.column(2).int32_value(), 2);
    EXPECT_EQ(row.column(3).string_value(), "b");
    EXPECT_EQ(row.column(4).int32_value(), 3);
    EXPECT_TRUE(row.column(5).IsNull());
  }

  {
    // update t set c2 = 'c' where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetStringColumnValue(req->add_column_values(), 5, "c");
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(0));
    req->add_column_ids(ColumnId(1));
    req->add_column_ids(ColumnId(2));
    req->add_column_ids(ColumnId(3));
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.column(0).int32_value(), 1);
    EXPECT_EQ(row.column(1).string_value(), "a");
    EXPECT_EQ(row.column(2).int32_value(), 2);
    EXPECT_EQ(row.column(3).string_value(), "b");
    EXPECT_EQ(row.column(4).int32_value(), 3);
    EXPECT_EQ(row.column(5).string_value(), "c");
  }
}

TEST_F(YsqlDmlTest, TestDelete) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetInt32ColumnValue(req->add_column_values(), 4, 3);
    SetStringColumnValue(req->add_column_values(), 5, "c");
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // delete c1 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_DELETE);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    SetColumn(req->add_column_values(), 4);
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect null, 'c' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_TRUE(row.column(0).IsNull());
    EXPECT_EQ(row.column(1).string_value(), "c");
  }

  {
    // delete from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_DELETE);
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32ColumnValue(req->add_range_column_values(), 2, 2);
    SetStringColumnValue(req->add_range_column_values(), 3, "b");
    shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect no row returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 0);
  }
}

TEST_F(YsqlDmlTest, TestError) {
  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 <> 'b';
    shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* req = op->mutable_request();
    SetInt32ColumnValue(req->add_hashed_column_values(), 0, 1);
    SetStringColumnValue(req->add_hashed_column_values(), 1, "a");
    SetInt32Relation(req->add_relations(), 2, YSQL_OP_EQUAL, 2);
    SetStringRelation(req->add_relations(), 3, YSQL_OP_NOT_EQUAL, "b");
    req->add_column_ids(ColumnId(4));
    req->add_column_ids(ColumnId(5));
    shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not-supported error
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_RUNTIME_ERROR);
    EXPECT_EQ(op->response().error_message(), "Only equal relation operator is supported");
  }
}

}  // namespace client
}  // namespace yb
