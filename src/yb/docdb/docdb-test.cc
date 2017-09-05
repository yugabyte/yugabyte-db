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

#include "yb/docdb/docdb.h"

#include <memory>
#include <string>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/util/statistics.h"

#include "yb/common/hybrid_time.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/in_mem_docdb.h"
#include "yb/docdb/intent.h"
#include "yb/gutil/stringprintf.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"

#include "yb/util/minmax.h"
#include "yb/util/path_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::cout;
using std::endl;
using std::make_pair;
using std::map;
using std::string;
using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;

using yb::util::TrimStr;
using yb::util::ApplyEagerLineContinuation;

using rocksdb::WriteOptions;

DECLARE_bool(use_docdb_aware_bloom_filter);
DECLARE_int32(max_nexts_to_avoid_seek);

namespace yb {
namespace docdb {

CHECKED_STATUS GetPrimitiveValue(const rocksdb::UserBoundaryValues& values,
                                 size_t index,
                                 PrimitiveValue* out);
CHECKED_STATUS GetDocHybridTime(const rocksdb::UserBoundaryValues& values, DocHybridTime* out);

class DocDBTest : public DocDBTestBase {
 protected:
  DocDBTest() {
    SeedRandom();
  }

  ~DocDBTest() override {
  }

  // This is the baseline state of the database that we set up and come back to as we test various
  // operations.
  static constexpr const char* const kPredefinedDBStateDebugDumpStr = R"#(
      SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=2000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(p=2000, w=1)]) -> "value_a"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=7000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=6000)]) -> DEL
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=3000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=7000, w=1)]) -> \
          "value_bc_prime"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=5000)]) -> DEL
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=3000, w=1)]) -> \
          "value_bc"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(p=3500)]) -> \
          "value_bd"
      )#";

  static constexpr const char* const kPredefinedDocumentDebugDumpStr =
      "StartSubDocument(SubDocKey(DocKey([], [\"mydockey\", 123456]), [HT(p=2000)]))\n"
      "StartObject\n"
      "VisitKey(\"subkey_a\")\n"
      "VisitValue(\"value_a\")\n"
      "VisitKey(\"subkey_b\")\n"
      "StartObject\n"
      "VisitKey(\"subkey_c\")\n"
      "VisitValue(\"value_bc_prime\")\n"
      "EndObject\n"
      "EndObject\n"
      "EndSubDocument\n";

  static const DocKey kDocKey1;
  static const DocKey kDocKey2;
  static const KeyBytes kEncodedDocKey1;
  static const KeyBytes kEncodedDocKey2;
  static const Schema kSchemaForIteratorTests;
  static Schema kProjectionForIteratorTests;

  static void SetUpTestCase() {
    ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames( {"c", "d", "e"},
        &kProjectionForIteratorTests));
  }

  void TestInsertion(
      DocPath doc_path,
      const PrimitiveValue& value,
      HybridTime hybrid_time,
      string expected_write_batch_str);

  void TestDeletion(
      DocPath doc_path,
      HybridTime hybrid_time,
      string expected_write_batch_str);

  void SetupRocksDBState(KeyBytes encoded_doc_key, InitMarkerBehavior use_init_marker) {

    SubDocument root;
    SubDocument a, b, c, d, e, f, b2;

    // The test plan below:
    // Set root = {a: {1: 1, 2: 2}, b: {c: {1: 3}, d: {1: 5, 2: 6}}, u: 7}
    // Then set root.a.2 = 11
    // Then replace root.b = {e: {1: 8, 2: 9}, y: 10}
    // Then extend root.a by {1: 3, 3: 4}
    // Then Delete root.b.e.2
    // The end result should be {a: {1: 3, 2: 11, 3: 4, x: {}}, b: {e: {1: 8}, y: 10}, u: 7}

#define SET_CHILD(parent, child) parent.SetChild(PrimitiveValue(#child), std::move(child))
#define SET_VALUE(parent, key, value) parent.SetChild(PrimitiveValue(key), \
                                                      SubDocument(PrimitiveValue(value)))

    // Constructing top level document: "root"
    SET_VALUE(root, "u", "7");
    SET_VALUE(a, "1", "1");
    SET_VALUE(a, "2", "2");
    SET_VALUE(c, "1", "3");
    SET_VALUE(d, "1", "5");
    SET_VALUE(d, "2", "6");
    SET_CHILD(b, c);
    SET_CHILD(b, d);
    SET_CHILD(root, a);
    SET_CHILD(root, b);

    EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
        {
          "a": {
            "1": "1",
            "2": "2"
          },
          "b": {
            "c": {
              "1": "3"
            },
            "d": {
              "1": "5",
              "2": "6"
            }
          },
          "u": "7"
        }
        )#", root.ToString());

    // Constructing new version of b = b2 to be inserted later.
    SET_VALUE(b2, "y", "10");
    SET_VALUE(e, "1", "8");
    SET_VALUE(e, "2", "9");
    SET_CHILD(b2, e);

    EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  "e": {
    "1": "8",
    "2": "9"
  },
  "y": "10"
}
      )#", b2.ToString());

    // Constructing a doc with which we will extend a later
    SET_VALUE(f, "1", "3");
    SET_VALUE(f, "3", "4");

    EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  "1": "3",
  "3": "4"
}
      )#", f.ToString());

#undef SET_CHILD
#undef SET_VALUE

    ASSERT_OK(InsertSubDocument(
        DocPath(encoded_doc_key), root, HybridTime::FromMicros(1000), use_init_marker));
    // The Insert above could have been an Extend with no difference in external behavior.
    // Internally however, an insert writes an extra key (with value tombstone).
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key, PrimitiveValue("a"), PrimitiveValue("2")),
        Value(PrimitiveValue(11)), HybridTime::FromMicros(2000), use_init_marker));
    ASSERT_OK(InsertSubDocument(DocPath(encoded_doc_key, PrimitiveValue("b")), b2,
        HybridTime::FromMicros(3000), use_init_marker));
    ASSERT_OK(ExtendSubDocument(DocPath(encoded_doc_key, PrimitiveValue("a")), f,
        HybridTime::FromMicros(4000), use_init_marker));
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key, PrimitiveValue("b"), PrimitiveValue("e"), PrimitiveValue("2")),
        Value(PrimitiveValue(ValueType::kTombstone)), HybridTime::FromMicros(5000),
        use_init_marker));
  }

  void VerifySubDocument(SubDocKey subdoc_key, HybridTime ht, string subdoc_string) {
    SubDocument doc_from_rocksdb;
    bool subdoc_found_in_rocksdb = false;

    SCOPED_TRACE("\n" + GetStackTrace(StackTraceLineFormat::CLION_CLICKABLE) + "\n" +
                 DocDBDebugDumpToStr());

    EXPECT_OK(GetSubDocument(rocksdb(), subdoc_key, &doc_from_rocksdb,
                             &subdoc_found_in_rocksdb, rocksdb::kDefaultQueryId, ht));
    if (subdoc_string.empty()) {
      EXPECT_FALSE(subdoc_found_in_rocksdb);
      return;
    }
    EXPECT_TRUE(subdoc_found_in_rocksdb);
    EXPECT_STR_EQ_VERBOSE_TRIMMED(subdoc_string, doc_from_rocksdb.ToString());

  }

  // Tries to read some documents from the DB that is assumed to be in a state described by
  // kPredefinedDBStateDebugDumpStr, and verifies the result of those reads. Only the latest logical
  // state of documents matters for this check, so it is OK to call this after compacting previous
  // history.
  void CheckExpectedLatestDBState();

  // Checks bloom filter useful counter increment to be in range [1;expected_max_increment].
  // Updates total_useful.
  void CheckBloom(const int expected_max_increment, int* total_useful) {
    if (FLAGS_use_docdb_aware_bloom_filter) {
      const auto total_useful_updated =
          options().statistics->getTickerCount(rocksdb::BLOOM_FILTER_USEFUL);
      if (expected_max_increment > 0) {
        ASSERT_GT(total_useful_updated, *total_useful);
        ASSERT_LE(total_useful_updated, *total_useful + expected_max_increment);
        *total_useful = total_useful_updated;
      } else {
        ASSERT_EQ(*total_useful, total_useful_updated);
      }
    }
  }

  InetAddress GetInetAddress(const std::string& strval) {
    InetAddress addr;
    CHECK_OK(addr.FromString(strval));
    return addr;
  }

  void InsertInet(const std::string strval) {
    const DocKey doc_key(PrimitiveValues("mydockey"));
    KeyBytes encoded_doc_key(doc_key.Encode());
    ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(GetInetAddress(strval))),
                           PrimitiveValue(),
                           HybridTime::FromMicros(1000),
                           InitMarkerBehavior::OPTIONAL));
  }

};

class DocDBTestWithoutBlockCache : public DocDBTest {
 protected:
  size_t block_cache_size() const override { return 0; }
};

// Static constant initialization should be completely independent (cannot initialize one using the
// other).
const DocKey DocDBTest::kDocKey1(PrimitiveValues("row1", 11111));
const DocKey DocDBTest::kDocKey2(PrimitiveValues("row2", 22222));
const KeyBytes DocDBTest::kEncodedDocKey1(DocKey(PrimitiveValues("row1", 11111)).Encode());
const KeyBytes DocDBTest::kEncodedDocKey2(DocKey(PrimitiveValues("row2", 22222)).Encode());
const Schema DocDBTest::kSchemaForIteratorTests({
        ColumnSchema("a", DataType::STRING, /* is_nullable = */ false),
        ColumnSchema("b", DataType::INT64, false),
        // Non-key columns
        ColumnSchema("c", DataType::STRING, true),
        ColumnSchema("d", DataType::INT64, true),
        ColumnSchema("e", DataType::STRING, true)
    }, {
        ColumnId(10),
        ColumnId(20),
        ColumnId(30),
        ColumnId(40),
        ColumnId(50)
    }, 2);
Schema DocDBTest::kProjectionForIteratorTests;

void DocDBTest::TestInsertion(
    const DocPath doc_path,
    const PrimitiveValue& value,
    HybridTime hybrid_time,
    string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb());
  // Set write id to zero on the write path.
  ASSERT_OK(dwb.SetPrimitive(doc_path, value));
  ASSERT_OK(WriteToRocksDB(dwb, hybrid_time));
  string dwb_str;
  ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
  EXPECT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb_str);
}

void DocDBTest::TestDeletion(
    DocPath doc_path,
    HybridTime hybrid_time,
    string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb());
  // Set write id to zero on the write path.
  ASSERT_OK(dwb.DeleteSubDoc(doc_path));
  ASSERT_OK(WriteToRocksDB(dwb, hybrid_time));
  string dwb_str;
  ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
  EXPECT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb_str);
}

void DocDBTest::CheckExpectedLatestDBState() {
  const SubDocKey subdoc_key(DocKey(PrimitiveValues("mydockey", 123456)));

  // Verify that the latest state of the document as seen by our "document walking" facility has
  // not changed.
  string doc_str;
  ASSERT_OK(DebugWalkDocument(subdoc_key.Encode(), &doc_str));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(kPredefinedDocumentDebugDumpStr,
                                doc_str);

  SubDocument subdoc;
  bool doc_found = false;
  ASSERT_OK(GetSubDocument(rocksdb(), subdoc_key, &subdoc, &doc_found,
                           rocksdb::kDefaultQueryId));
  ASSERT_TRUE(doc_found);
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
{
  "subkey_a": "value_a",
  "subkey_b": {
    "subkey_c": "value_bc_prime"
  }
}
      )#",
      subdoc.ToString()
  );
}

// ------------------------------------------------------------------------------------------------

TEST_F(DocDBTest, IntentEncodingTest) {
  Uuid uuid(Uuid::Generate());
  SubDocKey subdoc_key(DocKey(PrimitiveValues("test_dockey")),
      PrimitiveValue("test_subdoc_key"), HybridTime(10));
  Intent intent(subdoc_key,
      IntentType::kSnapshotParentWrite, uuid, Value(PrimitiveValue("test_intent_value")));
  ASSERT_EQ("Intent(" + subdoc_key.ToString() + ", kSnapshotParentWrite, " + uuid.ToString()
      + ", \"test_intent_value\")", intent.ToString());
  string encoded_intent_key = intent.EncodeKey();
  string encoded_intent_value = intent.EncodeValue();
  Intent decoded_intent;
  ASSERT_OK(decoded_intent.DecodeFromKey(Slice(encoded_intent_key)));
  ASSERT_OK(decoded_intent.DecodeFromValue(Slice(encoded_intent_value)));
  ASSERT_EQ(intent.ToString(), decoded_intent.ToString());
}

TEST_F(DocDBTest, DocPathTest) {
  DocKey doc_key(PrimitiveValues("mydockey", 10, "mydockey", 20));
  DocPath doc_path(doc_key.Encode(), "first_subkey", 123);
  ASSERT_EQ(2, doc_path.num_subkeys());
  ASSERT_EQ("\"first_subkey\"", doc_path.subkey(0).ToString());
  ASSERT_EQ("123", doc_path.subkey(1).ToString());
}

TEST_F(DocDBTest, HistoryCompactionFirstRowHandlingRegression) {
  // A regression test for a bug in an initial version of compaction cleanup.
  const DocKey doc_key(PrimitiveValues("mydockey", 123456));
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value1"),
                         HybridTime::FromMicros(1000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value2"),
                         HybridTime::FromMicros(2000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value3"),
                         HybridTime::FromMicros(3000)));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key),
                         PrimitiveValue(ValueType::kObject),
                         HybridTime::FromMicros(4000)));
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=4000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=1000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(p=3000)]) -> "value3"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(p=2000)]) -> "value2"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(p=1000, w=1)]) -> "value1"
      )#");
  CompactHistoryBefore(HybridTime::FromMicros(3500));
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=4000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=1000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(p=3000)]) -> "value3"
      )#");
}

TEST_F(DocDBTest, SetPrimitiveYQL) {
  const DocKey doc_key(PrimitiveValues("mydockey", 123456));
  SetupRocksDBState(doc_key.Encode(), InitMarkerBehavior::OPTIONAL);
  AssertDocDbDebugDumpStrEq(
      R"#(
          SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=1000)]) -> {}
          SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "1"; HT(p=4000)]) -> "3"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "1"; HT(p=1000, w=1)]) -> "1"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "2"; HT(p=2000)]) -> 11
          SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "2"; HT(p=1000, w=2)]) -> "2"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["a", "3"; HT(p=4000, w=1)]) -> "4"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b"; HT(p=3000)]) -> {}
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "c", "1"; HT(p=1000, w=3)]) -> "3"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "d", "1"; HT(p=1000, w=4)]) -> "5"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "d", "2"; HT(p=1000, w=5)]) -> "6"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "1"; HT(p=3000, w=1)]) -> "8"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "2"; HT(p=5000)]) -> DEL
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "e", "2"; HT(p=3000, w=2)]) -> "9"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["b", "y"; HT(p=3000, w=3)]) -> "10"
          SubDocKey(DocKey([], ["mydockey", 123456]), ["u"; HT(p=1000, w=6)]) -> "7"
     )#");
}

// This tests GetSubDocument without init markers. Basic Test tests with init markers.
TEST_F(DocDBTest, GetSubDocumentTest) {
  const DocKey doc_key(PrimitiveValues("mydockey", 123456));
  SetupRocksDBState(doc_key.Encode(), InitMarkerBehavior::OPTIONAL);

  // We will test the state of the entire document after every operation, using timestamps
  // 500, 1500, 2500, 3500, 4500, 5500.

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(500), "");

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(1500),
      R"#(
{
  "a": {
    "1": "1",
    "2": "2"
  },
  "b": {
    "c": {
      "1": "3"
    },
    "d": {
      "1": "5",
      "2": "6"
    }
  },
  "u": "7"
}
      )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(2500),
      R"#(
{
  "a": {
    "1": "1",
    "2": 11
  },
  "b": {
    "c": {
      "1": "3"
    },
    "d": {
      "1": "5",
      "2": "6"
    }
  },
  "u": "7"
}
      )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(3500),
      R"#(
{
  "a": {
    "1": "1",
    "2": 11
  },
  "b": {
    "e": {
      "1": "8",
      "2": "9"
    },
    "y": "10"
  },
  "u": "7"
}
      )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(4500),
      R"#(
{
  "a": {
    "1": "3",
    "2": 11,
    "3": "4"
  },
  "b": {
    "e": {
      "1": "8",
      "2": "9"
    },
    "y": "10"
  },
  "u": "7"
}
      )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime::FromMicros(5500),
      R"#(
{
  "a": {
    "1": "3",
    "2": 11,
    "3": "4"
  },
  "b": {
    "e": {
      "1": "8"
    },
    "y": "10"
  },
  "u": "7"
}
      )#");

  // Test the evolution of SubDoc root.b at various timestamps.

  VerifySubDocument(SubDocKey(doc_key, PrimitiveValue("b")), HybridTime::FromMicros(500), "");

  VerifySubDocument(SubDocKey(doc_key, PrimitiveValue("b")), HybridTime::FromMicros(2500),
      R"#(
{
  "c": {
    "1": "3"
  },
  "d": {
    "1": "5",
    "2": "6"
  }
}
      )#");

  VerifySubDocument(SubDocKey(doc_key, PrimitiveValue("b")), HybridTime::FromMicros(3500),
      R"#(
{
  "e": {
    "1": "8",
    "2": "9"
  },
  "y": "10"
}
      )#");

  VerifySubDocument(SubDocKey(doc_key, PrimitiveValue("b")), HybridTime::FromMicros(5500),
      R"#(
{
  "e": {
    "1": "8"
  },
  "y": "10"
}
      )#");

  VerifySubDocument(SubDocKey(
      doc_key, PrimitiveValue("b"), PrimitiveValue("d")), HybridTime::FromMicros(10000), "");

  VerifySubDocument(SubDocKey(doc_key, PrimitiveValue("b"), PrimitiveValue("d")),
                    HybridTime::FromMicros(2500),
        R"#(
  {
    "1": "5",
    "2": "6"
  }
        )#");

}

TEST_F(DocDBTest, ListInsertAndGetTest) {
  SubDocument parent;
  SubDocument list({PrimitiveValue(10), PrimitiveValue(2)});
  DocKey doc_key(PrimitiveValues("list_test", 231));
  KeyBytes encoded_doc_key = doc_key.Encode();
  parent.SetChild(PrimitiveValue("other"), SubDocument(PrimitiveValue("other_value")));
  parent.SetChild(PrimitiveValue("list2"), SubDocument(list));
  ASSERT_OK(InsertSubDocument(
      DocPath(encoded_doc_key), parent, HybridTime(100), InitMarkerBehavior::OPTIONAL));

  // GetSubDocument Doesn't know that this is an array so it is returned as an object for now.
  VerifySubDocument(SubDocKey(doc_key), HybridTime(250),
        R"#(
  {
    "list2": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "other": "other_value"
  }
        )#");

  ASSERT_OK(ExtendSubDocument(DocPath(encoded_doc_key, PrimitiveValue("list1")),
      SubDocument({PrimitiveValue(1), PrimitiveValue("3"), PrimitiveValue(2), PrimitiveValue(2)}),
      HybridTime(200), InitMarkerBehavior::OPTIONAL));

  VerifySubDocument(SubDocKey(doc_key), HybridTime(250),
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

  AssertDocDbDebugDumpStrEq(
        R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT(p=0, l=100)]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); HT(p=0, l=200)]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); HT(p=0, l=200, w=1)]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); HT(p=0, l=200, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); HT(p=0, l=200, w=3)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); HT(p=0, l=100, w=1)]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=100, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["other"; HT(p=0, l=100, w=3)]) -> "other_value"
        )#");

  ASSERT_OK(ExtendList(DocPath(encoded_doc_key, PrimitiveValue("list2")),
    SubDocument({PrimitiveValue(5), PrimitiveValue(2)}), ListExtendOrder::PREPEND,
    HybridTime(300), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(ExtendList(DocPath(encoded_doc_key, PrimitiveValue("list2")),
    SubDocument({PrimitiveValue(7), PrimitiveValue(4)}), ListExtendOrder::APPEND,
    HybridTime(400), InitMarkerBehavior::OPTIONAL));

AssertDocDbDebugDumpStrEq(
        R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT(p=0, l=100)]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); HT(p=0, l=200)]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); HT(p=0, l=200, w=1)]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); HT(p=0, l=200, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); HT(p=0, l=200, w=3)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); HT(p=0, l=300, w=1)]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); HT(p=0, l=300)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); HT(p=0, l=100, w=1)]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=100, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); HT(p=0, l=400)]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); HT(p=0, l=400, w=1)]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["other"; HT(p=0, l=100, w=3)]) -> "other_value"
        )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime(150),
        R"#(
  {
    "list2": {
      ArrayIndex(1): 10,
      ArrayIndex(2): 2
    },
    "other": "other_value"
  }
        )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime(450),
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

  vector<int> indexes = {2, 4};
  vector<SubDocument> values = {
      SubDocument(PrimitiveValue(ValueType::kTombstone)), SubDocument(PrimitiveValue(17))};
  ASSERT_OK(ReplaceInList(DocPath(encoded_doc_key, PrimitiveValue("list2")),
      indexes, values,  HybridTime(460), HybridTime(500), rocksdb::kDefaultQueryId));

  AssertDocDbDebugDumpStrEq(
        R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT(p=0, l=100)]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); HT(p=0, l=200)]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); HT(p=0, l=200, w=1)]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); HT(p=0, l=200, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); HT(p=0, l=200, w=3)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); HT(p=0, l=300, w=1)]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); HT(p=0, l=500)]) -> DEL
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); HT(p=0, l=300)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); HT(p=0, l=100, w=1)]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=500, w=1)]) -> 17
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=100, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); HT(p=0, l=400)]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); HT(p=0, l=400, w=1)]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["other"; HT(p=0, l=100, w=3)]) -> "other_value"
        )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime(550),
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

  SubDocKey sub_doc_key(doc_key, PrimitiveValue("list3"));
  KeyBytes encoded_sub_doc_key = sub_doc_key.Encode();
  SubDocument list3({PrimitiveValue(31), PrimitiveValue(32)});

  ASSERT_OK(InsertSubDocument(
      DocPath(encoded_sub_doc_key), list3, HybridTime(100), InitMarkerBehavior::OPTIONAL));

  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["list_test", 231]), [HT(p=0, l=100)]) -> {}
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(3); HT(p=0, l=200)]) -> 1
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(4); HT(p=0, l=200, w=1)]) -> "3"
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(5); HT(p=0, l=200, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list1", ArrayIndex(6); HT(p=0, l=200, w=3)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-8); HT(p=0, l=300, w=1)]) -> 5
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); HT(p=0, l=500)]) -> DEL
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(-7); HT(p=0, l=300)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(1); HT(p=0, l=100, w=1)]) -> 10
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=500, w=1)]) -> 17
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(2); HT(p=0, l=100, w=2)]) -> 2
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(9); HT(p=0, l=400)]) -> 7
SubDocKey(DocKey([], ["list_test", 231]), ["list2", ArrayIndex(10); HT(p=0, l=400, w=1)]) -> 4
SubDocKey(DocKey([], ["list_test", 231]), ["list3"; HT(p=0, l=100)]) -> []
SubDocKey(DocKey([], ["list_test", 231]), ["list3", ArrayIndex(11); HT(p=0, l=100, w=1)]) -> 31
SubDocKey(DocKey([], ["list_test", 231]), ["list3", ArrayIndex(12); HT(p=0, l=100, w=2)]) -> 32
SubDocKey(DocKey([], ["list_test", 231]), ["other"; HT(p=0, l=100, w=3)]) -> "other_value"
        )#");

  VerifySubDocument(SubDocKey(doc_key), HybridTime(550),
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

TEST_F(DocDBTest, ExpiredValueCompactionTest) {
  const DocKey doc_key(PrimitiveValues("k1"));
  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const MonoDelta two_ms = MonoDelta::FromMilliseconds(2);
  const HybridTime t0 = HybridTime::FromMicros(1000);
  HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, two_ms);
  HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, two_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
      Value(PrimitiveValue("v11"), one_ms), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
      PrimitiveValue("v14"), t2));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
      Value(PrimitiveValue("v21"), MonoDelta::FromMilliseconds(3)), t0));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
      PrimitiveValue("v24"), t2));

  // Note: HT(p=1000) + 4ms = HT(p=5000)
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["k1"]), [HT(p=1000)]) -> {}
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT(p=5000)]) -> "v14"
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT(p=1000, w=1)]) -> "v11"; ttl: 0.001s
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=5000)]) -> "v24"
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=1000)]) -> "v21"; ttl: 0.003s
      )#");
  CompactHistoryBefore(t1);
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(p=1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s1"; HT(p=5000)]) -> "v14"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=5000)]) -> "v24"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=1000)]) -> "v21"; ttl: 0.003s
      )#");
}

TEST_F(DocDBTest, TTLCompactionTest) {
  const DocKey doc_key(PrimitiveValues("k1"));
  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const HybridTime t0 = HybridTime::FromMicros(1000);
  HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, one_ms);
  HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, one_ms);
  HybridTime t3 = server::HybridClock::AddPhysicalTimeToHybridTime(t2, one_ms);
  HybridTime t4 = server::HybridClock::AddPhysicalTimeToHybridTime(t3, one_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
  // First row.
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key,
                                 PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn)),
                         Value(PrimitiveValue(), MonoDelta::FromMilliseconds(1)), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(0))),
                         Value(PrimitiveValue("v1"), MonoDelta::FromMilliseconds(2)), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue("v2"), MonoDelta::FromMilliseconds(3)), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue("v3"), Value::kMaxTtl), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue("v4"), Value::kMaxTtl), t0,
                         InitMarkerBehavior::OPTIONAL));
  // Second row.
  const DocKey doc_key_row2(PrimitiveValues("k2"));
  KeyBytes encoded_doc_key_row2(doc_key_row2.Encode());
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2,
                                 PrimitiveValue::SystemColumnId(SystemColumnIds::kLivenessColumn)),
                         Value(PrimitiveValue(), MonoDelta::FromMilliseconds(3)), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2, PrimitiveValue(ColumnId(0))),
                         Value(PrimitiveValue("v1"), MonoDelta::FromMilliseconds(2)), t0,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key_row2, PrimitiveValue(ColumnId(1))),
                         Value(PrimitiveValue("v2"), MonoDelta::FromMilliseconds(1)), t0,
                         InitMarkerBehavior::OPTIONAL));
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 0.001s
SubDocKey(DocKey([], ["k1"]), [ColumnId(0); HT(p=1000)]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT(p=1000)]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 0.003s
SubDocKey(DocKey([], ["k2"]), [ColumnId(0); HT(p=1000)]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k2"]), [ColumnId(1); HT(p=1000)]) -> "v2"; ttl: 0.001s
      )#");

  CompactHistoryBefore(t2);

  // Liveness column is gone for row1, v2 gone for row2.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(0); HT(p=1000)]) -> "v1"; ttl: 0.002s
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT(p=1000)]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 0.003s
SubDocKey(DocKey([], ["k2"]), [ColumnId(0); HT(p=1000)]) -> "v1"; ttl: 0.002s
      )#");

  CompactHistoryBefore(t3);

  // v1 is gone.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(1); HT(p=1000)]) -> "v2"; ttl: 0.003s
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
SubDocKey(DocKey([], ["k2"]), [SystemColumnId(0); HT(p=1000)]) -> null; ttl: 0.003s
      )#");

  CompactHistoryBefore(t4);
  // v2 is gone for row 1, liveness column gone for row 2.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
      )#");

  // Delete values.
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(2))),
                         Value(PrimitiveValue(ValueType::kTombstone), Value::kMaxTtl), t1,
                         InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue(ColumnId(3))),
                         Value(PrimitiveValue(ValueType::kTombstone), Value::kMaxTtl), t1,
                         InitMarkerBehavior::OPTIONAL));

  // Values are now marked with tombstones.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=2000)]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=2000)]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
      )#");

  CompactHistoryBefore(t0);
  // Nothing is removed.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=2000)]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(2); HT(p=1000)]) -> "v3"
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=2000)]) -> DEL
SubDocKey(DocKey([], ["k1"]), [ColumnId(3); HT(p=1000)]) -> "v4"
      )#");

  CompactHistoryBefore(t1);
  // Next compactions removes everything.
  AssertDocDbDebugDumpStrEq(
      R"#(
      )#");
}

TEST_F(DocDBTest, TableTTLCompactionTest) {
  const DocKey doc_key(PrimitiveValues("k1"));
  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const HybridTime t0 = HybridTime::FromMicros(1000);
  HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, one_ms);
  HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, one_ms);
  HybridTime t3 = server::HybridClock::AddPhysicalTimeToHybridTime(t2, one_ms);
  HybridTime t4 = server::HybridClock::AddPhysicalTimeToHybridTime(t3, one_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
      ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
                             Value(PrimitiveValue("v1"), MonoDelta::FromMilliseconds(1)), t0));
      ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
                             Value(PrimitiveValue("v2"), Value::kMaxTtl), t0));
      ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s3")),
                             Value(PrimitiveValue("v3"), MonoDelta::FromMilliseconds(0)), t1));
      ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s4")),
                             Value(PrimitiveValue("v4"), MonoDelta::FromMilliseconds(3)), t0));
  // Note: HT(p=1000) + 1ms = HT(p=4097000)
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["k1"]), [HT(p=1000)]) -> {}
      SubDocKey(DocKey([], ["k1"]), ["s1"; HT(p=1000, w=1)]) -> "v1"; ttl: 0.001s
      SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=1000)]) -> "v2"
      SubDocKey(DocKey([], ["k1"]), ["s3"; HT(p=2000)]) -> "v3"; ttl: 0.000s
      SubDocKey(DocKey([], ["k1"]), ["s4"; HT(p=1000)]) -> "v4"; ttl: 0.003s
      )#");
  SetTableTTL(2);
  CompactHistoryBefore(t2);

  // v1 compacted due to column level ttl.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(p=1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(p=1000)]) -> "v2"
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(p=2000)]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT(p=1000)]) -> "v4"; ttl: 0.003s
      )#");

  CompactHistoryBefore(t3);
  // v2 compacted due to table level ttl.
  // init marker compacted due to table level ttl.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(p=2000)]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT(p=1000)]) -> "v4"; ttl: 0.003s
      )#");

  CompactHistoryBefore(t4);
  // v4 compacted due to column level ttl.
  // v3 stays forever due to ttl being set to 0.
  AssertDocDbDebugDumpStrEq(
      R"#(
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(p=2000)]) -> "v3"; ttl: 0.000s
      )#");
}

TEST_F(DocDBTest, BasicTest) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as '$' (kString), 'I' (kInt64) correspond to members of the enum
  //   ValueType.
  // - Strings are terminated with \x00\x00.
  // - Groups of key components in the document key ("hashed" and "range" components) are terminated
  //   with '!' (kGroupEnd).
  // - 64-bit signed integers are encoded in the key using big-endian format with sign bit
  //   inverted.
  // - HybridTimes are represented as 64-bit unsigned integers with all bits inverted, so that's
  //   where we get a lot of \xff bytes from.

  DocKey string_valued_doc_key(PrimitiveValues("my_key_where_value_is_a_string"));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      // Two zeros indicate the end of a string primitive field, and the '!' indicates the end
      // of the "range" part of the DocKey. There is no "hash" part, because the first
      // PrimitiveValue is not a hash value.
      "\"$my_key_where_value_is_a_string\\x00\\x00!\"",
      string_valued_doc_key.Encode().ToString());

  TestInsertion(
      DocPath(string_valued_doc_key.Encode()),
      PrimitiveValue("value1"),
      HybridTime::FromMicros(1000),
      R"#(1. PutCF('$my_key_where_value_is_a_string\x00\x00\
                    !', '$value1'))#");

  DocKey doc_key(PrimitiveValues("mydockey", 123456));
  KeyBytes encoded_doc_key(doc_key.Encode());

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_a"),
      PrimitiveValue("value_a"),
      HybridTime::FromMicros(2000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_a\x00\x00', '$value_a')
      )#");

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc"),
      HybridTime::FromMicros(3000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00', '$value_bc')
      )#");

  // This only has one insertion, because the object at subkey "subkey_b" already exists.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_d"),
      PrimitiveValue("value_bd"),
      HybridTime::FromMicros(3500),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_d\x00\x00', '$value_bd')
      )#");

  // Delete a non-existent top-level document. We don't expect any tombstones to be created.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_x"),
      HybridTime::FromMicros(4000),
      "");

  // Delete a leaf-level value in a subdocument.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      HybridTime::FromMicros(5000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00', 'X')
      )#");

  // Now delete an entire object.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b"),
      HybridTime::FromMicros(6000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00', 'X')
      )#");

  // Re-insert a value at subkey_b.subkey_c. This should see the tombstone from the previous
  // operation and create a new object at subkey_b at the new hybrid_time, hence two writes.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc_prime"),
      HybridTime::FromMicros(7000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00', '$value_bc_prime')
      )#");

  // Check the final state of the database.
  AssertDocDbDebugDumpStrEq(kPredefinedDBStateDebugDumpStr);
  string doc_str;
  ASSERT_OK(DebugWalkDocument(encoded_doc_key, &doc_str));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(kPredefinedDocumentDebugDumpStr,
                                doc_str);
  CheckExpectedLatestDBState();

  // Compaction cleanup testing.

  ClearLogicalSnapshots();
  CaptureLogicalSnapshot();
  CompactHistoryBefore(HybridTime::FromMicros(5000));
  // The following entry gets deleted because it is invisible at hybrid_time 5000:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=3000)]) -> "value_bc"
  //
  // This entry is deleted because we can always remove deletes at or below the cutoff hybrid_time:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=5000)]) -> DEL
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=2000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(p=2000, w=1)]) -> "value_a"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=7000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=6000)]) -> DEL
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=3000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=7000, w=1)]) -> \
          "value_bc_prime"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(p=3500)]) -> \
          "value_bd"
      )#");
  CheckExpectedLatestDBState();

  CaptureLogicalSnapshot();
  // Perform the next history compaction starting both from the initial state as well as from the
  // state with the first history compaction (at hybrid_time 5000) already performed.
  for (const auto& snapshot : logical_snapshots()) {
    snapshot.RestoreTo(rocksdb());
    CompactHistoryBefore(HybridTime::FromMicros(6000));
    // Now the following entries get deleted, because the entire subdocument at "subkey_b" gets
    // deleted at hybrid_time 6000, so we won't look at these records if we do a scan at HT(p=6000):
    //
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=3000)]) -> {}
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=5000)]) -> DEL
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(p=3500)]) ->
    //     "value_bd"
    //
    // And the deletion itself is removed because it is at the history cutoff hybrid_time:
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=6000)]) -> DEL
    AssertDocDbDebugDumpStrEq(R"#(
        SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
        SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=2000)]) -> {}
        SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(p=2000, w=1)]) -> "value_a"
        SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=7000)]) -> {}
        SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=7000, w=1)]) -> \
            "value_bc_prime"
        )#");
    CheckExpectedLatestDBState();
  }
  CaptureLogicalSnapshot();

  // Also test the next compaction starting with all previously captured states, (1) initial,
  // (2) after a compaction at hybrid_time 5000, and (3) after a compaction at hybrid_time 6000.
  // We are going through snapshots in reverse order so that we end with the initial snapshot that
  // does not have any history trimming done yet.
  for (int i = num_logical_snapshots() - 1; i >= 0; --i) {
    RestoreToRocksDBLogicalSnapshot(i);
    // Test overwriting an entire document with an empty object. This should ideally happen with no
    // reads.
    TestInsertion(
        DocPath(encoded_doc_key),
        PrimitiveValue(ValueType::kObject),
        HybridTime::FromMicros(8000),
        R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !', '{')
        )#");

    string doc_str;
    ASSERT_OK(DebugWalkDocument(encoded_doc_key, &doc_str));
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        "StartSubDocument(SubDocKey(DocKey([], [\"mydockey\", 123456]), [HT(p=8000)]))\n"
        "StartObject\n"
        "EndObject\n"
        "EndSubDocument\n", doc_str);
  }

  // Reset our collection of snapshots now that we've performed one more operation.
  ClearLogicalSnapshots();

  CaptureLogicalSnapshot();
  // This is similar to the kPredefinedDBStateDebugDumpStr, but has an additional overwrite of the
  // document with an empty object at hybrid_time 8000.
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=8000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=2000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(p=2000, w=1)]) -> "value_a"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=7000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=6000)]) -> DEL
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=3000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=7000, w=1)]) -> \
          "value_bc_prime"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=5000)]) -> DEL
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=3000, w=1)]) -> \
          "value_bc"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(p=3500)]) -> \
          "value_bd"
      )#");

  CompactHistoryBefore(HybridTime::FromMicros(7999));
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=8000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=2000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(p=2000, w=1)]) -> "value_a"
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(p=7000)]) -> {}
      SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(p=7000, w=1)]) -> \
          "value_bc_prime"
      )#");
  CaptureLogicalSnapshot();

  // Starting with each snapshot, perform the final history compaction and verify we always get the
  // same result.
  for (int i = 0; i < logical_snapshots().size(); ++i) {
    RestoreToRocksDBLogicalSnapshot(i);
    CompactHistoryBefore(HybridTime::FromMicros(8000));
    AssertDocDbDebugDumpStrEq(R"#(
        SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(p=1000)]) -> "value1"
        SubDocKey(DocKey([], ["mydockey", 123456]), [HT(p=8000)]) -> {}
        )#");
  }
}

TEST_F(DocDBTest, MultiOperationDocWriteBatch) {
  const auto encoded_doc_key = DocKey(PrimitiveValues("a")).Encode();
  DocWriteBatch dwb(rocksdb());
  ASSERT_OK(dwb.SetPrimitive(DocPath(encoded_doc_key, "b"), PrimitiveValue("v1")));
  ASSERT_OK(dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "d"), PrimitiveValue("v2")));
  ASSERT_OK(dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "e"), PrimitiveValue("v3")));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["a"]), [HT(p=1000)]) -> {}
      SubDocKey(DocKey([], ["a"]), ["b"; HT(p=1000, w=1)]) -> "v1"
      SubDocKey(DocKey([], ["a"]), ["c"; HT(p=1000, w=2)]) -> {}
      SubDocKey(DocKey([], ["a"]), ["c", "d"; HT(p=1000, w=3)]) -> "v2"
      SubDocKey(DocKey([], ["a"]), ["c", "e"; HT(p=1000, w=4)]) -> "v3"
      )#");

  string dwb_str;
  ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
  EXPECT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          1. PutCF('$a\x00\x00!', '{')
          2. PutCF('$a\x00\x00!$b\x00\x00', '$v1')
          3. PutCF('$a\x00\x00!$c\x00\x00', '{')
          4. PutCF('$a\x00\x00!$c\x00\x00$d\x00\x00', '$v2')
          5. PutCF('$a\x00\x00!$c\x00\x00$e\x00\x00', '$v3')
      )#", dwb_str);
}

TEST_F(DocDBTest, DocRowwiseIteratorTest) {
  DocWriteBatch dwb(rocksdb());

  // Row 1
  // We don't need any seeks for writes, where column values are primitives.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(30))),
      PrimitiveValue("row1_c"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // Row 2: one null column, one column that gets deleted and overwritten, another that just gets
  // overwritten. No seeks needed for writes.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(20000), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // Deletions normally perform a lookup of the key to see whether it's already there. We will use
  // that to provide the expected result (the number of rows deleted in SQL or whether a key was
  // deleted in Redis). However, because we've just set a value at this path, we don't expect to
  // perform any reads for this deletion.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
      HybridTime::FromMicros(2500), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // The entire subdocument under DocPath(encoded_doc_key2, 40) just got deleted, and that fact
  // should still be in the write batch's cache, so we should not perform a seek to overwrite it.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(30000), HybridTime::FromMicros(3000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(50))),
      PrimitiveValue("row2_e"), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(50))),
      PrimitiveValue("row2_e_prime"), HybridTime::FromMicros(4000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  string dwb_str;
  ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
  SCOPED_TRACE("\nWrite batch:\n" + dwb_str);
  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT(p=1000)]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=1000)]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=3000)]) -> 30000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2500)]) -> DEL
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2000)]) -> 20000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=4000)]) -> "row2_e_prime"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=2000)]) -> "row2_e"
      )#");

  const Schema& schema = kSchemaForIteratorTests;
  const Schema& projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2000));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    const auto& row1 = row_block.row(0);
    ASSERT_FALSE(row_block.row(0).is_null(0));
    ASSERT_EQ("row1_c", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto& row2 = row_block.row(0);

    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row_block.row(0).is_null(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(20000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row2_e", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }

  // Scan at a later hybrid_time.

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(5000));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    // This row is exactly the same as in the previous case. TODO: deduplicate.
    const auto& row1 = row_block.row(0);
    ASSERT_FALSE(row_block.row(0).is_null(0));
    ASSERT_EQ("row1_c", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto& row2 = row_block.row(0);

    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row_block.row(0).is_null(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));

    // These two rows have different values compared to the previous case.
    ASSERT_EQ(30000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row2_e_prime", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }
}

class DocDBTestBoundaryValues : public DocDBTest {
 protected:
  void TestBoundaryValues(size_t flush_rate) {
    struct Trackers {
      MinMaxTracker<int64_t> key_ints;
      MinMaxTracker<std::string> key_strs;
      MinMaxTracker<HybridTime> times;
    };

    DocWriteBatch dwb(rocksdb());
    constexpr int kTotalRows = 1000;
    constexpr std::mt19937_64::result_type kSeed = 2886476510;

    std::mt19937_64 rng(kSeed);
    std::uniform_int_distribution<int64_t> distribution(0, std::numeric_limits<int64_t>::max());

    std::vector<Trackers> trackers;
    for (int i = 0; i != kTotalRows; ++i) {
      if (i % flush_rate == 0) {
        trackers.emplace_back();
        ASSERT_OK(FlushRocksDB());
      }
      auto key_str = "key_" + std::to_string(distribution(rng));
      auto key_int = distribution(rng);
      auto value_str = "value_" + std::to_string(distribution(rng));
      auto time = HybridTime::FromMicros(distribution(rng));
      auto key = DocKey(PrimitiveValues(key_str, key_int)).Encode();
      DocPath path(key);
      ASSERT_OK(SetPrimitive(path, PrimitiveValue(value_str), time, InitMarkerBehavior::OPTIONAL));
      trackers.back().key_ints(key_int);
      trackers.back().key_strs(key_str);
      trackers.back().times(time);
    }

    string dwb_str;
    ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
    SCOPED_TRACE("\nWrite batch:\n" + dwb_str);
    ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));
    ASSERT_OK(FlushRocksDB());

    for (auto i = 0; i != 2; ++i) {
      if (i) {
        ASSERT_OK(ReopenRocksDB());
      }
      std::vector<rocksdb::LiveFileMetaData> files;
      rocksdb()->GetLiveFilesMetaData(&files);
      ASSERT_EQ(trackers.size(), files.size());
      sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
      });

      for (size_t j = 0; j != trackers.size(); ++j) {
        const auto& file = files[j];
        const auto& smallest = file.smallest.user_values;
        const auto& largest = file.largest.user_values;
        {
          auto& times = trackers[j].times;
          DocHybridTime temp;
          ASSERT_OK(GetDocHybridTime(smallest, &temp));
          ASSERT_EQ(times.min, temp.hybrid_time());
          ASSERT_OK(GetDocHybridTime(largest, &temp));
          ASSERT_EQ(times.max, temp.hybrid_time());
        }
        {
          auto& key_ints = trackers[j].key_ints;
          auto& key_strs = trackers[j].key_strs;
          PrimitiveValue temp;
          ASSERT_OK(GetPrimitiveValue(smallest, 0, &temp));
          ASSERT_EQ(PrimitiveValue(key_strs.min), temp);
          ASSERT_OK(GetPrimitiveValue(largest, 0, &temp));
          ASSERT_EQ(PrimitiveValue(key_strs.max), temp);
          ASSERT_OK(GetPrimitiveValue(smallest, 1, &temp));
          ASSERT_EQ(PrimitiveValue(key_ints.min), temp);
          ASSERT_OK(GetPrimitiveValue(largest, 1, &temp));
          ASSERT_EQ(PrimitiveValue(key_ints.max), temp);
        }
      }
    }
  }
};


TEST_F_EX(DocDBTest, BoundaryValues, DocDBTestBoundaryValues) {
  TestBoundaryValues(std::numeric_limits<size_t>::max());
}

TEST_F_EX(DocDBTest, BoundaryValuesMultiFiles, DocDBTestBoundaryValues) {
  TestBoundaryValues(350);
}

TEST_F(DocDBTest, DocRowwiseIteratorDeletedDocumentTest) {
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(30))),
      PrimitiveValue("row1_c"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(20000),  HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));

  // Delete entire row1 document to test that iterator can successfully jump to next document
  // when it finds deleted document.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1), HybridTime::FromMicros(2500), InitMarkerBehavior::OPTIONAL));

  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [HT(p=2500)]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT(p=1000)]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=1000)]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2000)]) -> 20000
      )#");

  const Schema& schema = kSchemaForIteratorTests;
  const Schema& projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2500));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows. Anyway in this specific test we only have one row matching criteria.
    ASSERT_EQ(1, row_block.nrows());

    const auto& row2 = row_block.row(0);

    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));
    ASSERT_TRUE(row2.is_null(2));

    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocDBTest, BloomFilterTest) {
  // Turn off "next instead of seek" optimization, because this test rely on DocDB to do seeks.
  FLAGS_max_nexts_to_avoid_seek = 0;
  // Write batch and flush options.
  DocWriteBatch dwb(rocksdb());
  ASSERT_OK(FlushRocksDB());

  DocKey key1(0, PrimitiveValues("key1"), PrimitiveValues());
  DocKey key2(0, PrimitiveValues("key2"), PrimitiveValues());
  DocKey key3(0, PrimitiveValues("key3"), PrimitiveValues());
  HybridTime ht;

  SubDocument doc_from_rocksdb;
  bool subdoc_found_in_rocksdb = false;
  int total_bloom_useful = 0;

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
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), PrimitiveValue("value")));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), PrimitiveValue("value")));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  ASSERT_OK(FlushRocksDB());

  auto get_doc = [this, &doc_from_rocksdb, &subdoc_found_in_rocksdb](const DocKey& key) {
    ASSERT_OK(GetSubDocument(rocksdb(), SubDocKey(key), &doc_from_rocksdb, &subdoc_found_in_rocksdb,
                             rocksdb::kDefaultQueryId));
  };

  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful));
  ASSERT_NO_FATALS(get_doc(key1));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful));

  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_TRUE(!subdoc_found_in_rocksdb);
  // Bloom filter excluded this file.
  // docdb::GetSubDocument sometimes seeks twice - first time on key2 and second time to advance
  // out of it, because key2 was found.
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));

  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful));

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(2000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), PrimitiveValue("value")));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), PrimitiveValue("value")));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  ASSERT_OK(FlushRocksDB());
  ASSERT_NO_FATALS(get_doc(key1));
  ASSERT_NO_FATALS(CheckBloom(0, &total_bloom_useful));
  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));
  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(3000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), PrimitiveValue("value")));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), PrimitiveValue("value")));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  ASSERT_OK(FlushRocksDB());
  ASSERT_NO_FATALS(get_doc(key1));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));
  ASSERT_NO_FATALS(get_doc(key2));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));
  ASSERT_NO_FATALS(get_doc(key3));
  ASSERT_NO_FATALS(CheckBloom(2, &total_bloom_useful));
}

TEST_F(DocDBTest, MergingIterator) {
  // Test for the case described in https://yugabyte.atlassian.net/browse/ENG-1677.

  // Turn off "next instead of seek" optimization, because this test rely on DocDB to do seeks.
  FLAGS_max_nexts_to_avoid_seek = 0;

  HybridTime ht;
  ASSERT_OK(ht.FromUint64(1000));

  // Put smaller key into SST file.
  DocKey key1(123, PrimitiveValues("key1"), PrimitiveValues());
  DocWriteBatch dwb(rocksdb());
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), PrimitiveValue("value1")));
  ASSERT_OK(WriteToRocksDB(dwb, ht));
  ASSERT_OK(FlushRocksDB());

  // Put bigger key into memtable.
  DocKey key2(234, PrimitiveValues("key2"), PrimitiveValues());
  dwb.Clear();
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), PrimitiveValue("value2")));
  ASSERT_OK(WriteToRocksDB(dwb, ht));

  // Get key2 from DocDB. Bloom filter will skip SST file and it should invalidate SST file
  // iterator in order for MergingIterator to not pickup key1 incorrectly.
  VerifySubDocument(SubDocKey(key2), ht, "\"value2\"");
}

TEST_F(DocDBTest, SetPrimitiveWithInitMarker) {
  DocWriteBatch dwb(rocksdb());
  // Both required and optional init marker should be ok.
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1), PrimitiveValue(ValueType::kObject)));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey1), PrimitiveValue(ValueType::kObject), InitMarkerBehavior::OPTIONAL));
}

TEST_F(DocDBTest, DocRowwiseIteratorTestRowDeletes) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(30))),
                             PrimitiveValue("row1_c"),
                             InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(10000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
                             PrimitiveValue("row1_e"),
                             InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(20000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(2800)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT(p=2500)]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT(p=1000)]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000, w=1)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=2800)]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2800, w=1)]) -> 20000
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  const Schema &projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2800));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());

    ASSERT_OK(iter.NextBlock(&row_block));
    ASSERT_EQ(1, row_block.nrows());

    const auto &row1 = row_block.row(0);

    // ColumnId 30, 40 should be hidden whereas ColumnId 50 should be visible.
    ASSERT_TRUE(row1.is_null(0));
    ASSERT_TRUE(row1.is_null(1));
    ASSERT_FALSE(row1.is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row2 = row_block.row(0);
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_TRUE(row2.is_null(2));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));
  }
}

TEST_F(DocDBTest, DocRowwiseIteratorHasNextIdempotence) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(2800), InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(DeleteSubDoc(DocPath(kEncodedDocKey1), HybridTime::FromMicros(2500),
                         InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT(p=2500)]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=2800)]) -> "row1_e"
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  const Schema &projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2800));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    // Ensure calling HasNext() again doesn't mess up anything.
    ASSERT_TRUE(iter.HasNext());

    ASSERT_OK(iter.NextBlock(&row_block));
    ASSERT_EQ(1, row_block.nrows());

    const auto &row1 = row_block.row(0);

    // ColumnId 40 should be deleted whereas ColumnId 50 should be visible.
    ASSERT_TRUE(row1.is_null(0));
    ASSERT_TRUE(row1.is_null(1));
    ASSERT_FALSE(row1.is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));
  }
}

TEST_F(DocDBTest, DocRowwiseIteratorIncompleteProjection) {
  DocWriteBatch dwb(rocksdb());


  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(10000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
                             PrimitiveValue("row1_e"), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(20000), InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=1000, w=1)]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=1000, w=2)]) -> 20000
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  Schema projection;
  ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames({"c", "d"},
                                                            &projection));
  ScanSpec scan_spec;
  Arena arena(32768, 1048576);
  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2800));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));

    const auto &row1 = row_block.row(0);
    ASSERT_TRUE(row1.is_null(0));
    ASSERT_FALSE(row1.is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));

    // Now find next row.
    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));

    const auto &row2 = row_block.row(0);
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));
    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocDBTest, DocRowwiseIteratorMultipleDeletes) {
  DocWriteBatch dwb(rocksdb());

  MonoDelta ttl = MonoDelta::FromMilliseconds(1);
  MonoDelta ttl_expiry = MonoDelta::FromMilliseconds(2);
  HybridTime read_time = server::HybridClock::AddPhysicalTimeToHybridTime(
      HybridTime::FromMicros(2800), ttl_expiry);

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(30))),
                             PrimitiveValue("row1_c"),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(10000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  // Deletes.
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey2), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));
  dwb.Clear();

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
                             Value(PrimitiveValue("row1_e"), ttl),
                             InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(30))),
                             PrimitiveValue(ValueType::kTombstone),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(20000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(50))),
                             Value(PrimitiveValue("row2_e"), MonoDelta::FromMilliseconds(3)),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2800)));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT(p=2500)]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT(p=1000)]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000, w=1)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=2800)]) -> "row1_e"; ttl: 0.001s
SubDocKey(DocKey([], ["row2", 22222]), [HT(p=2500, w=1)]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(30); HT(p=2800, w=1)]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2800, w=2)]) -> 20000
SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=2800, w=3)]) -> "row2_e"; ttl: 0.003s
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  Schema projection;
  ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames({"c", "e"},
                                                            &projection));

  ScanSpec scan_spec;
  Arena arena(32768, 1048576);
  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), read_time);
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    // Ensure Idempotency.
    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));

    const auto &row2 = row_block.row(0);
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ("row2_e", row2.get_field<DataType::STRING>(1));

    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocDBTest, DocRowwiseIteratorValidColumnNotInProjection) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(10000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(20000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(50))),
                             PrimitiveValue("row2_e"),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(30))),
                             PrimitiveValue("row2_c"),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
                             PrimitiveValue("row1_e"),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2800)));


  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), [HT(p=2500)]) -> DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=2800)]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(30); HT(p=2000, w=1)]) -> "row2_c"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=1000, w=1)]) -> 20000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=2000)]) -> "row2_e"
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  Schema projection;
  ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames({"c", "d"},
                                                            &projection));
  ScanSpec scan_spec;
  Arena arena(32768, 1048576);
  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2800));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());

    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row1 = row_block.row(0);
    ASSERT_TRUE(row1.is_null(0));
    ASSERT_TRUE(row1.is_null(1));

    ASSERT_TRUE(iter.HasNext());

    ASSERT_OK(iter.NextBlock(&row_block));

    const auto &row2 = row_block.row(0);
    ASSERT_FALSE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ("row2_c", row2.get_field<DataType::STRING>(0));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));

    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocDBTest, DocRowwiseIteratorKeyProjection) {
  DocWriteBatch dwb(rocksdb());

  // Row 1
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(40))),
                             PrimitiveValue(10000),
                             InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(50))),
                             PrimitiveValue("row1_e"),
                             InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(WriteToRocksDB(dwb, HybridTime::FromMicros(1000)));

  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=1000, w=1)]) -> "row1_e"
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  Schema projection;
  ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames({"a", "b"},
                                                            &projection, 2));
  ScanSpec scan_spec;
  Arena arena(32768, 1048576);
  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime::FromMicros(2800));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());

    ASSERT_OK(iter.NextBlock(&row_block));

    const auto &row1 = row_block.row(0);
    ASSERT_EQ("row1", row1.get_field_no_nullcheck<DataType::STRING>(0));
    ASSERT_EQ(11111, row1.get_field_no_nullcheck<DataType::INT64>(1));

    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocDBTest, TestInetSortOrder) {
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
  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["mydockey"]), [::1; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [::255.255.255.255; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [::ff:ffff:ffff; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [1.2.3.4; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [180::2978:9018:b288:3f6c; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [2.2.3.4; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [fe80::2978:9018:b288:3f6c; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [255.255.255.255; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [ffff:ffff::; HT(p=1000)]) -> null
      SubDocKey(DocKey([], ["mydockey"]), [ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff; HT(p=1000)]) \
          -> null
      )#");
}

TEST_F(DocDBTest, TestDisambiguationOnWriteId) {
  // Set a column and then delete the entire row in the same write batch. The row disappears.
  DocWriteBatch dwb(rocksdb());
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(ColumnId(10))),
      PrimitiveValue("value1"), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey1), PrimitiveValue(ValueType::kTombstone),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  SubDocKey subdoc_key(kDocKey1);
  SubDocument subdoc;
  bool doc_found = false;
  GetSubDocument(rocksdb(), SubDocKey(kDocKey1), &subdoc, &doc_found,
                 rocksdb::kDefaultQueryId);
  ASSERT_FALSE(doc_found);

  CaptureLogicalSnapshot();
  for (int cutoff_time_ms = 1000; cutoff_time_ms <= 1001; ++cutoff_time_ms) {
    RestoreToLastLogicalRocksDBSnapshot();

    // The row should still be absent after a compaction.
    CompactHistoryBefore(HybridTime::FromMicros(cutoff_time_ms));
    GetSubDocument(rocksdb(), SubDocKey(kDocKey1), &subdoc, &doc_found,
                   rocksdb::kDefaultQueryId);
    ASSERT_FALSE(doc_found);
    AssertDocDbDebugDumpStrEq("");
  }

  // Delete the row first, and then set a column. This row will exist.
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey2), PrimitiveValue(ValueType::kTombstone),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(ColumnId(10))),
      PrimitiveValue("value2"), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2000)));
  GetSubDocument(rocksdb(), SubDocKey(kDocKey2), &subdoc, &doc_found,
                 rocksdb::kDefaultQueryId);
  ASSERT_TRUE(doc_found);

  // The row should still exist after a compaction. The deletion marker should be compacted away.
  CaptureLogicalSnapshot();
  for (int cutoff_time_ms = 2000; cutoff_time_ms <= 2001; ++cutoff_time_ms) {
    RestoreToLastLogicalRocksDBSnapshot();
    CompactHistoryBefore(HybridTime::FromMicros(cutoff_time_ms));
    GetSubDocument(rocksdb(), SubDocKey(kDocKey2), &subdoc, &doc_found,
                   rocksdb::kDefaultQueryId);
    ASSERT_TRUE(doc_found);
    AssertDocDbDebugDumpStrEq(R"#(
        SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(10); HT(p=2000, w=1)]) -> "value2"
        )#");
  }
}

TEST_F(DocDBTest, StaticColumnCompaction) {
  const DocKey hk(0, PrimitiveValues("h1")); // hash key
  const DocKey pk1(hk.hash(), hk.hashed_group(), PrimitiveValues("r1")); // primary key
  const DocKey pk2(hk.hash(), hk.hashed_group(), PrimitiveValues("r2")); //   "      "
  const KeyBytes encoded_hk(hk.Encode());
  const KeyBytes encoded_pk1(pk1.Encode());
  const KeyBytes encoded_pk2(pk2.Encode());

  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const MonoDelta two_ms = MonoDelta::FromMilliseconds(2);
  const HybridTime t0 = HybridTime::FromMicros(1000);
  const HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, two_ms);
  const HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, two_ms);

  // Add some static columns: s1 and s2 with TTL, s3 and s4 without.
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s1")),
      Value(PrimitiveValue("v1"), one_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s2")),
      Value(PrimitiveValue("v2"), two_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s3")),
      Value(PrimitiveValue("v3old")), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s4")),
      Value(PrimitiveValue("v4")), t0, InitMarkerBehavior::OPTIONAL));

  // Add some non-static columns for pk1: c5 and c6 with TTL, c7 and c8 without.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, PrimitiveValue("c5")),
      Value(PrimitiveValue("v51"), one_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, PrimitiveValue("c6")),
      Value(PrimitiveValue("v61"), two_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, PrimitiveValue("c7")),
      Value(PrimitiveValue("v71old")), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, PrimitiveValue("c8")),
      Value(PrimitiveValue("v81")), t0, InitMarkerBehavior::OPTIONAL));

  // More non-static columns for another primary key pk2.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, PrimitiveValue("c5")),
      Value(PrimitiveValue("v52"), one_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, PrimitiveValue("c6")),
      Value(PrimitiveValue("v62"), two_ms), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, PrimitiveValue("c7")),
      Value(PrimitiveValue("v72")), t0, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, PrimitiveValue("c8")),
      Value(PrimitiveValue("v82")), t0, InitMarkerBehavior::OPTIONAL));

  // Update s3 and delete s4 at t1.
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s3")),
      Value(PrimitiveValue("v3new")), t1, InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(DocPath(encoded_hk, PrimitiveValue("s4")),
      Value(PrimitiveValue(ValueType::kTombstone)), t1, InitMarkerBehavior::OPTIONAL));

  // Update c7 of pk1 at t1 also.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk1, PrimitiveValue("c7")),
      Value(PrimitiveValue("v71new")), t1, InitMarkerBehavior::OPTIONAL));

  // Delete c8 of pk2 at t2.
  ASSERT_OK(SetPrimitive(DocPath(encoded_pk2, PrimitiveValue("c8")),
      Value(PrimitiveValue(ValueType::kTombstone)), t2, InitMarkerBehavior::OPTIONAL));

  // Verify before compaction.
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), ["s1"; HT(p=1000)]) -> "v1"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s2"; HT(p=1000)]) -> "v2"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT(p=3000)]) -> "v3new"
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT(p=1000)]) -> "v3old"
SubDocKey(DocKey(0x0000, ["h1"], []), ["s4"; HT(p=3000)]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], []), ["s4"; HT(p=1000)]) -> "v4"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c5"; HT(p=1000)]) -> "v51"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c6"; HT(p=1000)]) -> "v61"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT(p=3000)]) -> "v71new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT(p=1000)]) -> "v71old"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c8"; HT(p=1000)]) -> "v81"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c5"; HT(p=1000)]) -> "v52"; ttl: 0.001s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c6"; HT(p=1000)]) -> "v62"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c7"; HT(p=1000)]) -> "v72"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT(p=5000)]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT(p=1000)]) -> "v82"
      )#");

  // Compact at t1 = HT(p=3000).
  CompactHistoryBefore(t1);

  // Verify after compaction:
  //   s1 -> expired
  //   s4 -> deleted
  //   s3 = v3old -> compacted
  //   pk1.c5 -> expired
  //   pk1.c7 = v71old -> compacted
  //   pk2.c5 -> expired
  AssertDocDbDebugDumpStrEq(R"#(
SubDocKey(DocKey(0x0000, ["h1"], []), ["s2"; HT(p=1000)]) -> "v2"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], []), ["s3"; HT(p=3000)]) -> "v3new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c6"; HT(p=1000)]) -> "v61"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c7"; HT(p=3000)]) -> "v71new"
SubDocKey(DocKey(0x0000, ["h1"], ["r1"]), ["c8"; HT(p=1000)]) -> "v81"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c6"; HT(p=1000)]) -> "v62"; ttl: 0.002s
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c7"; HT(p=1000)]) -> "v72"
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT(p=5000)]) -> DEL
SubDocKey(DocKey(0x0000, ["h1"], ["r2"]), ["c8"; HT(p=1000)]) -> "v82"
      )#");
}

}  // namespace docdb
}  // namespace yb
