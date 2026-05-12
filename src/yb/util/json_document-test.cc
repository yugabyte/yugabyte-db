// Copyright (c) YugabyteDB, Inc.
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

#include "yb/util/json_document.h"

#include <cstdint>
#include <string>
#include <vector>

#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {

class JsonDocumentTest : public YBTest {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(JsonDocumentTest, ParseValidJson) {
  std::string json_str = R"({"a":null,"b":[{"c":"x"},{"c":"y"}],"d":true})";
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(json_str));

  ASSERT_EQ(ASSERT_RESULT(root.ToString()), json_str);

  ASSERT_TRUE(root["a"].IsNull());

  auto b = root["b"];
  auto b_size = ASSERT_RESULT(b.size());
  ASSERT_EQ(b_size, 2u);

  std::vector<std::string> vals;
  for (size_t i = 0; i < b_size; ++i) {
    JsonValue item = b[i];
    auto c = ASSERT_RESULT(item["c"].GetString());
    vals.push_back(c);
  }
  ASSERT_EQ(vals, (decltype(vals){"x", "y"}));

  auto d = ASSERT_RESULT(root["d"].GetBool());
  ASSERT_TRUE(d);
}

TEST_F(JsonDocumentTest, ParseInvalidJson) {
  JsonDocument doc;
  auto res = doc.Parse("{invalid json");
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsInvalidArgument());
}

TEST_F(JsonDocumentTest, TypeMismatch) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"a": "str"})"));
  auto res = root["a"].GetInt32();
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsInvalidArgument());

  auto res2 = root["a"].GetArray();
  ASSERT_NOK(res2);
  ASSERT_TRUE(res2.status().IsInvalidArgument());
}

TEST_F(JsonDocumentTest, IsValidAndIsNull) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({
    "present": null,
    "object": {"k": 1}
  })"));

  // Existing member that's null.
  JsonValue v_present = root["present"];
  ASSERT_TRUE(v_present.IsValid());
  ASSERT_TRUE(v_present.IsNull());

  // Existing member that's an object.
  JsonValue v_obj = root["object"];
  ASSERT_TRUE(v_obj.IsValid());
  ASSERT_FALSE(v_obj.IsNull());
  ASSERT_TRUE(v_obj.IsObject());

  // Missing member yields invalid JsonValue.
  JsonValue v_missing = root["missing"];
  ASSERT_FALSE(v_missing.IsValid());
  ASSERT_FALSE(v_missing.IsNull());
}

TEST_F(JsonDocumentTest, Size) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"a": 1, "arr":[1,2,3], "obj":{"k":"v"}})"));

  auto s1 = root["a"].size();
  ASSERT_NOK(s1);
  ASSERT_TRUE(s1.status().IsInvalidArgument());

  auto s2 = ASSERT_RESULT(root["arr"].size());
  ASSERT_EQ(s2, 3u);
  ASSERT_EQ(ASSERT_RESULT(root["arr"].GetArray()).size(), s2);

  auto s3 = ASSERT_RESULT(root["obj"].size());
  ASSERT_EQ(s3, 1u);
  ASSERT_EQ(ASSERT_RESULT(root["obj"].GetObject()).size(), s3);
}

TEST_F(JsonDocumentTest, NumberRange) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"-large": -1.0e+39,
                                          "-2^63-1": -9223372036854775809,
                                          "-2^63": -9223372036854775808,
                                          "-2^31-1": -2147483649,
                                          "-2^31": -2147483648,
                                          "-1": -1,
                                          "0": 0,
                                          "0.0": 0.0,
                                          "2^31-1": 2147483647,
                                          "2^31": 2147483648,
                                          "2^32-1": 4294967295,
                                          "2^32": 4294967296,
                                          "2^64-1": 18446744073709551615,
                                          "2^64": 18446744073709551616,
                                          "+large": 1.0e+39})"));
  int32_t res_int32;
  uint32_t res_uint32;
  int64_t res_int64;
  uint64_t res_uint64;
  float res_float;
  double res_double;

  EXPECT_FALSE(root["-large"].IsInt32());
  EXPECT_FALSE(root["-large"].IsUint32());
  EXPECT_FALSE(root["-large"].IsInt64());
  EXPECT_FALSE(root["-large"].IsUint64());
  EXPECT_FALSE(root["-large"].IsFloat());
  EXPECT_TRUE(root["-large"].IsDouble());
  EXPECT_TRUE(root["-large"].IsNumber());
  res_double = ASSERT_RESULT(root["-large"].GetDouble());
  EXPECT_EQ(res_double, -1.0e+39);

  EXPECT_FALSE(root["-2^63-1"].IsInt32());
  EXPECT_FALSE(root["-2^63-1"].IsUint32());
  EXPECT_FALSE(root["-2^63-1"].IsInt64());
  EXPECT_FALSE(root["-2^63-1"].IsUint64());
  EXPECT_TRUE(root["-2^63-1"].IsFloat());
  EXPECT_TRUE(root["-2^63-1"].IsDouble());
  EXPECT_TRUE(root["-2^63-1"].IsNumber());
  res_float = ASSERT_RESULT(root["-2^63-1"].GetFloat());
  EXPECT_EQ(res_float, static_cast<float>(std::numeric_limits<int64_t>::min()) - 1);
  res_double = ASSERT_RESULT(root["-2^63-1"].GetDouble());
  EXPECT_EQ(res_double, static_cast<double>(std::numeric_limits<int64_t>::min()) - 1);

  EXPECT_FALSE(root["-2^63"].IsInt32());
  EXPECT_FALSE(root["-2^63"].IsUint32());
  EXPECT_TRUE(root["-2^63"].IsInt64());
  EXPECT_FALSE(root["-2^63"].IsUint64());
  EXPECT_FALSE(root["-2^63"].IsFloat());
  EXPECT_FALSE(root["-2^63"].IsDouble());
  EXPECT_TRUE(root["-2^63"].IsNumber());
  res_int64 = ASSERT_RESULT(root["-2^63"].GetInt64());
  EXPECT_EQ(res_int64, std::numeric_limits<int64_t>::min());

  EXPECT_FALSE(root["-2^31-1"].IsInt32());
  EXPECT_FALSE(root["-2^31-1"].IsUint32());
  EXPECT_TRUE(root["-2^31-1"].IsInt64());
  EXPECT_FALSE(root["-2^31-1"].IsUint64());
  EXPECT_FALSE(root["-2^31-1"].IsFloat());
  EXPECT_FALSE(root["-2^31-1"].IsDouble());
  EXPECT_TRUE(root["-2^31-1"].IsNumber());
  res_int64 = ASSERT_RESULT(root["-2^31-1"].GetInt64());
  EXPECT_EQ(res_int64, static_cast<int64_t>(std::numeric_limits<int32_t>::min()) - 1);

  EXPECT_TRUE(root["-2^31"].IsInt32());
  EXPECT_FALSE(root["-2^31"].IsUint32());
  EXPECT_TRUE(root["-2^31"].IsInt64());
  EXPECT_FALSE(root["-2^31"].IsUint64());
  EXPECT_FALSE(root["-2^31"].IsFloat());
  EXPECT_FALSE(root["-2^31"].IsDouble());
  EXPECT_TRUE(root["-2^31"].IsNumber());
  res_int32 = ASSERT_RESULT(root["-2^31"].GetInt32());
  EXPECT_EQ(res_int32, std::numeric_limits<int32_t>::min());

  EXPECT_TRUE(root["-1"].IsInt32());
  EXPECT_FALSE(root["-1"].IsUint32());
  EXPECT_TRUE(root["-1"].IsInt64());
  EXPECT_FALSE(root["-1"].IsUint64());
  EXPECT_FALSE(root["-1"].IsFloat());
  EXPECT_FALSE(root["-1"].IsDouble());
  EXPECT_TRUE(root["-1"].IsNumber());
  res_int32 = ASSERT_RESULT(root["-1"].GetInt32());
  EXPECT_EQ(res_int32, -1);

  EXPECT_TRUE(root["0"].IsInt32());
  EXPECT_TRUE(root["0"].IsUint32());
  EXPECT_TRUE(root["0"].IsInt64());
  EXPECT_TRUE(root["0"].IsUint64());
  EXPECT_FALSE(root["0"].IsFloat());
  EXPECT_FALSE(root["0"].IsDouble());
  EXPECT_OK(root["0"].GetFloat());
  EXPECT_OK(root["0"].GetDouble());
  EXPECT_TRUE(root["0"].IsNumber());
  res_int32 = ASSERT_RESULT(root["0"].GetInt32());
  EXPECT_EQ(res_int32, 0);

  EXPECT_FALSE(root["0.0"].IsInt32());
  EXPECT_FALSE(root["0.0"].IsUint32());
  EXPECT_FALSE(root["0.0"].IsInt64());
  EXPECT_FALSE(root["0.0"].IsUint64());
  EXPECT_TRUE(root["0.0"].IsFloat());
  EXPECT_TRUE(root["0.0"].IsDouble());
  EXPECT_TRUE(root["0.0"].IsNumber());
  EXPECT_TRUE(root["0.0"].IsNumber());
  res_float = ASSERT_RESULT(root["0.0"].GetFloat());
  EXPECT_EQ(res_float, 0.0);
  res_double = ASSERT_RESULT(root["0.0"].GetDouble());
  EXPECT_EQ(res_double, 0.0);

  EXPECT_TRUE(root["2^31-1"].IsInt32());
  EXPECT_TRUE(root["2^31-1"].IsUint32());
  EXPECT_TRUE(root["2^31-1"].IsInt64());
  EXPECT_TRUE(root["2^31-1"].IsUint64());
  EXPECT_FALSE(root["2^31-1"].IsFloat());
  EXPECT_FALSE(root["2^31-1"].IsDouble());
  EXPECT_TRUE(root["2^31-1"].IsNumber());
  res_int32 = ASSERT_RESULT(root["2^31-1"].GetInt32());
  EXPECT_EQ(res_int32, std::numeric_limits<int32_t>::max());

  EXPECT_FALSE(root["2^31"].IsInt32());
  EXPECT_TRUE(root["2^31"].IsUint32());
  EXPECT_TRUE(root["2^31"].IsInt64());
  EXPECT_TRUE(root["2^31"].IsUint64());
  EXPECT_FALSE(root["2^31"].IsFloat());
  EXPECT_FALSE(root["2^31"].IsDouble());
  EXPECT_TRUE(root["2^31"].IsNumber());
  res_uint32 = ASSERT_RESULT(root["2^31"].GetUint32());
  EXPECT_EQ(res_uint32, static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1);

  EXPECT_FALSE(root["2^32-1"].IsInt32());
  EXPECT_TRUE(root["2^32-1"].IsUint32());
  EXPECT_TRUE(root["2^32-1"].IsInt64());
  EXPECT_TRUE(root["2^32-1"].IsUint64());
  EXPECT_FALSE(root["2^32-1"].IsFloat());
  EXPECT_FALSE(root["2^32-1"].IsDouble());
  EXPECT_TRUE(root["2^32-1"].IsNumber());
  res_uint32 = ASSERT_RESULT(root["2^32-1"].GetUint32());
  EXPECT_EQ(res_uint32, std::numeric_limits<uint32_t>::max());

  EXPECT_FALSE(root["2^32"].IsInt32());
  EXPECT_FALSE(root["2^32"].IsUint32());
  EXPECT_TRUE(root["2^32"].IsInt64());
  EXPECT_TRUE(root["2^32"].IsUint64());
  EXPECT_FALSE(root["2^32"].IsFloat());
  EXPECT_FALSE(root["2^32"].IsDouble());
  EXPECT_TRUE(root["2^32"].IsNumber());
  res_int64 = ASSERT_RESULT(root["2^32"].GetInt64());
  EXPECT_EQ(res_int64, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);

  EXPECT_FALSE(root["2^64-1"].IsInt32());
  EXPECT_FALSE(root["2^64-1"].IsUint32());
  EXPECT_FALSE(root["2^64-1"].IsInt64());
  EXPECT_TRUE(root["2^64-1"].IsUint64());
  EXPECT_FALSE(root["2^64-1"].IsFloat());
  EXPECT_FALSE(root["2^64-1"].IsDouble());
  EXPECT_TRUE(root["2^64-1"].IsNumber());
  res_uint64 = ASSERT_RESULT(root["2^64-1"].GetUint64());
  EXPECT_EQ(res_uint64, std::numeric_limits<uint64_t>::max());

  EXPECT_FALSE(root["2^64"].IsInt32());
  EXPECT_FALSE(root["2^64"].IsUint32());
  EXPECT_FALSE(root["2^64"].IsInt64());
  EXPECT_FALSE(root["2^64"].IsUint64());
  EXPECT_TRUE(root["2^64"].IsFloat());
  EXPECT_TRUE(root["2^64"].IsDouble());
  EXPECT_TRUE(root["2^64"].IsNumber());
  EXPECT_OK(root["2^64"].GetFloat());
  EXPECT_OK(root["2^64"].GetDouble());
  res_float = ASSERT_RESULT(root["2^64"].GetFloat());
  EXPECT_EQ(res_float, static_cast<float>(std::numeric_limits<uint64_t>::max()) + 1);
  res_double = ASSERT_RESULT(root["2^64"].GetDouble());
  EXPECT_EQ(res_double, static_cast<double>(std::numeric_limits<uint64_t>::max()) + 1);

  EXPECT_FALSE(root["+large"].IsInt32());
  EXPECT_FALSE(root["+large"].IsUint32());
  EXPECT_FALSE(root["+large"].IsInt64());
  EXPECT_FALSE(root["+large"].IsUint64());
  EXPECT_FALSE(root["+large"].IsFloat());
  EXPECT_TRUE(root["+large"].IsDouble());
  EXPECT_TRUE(root["+large"].IsNumber());
  res_double = ASSERT_RESULT(root["+large"].GetDouble());
  EXPECT_EQ(res_double, 1.0e+39);
}

TEST_F(JsonDocumentTest, MissingMember) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"a": {"b": 2}})"));
  // missing member -> NotFound
  auto res = root["a"]["nope"].GetInt32();
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsNotFound());
}

TEST_F(JsonDocumentTest, MissingIndex) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"arr": [1]})"));

  // Out-of-range index via operator[] should produce an invalid JsonValue.
  JsonValue out = root["arr"][5];
  ASSERT_FALSE(out.IsValid());
  ASSERT_TRUE(out.Path().find("arr") != std::string::npos);
  auto res = out.GetInt32();
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsNotFound());
}

TEST_F(JsonDocumentTest, IterateArrayElements) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"items": ["a", "b", "c"], "empty": []})"));

  // Iterate items array and collect strings.
  std::vector<std::string> vals;
  for (const auto& el : ASSERT_RESULT(root["items"].GetArray())) {
    auto s = ASSERT_RESULT(el.GetString());
    vals.push_back(s);
  }
  ASSERT_EQ(vals, (decltype(vals){"a", "b", "c"}));
}

TEST_F(JsonDocumentTest, IterateObjectMembers) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"map": {"k1": "v1", "k2": "v2", "k3": "v3"}})"));

  // Iterate map object and collect key/value pairs.
  std::vector<std::pair<std::string_view, std::string>> pairs;
  for (const auto& kv : ASSERT_RESULT(root["map"].GetObject())) {
    auto key = kv.first;
    auto val = ASSERT_RESULT(kv.second.GetString());
    pairs.emplace_back(key, val);
  }

  // Because JSON object member order is preserved by rapidjson but not relied upon, sort before
  // comparing to keep the test robust.
  std::sort(pairs.begin(), pairs.end());

  std::vector<std::pair<std::string_view, std::string>> expected{
      {"k1", "v1"}, {"k2", "v2"}, {"k3", "v3"}};
  std::sort(expected.begin(), expected.end());

  ASSERT_EQ(pairs, expected);
}

TEST_F(JsonDocumentTest, IterateEmptyArray) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"empty": []})"));

  for ([[maybe_unused]] const auto& el : ASSERT_RESULT(root["empty"].GetArray())) {
    FAIL();
  }
}

TEST_F(JsonDocumentTest, IterateEmptyObject) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"empty": {}})"));

  for ([[maybe_unused]] const auto& kv : ASSERT_RESULT(root["empty"].GetObject())) {
    FAIL();
  }
}

TEST_F(JsonDocumentTest, ArrayIndexOperator) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"items": [10, 20, 30]})"));

  auto arr = ASSERT_RESULT(root["items"].GetArray());
  // Index directly from JsonArray
  auto val0 = arr[0];
  ASSERT_TRUE(val0.IsValid());
  ASSERT_EQ(ASSERT_RESULT(val0.GetInt32()), 10);
  auto val2 = arr[2];
  ASSERT_TRUE(val2.IsValid());
  ASSERT_EQ(ASSERT_RESULT(val2.GetInt32()), 30);

  // Out of range yields invalid
  ASSERT_FALSE(arr[-1].IsValid());
  ASSERT_FALSE(arr[10].IsValid());
}

TEST_F(JsonDocumentTest, ObjectIndexOperator) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"obj": {"a": 1, "b": 2}})"));

  auto obj = ASSERT_RESULT(root["obj"].GetObject());
  auto a = obj["a"];
  ASSERT_TRUE(a.IsValid());
  ASSERT_EQ(ASSERT_RESULT(a.GetInt32()), 1);

  auto missing = obj["missing"];
  ASSERT_FALSE(missing.IsValid());
}

TEST_F(JsonDocumentTest, FindInArrayWithStdFindIf) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({
    "flags": [
      {"name": "a", "val": 1},
      {"name": "v", "val": 2},
      {"name": "c", "val": 3}
    ]
  })"));

  auto entries = ASSERT_RESULT(root["flags"].GetArray());
  auto it = std::find_if(entries.begin(), entries.end(),
    [](const JsonValue& value) {
      return EXPECT_RESULT(value["name"].GetString()) == "v";
    });

  ASSERT_NE(it, entries.end());
  // Dereference iterator to inspect found element.
  auto found = *it;
  ASSERT_TRUE(found.IsObject());
  ASSERT_EQ(ASSERT_RESULT(found["name"].GetString()), "v");
  ASSERT_EQ(ASSERT_RESULT(found["val"].GetInt32()), 2);
}

TEST_F(JsonDocumentTest, FindInObjectWithStdFindIf) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({
    "map": { "x": {"v":1}, "target": {"v":2}, "y": {"v":3} }
  })"));

  auto obj = ASSERT_RESULT(root["map"].GetObject());
  // find member with key == "target"
  auto it = std::find_if(obj.begin(), obj.end(),
    [](const std::pair<std::string_view, JsonValue>& kv) {
      return kv.first == "target";
    });

  ASSERT_NE(it, obj.end());
  auto kv = *it;
  ASSERT_EQ(kv.first, "target");
  ASSERT_EQ(ASSERT_RESULT(kv.second["v"].GetInt32()), 2);
}

TEST_F(JsonDocumentTest, ElementPathsAreCorrect) {
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(R"({"arr": [{"name":"one"}, {"name":"two"}]})"));

  size_t idx = 0;
  for (auto el : ASSERT_RESULT(root["arr"].GetArray())) {
    ASSERT_EQ(el.Path(), Format(".arr[$0]", idx));
    ASSERT_EQ(el["name"].Path(), Format(".arr[$0].name", idx));
    auto name = ASSERT_RESULT(el["name"].GetString());
    if (idx == 0) {
      ASSERT_EQ(name, "one");
    }
    if (idx == 1) {
      ASSERT_EQ(name, "two");
    }
    ++idx;
  }
  ASSERT_EQ(idx, 2u);
}

} // namespace yb
