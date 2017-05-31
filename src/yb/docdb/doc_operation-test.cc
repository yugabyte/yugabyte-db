// Copyright (c) YugaByte, Inc.

#include <thread>

#include "rocksdb/statistics.h"

#include "yb/common/partial_row.h"

#include "yb/docdb/yql_rocksdb_storage.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_yql_scanspec.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/tostring.h"

DECLARE_uint64(rocksdb_max_file_size_for_compaction);

namespace yb {
namespace docdb {

using server::HybridClock;

namespace {

std::vector<ColumnId> CreateColumnIds(size_t count) {
  std::vector<ColumnId> result;
  result.reserve(count);
  for (size_t i = 0; i != count; ++i) {
    result.emplace_back(i);
  }
  return result;
}

} // namespace

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
    Schema schema(columns, CreateColumnIds(columns.size()), 1);
    return schema;
  }

  void AddPrimaryKeyColumn(yb::YQLWriteRequestPB* yql_writereq_pb, int32_t value) {
    auto hashed_column = yql_writereq_pb->add_hashed_column_values();
    hashed_column->set_column_id(0);
    hashed_column->mutable_expr()->mutable_value()->set_int32_value(value);
  }

  void AddRangeKeyColumn(int32_t column_id, int32_t value, yb::YQLWriteRequestPB* yql_writereq_pb) {
    auto hashed_column = yql_writereq_pb->add_range_column_values();
    hashed_column->set_column_id(column_id);
    hashed_column->mutable_expr()->mutable_value()->set_int32_value(value);
  }

  void AddColumnValues(const Schema& schema,
                       const vector<int32_t>& column_values,
                       yb::YQLWriteRequestPB* yql_writereq_pb) {
    ASSERT_EQ(schema.num_columns() - schema.num_key_columns(), column_values.size());
    for (int i = 0; i < column_values.size(); i++) {
      auto column = yql_writereq_pb->add_column_values();
      column->set_column_id(schema.num_key_columns() + i);
      column->mutable_expr()->mutable_value()->set_int32_value(column_values[i]);
    }
  }

  void WriteYQL(const YQLWriteRequestPB& yql_writereq_pb, const Schema& schema,
             YQLResponsePB* yql_writeresp_pb,
             const HybridTime& hybrid_time = HybridTime::kMax) {
    YQLWriteOperation yql_write_op(yql_writereq_pb, schema, yql_writeresp_pb);
    DocWriteBatch doc_write_batch(rocksdb());
    CHECK_OK(yql_write_op.Apply(&doc_write_batch, rocksdb(), HybridTime()));
    ASSERT_OK(WriteToRocksDB(doc_write_batch, hybrid_time));
  }

  void AssertWithTTL(YQLWriteRequestPB_YQLStmtType stmt_type) {
    if (stmt_type == YQLWriteRequestPB::YQL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max, w=1)]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max, w=2)]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max, w=3)]) -> 4; ttl: 2.000s
      )#");
    } else {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max, w=1)]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max, w=2)]) -> 4; ttl: 2.000s
      )#");
    }
  }

  void AssertWithoutTTL(YQLWriteRequestPB_YQLStmtType stmt_type) {
    if (stmt_type == YQLWriteRequestPB::YQL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max, w=1)]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max, w=2)]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max, w=3)]) -> 4
      )#");
    } else {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max)]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max, w=1)]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max, w=2)]) -> 4
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
    yql_writereq_pb.set_hash_code(0);

    AddColumnValues(schema, {2, 3, 4}, &yql_writereq_pb);

    if (ttl != -1) {
      yql_writereq_pb.set_ttl(ttl);
    }

    // Write to docdb.
    WriteYQL(yql_writereq_pb, schema, &yql_writeresp_pb);

    if (ttl == -1) {
      AssertWithoutTTL(stmt_type);
    } else {
      AssertWithTTL(stmt_type);
    }
  }

  void WriteYQLRow(YQLWriteRequestPB_YQLStmtType stmt_type, const Schema& schema,
                const vector<int32_t>& column_values, int64_t ttl, const HybridTime& hybrid_time) {
    yb::YQLWriteRequestPB yql_writereq_pb;
    yb::YQLResponsePB yql_writeresp_pb;
    yql_writereq_pb.set_type(stmt_type);

    // Add primary key column.
    yql_writereq_pb.set_hash_code(0);
    for (size_t i = 0; i != schema.num_key_columns(); ++i) {
      if (i < schema.num_hash_key_columns()) {
        AddPrimaryKeyColumn(&yql_writereq_pb, column_values[i]);
      } else {
        AddRangeKeyColumn(i, column_values[i], &yql_writereq_pb);
      }
    }
    std::vector<int32_t> values(column_values.begin() + schema.num_key_columns(),
                                column_values.end());
    AddColumnValues(schema, values, &yql_writereq_pb);
    yql_writereq_pb.set_ttl(ttl);

    // Write to docdb.
    WriteYQL(yql_writereq_pb, schema, &yql_writeresp_pb, hybrid_time);
  }

  YQLRowBlock ReadYQLRow(const Schema& schema, int32_t primary_key, const HybridTime& read_time) {
    YQLReadRequestPB yql_read_req;
    YQLColumnValuePB* hash_column = yql_read_req.add_hashed_column_values();
    hash_column->set_column_id(schema.column_id(0));
    YQLValuePB* value_pb = hash_column->mutable_expr()->mutable_value();
    value_pb->set_int32_value(primary_key);

    for (int i = 1; i <= 3 ; i++) {
      yql_read_req.add_column_ids(i);
    }

    YQLReadOperation read_op(yql_read_req);
    YQLRowBlock row_block(schema, vector<ColumnId> ({ColumnId(0), ColumnId(1), ColumnId(2),
                                                        ColumnId(3)}));
    YQLRocksDBStorage yql_storage(rocksdb());
    EXPECT_OK(read_op.Execute(yql_storage, read_time, schema, &row_block));
    return row_block;
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
  ASSERT_OK(redis_write_operation.Apply(&doc_write_batch, db, HybridTime()));

  ASSERT_OK(WriteToRocksDB(doc_write_batch, HybridTime::FromMicros(1000)));

  // Read key from rocksdb.
  const KeyBytes doc_key = DocKey::FromRedisKey(123, "abc").Encode();
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(read_opts));
  ROCKSDB_SEEK(iter.get(), doc_key.AsSlice());
  ASSERT_TRUE(iter->Valid());

  // Verify correct ttl.
  MonoDelta ttl;
  auto value = iter->value();
  ASSERT_OK(Value::DecodeTTL(&value, &ttl));
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
  yql_writereq_pb.set_hash_code(0);

  // Add primary key column.
  AddPrimaryKeyColumn(&yql_writereq_pb, 1);

  // Add null columns.
  for (int i = 0; i < 3; i++) {
    auto column = yql_writereq_pb.add_column_values();
    column->set_column_id(i + 1);
    column->mutable_expr()->mutable_value();
  }

  // Write to docdb.
  WriteYQL(yql_writereq_pb, schema, &yql_writeresp_pb);

  // Null columns are converted to tombstones.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(Max)]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(Max, w=1)]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(Max, w=2)]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(Max, w=3)]) -> DEL
      )#");
}

TEST_F(DocOperationTest, TestYQLReadWriteSimple) {
  yb::YQLWriteRequestPB yql_writereq_pb;
  yb::YQLResponsePB yql_writeresp_pb;

  // Define the schema.
  Schema schema = CreateSchema();
  WriteYQLRow(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
           1000, HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 0));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, w=1)]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, w=2)]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, w=3)]) -> 3; ttl: 1.000s
      )#");

  // Now read the value.
  YQLRowBlock row_block = ReadYQLRow(schema, 1,
                                  HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(2000, 0));
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(1, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(2, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(3, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, TestYQLReadWithoutLivenessColumn) {
  const DocKey doc_key(0, PrimitiveValues(100), PrimitiveValues());
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue(2)), HybridTime(1000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue(3)), HybridTime(2000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue(4)), HybridTime(3000),
                         InitMarkerBehavior::kOptional));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1000)]) -> 2
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2000)]) -> 3
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3000)]) -> 4
      )#");

  // Now verify we can read without the system column id.
  Schema schema = CreateSchema();
  HybridTime read_time(3000);
  YQLRowBlock row_block = ReadYQLRow(schema, 100, read_time);
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(100, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(2, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(3, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(4, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, TestYQLReadWithTombstone) {
  DocKey doc_key(0, PrimitiveValues(100), PrimitiveValues());
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(1000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(2000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(3000),
                         InitMarkerBehavior::kOptional));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3000)]) -> DEL
      )#");

  Schema schema = CreateSchema();
  ScanSpec scan_spec;
  DocRowwiseIterator iter(schema, schema, rocksdb(), HybridTime(3000));
  ASSERT_OK(iter.Init(&scan_spec));
  ASSERT_FALSE(iter.HasNext());

  // Now verify row exists even with one valid column.
  doc_key = DocKey(0, PrimitiveValues(100), PrimitiveValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(1001),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue(2), MonoDelta::FromMilliseconds(1)), HybridTime(2001),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue(101)), HybridTime(3001),
                         InitMarkerBehavior::kOptional));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1001)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2001)]) -> 2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3001)]) -> 101
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3000)]) -> DEL
      )#");

  vector<PrimitiveValue> hashed_components({PrimitiveValue(100)});
  DocYQLScanSpec yql_scan_spec(schema, -1, -1, hashed_components, /* request = */ nullptr);
  DocRowwiseIterator yql_iter(schema, schema, rocksdb(),
                              HybridClock::HybridTimeFromMicroseconds(3000));
  ASSERT_OK(yql_iter.Init(yql_scan_spec));
  ASSERT_TRUE(yql_iter.HasNext());
  YQLValueMap value_map;
  ASSERT_OK(yql_iter.NextRow(schema, &value_map));
  ASSERT_EQ(4, value_map.size());
  EXPECT_EQ(100, value_map.at(ColumnId(0)).int32_value());
  EXPECT_TRUE(YQLValue::IsNull(value_map.at(ColumnId(1))));
  EXPECT_TRUE(YQLValue::IsNull(value_map.at(ColumnId(2))));
  EXPECT_EQ(101, value_map.at(ColumnId(3)).int32_value());

  // Now verify row exists as long as liveness system column exists.
  doc_key = DocKey(0, PrimitiveValues(101), PrimitiveValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key,
                                 PrimitiveValue::SystemColumnId(
                                     SystemColumnIds::kLivenessColumn)),
                         Value(PrimitiveValue(ValueType::kNull)),
                         HybridTime(1000), InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(1000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue(2), MonoDelta::FromMilliseconds(1)), HybridTime(2000),
                         InitMarkerBehavior::kOptional));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue(ValueType::kTombstone)), HybridTime(3000),
                         InitMarkerBehavior::kOptional));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1001)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT(p=0, l=1000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2001)]) -> 2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT(p=0, l=2000)]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3001)]) -> 101
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT(p=0, l=3000)]) -> DEL
SubDocKey(DocKey(0x0000, [101], []), [SystemColumnId(0); HT(p=0, l=1000)]) -> null
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(1); HT(p=0, l=1000)]) -> DEL
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(2); HT(p=0, l=2000)]) -> 2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(3); HT(p=0, l=3000)]) -> DEL
      )#");

  vector<PrimitiveValue> hashed_components_system({PrimitiveValue(101)});
  DocYQLScanSpec yql_scan_spec_system(schema, -1, -1, hashed_components_system, nullptr);
  DocRowwiseIterator yql_iter_system(schema, schema, rocksdb(),
                                     HybridClock::HybridTimeFromMicroseconds(3000));
  ASSERT_OK(yql_iter_system.Init(yql_scan_spec_system));
  ASSERT_TRUE(yql_iter_system.HasNext());
  YQLValueMap value_map_system;
  ASSERT_OK(yql_iter_system.NextRow(schema, &value_map_system));
  ASSERT_EQ(4, value_map_system.size());
  EXPECT_EQ(101, value_map_system.at(ColumnId(0)).int32_value());
  EXPECT_TRUE(YQLValue::IsNull(value_map_system.at(ColumnId(1))));
  EXPECT_TRUE(YQLValue::IsNull(value_map_system.at(ColumnId(2))));
  EXPECT_TRUE(YQLValue::IsNull(value_map_system.at(ColumnId(3))));
}

namespace {

int32_t NewInt(std::mt19937_64* rng, std::unordered_set<int32_t>* existing) {
  std::uniform_int_distribution<int32_t> distribution(std::numeric_limits<int32_t>::min());
  for (;;) {
    auto result = distribution(*rng);
    if (existing->insert(result).second) {
      return result;
    }
  }
}

template<class It>
std::pair<It, It> GetIteratorRange(It begin, It end, It it, YQLOperator op) {
  switch (op) {
    case YQL_OP_EQUAL:
      return std::make_pair(it, it + 1);
    case YQL_OP_LESS_THAN:
      return std::make_pair(begin, it);
    case YQL_OP_LESS_THAN_EQUAL:
      return std::make_pair(begin, it + 1);
    case YQL_OP_GREATER_THAN:
      return std::make_pair(it + 1, end);
    case YQL_OP_GREATER_THAN_EQUAL:
      return std::make_pair(it, end);
    default: // We should not handle all cases here
      LOG(FATAL) << "Unexpected op: " << YQLOperator_Name(op);
      return std::make_pair(begin, end);
  }
}

struct RowData {
  int32_t k;
  int32_t r;
  int32_t v;
};

bool operator==(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k && lhs.r == rhs.r && lhs.v == rhs.v;
}

bool operator<(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k ? lhs.r < rhs.r : lhs.k < rhs.k;
}

std::ostream& operator<<(std::ostream& out, const RowData& row) {
  return out << "{ k = " << row.k << ", r = " << row.r << ", v = " << row.v << " }";
}

class DocOperationRangeFilterTest : public DocOperationTest {
 public:
  void TestWithSortingType(ColumnSchema::SortingType type);
 private:
};

void DocOperationRangeFilterTest::TestWithSortingType(ColumnSchema::SortingType type) {
  DisableCompactions();

  ColumnSchema hash_column("k", INT32, false, true);
  ColumnSchema range_column("r", INT32, false, false, false, type);
  ColumnSchema value_column("v", INT32, false, false);
  auto columns = { hash_column, range_column, value_column };
  Schema schema(columns, CreateColumnIds(columns.size()), 2);

  auto t = HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 0);

  std::mt19937_64 rng;
  Seed(&rng);
  std::unordered_set<int32_t> used_ints;
  int32_t key = NewInt(&rng, &used_ints);
  std::vector<RowData> rows;
  constexpr int32_t kNumRows = 20;
  for (int32_t i = 0; i != kNumRows; ++i) {
    RowData row = {key,
                   NewInt(&rng, &used_ints),
                   NewInt(&rng, &used_ints)};
    rows.push_back(row);

    WriteYQLRow(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT,
                schema,
                { row.k, row.r, row.v },
                1000,
                t);
    FlushRocksDB();
  }
  std::vector<rocksdb::LiveFileMetaData> live_files;
  rocksdb()->GetLiveFilesMetaData(&live_files);
  ASSERT_EQ(kNumRows, live_files.size());

  std::shuffle(rows.begin(), rows.end(), rng);
  auto ordered_rows = rows;
  std::sort(ordered_rows.begin(), ordered_rows.end());

  LOG(INFO) << "Dump:\n" << DocDBDebugDumpToStr();

  std::vector<YQLOperator> operators = {
    YQL_OP_EQUAL,
    YQL_OP_LESS_THAN_EQUAL,
    YQL_OP_GREATER_THAN_EQUAL,
  };

  auto old_iterators =
      rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
  for (auto op : operators) {
    LOG(INFO) << "Testing: " << YQLOperator_Name(op);
    for (auto& row : rows) {
      std::vector<PrimitiveValue> hashed_components = { PrimitiveValue(key) };
      YQLConditionPB condition;
      condition.add_operands()->set_column_id(1_ColId);
      condition.set_op(op);
      condition.add_operands()->mutable_value()->set_int32_value(row.r);

      auto it = std::lower_bound(ordered_rows.begin(), ordered_rows.end(), row);
      auto range = GetIteratorRange(ordered_rows.begin(), ordered_rows.end(), it, op);

      std::vector<RowData> expected_rows(range.first, range.second);
      DocYQLScanSpec yql_scan_spec(schema, -1, -1, hashed_components, &condition);
      DocRowwiseIterator yql_iter(schema, schema, rocksdb(),
          HybridClock::HybridTimeFromMicroseconds(3000));
      ASSERT_OK(yql_iter.Init(yql_scan_spec));
      LOG(INFO) << "Expected rows: " << util::ToString(expected_rows);

      while(yql_iter.HasNext()) {
        YQLValueMap value_map;
        ASSERT_OK(yql_iter.NextRow(schema, &value_map));
        ASSERT_EQ(3, value_map.size());

        RowData fetched_row = { value_map[0_ColId].int32_value(),
                                value_map[1_ColId].int32_value(),
                                value_map[2_ColId].int32_value() };
        LOG(INFO) << "Fetched row: " << fetched_row;
        it = std::lower_bound(expected_rows.begin(), expected_rows.end(), fetched_row);
        ASSERT_NE(it, expected_rows.end());
        ASSERT_EQ(*it, fetched_row);
        expected_rows.erase(it);
      }
      ASSERT_TRUE(expected_rows.empty());
      auto new_iterators =
          rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
      ASSERT_EQ(range.second - range.first, new_iterators - old_iterators);
      old_iterators = new_iterators;
    }
  }
}

} // namespace

TEST_F_EX(DocOperationTest, YQLRangeFilterAscending, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kAscending);
}

TEST_F_EX(DocOperationTest, YQLRangeFiltervDescending, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kDescending);
}

TEST_F(DocOperationTest, TestYQLCompactions) {
  yb::YQLWriteRequestPB yql_writereq_pb;
  yb::YQLResponsePB yql_writeresp_pb;

  HybridTime t0 = HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 0);
  HybridTime t0prime = HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 1);
  HybridTime t1 = HybridClock::AddPhysicalTimeToHybridTime(t0, MonoDelta::FromMilliseconds(1001));

  // Define the schema.
  Schema schema = CreateSchema();
  WriteYQLRow(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, w=1)]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, w=2)]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, w=3)]) -> 3; ttl: 1.000s
      )#");

  CompactHistoryBefore(t1);

  // Verify all entries are purged.
  AssertDocDbDebugDumpStrEq(R"#(
      )#");

  // Add a row with a TTL.
  WriteYQLRow(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, w=1)]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, w=2)]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, w=3)]) -> 3; ttl: 1.000s
     )#");

  // Update the columns with a higher TTL.
  yb::YQLWriteRequestPB yql_update_pb;
  yb::YQLResponsePB yql_update_resp_pb;
  yql_writereq_pb.set_type(
      YQLWriteRequestPB_YQLStmtType::YQLWriteRequestPB_YQLStmtType_YQL_STMT_UPDATE);

  yql_writereq_pb.set_hash_code(0);
  AddPrimaryKeyColumn(&yql_writereq_pb, 1);
  AddColumnValues(schema, {10, 20, 30}, &yql_writereq_pb);
  yql_writereq_pb.set_ttl(2000);

  // Write to docdb at the same physical time and a bumped-up logical time.
  WriteYQL(yql_writereq_pb, schema, &yql_writeresp_pb, t0prime);

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, l=1)]) -> 10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, w=1)]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, l=1, w=1)]) -> 20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, w=2)]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, l=1, w=2)]) -> 30; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, w=3)]) -> 3; ttl: 1.000s
      )#");

  CompactHistoryBefore(t1);

  // Verify the rest of the columns still live.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT(p=1000, l=1)]) -> 10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT(p=1000, l=1, w=1)]) -> 20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT(p=1000, l=1, w=2)]) -> 30; ttl: 2.000s
      )#");

  // Verify reads work well without system column id.
  YQLRowBlock row_block = ReadYQLRow(schema, 1, t1);
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(10, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(20, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(30, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, MaxFileSizeForCompaction) {
  google::FlagSaver flag_saver;

  DisableCompactions();
  auto schema = CreateSchema();

  auto t0 = HybridTime::FromMicrosecondsAndLogicalValue(1000, 0);
  const int kTotalBatches = 20;
  const int kBigFileFrequency = 7;
  const int kBigFileRows = 10000;
  size_t expected_files = 0;
  bool first = true;
  for (int i = 0; i != kTotalBatches; ++i) {
    int base = i * kBigFileRows;
    int count;
    if (i % kBigFileFrequency == 0) {
      count = kBigFileRows;
      ++expected_files;
      first = true;
    } else {
      count = 1;
      if (first) {
        ++expected_files;
        first = false;
      }
    }
    for (int j = base; j != base + count; ++j) {
      WriteYQLRow(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT, schema, {j, j, j, j}, 1000000, t0);
    }
    FlushRocksDB();
  }

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(kTotalBatches, files.size());

  FLAGS_rocksdb_max_file_size_for_compaction = 100_KB;
  ReinitDBOptions();

  // Wait some time for background compactions to happen.
  std::this_thread::sleep_for(std::chrono::seconds(2));

  files.clear();
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(expected_files, files.size());
}

}  // namespace docdb
}  // namespace yb
