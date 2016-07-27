// Copyright (c) YugaByte, Inc.

#include "yb/docdb/docdb.h"

#include <memory>
#include <string>

#include "rocksdb/db.h"
#include "yb/common/timestamp.h"
#include "yb/gutil/stringprintf.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/path_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::unique_ptr;
using std::string;
using std::endl;
using std::cout;

using yb::util::TrimStr;
using yb::util::ApplyEagerLineContinuation;

using rocksdb::WriteOptions;

#define SIMPLE_DEBUG_DOC_VISITOR_METHOD(method_name) \
  virtual void method_name() override { \
    out_ << __FUNCTION__ << endl; \
  }

namespace yb {
namespace docdb {

class DebugDocVisitor : public DocVisitor {
 public:
  DebugDocVisitor() {}
  virtual ~DebugDocVisitor() {}

  virtual void StartDocument(const DocKey& key) override {
    out_ << __FUNCTION__ << "(" << key << ")" << endl;
  }

  virtual void VisitKey(const PrimitiveValue& key) override {
    out_ << __FUNCTION__ << "(" << key << ")" << endl;
  }

  virtual void VisitValue(const PrimitiveValue& value) override {
    out_ << __FUNCTION__ << "(" << value << ")" << endl;
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
    yb::InitRocksDBOptions(&rocksdb_options_, "mytablet", nullptr);
    string test_dir;
    CHECK_OK(Env::Default()->GetTestDirectory(&test_dir));
    SeedRandom();
    rocksdb_dir_ = JoinPathSegments(test_dir, StringPrintf("mytestdb-%d", rand()));
  }

  virtual void SetUp() override {
    rocksdb::DB* rocksdb = nullptr;
    rocksdb::Status rocksdb_open_status = rocksdb::DB::Open(rocksdb_options_,
        rocksdb_dir_, &rocksdb);
    ASSERT_TRUE(rocksdb_open_status.ok()) << rocksdb_open_status.ToString();
    rocksdb_.reset(rocksdb);
  }

  virtual void TearDown() override {
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

  rocksdb::Options rocksdb_options_;
  string rocksdb_dir_;
  unique_ptr<rocksdb::DB> rocksdb_;
};

void DocDBTest::TestInsertion(DocPath doc_path,
                              const PrimitiveValue& value,
                              Timestamp timestamp,
                              string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb_.get());
  WriteOptions write_options;
  ASSERT_OK(dwb.SetPrimitive(doc_path, value, timestamp));
  if (dwb.write_batch()->Count() > 0) {
    rocksdb_->Write(write_options, dwb.write_batch());
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

void DocDBTest::TestDeletion(DocPath doc_path,
                             Timestamp timestamp,
                             string expected_write_batch_str) {
  DocWriteBatch dwb(rocksdb_.get());
  WriteOptions write_options;
  ASSERT_OK(dwb.DeleteSubDoc(doc_path, timestamp));
  if (dwb.write_batch()->Count() > 0) {
    rocksdb_->Write(write_options, dwb.write_batch());
  }
  ASSERT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
                                dwb.ToDebugString());
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

  // Delete a non-existent subdocument. No changes expected.
  TestDeletion(DocPath(encoded_doc_key, "subkey_x"),
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

  DebugDocVisitor doc_visitor;
  ASSERT_OK(ScanDocument(rocksdb_.get(), encoded_doc_key, &doc_visitor));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
StartDocument(DocKey([], ["mydockey", 123456]))
StartObject
VisitKey("subkey_a")
VisitValue("value_a")
VisitKey("subkey_b")
StartObject
VisitKey("subkey_c")
VisitValue("value_bc_prime")
EndObject
EndObject
EndDocument
      )#", doc_visitor.ToString());
}

}
}
