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

#include "yb/common/ql_type.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb-test.h"

#include "yb/util/yb_partition.h"

namespace yb {
namespace docdb {

INSTANTIATE_TEST_CASE_P(DocDBTests,
                        DocDBTestWrapper,
                        testing::Values(TestDocDb::kQlReader, TestDocDb::kRedisReader));

TEST_P(DocDBTestWrapper, DocPathTest) {
  auto doc_key = MakeDocKey("mydockey", 10, "mydockey", 20);
  DocPath doc_path(doc_key.Encode(), KeyEntryValue("first_subkey"), KeyEntryValue::Int64(123));
  ASSERT_EQ(2, doc_path.num_subkeys());
  ASSERT_EQ("\"first_subkey\"", doc_path.subkey(0).ToString());
  ASSERT_EQ("123", doc_path.subkey(1).ToString());
}

TEST_P(DocDBTestWrapper, KeyAsEmptyObjectIsNotMasked) {
  const auto doc_key = MakeDocKey(DocKeyHash(1234));
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 252_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue(KeyEntryType::kNullLow)),
      ValueRef(ValueEntryType::kObject), 617_usec_ht));
  ASSERT_OK(DeleteSubDoc(
      DocPath(encoded_doc_key, KeyEntryValue(KeyEntryType::kNullLow),
              KeyEntryValue(KeyEntryType::kFalse)), 675_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue(KeyEntryType::kNullLow),
              KeyEntryValue(KeyEntryType::kFalse)),
      QLValue::Primitive(12345), 617_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("later")), QLValue::Primitive(1), 336_usec_ht));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], [1234]), [HT{ physical: 252 }]) -> {}
      SubDocKey(DocKey([], [1234]), [null; HT{ physical: 617 }]) -> {}
      SubDocKey(DocKey([], [1234]), [null, false; HT{ physical: 675 }]) -> DEL
      SubDocKey(DocKey([], [1234]), [null, false; HT{ physical: 617 }]) -> 12345
      SubDocKey(DocKey([], [1234]), ["later"; HT{ physical: 336 }]) -> 1
      )#");
  VerifyDocument(doc_key, 4000_usec_ht,
                    R"#(
{
  null: {},
  "later": 1
}
      )#");
}

TEST_P(DocDBTestWrapper, NullChildObjectShouldMaskValues) {
  const auto doc_key = MakeDocKey("mydockey", kIntKey1);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("obj")),
      ValueRef(ValueEntryType::kObject), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("obj"), KeyEntryValue("key")),
      QLValue::Primitive("value"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("obj")),
      ValueRef(ValueEntryType::kNullLow), 3000_usec_ht));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["obj"; HT{ physical: 3000 }]) -> null
      SubDocKey(DocKey([], ["mydockey", 123456]), ["obj"; HT{ physical: 2000 }]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["obj", "key"; HT{ physical: 2000 }]) -> "value"
      )#");
  VerifyDocument(doc_key, 4000_usec_ht,
                    R"#(
{
  "obj": null
}
      )#");
}

TEST_P(DocDBTestWrapper, HistoryCompactionFirstRowHandlingRegression) {
  // A regression test for a bug in an initial version of compaction cleanup.
  const auto doc_key = MakeDocKey("mydockey", kIntKey1);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"),
      1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"),
      2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"),
      3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 4000_usec_ht));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 4000 }]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 3000 }]) -> "value3"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 2000 }]) -> "value2"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 1000 }]) -> "value1"
      )#");
  FullyCompactHistoryBefore(3500_usec_ht);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 3000 }]) -> "value3"
      )#");
}

TEST_P(DocDBTestWrapper, SetPrimitiveQL) {
  const auto doc_key = MakeDocKey("mydockey", kIntKey1);
  SetupRocksDBState(doc_key.Encode());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "1"; HT{ physical: 4000 }]) -> "3"
SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "1"; HT{ physical: 1000 w: 2 }]) -> "1"
SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "2"; HT{ physical: 2000 }]) -> 11
SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "2"; HT{ physical: 1000 w: 3 }]) -> "2"
SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "3"; HT{ physical: 4000 w: 1 }]) -> "4"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b"; HT{ physical: 3000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "c", "1"; HT{ physical: 1000 w: 4 }]) -> "3"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "d", "1"; HT{ physical: 1000 w: 5 }]) -> "5"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "d", "2"; HT{ physical: 1000 w: 6 }]) -> "6"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "1"; HT{ physical: 3000 w: 2 }]) -> "8"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "2"; HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "2"; HT{ physical: 3000 w: 3 }]) -> "9"
SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "y"; HT{ physical: 3000 w: 1 }]) -> "10"
SubDocKey(DocKey([], ["mydockey", 123456]), ["u"; HT{ physical: 1000 w: 1 }]) -> "7"
     )#");
}

TEST_P(DocDBTestWrapper, ListInsertAndGetTest) {
  QLValuePB parent;
  QLValuePB list = QLValue::PrimitiveArray(10, 2);
  auto doc_key = MakeDocKey("list_test", 231);
  KeyBytes encoded_doc_key = doc_key.Encode();
  AddMapValue("list2", list, &parent);
  AddMapValue("other", "other_value", &parent);
  ASSERT_OK(InsertSubDocument(DocPath(encoded_doc_key), ValueRef(parent), HybridTime(100)));

  VerifyDocument(doc_key, HybridTime(250),
      R"#(
  {
    "list2": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "other": "other_value"
  }
        )#");

  QLValuePB list1;
  ASSERT_OK(ExtendSubDocument(
      DocPath(encoded_doc_key, KeyEntryValue("list1")),
      ValueRef(QLValue::PrimitiveArray(1, "3", 2, 2)),
      HybridTime(200)));

  VerifyDocument(doc_key, HybridTime(250),
      R"#(
  {
    "list1": {
      ArrayIndex(3): 1,
      ArrayIndex(4): "3",
      ArrayIndex(5): 2,
      ArrayIndex(6): 2
    },
    "list2": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "other": "other_value"
  }
        )#");

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); \
    HT{ physical: 0 logical: 100 w: 1 }]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 100 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["other"; \
    HT{ physical: 0 logical: 100 w: 3 }]) -> "other_value"
        )#");
  ASSERT_OK(ExtendList(
      DocPath(encoded_doc_key, KeyEntryValue("list2")),
      ValueRef(QLValue::PrimitiveArray(5, 2), dockv::ListExtendOrder::PREPEND_BLOCK),
      HybridTime(300)));
  ASSERT_OK(ExtendList(DocPath(encoded_doc_key, KeyEntryValue("list2")),
      ValueRef(QLValue::PrimitiveArray(7, 4), dockv::ListExtendOrder::APPEND),
      HybridTime(400)));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); \
    HT{ physical: 0 logical: 300 w: 1 }]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); \
    HT{ physical: 0 logical: 300 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); \
    HT{ physical: 0 logical: 100 w: 1 }]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 100 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); \
    HT{ physical: 0 logical: 400 }]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); \
    HT{ physical: 0 logical: 400 w: 1 }]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["other"; \
    HT{ physical: 0 logical: 100 w: 3 }]) -> "other_value"
        )#");

  VerifyDocument(doc_key, HybridTime(150),
      R"#(
  {
    "list2": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "other": "other_value"
  }
        )#");

  VerifyDocument(doc_key, HybridTime(450),
      R"#(
  {
    "list1": {
      ArrayIndex(3): 1,
      ArrayIndex(4): "3",
      ArrayIndex(5): 2,
      ArrayIndex(6): 2
    },
    "list2": {
      ArrayIndex(-8): 5,
      ArrayIndex(-7): 2,
      ArrayIndex(1): 10,
      ArrayIndex(2): 2,
      ArrayIndex(9): 7,
      ArrayIndex(10): 4
    },
    "other": "other_value"
  }
        )#");

  ReadHybridTime read_ht;
  read_ht.read = HybridTime(460);
  ASSERT_OK(ReplaceInList(
      DocPath(encoded_doc_key, KeyEntryValue("list2")), 1, ValueRef(ValueEntryType::kTombstone),
      read_ht, HybridTime(500), rocksdb::kDefaultQueryId));
  ASSERT_OK(ReplaceInList(
      DocPath(encoded_doc_key, KeyEntryValue("list2")), 2, ValueRef(QLValue::Primitive(17)),
      read_ht, HybridTime(500), rocksdb::kDefaultQueryId));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); \
    HT{ physical: 0 logical: 300 w: 1 }]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); \
    HT{ physical: 0 logical: 500 }]) -> DEL
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); \
    HT{ physical: 0 logical: 300 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); \
    HT{ physical: 0 logical: 100 w: 1 }]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 500 }]) -> 17
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 100 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); \
    HT{ physical: 0 logical: 400 }]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); \
    HT{ physical: 0 logical: 400 w: 1 }]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["other"; \
    HT{ physical: 0 logical: 100 w: 3 }]) -> "other_value"
        )#");

  VerifyDocument(doc_key, HybridTime(550),
      R"#(
  {
    "list1": {
      ArrayIndex(3): 1,
      ArrayIndex(4): "3",
      ArrayIndex(5): 2,
      ArrayIndex(6): 2
    },
    "list2": {
      ArrayIndex(-8): 5,
      ArrayIndex(1): 10,
      ArrayIndex(2): 17,
      ArrayIndex(9): 7,
      ArrayIndex(10): 4
    },
    "other": "other_value"
  }
        )#");

  SubDocKey sub_doc_key(doc_key, KeyEntryValue("list3"));
  KeyBytes encoded_sub_doc_key = sub_doc_key.Encode();
  auto list3 = QLValue::PrimitiveArray(31, 32);

  ASSERT_OK(InsertSubDocument(DocPath(encoded_sub_doc_key), ValueRef(list3), HybridTime(100)));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); \
    HT{ physical: 0 logical: 300 w: 1 }]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); \
    HT{ physical: 0 logical: 500 }]) -> DEL
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); \
    HT{ physical: 0 logical: 300 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); \
    HT{ physical: 0 logical: 100 w: 1 }]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 500 }]) -> 17
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); \
    HT{ physical: 0 logical: 100 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); \
    HT{ physical: 0 logical: 400 }]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); \
    HT{ physical: 0 logical: 400 w: 1 }]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["list3"; HT{ physical: 0 logical: 100 }]) -> []
SubDocKey(DocKey([], ["list_test", 231]), ["list3", ArrayIndex(11); \
    HT{ physical: 0 logical: 100 w: 1 }]) -> 31
SubDocKey(DocKey([], ["list_test", 231]), ["list3", ArrayIndex(12); \
    HT{ physical: 0 logical: 100 w: 2 }]) -> 32
SubDocKey(DocKey([], ["list_test", 231]), ["other"; \
    HT{ physical: 0 logical: 100 w: 3 }]) -> "other_value"
        )#");

  VerifyDocument(doc_key, HybridTime(550),
      R"#(
  {
    "list1": {
      ArrayIndex(3): 1,
      ArrayIndex(4): "3",
      ArrayIndex(5): 2,
      ArrayIndex(6): 2
    },
    "list2": {
      ArrayIndex(-8): 5,
      ArrayIndex(1): 10,
      ArrayIndex(2): 17,
      ArrayIndex(9): 7,
      ArrayIndex(10): 4
    },
    "list3": {
      ArrayIndex(11): 31,
      ArrayIndex(12): 32
    },
    "other": "other_value"
  }
        )#");
}

TEST_P(DocDBTestWrapper, ListOverwriteAndInsertTest) {
  auto doc_key = MakeDocKey("list_test", 231);
  KeyBytes encoded_doc_key = doc_key.Encode();
  ASSERT_OK(InsertSubDocument(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), HybridTime(100)));

  auto write_list = [&](const std::vector<int>& children, const int logical_time) {
    QLValuePB list;
    int64_t idx = 1;
    for (const auto& child : children) {
      AddMapValue(idx++, child, &list);
    }
    ValueRef value_ref(list);
    value_ref.set_write_instruction(bfql::TSOpcode::kListAppend);
    ASSERT_OK(InsertSubDocument(
        DocPath(encoded_doc_key, KeyEntryValue("list")), value_ref, HybridTime(logical_time)));
  };

  write_list({1, 2, 3, 4, 5}, 200);
  write_list({6, 7, 8}, 300);

  VerifyDocument(doc_key, HybridTime(350),
      R"#(
  {
    "list": {
      ArrayIndex(1): 6,
      ArrayIndex(2): 7,
      ArrayIndex(3): 8
    }
  }
        )#");

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list"; HT{ physical: 0 logical: 300 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list"; HT{ physical: 0 logical: 200 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(1); \
    HT{ physical: 0 logical: 300 w: 1 }]) -> 6
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(1); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(2); \
    HT{ physical: 0 logical: 300 w: 2 }]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(2); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(3); \
    HT{ physical: 0 logical: 300 w: 3 }]) -> 8
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 3
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 4 }]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 5 }]) -> 5
        )#");

  // Replacing cql index 1 with 17 should work as expected, ignoring overwritten DocDB entries
  ASSERT_OK(ReplaceInList(
      DocPath(encoded_doc_key, KeyEntryValue("list")), 1, ValueRef(QLValue::Primitive(17)),
      ReadHybridTime::SingleTime(HybridTime(400)), HybridTime(500), rocksdb::kDefaultQueryId));
  // Replacing cql index 3 should fail, rather than overwrite an old overwritten index
  ASSERT_NOK(ReplaceInList(
      DocPath(encoded_doc_key, KeyEntryValue("list")), 3, ValueRef(QLValue::Primitive(17)),
      ReadHybridTime::SingleTime(HybridTime(400)), HybridTime(500), rocksdb::kDefaultQueryId));
  VerifyDocument(doc_key, HybridTime(500),
      R"#(
  {
    "list": {
      ArrayIndex(1): 6,
      ArrayIndex(2): 17,
      ArrayIndex(3): 8
    }
  }
        )#");
}

TEST_P(DocDBTestWrapper, ListInsertAndDeleteTest) {
  auto doc_key = MakeDocKey("list_test", 231);
  KeyBytes encoded_doc_key = doc_key.Encode();
  ASSERT_OK(InsertSubDocument(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), HybridTime(100)));

  auto write_list = [&](const std::vector<int>& children, const int logical_time) {
    QLValuePB list;
    int64_t idx = 1;
    for (const auto& child : children) {
      AddMapValue(idx++, child, &list);
    }
    ValueRef value_ref(list);
    value_ref.set_write_instruction(bfql::TSOpcode::kListAppend);
    ASSERT_OK(InsertSubDocument(
        DocPath(encoded_doc_key, KeyEntryValue("list")), value_ref, HybridTime(logical_time)));
  };

  write_list({1, 2, 3, 4, 5}, 200);
  write_list({6, 7, 8}, 300);

  VerifyDocument(
      doc_key, HybridTime(350),
      R"#(
  {
    "list": {
      ArrayIndex(1): 6,
      ArrayIndex(2): 7,
      ArrayIndex(3): 8
    }
  }
        )#");

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list"; HT{ physical: 0 logical: 300 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list"; HT{ physical: 0 logical: 200 }]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(1); \
    HT{ physical: 0 logical: 300 w: 1 }]) -> 6
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(1); \
    HT{ physical: 0 logical: 200 w: 1 }]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(2); \
    HT{ physical: 0 logical: 300 w: 2 }]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(2); \
    HT{ physical: 0 logical: 200 w: 2 }]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(3); \
    HT{ physical: 0 logical: 300 w: 3 }]) -> 8
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(3); \
    HT{ physical: 0 logical: 200 w: 3 }]) -> 3
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(4); \
    HT{ physical: 0 logical: 200 w: 4 }]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["list", ArrayIndex(5); \
    HT{ physical: 0 logical: 200 w: 5 }]) -> 5
        )#");

  // Replacing cql index 1 with tombstone should work as expected
  ASSERT_OK(ReplaceInList(
      DocPath(encoded_doc_key, KeyEntryValue("list")), 1, ValueRef(ValueEntryType::kTombstone),
      ReadHybridTime::SingleTime(HybridTime(400)), HybridTime(500), rocksdb::kDefaultQueryId));
  VerifyDocument(
      doc_key, HybridTime(500),
      R"#(
  {
    "list": {
      ArrayIndex(1): 6,
      ArrayIndex(3): 8
    }
  }
        )#");
  VerifyDocument(
      doc_key, HybridTime(450),
      R"#(
  {
    "list": {
      ArrayIndex(1): 6,
      ArrayIndex(2): 7,
      ArrayIndex(3): 8
    }
  }
        )#");
}

TEST_P(DocDBTestWrapper, MapInsertAndDeleteTest) {
  auto doc_key = MakeDocKey("map_test", 231);
  KeyBytes encoded_doc_key = doc_key.Encode();
  ASSERT_OK(InsertSubDocument(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), HybridTime(100)));

  auto write_map = [&](const std::map<std::string, std::string>& values, const int logical_time) {
    QLValuePB map_value;
    for (const auto& child : values) {
      AddMapValue(child.first, child.second, &map_value);
    }
    ValueRef value_ref(map_value);
    value_ref.set_write_instruction(bfql::TSOpcode::kMapExtend);
    ASSERT_OK(InsertSubDocument(DocPath(encoded_doc_key), value_ref, HybridTime(logical_time)));
  };

  write_map({{"mk1", "mv1"}, {"mk2", "mv2"}, {"mk3", "mv3"}}, 200);
  write_map({{"mk3", "mv3_updated"}, {"mk4", "mv4"}, {"mk5", "mv5"}, {"mk6", "mv6"}}, 300);

  VerifyDocument(
      doc_key, HybridTime(350),
      R"#(
  {
    "mk3": "mv3_updated",
    "mk4": "mv4",
    "mk5": "mv5",
    "mk6": "mv6"
  }
        )#");
  VerifyDocument(
      doc_key, HybridTime(250),
      R"#(
  {
    "mk1": "mv1",
    "mk2": "mv2",
    "mk3": "mv3"
  }
        )#");

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(
      R"#(
          SubDocKey($0, [HT{ physical: 0 logical: 300 }]) -> {}
          SubDocKey($0, [HT{ physical: 0 logical: 200 }]) -> {}
          SubDocKey($0, [HT{ physical: 0 logical: 100 }]) -> {}
          SubDocKey($0, ["mk1"; HT{ physical: 0 logical: 200 w: 1 }]) -> "mv1"
          SubDocKey($0, ["mk2"; HT{ physical: 0 logical: 200 w: 2 }]) -> "mv2"
          SubDocKey($0, ["mk3"; HT{ physical: 0 logical: 300 w: 1 }]) -> "mv3_updated"
          SubDocKey($0, ["mk3"; HT{ physical: 0 logical: 200 w: 3 }]) -> "mv3"
          SubDocKey($0, ["mk4"; HT{ physical: 0 logical: 300 w: 2 }]) -> "mv4"
          SubDocKey($0, ["mk5"; HT{ physical: 0 logical: 300 w: 3 }]) -> "mv5"
          SubDocKey($0, ["mk6"; HT{ physical: 0 logical: 300 w: 4 }]) -> "mv6"
          )#",
      doc_key.ToString()));

  // Deleting key mk5 and mk3 should work as expected.
  ASSERT_OK(DeleteSubDoc(DocPath(encoded_doc_key, KeyEntryValue("mk5")), HybridTime(450)));
  ASSERT_OK(DeleteSubDoc(DocPath(encoded_doc_key, KeyEntryValue("mk3")), HybridTime(500)));

  VerifyDocument(
      doc_key, HybridTime(500),
      R"#(
  {
    "mk4": "mv4",
    "mk6": "mv6"
  }
        )#");
  VerifyDocument(
      doc_key, HybridTime(450),
      R"#(
  {
    "mk3": "mv3_updated",
    "mk4": "mv4",
    "mk6": "mv6"
  }
        )#");
}

TEST_P(DocDBTestWrapper, GetDocTwoLists) {
  auto list1 = QLValue::PrimitiveArray(10, 2);
  auto doc_key = MakeDocKey("foo", 231);

  KeyBytes encoded_doc_key = doc_key.Encode();
  QLValuePB parent;
  AddMapValue("key1", list1, &parent);
  ASSERT_OK(InsertSubDocument(DocPath(encoded_doc_key), ValueRef(parent), HybridTime(100)));

  SubDocKey sub_doc_key(doc_key, KeyEntryValue("key2"));
  KeyBytes encoded_sub_doc_key = sub_doc_key.Encode();
  auto list2 = QLValue::PrimitiveArray(31, 32);

  ASSERT_OK(InsertSubDocument(DocPath(encoded_sub_doc_key), ValueRef(list2), HybridTime(100)));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["foo", 231]), [HT{ physical: 0 logical: 100 }]) -> {}
SubDocKey(DocKey([], ["foo", 231]), ["key1", ArrayIndex(1); HT{ physical: 0 logical: 100 w: 1 }]) \
-> 10
SubDocKey(DocKey([], ["foo", 231]), ["key1", ArrayIndex(2); HT{ physical: 0 logical: 100 w: 2 }]) \
-> 2
SubDocKey(DocKey([], ["foo", 231]), ["key2"; HT{ physical: 0 logical: 100 }]) -> []
SubDocKey(DocKey([], ["foo", 231]), ["key2", ArrayIndex(3); HT{ physical: 0 logical: 100 w: 1 }]) \
-> 31
SubDocKey(DocKey([], ["foo", 231]), ["key2", ArrayIndex(4); HT{ physical: 0 logical: 100 w: 2 }]) \
-> 32
      )#");

  VerifyDocument(doc_key, HybridTime(550),
      R"#(
  {
    "key1": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "key2": {
      ArrayIndex(3): 31,
      ArrayIndex(4): 32
    }
  }
      )#");
}

TEST_P(DocDBTestWrapper, MinorCompactionNoDeletions) {
  ASSERT_OK(DisableCompactions());
  const auto doc_key = MakeDocKey("k");
  KeyBytes encoded_doc_key(doc_key.Encode());
  for (int i = 1; i <= 6; ++i) {
    auto value_str = Format("v$0", i);
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key), ValueRef(QLValue::Primitive(value_str)),
        HybridTime::FromMicros(i * 1000)));
    ASSERT_OK(FlushRocksDbAndWait());
  }

  ASSERT_EQ(6, NumSSTableFiles());
  const char* kInitialDocDbStateStr = R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 6
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> "v5"  // file 5
SubDocKey(DocKey([], ["k"]), [HT{ physical: 4000 }]) -> "v4"  // file 4
SubDocKey(DocKey([], ["k"]), [HT{ physical: 3000 }]) -> "v3"  // file 3
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#";

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(kInitialDocDbStateStr);
  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);

  ASSERT_EQ(5, NumSSTableFiles());
  // No changes in DocDB rows as we still need the entry at 5000_ms_ht.
  // Let's call the output file resulting from the last compaction "file 7".
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(kInitialDocDbStateStr);

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(4, NumSSTableFiles());
  // Removed the entry at 4000_ms_ht as it was overwritten at time 5000. Earlier entries are in
  // other files that haven't been compacted yet.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 8
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> "v5"  // file 8
SubDocKey(DocKey([], ["k"]), [HT{ physical: 3000 }]) -> "v3"  // file 3
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(3, NumSSTableFiles());
  // Removed the entry at 3000_ms_ht.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 9
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> "v5"  // file 9
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(2, NumSSTableFiles());
  // Removed the entry at 2000_ms_ht.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 10
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> "v5"  // file 10
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(1, NumSSTableFiles());
  // Removed the entry at 2000_ms_ht.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 11
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> "v5"  // file 11
      )#");
}

TEST_P(DocDBTestWrapper, MinorCompactionWithDeletions) {
  ASSERT_OK(DisableCompactions());
  const auto doc_key = MakeDocKey("k");
  KeyBytes encoded_doc_key(doc_key.Encode());
  for (int i = 1; i <= 6; ++i) {
    auto value = QLValue::Primitive(Format("v$0", i));
    auto value_ref = i == 5 ? ValueRef(ValueEntryType::kTombstone) : ValueRef(value);
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key), value_ref, HybridTime::FromMicros(i * 1000)));
    ASSERT_OK(FlushRocksDbAndWait());
  }

  ASSERT_EQ(6, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 6
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> DEL   // file 5
SubDocKey(DocKey([], ["k"]), [HT{ physical: 4000 }]) -> "v4"  // file 4
SubDocKey(DocKey([], ["k"]), [HT{ physical: 3000 }]) -> "v3"  // file 3
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");
  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);

  ASSERT_EQ(5, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 7
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> DEL   // file 7 as well
SubDocKey(DocKey([], ["k"]), [HT{ physical: 4000 }]) -> "v4"  // file 4
SubDocKey(DocKey([], ["k"]), [HT{ physical: 3000 }]) -> "v3"  // file 3
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(4, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 8
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> DEL   // file 8
SubDocKey(DocKey([], ["k"]), [HT{ physical: 3000 }]) -> "v3"  // file 3
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(3, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 9
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> DEL   // file 9
SubDocKey(DocKey([], ["k"]), [HT{ physical: 2000 }]) -> "v2"  // file 2
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(2, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 10
SubDocKey(DocKey([], ["k"]), [HT{ physical: 5000 }]) -> DEL   // file 10
SubDocKey(DocKey([], ["k"]), [HT{ physical: 1000 }]) -> "v1"  // file 1
      )#");

  // Now the minor compaction turns into a major compaction and we end up with one file.
  // The tombstone is now gone as well.
  MinorCompaction(5000_usec_ht, /* num_files_to_compact */ 2);
  ASSERT_EQ(1, NumSSTableFiles());
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["k"]), [HT{ physical: 6000 }]) -> "v6"  // file 11
      )#");
}

TEST_P(DocDBTestWrapper, BasicTest) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as 'S' (kString), 'I' (kInt64) correspond to members of the enum
  //   ValueType.
  // - Strings are terminated with \x00\x00.
  // - Groups of key components in the document key ("hashed" and "range" components) are terminated
  //   with '!' (kGroupEnd).
  // - 64-bit signed integers are encoded in the key using big-endian format with sign bit
  //   inverted.
  // - HybridTimes are represented as 64-bit unsigned integers with all bits inverted, so that's
  //   where we get a lot of \xff bytes from.

  SetInitMarkerBehavior(InitMarkerBehavior::kRequired);

  auto string_valued_doc_key = MakeDocKey("my_key_where_value_is_a_string");
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
  // Two zeros indicate the end of a string primitive field, and the '!' indicates the end
  // of the "range" part of the DocKey. There is no "hash" part, because the first
  // PrimitiveValue is not a hash value.
      "\"Smy_key_where_value_is_a_string\\x00\\x00!\"",
      string_valued_doc_key.Encode().ToString());

  TestInsertion(
      DocPath(string_valued_doc_key.Encode()),
      ValueRef(QLValue::Primitive("value1")),
      1000_usec_ht,
      R"#(1. PutCF('Smy_key_where_value_is_a_string\x00\x00\
                    !', 'Svalue1'))#");

  auto doc_key = MakeDocKey("mydockey", kIntKey1);
  KeyBytes encoded_doc_key(doc_key.Encode());

  TestInsertion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_a")),
      ValueRef(QLValue::Primitive("value_a")),
      2000_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !', '{')
2. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_a\x00\x00', 'Svalue_a')
      )#");

  TestInsertion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_b"), KeyEntryValue("subkey_c")),
      ValueRef(QLValue::Primitive("value_bc")),
      3000_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00', '{')
2. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00\
          Ssubkey_c\x00\x00', 'Svalue_bc')
      )#");

  // This only has one insertion, because the object at subkey "subkey_b" already exists.
  TestInsertion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_b"), KeyEntryValue("subkey_d")),
      ValueRef(QLValue::Primitive("value_bd")),
      3500_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00\
          Ssubkey_d\x00\x00', 'Svalue_bd')
      )#");

  // Delete a non-existent top-level document. We don't expect any tombstones to be created.
  TestDeletion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_x")),
      4000_usec_ht,
      "");

  // Delete a leaf-level value in a subdocument.
  TestDeletion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_b"), KeyEntryValue("subkey_c")),
      5000_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00\
          Ssubkey_c\x00\x00', 'X')
      )#");

  // Now delete an entire object.
  TestDeletion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_b")),
      6000_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00', 'X')
      )#");

  // Re-insert a value at subkey_b.subkey_c. This should see the tombstone from the previous
  // operation and create a new object at subkey_b at the new hybrid_time, hence two writes.
  TestInsertion(
      DocPath(encoded_doc_key, KeyEntryValue("subkey_b"), KeyEntryValue("subkey_c")),
      ValueRef(QLValue::Primitive("value_bc_prime")),
      7000_usec_ht,
      R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00', '{')
2. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          Ssubkey_b\x00\x00\
          Ssubkey_c\x00\x00', 'Svalue_bc_prime')
      )#");

  // Check the final state of the database.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(kPredefinedDBStateDebugDumpStr);
  CheckExpectedLatestDBState();

  // Compaction cleanup testing.

  ClearLogicalSnapshots();
  ASSERT_OK(CaptureLogicalSnapshot());
  FullyCompactHistoryBefore(5000_usec_ht);
  // The following entry gets deleted because it is invisible at hybrid_time 5000:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 3000 }])
  //     -> "value_bc"
  //
  // This entry is deleted because we can always remove deletes at or below the cutoff hybrid_time:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 5000 }])
  //     -> DEL
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 2000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT{ physical: 2000 w: 1 }]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 7000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 6000 }]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 3000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 7000 w: 1 }]) \
    -> "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT{ physical: 3500 }]) -> \
    "value_bd"
      )#");
  CheckExpectedLatestDBState();
  ASSERT_OK(CaptureLogicalSnapshot());
  // Perform the next history compaction starting both from the initial state as well as from the
  // state with the first history compaction (at hybrid_time 5000) already performed.
  for (const auto &snapshot : logical_snapshots()) {
    ASSERT_OK(snapshot.RestoreTo(rocksdb()));
    FullyCompactHistoryBefore(6000_usec_ht);
    // Now the following entries get deleted, because the entire subdocument at "subkey_b" gets
    // deleted at hybrid_time 6000, so we won't look at these records if we do a scan at
    // HT{ physical: 6000 }:
    //
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 3000 }]) -> {}
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 5000 }])
    //     -> DEL
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT{ physical: 3500 }])
    //     -> "value_bd"
    //
    // And the deletion itself is removed because it is at the history cutoff hybrid_time:
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 6000 }]) -> DEL
    ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 2000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT{ physical: 2000 w: 1 }]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 7000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 7000 w: 1 }]) \
    -> "value_bc_prime"
        )#");
    CheckExpectedLatestDBState();
  }
  ASSERT_OK(CaptureLogicalSnapshot());
  // Also test the next compaction starting with all previously captured states, (1) initial,
  // (2) after a compaction at hybrid_time 5000, and (3) after a compaction at hybrid_time 6000.
  // We are going through snapshots in reverse order so that we end with the initial snapshot that
  // does not have any history trimming done yet.
  for (auto i = num_logical_snapshots(); i > 0;) {
    --i;
    ASSERT_OK(RestoreToRocksDBLogicalSnapshot(i));
    // Test overwriting an entire document with an empty object. This should ideally happen with no
    // reads.
    TestInsertion(
        DocPath(encoded_doc_key),
        ValueRef(ValueEntryType::kObject),
        8000_usec_ht,
        R"#(
1. PutCF('Smydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !', '{')
        )#");
    VerifyDocument(doc_key, 8000_usec_ht, "{}");
  }

  // Reset our collection of snapshots now that we've performed one more operation.
  ClearLogicalSnapshots();

  ASSERT_OK(CaptureLogicalSnapshot());
  // This is similar to the kPredefinedDBStateDebugDumpStr, but has an additional overwrite of the
  // document with an empty object at hybrid_time 8000.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 8000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 2000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT{ physical: 2000 w: 1 }]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 7000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 6000 }]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 3000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 7000 w: 1 }]) \
    -> "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 3000 w: 1 }]) \
    -> "value_bc"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT{ physical: 3500 }]) -> \
    "value_bd"
      )#");
  FullyCompactHistoryBefore(7999_usec_ht);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 8000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 2000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT{ physical: 2000 w: 1 }]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT{ physical: 7000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT{ physical: 7000 w: 1 }]) \
    -> "value_bc_prime"
      )#");
  ASSERT_OK(CaptureLogicalSnapshot());
  // Starting with each snapshot, perform the final history compaction and verify we always get the
  // same result.
  for (size_t i = 0; i < logical_snapshots().size(); ++i) {
    ASSERT_OK(RestoreToRocksDBLogicalSnapshot(i));
    FullyCompactHistoryBefore(8000_usec_ht);
    ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 8000 }]) -> {}
        )#");
  }
}

TEST_P(DocDBTestWrapper, MultiOperationDocWriteBatch) {
  const auto encoded_doc_key = MakeDocKey("a").Encode();
  auto dwb = MakeDocWriteBatch();
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("b")), ValueRef(QLValue::Primitive("v1"))));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("c"), KeyEntryValue("d")),
      ValueRef(QLValue::Primitive("v2"))));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("c"), KeyEntryValue("e")),
      ValueRef(QLValue::Primitive("v3"))));

  ASSERT_OK(WriteToRocksDB(dwb, 1000_usec_ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["a"]), ["b"; HT{ physical: 1000 }]) -> "v1"
      SubDocKey(DocKey([], ["a"]), ["c", "d"; HT{ physical: 1000 w: 1 }]) -> "v2"
      SubDocKey(DocKey([], ["a"]), ["c", "e"; HT{ physical: 1000 w: 2 }]) -> "v3"
      )#");

  string dwb_str;
  ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
  EXPECT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          1. PutCF('Sa\x00\x00!Sb\x00\x00', 'Sv1')
          2. PutCF('Sa\x00\x00!Sc\x00\x00Sd\x00\x00', 'Sv2')
          3. PutCF('Sa\x00\x00!Sc\x00\x00Se\x00\x00', 'Sv3')
      )#", dwb_str);
}

TEST_P(DocDBTestWrapper, BloomFilterTest) {
  // Turn off "next instead of seek" optimization, because this test rely on DocDB to do seeks.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_nexts_to_avoid_seek) = 0;
  // Write batch and flush options.
  auto dwb = MakeDocWriteBatch();
  ASSERT_OK(FlushRocksDbAndWait());

  DocKey key1(0, MakeKeyEntryValues("key1"), KeyEntryValues());
  DocKey key2(0, MakeKeyEntryValues("key2"), KeyEntryValues());
  DocKey key3(0, MakeKeyEntryValues("key3"), KeyEntryValues());
  HybridTime ht;

  SubDocument doc_from_rocksdb;
  bool subdoc_found_in_rocksdb = false;
  uint64_t total_bloom_useful = 0;
  uint64_t total_table_iterators = 0;

  auto flush_rocksdb = [this, &total_table_iterators]() {
    ASSERT_OK(FlushRocksDbAndWait());
    total_table_iterators =
        regular_db_options().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
  };

  // The following code will set 2/3 keys at a time and flush those 2 writes in a new file. That
  // way we can control and know exactly when the bloom filter is useful.
  // We first write out k1 and k3 and confirm the bloom filter usage is bumped only for checking for
  // k2, as the file does not contain it:
  // file1: k1, k3
  //
  // We then proceed to write k1 and k2 in a new file and check the bloom usage again. At this
  // point, we have:
  // file1: k1, k3
  // file2: k1, k2
  // So the blooms will prune out one file each for k2 and k3 and nothing for k1.
  //
  // Finally, we write out k2 and k3 in a third file, leaving us with:
  // file1: k1, k3
  // file2: k1, k2
  // file3: k2, k3
  // At this point, the blooms will effectively filter out one file for each key.

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(1000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  flush_rocksdb();

  auto get_doc = [this, &doc_from_rocksdb, &subdoc_found_in_rocksdb](const DocKey &key) {
    auto encoded_subdoc_key = SubDocKey(key).EncodeWithoutHt();
    GetSubDoc(encoded_subdoc_key, &doc_from_rocksdb, &subdoc_found_in_rocksdb);
  };

  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful, 0, &total_table_iterators));
  ASSERT_NO_FATALS(get_doc(key1));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful, 1, &total_table_iterators));

  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_TRUE(!subdoc_found_in_rocksdb);
  // Bloom filter excluded this file.
  // docdb::TEST_GetSubDocument sometimes seeks twice - first time on key2 and second time to
  // advance out of it, because key2 was found.
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 0, &total_table_iterators));

  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful, 1, &total_table_iterators));
  dwb.Clear();
  ASSERT_OK(ht.FromUint64(2000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  flush_rocksdb();
  ASSERT_NO_FATALS(get_doc(key1));

  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful, 2, &total_table_iterators));
  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 1, &total_table_iterators));
  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 1, &total_table_iterators));

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(3000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), ValueRef(QLValue::Primitive("value"))));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  flush_rocksdb();
  ASSERT_NO_FATALS(get_doc(key1));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 2, &total_table_iterators));
  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 2, &total_table_iterators));
  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful, 2, &total_table_iterators));
}

TEST_P(DocDBTestWrapper, BloomFilterCorrectness) {
  // Write batch and flush options.
  auto dwb = MakeDocWriteBatch();
  ASSERT_OK(FlushRocksDbAndWait());

  // We need to write enough keys for fixed-size bloom filter to have more than one block.
  constexpr auto kNumKeys = 100000;
  const ColumnId kColumnId(11);
  const HybridTime ht(1000);

  const auto get_value = [](const int32_t i) {
    return QLValue::Primitive(i);
  };

  const auto get_doc_key = [&](const int32_t i, const bool is_range_key) {
    if (is_range_key) {
      return DocKey({ KeyEntryValue::Int32(i) });
    }
    const auto hash_component = KeyEntryValue::Int32(i);
    auto doc_key = DocKey(i, { hash_component });
    {
      std::string hash_components_buf;
      QLValuePB hash_component_pb;
      hash_component.ToQLValuePB(QLType::Create(DataType::INT32), &hash_component_pb);
      AppendToKey(hash_component_pb, &hash_components_buf);
      doc_key.set_hash(YBPartition::HashColumnCompoundValue(hash_components_buf));
    }
    return doc_key;
  };

  const auto get_sub_doc_key = [&](const int32_t i, const bool is_range_key) {
    return SubDocKey(get_doc_key(i, is_range_key), KeyEntryValue::MakeColumnId(kColumnId));
  };

  for (const auto is_range_key : { false, true }) {
    for (int32_t i = 0; i < kNumKeys; ++i) {
      const auto sub_doc_key = get_sub_doc_key(i, is_range_key);
      const auto value = get_value(i);
      dwb.Clear();
      ASSERT_OK(
          dwb.SetPrimitive(DocPath(sub_doc_key.doc_key().Encode(), sub_doc_key.subkeys()),
                           ValueRef(value)));
      ASSERT_OK(WriteToRocksDB(dwb, ht));
    }
    ASSERT_OK(FlushRocksDbAndWait());

    for (int32_t i = 0; i < kNumKeys; ++i) {
      const auto sub_doc_key = get_sub_doc_key(i, is_range_key);
      const auto value = get_value(i);
      const auto encoded_subdoc_key = sub_doc_key.EncodeWithoutHt();
      SubDocument sub_doc;
      bool sub_doc_found;
      GetSubDoc(encoded_subdoc_key, &sub_doc, &sub_doc_found);
      ASSERT_TRUE(sub_doc_found) << "Entry for key #" << i
                                 << " not found, is_range_key: " << is_range_key;
      ASSERT_EQ(static_cast<PrimitiveValue>(sub_doc), PrimitiveValue::FromQLValuePB(value));
    }
  }

  rocksdb::TablePropertiesCollection props;
  ASSERT_OK(rocksdb()->GetPropertiesOfAllTables(&props));
  for (const auto& prop : props) {
    ASSERT_GE(prop.second->num_filter_blocks, 2) << Format(
        "To test rolling over filter block we need at least 2 filter blocks, but got $0 for $1. "
        "Increase kNumKeys in this test.",
        prop.second->num_filter_blocks, prop.first);
  }
}

TEST_P(DocDBTestWrapper, MergingIterator) {
  // Test for the case described in https://yugabyte.atlassian.net/browse/ENG-1677.

  // Turn off "next instead of seek" optimization, because this test rely on DocDB to do seeks.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_nexts_to_avoid_seek) = 0;

  HybridTime ht;
  ASSERT_OK(ht.FromUint64(1000));

  // Put smaller key into SST file.
  DocKey key1(123, MakeKeyEntryValues("key1"), KeyEntryValues());
  auto dwb = MakeDocWriteBatch();
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), ValueRef(QLValue::Primitive("value1"))));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  ASSERT_OK(FlushRocksDbAndWait());

  // Put bigger key into memtable.
  DocKey key2(234, MakeKeyEntryValues("key2"), KeyEntryValues());
  dwb.Clear();
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), ValueRef(QLValue::Primitive("value2"))));
  ASSERT_OK(WriteToRocksDB(dwb, ht));

  // Get key2 from DocDB. Bloom filter will skip SST file and it should invalidate SST file
  // iterator in order for MergingIterator to not pickup key1 incorrectly.
  VerifyDocument(key2, ht, "\"value2\"");
}

TEST_P(DocDBTestWrapper, SetPrimitiveWithInitMarker) {
  // Both required and optional init marker should be ok.
  for (auto init_marker_behavior : InitMarkerBehaviorList()) {
    auto dwb = MakeDocWriteBatch(init_marker_behavior);
    ASSERT_OK(dwb.SetPrimitive(DocPath(
        kEncodedDocdbTestDocKey1), ValueRef(ValueEntryType::kObject)));
  }
}

TEST_P(DocDBTestWrapper, TestInetSortOrder) {
  InsertInet("1.2.3.4");
  InsertInet("2.2.3.4");
  InsertInet("::1");
  InsertInet("::ffff:ffff");
  InsertInet("::ff:ffff:ffff");
  InsertInet("180::2978:9018:b288:3f6c");
  InsertInet("fe80::2978:9018:b288:3f6c");
  InsertInet("255.255.255.255");
  InsertInet("ffff:ffff::");
  InsertInet("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["mydockey"]), [::1; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [::255.255.255.255; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [::ff:ffff:ffff; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [1.2.3.4; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [180::2978:9018:b288:3f6c; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [2.2.3.4; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [fe80::2978:9018:b288:3f6c; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [255.255.255.255; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [ffff:ffff::; HT{ physical: 1000 }]) -> null
SubDocKey(DocKey([], ["mydockey"]), [ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff; \
    HT{ physical: 1000 }]) -> null
      )#");
}

TEST_P(DocDBTestWrapper, TestDisambiguationOnWriteId) {
  DisableYcqlPackedRow();

  // Set a column and then delete the entire row in the same write batch. The row disappears.
  auto dwb = MakeDocWriteBatch();
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocdbTestDocKey1, KeyEntryValue::MakeColumnId(ColumnId(10))),
      ValueRef(QLValue::Primitive("value1"))));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocdbTestDocKey1), ValueRef(ValueEntryType::kTombstone)));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, 1000_usec_ht));

  SubDocKey subdoc_key(kDocdbTestDocKey1);
  SubDocument subdoc;
  bool doc_found = false;
  // TODO(dtxn) - check both transaction and non-transaction path?
  auto encoded_subdoc_key = subdoc_key.EncodeWithoutHt();
  GetSubDoc(encoded_subdoc_key, &subdoc, &doc_found, kNonTransactionalOperationContext);
  ASSERT_FALSE(doc_found);

  ASSERT_OK(CaptureLogicalSnapshot());
  for (int cutoff_time_ms = 1000; cutoff_time_ms <= 1001; ++cutoff_time_ms) {
    ASSERT_OK(RestoreToLastLogicalRocksDBSnapshot());

    // The row should still be absent after a compaction.
    // TODO(dtxn) - check both transaction and non-transaction path?
    FullyCompactHistoryBefore(HybridTime::FromMicros(cutoff_time_ms));
    GetSubDoc(encoded_subdoc_key, &subdoc, &doc_found, kNonTransactionalOperationContext);
    ASSERT_FALSE(doc_found);
    ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ("");
  }

  // Delete the row first, and then set a column. This row will exist.
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocdbTestDocKey2), ValueRef(ValueEntryType::kTombstone)));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocdbTestDocKey2, KeyEntryValue::MakeColumnId(ColumnId(10))),
      ValueRef(QLValue::Primitive("value2"))));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, 2000_usec_ht));
  // TODO(dtxn) - check both transaction and non-transaction path?
  SubDocKey subdoc_key2(kDocdbTestDocKey2);
  auto encoded_subdoc_key2 = subdoc_key2.EncodeWithoutHt();
  GetSubDoc(encoded_subdoc_key2, &subdoc, &doc_found, kNonTransactionalOperationContext);
  ASSERT_TRUE(doc_found);

  // The row should still exist after a compaction. The deletion marker should be compacted away.
  ASSERT_OK(CaptureLogicalSnapshot());
  for (int cutoff_time_ms = 2000; cutoff_time_ms <= 2001; ++cutoff_time_ms) {
    ASSERT_OK(RestoreToLastLogicalRocksDBSnapshot());
    FullyCompactHistoryBefore(HybridTime::FromMicros(cutoff_time_ms));
    // TODO(dtxn) - check both transaction and non-transaction path?
    GetSubDoc(encoded_subdoc_key2, &subdoc, &doc_found, kNonTransactionalOperationContext);
    ASSERT_TRUE(doc_found);
    ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 w: 1 }]) -> "value2"
        )#");
  }
}

TEST_P(DocDBTestWrapper, StaticColumnCompaction) {
  const DocKey hk(0, MakeKeyEntryValues("h1")); // hash key
  const DocKey pk1(hk.hash(), hk.hashed_group(), MakeKeyEntryValues("r1")); // primary key
  const DocKey pk2(hk.hash(), hk.hashed_group(), MakeKeyEntryValues("r2")); //   "      "
  const auto encoded_hk = hk.Encode();
  const auto encoded_pk1 = pk1.Encode();
  const auto encoded_pk2 = pk2.Encode();

  const MonoDelta one_ms = 1ms;
  const MonoDelta two_ms = 2ms;
  const HybridTime t0 = 1000_usec_ht;
  const HybridTime t1 = t0.AddDelta(two_ms);
  const HybridTime t2 = t1.AddDelta(two_ms);

  // Add some static columns: s1 and s2 with TTL, s3 and s4 without.
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_hk, KeyEntryValue("s1")),
      Ttl(one_ms), ValueRef(QLValue::Primitive("v1")), t0));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_hk, KeyEntryValue("s2")),
      Ttl(two_ms), ValueRef(QLValue::Primitive("v2")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, KeyEntryValue("s3")),
                         QLValue::Primitive("v3old"), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, KeyEntryValue("s4")),
                         QLValue::Primitive("v4"), t0));

  // Add some non-static columns for pk1: c5 and c6 with TTL, c7 and c8 without.
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_pk1, KeyEntryValue("c5")),
      Ttl(one_ms), ValueRef(QLValue::Primitive("v51")), t0));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_pk1, KeyEntryValue("c6")),
      Ttl(two_ms), ValueRef(QLValue::Primitive("v61")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, KeyEntryValue("c7")),
                         QLValue::Primitive("v71old"), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, KeyEntryValue("c8")),
                         QLValue::Primitive("v81"), t0));

  // More non-static columns for another primary key pk2.
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_pk2, KeyEntryValue("c5")),
      Ttl(one_ms), ValueRef(QLValue::Primitive("v52")), t0));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_pk2, KeyEntryValue("c6")),
      Ttl(two_ms), ValueRef(QLValue::Primitive("v62")), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, KeyEntryValue("c7")),
                         QLValue::Primitive("v72"), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, KeyEntryValue("c8")),
                         QLValue::Primitive("v82"), t0));

  // Update s3 and delete s4 at t1.
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, KeyEntryValue("s3")),
                         QLValue::Primitive("v3new"), t1));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, KeyEntryValue("s4")),
                         ValueRef(ValueEntryType::kTombstone), t1));

  // Update c7 of pk1 at t1 also.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, KeyEntryValue("c7")),
                         QLValue::Primitive("v71new"), t1));

  // Delete c8 of pk2 at t2.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, KeyEntryValue("c8")),
                         ValueRef(ValueEntryType::kTombstone), t2));

  // Verify before compaction.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), ["s1"; HT{ physical: 1000 }]) -> "v1"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s2"; HT{ physical: 1000 }]) -> "v2"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT{ physical: 3000 }]) -> "v3new"
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT{ physical: 1000 }]) -> "v3old"
SubDocKey(DocKey(0x0000, ["h1"], []), ["s4"; HT{ physical: 3000 }]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], []), ["s4"; HT{ physical: 1000 }]) -> "v4"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c5"; HT{ physical: 1000 }]) -> "v51"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c6"; HT{ physical: 1000 }]) -> "v61"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT{ physical: 3000 }]) -> "v71new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT{ physical: 1000 }]) -> "v71old"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c8"; HT{ physical: 1000 }]) -> "v81"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c5"; HT{ physical: 1000 }]) -> "v52"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c6"; HT{ physical: 1000 }]) -> "v62"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c7"; HT{ physical: 1000 }]) -> "v72"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT{ physical: 1000 }]) -> "v82"
      )#");

  // Compact at t1 = HT{ physical: 3000 }.
  FullyCompactHistoryBefore(t1);

  // Verify after compaction:
  //   s1 -> expired
  //   s4 -> deleted
  //   s3 = v3old -> compacted
  //   pk1.c5 -> expired
  //   pk1.c7 = v71old -> compacted
  //   pk2.c5 -> expired
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), ["s2"; HT{ physical: 1000 }]) -> "v2"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT{ physical: 3000 }]) -> "v3new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c6"; HT{ physical: 1000 }]) -> "v61"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT{ physical: 3000 }]) -> "v71new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c8"; HT{ physical: 1000 }]) -> "v81"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c6"; HT{ physical: 1000 }]) -> "v62"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c7"; HT{ physical: 1000 }]) -> "v72"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT{ physical: 5000 }]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT{ physical: 1000 }]) -> "v82"
      )#");
}

ValueControlFields UserTs(UserTimeMicros user_timestamp) {
  return ValueControlFields {
    .timestamp = user_timestamp,
  };
}

TEST_P(DocDBTestWrapper, TestUserTimestamp) {
  const auto doc_key = MakeDocKey("k1");
  KeyBytes encoded_doc_key(doc_key.Encode());

  // Only optional init marker supported for user timestamp.
  SetInitMarkerBehavior(InitMarkerBehavior::kRequired);
  ASSERT_NOK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s10")),
                          UserTs(1000), ValueRef(QLValue::Primitive("v10")), 1000_usec_ht));

  SetInitMarkerBehavior(InitMarkerBehavior::kOptional);

  HybridTime ht = 10000_usec_ht;
  // Use same doc_write_batch to test cache.
  auto doc_write_batch = MakeDocWriteBatch();
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s1"), KeyEntryValue("s2")),
      UserTs(1000), ValueRef(QLValue::Primitive("v1"))));
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s1")),
      UserTs(500), ValueRef(ValueEntryType::kObject)));
  ASSERT_OK(WriteToRocksDB(doc_write_batch, ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 10000 w: 1 }]) -> {}; timestamp: 500
SubDocKey(DocKey([], ["k1"]), ["s1", "s2"; HT{ physical: 10000 }]) -> "v1"; timestamp: 1000
      )#");

  doc_write_batch.Clear();
  // Use same doc_write_batch to test cache.
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s3")),
      UserTs(1000), ValueRef(ValueEntryType::kObject)));
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s3"), KeyEntryValue("s4")),
      UserTs(500), ValueRef(QLValue::Primitive("v1"))));
  ASSERT_OK(WriteToRocksDB(doc_write_batch, ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 10000 w: 1 }]) -> {}; timestamp: 500
SubDocKey(DocKey([], ["k1"]), ["s1", "s2"; HT{ physical: 10000 }]) -> "v1"; timestamp: 1000
SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 10000 }]) -> {}; timestamp: 1000
      )#");

  doc_write_batch.Clear();
  // Use same doc_write_batch to test cache.
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s3"), KeyEntryValue("s4")),
      UserTs(2000), ValueRef(QLValue::Primitive("v1"))));
  ASSERT_OK(doc_write_batch.SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s3"), KeyEntryValue("s5")),
      UserTs(2000), ValueRef(QLValue::Primitive("v1"))));
  ASSERT_OK(WriteToRocksDB(doc_write_batch, ht));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 10000 w: 1 }]) -> {}; timestamp: 500
SubDocKey(DocKey([], ["k1"]), ["s1", "s2"; HT{ physical: 10000 }]) -> "v1"; timestamp: 1000
SubDocKey(DocKey([], ["k1"]), ["s3"; HT{ physical: 10000 }]) -> {}; timestamp: 1000
SubDocKey(DocKey([], ["k1"]), ["s3", "s4"; HT{ physical: 10000 }]) -> "v1"; timestamp: 2000
SubDocKey(DocKey([], ["k1"]), ["s3", "s5"; HT{ physical: 10000 w: 1 }]) -> "v1"; \
    timestamp: 2000
      )#");
}

TEST_P(DocDBTestWrapper, TestCompactionWithUserTimestamp) {
  const auto doc_key = MakeDocKey("k1");
  HybridTime t3000 = 3000_usec_ht;
  HybridTime t5000 = 5000_usec_ht;
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s1")),
                         QLValue::Primitive("v11"), t3000));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v11"
      )#");

  // Delete the row.
  ASSERT_OK(DeleteSubDoc(DocPath(encoded_doc_key, KeyEntryValue("s1")), t5000));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 5000 }]) -> DEL
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v11"
      )#");

  // Try insert with lower timestamp.
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s1")),
                         UserTs(4000), ValueRef(QLValue::Primitive("v13")), t3000));

  // No effect on DB.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 5000 }]) -> DEL
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v11"
      )#");

  // Compaction takes away everything.
  FullyCompactHistoryBefore(t5000);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      )#");

  // Same insert with lower timestamp now works!
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s1")),
                         UserTs(4000), ValueRef(QLValue::Primitive("v13")), t3000));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v13"; timestamp: 4000
      )#");

  // Now try the same with TTL.
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("s2")),
      Ttl(MonoDelta::FromMicroseconds(1000)), ValueRef(QLValue::Primitive("v11")), t3000));

  // Insert with TTL.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v13"; timestamp: 4000
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 3000 }]) -> "v11"; ttl: 0.001s
      )#");

  // Try insert with lower timestamp.
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s2")),
                         UserTs(2000), ValueRef(QLValue::Primitive("v13")),
                         t3000, ReadHybridTime::SingleTime(t3000)));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v13"; timestamp: 4000
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 3000 }]) -> "v11"; ttl: 0.001s
      )#");

  FullyCompactHistoryBefore(t5000);

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v13"; timestamp: 4000
      )#");

  // Insert with lower timestamp after compaction works!
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue("s2")),
                         UserTs(2000), ValueRef(QLValue::Primitive("v13")), t3000));
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT{ physical: 3000 }]) -> "v13"; timestamp: 4000
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT{ physical: 3000 }]) -> "v13"; timestamp: 2000
      )#");
}

TEST_P(DocDBTestWrapper, CompactionWithTransactions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_sort_weak_intents) = true;

  const auto doc_key = MakeDocKey("mydockey", kIntKey1);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), 4000_usec_ht));

  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  Result<TransactionId> txn1 = FullyDecodeTransactionId("0000000000000001");
  const auto kTxn1HT = 5000_usec_ht;
  ASSERT_OK(txn1);
  SetCurrentTransactionId(*txn1);
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), kTxn1HT));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")), QLValue::Primitive("value4"), kTxn1HT));

  Result<TransactionId> txn2 = FullyDecodeTransactionId("0000000000000002");
  const auto kTxn2HT = 6000_usec_ht;
  ASSERT_OK(txn2);
  SetCurrentTransactionId(*txn2);
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key), ValueRef(ValueEntryType::kObject), kTxn2HT));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey2")), QLValue::Primitive("value5"), kTxn2HT));

  ResetCurrentTransactionId();
  TransactionId txn3 = ASSERT_RESULT(FullyDecodeTransactionId("0000000000000003"));
  const auto kTxn3HT = 7000_usec_ht;
  std::vector<ExternalIntent> intents = {
    { DocPath(encoded_doc_key, KeyEntryValue("subkey3")),
      EncodeValue(QLValue::Primitive("value6")) },
    { DocPath(encoded_doc_key, KeyEntryValue("subkey4")),
      EncodeValue(QLValue::Primitive("value7")) }
  };
  Uuid status_tablet = ASSERT_RESULT(Uuid::FromString("4c3e1d91-5ea7-4449-8bb3-8b0a3f9ae903"));
  ASSERT_OK(AddExternalIntents(txn3, kMinSubTransactionId, intents, status_tablet, kTxn3HT));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 3000 }]) -> "value3"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 2000 }]) -> "value2"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 1000 }]) -> "value1"
TXN EXT 30303030-3030-3030-3030-303030303033 HT{ physical: 7000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey3"]) -> "value6", \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey4"]) -> "value7"]
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 3 } \
    -> TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 3 } \
    -> TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kStrongRead, kStrongWrite] HT{ physical: 6000 } \
    -> TransactionId(30303030-3030-3030-3030-303030303032) WriteId(2) {}
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kStrongRead, kStrongWrite] HT{ physical: 5000 } \
    -> TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 5000 } -> TransactionId(30303030-3030-3030-3030-303030303031) WriteId(1) "value4"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey2"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 6000 } -> TransactionId(30303030-3030-3030-3030-303030303032) WriteId(3) "value5"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 5000 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] \
    HT{ physical: 5000 w: 3 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey2"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 6000 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 2 } -> \
    SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 3 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] \
    HT{ physical: 6000 w: 3 }
    )#");
  FullyCompactHistoryBefore(3500_usec_ht);
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 4000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT{ physical: 1000 }]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT{ physical: 3000 }]) -> "value3"
TXN EXT 30303030-3030-3030-3030-303030303033 HT{ physical: 7000 } -> \
    IT 03e99a3f0a8bb38b4944a75e911d3e4c [\
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey3"]) -> "value6", \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey4"]) -> "value7"]
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 1 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 2 } -> \
    TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 3 } \
    -> TransactionId(30303030-3030-3030-3030-303030303032) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 3 } \
    -> TransactionId(30303030-3030-3030-3030-303030303031) none
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kStrongRead, kStrongWrite] HT{ physical: 6000 } \
    -> TransactionId(30303030-3030-3030-3030-303030303032) WriteId(2) {}
SubDocKey(DocKey([], ["mydockey", 123456]), []) [kStrongRead, kStrongWrite] HT{ physical: 5000 } \
    -> TransactionId(30303030-3030-3030-3030-303030303031) WriteId(0) {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 5000 } -> TransactionId(30303030-3030-3030-3030-303030303031) WriteId(1) "value4"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey2"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 6000 } -> TransactionId(30303030-3030-3030-3030-303030303032) WriteId(3) "value5"
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 5000 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 2 } -> \
    SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 5000 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303031 HT{ physical: 5000 w: 3 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] \
    HT{ physical: 5000 w: 3 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey2"]) [kStrongRead, kStrongWrite] \
    HT{ physical: 6000 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 1 } -> \
    SubDocKey(DocKey([], []), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 1 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 2 } -> \
    SubDocKey(DocKey([], ["mydockey"]), []) [kWeakRead, kWeakWrite] HT{ physical: 6000 w: 2 }
TXN REV 30303030-3030-3030-3030-303030303032 HT{ physical: 6000 w: 3 } -> \
    SubDocKey(DocKey([], ["mydockey", 123456]), []) [kWeakRead, kWeakWrite] \
    HT{ physical: 6000 w: 3 }
    )#");
}

TEST_P(DocDBTestWrapper, ForceFlushedFrontier) {
  // We run with compactions disabled, because they may interefere with force-setting the OpId.
  ASSERT_OK(DisableCompactions());
  op_id_ = {1, 1};
  rocksdb::UserFrontierPtr flushed_frontier;
  for (int i = 1; i < 20; ++i) {
    const auto doc_key = MakeDocKey(i);
    const KeyBytes encoded_doc_key = doc_key.Encode();
    SetupRocksDBState(encoded_doc_key);
    ASSERT_OK(FlushRocksDbAndWait());
    flushed_frontier = rocksdb()->GetFlushedFrontier();
    LOG(INFO) << "Flushed frontier after i=" << i << ": "
              << (flushed_frontier ? flushed_frontier->ToString() : "N/A");
  }
  ASSERT_TRUE(flushed_frontier.get() != nullptr);
  ConsensusFrontier consensus_frontier =
      down_cast<ConsensusFrontier&>(*flushed_frontier);
  ConsensusFrontier new_consensus_frontier = consensus_frontier;
  new_consensus_frontier.set_op_id({
      consensus_frontier.op_id().term,
      consensus_frontier.op_id().index / 2
  });
  ASSERT_EQ(new_consensus_frontier.op_id().term, consensus_frontier.op_id().term);
  ASSERT_LT(new_consensus_frontier.op_id().index, consensus_frontier.op_id().index);
  ASSERT_EQ(new_consensus_frontier.hybrid_time(), consensus_frontier.hybrid_time());
  ASSERT_EQ(new_consensus_frontier.history_cutoff(),
            consensus_frontier.history_cutoff());
  rocksdb::UserFrontierPtr new_user_frontier_ptr(new ConsensusFrontier(new_consensus_frontier));

  LOG(INFO) << "Attempting to change flushed frontier from " << consensus_frontier
            << " to " << new_consensus_frontier;
  ASSERT_OK(regular_db_->ModifyFlushedFrontier(
      new_user_frontier_ptr, rocksdb::FrontierModificationMode::kForce));
  LOG(INFO) << "Checking that flushed froniter was set to " << new_consensus_frontier;
  ASSERT_EQ(*new_user_frontier_ptr, *regular_db_->GetFlushedFrontier());

  LOG(INFO) << "Reopening RocksDB";
  ASSERT_OK(ReopenRocksDB());
  LOG(INFO) << "Checking that flushed frontier is still set to "
            << regular_db_->GetFlushedFrontier()->ToString();
  ASSERT_EQ(*new_user_frontier_ptr, *regular_db_->GetFlushedFrontier());
}

// Handy code to analyze some DB.
TEST_P(DocDBTestWrapper, DISABLED_DumpDB) {
  tablet::TabletOptions tablet_options;
  rocksdb::Options options;
  docdb::InitRocksDBOptions(
      &options, "" /* log_prefix */, tablet_id(), rocksdb::CreateDBStatisticsForTests(),
      tablet_options);

  rocksdb::DB* rocksdb = nullptr;
  std::string db_path = "";
  ASSERT_OK(rocksdb::DB::Open(options, db_path, &rocksdb));

  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  unique_ptr<rocksdb::Iterator> iter(rocksdb->NewIterator(read_opts));
  iter->SeekToFirst();

  int txn_meta = 0;
  int rev_key = 0;
  int intent = 0;
  while (ASSERT_RESULT(iter->CheckedValid())) {
    auto key_type = GetKeyType(iter->key(), StorageDbType::kIntents);
    if (key_type == KeyType::kTransactionMetadata) {
      ++txn_meta;
    } else if (key_type == KeyType::kReverseTxnKey) {
      ++rev_key;
    } else if (key_type == KeyType::kIntentKey) {
      ++intent;
    } else {
      ASSERT_TRUE(false);
    }
    iter->Next();
  }

  LOG(INFO) << "TXN meta: " << txn_meta << ", rev key: " << rev_key << ", intents: " << intent;
}

TEST_P(DocDBTestWrapper, SetHybridTimeFilterSingleFile) {
  auto dwb = MakeDocWriteBatch();
  for (int i = 1; i <= 4; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  ASSERT_OK(FlushRocksDbAndWait());

  CloseRocksDB();

  RocksDBPatcher patcher(rocksdb_dir_, regular_db_options_);

  ASSERT_OK(patcher.Load());
  ASSERT_OK(patcher.SetHybridTimeFilter(std::nullopt, HybridTime::FromMicros(2000)));

  ASSERT_OK(OpenRocksDB());

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
  )#");

  rocksdb::CompactRangeOptions options;
  ASSERT_OK(ForceRocksDBCompact(rocksdb(), options));

  RocksDBPatcher patcher2(rocksdb_dir_, regular_db_options_);
  ASSERT_OK(patcher2.Load());
  ASSERT_FALSE(patcher2.TEST_ContainsHybridTimeFilter());

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
  )#");
}

void ScanForwardWithMetricCheck(
    rocksdb::Iterator* iter, const rocksdb::Statistics* regular_db_statistics,
    const Slice& upperbound, rocksdb::KeyFilterCallback* key_filter_callback,
    rocksdb::ScanCallback* scan_callback, uint64_t expected_number_of_keys_visited) {
  auto initial_stats = regular_db_statistics->getTickerCount(rocksdb::NUMBER_DB_NEXT);
  ASSERT_TRUE(iter->ScanForward(upperbound, key_filter_callback, scan_callback));
  ASSERT_OK(iter->status());
  ASSERT_EQ(
      expected_number_of_keys_visited,
      regular_db_statistics->getTickerCount(rocksdb::NUMBER_DB_NEXT) - initial_stats);
}

void ValidateScanForwardAndRegularIterator(DocDB doc_db) {
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  unique_ptr<rocksdb::Iterator> regular_iter(doc_db.regular->NewIterator(read_opts));
  regular_iter->SeekToFirst();
  unique_ptr<rocksdb::Iterator> iter(doc_db.regular->NewIterator(read_opts));
  iter->SeekToFirst();

  rocksdb::ScanCallback scan_callback = [&](const Slice& key, const Slice& value) -> bool {
    EXPECT_TRUE(regular_iter->Valid());
    EXPECT_EQ(regular_iter->key(), key) << "Regular: " << regular_iter->key().ToDebugHexString()
                                        << ", ScanForward: " << key.ToDebugHexString();
    EXPECT_EQ(regular_iter->value(), value);
    regular_iter->Next();
    return true;
  };

  ASSERT_TRUE(iter->ScanForward(Slice(), nullptr, &scan_callback));

  ASSERT_FALSE(ASSERT_RESULT(iter->CheckedValid()));
  ASSERT_FALSE(ASSERT_RESULT(regular_iter->CheckedValid()));
}

TEST_P(DocDBTestWrapper, SetHybridTimeFilter) {
  auto dwb = MakeDocWriteBatch();
  for (int i = 1; i <= 4; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  ASSERT_OK(FlushRocksDbAndWait());

  ASSERT_OK(SetHybridTimeFilter(std::nullopt, HybridTime::FromMicros(2000)));

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
  )#");

  // Validate the iterator API with callback.
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  for (int j = 0; j < 2; j++) {
    unique_ptr<rocksdb::Iterator> iter(doc_db().regular->NewIterator(read_opts));
    iter->SeekToFirst();
    int scanned_keys = 0;
    rocksdb::ScanCallback scan_callback = [&scanned_keys](
                                              const Slice& key, const Slice& value) -> bool {
      scanned_keys++;
      SubDocKey expected_subdoc_key(
          dockv::MakeDocKey(Format("row$0", scanned_keys), 11111 * scanned_keys),
          KeyEntryValue::MakeColumnId(ColumnId(10)),
          HybridTime::FromMicros(scanned_keys * 1000));

      SubDocKey subdoc_key;
      EXPECT_OK(subdoc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kTrue));
      PrimitiveValue primitive_value(ValueEntryType::kInt32);
      EXPECT_OK(primitive_value.DecodeFromValue(value));

      EXPECT_EQ(expected_subdoc_key.ToString(), subdoc_key.ToString());
      EXPECT_EQ(scanned_keys, primitive_value.GetInt32());

      return true;
    };
    if (j == 0) {
      ScanForwardWithMetricCheck(
          iter.get(), regular_db_options_.statistics.get(), Slice(),
          /*key_filter_callback=*/nullptr, &scan_callback, 4);
    } else {
      int kf_calls = 0;
      rocksdb::KeyFilterCallback kf_callback = [&kf_calls](
                             Slice prefixed_key, size_t shared_bytes,
                             Slice delta) -> rocksdb::KeyFilterCallbackResult {
        kf_calls++;
        return rocksdb::KeyFilterCallbackResult{.skip_key = false, .cache_key = false};
      };
      ScanForwardWithMetricCheck(
          iter.get(), regular_db_options_.statistics.get(), Slice(), &kf_callback, &scan_callback,
          4);
      ASSERT_EQ(2, kf_calls);
    }
    ASSERT_EQ(2, scanned_keys);
  }

  ASSERT_OK(WriteSimple(5));

  for (int j = 0; j < 3; ++j) {
    SCOPED_TRACE(Format("Iteration $0", j));

    ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
      SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 5000 }]) -> 5
    )#");

    if (j == 0) {
      ASSERT_OK(FlushRocksDbAndWait());
    } else if (j == 1) {
      rocksdb::CompactRangeOptions options;
      ASSERT_OK(ForceRocksDBCompact(rocksdb(), options));
    }
  }
}

TEST_P(DocDBTestWrapper, SetHybridTimeFilterMap) {
  auto dwb = MakeDocWriteBatch();
  std::vector<uint32_t> db_oids;
  std::vector<Uuid> uuids;
  std::unordered_set<std::string> expected;

  for (int i = 1; i <= 3; ++i) {
    Uuid temp = ASSERT_RESULT(WriteSimpleWithCotablePrefix(
        i, HybridTime::FromMicros(4000), Uuid::Nil()));
    uint32_t db_oid = 16234 + i;
    uuids.push_back(temp);
    db_oids.push_back(db_oid);
  }
  LOG(INFO) << "Inserted 3 key value pairs of 3 different cotables "
            << "of db oids " << AsString(db_oids) << " of cotables " << AsString(uuids)
            << " at hybrid time " << HybridTime::FromMicros(4000);

  // For cotable uuids[0] and uuids[1], insert kvs
  // that should not be filtered out (see below for filter).
  Uuid prefix = ASSERT_RESULT(WriteSimpleWithCotablePrefix(
      4, HybridTime::FromMicros(2000), uuids[0]));
  prefix = ASSERT_RESULT(WriteSimpleWithCotablePrefix(5, HybridTime::FromMicros(2000), uuids[1]));

  LOG(INFO) << "Inserted 2 key value pairs of cotables "
            << uuids[0].ToString() << " and " << uuids[1].ToString()
            << " of db oids " << db_oids[0] << " and " << db_oids[1]
            << " at hybrid time " << HybridTime::FromMicros(2000);

  ASSERT_OK(FlushRocksDbAndWait());

  ASSERT_OK(SetHybridTimeFilter(db_oids[1], HybridTime::FromMicros(2000)));
  ASSERT_OK(SetHybridTimeFilter(db_oids[2], HybridTime::FromMicros(2000)));
  LOG(INFO) << "Set filter of hybrid time " << HybridTime::FromMicros(2000)
            << " for cotables " << uuids[1].ToString() << " and " << uuids[2].ToString();

  expected.insert(Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"row1\", 11111], []), "
      "[ColumnId(10); HT{ physical: 4000 }]) -> 1", uuids[0].ToString()));

  expected.insert(Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0004, [\"row4\", 44444], []), "
      "[ColumnId(10); HT{ physical: 2000 }]) -> 4", uuids[0].ToString()));

  expected.insert(Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0005, [\"row5\", 55555], []), "
      "[ColumnId(10); HT{ physical: 2000 }]) -> 5", uuids[1].ToString()));

  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));

  // Validate various cases of setting filter on top of an existing filter.
  ASSERT_OK(SetHybridTimeFilter(db_oids[1], HybridTime::FromMicros(1000)));
  ASSERT_OK(SetHybridTimeFilter(db_oids[2], HybridTime::FromMicros(6000)));
  ASSERT_OK(SetHybridTimeFilter(db_oids[0], HybridTime::FromMicros(2000)));

  // For uuid[2], the filter should remain as it was before since we are updating to a more recent
  // ht. For uuid[1], the filter should also filter out the entry inserted at 2000 now
  // and for uuid[0] this is a brand new filter so it should filter out entry inserted at 4000.
  expected.clear();
  expected.insert(Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0004, [\"row4\", 44444], []), "
      "[ColumnId(10); HT{ physical: 2000 }]) -> 4", uuids[0].ToString()));
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));

  // Filter should be removed after compaction.
  rocksdb::CompactRangeOptions options;
  ASSERT_OK(ForceRocksDBCompact(rocksdb(), options));

  LOG(INFO) << "Validating that filter gets removed post compaction";
  RocksDBPatcher patcher(rocksdb_dir_, regular_db_options_);
  ASSERT_OK(patcher.Load());
  ASSERT_FALSE(patcher.TEST_ContainsHybridTimeFilter());

  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
}

TEST_P(DocDBTestWrapper, CombinedHybridTimeFilterAndCotablesFilter) {
  auto dwb = MakeDocWriteBatch();
  std::vector<Uuid> uuids;
  std::vector<uint32_t> db_oids;
  std::unordered_set<std::string> expected;

  for (int i = 1; i <= 3; ++i) {
    Uuid temp = ASSERT_RESULT(WriteSimpleWithCotablePrefix(
        i, HybridTime::FromMicros(i * 2000), Uuid::Nil()));
    std::string table_id = temp.ToHexString();
    uint32_t db_oid = 16234 + i;
    uuids.push_back(temp);
    db_oids.push_back(db_oid);
  }
  LOG(INFO) << "Inserted 3 key value pairs of 3 different cotables "
            << AsString(uuids) << " of db oids "
            << AsString(db_oids) << " at hybrid time "
            << HybridTime::FromMicros(2000) << ", "
            << HybridTime::FromMicros(4000) << " and "
            << HybridTime::FromMicros(6000);

  ASSERT_OK(FlushRocksDbAndWait());

  ASSERT_OK(SetHybridTimeFilter(db_oids[1], HybridTime::FromMicros(3000)));
  ASSERT_OK(SetHybridTimeFilter(db_oids[2], HybridTime::FromMicros(7000)));
  LOG(INFO) << "Set filter of hybrid time " << HybridTime::FromMicros(3000)
            << " and " << HybridTime::FromMicros(7000)
            << " for cotables " << uuids[1].ToString() << " and " << uuids[2].ToString();
  ASSERT_OK(SetHybridTimeFilter(std::nullopt, HybridTime::FromMicros(3000)));
  LOG(INFO) << "Set global HT Filter of " << HybridTime::FromMicros(3000);

  // uuids[2] should be filtered because of the global filter and uuids[1] should be
  // filtered because of cotables filter. uuids[0] should not get filtered at all.
  expected.insert(Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"row1\", 11111], []), "
      "[ColumnId(10); HT{ physical: 2000 }]) -> 1", uuids[0].ToString()));
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));

  // Both the filters should be removed after compaction.
  rocksdb::CompactRangeOptions options;
  ASSERT_OK(ForceRocksDBCompact(rocksdb(), options));

  LOG(INFO) << "Validating that filter gets removed post compaction";
  RocksDBPatcher patcher(rocksdb_dir_, regular_db_options_);
  ASSERT_OK(patcher.Load());
  ASSERT_FALSE(patcher.TEST_ContainsHybridTimeFilter());

  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
}

TEST_P(DocDBTestWrapper, IteratorScanForwardUpperbound) {
  constexpr int kNumKeys = 9;
  auto dwb = MakeDocWriteBatch();
  for (int i = 1; i <= kNumKeys; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
      SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 3000 }]) -> 3
      SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 4000 }]) -> 4
      SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 5000 }]) -> 5
      SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 6000 }]) -> 6
      SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 7000 }]) -> 7
      SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 8000 }]) -> 8
      SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 9000 }]) -> 9
  )#");

  ValidateScanForwardAndRegularIterator(doc_db());

  {
    int scanned_keys = 0;
    rocksdb::ScanCallback scan_callback = [&scanned_keys](
                                              const Slice& key, const Slice& value) -> bool {
      auto expected_value = scanned_keys + 1;
      SubDocKey expected_subdoc_key(
          dockv::MakeDocKey(Format("row$0", expected_value), 11111 * expected_value),
          KeyEntryValue::MakeColumnId(ColumnId(10)),
          HybridTime::FromMicros(expected_value * 1000));

      SubDocKey subdoc_key;
      EXPECT_OK(subdoc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kTrue));
      PrimitiveValue primitive_value(ValueEntryType::kInt32);
      EXPECT_OK(primitive_value.DecodeFromValue(value));

      EXPECT_EQ(expected_subdoc_key.ToString(), subdoc_key.ToString());
      EXPECT_EQ(expected_value, primitive_value.GetInt32());

      scanned_keys++;
      return true;
    };

    // Validate upperbound in memtable and flushed SST.
    rocksdb::ReadOptions read_opts;
    read_opts.query_id = rocksdb::kDefaultQueryId;
    for (int k = 0; k < 2; k++) {
      for (int i = 1; i <= kNumKeys; ++i) {
        unique_ptr<rocksdb::Iterator> iter(doc_db().regular->NewIterator(read_opts));
        iter->SeekToFirst();
        scanned_keys = 0;

        auto encoded_doc_key = dockv::MakeDocKey(Format("row$0", i), 11111 * i).Encode();
        ScanForwardWithMetricCheck(
            iter.get(), regular_db_options_.statistics.get(), encoded_doc_key,
            /*key_filter_callback=*/nullptr, &scan_callback, i - 1);
        ASSERT_EQ(i - 1, scanned_keys);

        ASSERT_TRUE(ASSERT_RESULT(iter->CheckedValid()));
        ASSERT_EQ(encoded_doc_key.AsSlice(), iter->key().Prefix(encoded_doc_key.size()));

        ScanForwardWithMetricCheck(
            iter.get(), regular_db_options_.statistics.get(), Slice(),
            /*key_filter_callback=*/nullptr, &scan_callback, kNumKeys - i + 1);
        ASSERT_EQ(kNumKeys, scanned_keys);

        ASSERT_FALSE(ASSERT_RESULT(iter->CheckedValid()));
      }

      if (k == 0) {
        ASSERT_OK(FlushRocksDbAndWait());
      }
    }
  }

  // Add more records which will go to memtable and overlap in the range.
  for (int i = 1; i <= kNumKeys; i++) {
    auto index = i + 10;
    auto encoded_doc_key = dockv::MakeDocKey(Format("row$0", i), 11111 * i).Encode();
    op_id_.term = index / 2;
    op_id_.index = index;
    auto& dwb = DefaultDocWriteBatch();
    QLValuePB value;
    value.set_int32_value(index);
    ASSERT_OK(dwb.SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(10))), ValueRef(value)));
    ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000 * 10 * i)));
  }

  ValidateScanForwardAndRegularIterator(doc_db());
}

TEST_P(DocDBTestWrapper, InterleavedRecordsScanForward) {
  constexpr int kNumKeys = 9;
  auto dwb = MakeDocWriteBatch();
  for (int i = 1; i <= kNumKeys; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  ValidateScanForwardAndRegularIterator(doc_db());

  // Move first kNumKeys records to SST file.
  ASSERT_OK(FlushRocksDbAndWait());

  // Add records to memtable interleaved with first SST file.
  for (int i = 1; i <= kNumKeys; i++) {
    auto index = i + 10;
    auto encoded_doc_key = dockv::MakeDocKey(Format("row$0", i), 11111 * i).Encode();
    op_id_.term = index / 2;
    op_id_.index = index;
    auto& dwb = DefaultDocWriteBatch();
    QLValuePB value;
    value.set_int32_value(index);
    ASSERT_OK(dwb.SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(10))), ValueRef(value)));
    ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000 * 10 * i)));
  }

  ValidateScanForwardAndRegularIterator(doc_db());

  // Move second set of kNumKeys records to second SST file.
  ASSERT_OK(FlushRocksDbAndWait());

  // Add records to memtable interleaved with first SST file.
  for (int i = 1; i <= kNumKeys; i++) {
    auto index = i + 20;
    auto encoded_doc_key = dockv::MakeDocKey(Format("row$0", i), 11111 * i).Encode();
    op_id_.term = index / 2;
    op_id_.index = index;
    auto& dwb = DefaultDocWriteBatch();
    QLValuePB value;
    value.set_int32_value(index);
    ASSERT_OK(dwb.SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(10))), ValueRef(value)));
    ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000 * 5 * i)));
  }

  ValidateScanForwardAndRegularIterator(doc_db());

  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 10000 }]) -> 11
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 5000 }]) -> 21
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 20000 }]) -> 12
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 10000 }]) -> 22
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
      SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 30000 }]) -> 13
      SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 15000 }]) -> 23
      SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 3000 }]) -> 3
      SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 40000 }]) -> 14
      SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 20000 }]) -> 24
      SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 4000 }]) -> 4
      SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 50000 }]) -> 15
      SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 25000 }]) -> 25
      SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 5000 }]) -> 5
      SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 60000 }]) -> 16
      SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 30000 }]) -> 26
      SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 6000 }]) -> 6
      SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 70000 }]) -> 17
      SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 35000 }]) -> 27
      SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 7000 }]) -> 7
      SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 80000 }]) -> 18
      SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 40000 }]) -> 28
      SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 8000 }]) -> 8
      SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 90000 }]) -> 19
      SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 45000 }]) -> 29
      SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 9000 }]) -> 9
  )#");
}

TEST_P(DocDBTestWrapper, ScanForwardWithDuplicateKeys) {
  constexpr int kNumKeys = 9;
  for (int i = 1; i <= kNumKeys; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  ValidateScanForwardAndRegularIterator(doc_db());

  // Move first kNumKeys records to SST file.
  ASSERT_OK(FlushRocksDbAndWait());

  // Add same records again in memtable.
  for (int i = 1; i <= kNumKeys; ++i) {
    ASSERT_OK(WriteSimple(i));
  }

  // Validate that ScanForward API scans all keys.
  rocksdb::ReadOptions read_opts;
  read_opts.query_id = rocksdb::kDefaultQueryId;
  unique_ptr<rocksdb::Iterator> iter(doc_db().regular->NewIterator(read_opts));
  iter->SeekToFirst();
  size_t scanned_keys = 0;
  rocksdb::ScanCallback scan_callback = [&scanned_keys](
                                            const Slice& key, const Slice& value) -> bool {
    scanned_keys++;
    return true;
  };

  ScanForwardWithMetricCheck(
          iter.get(), regular_db_options_.statistics.get(), Slice(),
          /*key_filter_callback=*/nullptr, &scan_callback, kNumKeys * 2);

  ASSERT_EQ(kNumKeys * 2, scanned_keys);

  // DocDB debug dump to str uses ScanForward API therefore we see duplicate keys in below output.
  ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(R"#(
    SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
    SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(10); HT{ physical: 1000 }]) -> 1
    SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
    SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT{ physical: 2000 }]) -> 2
    SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 3000 }]) -> 3
    SubDocKey(DocKey([], ["row3", 33333]), [ColumnId(10); HT{ physical: 3000 }]) -> 3
    SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 4000 }]) -> 4
    SubDocKey(DocKey([], ["row4", 44444]), [ColumnId(10); HT{ physical: 4000 }]) -> 4
    SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 5000 }]) -> 5
    SubDocKey(DocKey([], ["row5", 55555]), [ColumnId(10); HT{ physical: 5000 }]) -> 5
    SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 6000 }]) -> 6
    SubDocKey(DocKey([], ["row6", 66666]), [ColumnId(10); HT{ physical: 6000 }]) -> 6
    SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 7000 }]) -> 7
    SubDocKey(DocKey([], ["row7", 77777]), [ColumnId(10); HT{ physical: 7000 }]) -> 7
    SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 8000 }]) -> 8
    SubDocKey(DocKey([], ["row8", 88888]), [ColumnId(10); HT{ physical: 8000 }]) -> 8
    SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 9000 }]) -> 9
    SubDocKey(DocKey([], ["row9", 99999]), [ColumnId(10); HT{ physical: 9000 }]) -> 9
  )#");
}

TEST_P(DocDBTestWrapper, DISABLED_KeyBuffer) {
  TestKeyBytes<std::string>("std::string");
  TestKeyBytes<faststring>("faststring");
  TestKeyBytes<boost::container::small_vector<char, 8>>("small_vector<char, 8>");
  TestKeyBytes<boost::container::small_vector<char, 16>>("small_vector<char, 16>");
  TestKeyBytes<boost::container::small_vector<char, 32>>("small_vector<char, 32>");
  TestKeyBytes<boost::container::small_vector<char, 64>>("small_vector<char, 64>");
  TestKeyBytes<ByteBuffer<8>>("ByteBuffer<8>");
  TestKeyBytes<ByteBuffer<16>>("ByteBuffer<16>");
  TestKeyBytes<ByteBuffer<32>>("ByteBuffer<32>");
  TestKeyBytes<ByteBuffer<64>>("ByteBuffer<64>");
}

TEST_F(DocDBTestWrapper, HistoryRetentionWithCotables) {
  // One key with cotable.
  DocKeyHash key_hash = 1;
  uint32_t db_oid = 16386;
  uint32_t table_oid = 16389;
  dockv::KeyEntryValues hash_components =
      dockv::MakeKeyEntryValues("cotablekey", 10000);
  std::string table_id = GetPgsqlTableId(db_oid, table_oid);
  Uuid cotable_id = ASSERT_RESULT(Uuid::FromHexString(table_id));
  auto encoded_doc_key = dockv::DocKey(cotable_id, key_hash, hash_components).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value4"), 4000_usec_ht));
  // Another cotable.
  DocKeyHash key_hash2 = 3;
  uint32_t db_oid2 = 16389;
  uint32_t table_oid2 = 16389;
  dockv::KeyEntryValues hash_components2 =
      dockv::MakeKeyEntryValues("cotablekey2", 10000);
  std::string table_id2 = GetPgsqlTableId(db_oid2, table_oid2);
  Uuid cotable_id2 = ASSERT_RESULT(Uuid::FromHexString(table_id2));
  auto encoded_doc_key2 = dockv::DocKey(cotable_id2, key_hash2, hash_components2).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key2, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  // Another key without cotable.
  DocKeyHash non_cotable_hash = 2;
  dockv::KeyEntryValues non_cotable_hash_components =
      dockv::MakeKeyEntryValues("noncotablekey", 10000);
  auto encoded_non_cotable_key =
      dockv::DocKey(non_cotable_hash, non_cotable_hash_components).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value4"), 4000_usec_ht));
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();

  std::unordered_set<std::string> expected;
  expected = {
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", cotable_id.ToString()),
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"", cotable_id.ToString()),
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", cotable_id.ToString()),
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", cotable_id.ToString()),
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"",
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0003, [\"cotablekey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", cotable_id2.ToString()) };
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  ASSERT_OK(FlushRocksDbAndWait());
  auto frontier = rocksdb()->GetFlushedFrontier();
  ASSERT_EQ(frontier.get(), nullptr);
  HistoryCutoff cutoff = { 3000_usec_ht, 2000_usec_ht };
  FullyCompactHistoryBefore(cutoff);
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();
  // For non-cotable, records inserted at 2000, 3000 and 4000 should exist.
  // For cotable, records inserted at 3000 and 4000 should exist.
  expected = {
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"",
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", cotable_id.ToString()),
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0001, [\"cotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", cotable_id.ToString()),
    Format(
      "SubDocKey(DocKey(CoTableId=$0, 0x0003, [\"cotablekey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", cotable_id2.ToString()) };
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  frontier = rocksdb()->GetFlushedFrontier();
  auto consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after compaction "
            << consensus_frontier.ToString();
  LOG(INFO) << "Reopening RocksDB";
  ASSERT_OK(ReopenRocksDB());
  frontier = rocksdb()->GetFlushedFrontier();
  consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after restart "
            << consensus_frontier.ToString();
}

TEST_F(DocDBTestWrapper, HistoryRetentionWithColocatedTables) {
  // One key with colocation.
  DocKeyHash key_hash = 1;
  dockv::KeyEntryValues hash_components =
      dockv::MakeKeyEntryValues("colocationkey", 10000);
  uint32_t colocation_id = 16384;
  auto encoded_doc_key = dockv::DocKey(colocation_id, key_hash, hash_components).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_doc_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value4"), 4000_usec_ht));
  // Another key with a different colocation.
  DocKeyHash hash2 = 2;
  dockv::KeyEntryValues hash_components2 =
      dockv::MakeKeyEntryValues("colocationkey2", 10000);
  uint32_t colocation_id2 = 16389;
  auto encoded_key2 =
      dockv::DocKey(colocation_id2, hash2, hash_components2).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_key2, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_key2, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_key2, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_key2, KeyEntryValue("subkey1")),
      QLValue::Primitive("value4"), 4000_usec_ht));
  // Third key with just one record.
  DocKeyHash hash3 = 3;
  dockv::KeyEntryValues hash_components3 =
      dockv::MakeKeyEntryValues("colocationkey3", 10000);
  uint32_t colocation_id3 = 16394;
  auto encoded_key3 =
      dockv::DocKey(colocation_id3, hash3, hash_components3).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_key3, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();

  std::unordered_set<std::string> expected;
  expected = {
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0003, [\"colocationkey3\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", colocation_id3) };

  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  ASSERT_OK(FlushRocksDbAndWait());
  auto frontier = rocksdb()->GetFlushedFrontier();
  ASSERT_EQ(frontier.get(), nullptr);
  HistoryCutoff cutoff = { HybridTime::kInvalid, 3000_usec_ht };
  FullyCompactHistoryBefore(cutoff);
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();
  // records inserted at 3000, and 4000 should exist.
  expected = {
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0001, [\"colocationkey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", colocation_id),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0002, [\"colocationkey2\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"", colocation_id2),
    Format(
      "SubDocKey(DocKey(ColocationId=$0, 0x0003, [\"colocationkey3\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"", colocation_id3) };
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  frontier = rocksdb()->GetFlushedFrontier();
  auto consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after compaction "
            << consensus_frontier.ToString();
  LOG(INFO) << "Reopening RocksDB";
  ASSERT_OK(ReopenRocksDB());
  frontier = rocksdb()->GetFlushedFrontier();
  consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after restart "
            << consensus_frontier.ToString();
}

TEST_F(DocDBTestWrapper, HistoryRetentionWithNonColocatedTables) {
  // key without prefix.
  DocKeyHash non_cotable_hash = 2;
  dockv::KeyEntryValues non_cotable_hash_components =
      dockv::MakeKeyEntryValues("noncotablekey", 10000);
  auto encoded_non_cotable_key =
      dockv::DocKey(non_cotable_hash, non_cotable_hash_components).Encode();
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value2"), 2000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value3"), 3000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey1")),
      QLValue::Primitive("value4"), 4000_usec_ht));
  ASSERT_OK(SetPrimitive(
      DocPath(encoded_non_cotable_key, KeyEntryValue("subkey2")),
      QLValue::Primitive("value1"), 1000_usec_ht));
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();

  std::unordered_set<std::string> expected;
  expected = {
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 1000 }]) -> \"value1\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey2\"; HT{ physical: 1000 }]) -> \"value1\"" };
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  ASSERT_OK(FlushRocksDbAndWait());
  auto frontier = rocksdb()->GetFlushedFrontier();
  ASSERT_EQ(frontier.get(), nullptr);
  HistoryCutoff cutoff = { HybridTime::kInvalid, 2000_usec_ht };
  FullyCompactHistoryBefore(cutoff);
  LOG(INFO) << "Expected records " << DocDBDebugDumpToStr();
  // For non-cotable, records inserted at 2000, 3000 and 4000 should exist.
  expected = {
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 2000 }]) -> \"value2\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 3000 }]) -> \"value3\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey1\"; HT{ physical: 4000 }]) -> \"value4\"",
      "SubDocKey(DocKey(0x0002, [\"noncotablekey\", 10000], []), "
      "[\"subkey2\"; HT{ physical: 1000 }]) -> \"value1\"" };
  ASSERT_OK(ValidateRocksDbEntriesUnordered(&expected));
  frontier = rocksdb()->GetFlushedFrontier();
  auto consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after compaction "
            << consensus_frontier.ToString();
  LOG(INFO) << "Reopening RocksDB";
  ASSERT_OK(ReopenRocksDB());
  frontier = rocksdb()->GetFlushedFrontier();
  consensus_frontier = down_cast<ConsensusFrontier&>(*frontier);
  ASSERT_EQ(consensus_frontier.history_cutoff(), cutoff);
  LOG(INFO) << "Frontier obtained after restart "
            << consensus_frontier.ToString();
}

} // namespace docdb
} // namespace yb
