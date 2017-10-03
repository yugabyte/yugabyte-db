// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>
#include "kudu/common/row.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/util/status.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

namespace kudu {

class WireProtocolTest : public KuduTest {
 public:
  WireProtocolTest()
    : schema_({ ColumnSchema("col1", STRING),
                ColumnSchema("col2", STRING),
                ColumnSchema("col3", UINT32, true /* nullable */) },
              1) {
  }

  void FillRowBlockWithTestRows(RowBlock* block) {
    block->selection_vector()->SetAllTrue();

    for (int i = 0; i < block->nrows(); i++) {
      RowBlockRow row = block->row(i);
      *reinterpret_cast<Slice*>(row.mutable_cell_ptr(0)) = Slice("hello world col1");
      *reinterpret_cast<Slice*>(row.mutable_cell_ptr(1)) = Slice("hello world col2");
      *reinterpret_cast<uint32_t*>(row.mutable_cell_ptr(2)) = i;
      row.cell(2).set_null(false);
    }
  }
 protected:
  Schema schema_;
};

TEST_F(WireProtocolTest, TestOKStatus) {
  Status s = Status::OK();
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::OK, pb.code());
  EXPECT_FALSE(pb.has_message());
  EXPECT_FALSE(pb.has_posix_code());

  Status s2 = StatusFromPB(pb);
  ASSERT_OK(s2);
}

TEST_F(WireProtocolTest, TestBadStatus) {
  Status s = Status::NotFound("foo", "bar");
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_FALSE(pb.has_posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(s.ToString(), s2.ToString());
}

TEST_F(WireProtocolTest, TestBadStatusWithPosixCode) {
  Status s = Status::NotFound("foo", "bar", 1234);
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_TRUE(pb.has_posix_code());
  EXPECT_EQ(1234, pb.posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(1234, s2.posix_code());
  EXPECT_EQ(s.ToString(), s2.ToString());
}

TEST_F(WireProtocolTest, TestSchemaRoundTrip) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  ASSERT_OK(SchemaToColumnPBs(schema_, &pbs));
  ASSERT_EQ(3, pbs.size());

  // Column 0.
  EXPECT_TRUE(pbs.Get(0).is_key());
  EXPECT_EQ("col1", pbs.Get(0).name());
  EXPECT_EQ(STRING, pbs.Get(0).type());
  EXPECT_FALSE(pbs.Get(0).is_nullable());

  // Column 1.
  EXPECT_FALSE(pbs.Get(1).is_key());
  EXPECT_EQ("col2", pbs.Get(1).name());
  EXPECT_EQ(STRING, pbs.Get(1).type());
  EXPECT_FALSE(pbs.Get(1).is_nullable());

  // Column 2.
  EXPECT_FALSE(pbs.Get(2).is_key());
  EXPECT_EQ("col3", pbs.Get(2).name());
  EXPECT_EQ(UINT32, pbs.Get(2).type());
  EXPECT_TRUE(pbs.Get(2).is_nullable());

  // Convert back to a Schema object and verify they're identical.
  Schema schema2;
  ASSERT_OK(ColumnPBsToSchema(pbs, &schema2));
  EXPECT_EQ(schema_.ToString(), schema2.ToString());
  EXPECT_EQ(schema_.num_key_columns(), schema2.num_key_columns());
}

// Test that, when non-contiguous key columns are passed, an error Status
// is returned.
TEST_F(WireProtocolTest, TestBadSchema_NonContiguousKey) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  // Column 0: key
  ColumnSchemaPB* col_pb = pbs.Add();
  col_pb->set_name("c0");
  col_pb->set_type(STRING);
  col_pb->set_is_key(true);

  // Column 1: not a key
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  col_pb->set_type(STRING);
  col_pb->set_is_key(false);

  // Column 2: marked as key. This is an error.
  col_pb = pbs.Add();
  col_pb->set_name("c2");
  col_pb->set_type(STRING);
  col_pb->set_is_key(true);

  Schema schema;
  Status s = ColumnPBsToSchema(pbs, &schema);
  ASSERT_STR_CONTAINS(s.ToString(), "Got out-of-order key column");
}

// Test that, when multiple columns with the same name are passed, an
// error Status is returned.
TEST_F(WireProtocolTest, TestBadSchema_DuplicateColumnName) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  // Column 0:
  ColumnSchemaPB* col_pb = pbs.Add();
  col_pb->set_name("c0");
  col_pb->set_type(STRING);
  col_pb->set_is_key(true);

  // Column 1:
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  col_pb->set_type(STRING);
  col_pb->set_is_key(false);

  // Column 2: same name as column 0
  col_pb = pbs.Add();
  col_pb->set_name("c0");
  col_pb->set_type(STRING);
  col_pb->set_is_key(false);

  Schema schema;
  Status s = ColumnPBsToSchema(pbs, &schema);
  ASSERT_EQ("Invalid argument: Duplicate column name: c0", s.ToString());
}

// Create a block of rows in columnar layout and ensure that it can be
// converted to and from protobuf.
TEST_F(WireProtocolTest, TestColumnarRowBlockToPB) {
  Arena arena(1024, 1024 * 1024);
  RowBlock block(schema_, 10, &arena);
  FillRowBlockWithTestRows(&block);

  // Convert to PB.
  RowwiseRowBlockPB pb;
  faststring direct, indirect;
  SerializeRowBlock(block, &pb, nullptr, &direct, &indirect);
  SCOPED_TRACE(pb.DebugString());
  SCOPED_TRACE("Row data: " + direct.ToString());
  SCOPED_TRACE("Indirect data: " + indirect.ToString());

  // Convert back to a row, ensure that the resulting row is the same
  // as the one we put in.
  vector<const uint8_t*> row_ptrs;
  Slice direct_sidecar = direct;
  ASSERT_OK(ExtractRowsFromRowBlockPB(schema_, pb, indirect,
                                             &direct_sidecar, &row_ptrs));
  ASSERT_EQ(block.nrows(), row_ptrs.size());
  for (int i = 0; i < block.nrows(); ++i) {
    ConstContiguousRow row_roundtripped(&schema_, row_ptrs[i]);
    EXPECT_EQ(schema_.DebugRow(block.row(i)),
              schema_.DebugRow(row_roundtripped));
  }
}

#ifdef NDEBUG
TEST_F(WireProtocolTest, TestColumnarRowBlockToPBBenchmark) {
  Arena arena(1024, 1024 * 1024);
  const int kNumTrials = AllowSlowTests() ? 100 : 10;
  RowBlock block(schema_, 10000 * kNumTrials, &arena);
  FillRowBlockWithTestRows(&block);

  RowwiseRowBlockPB pb;

  LOG_TIMING(INFO, "Converting to PB") {
    for (int i = 0; i < kNumTrials; i++) {
      pb.Clear();
      faststring direct, indirect;
      SerializeRowBlock(block, &pb, NULL, &direct, &indirect);
    }
  }
}
#endif

// Test that trying to extract rows from an invalid block correctly returns
// Corruption statuses.
TEST_F(WireProtocolTest, TestInvalidRowBlock) {
  Schema schema({ ColumnSchema("col1", STRING) }, 1);
  RowwiseRowBlockPB pb;
  vector<const uint8_t*> row_ptrs;

  // Too short to be valid data.
  const char* shortstr = "x";
  pb.set_num_rows(1);
  Slice direct = shortstr;
  Status s = ExtractRowsFromRowBlockPB(schema, pb, Slice(), &direct, &row_ptrs);
  ASSERT_STR_CONTAINS(s.ToString(), "Corruption: Row block has 1 bytes of data");

  // Bad pointer into indirect data.
  shortstr = "xxxxxxxxxxxxxxxx";
  pb.set_num_rows(1);
  direct = Slice(shortstr);
  s = ExtractRowsFromRowBlockPB(schema, pb, Slice(), &direct, &row_ptrs);
  ASSERT_STR_CONTAINS(s.ToString(),
                      "Corruption: Row #0 contained bad indirect slice");
}

// Test serializing a block which has a selection vector but no columns.
// This is the sort of result that is returned from a scan with an empty
// projection (a COUNT(*) query).
TEST_F(WireProtocolTest, TestBlockWithNoColumns) {
  Schema empty(std::vector<ColumnSchema>(), 0);
  Arena arena(1024, 1024 * 1024);
  RowBlock block(empty, 1000, &arena);
  block.selection_vector()->SetAllTrue();
  // Unselect 100 rows
  for (int i = 0; i < 100; i++) {
    block.selection_vector()->SetRowUnselected(i * 2);
  }
  ASSERT_EQ(900, block.selection_vector()->CountSelected());

  // Convert it to protobuf, ensure that the results look right.
  RowwiseRowBlockPB pb;
  faststring direct, indirect;
  SerializeRowBlock(block, &pb, nullptr, &direct, &indirect);
  ASSERT_EQ(900, pb.num_rows());
}

TEST_F(WireProtocolTest, TestColumnDefaultValue) {
  Slice write_default_str("Hello Write");
  Slice read_default_str("Hello Read");
  uint32_t write_default_u32 = 512;
  uint32_t read_default_u32 = 256;
  ColumnSchemaPB pb;

  ColumnSchema col1("col1", STRING);
  ColumnSchemaToPB(col1, &pb);
  ColumnSchema col1fpb = ColumnSchemaFromPB(pb);
  ASSERT_FALSE(col1fpb.has_read_default());
  ASSERT_FALSE(col1fpb.has_write_default());
  ASSERT_TRUE(col1fpb.read_default_value() == nullptr);

  ColumnSchema col2("col2", STRING, false, &read_default_str);
  ColumnSchemaToPB(col2, &pb);
  ColumnSchema col2fpb = ColumnSchemaFromPB(pb);
  ASSERT_TRUE(col2fpb.has_read_default());
  ASSERT_FALSE(col2fpb.has_write_default());
  ASSERT_EQ(read_default_str, *static_cast<const Slice *>(col2fpb.read_default_value()));
  ASSERT_EQ(nullptr, static_cast<const Slice *>(col2fpb.write_default_value()));

  ColumnSchema col3("col3", STRING, false, &read_default_str, &write_default_str);
  ColumnSchemaToPB(col3, &pb);
  ColumnSchema col3fpb = ColumnSchemaFromPB(pb);
  ASSERT_TRUE(col3fpb.has_read_default());
  ASSERT_TRUE(col3fpb.has_write_default());
  ASSERT_EQ(read_default_str, *static_cast<const Slice *>(col3fpb.read_default_value()));
  ASSERT_EQ(write_default_str, *static_cast<const Slice *>(col3fpb.write_default_value()));

  ColumnSchema col4("col4", UINT32, false, &read_default_u32);
  ColumnSchemaToPB(col4, &pb);
  ColumnSchema col4fpb = ColumnSchemaFromPB(pb);
  ASSERT_TRUE(col4fpb.has_read_default());
  ASSERT_FALSE(col4fpb.has_write_default());
  ASSERT_EQ(read_default_u32, *static_cast<const uint32_t *>(col4fpb.read_default_value()));
  ASSERT_EQ(nullptr, static_cast<const uint32_t *>(col4fpb.write_default_value()));

  ColumnSchema col5("col5", UINT32, false, &read_default_u32, &write_default_u32);
  ColumnSchemaToPB(col5, &pb);
  ColumnSchema col5fpb = ColumnSchemaFromPB(pb);
  ASSERT_TRUE(col5fpb.has_read_default());
  ASSERT_TRUE(col5fpb.has_write_default());
  ASSERT_EQ(read_default_u32, *static_cast<const uint32_t *>(col5fpb.read_default_value()));
  ASSERT_EQ(write_default_u32, *static_cast<const uint32_t *>(col5fpb.write_default_value()));
}

} // namespace kudu
