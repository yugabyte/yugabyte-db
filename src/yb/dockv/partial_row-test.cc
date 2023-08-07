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

#include "yb/dockv/partial_row.h"
#include "yb/common/schema.h"

#include "yb/util/test_util.h"

using std::string;

namespace yb::dockv {

class PartialRowTest : public YBTest {
 public:
  PartialRowTest()
    : schema_({
          ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
          ColumnSchema("int_val", DataType::INT32),
          ColumnSchema("string_val", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue),
          ColumnSchema("binary_val", DataType::BINARY, ColumnKind::VALUE, Nullable::kTrue) }) {
    SeedRandom();
  }
 protected:
  Schema schema_;
};

TEST_F(PartialRowTest, UnitTest) {
  YBPartialRow row(&schema_);

  // Initially all columns are unset.
  EXPECT_FALSE(row.IsColumnSet(0));
  EXPECT_FALSE(row.IsColumnSet(1));
  EXPECT_FALSE(row.IsColumnSet(2));
  EXPECT_FALSE(row.IsKeySet());
  EXPECT_EQ("", row.ToString());

  // Set just the key.
  EXPECT_OK(row.SetInt32("key", 12345));
  EXPECT_TRUE(row.IsKeySet());
  EXPECT_FALSE(row.IsColumnSet(1));
  EXPECT_FALSE(row.IsColumnSet(2));
  EXPECT_EQ("int32 key=12345", row.ToString());
  int32_t x;
  EXPECT_OK(row.GetInt32("key", &x));
  EXPECT_EQ(12345, x);
  EXPECT_FALSE(row.IsNull("key"));

  // Fill in the other columns.
  EXPECT_OK(row.SetInt32("int_val", 54321));
  EXPECT_OK(row.SetStringCopy("string_val", "hello world"));
  EXPECT_TRUE(row.IsColumnSet(1));
  EXPECT_TRUE(row.IsColumnSet(2));
  EXPECT_EQ("int32 key=12345, int32 int_val=54321, string string_val=hello world",
            row.ToString());
  Slice slice;
  EXPECT_OK(row.GetString("string_val", &slice));
  EXPECT_EQ("hello world", slice.ToString());
  EXPECT_FALSE(row.IsNull("key"));

  // Set a nullable entry to NULL
  EXPECT_OK(row.SetNull("string_val"));
  EXPECT_EQ("int32 key=12345, int32 int_val=54321, string string_val=NULL",
            row.ToString());
  EXPECT_TRUE(row.IsNull("string_val"));

  // Try to set an entry with the wrong type
  Status s = row.SetStringCopy("int_val", "foo");
  EXPECT_EQ("Invalid argument: invalid type string provided for column 'int_val' (expected int32)",
            s.ToString(/* no file/line */ false));

  // Try to get an entry with the wrong type
  s = row.GetString("int_val", &slice);
  EXPECT_EQ("Invalid argument: invalid type string provided for column 'int_val' (expected int32)",
            s.ToString(false));

  // Try to set a non-nullable entry to NULL
  s = row.SetNull("key");
  EXPECT_EQ("Invalid argument: column not nullable: key[int32 NOT NULL RANGE_ASC_NULL_FIRST]",
            s.ToString(false));

  // Set the NULL string back to non-NULL
  EXPECT_OK(row.SetStringCopy("string_val", "goodbye world"));
  EXPECT_EQ("int32 key=12345, int32 int_val=54321, string string_val=goodbye world",
            row.ToString());

  // Unset some columns.
  EXPECT_OK(row.Unset("string_val"));
  EXPECT_EQ("int32 key=12345, int32 int_val=54321", row.ToString());

  EXPECT_OK(row.Unset("key"));
  EXPECT_EQ("int32 int_val=54321", row.ToString());

  // Set the column by index
  EXPECT_OK(row.SetInt32(1, 99999));
  EXPECT_EQ("int32 int_val=99999", row.ToString());

  // Set the binary column as a copy.
  EXPECT_OK(row.SetBinaryCopy("binary_val", "hello_world"));
  EXPECT_EQ("int32 int_val=99999, binary binary_val=hello_world",
              row.ToString());
  // Unset the binary column.
  EXPECT_OK(row.Unset("binary_val"));
  EXPECT_EQ("int32 int_val=99999", row.ToString());

  // Even though the storage is actually the same at the moment, we shouldn't be
  // able to set string columns with SetBinary and vice versa.
  EXPECT_FALSE(row.SetBinaryCopy("string_val", "oops").ok());
  EXPECT_FALSE(row.SetStringCopy("binary_val", "oops").ok());
}

TEST_F(PartialRowTest, TestCopy) {
  YBPartialRow row(&schema_);

  // The assignment operator is used in this test because it internally calls
  // the copy constructor.

  // Check an empty copy.
  YBPartialRow copy = row;
  EXPECT_FALSE(copy.IsColumnSet(0));
  EXPECT_FALSE(copy.IsColumnSet(1));
  EXPECT_FALSE(copy.IsColumnSet(2));

  ASSERT_OK(row.SetInt32(0, 42));
  ASSERT_OK(row.SetInt32(1, 99));
  ASSERT_OK(row.SetStringCopy(2, "copied-string"));

  int32_t int_val;
  Slice string_val;
  Slice binary_val;

  // Check a copy with values.
  copy = row;
  ASSERT_OK(copy.GetInt32(0, &int_val));
  EXPECT_EQ(42, int_val);
  ASSERT_OK(copy.GetInt32(1, &int_val));
  EXPECT_EQ(99, int_val);
  ASSERT_OK(copy.GetString(2, &string_val));
  EXPECT_EQ("copied-string", string_val.ToString());

  // Check a copy with a null value.
  ASSERT_OK(row.SetNull(2));
  copy = row;
  EXPECT_TRUE(copy.IsNull(2));

  // Check a copy with a borrowed value.
  string borrowed_string = "borrowed-string";
  string borrowed_binary = "borrowed-binary";
  ASSERT_OK(row.SetString(2, borrowed_string));
  ASSERT_OK(row.SetBinary(3, borrowed_binary));

  copy = row;
  ASSERT_OK(copy.GetString(2, &string_val));
  EXPECT_EQ("borrowed-string", string_val.ToString());
  ASSERT_OK(copy.GetBinary(3, &binary_val));
  EXPECT_EQ("borrowed-binary", binary_val.ToString());

  borrowed_string.replace(0, 8, "mutated-");
  borrowed_binary.replace(0, 8, "mutated-");
  ASSERT_OK(copy.GetString(2, &string_val));
  EXPECT_EQ("mutated--string", string_val.ToString());
  ASSERT_OK(copy.GetBinary(3, &string_val));
  EXPECT_EQ("mutated--binary", string_val.ToString());
}

} // namespace yb::dockv
