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
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/integral_types.h"
#include "yb/util/jsonreader.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

using rapidjson::Value;
using std::string;
using std::vector;
using strings::Substitute;

namespace yb {

TEST(JsonReaderTest, Corrupt) {
  JsonReader r("");
  Status s = r.Init();
  ASSERT_TRUE(s.IsCorruption());
  ASSERT_STR_CONTAINS(
      s.ToString(), "JSON text is corrupt: The document is empty.");
}

TEST(JsonReaderTest, Empty) {
  JsonReader r("{}");
  ASSERT_OK(r.Init());
  JsonReader r2("[]");
  ASSERT_OK(r2.Init());

  // Not found.
  ASSERT_TRUE(r.ExtractInt32(r.root(), "foo", nullptr).IsNotFound());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "foo", nullptr).IsNotFound());
  ASSERT_TRUE(r.ExtractString(r.root(), "foo", nullptr).IsNotFound());
  ASSERT_TRUE(r.ExtractObject(r.root(), "foo", nullptr).IsNotFound());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "foo", nullptr).IsNotFound());
}

TEST(JsonReaderTest, Basic) {
  JsonReader r("{ \"foo\" : \"bar\" }");
  ASSERT_OK(r.Init());
  string foo;
  ASSERT_OK(r.ExtractString(r.root(), "foo", &foo));
  ASSERT_EQ("bar", foo);

  // Bad types.
  ASSERT_TRUE(r.ExtractInt32(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "foo", nullptr).IsInvalidArgument());
}

TEST(JsonReaderTest, LessBasic) {
  string doc = Substitute(
      "{ \"small\" : 1, \"big\" : $0, \"null\" : null, \"empty\" : \"\" }", kint64max);
  JsonReader r(doc);
  ASSERT_OK(r.Init());
  int32_t small;
  ASSERT_OK(r.ExtractInt32(r.root(), "small", &small));
  ASSERT_EQ(1, small);
  int64_t big;
  ASSERT_OK(r.ExtractInt64(r.root(), "big", &big));
  ASSERT_EQ(kint64max, big);
  string str;
  ASSERT_OK(r.ExtractString(r.root(), "null", &str));
  ASSERT_EQ("", str);
  ASSERT_OK(r.ExtractString(r.root(), "empty", &str));
  ASSERT_EQ("", str);

  // Bad types.
  ASSERT_TRUE(r.ExtractString(r.root(), "small", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "small", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "small", nullptr).IsInvalidArgument());

  ASSERT_TRUE(r.ExtractInt32(r.root(), "big", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractString(r.root(), "big", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "big", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "big", nullptr).IsInvalidArgument());

  ASSERT_TRUE(r.ExtractInt32(r.root(), "null", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "null", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "null", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "null", nullptr).IsInvalidArgument());

  ASSERT_TRUE(r.ExtractInt32(r.root(), "empty", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "empty", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "empty", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "empty", nullptr).IsInvalidArgument());
}

TEST(JsonReaderTest, Objects) {
  JsonReader r("{ \"foo\" : { \"1\" : 1 } }");
  ASSERT_OK(r.Init());

  const Value* foo = nullptr;
  ASSERT_OK(r.ExtractObject(r.root(), "foo", &foo));
  ASSERT_TRUE(foo);

  int32_t one;
  ASSERT_OK(r.ExtractInt32(foo, "1", &one));
  ASSERT_EQ(1, one);

  // Bad types.
  ASSERT_TRUE(r.ExtractInt32(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractString(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObjectArray(r.root(), "foo", nullptr).IsInvalidArgument());
}

TEST(JsonReaderTest, TopLevelArray) {
  JsonReader r("[ { \"name\" : \"foo\" }, { \"name\" : \"bar\" } ]");
  ASSERT_OK(r.Init());

  vector<const Value*> objs;
  ASSERT_OK(r.ExtractObjectArray(r.root(), nullptr, &objs));
  ASSERT_EQ(2, objs.size());
  string name;
  ASSERT_OK(r.ExtractString(objs[0], "name", &name));
  ASSERT_EQ("foo", name);
  ASSERT_OK(r.ExtractString(objs[1], "name", &name));
  ASSERT_EQ("bar", name);

  // Bad types.
  ASSERT_TRUE(r.ExtractInt32(r.root(), nullptr, NULL).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), nullptr, NULL).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractString(r.root(), nullptr, NULL).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), nullptr, NULL).IsInvalidArgument());
}

TEST(JsonReaderTest, NestedArray) {
  JsonReader r("{ \"foo\" : [ { \"val\" : 0 }, { \"val\" : 1 }, { \"val\" : 2 } ] }");
  ASSERT_OK(r.Init());

  vector<const Value*> foo;
  ASSERT_OK(r.ExtractObjectArray(r.root(), "foo", &foo));
  ASSERT_EQ(3, foo.size());
  int i = 0;
  for (const Value* v : foo) {
    int32_t number;
    ASSERT_OK(r.ExtractInt32(v, "val", &number));
    ASSERT_EQ(i, number);
    i++;
  }

  // Bad types.
  ASSERT_TRUE(r.ExtractInt32(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractInt64(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractString(r.root(), "foo", nullptr).IsInvalidArgument());
  ASSERT_TRUE(r.ExtractObject(r.root(), "foo", nullptr).IsInvalidArgument());
}

} // namespace yb
