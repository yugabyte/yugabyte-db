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

#include <memory>
#include <string>

#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/intent.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace docdb {

class DocRowwiseIteratorTest: public DocDBTestBase {
 protected:
  DocRowwiseIteratorTest() {
    SeedRandom();
  }
  ~DocRowwiseIteratorTest() override {}

  // TODO Could define them out of class, so one line would be enough for them.
  static const KeyBytes kEncodedDocKey1;
  static const KeyBytes kEncodedDocKey2;
  static const Schema kSchemaForIteratorTests;
  static Schema kProjectionForIteratorTests;

  static void SetUpTestCase() {
    ASSERT_OK(kSchemaForIteratorTests.CreateProjectionByNames({"c", "d", "e"},
        &kProjectionForIteratorTests));
  }
};

const KeyBytes DocRowwiseIteratorTest::kEncodedDocKey1(
    DocKey(PrimitiveValues("row1", 11111)).Encode());
const KeyBytes DocRowwiseIteratorTest::kEncodedDocKey2(
    DocKey(PrimitiveValues("row2", 22222)).Encode());
const Schema DocRowwiseIteratorTest::kSchemaForIteratorTests({
    ColumnSchema("a", DataType::STRING, /* is_nullable = */ false),
    ColumnSchema("b", DataType::INT64, false),
    // Non-key columns
    ColumnSchema("c", DataType::STRING, true),
    ColumnSchema("d", DataType::INT64, true),
    ColumnSchema("e", DataType::STRING, true)
}, {
    10_ColId,
    20_ColId,
    30_ColId,
    40_ColId,
    50_ColId
}, 2);
Schema DocRowwiseIteratorTest::kProjectionForIteratorTests;

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorTest) {
  DocWriteBatch dwb(rocksdb());

  // Row 1
  // We don't need any seeks for writes, where column values are primitives.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  // TODO `DocWriteBatch dwb` is empty and doesn't get populated at all, so these asserts are not
  // checking anything. Should probably be moved into `SetPrimitive` with passing number of seeks
  // expected optionally in order to check local_doc_write_batch there. `dwb` should be removed.
  // This is applicable for all other tests below and in DocDBTest.
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // Row 2: one null column, one column that gets deleted and overwritten, another that just gets
  // overwritten. No seeks needed for writes.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(20000), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // Deletions normally perform a lookup of the key to see whether it's already there. We will use
  // that to provide the expected result (the number of rows deleted in SQL or whether a key was
  // deleted in Redis). However, because we've just set a value at this path, we don't expect to
  // perform any reads for this deletion.
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      HybridTime::FromMicros(2500), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  // The entire subdocument under DocPath(encoded_doc_key2, 40) just got deleted, and that fact
  // should still be in the write batch's cache, so we should not perform a seek to overwrite it.
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(30000), HybridTime::FromMicros(3000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e"), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));
  ASSERT_EQ(0, dwb.GetAndResetNumRocksDBSeeks());

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
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

  const Schema &schema = kSchemaForIteratorTests;
  const Schema &projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32_KB, 1_MB);

  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2000));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    const auto &row1 = row_block.row(0);
    ASSERT_FALSE(row_block.row(0).is_null(0));
    ASSERT_EQ("row1_c", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row2 = row_block.row(0);

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
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(5000));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    // This row is exactly the same as in the previous case. TODO: deduplicate.
    const auto &row1 = row_block.row(0);
    ASSERT_FALSE(row_block.row(0).is_null(0));
    ASSERT_EQ("row1_c", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row2 = row_block.row(0);

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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorDeletedDocumentTest) {
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(20000), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));

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

  const Schema &schema = kSchemaForIteratorTests;
  const Schema &projection = kProjectionForIteratorTests;

  ScanSpec scan_spec;

  Arena arena(32_KB, 1_MB);

  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2500));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows. Anyway in this specific test we only have one row matching criteria.
    ASSERT_EQ(1, row_block.nrows());

    const auto &row2 = row_block.row(0);

    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));
    ASSERT_TRUE(row2.is_null(2));

    ASSERT_FALSE(iter.HasNext());
  }
}

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorTestRowDeletes) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c"),
      InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e"),
      InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
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

  Arena arena(32_KB, 1_MB);

  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2800));
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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorHasNextIdempotence) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
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

  Arena arena(32_KB, 1_MB);

  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2800));
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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorIncompleteProjection) {
  DocWriteBatch dwb(rocksdb());


  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e"), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
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
  Arena arena(32_KB, 1_MB);
  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2800));
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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorMultipleDeletes) {
  DocWriteBatch dwb(rocksdb());

  MonoDelta ttl = MonoDelta::FromMilliseconds(1);
  MonoDelta ttl_expiry = MonoDelta::FromMilliseconds(2);
  HybridTime read_time = server::HybridClock::AddPhysicalTimeToHybridTime(
      HybridTime::FromMicros(2800), ttl_expiry);

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c"),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  // Deletes.
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey2), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));
  dwb.Clear();

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      Value(PrimitiveValue("row1_e"), ttl),
      InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(30_ColId)),
      PrimitiveValue(ValueType::kTombstone),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(20000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
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
  Arena arena(32_KB, 1_MB);
  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(), read_time);
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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorValidColumnNotInProjection) {
  DocWriteBatch dwb(rocksdb());

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(20000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(1000)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e"),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey2, PrimitiveValue(30_ColId)),
      PrimitiveValue("row2_c"),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2000)));

  ASSERT_OK(dwb.DeleteSubDoc(DocPath(kEncodedDocKey1), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(WriteToRocksDBAndClear(&dwb, HybridTime::FromMicros(2500)));

  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
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
  Arena arena(32_KB, 1_MB);
  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2800));
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

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorKeyProjection) {
  DocWriteBatch dwb(rocksdb());

  // Row 1
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000),
      InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(dwb.SetPrimitive(DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
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
  Arena arena(32_KB, 1_MB);
  {
    DocRowwiseIterator iter(
        projection, schema, kNonTransactionalOperationContext, rocksdb(),
        HybridTime::FromMicros(2800));
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

namespace {

class TransactionStatusManagerMock : public TransactionStatusManager {
 public:
  HybridTime LocalCommitTime(const TransactionId &id) override {
    return HybridTime::kInvalidHybridTime;
  }

  void RequestStatusAt(
      const TransactionId &id,
      HybridTime time,
      TransactionStatusCallback callback) override {
    auto it = txn_commit_time_.find(id);
    if (it == txn_commit_time_.end()) {
      callback(STATUS_FORMAT(TryAgain, "Unknown transaction id: $0", id));
    } else {
      if (time >= it->second) {
        callback(TransactionStatusResult{TransactionStatus::COMMITTED, it->second});
      } else {
        callback(TransactionStatusResult{TransactionStatus::PENDING, HybridTime::kMin});
      }
    }
  }

  void Commit(const TransactionId& txn_id, HybridTime commit_time) {
    txn_commit_time_.emplace(txn_id, commit_time);
  }

  boost::optional<TransactionMetadata> Metadata(rocksdb::DB* db,
                                                const TransactionId& id) override {
    return boost::none;
  }

  void Abort(const TransactionId& id, TransactionStatusCallback callback) override {
  }

 private:
  std::unordered_map<TransactionId, HybridTime, TransactionIdHash> txn_commit_time_;
};

} // namespace

TEST_F(DocRowwiseIteratorTest, DocRowwiseIteratorResolveWriteIntents) {
  SetTransactionIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

  TransactionStatusManagerMock txn_status_manager;

  Result<TransactionId> txn1 = FullyDecodeTransactionId("0000000000000001");
  ASSERT_OK(txn1);
  Result<TransactionId> txn2 = FullyDecodeTransactionId("0000000000000002");
  ASSERT_OK(txn2);

  SetCurrentTransactionId(*txn1);
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c_t1"), HybridTime::FromMicros(500), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(40000), HybridTime::FromMicros(500), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e_t1"), HybridTime::FromMicros(500), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(42000), HybridTime::FromMicros(500), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e_t1"), HybridTime::FromMicros(500), InitMarkerBehavior::OPTIONAL));
  ResetCurrentTransactionId();

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(30_ColId)),
      PrimitiveValue("row1_c"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(40_ColId)),
      PrimitiveValue(10000), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey1, PrimitiveValue(50_ColId)),
      PrimitiveValue("row1_e"), HybridTime::FromMicros(1000), InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(20000), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));

  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      HybridTime::FromMicros(2500), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(40_ColId)),
      PrimitiveValue(30000), HybridTime::FromMicros(3000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e"), HybridTime::FromMicros(2000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e_prime"), HybridTime::FromMicros(4000), InitMarkerBehavior::OPTIONAL));

  txn_status_manager.Commit(*txn1, HybridTime::FromMicros(3500));

  SetCurrentTransactionId(*txn2);
  ASSERT_OK(DeleteSubDoc(
      DocPath(kEncodedDocKey1),
      HybridTime::FromMicros(4000), InitMarkerBehavior::OPTIONAL));
  ASSERT_OK(SetPrimitive(
      DocPath(kEncodedDocKey2, PrimitiveValue(50_ColId)),
      PrimitiveValue("row2_e_t2"), HybridTime::FromMicros(4000), InitMarkerBehavior::OPTIONAL));
  ResetCurrentTransactionId();
  txn_status_manager.Commit(*txn2, HybridTime::FromMicros(6000));

  AssertDocDbDebugDumpStrEq(R"#(
      SubDocKey(DocKey([], ["row1", 11111]), []) kWeakSnapshotWrite HT(p=500, w=1) -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
      SubDocKey(DocKey([], ["row1", 11111]), []) kStrongSnapshotWrite HT(p=4000) -> \
TransactionId(30303030-3030-3030-3030-303030303032) DEL
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30)]) kStrongSnapshotWrite HT(p=500) -> \
TransactionId(30303030-3030-3030-3030-303030303031) "row1_c_t1"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40)]) kStrongSnapshotWrite HT(p=500) -> \
TransactionId(30303030-3030-3030-3030-303030303031) 40000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50)]) kStrongSnapshotWrite HT(p=500) -> \
TransactionId(30303030-3030-3030-3030-303030303031) "row1_e_t1"
      SubDocKey(DocKey([], ["row2", 22222]), []) kWeakSnapshotWrite HT(p=4000, w=1) -> \
TransactionId(30303030-3030-3030-3030-303030303032) none
      SubDocKey(DocKey([], ["row2", 22222]), []) kWeakSnapshotWrite HT(p=500, w=1) -> \
TransactionId(30303030-3030-3030-3030-303030303031) none
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40)]) kStrongSnapshotWrite HT(p=500) -> \
TransactionId(30303030-3030-3030-3030-303030303031) 42000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) kStrongSnapshotWrite HT(p=4000) -> \
TransactionId(30303030-3030-3030-3030-303030303032) "row2_e_t2"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50)]) kStrongSnapshotWrite HT(p=500) -> \
TransactionId(30303030-3030-3030-3030-303030303031) "row2_e_t1"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(30); HT(p=1000)]) -> "row1_c"
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(40); HT(p=1000)]) -> 10000
      SubDocKey(DocKey([], ["row1", 11111]), [ColumnId(50); HT(p=1000)]) -> "row1_e"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=3000)]) -> 30000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2500)]) -> DEL
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(40); HT(p=2000)]) -> 20000
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=4000)]) -> "row2_e_prime"
      SubDocKey(DocKey([], ["row2", 22222]), [ColumnId(50); HT(p=2000)]) -> "row2_e"
      )#");

  const Schema &schema = kSchemaForIteratorTests;
  const Schema &projection = kProjectionForIteratorTests;
  const auto txn_context = TransactionOperationContext(
      GenerateTransactionId(), &txn_status_manager);

  ScanSpec scan_spec;
  Arena arena(32_KB, 1_MB);

  {
    DocRowwiseIterator iter(
        projection, schema, txn_context, rocksdb(), HybridTime::FromMicros(2000));
    ASSERT_OK(iter.Init(&scan_spec));

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    const auto& row1 = row_block.row(0);
    ASSERT_FALSE(row1.is_null(0));
    ASSERT_EQ("row1_c", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row1.is_null(1));
    ASSERT_EQ(10000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row1.is_null(2));
    ASSERT_EQ("row1_e", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row2 = row_block.row(0);

    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(20000, row2.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row2.is_null(2));
    ASSERT_EQ("row2_e", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }

  // Scan at a later hybrid_time.

  {
    DocRowwiseIterator iter(
        projection, schema, txn_context, rocksdb(), HybridTime::FromMicros(5000));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    const auto &row1 = row_block.row(0);
    ASSERT_FALSE(row1.is_null(0));
    ASSERT_EQ("row1_c_t1", row1.get_field<DataType::STRING>(0));
    ASSERT_FALSE(row1.is_null(1));
    ASSERT_EQ(40000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row1.is_null(2));
    ASSERT_EQ("row1_e_t1", row1.get_field<DataType::STRING>(2));

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    const auto &row2 = row_block.row(0);

    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(42000, row2.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row2.is_null(2));
    ASSERT_EQ("row2_e_prime", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }

  // Scan at a later hybrid_time.

  {
    DocRowwiseIterator iter(
        projection, schema, txn_context, rocksdb(), HybridTime::FromMicros(6000));
    ASSERT_OK(iter.Init(&scan_spec));
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
    // Current implementation of DocRowwiseIterator::NextBlock always returns 1 row if there are
    // next rows.
    ASSERT_EQ(1, row_block.nrows());

    const auto &row2 = row_block.row(0);
    ASSERT_TRUE(row2.is_null(0));
    ASSERT_FALSE(row2.is_null(1));
    ASSERT_EQ(42000, row2.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row2.is_null(2));
    ASSERT_EQ("row2_e_t2", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }
}

}  // namespace docdb
}  // namespace yb
