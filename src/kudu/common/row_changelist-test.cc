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

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "kudu/common/schema.h"
#include "kudu/common/row_changelist.h"
#include "kudu/common/row.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/faststring.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

namespace kudu {

using strings::Substitute;

class TestRowChangeList : public KuduTest {
 public:
  TestRowChangeList() :
    schema_(CreateSchema())
  {}

  static Schema CreateSchema() {
    SchemaBuilder builder;
    CHECK_OK(builder.AddKeyColumn("col1", STRING));
    CHECK_OK(builder.AddColumn("col2", STRING));
    CHECK_OK(builder.AddColumn("col3", UINT32));
    CHECK_OK(builder.AddNullableColumn("col4", UINT32));
    return(builder.Build());
  }

 protected:
  Schema schema_;
};

TEST_F(TestRowChangeList, TestEncodeDecodeUpdates) {
  faststring buf;
  RowChangeListEncoder rcl(&buf);

  // Construct an update with several columns changed
  Slice update1("update1");
  Slice update2("update2");
  uint32 update3 = 12345;

  int c0_id = schema_.column_id(0);
  int c1_id = schema_.column_id(1);
  int c2_id = schema_.column_id(2);
  int c3_id = schema_.column_id(3);

  rcl.AddColumnUpdate(schema_.column(0), c0_id, &update1);
  rcl.AddColumnUpdate(schema_.column(1), c1_id, &update2);
  rcl.AddColumnUpdate(schema_.column(2), c2_id, &update3);
  rcl.AddColumnUpdate(schema_.column(3), c3_id, nullptr);

  LOG(INFO) << "Encoded: " << HexDump(buf);

  // Read it back.
  EXPECT_EQ(string("SET col1=update1, col2=update2, col3=12345, col4=NULL"),
            RowChangeList(Slice(buf)).ToString(schema_));

  RowChangeListDecoder decoder((RowChangeList(buf)));
  ASSERT_OK(decoder.Init());
  RowChangeListDecoder::DecodedUpdate dec;
  ASSERT_TRUE(decoder.HasNext());
  ASSERT_OK(decoder.DecodeNext(&dec));
  ASSERT_EQ(c0_id, dec.col_id);
  ASSERT_EQ(update1, dec.raw_value);

  ASSERT_TRUE(decoder.HasNext());
  ASSERT_OK(decoder.DecodeNext(&dec));
  ASSERT_EQ(c1_id, dec.col_id);
  ASSERT_EQ(update2, dec.raw_value);

  ASSERT_TRUE(decoder.HasNext());
  ASSERT_OK(decoder.DecodeNext(&dec));
  ASSERT_EQ(c2_id, dec.col_id);
  ASSERT_EQ("90\\x00\\x00", dec.raw_value.ToDebugString());

  ASSERT_TRUE(decoder.HasNext());
  ASSERT_OK(decoder.DecodeNext(&dec));
  ASSERT_EQ(c3_id, dec.col_id);
  ASSERT_TRUE(dec.null);

  ASSERT_FALSE(decoder.HasNext());

  // ToString() with unknown columns should still be able to parse
  // the whole changelist.
  EXPECT_EQ(Substitute("SET [unknown column id $0]=update1, "
                       "[unknown column id $1]=update2, "
                       "[unknown column id $2]=90\\x00\\x00, "
                       "[unknown column id $3]=NULL",
                       c0_id, c1_id, c2_id, c3_id),
            RowChangeList(Slice(buf)).ToString(Schema()));
}

TEST_F(TestRowChangeList, TestDeletes) {
  faststring buf;
  RowChangeListEncoder rcl(&buf);

  // Construct a deletion.
  rcl.SetToDelete();

  LOG(INFO) << "Encoded: " << HexDump(buf);

  // Read it back.
  EXPECT_EQ(string("DELETE"), RowChangeList(Slice(buf)).ToString(schema_));

  RowChangeListDecoder decoder((RowChangeList(buf)));
  ASSERT_OK(decoder.Init());
  ASSERT_TRUE(decoder.is_delete());
}

TEST_F(TestRowChangeList, TestReinserts) {
  RowBuilder rb(schema_);
  rb.AddString(Slice("hello"));
  rb.AddString(Slice("world"));
  rb.AddUint32(12345);
  rb.AddNull();

  // Construct a REINSERT.
  faststring buf;
  RowChangeListEncoder rcl(&buf);
  rcl.SetToReinsert(rb.data());

  LOG(INFO) << "Encoded: " << HexDump(buf);

  // Read it back.
  EXPECT_EQ(string("REINSERT (string col1=hello, string col2=world, "
                   "uint32 col3=12345, uint32 col4=NULL)"),
            RowChangeList(Slice(buf)).ToString(schema_));

  RowChangeListDecoder decoder((RowChangeList(buf)));
  ASSERT_OK(decoder.Init());
  ASSERT_TRUE(decoder.is_reinsert());

  Slice s;
  ASSERT_OK(decoder.GetReinsertedRowSlice(schema_, &s));
  ASSERT_EQ(s, rb.data());
}

TEST_F(TestRowChangeList, TestInvalid_EmptySlice) {
  RowChangeListDecoder decoder((RowChangeList(Slice())));
  ASSERT_STR_CONTAINS(decoder.Init().ToString(),
                      "empty changelist");
}

TEST_F(TestRowChangeList, TestInvalid_BadTypeEnum) {
  RowChangeListDecoder decoder(RowChangeList(Slice("\xff", 1)));
  ASSERT_STR_CONTAINS(decoder.Init().ToString(),
                      "Corruption: bad type enum value: 255 in \\xff");
}

TEST_F(TestRowChangeList, TestInvalid_TooLongDelete) {
  RowChangeListDecoder decoder(RowChangeList(Slice("\x02""blahblah")));
  ASSERT_STR_CONTAINS(decoder.Init().ToString(),
                      "Corruption: DELETE changelist too long");
}

TEST_F(TestRowChangeList, TestInvalid_TooShortReinsert) {
  RowChangeListDecoder decoder(RowChangeList(Slice("\x03")));
  ASSERT_OK(decoder.Init());
  Slice s;
  ASSERT_STR_CONTAINS(decoder.GetReinsertedRowSlice(schema_, &s).ToString(),
                      "Corruption: REINSERT changelist wrong length");
}

TEST_F(TestRowChangeList, TestInvalid_SetNullForNonNullableColumn) {
  faststring buf;
  RowChangeListEncoder rcl(&buf);
  // Set column 0 = NULL
  rcl.AddRawColumnUpdate(schema_.column_id(0), true, Slice());

  ASSERT_EQ("[invalid update: Corruption: decoded set-to-NULL "
            "for non-nullable column: col1[string NOT NULL], "
            "before corruption: SET ]",
            RowChangeList(Slice(buf)).ToString(schema_));
}

TEST_F(TestRowChangeList, TestInvalid_SetWrongSizeForIntColumn) {
  faststring buf;
  RowChangeListEncoder rcl(&buf);
  // Set column id 2 = \xff
  // (column id 2 is UINT32, so should be 4 bytes)
  rcl.AddRawColumnUpdate(schema_.column_id(2), false, Slice("\xff"));

  ASSERT_EQ("[invalid update: Corruption: invalid value \\xff "
            "for column col3[uint32 NOT NULL], "
            "before corruption: SET ]",
            RowChangeList(Slice(buf)).ToString(schema_));
}

} // namespace kudu
