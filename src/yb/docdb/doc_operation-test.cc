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

#include "yb/common/common.pb.h"
#include "yb/qlexpr/index.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/qlexpr/ql_resultset.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/ql_value.h"
#include "yb/common/transaction-test-util.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_debug.h"
#include "yb/docdb/iter_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/ql_rocksdb_storage.h"
#include "yb/docdb/redis_operation.h"

#include "yb/gutil/casts.h"

#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/internal_stats.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/tostring.h"

using std::vector;

DECLARE_bool(ycql_enable_packed_row);
DECLARE_uint64(rocksdb_max_file_size_for_compaction);
DECLARE_int32(rocksdb_level0_slowdown_writes_trigger);
DECLARE_int32(rocksdb_level0_stop_writes_trigger);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(test_random_seed);

using namespace std::literals; // NOLINT

namespace yb {
namespace docdb {

using server::HybridClock;
using dockv::DocKey;
using dockv::DocPath;
using dockv::KeyEntryValue;
using dockv::KeyEntryValues;
using dockv::ValueEntryType;
using qlexpr::QLRowBlock;

namespace {

std::vector<ColumnId> CreateColumnIds(size_t count) {
  std::vector<ColumnId> result;
  result.reserve(count);
  for (size_t i = 0; i != count; ++i) {
    result.emplace_back(i);
  }
  return result;
}

constexpr int32_t kFixedHashCode = 0;
const int64_t kAlwaysDiscard = -1;

// DiscardUntilFileFilter is a test CompactionFileFilter than will be used to mark some
// files for direct deletion during the PickCompaction process in UniversalCompactionPicker.
// Any files with numbers less than the provided last discard file number will be
// included in the compaction but directly deleted.
//
// DiscardUntilFileFilter emulates behavior of DocDBCompactionFileFilter, which expires
// files based on their TTL.
class DiscardUntilFileFilter : public rocksdb::CompactionFileFilter {
 public:
  explicit DiscardUntilFileFilter(int64_t last_discard) :
      last_discard_(last_discard) {}

  // Filters all file numbers less than or equal to the filter's last_discard_.
  // Setting last_discard_ to the kAlwaysDiscard constant will result in every
  // incoming file being filtered.
  rocksdb::FilterDecision Filter(const rocksdb::FileMetaData* file) override {
    if (last_discard_ == kAlwaysDiscard ||
        file->fd.GetNumber() <= implicit_cast<uint64_t>(last_discard_)) {
      LOG(INFO) << "Filtering file: " << file->fd.GetNumber() << ", size: "
          << file->fd.GetBaseFileSize() << ", total file size: " << file->fd.GetTotalFileSize();
      return rocksdb::FilterDecision::kDiscard;
    }
    return rocksdb::FilterDecision::kKeep;
  }

  const char* Name() const override { return "DiscardUntilFileFilter"; }

 private:
  const int64_t last_discard_;
};

// A CompactionFileFilterFactory that takes a file number as input, and filters all
// file numbers lower than or equal to that one. Any larger file numbers will not
// be filtered.
class DiscardUntilFileFilterFactory : public rocksdb::CompactionFileFilterFactory {
 public:
  explicit DiscardUntilFileFilterFactory(const int64_t last_to_discard) :
      last_to_discard_(last_to_discard) {}

  std::unique_ptr<rocksdb::CompactionFileFilter> CreateCompactionFileFilter(
      const std::vector<rocksdb::FileMetaData*>& inputs) override {
    return std::make_unique<DiscardUntilFileFilter>(last_to_discard_);
  }

  const char* Name() const override { return "DiscardUntilFileFilterFactory"; }

 private:
  const int64_t last_to_discard_;
};

// MakeMaxFileSizeFunction will create a function that returns the
// rocksdb_max_file_size_for_compaction flag if it is set to a positive number, and returns
// the max uint64 otherwise. It does NOT take the schema's table TTL into consideration.
auto MakeMaxFileSizeFunction() {
  // Trick to get type of max_file_size_for_compaction field.
  typedef typename decltype(
      static_cast<rocksdb::Options*>(nullptr)->max_file_size_for_compaction)::element_type
      MaxFileSizeFunction;
  return std::make_shared<MaxFileSizeFunction>([]{
    if (FLAGS_rocksdb_max_file_size_for_compaction > 0) {
      return FLAGS_rocksdb_max_file_size_for_compaction;
    }
    return std::numeric_limits<uint64_t>::max();
  });
}

} // namespace

class DocOperationTest : public DocDBTestBase {
 public:
  DocOperationTest() {
    SeedRandom();
  }

  void SetUp() override {
    SetMaxFileSizeForCompaction(0);
    // Make a function that will always use rocksdb_max_file_size_for_compaction.
    // Normally, max_file_size_for_compaction is only used for tables with TTL.
    ANNOTATE_UNPROTECTED_WRITE(max_file_size_for_compaction_) = MakeMaxFileSizeFunction();
    DocDBTestBase::SetUp();
  }

  Schema CreateSchema() override {
    ColumnSchema hash_column_schema("k", DataType::INT32, ColumnKind::HASH);
    ColumnSchema column1_schema("c1", DataType::INT32);
    ColumnSchema column2_schema("c2", DataType::INT32);
    ColumnSchema column3_schema("c3", DataType::INT32);
    const vector<ColumnSchema> columns({
        hash_column_schema, column1_schema, column2_schema, column3_schema});
    return Schema(columns, CreateColumnIds(columns.size()));
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
    for (size_t i = 0; i < column_values.size(); i++) {
      auto column = ql_writereq_pb->add_column_values();
      column->set_column_id(narrow_cast<int32_t>(schema.num_key_columns() + i));
      column->mutable_expr()->mutable_value()->set_int32_value(column_values[i]);
    }
  }

  void WriteQL(const QLWriteRequestPB& ql_writereq_pb, const Schema& schema,
               QLResponsePB* ql_writeresp_pb,
               const HybridTime& hybrid_time = HybridTime::kMax,
               const TransactionOperationContext& txn_op_context =
                   kNonTransactionalOperationContext) {
    qlexpr::IndexMap index_map;
    QLWriteOperation ql_write_op(
        ql_writereq_pb, ql_writereq_pb.schema_version(),
        std::make_shared<DocReadContext>(DocReadContext::TEST_Create(schema)), index_map,
        /* unique_index_key_projection= */ nullptr, txn_op_context);
    ASSERT_OK(ql_write_op.Init(ql_writeresp_pb));
    auto doc_write_batch = MakeDocWriteBatch();
    HybridTime restart_read_ht;
    ASSERT_OK(ql_write_op.Apply({
      .doc_write_batch = &doc_write_batch,
      .read_operation_data = ReadOperationData(),
      .restart_read_ht = &restart_read_ht,
      .iterator = nullptr,
      .restart_seek = true,
      .schema_packing_provider = nullptr,
    }));
    ASSERT_OK(WriteToRocksDB(doc_write_batch, hybrid_time));
  }

  void AssertWithTTL(QLWriteRequestPB_QLStmtType stmt_type) {
    if (stmt_type == QLWriteRequestPB::QL_STMT_INSERT) {
      AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT<max>]) -> null; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ <max> w: 1 }]) -> 2; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 2 }]) -> 3; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 3 }]) -> 4; ttl: 2.000s
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT<max>]) -> \
{ 1: 2; ttl: 2.000s 2: 3; ttl: 2.000s 3: 4; ttl: 2.000s }; ttl: 2.000s
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
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT<max>]) -> { 1: 2 2: 3 3: 4 }
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
    WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb);

    if (ttl == -1) {
      AssertWithoutTTL(stmt_type);
    } else {
      AssertWithTTL(stmt_type);
    }
  }

  yb::QLWriteRequestPB WriteQLRowReq(QLWriteRequestPB_QLStmtType stmt_type, const Schema& schema,
                  const vector<int32_t>& column_values, const HybridTime& hybrid_time,
                  const TransactionOperationContext& txn_op_content =
                      kNonTransactionalOperationContext) {
    yb::QLWriteRequestPB ql_writereq_pb;
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
    return ql_writereq_pb;
  }

  void WriteQLRow(QLWriteRequestPB_QLStmtType stmt_type, const Schema& schema,
                  const vector<int32_t>& column_values, int64_t ttl, const HybridTime& hybrid_time,
                  const TransactionOperationContext& txn_op_content =
                      kNonTransactionalOperationContext) {
    yb::QLWriteRequestPB ql_writereq_pb = WriteQLRowReq(
        stmt_type, schema, column_values, hybrid_time, txn_op_content);
    ql_writereq_pb.set_ttl(ttl);
    yb::QLResponsePB ql_writeresp_pb;
    // Write to docdb.
    WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb, hybrid_time, txn_op_content);
  }

  void WriteQLRow(QLWriteRequestPB_QLStmtType stmt_type, const Schema& schema,
                  const vector<int32_t>& column_values, const HybridTime& hybrid_time,
                  const TransactionOperationContext& txn_op_content =
                      kNonTransactionalOperationContext) {
    yb::QLWriteRequestPB ql_writereq_pb = WriteQLRowReq(
        stmt_type, schema, column_values, hybrid_time, txn_op_content);
    yb::QLResponsePB ql_writeresp_pb;
    // Write to docdb.
    WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb, hybrid_time, txn_op_content);
  }

  QLRowBlock ReadQLRow(const Schema& schema, int32_t primary_key, const HybridTime& read_time) {
    QLReadRequestPB ql_read_req;
    ql_read_req.add_hashed_column_values()->mutable_value()->set_int32_value(primary_key);
    ql_read_req.set_hash_code(kFixedHashCode);
    ql_read_req.set_max_hash_code(kFixedHashCode);

    QLRowBlock row_block(schema, vector<ColumnId> ({ColumnId(0), ColumnId(1), ColumnId(2),
                                                        ColumnId(3)}));
    const Schema& projection = row_block.schema();
    auto *rsrow_desc_pb = ql_read_req.mutable_rsrow_desc();
    for (int32_t i = 0; i <= 3 ; i++) {
      ql_read_req.add_selected_exprs()->set_column_id(i);
      ql_read_req.mutable_column_refs()->add_ids(i);

      auto col = projection.column_by_id(ColumnId(i));
      EXPECT_OK(col);
      QLRSColDescPB *rscol_desc = rsrow_desc_pb->add_rscol_descs();
      rscol_desc->set_name(col->name());
      col->type()->ToQLTypePB(rscol_desc->mutable_ql_type());
    }

    QLReadOperation read_op(ql_read_req, kNonTransactionalOperationContext);
    QLRocksDBStorage ql_storage(doc_db());
    const qlexpr::QLRSRowDesc rsrow_desc(*rsrow_desc_pb);
    WriteBuffer rows_data(1024);
    qlexpr::QLResultSet resultset(&rsrow_desc, &rows_data);
    HybridTime read_restart_ht;
    auto doc_read_context = DocReadContext::TEST_Create(schema);
    auto pending_op = ScopedRWOperation::TEST_Create();
    EXPECT_OK(read_op.Execute(
        ql_storage, ReadOperationData::FromSingleReadTime(read_time), doc_read_context, pending_op,
        &resultset, &read_restart_ht));
    EXPECT_FALSE(read_restart_ht.is_valid());

    // Transfer the column values from result set to rowblock.
    auto data_str = rows_data.ToBuffer();
    Slice data(data_str);
    EXPECT_OK(row_block.Deserialize(YQL_CLIENT_CQL, &data));
    return row_block;
  }

  void SetMaxFileSizeForCompaction(const uint64_t max_size) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_file_size_for_compaction) = max_size;
  }

  void TestWriteNulls();
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
  RedisWriteOperation redis_write_operation(redis_write_operation_pb);
  auto doc_write_batch = MakeDocWriteBatch();
  ASSERT_OK(redis_write_operation.Apply(docdb::DocOperationApplyData{
      .doc_write_batch = &doc_write_batch,
      .read_operation_data = {},
      .restart_read_ht = nullptr,
      .iterator = nullptr,
      .restart_seek = true,
      .schema_packing_provider = nullptr,
  }));

  ASSERT_OK(WriteToRocksDB(doc_write_batch, HybridTime::FromMicros(1000)));

  // Read key from rocksdb.
  const auto doc_key = DocKey::FromRedisKey(123, "abc").Encode();
  rocksdb::ReadOptions read_opts;
  auto iter = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(read_opts));
  ROCKSDB_SEEK(iter.get(), doc_key.AsSlice());
  ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));

  // Verify correct ttl.
  auto value = iter->value();
  auto ttl = ASSERT_RESULT(dockv::ValueControlFields::Decode(&value)).ttl;
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

void DocOperationTest::TestWriteNulls() {
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
  WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb);
}

TEST_F(DocOperationTest, TestQLWriteNulls) {
  TestWriteNulls();
  // Null columns are converted to tombstones.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT<max>]) -> null
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ <max> w: 1 }]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ <max> w: 2 }]) -> DEL
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ <max> w: 3 }]) -> DEL
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT<max>]) -> { 1: NULL 2: NULL 3: NULL }
      )#");
}

TEST_F(DocOperationTest, WritePackedNulls) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_enable_packed_row) = true;
  TestWriteNulls();
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT<max>]) -> { 1: NULL 2: NULL 3: NULL }
  )#");
}

TEST_F(DocOperationTest, TestQLReadWriteSimple) {
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;

  // Define the schema.
  Schema schema = CreateSchema();
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
           1000, HybridTime::FromMicrosecondsAndLogicalValue(1000, 0));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT{ physical: 1000 }]) -> \
{ 1: 1; ttl: 1.000s 2: 2; ttl: 1.000s 3: 3; ttl: 1.000s }; ttl: 1.000s
      )#");

  // Now read the value.
  QLRowBlock row_block = ReadQLRow(schema, 1,
                                  HybridTime::FromMicrosecondsAndLogicalValue(2000, 0));
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(1, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(2, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(3, row_block.row(0).column(3).int32_value());
}

TEST_F(DocOperationTest, TestQLRangeDeleteWithStaticColumnAvoidsFullPartitionKeyScan) {
  constexpr int kNumRows = 10000;
  constexpr int kDeleteRangeLow = 100;
  constexpr int kDeleteRangeHigh = 200;

  // Define the schema with a partition key, range key, static column, and value.
  SchemaBuilder builder;
  builder.set_next_column_id(ColumnId(0));
  ASSERT_OK(builder.AddHashKeyColumn("k", DataType::INT32));
  ASSERT_OK(builder.AddKeyColumn("r", DataType::INT32));
  ASSERT_OK(builder.AddColumn(
      ColumnSchema("s", DataType::INT32, ColumnKind::VALUE, Nullable::kTrue)));
  ASSERT_OK(builder.AddColumn(ColumnSchema("v", DataType::INT32)));
  auto schema = builder.Build();

  // Write rows with the same partition key but different range key
  for (int row_num = 0; row_num < kNumRows; ++row_num) {
    WriteQLRow(
        QLWriteRequestPB_QLStmtType_QL_STMT_INSERT,
        schema,
        vector<int>({1, row_num, 0, row_num}),
        HybridTime::FromMicrosecondsAndLogicalValue(1000, 0));
  }
  auto get_num_rocksdb_iter_moves = [&]() {
    auto num_next = regular_db_options().statistics->getTickerCount(
      rocksdb::Tickers::NUMBER_DB_NEXT);
    auto num_seek = regular_db_options().statistics->getTickerCount(
      rocksdb::Tickers::NUMBER_DB_SEEK);
    return num_next + num_seek;
  };

  auto rocksdb_iter_moves_before_delete = get_num_rocksdb_iter_moves();

  // Delete a subset of the partition
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;
  ql_writereq_pb.set_type(QLWriteRequestPB_QLStmtType_QL_STMT_DELETE);
  AddPrimaryKeyColumn(&ql_writereq_pb, 1);

  auto where_clause_and = ql_writereq_pb.mutable_where_expr()->mutable_condition();
  where_clause_and->set_op(QLOperator::QL_OP_AND);
  QLAddInt32Condition(where_clause_and, 1, QL_OP_GREATER_THAN_EQUAL, kDeleteRangeLow);
  QLAddInt32Condition(where_clause_and, 1, QL_OP_LESS_THAN_EQUAL, kDeleteRangeHigh);
  ql_writereq_pb.set_hash_code(0);

  WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb,
          HybridTime::FromMicrosecondsAndLogicalValue(2000, 0),
          kNonTransactionalOperationContext);

  // During deletion, we expect to move the RocksDB iterator *at most* once per docdb row in range,
  // plus once for the first docdb row out of range. We say *at most*, because it's possible to have
  // various combinations of Next() vs. Seek() calls. This simply tests that we do not have more
  // than the maximum allowed based on the schema and the restriction that we should not scan
  // outside the boundaries of the relevant deletion range.
  auto num_cql_rows_in_range = kDeleteRangeHigh - kDeleteRangeLow + 1;
  auto max_num_rocksdb_moves_per_cql_row = 3;
  auto max_num_rocksdb_iter_moves = num_cql_rows_in_range * max_num_rocksdb_moves_per_cql_row;
  ASSERT_LE(
      get_num_rocksdb_iter_moves() - rocksdb_iter_moves_before_delete,
      max_num_rocksdb_iter_moves + 1);
}

TEST_F(DocOperationTest, TestQLReadWithoutLivenessColumn) {
  const DocKey doc_key(kFixedHashCode, dockv::MakeKeyEntryValues(100), KeyEntryValues());
  auto encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         QLValue::Primitive(2), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
                         QLValue::Primitive(3), HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
                         QLValue::Primitive(4), HybridTime(3000)));

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
  DocKey doc_key(0, dockv::MakeKeyEntryValues(100), KeyEntryValues());
  auto encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(3000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
      )#");

  Schema schema = CreateSchema();
  auto doc_read_context = DocReadContext::TEST_Create(schema);
  dockv::ReaderProjection projection(schema);
  auto pending_op = ScopedRWOperation::TEST_Create();

  DocRowwiseIterator iter(
      projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
      ReadOperationData::FromReadTime(ReadHybridTime::FromUint64(3000)), pending_op);
  iter.InitForTableType(YQL_TABLE_TYPE);
  ASSERT_FALSE(ASSERT_RESULT(iter.FetchNext(nullptr)));

  // Now verify row exists even with one valid column.
  doc_key = DocKey(kFixedHashCode, dockv::MakeKeyEntryValues(100), KeyEntryValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(1001)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
                         dockv::ValueControlFields{.ttl = MonoDelta::FromMilliseconds(1)},
                         ValueRef(QLValue::Primitive(2)),
                         HybridTime(2001)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
                         QLValue::Primitive(101), HybridTime(3001)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1001 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(1); HT{ physical: 0 logical: 1000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2001 }]) -> \
    2; ttl: 0.001s
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(2); HT{ physical: 0 logical: 2000 }]) -> DEL
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3001 }]) -> 101
SubDocKey(DocKey(0x0000, [100], []), [ColumnId(3); HT{ physical: 0 logical: 3000 }]) -> DEL
      )#");

  KeyEntryValues hashed_components({KeyEntryValue::Int32(100)});
  DocQLScanSpec ql_scan_spec(schema, kFixedHashCode, kFixedHashCode, hashed_components,
      /* req */ nullptr, /* if_req */ nullptr, rocksdb::kDefaultQueryId);

  DocRowwiseIterator ql_iter(
      projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
      ReadOperationData::TEST_FromReadTimeMicros(3000), pending_op);
  ASSERT_OK(ql_iter.Init(ql_scan_spec));
  qlexpr::QLTableRow value_map;
  ASSERT_TRUE(ASSERT_RESULT(ql_iter.FetchNext(&value_map)));
  ASSERT_EQ(4, value_map.ColumnCount());
  EXPECT_EQ(100, value_map.TestValue(0).value.int32_value());
  EXPECT_TRUE(IsNull(value_map.TestValue(1).value));
  EXPECT_TRUE(IsNull(value_map.TestValue(2).value));
  EXPECT_EQ(101, value_map.TestValue(3).value.int32_value());

  // Now verify row exists as long as liveness system column exists.
  doc_key = DocKey(kFixedHashCode, dockv::MakeKeyEntryValues(101), KeyEntryValues());
  encoded_doc_key = doc_key.Encode();
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::kLivenessColumn),
                         ValueRef(ValueEntryType::kNullLow),
                         HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(1))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(2))),
                         dockv::ValueControlFields{.ttl = MonoDelta::FromMilliseconds(1)},
                         ValueRef(QLValue::Primitive(2)),
                         HybridTime(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(3))),
                         ValueRef(ValueEntryType::kTombstone), HybridTime(3000)));

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

  vector<KeyEntryValue> hashed_components_system({KeyEntryValue::Int32(101)});
  DocQLScanSpec ql_scan_spec_system(schema, kFixedHashCode, kFixedHashCode,
      hashed_components_system, /* req */ nullptr,  /* if_req */ nullptr,
      rocksdb::kDefaultQueryId);

  DocRowwiseIterator ql_iter_system(
      projection, doc_read_context, kNonTransactionalOperationContext, doc_db(),
      ReadOperationData::TEST_FromReadTimeMicros(3000), pending_op);
  ASSERT_OK(ql_iter_system.Init(ql_scan_spec_system));
  qlexpr::QLTableRow value_map_system;
  ASSERT_TRUE(ASSERT_RESULT(ql_iter_system.FetchNext(&value_map_system)));
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

int32_t NewInt(std::mt19937_64* rng, std::unordered_set<int32_t>* existing,
    const int32_t min, const int32_t max) {
  std::uniform_int_distribution<int32_t> distribution(min, max);
  for (;;) {
    auto result = distribution(*rng);
    if (existing->insert(result).second) {
      return result;
    }
  }
}

struct RowData {
  int32_t k;
  int32_t r;
  int32_t v;
};

struct RowDataWithHt {
  RowData data;
  HybridTime ht;
};

bool operator==(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k && lhs.r == rhs.r && lhs.v == rhs.v;
}

bool operator<(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k ? (lhs.r == rhs.r ? lhs.v < rhs.v : lhs.r < rhs.r) : lhs.k < rhs.k;
}

bool IsSameKey(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k && lhs.r == rhs.r;
}

bool IsKeyLessThan(const RowData& lhs, const RowData& rhs) {
  return lhs.k == rhs.k ? lhs.r < rhs.r : lhs.k < rhs.k;
}

std::ostream& operator<<(std::ostream& out, const RowData& row) {
  return out << "{ k: " << row.k << " r: " << row.r << " v: " << row.v << " }";
}

bool IsSameKey(const RowDataWithHt& lhs, const RowDataWithHt& rhs) {
  return IsSameKey(lhs.data, rhs.data);
}

bool IsKeyLessThan(const RowDataWithHt& lhs, const RowDataWithHt& rhs) {
  return IsKeyLessThan(lhs.data, rhs.data);
}

bool operator<(const RowDataWithHt& lhs, const RowDataWithHt& rhs) {
  return IsSameKey(lhs, rhs)
      ? (lhs.ht == rhs.ht ? lhs.data < rhs.data : lhs.ht > rhs.ht)
      : IsKeyLessThan(lhs, rhs);
}

std::ostream& operator<<(std::ostream& out, const RowDataWithHt& row) {
  return out << "{ data: " << row.data << " ht: " << row.ht << " }";
}

template<class It>
It MoveForwardToNextKey(It it, const It end) {
  return std::find_if_not(it, end, [it](auto k) { return IsSameKey(k, *it); });
}

template<class It>
It MoveBackToBeginningOfCurrentKey(const It begin, It it) {

  return std::lower_bound(begin, it, *it, [](auto k1, auto k2) { return IsKeyLessThan(k1, k2); });
}

template<class It>
std::pair<It, It> GetIteratorRange(const It begin, const It end, const It it, QLOperator op) {
  switch (op) {
    case QL_OP_EQUAL:
      {
        return std::make_pair(MoveBackToBeginningOfCurrentKey(begin, it),
            MoveForwardToNextKey(it, end));
      }
    case QL_OP_LESS_THAN:
      {
        return std::make_pair(begin, MoveBackToBeginningOfCurrentKey(begin, it));
      }
    case QL_OP_LESS_THAN_EQUAL:
      {
        return std::make_pair(begin, MoveForwardToNextKey(it, end));
      }
    case QL_OP_GREATER_THAN:
      {
        return std::make_pair(MoveForwardToNextKey(it, end), end);
      }
    case QL_OP_GREATER_THAN_EQUAL:
      {
        return std::make_pair(MoveBackToBeginningOfCurrentKey(begin, it), end);
      }
    default: // We should not handle all cases here
      LOG(FATAL) << "Unexpected op: " << QLOperator_Name(op);
      return std::make_pair(begin, end);
  }
}

} // namespace

class DocOperationScanTest : public DocOperationTest {
 protected:
  DocOperationScanTest() {
    if (FLAGS_test_random_seed == 0) {
      Seed(&rng_);
    } else {
      rng_.seed(FLAGS_test_random_seed);
    }
  }

  Schema CreateSchema() override {
    ColumnSchema hash_column("k", DataType::INT32, ColumnKind::HASH);
    ColumnSchema range_column(
        "r", DataType::INT32, SortingTypeToColumnKind(range_column_sorting_type_));
    ColumnSchema value_column("v", DataType::INT32);
    auto columns = { hash_column, range_column, value_column };
    return Schema(columns, CreateColumnIds(columns.size()));
  }

  void InsertRows(const size_t num_rows_per_key,
      TransactionStatusManagerMock* txn_status_manager = nullptr) {
    ResetCurrentTransactionId();
    ASSERT_OK(DisableCompactions());

    std::unordered_set<int32_t> used_ints;
    h_key_ = NewInt(&rng_, &used_ints);
    rows_.clear();
    for (int32_t i = 0; i != kNumKeys; ++i) {
      int32_t r_key = NewInt(&rng_, &used_ints);
      for (size_t j = 0; j < num_rows_per_key; ++j) {
        RowData row_data = {h_key_, r_key, NewInt(&rng_, &used_ints)};
        auto ht = HybridTime::FromMicrosecondsAndLogicalValue(
            NewInt(&rng_, &used_ints, kMinTime, kMaxTime), 0);
        RowDataWithHt row = {row_data, ht};
        rows_.push_back(row);

        std::unique_ptr<TransactionOperationContext> txn_op_context;
        boost::optional<TransactionId> txn_id;
        if (txn_status_manager) {
          if (RandomActWithProbability(0.5, &rng_)) {
            txn_id = TransactionId::GenerateRandom();
            SetCurrentTransactionId(*txn_id);
            txn_op_context = std::make_unique<TransactionOperationContext>(*txn_id,
                txn_status_manager);
          } else {
            ResetCurrentTransactionId();
          }
        }
        auto row_values = { row_data.k, row_data.r, row_data.v };
        LOG(INFO) << "Writing row: " << AsString(row_values) << ", ht: " << ht;
        WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT,
                   doc_read_context().schema(),
                   row_values,
                   1000,
                   ht,
                   txn_op_context ? *txn_op_context : kNonTransactionalOperationContext);
        if (txn_id) {
          txn_status_manager->Commit(*txn_id, ht);
        }
      }

      ASSERT_OK(FlushRocksDbAndWait());
    }

    SchemaPackingProvider* schema_packing_provider = this;
    DumpRocksDBToLog(rocksdb(), schema_packing_provider, StorageDbType::kRegular);
    DumpRocksDBToLog(intents_db(), schema_packing_provider, StorageDbType::kIntents);
  }

  void PerformScans(const bool is_forward_scan,
      const TransactionOperationContext& txn_op_context,
      boost::function<void(const size_t keys_in_scan_range)> after_scan_callback) {
    std::vector <KeyEntryValue> hashed_components = {KeyEntryValue::Int32(h_key_)};
    std::vector <QLOperator> operators = {
        QL_OP_EQUAL,
        QL_OP_LESS_THAN_EQUAL,
        QL_OP_GREATER_THAN_EQUAL,
    };

    std::shuffle(rows_.begin(), rows_.end(), rng_);
    auto ordered_rows = rows_;
    std::sort(ordered_rows.begin(), ordered_rows.end());

    for (const auto op : operators) {
      LOG(INFO) << "Testing: " << QLOperator_Name(op) << ", is_forward_scan: " << is_forward_scan;
      for (const auto& row : rows_) {
        QLConditionPB condition;
        condition.add_operands()->set_column_id(1_ColId);
        condition.set_op(op);
        condition.add_operands()->mutable_value()->set_int32_value(row.data.r);
        auto it = std::lower_bound(ordered_rows.begin(), ordered_rows.end(), row);
        LOG(INFO) << "Bound: " << yb::ToString(*it);
        const auto range = GetIteratorRange(ordered_rows.begin(), ordered_rows.end(), it, op);
        std::vector <int32_t> ht_diffs;
        if (op == QL_OP_EQUAL) {
          ht_diffs = {0};
        } else {
          ht_diffs = {-1, 0, 1};
        }
        for (auto ht_diff : ht_diffs) {
          std::vector <RowDataWithHt> expected_rows;
          auto read_ht = ReadHybridTime::FromMicros(row.ht.GetPhysicalValueMicros() + ht_diff);
          LOG(INFO) << "Read time: " << read_ht;
          size_t keys_in_range = range.first < range.second;
          for (auto it_r = range.first; it_r < range.second;) {
            auto it = it_r;
            if (it_r->ht <= read_ht.read) {
              expected_rows.emplace_back(*it_r);
              while (it_r < range.second && IsSameKey(*it_r, *it)) {
                ++it_r;
              }
            } else {
              ++it_r;
            }
            if (it_r < range.second && !IsSameKey(*it_r, *it)) {
              ++keys_in_range;
            }
          }

          if (is_forward_scan ==
              (range_column_sorting_type_ == SortingType::kDescending)) {
            std::reverse(expected_rows.begin(), expected_rows.end());
          }
          DocQLScanSpec ql_scan_spec(
              doc_read_context().schema(), kFixedHashCode, kFixedHashCode, hashed_components,
              &condition, nullptr /* if_ req */, rocksdb::kDefaultQueryId, is_forward_scan);
          dockv::ReaderProjection projection(doc_read_context().schema());
          auto pending_op = ScopedRWOperation::TEST_Create();
          DocRowwiseIterator ql_iter(
              projection, doc_read_context(), txn_op_context, doc_db(),
              ReadOperationData::FromReadTime(read_ht), pending_op);
          ASSERT_OK(ql_iter.Init(ql_scan_spec));
          LOG(INFO) << "Expected rows: " << AsString(expected_rows);
          it = expected_rows.begin();
          qlexpr::QLTableRow value_map;
          while (ASSERT_RESULT(ql_iter.FetchNext(&value_map))) {
            ASSERT_EQ(3, value_map.ColumnCount());

            RowData fetched_row = {value_map.TestValue(0_ColId).value.int32_value(),
                value_map.TestValue(1_ColId).value.int32_value(),
                value_map.TestValue(2_ColId).value.int32_value()};
            LOG(INFO) << "Fetched row: " << fetched_row;
            ASSERT_LT(it, expected_rows.end());
            ASSERT_EQ(fetched_row, it->data);
            it++;
          }
          ASSERT_EQ(expected_rows.end(), it) << "Missing row: " << AsString(*it);

          after_scan_callback(keys_in_range);
        }
      }
    }
  }

  void TestWithSortingType(SortingType sorting_type, bool is_forward_scan) {
    DoTestWithSortingType(sorting_type, is_forward_scan, 1 /* num_rows_per_key */);
    ASSERT_OK(DestroyRocksDB());
    ASSERT_OK(ReopenRocksDB());
    DoTestWithSortingType(sorting_type, is_forward_scan, 5 /* num_rows_per_key */);
  }

  virtual void DoTestWithSortingType(SortingType schema_type, bool is_forward_scan,
      size_t num_rows_per_key) = 0;

  void SetSortingType(SortingType value) {
    range_column_sorting_type_ = value;
    CHECK_EQ(doc_read_context().schema().column(1).sorting_type(), value);
  }

  constexpr static int32_t kNumKeys = 20;
  constexpr static uint32_t kMinTime = 500;
  constexpr static uint32_t kMaxTime = 1500;

  std::mt19937_64 rng_;
  SortingType range_column_sorting_type_;
  int32_t h_key_;
  std::vector<RowDataWithHt> rows_;
};

class DocOperationRangeFilterTest : public DocOperationScanTest {
 protected:
  void DoTestWithSortingType(SortingType sorting_type, bool is_forward_scan,
      size_t num_rows_per_key) override;
};

// Currently we test using one column and one scan type.
// TODO(akashnil): In future we want to implement and test arbitrary ASC DESC combinations for scan.
void DocOperationRangeFilterTest::DoTestWithSortingType(SortingType sorting_type,
    const bool is_forward_scan, const size_t num_rows_per_key) {
  ASSERT_OK(DisableCompactions());

  SetSortingType(sorting_type);
  InsertRows(num_rows_per_key);

  {
    std::vector<rocksdb::LiveFileMetaData> live_files;
    rocksdb()->GetLiveFilesMetaData(&live_files);
    const auto expected_live_files = kNumKeys;
    ASSERT_EQ(expected_live_files, live_files.size());
  }

  auto old_iterators =
      rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);

  PerformScans(is_forward_scan, kNonTransactionalOperationContext,
      [this, &old_iterators](const size_t key_in_scan_range) {
    auto new_iterators =
        rocksdb()->GetDBOptions().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
    ASSERT_EQ(key_in_scan_range, new_iterators - old_iterators);
    old_iterators = new_iterators;
  });
}

TEST_F_EX(DocOperationTest, QLRangeFilterAscendingForwardScan, DocOperationRangeFilterTest) {
  TestWithSortingType(SortingType::kAscending, true);
}

TEST_F_EX(DocOperationTest, QLRangeFilterDescendingForwardScan, DocOperationRangeFilterTest) {
  TestWithSortingType(SortingType::kDescending, true);
}

TEST_F_EX(DocOperationTest, QLRangeFilterAscendingReverseScan, DocOperationRangeFilterTest) {
  TestWithSortingType(SortingType::kAscending, false);
}

TEST_F_EX(DocOperationTest, QLRangeFilterDescendingReverseScan, DocOperationRangeFilterTest) {
  TestWithSortingType(SortingType::kDescending, false);
}

class DocOperationTxnScanTest : public DocOperationScanTest {
 protected:
  void DoTestWithSortingType(SortingType sorting_type, bool is_forward_scan,
      size_t num_rows_per_key) override {
    ASSERT_OK(DisableCompactions());

    SetSortingType(sorting_type);

    TransactionStatusManagerMock txn_status_manager;
    SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

    InsertRows(num_rows_per_key, &txn_status_manager);

    PerformScans(is_forward_scan,
                 TransactionOperationContext(TransactionId::GenerateRandom(), &txn_status_manager),
                 [](size_t){});
  }
};

TEST_F_EX(DocOperationTest, QLTxnAscendingForwardScan, DocOperationTxnScanTest) {
  TestWithSortingType(SortingType::kAscending, true);
}

TEST_F_EX(DocOperationTest, QLTxnDescendingForwardScan, DocOperationTxnScanTest) {
  TestWithSortingType(SortingType::kDescending, true);
}

TEST_F_EX(DocOperationTest, QLTxnAscendingReverseScan, DocOperationTxnScanTest) {
  TestWithSortingType(SortingType::kAscending, false);
}

TEST_F_EX(DocOperationTest, QLTxnDescendingReverseScan, DocOperationTxnScanTest) {
  TestWithSortingType(SortingType::kDescending, false);
}

TEST_F(DocOperationTest, TestQLCompactions) {
  yb::QLWriteRequestPB ql_writereq_pb;
  yb::QLResponsePB ql_writeresp_pb;

  HybridTime t0 = HybridTime::FromMicrosecondsAndLogicalValue(1000, 0);
  HybridTime t0prime = HybridTime::FromMicrosecondsAndLogicalValue(1000, 1);
  HybridTime t1 = t0.AddMilliseconds(1001);

  // Define the schema.
  Schema schema = CreateSchema();
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);

  ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT{ physical: 1000 }]) -> \
{ 1: 1; ttl: 1.000s 2: 2; ttl: 1.000s 3: 3; ttl: 1.000s }; ttl: 1.000s
      )#"));

  FullyCompactHistoryBefore(t1);

  // Verify all entries are purged.
  ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(R"#(
      )#"));

  // Add a row with a TTL.
  WriteQLRow(QLWriteRequestPB_QLStmtType_QL_STMT_INSERT, schema, vector<int>({1, 1, 2, 3}),
      1000, t0);
  ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [SystemColumnId(0); HT{ physical: 1000 }]) -> null; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 w: 1 }]) -> 1; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 w: 2 }]) -> 2; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 w: 3 }]) -> 3; ttl: 1.000s
     )#",
     R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT{ physical: 1000 }]) -> \
{ 1: 1; ttl: 1.000s 2: 2; ttl: 1.000s 3: 3; ttl: 1.000s }; ttl: 1.000s
     )#"));

  // Update the columns with a higher TTL.
  yb::QLWriteRequestPB ql_update_pb;
  yb::QLResponsePB ql_update_resp_pb;
  ql_writereq_pb.set_type(
      QLWriteRequestPB_QLStmtType::QLWriteRequestPB_QLStmtType_QL_STMT_UPDATE);

  ql_writereq_pb.set_hash_code(kFixedHashCode);
  AddPrimaryKeyColumn(&ql_writereq_pb, 1);
  AddColumnValues(schema, {10, 20, 30}, &ql_writereq_pb);
  ql_writereq_pb.set_ttl(2000);

  // Write to docdb at the same physical time and a bumped-up logical time.
  WriteQL(ql_writereq_pb, schema, &ql_writeresp_pb, t0prime);

  ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(R"#(
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
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT{ physical: 1000 }]) -> \
    { 1: 1; ttl: 1.000s 2: 2; ttl: 1.000s 3: 3; ttl: 1.000s }; ttl: 1.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 logical: 1 }]) -> \
    10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 logical: 1 w: 1 }]) -> \
    20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 logical: 1 w: 2 }]) -> \
    30; ttl: 2.000s
      )#"));

  FullyCompactHistoryBefore(t1);

  // Verify the rest of the columns still live.
  ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(1); HT{ physical: 1000 logical: 1 }]) -> \
    10; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(2); HT{ physical: 1000 logical: 1 w: 1 }]) -> \
    20; ttl: 2.000s
SubDocKey(DocKey(0x0000, [1], []), [ColumnId(3); HT{ physical: 1000 logical: 1 w: 2 }]) -> \
    30; ttl: 2.000s
      )#",
      R"#(
SubDocKey(DocKey(0x0000, [1], []), [HT{ physical: 1000 logical: 1 }]) -> \
{ 1: 10; ttl: 2.000s; timestamp: 1000 2: 20; ttl: 2.000s; timestamp: 1000 \
3: 30; ttl: 2.000s; timestamp: 1000 }
      )#"));

  // Verify reads work well without system column id.
  QLRowBlock row_block = ReadQLRow(schema, 1, t1);
  ASSERT_EQ(1, row_block.row_count());
  EXPECT_EQ(1, row_block.row(0).column(0).int32_value());
  EXPECT_EQ(10, row_block.row(0).column(1).int32_value());
  EXPECT_EQ(20, row_block.row(0).column(2).int32_value());
  EXPECT_EQ(30, row_block.row(0).column(3).int32_value());
}

namespace {

size_t GenerateFiles(int total_batches, DocOperationTest* test, const int kBigFileFrequency = 7) {
  auto schema = test->CreateSchema();

  auto t0 = HybridTime::FromMicrosecondsAndLogicalValue(1000, 0);
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
    EXPECT_OK(test->FlushRocksDbAndWait());
  }
  return expected_files;
}

int CountBigFiles(const std::vector<rocksdb::LiveFileMetaData>& files,
    const size_t kMaxFileSize) {
  int num_large_files = 0;
  for (auto file : files) {
    if (ANNOTATE_UNPROTECTED_READ(file.uncompressed_size) > kMaxFileSize) {
      num_large_files++;
    }
  }
  return num_large_files;
}

void WaitCompactionsDone(rocksdb::DB* db) {
  for (;;) {
    std::this_thread::sleep_for(500ms);
    uint64_t value = 0;
    ANNOTATE_IGNORE_READS_BEGIN();
    ASSERT_TRUE(db->GetIntProperty(rocksdb::DB::Properties::kNumRunningCompactions, &value));
    ANNOTATE_IGNORE_READS_END();
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

  SetMaxFileSizeForCompaction(100_KB);
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

  SetMaxFileSizeForCompaction(100_KB);
  ASSERT_OK(ReinitDBOptions());

  WaitCompactionsDone(rocksdb());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_slowdown_writes_trigger) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_stop_writes_trigger) = 1;
  ASSERT_OK(ReinitDBOptions());

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_GE(files.size(), 3);

  auto handle_impl = down_cast<rocksdb::ColumnFamilyHandleImpl*>(
      regular_db_->DefaultColumnFamily());
  auto stats = handle_impl->cfd()->internal_stats();
  ANNOTATE_IGNORE_READS_BEGIN();
  ASSERT_EQ(0, stats->GetCFStats(rocksdb::InternalStats::LEVEL0_NUM_FILES_TOTAL));
  ASSERT_EQ(0, stats->GetCFStats(rocksdb::InternalStats::LEVEL0_SLOWDOWN_TOTAL));
  ANNOTATE_IGNORE_READS_END();
}

TEST_F(DocOperationTest, MaxFileSizeIgnoredWithFileFilter) {
  ASSERT_OK(DisableCompactions());
  const size_t kMaxFileSize = 100_KB;
  const int kNumFilesToWrite = 20;
  const int kBigFileFrequency = 7;
  const int kExpectedBigFiles = (kNumFilesToWrite-1)/kBigFileFrequency + 1;
  auto expected_files = GenerateFiles(kNumFilesToWrite, this, kBigFileFrequency);
  LOG(INFO) << "Files that would exist without compaction, if no filtering: " << expected_files;

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(kNumFilesToWrite, files.size());
  ASSERT_EQ(kExpectedBigFiles, CountBigFiles(files, kMaxFileSize));

  SetMaxFileSizeForCompaction(kMaxFileSize);

  // Use a filter factory that will expire every file.
  compaction_file_filter_factory_ =
      std::make_shared<DiscardUntilFileFilterFactory>(kAlwaysDiscard);

  ASSERT_OK(ReinitDBOptions());

  WaitCompactionsDone(rocksdb());

  files.clear();
  rocksdb()->GetLiveFilesMetaData(&files);

  // We expect all files to be filtered and thus removed, no matter the size.
  ASSERT_EQ(0, files.size());
  auto stats = rocksdb()->GetOptions().statistics;
  ASSERT_EQ(kNumFilesToWrite, stats->getTickerCount(rocksdb::COMPACTION_FILES_FILTERED));
}

TEST_F(DocOperationTest, EarlyFilesFilteredBeforeBigFile) {
  // Ensure that any number of files can trigger a compaction.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  ASSERT_OK(DisableCompactions());
  const size_t kMaxFileSize = 100_KB;
  const int kNumFilesToWrite = 20;
  const int kBigFileFrequency = 7;
  const int kNumFilesToExpire = 3;
  const int kExpectedBigFiles = (kNumFilesToWrite-1)/kBigFileFrequency + 1;
  // Expected files should be one fewer than if none were discarded, since the first "big file"
  // will be expired.
  auto expected_files = GenerateFiles(kNumFilesToWrite, this, kBigFileFrequency) - 1;
  LOG(INFO) << "Expiring " << kNumFilesToExpire <<
      " files, first kept big file at " << kBigFileFrequency + 1;
  LOG(INFO) << "Expected files after compaction: " << expected_files;

  std::vector<rocksdb::LiveFileMetaData> files;
  rocksdb()->GetLiveFilesMetaData(&files);
  ASSERT_EQ(kNumFilesToWrite, files.size());
  ASSERT_EQ(kExpectedBigFiles, CountBigFiles(files, kMaxFileSize));

  // Files will be ordered from latest to earliest, so select the nth file from the back.
  auto last_to_discard = files[files.size() - kNumFilesToExpire].name_id;

  SetMaxFileSizeForCompaction(kMaxFileSize);

  // Use a filter factory that will expire exactly three files.
  compaction_file_filter_factory_ =
      std::make_shared<DiscardUntilFileFilterFactory>(last_to_discard);

  // Reinitialize the DB options with the file filter factory.
  ASSERT_OK(ReinitDBOptions());

  // Compactions will be kicked off as part of options reinitialization.
  WaitCompactionsDone(rocksdb());

  files.clear();
  rocksdb()->GetLiveFilesMetaData(&files);

  // We expect three files to expire, one big file and two small ones.
  // The remaining small files should be compacted, leaving 5 files remaining.
  //
  // [large][small][small]....[large][small][small]...[large][small][small]...
  // becomes [compacted small][large][compacted small][large][compacted small]
  ASSERT_EQ(expected_files, files.size());

  auto stats = rocksdb()->GetOptions().statistics;
  ASSERT_EQ(kNumFilesToExpire, stats->getTickerCount(rocksdb::COMPACTION_FILES_FILTERED));
}

}  // namespace docdb
}  // namespace yb
