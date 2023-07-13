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
// Tests for the client which are true unit tests and don't require a cluster, etc.

#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/client-internal.h"
#include "yb/client/schema.h"

namespace yb {
namespace client {

using std::string;
using std::vector;

using namespace std::literals;
using namespace std::placeholders;

const std::string kNoPrimaryKeyMessage = "Invalid argument: No primary key specified";

TEST(ClientUnitTest, TestSchemaBuilder_EmptySchema) {
  YBSchema s;
  YBSchemaBuilder b;
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotSpecified) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull();
  b.AddColumn("b")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ(kNoPrimaryKeyMessage, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_DuplicateColumn) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32);
  ASSERT_EQ("Invalid argument: Duplicate column name: x",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongPrimaryKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32);
  b.AddColumn("x")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(DataType::INT32);
  const char *expected_status =
    "Invalid argument: Primary key column 'x' should be before regular column 'key'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongHashKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32)->HashPrimaryKey();
  const char *expected_status =
    "Invalid argument: Hash primary key column 'b' should be before primary key 'a'";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_SingleKey_GoodSchema) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(DataType::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(DataType::INT32);
  b.AddColumn("c")->Type(DataType::INT32)->NotNull();
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

} // namespace client
} // namespace yb
