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

#include <gtest/gtest.h>

#include "yb/dockv/subdocument.h"
#include "yb/dockv/value_type.h"

#include "yb/util/monotime.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"

using std::string;

namespace yb::dockv {

TEST(SubDocumentTest, TestGetOrAddChild) {
  SubDocument d;
  ASSERT_TRUE(d.GetOrAddChild(KeyEntryValue("foo")).second);
  ASSERT_FALSE(d.GetOrAddChild(KeyEntryValue("foo")).second);  // No new subdocument created.
  ASSERT_TRUE(d.GetOrAddChild(KeyEntryValue("bar")).second);
  ASSERT_TRUE(d.GetOrAddChild(KeyEntryValue::Int32(100)).second);
  ASSERT_TRUE(d.GetOrAddChild(KeyEntryValue::Int32(200)).second);
  ASSERT_TRUE(d.GetOrAddChild(KeyEntryValue(string("\x00", 1))).second);
  ASSERT_FALSE(d.GetOrAddChild(KeyEntryValue(string("\x00", 1))).second);  // No new subdoc added.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  100: {},
  200: {},
  "\x00": {},
  "bar": {},
  "foo": {}
}
)#", d.ToString());
}

TEST(SubDocumentTest, TestToString) {
  SubDocument subdoc(ValueEntryType::kObject);
  SubDocument mathematicians;
  SubDocument cs;
  mathematicians.SetChildPrimitive(KeyEntryValue("Isaac Newton"), PrimitiveValue::Int32(1643));
  ASSERT_EQ(1, mathematicians.object_num_keys());
  mathematicians.SetChildPrimitive(KeyEntryValue("Pythagoras"), PrimitiveValue::Int32(-570));
  ASSERT_EQ(2, mathematicians.object_num_keys());
  mathematicians.SetChildPrimitive(KeyEntryValue("Leonard Euler"), PrimitiveValue::Int32(1601));
  ASSERT_EQ(3, mathematicians.object_num_keys());
  mathematicians.SetChildPrimitive(KeyEntryValue("Blaise Pascal"), PrimitiveValue::Int32(1623));
  ASSERT_EQ(4, mathematicians.object_num_keys());
  mathematicians.SetChildPrimitive(
      KeyEntryValue("Srinivasa Ramanujan"), PrimitiveValue::Int32(1887));
  ASSERT_EQ(5, mathematicians.object_num_keys());
  mathematicians.SetChildPrimitive(KeyEntryValue("Euclid"), PrimitiveValue("Mid-4th century BCE"));
  ASSERT_EQ(6, mathematicians.object_num_keys());

  cs.SetChildPrimitive(KeyEntryValue("Alan Turing"), PrimitiveValue::Int32(1912));
  ASSERT_EQ(1, cs.object_num_keys());
  cs.SetChildPrimitive(KeyEntryValue("Ada Lovelace"), PrimitiveValue::Int32(1815));
  ASSERT_EQ(2, cs.object_num_keys());
  cs.SetChildPrimitive(KeyEntryValue("Edsger W. Dijkstra"), PrimitiveValue::Int32(1930));
  ASSERT_EQ(3, cs.object_num_keys());
  cs.SetChildPrimitive(KeyEntryValue("John von Neumann"), PrimitiveValue::Int32(1903));
  ASSERT_EQ(4, cs.object_num_keys());
  cs.SetChildPrimitive(KeyEntryValue("Dennis Ritchie"), PrimitiveValue::Int32(1941));
  ASSERT_EQ(5, cs.object_num_keys());

  subdoc.SetChild(KeyEntryValue("Mathematicians"), std::move(mathematicians));
  ASSERT_EQ(1, subdoc.object_num_keys());
  subdoc.SetChild(KeyEntryValue("Computer Scientists"), std::move(cs));
  ASSERT_EQ(2, subdoc.object_num_keys());

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
{
  "Computer Scientists": {
    "Ada Lovelace": 1815,
    "Alan Turing": 1912,
    "Dennis Ritchie": 1941,
    "Edsger W. Dijkstra": 1930,
    "John von Neumann": 1903
  },
  "Mathematicians": {
    "Blaise Pascal": 1623,
    "Euclid": "Mid-4th century BCE",
    "Isaac Newton": 1643,
    "Leonard Euler": 1601,
    "Pythagoras": -570,
    "Srinivasa Ramanujan": 1887
  }
}
      )#", subdoc.ToString());

  ASSERT_TRUE(subdoc.DeleteChild(KeyEntryValue("Mathematicians")));
  ASSERT_EQ(1, subdoc.object_num_keys());
  ASSERT_TRUE(subdoc.DeleteChild(KeyEntryValue("Computer Scientists")));
  ASSERT_EQ(0, subdoc.object_num_keys());
}

TEST(SubDocumentTest, InitializerListConstructor) {
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
{
  "France": "Paris",
  "Germany": "Berlin"
}
      )#", SubDocument({{"France", "Paris"}, {"Germany", "Berlin"}}).ToString());

  SubDocument d2({{"France", "Paris"}, {"Germany", "Berlin"}});
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
{
  1: 1,
  2: 4,
  3: 9,
  4: 16,
  5: 25,
  10: 100
}
      )#", SubDocument({{10, 100}, {1, 1}, {2, 4}, {3, 9}, {4, 16}, {5, 25}}).ToString());
}

TEST(SubDocumentTest, Equality) {
  ASSERT_EQ(SubDocument({{1, 2}, {3, 4}}), SubDocument({{1, 2}, {3, 4}}));
  ASSERT_EQ(SubDocument({{1, 2}, {3, 4}}), SubDocument({{3, 4}, {1, 2}}));
  ASSERT_NE(SubDocument({{1, 2}, {3, 4}}), SubDocument({{1, 2}, {3, 5}}));
  ASSERT_NE(SubDocument({{1, 2}, {3, 4}}), SubDocument({{1, 2}, {5, 4}}));
}

TEST(SubDocumentTest, TestCopyMove) {
  // Try Copies.
  SubDocument s1(ValueEntryType::kObject);
  s1.SetTtl(1000);
  s1.SetWriteTime(1000);
  SubDocument s2 = s1;
  ASSERT_EQ(s1, s2);
  ASSERT_EQ(s1.GetTtl(), s2.GetTtl());
  ASSERT_EQ(s1.GetWriteTime(), s2.GetWriteTime());

  SubDocument s3;
  s3 = s1;
  ASSERT_EQ(s1, s3);
  ASSERT_EQ(s1.GetTtl(), s3.GetTtl());
  ASSERT_EQ(s1.GetWriteTime(), s3.GetWriteTime());

  // Try Moves.
  SubDocument s4 = std::move(s1);
  ASSERT_EQ(s3, s4);
  ASSERT_EQ(s3.GetTtl(), s4.GetTtl());
  ASSERT_EQ(s3.GetWriteTime(), s4.GetWriteTime());
  ASSERT_EQ(ValueEntryType::kNullLow, s1.value_type());

  SubDocument s5;
  s5 = std::move(s2);
  ASSERT_EQ(s3, s5);
  ASSERT_EQ(s3.GetTtl(), s5.GetTtl());
  ASSERT_EQ(s3.GetWriteTime(), s5.GetWriteTime());
  ASSERT_EQ(ValueEntryType::kNullLow, s2.value_type());
}

}  // namespace yb::dockv
