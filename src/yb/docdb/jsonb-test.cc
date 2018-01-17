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

#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

#include "yb/docdb/jsonb.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace docdb {

TEST(JsonbTest, TestJsonbSerialization) {
  std::string jsonb;
  ASSERT_OK(Jsonb::ToJsonb(
      "{ \"b\" : 1, \"a\" : { \"d\" : true, \"g\" : -100, \"c\" : false, \"f\" : \"hello\", "
          "\"e\" : null} }", &jsonb));
  ASSERT_FALSE(jsonb.empty());

  rapidjson::Document document;
  ASSERT_OK(Jsonb::FromJsonb(jsonb, &document));
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  LOG (INFO) << "Deserialized json: " << buffer.GetString();

  // Verify the json.
  ASSERT_TRUE(document.HasMember("a"));
  ASSERT_TRUE(document["a"].IsObject());

  ASSERT_TRUE(document["a"].HasMember("c"));
  ASSERT_TRUE(document["a"]["c"].IsFalse());
  ASSERT_TRUE(document["a"].HasMember("d"));
  ASSERT_TRUE(document["a"]["d"].IsTrue());
  ASSERT_TRUE(document["a"].HasMember("e"));
  ASSERT_TRUE(document["a"]["e"].IsNull());
  ASSERT_TRUE(document["a"].HasMember("f"));
  ASSERT_TRUE(document["a"]["f"].IsString());
  ASSERT_EQ("hello", std::string(document["a"]["f"].GetString()));
  ASSERT_TRUE(document["a"].HasMember("g"));
  ASSERT_TRUE(document["a"]["g"].IsInt64());
  ASSERT_EQ(-100, document["a"]["g"].GetInt64());

  ASSERT_TRUE(document.HasMember("b"));
  ASSERT_TRUE(document["b"].IsInt64());
  ASSERT_EQ(1, document["b"].GetInt64());
}

}  // namespace docdb
}  // namespace yb
