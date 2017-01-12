// Copyright (c) YugaByte, Inc.

#include "yb/client/ysql-dml-base.h"

namespace yb {
namespace client {

class YsqlDmlTest : public YsqlDmlBase {
 public:
  YsqlDmlTest() {
  }

  virtual void addColumns(YBSchemaBuilder *b) override {
    b->AddColumn("h1")->Type(YBColumnSchema::INT32)->HashPrimaryKey()->NotNull();
    b->AddColumn("h2")->Type(YBColumnSchema::STRING)->HashPrimaryKey()->NotNull();
    b->AddColumn("r1")->Type(YBColumnSchema::INT32)->PrimaryKey()->NotNull();
    b->AddColumn("r2")->Type(YBColumnSchema::STRING)->PrimaryKey()->NotNull();
    b->AddColumn("c1")->Type(YBColumnSchema::INT32);
    b->AddColumn("c2")->Type(YBColumnSchema::STRING);
  }
};

TEST_F(YsqlDmlTest, TestInsertUpdateAndSelect) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "d");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    // Flush manually and async
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session = client_->NewSession(true /* read_only */);
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
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    YSQLWriteRequestPB* req;

    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSqlWriteOp> op1 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op1->mutable_request();
    YBPartialRow *prow = op1->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    CHECK_OK(session->Apply(op1));

    // insert into t values (1, 'a', 2, 'd', 4, 'e');
    const shared_ptr<YBSqlWriteOp> op2 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op2->mutable_request();
    prow = op2->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "d");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "e");
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
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    {
      const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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
    auto* const opr = req->mutable_condition()->mutable_operands(1);
    opr->Clear();
    SetStringCondition(opr, "r2", YSQL_OP_EQUAL, "d");
    {
      const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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

TEST_F(YsqlDmlTest, TestSelectMultipleRows) {
  {
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    YSQLWriteRequestPB* req;

    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSqlWriteOp> op1 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op1->mutable_request();
    YBPartialRow *prow = op1->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    CHECK_OK(session->Apply(op1));

    // insert into t values (1, 'a', 2, 'd', 4, 'e');
    const shared_ptr<YBSqlWriteOp> op2 = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op2->mutable_request();
    prow = op2->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "d");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "e");
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
    // select * from t where h1 = 1 and h2 = 'a' and r2 = 'b' or r2 = 'd';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_OR);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "d");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' and 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 2);
    {
      const auto& row = rowblock->row(0);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2);
      EXPECT_EQ(row.string_value(3), "b");
      EXPECT_EQ(row.int32_value(4), 3);
      EXPECT_EQ(row.string_value(5), "c");
    }
    {
      const auto& row = rowblock->row(1);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2);
      EXPECT_EQ(row.string_value(3), "d");
      EXPECT_EQ(row.int32_value(4), 4);
      EXPECT_EQ(row.string_value(5), "e");
    }
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and (r2 = 'b' or r2 = 'd');
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    condition = condition->add_operands()->mutable_condition();
    condition->set_op(YSQL_OP_OR);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "d");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' and 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 2);
    {
      const auto& row = rowblock->row(0);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2);
      EXPECT_EQ(row.string_value(3), "b");
      EXPECT_EQ(row.int32_value(4), 3);
      EXPECT_EQ(row.string_value(5), "c");
    }
    {
      const auto& row = rowblock->row(1);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2);
      EXPECT_EQ(row.string_value(3), "d");
      EXPECT_EQ(row.int32_value(4), 4);
      EXPECT_EQ(row.string_value(5), "e");
    }
  }
}

TEST_F(YsqlDmlTest, TestSelectWithoutConditionWithLimit) {
  for (int32_t i = 0; i < 100; i++) {
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    YSQLWriteRequestPB* req;

    // insert 100 rows:
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    // insert into t values (1, 'a', 3, 'b', 4, 'c');
    // insert into t values (1, 'a', 4, 'b', 5, 'c');
    // ...
    // insert into t values (1, 'a', 101, 'b', 102, 'c');
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2 + i);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3 + i);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    CHECK_OK(session->Apply(op));

    Synchronizer s;
    YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
    session->FlushAsync(&cb);
    CHECK_OK(s.Wait());

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' limit 5;
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    req->set_limit(5);
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 5 rows:
    //   1, 'a', 2, 'b', 3, 'c'
    //   1, 'a', 3, 'b', 4, 'c'
    //   1, 'a', 4, 'b', 5, 'c'
    //   1, 'a', 5, 'b', 6, 'c'
    //   1, 'a', 6, 'b', 7, 'c'
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 5);
    for (int32_t i = 0; i < 5; i++) {
      const auto& row = rowblock->row(i);
      EXPECT_EQ(row.int32_value(0), 1);
      EXPECT_EQ(row.string_value(1), "a");
      EXPECT_EQ(row.int32_value(2), 2 + i);
      EXPECT_EQ(row.string_value(3), "b");
      EXPECT_EQ(row.int32_value(4), 3 + i);
      EXPECT_EQ(row.string_value(5), "c");
    }
  }
}

TEST_F(YsqlDmlTest, TestUpsert) {
  {
    // update t set c1 = 3 where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlWriteOp> op(table_->NewYSQLWrite());
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    req->set_type(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    req->set_client(YSQL_CLIENT_CQL);
    req->set_request_id(0);
    req->set_schema_version(0);
    req->set_hash_code(0);
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // delete c1 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetColumn(req->add_column_values(), "c1");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
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
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetInt32Condition(condition->add_operands(), "r1", YSQL_OP_EQUAL, 2);
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_EQUAL, "b");
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect no row returned
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
    unique_ptr<YSQLRowBlock> rowblock(op->GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 0);
  }
}

TEST_F(YsqlDmlTest, TestError) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSqlWriteOp> op = NewWriteOp(YSQLWriteRequestPB::YSQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 3);
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_OK);
  }
  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 <> '2' and r2 <> 'b';
    const shared_ptr<YBSqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_condition();
    condition->set_op(YSQL_OP_AND);
    SetStringCondition(condition->add_operands(), "r1", YSQL_OP_NOT_EQUAL, "2");
    SetStringCondition(condition->add_operands(), "r2", YSQL_OP_NOT_EQUAL, "b");
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect values not comparable error because r1 is an int32 column
    EXPECT_EQ(op->response().status(), YSQLResponsePB::YSQL_STATUS_RUNTIME_ERROR);
    EXPECT_EQ(op->response().error_message(), "values not comparable");
  }
}

}  // namespace client
}  // namespace yb
