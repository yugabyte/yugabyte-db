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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"

#include "yb/util/errno.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

class WireProtocolTest : public YBTest {
 public:
  WireProtocolTest()
    : schema_({ ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                ColumnSchema("col2", DataType::STRING),
                ColumnSchema("col3", DataType::UINT32, ColumnKind::VALUE, Nullable::kTrue) }) {
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
  Status s = STATUS(NotFound, "foo", "bar");
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_FALSE(pb.has_posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(s.ToString(/* no file/line */ false), s2.ToString(/* no file/line */ false));
}

TEST_F(WireProtocolTest, TestBadStatusWithPosixCode) {
  Status s = STATUS(NotFound, "foo", "bar", Errno(1234));
  AppStatusPB pb;
  StatusToPB(s, &pb);
  EXPECT_EQ(AppStatusPB::NOT_FOUND, pb.code());
  EXPECT_TRUE(pb.has_message());
  EXPECT_EQ("foo: bar", pb.message());
  EXPECT_TRUE(pb.has_posix_code());
  EXPECT_EQ(1234, pb.posix_code());

  Status s2 = StatusFromPB(pb);
  EXPECT_TRUE(s2.IsNotFound());
  EXPECT_EQ(1234, Errno(s2));
  EXPECT_EQ(s.ToString(/* no file/line */ false), s2.ToString(/* no file/line */ false));
}

TEST_F(WireProtocolTest, TestSchemaRoundTrip) {
  google::protobuf::RepeatedPtrField<ColumnSchemaPB> pbs;

  SchemaToColumnPBs(schema_, &pbs);
  ASSERT_EQ(3, pbs.size());

  // Column 0.
  EXPECT_TRUE(pbs.Get(0).is_key());
  EXPECT_EQ("col1", pbs.Get(0).name());
  EXPECT_EQ(PersistentDataType::STRING, pbs.Get(0).type().main());
  EXPECT_FALSE(pbs.Get(0).is_nullable());

  // Column 1.
  EXPECT_FALSE(pbs.Get(1).is_key());
  EXPECT_EQ("col2", pbs.Get(1).name());
  EXPECT_EQ(PersistentDataType::STRING, pbs.Get(1).type().main());
  EXPECT_FALSE(pbs.Get(1).is_nullable());

  // Column 2.
  EXPECT_FALSE(pbs.Get(2).is_key());
  EXPECT_EQ("col3", pbs.Get(2).name());
  EXPECT_EQ(PersistentDataType::UINT32, pbs.Get(2).type().main());
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
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(true);

  // Column 1: not a key
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  // Column 2: marked as key. This is an error.
  col_pb = pbs.Add();
  col_pb->set_name("c2");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
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
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(true);

  // Column 1:
  col_pb = pbs.Add();
  col_pb->set_name("c1");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  // Column 2: same name as column 0
  col_pb = pbs.Add();
  col_pb->set_name("c0");
  QLType::Create(DataType::STRING)->ToQLTypePB(col_pb->mutable_type());
  col_pb->set_is_key(false);

  Schema schema;
  Status s = ColumnPBsToSchema(pbs, &schema);
  ASSERT_EQ("Invalid argument: Duplicate column name: c0",
            s.ToString(/* no file/line */ false));
}

} // namespace yb
