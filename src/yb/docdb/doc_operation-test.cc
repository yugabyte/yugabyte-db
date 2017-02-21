// Copyright (c) YugaByte, Inc.

#include "yb/common/partial_row.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/server/hybrid_clock.h"

namespace yb {
namespace docdb {

class DocOperationTest : public DocDBTestBase {
 public:
  DocOperationTest() {
    SeedRandom();
  }

  Schema CreateSchema() {
    ColumnSchema hash_column_schema("k", INT32, false, true);
    ColumnSchema column1_schema("c1", INT32, false, false);
    ColumnSchema column2_schema("c2", INT32, false, false);
    ColumnSchema column3_schema("c3", INT32, false, false);
    const vector<ColumnSchema> columns({hash_column_schema, column1_schema, column2_schema,
                                           column3_schema});
    const vector<ColumnId> col_ids({ColumnId(0), ColumnId(1), ColumnId(2), ColumnId(3)});
    Schema schema(columns, col_ids, 1);
    return schema;
  }

  void AddPrimaryKeyColumn(yb::YQLWriteRequestPB* yql_writereq_pb, int32_t value) {
    auto hashed_column = yql_writereq_pb->add_hashed_column_values();
    hashed_column->set_column_id(0);
    hashed_column->mutable_value()->set_datatype(INT32);
    hashed_column->mutable_value()->set_int32_value(value);
  }

  void AddColumnValues(yb::YQLWriteRequestPB* yql_writereq_pb,
                       const vector<int32_t>& column_values) {
    ASSERT_EQ(3, column_values.size());
    for (int i = 0; i < 3; i++) {
      auto column = yql_writereq_pb->add_column_values();
      column->set_column_id(i + 1);
      column->mutable_value()->set_datatype(INT32);
      column->mutable_value()->set_int32_value(column_values[i]);
    }
  }

  void Write(const YQLWriteRequestPB& yql_writereq_pb, const Schema& schema,
             YQLResponsePB* yql_writeresp_pb) {
    YQLWriteOperation yql_write_op(yql_writereq_pb, schema, yql_writeresp_pb);
    DocWriteBatch doc_write_batch(rocksdb());
    CHECK_OK(yql_write_op.Apply(&doc_write_batch, rocksdb(), HybridTime()));
    WriteToRocksDB(doc_write_batch);
  }

  void AssertWithTTL(YQLWriteRequestPB_YQLStmtType stmt_type) {
    if (stmt_type == YQLWriteRequestPB::YQL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT(Max)]) -> {}
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max)]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max)]) -> 4; ttl: 2.000s
      )#");
    } else {
      AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT(Max)]) -> {}
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max)]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max)]) -> 4; ttl: 2.000s
      )#");
    }
  }

  void AssertWithoutTTL(YQLWriteRequestPB_YQLStmtType stmt_type) {
    if (stmt_type == YQLWriteRequestPB::YQL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT(Max)]) -> {}
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max)]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max)]) -> 4
      )#");
    } else {
      AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT(Max)]) -> {}
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max)]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max)]) -> 4
      )#");
    }
  }

  void RunTestYQLInsertUpdate(YQLWriteRequestPB_YQLStmtType stmt_type, const int ttl = -1) {
    yb::YQLWriteRequestPB yql_writereq_pb;
    yb::YQLResponsePB yql_writeresp_pb;

    // Define the schema.
    Schema schema = CreateSchema();

    yql_writereq_pb.set_type(stmt_type);
    // Add primary key column.
    AddPrimaryKeyColumn(&yql_writereq_pb, 1);

    AddColumnValues(&yql_writereq_pb, vector<int32_t>({2, 3, 4}));

    if (ttl != -1) {
      yql_writereq_pb.set_ttl(ttl);
    }

    // Write to docdb.
    Write(yql_writereq_pb, schema, &yql_writeresp_pb);

    if (ttl == -1) {
      AssertWithoutTTL(stmt_type);
    } else {
      AssertWithTTL(stmt_type);
    }
  }
};

TEST_F(DocOperationTest, TestRedisSetKVWithTTL) {
  // Write key with ttl to docdb.
  auto db = rocksdb();
  yb::RedisWriteRequestPB redis_write_operation_pb;
  auto set_request_pb = redis_write_operation_pb.mutable_set_request();
  set_request_pb->set_ttl(2000);
  redis_write_operation_pb.mutable_key_value()->set_key("abc");
  redis_write_operation_pb.mutable_key_value()->set_hash_code(123);
  redis_write_operation_pb.mutable_key_value()->add_value("xyz");
  RedisWriteOperation redis_write_operation(redis_write_operation_pb, HybridTime::kMax);
  DocWriteBatch doc_write_batch(db);
  CHECK_OK(redis_write_operation.Apply(&doc_write_batch, db, HybridTime()));

  WriteToRocksDB(doc_write_batch);

  // Read key from rocksdb.
  const KeyBytes doc_key = DocKey::FromRedisKey(123, "abc").Encode();
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(read_opts));
  ROCKSDB_SEEK(iter.get(), doc_key.AsSlice());
  ASSERT_TRUE(iter->Valid());

  // Verify correct ttl.
  MonoDelta ttl;
  auto value = iter->value();
  CHECK_OK(Value::DecodeTTL(&value, &ttl));
  EXPECT_EQ(2000, ttl.ToMilliseconds());
}

TEST_F(DocOperationTest, TestYQLInsertWithTTL) {
  RunTestYQLInsertUpdate(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT, 2000);
}

TEST_F(DocOperationTest, TestYQLUpdateWithTTL) {
  RunTestYQLInsertUpdate(YQLWriteRequestPB_YQLStmtType_YQL_STMT_UPDATE, 2000);
}

TEST_F(DocOperationTest, TestYQLInsertWithoutTTL) {
  RunTestYQLInsertUpdate(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT);
}

TEST_F(DocOperationTest, TestYQLUpdateWithoutTTL) {
  RunTestYQLInsertUpdate(YQLWriteRequestPB_YQLStmtType_YQL_STMT_UPDATE);
}

TEST_F(DocOperationTest, TestYQLWriteNulls) {
  yb::YQLWriteRequestPB yql_writereq_pb;
  yb::YQLResponsePB yql_writeresp_pb;

  // Define the schema.
  Schema schema = CreateSchema();
  yql_writereq_pb.set_type(
      YQLWriteRequestPB_YQLStmtType::YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT);

  // Add primary key column.
  AddPrimaryKeyColumn(&yql_writereq_pb, 1);

  // Add null columns.
  for (int i = 0; i < 3; i++) {
    auto column = yql_writereq_pb.add_column_values();
    column->set_column_id(i + 1);
    column->mutable_value()->set_datatype(INT32);
  }

  // Write to docdb.
  Write(yql_writereq_pb, schema, &yql_writeresp_pb);

  // Null columns are converted to tombstones.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT(Max)]) -> {}
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null
      )#");
}
}  // namespace docdb
}  // namespace yb
