// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb.h"

#include <memory>
#include <string>

#include "rocksdb/db.h"
#include "rocksdb/status.h"
#include "rocksdb/util/statistics.h"

#include "yb/common/hybrid_time.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_compaction_filter.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/in_mem_docdb.h"
#include "yb/gutil/stringprintf.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/server/hybrid_clock.h"
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

namespace yb {
namespace docdb {

class DocDBTest : public DocDBTestBase {
 protected:
  DocDBTest() {
    SeedRandom();
  }

  ~DocDBTest() override {
  }

  // This is the baseline state of the database that we set up and come back to as we test various
  // operations.
  static constexpr const char* const kPredefinedDBStateDebugDumpStr =
      R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(6000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(3000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(7000)]) -> "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(5000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(3000)]) -> "value_bc"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(3500)]) -> "value_bd"
      )#";

  static constexpr const char* const kPredefinedDocumentDebugDumpStr =
      "StartSubDocument(SubDocKey(DocKey([], [\"mydockey\", 123456]), [HT(2000)]))\n"
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

  static KeyBytes kEncodedDocKey1;
  static KeyBytes kEncodedDocKey2;
  static Schema kSchemaForIteratorTests;
  static Schema kProjectionForIteratorTests;

  static void SetUpTestCase() {
    kEncodedDocKey1 = DocKey(PrimitiveValues("row1", 11111)).Encode();
    kEncodedDocKey2 = DocKey(PrimitiveValues("row2", 22222)).Encode();
    kSchemaForIteratorTests = Schema({
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
    ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames( {"c", "d", "e"},
        &kProjectionForIteratorTests));
  }

  void TestInsertion(DocPath doc_path,
                     const PrimitiveValue& value,
                     HybridTime hybrid_time,
                     string expected_write_batch_str);

  void TestDeletion(DocPath doc_path,
                    HybridTime hybrid_time,
                    string expected_write_batch_str);

  // Tries to read some documents from the DB that is assumed to be in a state described by
  // kPredefinedDBStateDebugDumpStr, and verifies the result of those reads. Only the latest logical
  // state of documents matters for this check, so it is OK to call this after compacting previous
  // history.
  void CheckExpectedLatestDBState();

  void CheckBloom(const int expected_counter_val) {
    if (FLAGS_use_docdb_aware_bloom_filter) {
      ASSERT_EQ(
          options().statistics->getTickerCount(rocksdb::BLOOM_FILTER_USEFUL), expected_counter_val);
    }
  }
};

KeyBytes DocDBTest::kEncodedDocKey1;
KeyBytes DocDBTest::kEncodedDocKey2;
Schema DocDBTest::kSchemaForIteratorTests;
Schema DocDBTest::kProjectionForIteratorTests;

void DocDBTest::TestInsertion(const DocPath doc_path,
                              const PrimitiveValue& value,
                              HybridTime hybrid_time,
                              string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb());
  ASSERT_NO_FATAL_FAILURE(SetPrimitive(doc_path, value, hybrid_time, &dwb));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb.ToDebugString());
}

void DocDBTest::TestDeletion(DocPath doc_path,
  HybridTime hybrid_time,
  string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb());
  ASSERT_OK(dwb.DeleteSubDoc(doc_path, hybrid_time));
  dwb.WriteToRocksDBInTest(hybrid_time, write_options());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb.ToDebugString());
}

void DocDBTest::CheckExpectedLatestDBState() {
  const KeyBytes encoded_doc_key(DocKey(PrimitiveValues("mydockey", 123456)).Encode());

  // Verify that the latest state of the document as seen by our "document walking" facility has
  // not changed.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(kPredefinedDocumentDebugDumpStr,
                                DebugWalkDocument(encoded_doc_key));

  SubDocument subdoc;
  bool doc_found = false;
  ASSERT_OK(GetSubDocument(rocksdb(), encoded_doc_key, &subdoc, &doc_found));
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
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value1"),
                         HybridTime(1000)));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value2"),
                         HybridTime(2000)));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("subkey1")),
                         PrimitiveValue("value3"),
                         HybridTime(3000)));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key),
                         PrimitiveValue(ValueType::kObject),
                         HybridTime(4000)));
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(4000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(3000)]) -> "value3"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(2000)]) -> "value2"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(1000)]) -> "value1"
      )#");
  CompactHistoryBefore(HybridTime(3500));
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(4000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey1"; HT(3000)]) -> "value3"
      )#");
}

TEST_F(DocDBTest, ExpiredValueCompactionTest) {
  const DocKey doc_key(PrimitiveValues("k1"));
  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const MonoDelta two_ms = MonoDelta::FromMilliseconds(2);
  const HybridTime t0 = HybridTime(1000);
  HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, two_ms);
  HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, two_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
      Value(PrimitiveValue("v11"), one_ms), t0));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
      PrimitiveValue("v14"), t2));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
      Value(PrimitiveValue("v21"), MonoDelta::FromMilliseconds(3)), t0));
  NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
      PrimitiveValue("v24"), t2));
  // Note: HT(1000) + 4ms = HT(16385000)
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s1"; HT(16385000)]) -> "v14"
SubDocKey(DocKey([], ["k1"]), ["s1"; HT(1000)]) -> "v11"; ttl: 0.001s
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(16385000)]) -> "v24"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(1000)]) -> "v21"; ttl: 0.003s
      )#");
  CompactHistoryBefore(t1);
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s1"; HT(16385000)]) -> "v14"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(16385000)]) -> "v24"
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(1000)]) -> "v21"; ttl: 0.003s
      )#");
}

TEST_F(DocDBTest, TableTTLCompactionTest) {
  const DocKey doc_key(PrimitiveValues("k1"));
  const MonoDelta one_ms = MonoDelta::FromMilliseconds(1);
  const HybridTime t0 = HybridTime(1000);
  HybridTime t1 = server::HybridClock::AddPhysicalTimeToHybridTime(t0, one_ms);
  HybridTime t2 = server::HybridClock::AddPhysicalTimeToHybridTime(t1, one_ms);
  HybridTime t3 = server::HybridClock::AddPhysicalTimeToHybridTime(t2, one_ms);
  HybridTime t4 = server::HybridClock::AddPhysicalTimeToHybridTime(t3, one_ms);
  KeyBytes encoded_doc_key(doc_key.Encode());
      NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s1")),
                             Value(PrimitiveValue("v1"), MonoDelta::FromMilliseconds(1)), t0));
      NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s2")),
                             Value(PrimitiveValue("v2"), Value::kMaxTtl), t0));
      NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s3")),
                             Value(PrimitiveValue("v3"), MonoDelta::FromMilliseconds(0)), t1));
      NO_FATALS(SetPrimitive(DocPath(encoded_doc_key, PrimitiveValue("s4")),
                             Value(PrimitiveValue("v4"), MonoDelta::FromMilliseconds(3)), t0));
  // Note: HT(1000) + 2ms = HT(8193000)
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s1"; HT(1000)]) -> "v1"; ttl: 0.001s
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(1000)]) -> "v2"
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(4097000)]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT(1000)]) -> "v4"; ttl: 0.003s
      )#");
  SetTableTTL(2);
  CompactHistoryBefore(t2);

  // v1 compacted due to column level ttl.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s2"; HT(1000)]) -> "v2"
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(4097000)]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT(1000)]) -> "v4"; ttl: 0.003s
      )#");

  CompactHistoryBefore(t3);
  // v2 compacted due to table level ttl.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(4097000)]) -> "v3"; ttl: 0.000s
SubDocKey(DocKey([], ["k1"]), ["s4"; HT(1000)]) -> "v4"; ttl: 0.003s
      )#");

  CompactHistoryBefore(t4);
  // v4 compacted due to column level ttl.
  // v3 stays forever due to ttl being set to 0.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["k1"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["k1"]), ["s3"; HT(4097000)]) -> "v3"; ttl: 0.000s
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
      HybridTime(1000),
      R"#(1. PutCF('$my_key_where_value_is_a_string\x00\x00\
                    !\
                    #\xff\xff\xff\xff\xff\xff\xfc\x17', '$value1'))#");

  DocKey doc_key(PrimitiveValues("mydockey", 123456));
  KeyBytes encoded_doc_key(doc_key.Encode());

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_a"),
      PrimitiveValue("value_a"),
      HybridTime(2000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          #\xff\xff\xff\xff\xff\xff\xf8/', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_a\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf8/', '$value_a')
      )#");

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc"),
      HybridTime(3000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf4G', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf4G', '$value_bc')
      )#");

  // This only has one insertion, because the object at subkey "subkey_b" already exists.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_d"),
      PrimitiveValue("value_bd"),
      HybridTime(3500),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_d\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf2S', '$value_bd')
      )#");

  // Delete a non-existent top-level document. We don't expect any tombstones to be created.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_x"),
      HybridTime(4000),
      "");

  // Delete a leaf-level value in a subdocument.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      HybridTime(5000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xecw', 'X')
      )#");

  // Now delete an entire object.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b"),
      HybridTime(6000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xe8\x8f', 'X')
      )#");

  // Re-insert a value at subkey_b.subkey_c. This should see the tombstone from the previous
  // operation and create a new object at subkey_b at the new hybrid_time, hence two writes.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc_prime"),
      HybridTime(7000),
      R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xe4\xa7', '{')
2. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          $subkey_b\x00\x00\
          $subkey_c\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xe4\xa7', '$value_bc_prime')
      )#");

  // Check the final state of the database.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(kPredefinedDBStateDebugDumpStr);
  ASSERT_STR_EQ_VERBOSE_TRIMMED(kPredefinedDocumentDebugDumpStr,
                                DebugWalkDocument(encoded_doc_key));
  CheckExpectedLatestDBState();

  // Compaction cleanup testing.

  ClearLogicalSnapshots();
  CaptureLogicalSnapshot();
  CompactHistoryBefore(HybridTime(5000));
  // The following entry gets deleted because it is invisible at hybrid_time 5000:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(3000)]) -> "value_bc"
  //
  // This entry is deleted because we can always remove deletes at or below the cutoff hybrid_time:
  // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(5000)]) -> DEL
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(6000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(3000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(7000)]) -> "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(3500)]) -> "value_bd"
      )#");
  CheckExpectedLatestDBState();

  CaptureLogicalSnapshot();
  // Perform the next history compaction starting both from the initial state as well as from the
  // state with the first history compaction (at hybrid_time 5000) already performed.
  for (const auto& snapshot : logical_snapshots()) {
    snapshot.RestoreTo(rocksdb());
    CompactHistoryBefore(HybridTime(6000));
    // Now the following entries get deleted, because the entire subdocument at "subkey_b" gets
    // deleted at hybrid_time 6000, so we won't look at these records if we do a scan at HT(6000):
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(3000)]) -> {}
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(5000)]) -> DEL
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(3500)]) -> "value_bd"
    //
    // And the deletion itself is removed because it is at the history cutoff hybrid_time:
    // SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(6000)]) -> DEL
    AssertDocDbDebugDumpStrEqVerboseTrimmed(
        R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(7000)]) -> "value_bc_prime"
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
        HybridTime(8000),
        R"#(
1. PutCF('$mydockey\x00\x00\
          I\x80\x00\x00\x00\x00\x01\xe2@\
          !\
          #\xff\xff\xff\xff\xff\xff\xe0\xbf', '{')
        )#");

    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        "StartSubDocument(SubDocKey(DocKey([], [\"mydockey\", 123456]), [HT(8000)]))\n"
        "StartObject\n"
        "EndObject\n"
        "EndSubDocument\n", DebugWalkDocument(encoded_doc_key));
  }

  // Reset our collection of snapshots now that we've performed one more operation.
  ClearLogicalSnapshots();

  CaptureLogicalSnapshot();
  // This is similar to the kPredefinedDBStateDebugDumpStr, but has an additional overwrite of the
  // document with an empty object at hybrid_time 8000.
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(8000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(6000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(3000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(7000)]) -> "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(5000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(3000)]) -> "value_bc"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_d"; HT(3500)]) -> "value_bd"
      )#");

  CompactHistoryBefore(HybridTime(7999));
  AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(8000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_a"; HT(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b"; HT(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), ["subkey_b", "subkey_c"; HT(7000)]) -> "value_bc_prime"
      )#");
  CaptureLogicalSnapshot();

  // Starting with each snapshot, perform the final history compaction and verify we always get the
  // same result.
  for (int i = 0; i < logical_snapshots().size(); ++i) {
    RestoreToRocksDBLogicalSnapshot(i);
    CompactHistoryBefore(HybridTime(8000));
    AssertDocDbDebugDumpStrEqVerboseTrimmed(
      R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [HT(8000)]) -> {}
        )#");
  }
}

TEST_F(DocDBTest, MultiOperationDocWriteBatch) {
  DocWriteBatch dwb(rocksdb());
  const auto encoded_doc_key = DocKey(PrimitiveValues("a")).Encode();
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "b"), PrimitiveValue("v1"), HybridTime(1000)));
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "d"), PrimitiveValue("v2"), HybridTime(2000)));
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "e"), PrimitiveValue("v3"), HybridTime(3000)));

  ASSERT_OK(WriteToRocksDB(dwb));

  AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey([], ["a"]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["a"]), ["b"; HT(1000)]) -> "v1"
SubDocKey(DocKey([], ["a"]), ["c"; HT(2000)]) -> {}
SubDocKey(DocKey([], ["a"]), ["c", "d"; HT(2000)]) -> "v2"
SubDocKey(DocKey([], ["a"]), ["c", "e"; HT(3000)]) -> "v3"
      )#");

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
1. PutCF('$a\x00\x00\
          !\
          #\xff\xff\xff\xff\xff\xff\xfc\x17', '{')
2. PutCF('$a\x00\x00\
          !\
          $b\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xfc\x17', '$v1')
3. PutCF('$a\x00\x00\
          !\
          $c\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf8/', '{')
4. PutCF('$a\x00\x00\
          !\
          $c\x00\x00\
          $d\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf8/', '$v2')
5. PutCF('$a\x00\x00\
          !\
          $c\x00\x00\
          $e\x00\x00\
          #\xff\xff\xff\xff\xff\xff\xf4G', '$v3')
          )#"
      ), dwb.ToDebugString());
}

TEST_F(DocDBTest, DocRowwiseIteratorTest) {
  DocWriteBatch dwb(rocksdb());

  // Row 1
  // We only perform one seek to get the hybrid_time of the top-level document. Additional writes to
  // fields within that document do not incur any reads.
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 30), PrimitiveValue("row1_c"),
            HybridTime(1000)));
  ASSERT_EQ(1, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 40), PrimitiveValue(10000),
            HybridTime(1000)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 50), PrimitiveValue("row1_e"),
            HybridTime(1000)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // Row 2: one null column, one column that gets deleted and overwritten, another that just gets
  // overwritten. We should still need one seek, because the document key has changed.
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, 40),
                             PrimitiveValue(20000),
                             HybridTime(2000)));
  ASSERT_EQ(1, dwb.GetAndResetNumRocksDBSeeks());

  // Deletions normally perform a lookup of the key to see whether it's already there. We will use
  // that to provide the expected result (the number of rows deleted in SQL or whether a key was
  // deleted in Redis). However, because we've just set a value at this path, we don't expect to
  // perform any reads for this deletion.
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey2, 40), HybridTime(2500)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // The entire subdocument under DocPath(encoded_doc_key2, 40) just got deleted, and that fact
  // should still be in the write batch's cache, so we should not perform a seek to overwrite it.
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, 40),
                             PrimitiveValue(30000),
                             HybridTime(3000)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(kEncodedDocKey2, 50), PrimitiveValue("row2_e"), HybridTime(2000)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, 50),
      PrimitiveValue("row2_e_prime"), HybridTime(4000)));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  ASSERT_OK(WriteToRocksDB(dwb));

  AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["row1", 11111]), [30; HT(1000)]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [40; HT(1000)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [50; HT(1000)]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["row2", 22222]), [40; HT(3000)]) -> 30000
SubDocKey(DocKey([], ["row2", 22222]), [40; HT(2500)]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [40; HT(2000)]) -> 20000
SubDocKey(DocKey([], ["row2", 22222]), [50; HT(4000)]) -> "row2_e_prime"
SubDocKey(DocKey([], ["row2", 22222]), [50; HT(2000)]) -> "row2_e"
      )#");

  const Schema& schema = kSchemaForIteratorTests;
  const Schema& projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime(2000));
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
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime(5000));
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

TEST_F(DocDBTest, DocRowwiseIteratorDeletedDocumentTest) {
  DocWriteBatch dwb(rocksdb());


  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 30),
                             PrimitiveValue("row1_c"),
                             HybridTime(1000)));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 40),
                             PrimitiveValue(10000),
                             HybridTime(1000)));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, 50),
                             PrimitiveValue("row1_e"),
                             HybridTime(1000)));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, 40),
                             PrimitiveValue(20000),
                             HybridTime(2000)));

  // Delete entire row1 documekillnt to test that iterator can successfully jump to next document
  // when it finds deleted document.
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), HybridTime(2500)));

  ASSERT_OK(WriteToRocksDB(dwb));

  AssertDocDbDebugDumpStrEqVerboseTrimmed(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [HT(2500)]) -> DEL
SubDocKey(DocKey([], ["row1", 11111]), [HT(1000)]) -> {}
SubDocKey(DocKey([], ["row1", 11111]), [30; HT(1000)]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [40; HT(1000)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [50; HT(1000)]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [HT(2000)]) -> {}
SubDocKey(DocKey([], ["row2", 22222]), [40; HT(2000)]) -> 20000
      )#");

  const Schema& schema = kSchemaForIteratorTests;
  const Schema& projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb(), HybridTime(2500));
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
  // Write batch and flush options.
  DocWriteBatch dwb(rocksdb());
  FlushRocksDB();

  DocKey key1(PrimitiveValues("key1"));
  DocKey key2(PrimitiveValues("key2"));
  DocKey key3(PrimitiveValues("key3"));
  HybridTime ht;

  SubDocument doc_from_rocksdb;
  bool subdoc_found_in_rocksdb = false;
  int total_bloom_usage = 0;

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
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), PrimitiveValue("value"), ht));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), PrimitiveValue("value"), ht));
  WriteToRocksDB(dwb);
  FlushRocksDB();

  auto get_doc = [this, &doc_from_rocksdb, &subdoc_found_in_rocksdb](const DocKey& key) {
    ASSERT_OK(
        GetSubDocument(this->rocksdb(), key.Encode(), &doc_from_rocksdb, &subdoc_found_in_rocksdb));
  };

  // Bloom usage starts at the default 0.
  NO_FATALS(CheckBloom(total_bloom_usage));
  NO_FATALS(get_doc(key1));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  NO_FATALS(CheckBloom(total_bloom_usage));

  NO_FATALS(get_doc(key2));
  ASSERT_TRUE(!subdoc_found_in_rocksdb);
  // Bloom filter excluded this file.
  NO_FATALS(CheckBloom(++total_bloom_usage));

  NO_FATALS(get_doc(key3));
  ASSERT_TRUE(subdoc_found_in_rocksdb);
  NO_FATALS(CheckBloom(total_bloom_usage));

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(2000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key1.Encode()), PrimitiveValue("value"), ht));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), PrimitiveValue("value"), ht));
  WriteToRocksDB(dwb);
  FlushRocksDB();
  NO_FATALS(get_doc(key1));
  NO_FATALS(CheckBloom(total_bloom_usage));
  NO_FATALS(get_doc(key2));
  NO_FATALS(CheckBloom(++total_bloom_usage));
  NO_FATALS(get_doc(key3));
  NO_FATALS(CheckBloom(++total_bloom_usage));

  dwb.Clear();
  ASSERT_OK(ht.FromUint64(3000));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key2.Encode()), PrimitiveValue("value"), ht));
  ASSERT_OK(dwb.SetPrimitive(DocPath(key3.Encode()), PrimitiveValue("value"), ht));
  WriteToRocksDB(dwb);
  FlushRocksDB();
  NO_FATALS(get_doc(key1));
  NO_FATALS(CheckBloom(++total_bloom_usage));
  NO_FATALS(get_doc(key2));
  NO_FATALS(CheckBloom(++total_bloom_usage));
  NO_FATALS(get_doc(key3));
  NO_FATALS(CheckBloom(++total_bloom_usage));
}

}  // namespace docdb
}  // namespace yb
