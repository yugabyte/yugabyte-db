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
// Tests for the client which are true unit tests and don't require a cluster, etc.

#include <boost/bind.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "kudu/client/client.h"
#include "kudu/client/client-internal.h"

using std::string;
using std::vector;

namespace kudu {
namespace client {

TEST(ClientUnitTest, TestSchemaBuilder_EmptySchema) {
  KuduSchema s;
  KuduSchemaBuilder b;
  ASSERT_EQ("Invalid argument: no primary key specified",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotSpecified) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->NotNull();
  ASSERT_EQ("Invalid argument: no primary key specified",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_DuplicateColumn) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("key")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("x")->Type(KuduColumnSchema::INT32);
  b.AddColumn("x")->Type(KuduColumnSchema::INT32);
  ASSERT_EQ("Invalid argument: Duplicate column name: x",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_KeyNotFirstColumn) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("key")->Type(KuduColumnSchema::INT32);
  b.AddColumn("x")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();;
  b.AddColumn("x")->Type(KuduColumnSchema::INT32);
  ASSERT_EQ("Invalid argument: primary key column must be the first column",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_TwoPrimaryKeys) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->PrimaryKey();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->PrimaryKey();
  ASSERT_EQ("Invalid argument: multiple columns specified for primary key: a, b",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_PrimaryKeyOnColumnAndSet) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->PrimaryKey();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32);
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("Invalid argument: primary key specified by both "
            "SetPrimaryKey() and on a specific column: a",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_SingleKey_GoodSchema) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32);
  b.AddColumn("c")->Type(KuduColumnSchema::INT32)->NotNull();
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_GoodSchema) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->NotNull();
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("OK", b.Build(&s).ToString());

  vector<int> key_columns;
  s.GetPrimaryKeyColumnIndexes(&key_columns);
  ASSERT_EQ(vector<int>({ 0, 1 }), key_columns);
}

TEST(ClientUnitTest, TestSchemaBuilder_DefaultValues) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->NotNull()
    ->Default(KuduValue::FromInt(12345));
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_DefaultValueString) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
  b.AddColumn("b")->Type(KuduColumnSchema::STRING)->NotNull()
    ->Default(KuduValue::CopyString("abc"));
  b.AddColumn("c")->Type(KuduColumnSchema::BINARY)->NotNull()
    ->Default(KuduValue::CopyString("def"));
  ASSERT_EQ("OK", b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_KeyNotFirst) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("x")->Type(KuduColumnSchema::INT32)->NotNull();
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->NotNull();
  b.SetPrimaryKey({ "a", "b" });
  ASSERT_EQ("Invalid argument: primary key columns must be listed "
            "first in the schema: a",
            b.Build(&s).ToString());
}

TEST(ClientUnitTest, TestSchemaBuilder_CompoundKey_BadColumnName) {
  KuduSchema s;
  KuduSchemaBuilder b;
  b.AddColumn("a")->Type(KuduColumnSchema::INT32)->NotNull();
  b.AddColumn("b")->Type(KuduColumnSchema::INT32)->NotNull();
  b.SetPrimaryKey({ "foo" });
  ASSERT_EQ("Invalid argument: primary key column not defined: foo",
            b.Build(&s).ToString());
}

namespace {
Status TestFunc(const MonoTime& deadline, bool* retry, int* counter) {
  (*counter)++;
  *retry = true;
  return Status::RuntimeError("x");
}
} // anonymous namespace

TEST(ClientUnitTest, TestRetryFunc) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(100));
  int counter = 0;
  Status s = RetryFunc(deadline, "retrying test func", "timed out",
                       boost::bind(TestFunc, _1, _2, &counter));
  ASSERT_TRUE(s.IsTimedOut());
  ASSERT_GT(counter, 5);
  ASSERT_LT(counter, 20);
}

} // namespace client
} // namespace kudu

