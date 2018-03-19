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

#include <thread>

#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/internal_stats.h"

#include "yb/common/partial_row.h"

#include "yb/docdb/ql_rocksdb_storage.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_ql_scanspec.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/tostring.h"

DECLARE_uint64(rocksdb_max_file_size_for_compaction);
DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_level0_stop_writes_trigger);

using namespace std::literals; // NOLINT

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

  void AddPrimaryKeyColumn(yb::QLWriteRequestPB* ql_writereq_pb, int32_t value) {
    ql_writereq_pb->add_hashed_column_values()->mutable_value()->set_int32_value(value);
  }

  void AddRangeKeyColumn(int32_t value, yb::QLWriteRequestPB* ql_writereq_pb) {
    ql_writereq_pb->add_range_column_values()->mutable_value()->set_int32_value(value);
  }

  void AddColumnValues(const Schema& schema,
                       const vector<int32_t>& column_values,
                       yb::QLWriteRequestPB* ql_writereq_pb) {
    ASSERT_EQ(schema.num_columns() - schema.num_key_columns(), column_values.size());
    for (int i = 0; i < column_values.size(); i++) {
      auto column = ql_writereq_pb->add_column_values();
      column->set_column_id(schema.num_key_columns() + i);
      column->mutable_expr()->mutable_value()->set_int32_value(column_values[i]);
    }
  }

  void WriteQL(QLWriteRequestPB* ql_writereq_pb, const Schema& schema,
               QLResponsePB* ql_writeresp_pb,
               const HybridTime& hybrid_time = HybridTime::kMax) {
    QLWriteOperation ql_write_op(schema, IndexMap(), kNonTransactionalOperationContext);
    ASSERT_OK(ql_write_op.Init(ql_writereq_pb, ql_writeresp_pb));
    auto doc_write_batch = MakeDocWriteBatch();
    CHECK_OK(ql_write_op.Apply({&doc_write_batch, ReadHybridTime()}));
    ASSERT_OK(WriteToRocksDB(doc_write_batch, hybrid_time));
  }

  void AssertWithTTL(QLWriteRequestPB_QLStmtType stmt_type) {
    if (stmt_type == QLWriteRequestPB::QL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT<max>]) -> null; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ <max> w: 1 }]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 2 }]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 3 }]) -> 4; ttl: 2.000s
      )#");
    } else {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT<max>]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 1 }]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 2 }]) -> 4; ttl: 2.000s
      )#");
    }
  }

  void AssertWithoutTTL(QLWriteRequestPB_QLStmtType stmt_type) {
    if (stmt_type == QLWriteRequestPB::QL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT<max>]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ <max> w: 1 }]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 2 }]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 3 }]) -> 4
      )#");
    } else {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT<max>]) -> 2
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 1 }]) -> 3
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 2 }]) -> 4
      )#");
    }
  }

  void RunTestQLInsertUpdate(QLWriteRequestPB_QLStmtType stmt_type, const int ttl = -1) {
    yb::QLWriteRequestPB ql_writereq_pb;
    yb::QLResponsePB ql_writeresp_pb;

    // Define the schema.
    Schema schema = CreateSchema();

    ql_writereq_pb.set_type(stmt_type);
    // Add primary key column.
    AddPrimaryKeyColumn(&ql_writereq_pb, 1);
    ql_writereq_pb.set_hash_code(0);

    AddColumnValues(schema, {2, 3, 4}, &ql_writereq_pb);

    if (ttl != -1) {
      ql_writereq_pb.set_ttl(ttl);
    }

    // Write to docdb.
    WriteQL(&ql_writereq_pb, schema, &ql_writeresp_pb);

    if (ttl == -1) {
      AssertWithoutTTL(stmt_type);
    } else {
      AssertWithTTL(stmt_type);
    }
  }

  void WriteQLRow(QLWriteRequestPB_QLStmtType stmt_type, const Schema& schema,
                const vector<int32_t>& column_values, int64_t ttl, const HybridTime& hybrid_time) {
    yb::QLWriteRequestPB ql_writereq_pb;
    yb::QLResponsePB ql_writeresp_pb;
    ql_writereq_pb.set_type(stmt_type);

    // Add primary key column.
    ql_writereq_pb.set_hash_code(0);
    for (size_t i = 0; i != schema.num_key_columns(); ++i) {
      if (i < schema.num_hash_key_columns()) {
        AddPrimaryKeyColumn(&ql_writereq_pb, column_values[i]);
      } else {
        AddRangeKeyColumn(column_values[i], &ql_writereq_pb);
      }
    }
    std::vector<int32_t> values(column_values.begin() + schema.num_key_columns(),
                                column_values.end());
    AddColumnValues(schema, values, &ql_writereq_pb);
    ql_writereq_pb.set_ttl(ttl);

    // Write to docdb.
    WriteQL(&ql_writereq_pb, schema, &ql_writeresp_pb, hybrid_time);
  }

  QLRowBlock ReadQLRow(const Schema& schema, int32_t primary_key, const HybridTime& read_time) {
    QLReadRequestPB ql_read_req;
    ql_read_req.add_hashed_column_values()->mutable_value()->set_int32_value(primary_key);

    QLRowBlock row_block(schema, vector<ColumnId> ({ColumnId(0), ColumnId(1), ColumnId(2),
                                                        ColumnId(3)}));
    const Schema& query_schema = row_block.schema();
    QLRSRowDescPB *rsrow_desc = ql_read_req.mutable_rsrow_desc();
    for (int32_t i = 0; i <= 3 ; i++) {
      ql_read_req.add_selected_exprs()->set_column_id(i);
      ql_read_req.mutable_column_refs()->add_ids(i);

      auto col = query_schema.column_by_id(ColumnId(i));
      EXPECT_OK(col);
      QLRSColDescPB *rscol_desc = rsrow_desc->add_rscol_descs();
      rscol_desc->set_name(col->name());
      col->type()->ToQLTypePB(rscol_desc->mutable_ql_type());
    }

    QLReadOperation read_op(ql_read_req, kNonTransactionalOperationContext);
    QLRocksDBStorage ql_storage(rocksdb());
    QLResultSet resultset;
    HybridTime read_restart_ht;
    EXPECT_OK(read_op.Execute(
        ql_storage, ReadHybridTime::SingleTime(read_time), schema, query_schema, &resultset,
        &read_restart_ht));
    EXPECT_FALSE(read_restart_ht.is_valid());

    // Transfer the column values from result set to rowblock.
    for (const auto& rsrow : resultset.rsrows()) {
      QLRow& row = row_block.Extend();
      row.SetColumnValues(rsrow.rscols());
    }
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
  redis_write_operation_pb.mutable_key_value()->set_type(REDIS_TYPE_STRING);
  redis_write_operation_pb.mutable_key_value()->set_hash_code(123);
  redis_write_operation_pb.mutable_key_value()->add_value("xyz");
  RedisWriteOperation redis_write_operation(&redis_write_operation_pb);
  auto doc_write_batch = MakeDocWriteBatch();
  ASSERT_OK(redis_write_operation.Apply({&doc_write_batch, ReadHybridTime()}));

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

TEST_F(DocOperationTest, TestQLInsertWithTTL) {
  RunTestQLInsertUpdate(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, 2000);
}

TEST_F(DocOperationTest, TestQLUpdateWithTTL) {
  RunTestQLInsertUpdate(QLWriteRequestPB_QLStmtType_QL_STMT_UPDATE, 2000);
}

TEST_F(DocOperationTest, TestQLInsertWithoutTTL) {
  RunTestQLInsertUpdate(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT);
}

TEST_F(DocOperationTest, TestQLUpdateWithoutTTL) {
  RunTestQLInsertUpdate(QLWriteRequestPB_QLStmtType_QL_STMT_UPDATE);
}

TEST_F(DocOperationTest, TestQLWriteNulls) {
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;

  // Define the schema.
  Schema schema = CreateSchema();
  ql_writereq_pb.set_type(
      QLWriteRequestPB_QLStmtType::QLWriteRequestPB_QLStmtType_QL_STMT_INSERT);
  ql_writereq_pb.set_hash_code(0);

  // Add primary key column.
  AddPrimaryKeyColumn(&ql_writereq_pb, 1);

  // Add null columns.
  for (int i = 0; i < 3; i++) {
    auto column = ql_writereq_pb.add_column_values();
    column->set_column_id(i + 1);
    column->mutable_expr()->mutable_value();
  }

  // Write to docdb.
  WriteQL(&ql_writereq_pb, schema, &ql_writeresp_pb);

  // Null columns are converted to tombstones.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT<max>]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ <max> w: 1 }]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 2 }]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 3 }]) -> DEL
      )#");
}

TEST_F(DocOperationTest, TestQLReadWriteSimple) {
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;

  // Define the schema.
  Schema schema = CreateSchema();
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
           1000, HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 0));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
      )#");

  // Now read the value.
  QLRowBlock row_block = ReadQLRow(schema, 1,
                                  HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(2000, 0));
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(1, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(2, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(3, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, TestQLReadWithoutLivenessColumn) {
  const DocKey doc_key(0, PrimitiveValues(PrimitiveValue::Int32(100)), PrimitiveValues());
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue::Int32(2)), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue::Int32(3)), HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue::Int32(4)), HybridTime(3000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> 2
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> 3
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> 4
      )#");

  // Now verify we can read without the system column id.
  Schema schema = CreateSchema();
  HybridTime read_time(3000);
  QLRowBlock row_block = ReadQLRow(schema, 100, read_time);
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(100, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(2, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(3, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(4, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, TestQLReadWithTombstone) {
  DocKey doc_key(0, PrimitiveValues(PrimitiveValue::Int32(100)), PrimitiveValues());
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue::kTombstone), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue::kTombstone), HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue::kTombstone), HybridTime(3000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
      )#");

  Schema schema = CreateSchema();
  DocRowwiseIterator iter(schema, schema, kNonTransactionalOperationContext, rocksdb(),
                          ReadHybridTime::FromUint64(3000));
  ASSERT_OK(iter.Init());
  ASSERT_FALSE(iter.HasNext());

  // Now verify row exists even with one valid column.
  doc_key = DocKey(0, PrimitiveValues(PrimitiveValue::Int32(100)), PrimitiveValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue::kTombstone), HybridTime(1001)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue::Int32(2),
                               MonoDelta::FromMilliseconds(1)), HybridTime(2001)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue::Int32(101)), HybridTime(3001)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1001 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2001 }]) -> \
    2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3001 }]) -> 101
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
      )#");

  vector<PrimitiveValue> hashed_components({PrimitiveValue::Int32(100)});
  DocQLScanSpec ql_scan_spec(schema, -1, -1, hashed_components, /* request = */ nullptr,
                             rocksdb::kDefaultQueryId);
  DocRowwiseIterator ql_iter(
      schema, schema, kNonTransactionalOperationContext, rocksdb(),
      ReadHybridTime::FromMicros(3000));
  ASSERT_OK(ql_iter.Init(ql_scan_spec));
  ASSERT_TRUE(ql_iter.HasNext());
  QLTableRow value_map;
  ASSERT_OK(ql_iter.NextRow(&value_map));
  ASSERT_EQ(4, value_map.ColumnCount());
  EXPECT_EQ(100, value_map.TestValue(0).value.int32_value());
  EXPECT_TRUE(IsNull(value_map.TestValue(1).value));
  EXPECT_TRUE(IsNull(value_map.TestValue(2).value));
  EXPECT_EQ(101, value_map.TestValue(3).value.int32_value());

  // Now verify row exists as long as liveness system column exists.
  doc_key = DocKey(0, PrimitiveValues(PrimitiveValue::Int32(101)), PrimitiveValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key,
                                 PrimitiveValue::SystemColumnId(
                                     SystemColumnIds::kLivenessColumn)),
                         Value(PrimitiveValue(ValueType::kNull)),
                         HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue::kTombstone), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue::Int32(2),
                               MonoDelta::FromMilliseconds(1)), HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue::kTombstone), HybridTime(3000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1001 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2001 }]) -> \
    2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3001 }]) -> 101
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
SubDocKey(DocKey(0x0000, [101], []), [SystemColumnId(0); HT{ physical: 0 logical: 1000 }]) -> null
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> \
    2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [101], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
      )#");

  vector<PrimitiveValue> hashed_components_system({PrimitiveValue::Int32(101)});
  DocQLScanSpec ql_scan_spec_system(schema, -1, -1, hashed_components_system, nullptr,
                                    rocksdb::kDefaultQueryId);
  DocRowwiseIterator ql_iter_system(
      schema, schema, kNonTransactionalOperationContext, rocksdb(),
      ReadHybridTime::FromMicros(3000));
  ASSERT_OK(ql_iter_system.Init(ql_scan_spec_system));
  ASSERT_TRUE(ql_iter_system.HasNext());
  QLTableRow value_map_system;
  ASSERT_OK(ql_iter_system.NextRow(&value_map_system));
  ASSERT_EQ(4, value_map_system.ColumnCount());
  EXPECT_EQ(101, value_map_system.TestValue(0).value.int32_value());
  EXPECT_TRUE(IsNull(value_map_system.TestValue(1).value));
  EXPECT_TRUE(IsNull(value_map_system.TestValue(2).value));
  EXPECT_TRUE(IsNull(value_map_system.TestValue(3).value));
}

namespace {

int32_t NewInt(std::mt19937_64* rng, std::unordered_set<int32_t>* existing) {
  std::uniform_int_distribution<uint32_t> distribution;
  for (;;) {
    auto result = distribution(*rng);
    if (existing->insert(static_cast<int32_t>(result)).second) {
      return result;
    }
  }
}

template<class It>
std::pair<It, It> GetIteratorRange(It begin, It end, It it, QLOperator op) {
  switch (op) {
    case QL_OP_EQUAL:
      return std::make_pair(it, it + 1);
    case QL_OP_LESS_THAN:
      return std::make_pair(begin, it);
    case QL_OP_LESS_THAN_EQUAL:
      return std::make_pair(begin, it + 1);
    case QL_OP_GREATER_THAN:
      return std::make_pair(it + 1, end);
    case QL_OP_GREATER_THAN_EQUAL:
      return std::make_pair(it, end);
    default: // We should not handle all cases here
      LOG(FATAL) << "Unexpected op: " << QLOperator_Name(op);
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
  void TestWithSortingType(ColumnSchema::SortingType schema_type, bool is_forward_scan = true);
 private:
};

// Currently we test using one column and one scan type.
// TODO(akashnil): In future we want to implement and test arbitrary ASC DESC combinations for scan.
void DocOperationRangeFilterTest::TestWithSortingType(ColumnSchema::SortingType schema_type,
    bool is_forward_scan) {
  ASSERT_OK(DisableCompactions());

  ColumnSchema hash_column("k", INT32, false, true);
  ColumnSchema range_column("r", INT32, false, false, false, false, schema_type);
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

    WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT,
                schema,
                { row.k, row.r, row.v },
                1000,
                t);
    ASSERT_OK(FlushRocksDB());
  }
  std::vector<rocksdb::LiveFileMetaData> live_files;
  rocksdb()->GetLiveFilesMetaData(&live_files);
  ASSERT_EQ(kNumRows, live_files.size());

  std::shuffle(rows.begin(), rows.end(), rng);
  auto ordered_rows = rows;
  std::sort(ordered_rows.begin(), ordered_rows.end());

  LOG(INFO) << "Dump:\n" << DocDBDebugDumpToStr();

  std::vector<QLOperator> operators = {
    QL_OP_EQUAL,
    QL_OP_LESS_THAN_EQUAL,
    QL_OP_GREATER_THAN_EQUAL,
  };

  auto old_iterators =
      rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
  for (auto op : operators) {
    LOG(INFO) << "Testing: " << QLOperator_Name(op);
    for (auto& row : rows) {
      std::vector<PrimitiveValue> hashed_components = { PrimitiveValue::Int32(key) };
      QLConditionPB condition;
      condition.add_operands()->set_column_id(1_ColId);
      condition.set_op(op);
      condition.add_operands()->mutable_value()->set_int32_value(row.r);

      auto it = std::lower_bound(ordered_rows.begin(), ordered_rows.end(), row);
      auto range = GetIteratorRange(ordered_rows.begin(), ordered_rows.end(), it, op);

      std::vector<RowData> expected_rows(range.first, range.second);
      if (is_forward_scan == (schema_type == ColumnSchema::SortingType::kDescending)) {
        std::reverse(expected_rows.begin(), expected_rows.end());
      }
      DocQLScanSpec ql_scan_spec(schema, -1, -1, hashed_components, &condition,
                                 rocksdb::kDefaultQueryId, is_forward_scan);
      DocRowwiseIterator ql_iter(schema, schema, boost::none, rocksdb(),
          ReadHybridTime::FromMicros(3000));
      ASSERT_OK(ql_iter.Init(ql_scan_spec));
      LOG(INFO) << "Expected rows: " << yb::ToString(expected_rows);
      it = expected_rows.begin();
      while(ql_iter.HasNext()) {
        QLTableRow value_map;
        ASSERT_OK(ql_iter.NextRow(&value_map));
        ASSERT_EQ(3, value_map.ColumnCount());

        RowData fetched_row = { value_map.TestValue(0_ColId).value.int32_value(),
                                value_map.TestValue(1_ColId).value.int32_value(),
                                value_map.TestValue(2_ColId).value.int32_value() };
        LOG(INFO) << "Fetched row: " << fetched_row;
        ASSERT_EQ(*it, fetched_row);
        it++;
      }
      ASSERT_EQ(expected_rows.end(), it);
      auto new_iterators =
          rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);

      ASSERT_EQ(range.second - range.first, new_iterators - old_iterators);
      old_iterators = new_iterators;
    }
  }
}

} // namespace

TEST_F_EX(DocOperationTest, QLRangeFilterAscending, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kAscending, true);
}

TEST_F_EX(DocOperationTest, QLRangeFilterDescending, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kDescending, true);
}

TEST_F_EX(DocOperationTest, TestQLAscDescScan, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kAscending, false);
}

TEST_F_EX(DocOperationTest, TestQLDescAscScan, DocOperationRangeFilterTest) {
  TestWithSortingType(ColumnSchema::kDescending, false);
}

TEST_F(DocOperationTest, TestQLCompactions) {
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;

  HybridTime t0 = HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 0);
  HybridTime t0prime = HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(1000, 1);
  HybridTime t1 = HybridClock::AddPhysicalTimeToHybridTime(t0, MonoDelta::FromMilliseconds(1001));

  // Define the schema.
  Schema schema = CreateSchema();
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
      )#");

  CompactHistoryBefore(t1);

  // Verify all entries are purged.
  AssertDocDbDebugDumpStrEq(R"#(
      )#");

  // Add a row with a TTL.
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
     )#");

  // Update the columns with a higher TTL.
  yb::QLWriteRequestPB ql_update_pb;
  yb::QLResponsePB ql_update_resp_pb;
  ql_writereq_pb.set_type(
      QLWriteRequestPB_QLStmtType::QLWriteRequestPB_QLStmtType_QL_STMT_UPDATE);

  ql_writereq_pb.set_hash_code(0);
  AddPrimaryKeyColumn(&ql_writereq_pb, 1);
  AddColumnValues(schema, {10, 20, 30}, &ql_writereq_pb);
  ql_writereq_pb.set_ttl(2000);

  // Write to docdb at the same physical time and a bumped-up logical time.
  WriteQL(&ql_writereq_pb, schema, &ql_writeresp_pb, t0prime);

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> \
    null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 logical: 1 }]) -> \
    10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> \
    1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 logical: 1 w: 1 }]) -> \
    20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> \
    2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 logical: 1 w: 2 }]) -> \
    30; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
      )#");

  CompactHistoryBefore(t1);

  // Verify the rest of the columns still live.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 logical: 1 }]) -> \
    10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 logical: 1 w: 1 }]) -> \
    20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 logical: 1 w: 2 }]) -> \
    30; ttl: 2.000s
      )#");

  // Verify reads work well without system column id.
  QLRowBlock row_block = ReadQLRow(schema, 1, t1);
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(10, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(20, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(30, row_block.row(0).column(3).int32_value());
}

namespace {

size_t GenerateFiles(int total_batches, DocOperationTest* test) {
  auto schema = test->CreateSchema();

  auto t0 = HybridTime::FromMicrosecondsAndLogicalValue(1000, 0);
  const int kBigFileFrequency = 7;
  const int kBigFileRows = 10000;
  size_t expected_files = 0;
  bool first = true;
  for (int i = 0; i != total_batches; ++i) {
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
      test->WriteQLRow(
          QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, {j, j, j, j}, 1000000, t0);
    }
    EXPECT_OK(test->FlushRocksDB());
  }
  return expected_files;
}

void WaitCompactionsDone(rocksdb::DB* db) {
  for (;;) {
    std::this_thread::sleep_for(500ms);
    uint64_t value = 0;
    ASSERT_TRUE(db->GetIntProperty(rocksdb::DB::Properties::kNumRunningCompactions, &value));
    if (value == 0) {
      break;
    }
  }
}

} // namespace

TEST_F(DocOperationTest, MaxFileSizeForCompaction) {
  google::FlagSaver flag_saver;

  ASSERT_OK(DisableCompactions());
  const int kTotalBatches = 20;
  auto expected_files = GenerateFiles(kTotalBatches, this);

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(kTotalBatches, files.size());

  FLAGS_rocksdb_max_file_size_for_compaction = 100_KB;
  ASSERT_OK(ReinitDBOptions());

  WaitCompactionsDone(rocksdb());

  files.clear();
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(expected_files, files.size());
}

TEST_F(DocOperationTest, MaxFileSizeWithWritesTrigger) {
  google::FlagSaver flag_saver;

  ASSERT_OK(DisableCompactions());
  const int kTotalBatches = 20;
  GenerateFiles(kTotalBatches, this);

  FLAGS_rocksdb_max_file_size_for_compaction = 100_KB;
  ASSERT_OK(ReinitDBOptions());

  WaitCompactionsDone(rocksdb());

  FLAGS_rocksdb_level0_slowdown_writes_trigger = 2;
  FLAGS_rocksdb_level0_stop_writes_trigger = 1;
  ASSERT_OK(ReinitDBOptions());

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_GE(files.size(), 3);

  auto handle_impl = down_cast<rocksdb::ColumnFamilyHandleImpl*>(rocksdb_->DefaultColumnFamily());
  auto stats = handle_impl->cfd()->internal_stats();
  ASSERT_EQ(0, stats->GetCFStats(rocksdb::InternalStats::LEVEL0_NUM_FILES_TOTAL));
  ASSERT_EQ(0, stats->GetCFStats(rocksdb::InternalStats::LEVEL0_SLOWDOWN_TOTAL));
}

}  // namespace docdb
}  // namespace yb
