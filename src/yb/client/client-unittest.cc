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

#include "yb/client/client.h"
#include "yb/client/client-internal.h"

namespace yb {
namespace client {

using std::string;
using std::vector;

using namespace std::literals;
using namespace std::placeholders;

TEST(ClientUnitTest, TestSchemaBuilder_EmptySchema) {
  YBSchema s;
  YBSchemaBuilder b;
  ASSERT_EQ("Invalid argument: no primary key specified",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotSpecified) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->NotNull();
  b.AddColumn("b")->Type(INT32)->NotNull();
  ASSERT_EQ("Invalid argument: no primary key specified",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_DuplicateColumn) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(INT32);
  b.AddColumn("x")->Type(INT32);
  ASSERT_EQ("Invalid argument: Duplicate column name: x",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongPrimaryKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(INT32);
  b.AddColumn("x")->Type(INT32)->NotNull()->PrimaryKey();;
  b.AddColumn("x")->Type(INT32);
  const char *expected_status =
    "Invalid argument: The given columns in a schema must be ordered as hash primary key columns "
    "then primary key columns and then regular columns";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_WrongHashKeyOrder) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->PrimaryKey();
  b.AddColumn("b")->Type(INT32)->HashPrimaryKey();
  const char *expected_status =
    "Invalid argument: The given columns in a schema must be ordered as hash primary key columns "
    "then primary key columns and then regular columns";
  ASSERT_EQ(expected_status, b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_PrimaryKeyOnColumnAndSet) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->PrimaryKey();
  b.AddColumn("b")->Type(INT32);
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("Invalid argument: primary key specified by both "
            "SetPrimaryKey() and on a specific column: a",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_SingleKey_GoodSchema) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(INT32);
  b.AddColumn("c")->Type(INT32)->NotNull();
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_GoodSchema) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->NotNull();
  b.AddColumn("b")->Type(INT32)->NotNull();
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("OK", b.Build(&s).ToString());

  vector<int> key_columns;
  s.GetPrimaryKeyColumnIndexes(&key_columns);
  ASSERT_EQ(vector<int>({ 0, 1 }), key_columns);
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_KeyNotFirst) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("x")->Type(INT32)->NotNull();
  b.AddColumn("a")->Type(INT32)->NotNull();
  b.AddColumn("b")->Type(INT32)->NotNull();
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("Invalid argument: primary key columns must be listed "
            "first in the schema: a",
            b.Build(&s).ToString(/* no file/line */ false));
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_BadColumnName) {
  YBSchema s;
  YBSchemaBuilder b;
  b.AddColumn("a")->Type(INT32)->NotNull();
  b.AddColumn("b")->Type(INT32)->NotNull();
  b.SetPrimaryKey({ "foo" });
  ASSERT_EQ("Invalid argument: primary key column not defined: foo",
            b.Build(&s).ToString(/* no file/line */ false));
}

namespace {

Status TestFunc(CoarseTimePoint deadline, bool* retry, int* counter) {
  ++*counter;
  *retry = true;
  return STATUS(RuntimeError, "x");
}

} // anonymous namespace

TEST(ClientUnitTest, TestRetryFunc) {
  auto deadline = CoarseMonoClock::Now() + 100ms;
  int counter = 0;
  Status s =
      RetryFunc(deadline, "retrying test func", "timed out", std::bind(TestFunc, _1, _2, &counter));
  ASSERT_TRUE(s.IsTimedOut());
  ASSERT_GT(counter, 5);
  ASSERT_LT(counter, 20);
}

} // namespace client
} // namespace yb

