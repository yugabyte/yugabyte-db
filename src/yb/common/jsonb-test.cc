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

#include <string>
#include <type_traits>

#include <gtest/gtest.h>
#include <rapidjson/prettywriter.h>

#include "yb/common/jsonb.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

namespace yb {
namespace common {

void ParseJson(const std::string& json, rapidjson::Document* document) {
  Jsonb jsonb;
  LOG(INFO) << "Parsing json...";
  ASSERT_OK(jsonb.FromString(json));
  ASSERT_FALSE(jsonb.SerializedJsonb().empty());

  ASSERT_OK(jsonb.ToRapidJson(document));

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  document->Accept(writer);
  LOG (INFO) << "Deserialized json: " << buffer.GetString();
}

void VerifyArray(const rapidjson::Value& document) {
  ASSERT_TRUE(document.IsArray());
  ASSERT_EQ(1, document[0].GetInt());
  ASSERT_EQ(2, document[1].GetInt());
  ASSERT_DOUBLE_EQ(3.0, document[2].GetFloat());
  ASSERT_TRUE(document[3].IsFalse());
  ASSERT_TRUE(document[4].IsTrue());
  ASSERT_TRUE(document[5].IsObject());
  ASSERT_EQ(1, document[5]["k1"].GetInt());
  ASSERT_TRUE(document[5]["k2"].IsArray());
  ASSERT_EQ(100, document[5]["k2"][0].GetInt());
  ASSERT_EQ(200, document[5]["k2"][1].GetInt());
  ASSERT_EQ(300, document[5]["k2"][2].GetInt());
  ASSERT_TRUE(document[5]["k3"].IsTrue());
}

TEST(JsonbTest, TestJsonbSerialization) {
  rapidjson::Document document;
  ParseJson(R"#(
      {
        "b" : 1,
        "a1" : [1, 2, 3.0, false, true, { "k1" : 1, "k2" : [100, 200, 300], "k3" : true}],
        "a" :
        {
          "d" : true,
          "q" :
          {
            "p" : 4294967295,
            "r" : -2147483648,
            "s" : 2147483647
          },
          "g" : -100,
          "c" : false,
          "f" : "hello",
          "x" : 2.1,
          "y" : 9223372036854775807,
          "z" : -9223372036854775808,
          "u" : 18446744073709551615,
          "l" : 2147483647.123123e+75,
          "e" : null
        }
      })#", &document);

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

  ASSERT_TRUE(document["a"].HasMember("q"));
  ASSERT_TRUE(document["a"]["q"].IsObject());

  ASSERT_TRUE(document["a"]["q"].HasMember("p"));
  ASSERT_TRUE(document["a"]["q"]["p"].IsUint());
  ASSERT_EQ(4294967295, document["a"]["q"]["p"].GetUint());

  ASSERT_TRUE(document["a"]["q"].HasMember("r"));
  ASSERT_TRUE(document["a"]["q"]["r"].IsInt());
  ASSERT_EQ(-2147483648, document["a"]["q"]["r"].GetInt());

  ASSERT_TRUE(document["a"]["q"].HasMember("s"));
  ASSERT_TRUE(document["a"]["q"]["s"].IsInt());
  ASSERT_EQ(2147483647, document["a"]["q"]["s"].GetInt());

  ASSERT_TRUE(document["a"].HasMember("x"));
  ASSERT_TRUE(document["a"]["x"].IsFloat());
  ASSERT_FLOAT_EQ(2.1f, document["a"]["x"].GetFloat());

  ASSERT_TRUE(document["a"].HasMember("u"));
  ASSERT_TRUE(document["a"]["u"].IsUint64());
  ASSERT_EQ(18446744073709551615ULL, document["a"]["u"].GetUint64());

  ASSERT_TRUE(document["a"].HasMember("y"));
  ASSERT_TRUE(document["a"]["y"].IsInt64());
  ASSERT_EQ(9223372036854775807LL, document["a"]["y"].GetInt64());

  ASSERT_TRUE(document["a"].HasMember("z"));
  ASSERT_TRUE(document["a"]["z"].IsInt64());
  ASSERT_EQ(-9223372036854775808ULL, document["a"]["z"].GetInt64());

  ASSERT_TRUE(document["a"].HasMember("l"));
  ASSERT_TRUE(document["a"]["l"].IsDouble());
  ASSERT_DOUBLE_EQ(2147483647.123123e+75, document["a"]["l"].GetDouble());

  ASSERT_TRUE(document.HasMember("b"));
  ASSERT_TRUE(document["b"].IsInt64());
  ASSERT_EQ(1, document["b"].GetInt64());

  // Test array.
  ASSERT_TRUE(document.HasMember("a1"));
  ASSERT_TRUE(document["a1"].IsArray());
  VerifyArray(document["a1"]);
}

TEST(JsonbTest, TestArrays) {
  rapidjson::Document document;
  ParseJson(R"#(
        [1, 2, 3.0, false, true, { "k1" : 1, "k2" : [100, 200, 300], "k3" : true}]
      )#", &document);
  VerifyArray(document);
}

}  // namespace common
}  // namespace yb
