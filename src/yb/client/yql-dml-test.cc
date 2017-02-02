// Copyright (c) YugaByte, Inc.

#include "yb/client/yql-dml-base.h"
#include "yb/sql/util/rows_result.h"

namespace yb {
namespace client {

using yb::sql::RowsResult;

// Verify all column values of a row. We use a macro instead of a function so that EXPECT_EQ can
// show the caller's line number should the test fails.
#define EXPECT_ROW_VALUES(row, h1, h2, r1, r2, c1, c2) \
  do {                                                 \
    EXPECT_EQ(row.column(0).int32_value(), h1);        \
    EXPECT_EQ(row.column(1).string_value(), h2);       \
    EXPECT_EQ(row.column(2).int32_value(), r1);        \
    EXPECT_EQ(row.column(3).string_value(), r2);       \
    EXPECT_EQ(row.column(4).int32_value(), c1);        \
    EXPECT_EQ(row.column(5).string_value(), c2);       \
  } while (false)

class YqlDmlTest : public YqlDmlBase {
 public:
  YqlDmlTest() {
  }

  virtual void addColumns(YBSchemaBuilder *b) override {
    b->AddColumn("h1")->Type(YBColumnSchema::INT32)->HashPrimaryKey()->NotNull();
    b->AddColumn("h2")->Type(YBColumnSchema::STRING)->HashPrimaryKey()->NotNull();
    b->AddColumn("r1")->Type(YBColumnSchema::INT32)->PrimaryKey()->NotNull();
    b->AddColumn("r2")->Type(YBColumnSchema::STRING)->PrimaryKey()->NotNull();
    b->AddColumn("c1")->Type(YBColumnSchema::INT32);
    b->AddColumn("c2")->Type(YBColumnSchema::STRING);
  }

  // Insert a full, single row, equivalent to the insert statement below. Return a YB write op that
  // has been applied.
  //   insert into t values (h1, h2, r1, r2, c1, c2);
  shared_ptr<YBqlWriteOp> InsertRow(
      const shared_ptr<YBSession>& session,
      const int32 h1, const string& h2,
      const int32 r1, const string& r2,
      const int32 c1, const string& c2) {

    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", h1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", h2, prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", r1);
    SetStringColumnValue(req->add_range_column_values(), "r2", r2);
    SetInt32ColumnValue(req->add_column_values(), "c1", c1);
    SetStringColumnValue(req->add_column_values(), "c2", c2);
    CHECK_OK(session->Apply(op));
    return op;
  }

  // Select the specified columns of a row using a primary key, equivalent to the select statement
  // below. Return a YB read op that has been applied.
  //   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
  shared_ptr<YBqlReadOp> SelectRow(
      const shared_ptr<YBSession>& session,
      const vector<string>& columns,
      const int32 h1, const string& h2,
      const int32 r1, const string& r2) {

    const shared_ptr<YBqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", h1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", h2, prow, 1);
    auto* const condition = req->mutable_where_condition();
    condition->set_op(YQL_OP_AND);
    AddInt32Condition(condition, "r1", YQL_OP_EQUAL, r1);
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, r2);
    for (const auto column : columns) {
      req->add_column_ids(ColumnId(column));
    }
    CHECK_OK(session->Apply(op));
    return op;
  }
};

TEST_F(YqlDmlTest, TestInsertUpdateAndSelect) {
  {
    // Test inserting a row.
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 3, "c");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // Test selecting a row.
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test updating the row.
    // update t set c1 = 4, c2 = 'd' where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_UPDATE);
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

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // Test selecting the row back, but flush manually and using async API (inside FlushSession).
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session = client_->NewSession(true /* read_only */);
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    const shared_ptr<YBqlReadOp> op = SelectRow(session, {"c1", "c2"}, 1, "a", 2, "b");
    CHECK_OK(FlushSession(session.get()));

    // Expect 4, 'd' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.int32_value(0), 4);
    EXPECT_EQ(row.string_value(1), "d");
  }
}

TEST_F(YqlDmlTest, TestInsertMultipleRows) {
  {
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));

    // Test inserting 2 rows.
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    // insert into t values (1, 'a', 2, 'd', 4, 'e');
    const shared_ptr<YBqlWriteOp> op1 = InsertRow(session, 1, "a", 2, "b", 3, "c");
    const shared_ptr<YBqlWriteOp> op2 = InsertRow(session, 1, "a", 2, "d", 4, "e");

    CHECK_OK(FlushSession(session.get()));
    EXPECT_EQ(op1->response().status(), YQLResponsePB::YQL_STATUS_OK);
    EXPECT_EQ(op2->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // Test selecting the first row back.
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_where_condition();
    condition->set_op(YQL_OP_AND);
    AddInt32Condition(condition, "r1", YQL_OP_EQUAL, 2);
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, "b");
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
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    {
      unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
      EXPECT_EQ(rowblock->row_count(), 1);
      EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
    }

    // Test reusing the read op and updating where clause to select the other row.
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'd';
    req->mutable_where_condition()->mutable_operands()->RemoveLast();
    AddStringCondition(req->mutable_where_condition(), "r2", YQL_OP_EQUAL, "d");
    {
      const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
      CHECK_OK(session->Apply(op));
    }

    // Expect 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    {
      unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
      EXPECT_EQ(rowblock->row_count(), 1);
      EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "d", 4, "e");
    }
  }
}

TEST_F(YqlDmlTest, TestSelectMultipleRows) {
  {
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));

    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    // insert into t values (1, 'a', 2, 'd', 4, 'e');
    const shared_ptr<YBqlWriteOp> op1 = InsertRow(session, 1, "a", 2, "b", 3, "c");
    const shared_ptr<YBqlWriteOp> op2 = InsertRow(session, 1, "a", 2, "d", 4, "e");

    CHECK_OK(FlushSession(session.get()));
    EXPECT_EQ(op1->response().status(), YQLResponsePB::YQL_STATUS_OK);
    EXPECT_EQ(op2->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // Test selecting 2 rows with an OR condition.
    // select * from t where h1 = 1 and h2 = 'a' and r2 = 'b' or r2 = 'd';
    const shared_ptr<YBqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_where_condition();
    condition->set_op(YQL_OP_OR);
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, "b");
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, "d");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' and 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 2);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
    EXPECT_ROW_VALUES(rowblock->row(1), 1, "a", 2, "d", 4, "e");
  }

  {
    // Test selecting 2 rows with AND + OR column conditions.
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and (r2 = 'b' or r2 = 'd');
    const shared_ptr<YBqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* condition = req->mutable_where_condition();
    condition->set_op(YQL_OP_AND);
    AddInt32Condition(condition, "r1", YQL_OP_EQUAL, 2);
    condition = condition->add_operands()->mutable_condition();
    condition->set_op(YQL_OP_OR);
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, "b");
    AddStringCondition(condition, "r2", YQL_OP_EQUAL, "d");
    req->add_column_ids(ColumnId("h1"));
    req->add_column_ids(ColumnId("h2"));
    req->add_column_ids(ColumnId("r1"));
    req->add_column_ids(ColumnId("r2"));
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect 1, 'a', 2, 'b', 3, 'c' and 1, 'a', 2, 'd', 4, 'e' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 2);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
    EXPECT_ROW_VALUES(rowblock->row(1), 1, "a", 2, "d", 4, "e");
  }
}

TEST_F(YqlDmlTest, TestSelectWithoutConditionWithLimit) {
  {
    // Insert 100 rows.
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    // insert into t values (1, 'a', 3, 'b', 4, 'c');
    // insert into t values (1, 'a', 4, 'b', 5, 'c');
    // ...
    // insert into t values (1, 'a', 101, 'b', 102, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    vector<shared_ptr<YBqlWriteOp>> ops;
    for (int32_t i = 0; i < 100; i++) {
      ops.push_back(InsertRow(session, 1, "a", 2 + i, "b", 3 + i, "c"));
    }
    Synchronizer s;
    YBStatusMemberCallback<Synchronizer> cb(&s, &Synchronizer::StatusCB);
    session->FlushAsync(&cb);
    CHECK_OK(s.Wait());
    for (const auto op : ops) {
      EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    }
  }

  {
    // Test selecting multiple rows with a row limit.
    // select * from t where h1 = 1 and h2 = 'a' limit 5;
    const shared_ptr<YBqlReadOp> op = NewReadOp();
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
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 5);
    for (int32_t i = 0; i < 5; i++) {
      EXPECT_ROW_VALUES(rowblock->row(i), 1, "a", 2 + i, "b", 3 + i, "c");
    }
  }
}

TEST_F(YqlDmlTest, TestUpsert) {
  {
    // Test upserting a row (update as insert).
    // update t set c1 = 3 where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlWriteOp> op(table_->NewYQLWrite());
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    req->set_type(YQLWriteRequestPB::YQL_STMT_INSERT);
    req->set_client(YQL_CLIENT_CQL);
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

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, null returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
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
    // Test upsert to "insert" an additional column ("c2").
    // update t set c2 = 'c' where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetStringColumnValue(req->add_column_values(), "c2", "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }
}

TEST_F(YqlDmlTest, TestDelete) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 3, "c");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // Test deleting a column ("c1").
    // delete c1 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetColumn(req->add_column_values(), "c1");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op = SelectRow(session, {"c1", "c2"}, 1, "a", 2, "b");

    // Expect null, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_TRUE(row.column(0).IsNull());
    EXPECT_EQ(row.column(1).string_value(), "c");
  }

  {
    // Test deleting the whole row.
    // delete from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op = SelectRow(session, {"c1", "c2"}, 1, "a", 2, "b");

    // Expect no row returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 0);
  }
}

TEST_F(YqlDmlTest, TestConditionalInsert) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 3, "c");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF NOT EXISTS when the row exists
    // insert into t values (1, 'a', 2, 'b', 4, 'd') if not exists;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "d");
    auto* const condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_NOT_EXISTS);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_FALSE(row.column(0).bool_value());
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF NOT EXISTS AND a column condition when the row exists and column value is different.
    // insert into t values (1, 'a', 2, 'b', 4, 'd') if not exists or c2 = 'd';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "d");
    auto* condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_OR);
    AddCondition(condition, YQL_OP_NOT_EXISTS);
    AddStringCondition(condition, "c2", YQL_OP_EQUAL, "d");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not applied, return c2 = 'd'. Verify column names ("[applied]" and "c2") also.
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_EQ(rowblock->schema().column(1).name(), "c2");
    EXPECT_EQ(rowblock->schema().column(1).type_info()->type(), STRING);
    EXPECT_FALSE(row.column(0).bool_value());
    EXPECT_EQ(row.column(1).string_value(), "c");
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF NOT EXISTS AND a column condition when the row exists and column value matches.
    // insert into t values (1, 'a', 2, 'b', 4, 'd') if not exists or c2 = 'c';
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 4);
    SetStringColumnValue(req->add_column_values(), "c2", "d");
    auto* condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_OR);
    AddCondition(condition, YQL_OP_NOT_EXISTS);
    AddStringCondition(condition, "c2", YQL_OP_EQUAL, "c");
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_TRUE(row.column(0).bool_value());
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 4, "d");
  }

  {
    // Sanity check: test regular insert to override the old row.
    // insert into t values (1, 'a', 2, 'b', 5, 'e');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 5, "e");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 5, 'e' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 5, "e");
  }
}

TEST_F(YqlDmlTest, TestConditionalUpdate) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 3, "c");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF NOT EXISTS when the row exists.
    // update t set c1 = 6 where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b' if not exists;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 6);
    auto* const condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_NOT_EXISTS);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_FALSE(row.column(0).bool_value());
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF EXISTS when the row exists.
    // update t set c1 = 6 where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b' if exists;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetInt32ColumnValue(req->add_column_values(), "c1", 6);
    auto* const condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_EXISTS);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_TRUE(row.column(0).bool_value());
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 6, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 6, "c");
  }
}

TEST_F(YqlDmlTest, TestConditionalDelete) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    const shared_ptr<YBqlWriteOp> op = InsertRow(session, 1, "a", 2, "b", 3, "c");
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF with a column condition when the column value is different.
    // delete c1 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b' if c1 = 4;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetColumn(req->add_column_values(), "c1");
    SetInt32Condition(req->mutable_if_condition(), "c1", YQL_OP_EQUAL, 4);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not applied, return c1 = 3. Verify column names also.
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().num_columns(), 2);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_EQ(rowblock->schema().column(1).name(), "c1");
    EXPECT_EQ(rowblock->schema().column(1).type_info()->type(), INT32);
    EXPECT_FALSE(row.column(0).bool_value());
    EXPECT_EQ(row.column(1).int32_value(), 3);
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', 3, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    EXPECT_ROW_VALUES(rowblock->row(0), 1, "a", 2, "b", 3, "c");
  }

  {
    // Test IF EXISTS AND a column condition when the row exists and the column value matches.
    // delete c1 from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b' if exists and c1 = 3;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "b");
    SetColumn(req->add_column_values(), "c1");
    auto* const condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_AND);
    AddCondition(condition, YQL_OP_EXISTS);
    AddInt32Condition(condition, "c1", YQL_OP_EQUAL, 3);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_TRUE(row.column(0).bool_value());
  }

  {
    // select * from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'b';
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    const shared_ptr<YBqlReadOp> op =
        SelectRow(session, {"h1", "h2", "r1", "r2", "c1", "c2"}, 1, "a", 2, "b");

    // Expect 1, 'a', 2, 'b', null, 'c' returned
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(row.column(0).int32_value(), 1);
    EXPECT_EQ(row.column(1).string_value(), "a");
    EXPECT_EQ(row.column(2).int32_value(), 2);
    EXPECT_EQ(row.column(3).string_value(), "b");
    EXPECT_TRUE(row.column(4).IsNull());
    EXPECT_EQ(row.column(5).string_value(), "c");
  }

  {
    // Test deleting the whole row with IF EXISTS when the row does not exist (wrong "r1").
    // delete from t where h1 = 1 and h2 = 'a' and r1 = 2 and r2 = 'c' if exists;
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_DELETE);
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    SetInt32ColumnValue(req->add_range_column_values(), "r1", 2);
    SetStringColumnValue(req->add_range_column_values(), "r2", "c");
    auto* const condition = req->mutable_if_condition();
    condition->set_op(YQL_OP_EXISTS);
    const shared_ptr<YBSession> session(client_->NewSession(false /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect not applied
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
    unique_ptr<YQLRowBlock> rowblock(RowsResult(op.get()).GetRowBlock());
    EXPECT_EQ(rowblock->row_count(), 1);
    const auto& row = rowblock->row(0);
    EXPECT_EQ(rowblock->schema().column(0).name(), "[applied]");
    EXPECT_EQ(rowblock->schema().column(0).type_info()->type(), BOOL);
    EXPECT_FALSE(row.column(0).bool_value());
  }
}

TEST_F(YqlDmlTest, TestError) {
  {
    // insert into t values (1, 'a', 2, 'b', 3, 'c');
    const shared_ptr<YBqlWriteOp> op = NewWriteOp(YQLWriteRequestPB::YQL_STMT_INSERT);
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

    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_OK);
  }
  {
    // Test selecting with incomparable column condition (int32 column "r1" with a string value).
    // select c1, c2 from t where h1 = 1 and h2 = 'a' and r1 <> '2' and r2 <> 'b';
    const shared_ptr<YBqlReadOp> op = NewReadOp();
    auto* const req = op->mutable_request();
    YBPartialRow *prow = op->mutable_row();
    SetInt32ColumnValue(req->add_hashed_column_values(), "h1", 1, prow, 0);
    SetStringColumnValue(req->add_hashed_column_values(), "h2", "a", prow, 1);
    auto* const condition = req->mutable_where_condition();
    condition->set_op(YQL_OP_AND);
    AddStringCondition(condition, "r1", YQL_OP_NOT_EQUAL, "2");
    AddStringCondition(condition, "r2", YQL_OP_NOT_EQUAL, "b");
    req->add_column_ids(ColumnId("c1"));
    req->add_column_ids(ColumnId("c2"));
    const shared_ptr<YBSession> session(client_->NewSession(true /* read_only */));
    CHECK_OK(session->Apply(op));

    // Expect values not comparable error.
    EXPECT_EQ(op->response().status(), YQLResponsePB::YQL_STATUS_RUNTIME_ERROR);
    EXPECT_EQ(op->response().error_message(), "values not comparable");
  }
}

}  // namespace client
}  // namespace yb
