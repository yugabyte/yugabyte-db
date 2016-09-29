// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb.h"

#include <memory>
#include <string>

#include "rocksdb/db.h"

#include "yb/common/timestamp.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/in_mem_docdb.h"
#include "yb/docdb/docdb-internal.h"
#include "yb/gutil/stringprintf.h"
#include "yb/rocksutil/yb_rocksdb.h"
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

using yb::util::TrimStr;
using yb::util::ApplyEagerLineContinuation;

using rocksdb::WriteOptions;

#define SIMPLE_DEBUG_DOC_VISITOR_METHOD(method_name) \
  Status method_name() override { \
    out_ << __FUNCTION__ << endl; \
    return Status::OK(); \
  }

namespace yb {
namespace docdb {

class DebugDocVisitor : public DocVisitor {
 public:
  DebugDocVisitor() {}
  virtual ~DebugDocVisitor() {}

  Status StartDocument(const DocKey& key) override {
    out_ << __FUNCTION__ << "(" << key << ")" << endl;
    return Status::OK();
  }

  Status VisitKey(const PrimitiveValue& key) override {
    out_ << __FUNCTION__ << "(" << key << ")" << endl;
    return Status::OK();
  }

  Status VisitValue(const PrimitiveValue& value) override {
    out_ << __FUNCTION__ << "(" << value << ")" << endl;
    return Status::OK();
  }

  SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndDocument)
  SIMPLE_DEBUG_DOC_VISITOR_METHOD(StartObject)
  SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndObject)
  SIMPLE_DEBUG_DOC_VISITOR_METHOD(StartArray)
  SIMPLE_DEBUG_DOC_VISITOR_METHOD(EndArray)

  string ToString() {
    return out_.str();
  }

 private:
  std::stringstream out_;
};

class DocDBTest : public YBTest {
 protected:
  DocDBTest() {
    InitRocksDBOptions(&rocksdb_options_, "mytablet", nullptr);
    InitRocksDBWriteOptions(&write_options_);
    string test_dir;
    CHECK_OK(Env::Default()->GetTestDirectory(&test_dir));
    SeedRandom();
    rocksdb_dir_ = JoinPathSegments(test_dir, StringPrintf("mytestdb-%d", rand()));
  }

  void SetUp() override {
    rocksdb::DB* rocksdb = nullptr;
    rocksdb::Status rocksdb_open_status = rocksdb::DB::Open(rocksdb_options_,
        rocksdb_dir_, &rocksdb);
    ASSERT_TRUE(rocksdb_open_status.ok()) << rocksdb_open_status.ToString();
    rocksdb_.reset(rocksdb);
  }

  void TearDown() override {
    rocksdb_.reset(nullptr);
    LOG(INFO) << "Destroying RocksDB database at " << rocksdb_dir_;
    rocksdb::Status destroy_status = rocksdb::DestroyDB(rocksdb_dir_, rocksdb_options_);
    if (!destroy_status.ok()) {
      FAIL() << "Failed to destroy RocksDB database: " << destroy_status.ToString();
    }
  }

 protected:
  void TestInsertion(DocPath doc_path,
                     const PrimitiveValue& value,
                     Timestamp timestamp,
                     string expected_write_batch_str);

  void TestDeletion(DocPath doc_path,
                    Timestamp timestamp,
                    string expected_write_batch_str);

  string DebugDumpDocument(const KeyBytes& encoded_doc_key);

  rocksdb::Options rocksdb_options_;
  rocksdb::WriteOptions write_options_;
  string rocksdb_dir_;
  unique_ptr<rocksdb::DB> rocksdb_;
};

void DocDBTest::TestInsertion(DocPath doc_path,
                              const PrimitiveValue& value,
                              Timestamp timestamp,
                              string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb_.get());
  ASSERT_OK(dwb.SetPrimitive(doc_path, value, timestamp));
  if (dwb.write_batch()->Count() > 0) {
    rocksdb_->Write(write_options_, dwb.write_batch());
  }
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb.ToDebugString());
}

string DocDBTest::DebugDumpDocument(const KeyBytes& encoded_doc_key) {
  DebugDocVisitor doc_visitor;
  EXPECT_OK(ScanDocument(rocksdb_.get(), encoded_doc_key, &doc_visitor));
  return doc_visitor.ToString();
}

void DocDBTest::TestDeletion(DocPath doc_path,
  Timestamp timestamp,
  string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb_.get());
  ASSERT_OK(dwb.DeleteSubDoc(doc_path, timestamp));
  if (dwb.write_batch()->Count() > 0) {
    rocksdb_->Write(write_options_, dwb.write_batch());
  }
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
    dwb.ToDebugString());
}

TEST_F(DocDBTest, DocPathTest) {
  DocKey doc_key(PrimitiveValues("mydockey", 10, "mydockey", 20));
  DocPath doc_path(doc_key.Encode(), "first_subkey", 123);
  ASSERT_EQ(2, doc_path.num_subkeys());
  ASSERT_EQ("\"first_subkey\"", doc_path.subkey(0).ToString());
  ASSERT_EQ("123", doc_path.subkey(1).ToString());
}

TEST_F(DocDBTest, BasicTest) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as \x04, \x05 correspond to instances of the enum ValueType
  // - Strings are terminated with \x00\x00
  // - Groups of key components in the document key ("hashed" and "range" components) are separated
  //   terminated with another \x00.
  // - 64-bit signed integers are encoded using big-endian format with sign bit inverted.
  // - Timestamps are represented as 64-bit unsigned integers with all bits inverted, so that's
  //   where we get a lot of \xff bytes from.

  DocKey string_valued_doc_key(PrimitiveValues("my_key_where_value_is_a_string"));
  TestInsertion(
      DocPath(string_valued_doc_key.Encode()),
      PrimitiveValue("value1"),
      Timestamp(1000),
      R"#(1. PutCF('\x04my_key_where_value_is_a_string\x00\x00\x00\
                    \xff\xff\xff\xff\xff\xff\xfc\x17', '\x04value1'))#");

  DocKey doc_key(PrimitiveValues("mydockey", 123456));
  KeyBytes encoded_doc_key(doc_key.Encode());

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_a"),
      PrimitiveValue("value_a"),
      Timestamp(2000),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/', '@')
2. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_a\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf8/', '\x04value_a')
      )#");

  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc"),
      Timestamp(3000),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf4G', '@')
2. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf4G\
          \x04subkey_c\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf4G', '\x04value_bc')
      )#");

  // This only has one insertion, because the object at subkey "subkey_b" already exists.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_d"),
      PrimitiveValue("value_bd"),
      Timestamp(3500),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf4G\
          \x04subkey_d\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf2S', '\x04value_bd')
      )#");

  // Delete a non-existent top-level document. We don't expect any tombstones to be created.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_x"),
      Timestamp(4000),
      "");

  // Delete a leaf-level value in a subdocument.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      Timestamp(5000),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xf4G\
          \x04subkey_c\x00\x00\xff\xff\xff\xff\xff\xff\xecw', 'B')
      )#");

  // Now delete an entire object.
  TestDeletion(
      DocPath(encoded_doc_key, "subkey_b"),
      Timestamp(6000),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xe8\x8f', 'B')
      )#");

  // Re-insert a value at subkey_b.subkey_c. This should see the tombstone from the previous
  // operation and create a new object at subkey_b at the new timestamp, hence two writes.
  TestInsertion(
      DocPath(encoded_doc_key, "subkey_b", "subkey_c"),
      PrimitiveValue("value_bc_prime"),
      Timestamp(7000),
      R"#(
1. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xe4\xa7', '@')
2. PutCF('\x04mydockey\x00\x00\x05\x80\x00\x00\x00\x00\x01\xe2@\x00\
          \xff\xff\xff\xff\xff\xff\xf8/\
          \x04subkey_b\x00\x00\
          \xff\xff\xff\xff\xff\xff\xe4\xa7\
          \x04subkey_c\x00\x00\
          \xff\xff\xff\xff\xff\xff\xe4\xa7', '\x04value_bc_prime')
      )#");

  // Check the final state of the database.
  std::stringstream debug_dump;
  ASSERT_OK(DocDBDebugDump(rocksdb_.get(), debug_dump));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [TS(1000)]) -> "value1"
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000), "subkey_a", TS(2000)]) -> "value_a"
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000), "subkey_b", TS(7000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), \
          [TS(2000), "subkey_b", TS(7000), "subkey_c", TS(7000)]) -> \
    "value_bc_prime"
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000), "subkey_b", TS(6000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000), "subkey_b", TS(3000)]) -> {}
SubDocKey(DocKey([], ["mydockey", 123456]), [TS(2000), "subkey_b", TS(3000), "subkey_c", TS(5000)]) -> DEL
SubDocKey(DocKey([], ["mydockey", 123456]), \
          [TS(2000), "subkey_b", TS(3000), "subkey_c", TS(3000)]) -> \
    "value_bc"
SubDocKey(DocKey([], ["mydockey", 123456]), \
          [TS(2000), "subkey_b", TS(3000), "subkey_d", TS(3500)]) -> \
    "value_bd"
      )#"),
      debug_dump.str());

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      "StartDocument(DocKey([], [\"mydockey\", 123456]))\n"
      "StartObject\n"
      "VisitKey(\"subkey_a\")\n"
      "VisitValue(\"value_a\")\n"
      "VisitKey(\"subkey_b\")\n"
      "StartObject\n"
      "VisitKey(\"subkey_c\")\n"
      "VisitValue(\"value_bc_prime\")\n"
      "EndObject\n"
      "EndObject\n"
      "EndDocument\n", DebugDumpDocument(encoded_doc_key));

  SubDocument subdoc;
  bool doc_found = false;
  ASSERT_OK(GetDocument(rocksdb_.get(), encoded_doc_key, &subdoc, &doc_found));
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

TEST_F(DocDBTest, MultiOperationDocWriteBatch) {
  DocWriteBatch dwb(rocksdb_.get());
  const auto encoded_doc_key = DocKey(PrimitiveValues("a")).Encode();
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "b"), PrimitiveValue("v1"), Timestamp(1000)));
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "d"), PrimitiveValue("v2"), Timestamp(2000)));
  ASSERT_OK(
      dwb.SetPrimitive(DocPath(encoded_doc_key, "c", "e"), PrimitiveValue("v3"), Timestamp(3000)));

  ASSERT_TRUE(rocksdb_->Write(write_options_, dwb.write_batch()).ok());

  // TODO: we need to be able to do these debug dumps with one line of code.
  std::stringstream debug_dump;
  ASSERT_OK(DocDBDebugDump(rocksdb_.get(), debug_dump));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(R"#(
SubDocKey(DocKey([], ["a"]), [TS(1000)]) -> {}
SubDocKey(DocKey([], ["a"]), [TS(1000), "b", TS(1000)]) -> "v1"
SubDocKey(DocKey([], ["a"]), [TS(1000), "c", TS(2000)]) -> {}
SubDocKey(DocKey([], ["a"]), [TS(1000), "c", TS(2000), "d", TS(2000)]) -> "v2"
SubDocKey(DocKey([], ["a"]), [TS(1000), "c", TS(2000), "e", TS(3000)]) -> "v3"
      )#"),
      debug_dump.str());

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
1. PutCF('\x04a\x00\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17', '@')
2. PutCF('\x04a\x00\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17\
          \x04b\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17', '\x04v1')
3. PutCF('\x04a\x00\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17\
          \x04c\x00\x00\xff\xff\xff\xff\xff\xff\xf8/', '@')
4. PutCF('\x04a\x00\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17\
          \x04c\x00\x00\xff\xff\xff\xff\xff\xff\xf8/\
          \x04d\x00\x00\xff\xff\xff\xff\xff\xff\xf8/', '\x04v2')
5. PutCF('\x04a\x00\x00\x00\xff\xff\xff\xff\xff\xff\xfc\x17\
          \x04c\x00\x00\xff\xff\xff\xff\xff\xff\xf8/\
          \x04e\x00\x00\xff\xff\xff\xff\xff\xff\xf4G', '\x04v3')
          )#"
      ), dwb.ToDebugString());
}

TEST_F(DocDBTest, DocRowwiseIteratorTest) {
  DocWriteBatch dwb(rocksdb_.get());
  WriteOptions write_options;

  const auto encoded_doc_key1 = DocKey(PrimitiveValues("row1", 11111)).Encode();
  const auto encoded_doc_key2 = DocKey(PrimitiveValues("row2", 22222)).Encode();

  // Row 1
  dwb.SetPrimitive(DocPath(encoded_doc_key1, 30), PrimitiveValue("row1_c"), Timestamp(1000));
  dwb.SetPrimitive(DocPath(encoded_doc_key1, 40), PrimitiveValue(10000), Timestamp(1000));
  dwb.SetPrimitive(DocPath(encoded_doc_key1, 50), PrimitiveValue("row1_e"), Timestamp(1000));

  // Row 2: one null column, one column that gets deleted and overwritten, another that just gets
  // overwritten.
  dwb.SetPrimitive(DocPath(encoded_doc_key2, 40), PrimitiveValue(20000), Timestamp(2000));

  dwb.DeleteSubDoc(DocPath(encoded_doc_key2, 40), Timestamp(2500));
  dwb.SetPrimitive(DocPath(encoded_doc_key2, 40), PrimitiveValue(30000), Timestamp(3000));
  dwb.SetPrimitive(DocPath(encoded_doc_key2, 50), PrimitiveValue("row2_e"), Timestamp(2000));
  dwb.SetPrimitive(DocPath(encoded_doc_key2, 50), PrimitiveValue("row2_e'"), Timestamp(4000));
  ASSERT_TRUE(rocksdb_->Write(write_options, dwb.write_batch()).ok());

  std::stringstream debug_dump;
  ASSERT_OK(DocDBDebugDump(rocksdb_.get(), debug_dump));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(R"#(
SubDocKey(DocKey([], ["row1", 11111]), [TS(1000)]) -> {}
SubDocKey(DocKey([], ["row1", 11111]), [TS(1000), 30, TS(1000)]) -> "row1_c"
SubDocKey(DocKey([], ["row1", 11111]), [TS(1000), 40, TS(1000)]) -> 10000
SubDocKey(DocKey([], ["row1", 11111]), [TS(1000), 50, TS(1000)]) -> "row1_e"
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000)]) -> {}
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000), 40, TS(3000)]) -> 30000
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000), 40, TS(2500)]) -> DEL
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000), 40, TS(2000)]) -> 20000
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000), 50, TS(4000)]) -> "row2_e'"
SubDocKey(DocKey([], ["row2", 22222]), [TS(2000), 50, TS(2000)]) -> "row2_e"
      )#"),
      debug_dump.str());

  const Schema schema({
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
      ColumnId(50),
  }, 2);

  Schema projection;
  ASSERT_OK(schema.CreateProjectionByNames( {"c", "d", "e"}, &projection));

  ScanSpec scan_spec;

  Arena arena(32768, 1048576);

  {
    DocRowwiseIterator iter(projection, schema, rocksdb_.get(), Timestamp(2000));
    iter.Init(&scan_spec);

    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
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

    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row_block.row(0).is_null(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    ASSERT_EQ(20000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row2_e", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }

  // Scan at a later timestamp.

  {
    DocRowwiseIterator iter(projection, schema, rocksdb_.get(), Timestamp(5000));
    iter.Init(&scan_spec);
    RowBlock row_block(projection, 10, &arena);

    ASSERT_TRUE(iter.HasNext());
    ASSERT_OK(iter.NextBlock(&row_block));
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

    ASSERT_EQ(1, row_block.nrows());
    ASSERT_TRUE(row_block.row(0).is_null(0));
    ASSERT_FALSE(row_block.row(0).is_null(1));
    // These two rows have different values compared to the previous case.
    ASSERT_EQ(30000, row1.get_field<DataType::INT64>(1));
    ASSERT_FALSE(row_block.row(0).is_null(2));
    ASSERT_EQ("row2_e'", row2.get_field<DataType::STRING>(2));

    ASSERT_FALSE(iter.HasNext());
  }

}

TEST_F(DocDBTest, RandomizedDocDBTest) {
  RandomNumberGenerator rng;  // Using default seed.
  auto random_doc_keys(GenRandomDocKeys(&rng, /* use_hash = */ false, 50));

  auto random_subkeys(GenRandomPrimitiveValues(&rng, 500));

  uint64_t timestamp_counter = Timestamp::kMin.ToUint64() + 1;
  InMemDocDB debug_db_state;

  for (int i = 0; i < 50000; ++i) {
    DOCDB_DEBUG_LOG("Starting iteration i=$0", i);
    DocWriteBatch dwb(rocksdb_.get());
    const auto& doc_key = RandomElementOf(random_doc_keys, &rng);
    const KeyBytes encoded_doc_key(doc_key.Encode());

    const SubDocument* current_doc = debug_db_state.GetDocument(encoded_doc_key);

    bool is_deletion = false;
    if (current_doc != nullptr &&
        current_doc->value_type() != ValueType::kObject) {
      // The entire document is not an object, let's delete it.
      is_deletion = true;
    }

    vector<PrimitiveValue> subkeys;
    if (!is_deletion) {
      for (int j = 0; j < rng() % 10; ++j) {
        if (current_doc != nullptr && current_doc->value_type() != ValueType::kObject) {
          // We can't add any more subkeys because we've found a primitive subdocument.
          break;
        }
        subkeys.emplace_back(RandomElementOf(random_subkeys, &rng));
        if (current_doc != nullptr) {
          current_doc = current_doc->GetChild(subkeys.back());
        }
      }
    }

    DocPath doc_path(encoded_doc_key, subkeys);
    const auto value = GenRandomPrimitiveValue(&rng);
    const Timestamp timestamp(timestamp_counter);

    if (rng() % 100 == 0) {
      is_deletion = true;
    }

    const bool doc_already_exists_in_mem =
        debug_db_state.GetDocument(encoded_doc_key) != nullptr;

    if (is_deletion) {
      DOCDB_DEBUG_LOG("Iteration $0: deleting doc path $1", i, doc_path.ToString());
      ASSERT_OK(dwb.DeleteSubDoc(doc_path, timestamp));
      ASSERT_OK(debug_db_state.DeleteSubDoc(doc_path));
    } else {
      DOCDB_DEBUG_LOG("Iteration $0: setting value at doc path $1 to $2",
                      i, doc_path.ToString(), value.ToString());
      ASSERT_OK(dwb.SetPrimitive(doc_path, value, timestamp));
      ASSERT_OK(debug_db_state.SetPrimitive(doc_path, value));
    }

    if (dwb.write_batch()->Count() > 0) {
      rocksdb_->Write(write_options_, dwb.write_batch());
    }
    SubDocument doc_from_rocksdb;
    bool subdoc_found_in_rocksdb = false;
    ASSERT_OK(
        GetDocument(rocksdb_.get(), encoded_doc_key, &doc_from_rocksdb, &subdoc_found_in_rocksdb));
    const SubDocument* const subdoc_from_mem = debug_db_state.GetDocument(encoded_doc_key);
    if (is_deletion && (
            doc_path.num_subkeys() == 0 ||  // Deleted the entire sub-document,
            !doc_already_exists_in_mem)) {  // ...or the document did not exist in the first place.
      // In this case, after performing the deletion operation, we definitely should not see the
      // top-level document in RocksDB or in the in-memory database.
      ASSERT_FALSE(subdoc_found_in_rocksdb);
      ASSERT_EQ(nullptr, subdoc_from_mem);
    } else {
      // This is not a deletion, or we've deleted a sub-key from a document, but the top-level
      // document should still be there in RocksDB.
      ASSERT_TRUE(subdoc_found_in_rocksdb);
      ASSERT_NE(nullptr, subdoc_from_mem);

      ASSERT_EQ(*subdoc_from_mem, doc_from_rocksdb);
      DOCDB_DEBUG_LOG("Retrieved a document from RocksDB: $0", doc_from_rocksdb.ToString());
      ASSERT_STR_EQ_VERBOSE_TRIMMED(subdoc_from_mem->ToString(), doc_from_rocksdb.ToString());
    }

    ++timestamp_counter;
  }
}

}  // namespace docdb
}  // namespace yb
