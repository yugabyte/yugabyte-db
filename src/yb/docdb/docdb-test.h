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

#pragma once

#include "yb/common/ql_value.h"

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_reader_redis.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/walltime.h"
#include "yb/util/debug-util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/stack_trace.h"
#include "yb/util/tsan_util.h"

using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

using yb::util::ApplyEagerLineContinuation;

using namespace std::literals;

DECLARE_bool(TEST_docdb_sort_weak_intents);
DECLARE_bool(use_docdb_aware_bloom_filter);
DECLARE_int32(max_nexts_to_avoid_seek);

#define ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(str) ASSERT_NO_FATALS(AssertDocDbDebugDumpStrEq(str))

namespace yb {
namespace docdb {

using dockv::DocKey;
using dockv::DocPath;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;
using dockv::KeyEntryValues;
using dockv::SubDocKey;
using dockv::MakeDocKey;
using dockv::MakeKeyEntryValues;
using dockv::PrimitiveValue;
using dockv::SubDocument;
using dockv::ValueControlFields;
using dockv::ValueEntryType;

constexpr int64_t kIntKey1 = 123456;
constexpr int64_t kIntKey2 = 789123;

Result<KeyEntryValue> TEST_GetKeyEntryValue(
    const rocksdb::UserBoundaryValues& values, size_t index);

YB_STRONGLY_TYPED_BOOL(InitMarkerExpired);
YB_STRONGLY_TYPED_BOOL(UseIntermediateFlushes);

template <class K, class V>
void AddMapValue(const K& key, const V& value, QLValuePB* parent) {
  *parent->mutable_map_value()->mutable_keys()->Add() = QLValue::Primitive(key);
  *parent->mutable_map_value()->mutable_values()->Add() = QLValue::Primitive(value);
}

template <class K>
void AddMapValue(const K& key, const QLValuePB& value, QLValuePB* parent) {
  *parent->mutable_map_value()->mutable_keys()->Add() = QLValue::Primitive(key);
  *parent->mutable_map_value()->mutable_values()->Add() = value;
}

template <class K>
void SetChild(const K& key, QLValuePB&& value, QLValuePB* parent) {
  *parent->mutable_map_value()->mutable_keys()->Add() = QLValue::Primitive(key);
  *parent->mutable_map_value()->mutable_values()->Add() = std::move(value);
}

class DocDBTest : public DocDBTestBase {
 protected:
  DocDBTest() {
    SeedRandom();
  }

  ~DocDBTest() override {
  }

  Schema CreateSchema() override {
    return Schema();
  }

  virtual void GetSubDoc(
      const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context = TransactionOperationContext(),
      const ReadHybridTime& read_time = ReadHybridTime::Max()) = 0;

  // This is the baseline state of the database that we set up and come back to as we test various
  // operations.
  static constexpr const char *const kPredefinedDBStateDebugDumpStr = R"#(
SubDocKey(DocKey([], ["my_key_where_value_is_a_string"]), [HT{ physical: 1000 }]) -> "value1"
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
      )#";

  static const DocKey kDocdbTestDocKey1;
  static const DocKey kDocdbTestDocKey2;
  static const KeyBytes kEncodedDocdbTestDocKey1;
  static const KeyBytes kEncodedDocdbTestDocKey2;

  void TestInsertion(
      DocPath doc_path,
      const ValueRef& value,
      HybridTime hybrid_time,
      string expected_write_batch_str) {
    auto dwb = MakeDocWriteBatch();
    // Set write id to zero on the write path.
    ASSERT_OK(dwb.SetPrimitive(doc_path, value));
    ASSERT_OK(WriteToRocksDB(dwb, hybrid_time));
    string dwb_str;
    ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
    EXPECT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str), dwb_str);
  }

  void TestDeletion(
      DocPath doc_path,
      HybridTime hybrid_time,
      string expected_write_batch_str) {
    auto dwb = MakeDocWriteBatch();
    // Set write id to zero on the write path.
    ASSERT_OK(dwb.DeleteSubDoc(doc_path));
    ASSERT_OK(WriteToRocksDB(dwb, hybrid_time));
    string dwb_str;
    ASSERT_OK(FormatDocWriteBatch(dwb, &dwb_str));
    EXPECT_STR_EQ_VERBOSE_TRIMMED(ApplyEagerLineContinuation(expected_write_batch_str),
        dwb_str);
  }

  void SetupRocksDBState(KeyBytes encoded_doc_key) {
    QLValuePB root;
    QLValuePB a, b, c, d, e, f, b2;

    // The test plan below:
    // Set root = {a: {1: 1, 2: 2}, b: {c: {1: 3}, d: {1: 5, 2: 6}}, u: 7}
    // Then set root.a.2 = 11
    // Then replace root.b = {e: {1: 8, 2: 9}, y: 10}
    // Then extend root.a by {1: 3, 3: 4}
    // Then Delete root.b.e.2
    // The end result should be {a: {1: 3, 2: 11, 3: 4, x: {}}, b: {e: {1: 8}, y: 10}, u: 7}

    // Constructing top level document: "root"
    AddMapValue("u"s, "7"s, &root);
    AddMapValue("1"s, "1"s, &a);
    AddMapValue("2"s, "2"s, &a);
    AddMapValue("1"s, "3"s, &c);
    AddMapValue("1"s, "5"s, &d);
    AddMapValue("2"s, "6"s, &d);
    SetChild("c", std::move(c), &b);
    SetChild("d", std::move(d), &b);
    SetChild("a", std::move(a), &root);
    SetChild("b", std::move(b), &root);

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
        )#", SubDocument::FromQLValuePB(root, SortingType::kNotSpecified).ToString());

    // Constructing new version of b = b2 to be inserted later.
    AddMapValue("y", "10", &b2);
    AddMapValue("1", "8", &e);
    AddMapValue("2", "9", &e);
    SetChild("e", std::move(e), &b2);

    EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  "e": {
    "1": "8",
    "2": "9"
  },
  "y": "10"
}
      )#", SubDocument::FromQLValuePB(b2, SortingType::kNotSpecified).ToString());

    // Constructing a doc with which we will extend a later
    AddMapValue("1", "3", &f);
    AddMapValue("3", "4", &f);

    EXPECT_STR_EQ_VERBOSE_TRIMMED(R"#(
{
  "1": "3",
  "3": "4"
}
      )#", SubDocument::FromQLValuePB(f, SortingType::kNotSpecified).ToString());

    ASSERT_OK(InsertSubDocument(
        DocPath(encoded_doc_key), ValueRef(root), 1000_usec_ht));
    // The Insert above could have been an Extend with no difference in external behavior.
    // Internally however, an insert writes an extra key (with value tombstone).
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue("a"), KeyEntryValue("2")),
        ValueRef(QLValue::Primitive(11)), 2000_usec_ht));
    ASSERT_OK(InsertSubDocument(DocPath(encoded_doc_key, KeyEntryValue("b")), ValueRef(b2),
                                3000_usec_ht));
    ASSERT_OK(ExtendSubDocument(DocPath(encoded_doc_key, KeyEntryValue("a")), ValueRef(f),
                                4000_usec_ht));
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue("b"), KeyEntryValue("e"), KeyEntryValue("2")),
        ValueRef(ValueEntryType::kTombstone), 5000_usec_ht));
  }

  void VerifyDocument(const DocKey& doc_key, HybridTime ht, string subdoc_string) {
    SubDocument doc_from_rocksdb;
    bool subdoc_found_in_rocksdb = false;

    SCOPED_TRACE("\n" + GetStackTrace(StackTraceLineFormat::CLION_CLICKABLE) + "\n" +
        DocDBDebugDumpToStr());

    // TODO(dtxn) - check both transaction and non-transaction path?
    // https://yugabyte.atlassian.net/browse/ENG-2177
    auto encoded_subdoc_key = doc_key.Encode();
    GetSubDoc(
        encoded_subdoc_key, &doc_from_rocksdb, &subdoc_found_in_rocksdb,
        kNonTransactionalOperationContext, ReadHybridTime::SingleTime(ht));
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
  void CheckExpectedLatestDBState() {
    const SubDocKey subdoc_key(MakeDocKey("mydockey", kIntKey1));

    SubDocument subdoc;
    bool doc_found = false;
    // TODO(dtxn) - check both transaction and non-transaction path?
    auto encoded_subdoc_key = subdoc_key.EncodeWithoutHt();
    GetSubDoc(
        encoded_subdoc_key, &subdoc, &doc_found,
        kNonTransactionalOperationContext);
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

  // Checks bloom filter useful counter increment to be in range [1;expected_max_increment] and
  // table iterators number increment to be expected_num_iterators_increment.
  // Updates total_useful, total_iterators
  void CheckBloom(const int expected_max_increment, uint64_t *total_useful,
                  const int expected_num_iterators_increment, uint64_t *total_iterators) {
    if (FLAGS_use_docdb_aware_bloom_filter) {
      const auto total_useful_updated =
          regular_db_options().statistics->getTickerCount(rocksdb::BLOOM_FILTER_USEFUL);
      const auto total_iterators_updated =
          regular_db_options().statistics->getTickerCount(rocksdb::NO_TABLE_CACHE_ITERATORS);
      if (expected_max_increment > 0) {
        ASSERT_GT(total_useful_updated, *total_useful);
        ASSERT_LE(total_useful_updated, *total_useful + expected_max_increment);
        *total_useful = total_useful_updated;
      } else {
        ASSERT_EQ(*total_useful, total_useful_updated);
      }
      ASSERT_EQ(*total_iterators + expected_num_iterators_increment, total_iterators_updated);
      *total_iterators = total_iterators_updated;
    }
  }

  InetAddress GetInetAddress(const std::string &strval) {
    return InetAddress(CHECK_RESULT(ParseIpAddress(strval)));
  }

  void InsertInet(const std::string strval) {
    const auto doc_key = MakeDocKey("mydockey");
    KeyBytes encoded_doc_key(doc_key.Encode());
    ASSERT_OK(SetPrimitive(
        DocPath(encoded_doc_key, KeyEntryValue::MakeInetAddress(GetInetAddress(strval))),
        ValueRef(ValueEntryType::kNullLow),
        1000_usec_ht));
  }

  // Inserts a bunch of subkeys starting with the provided doc key. It also, fills out the
  // expected_docdb_str with the expected state of DocDB after the operation.
  void AddSubKeys(const KeyBytes& encoded_doc_key, int num_subkeys, int base,
                  string* expected_docdb_str) {
    *expected_docdb_str = "";
    for (int i = 0; i < num_subkeys; i++) {
      string subkey = "subkey" + std::to_string(base + i);
      string value = "value" + std::to_string(i);
      MicrosTime hybrid_time = (i + 1) * 1000;
      ASSERT_OK(SetPrimitive(DocPath(encoded_doc_key, KeyEntryValue(subkey)),
                             QLValue::Primitive(value), HybridTime::FromMicros(hybrid_time)));
      *expected_docdb_str += strings::Substitute(
          R"#(SubDocKey(DocKey([], ["key"]), ["$0"; HT{ physical: $1 }]) -> "$2")#",
          subkey, hybrid_time, value);
      *expected_docdb_str += "\n";
    }
  }

  static constexpr int kNumSubKeysForCollectionsWithTTL = 3;

  void SetUpCollectionWithTTL(DocKey collection_key, UseIntermediateFlushes intermediate_flushes) {
    {
      QLValuePB map_value;
      for (int i = 0; i < kNumSubKeysForCollectionsWithTTL; i++) {
        string key = "k" + std::to_string(i);
        string value = "v" + std::to_string(i);
        AddMapValue(key, value, &map_value);
      }
      ASSERT_OK(InsertSubDocument(
          DocPath(collection_key.Encode()), ValueRef(map_value), 1000_usec_ht, 10s));

      ASSERT_DOC_DB_DEBUG_DUMP_STR_EQ(Format(R"#(
          SubDocKey($0, [HT{ physical: 1000 }]) -> {}; ttl: 10.000s
          SubDocKey($0, ["k0"; HT{ physical: 1000 w: 1 }]) -> "v0"; ttl: 10.000s
          SubDocKey($0, ["k1"; HT{ physical: 1000 w: 2 }]) -> "v1"; ttl: 10.000s
          SubDocKey($0, ["k2"; HT{ physical: 1000 w: 3 }]) -> "v2"; ttl: 10.000s
          )#", collection_key.ToString()));
      if (intermediate_flushes) {
        ASSERT_OK(FlushRocksDbAndWait());
      }
    }

    // Set separate TTLs for each element.
    for (int i = 0; i < kNumSubKeysForCollectionsWithTTL * 2; i++) {
      string key = "k" + std::to_string(i);
      string value = "vv" + std::to_string(i);
      QLValuePB map_value;
      AddMapValue(key, value, &map_value);
      ASSERT_OK(ExtendSubDocument(
          DocPath(collection_key.Encode()), ValueRef(map_value), 1100_usec_ht,
          MonoDelta::FromSeconds(20 + i)));
      if (intermediate_flushes) {
        ASSERT_OK(FlushRocksDbAndWait());
      }
    }
  }

  string ExpectedDebugDumpForCollectionWithTTL(DocKey collection_key,
                                               InitMarkerExpired init_marker_expired) {
    // The "file ..." comments below are for the case of intermediate_flushes = true above.
    const string result_template = init_marker_expired ?
        // After the init marker expires, we should not see a tombstone for it. We do not replace
        // timed-out collection init markers with tombstones on minor compactions, because that
        // could hide keys that
        R"#(
            SubDocKey($0, ["k0"; HT{ physical: 1100 }]) -> "vv0"; ttl: 20.000s
            SubDocKey($0, ["k1"; HT{ physical: 1100 }]) -> "vv1"; ttl: 21.000s
            SubDocKey($0, ["k2"; HT{ physical: 1100 }]) -> "vv2"; ttl: 22.000s
            SubDocKey($0, ["k3"; HT{ physical: 1100 }]) -> "vv3"; ttl: 23.000s
            SubDocKey($0, ["k4"; HT{ physical: 1100 }]) -> "vv4"; ttl: 24.000s
            SubDocKey($0, ["k5"; HT{ physical: 1100 }]) -> "vv5"; ttl: 25.000s
        )#" : R"#(
            SubDocKey($0, [HT{ physical: 1000 }]) -> {}; ttl: 10.000s               // file 1
            SubDocKey($0, ["k0"; HT{ physical: 1100 }]) -> "vv0"; ttl: 20.000s      // file 2
            SubDocKey($0, ["k0"; HT{ physical: 1000 w: 1 }]) -> "v0"; ttl: 10.000s  // file 1
            SubDocKey($0, ["k1"; HT{ physical: 1100 }]) -> "vv1"; ttl: 21.000s      // file 3
            SubDocKey($0, ["k1"; HT{ physical: 1000 w: 2 }]) -> "v1"; ttl: 10.000s  // file 1
            SubDocKey($0, ["k2"; HT{ physical: 1100 }]) -> "vv2"; ttl: 22.000s      // file 4
            SubDocKey($0, ["k2"; HT{ physical: 1000 w: 3 }]) -> "v2"; ttl: 10.000s  // file 1
            SubDocKey($0, ["k3"; HT{ physical: 1100 }]) -> "vv3"; ttl: 23.000s      // file 5
            SubDocKey($0, ["k4"; HT{ physical: 1100 }]) -> "vv4"; ttl: 24.000s      // file 6
            SubDocKey($0, ["k5"; HT{ physical: 1100 }]) -> "vv5"; ttl: 25.000s      // file 7
        )#";
    return Format(result_template, collection_key.ToString());
  }

  void InitializeCollection(const std::string& key_string,
                            std::string* val_string,
                            vector<HybridTime>::iterator* time_iter,
                            std::set<std::pair<string, string>>* docdb_dump) {
    auto collection_key = MakeDocKey(key_string);

    QLValuePB map_value;
    for (int i = 0; i < kNumSubKeysForCollectionsWithTTL; i++) {
      string key = "sk" + std::to_string(i);
      AddMapValue(key, *val_string, &map_value);
      (*val_string)[1]++;
    }

    ASSERT_OK(InsertSubDocument(
        DocPath(collection_key.Encode()), ValueRef(map_value), **time_iter));
    ++*time_iter;

    QLValuePB new_map_value;
    // Add new keys as well.
    for (int i = kNumSubKeysForCollectionsWithTTL / 2;
         i < 3 * kNumSubKeysForCollectionsWithTTL / 2; i++) {
      string key = "sk" + std::to_string(i);
      AddMapValue(key, *val_string, &new_map_value);
      (*val_string)[1]++;
    }
    ASSERT_OK(ExtendSubDocument(
      DocPath(collection_key.Encode()), ValueRef(new_map_value), **time_iter));
    ++*time_iter;
  }
};

void GetSubDocQl(
      const DocDB& doc_db, const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context, const ReadHybridTime& read_time,
      const dockv::ReaderProjection* projection = nullptr) {
  auto doc_from_rocksdb_opt = ASSERT_RESULT(TEST_GetSubDocument(
    subdoc_key, doc_db, rocksdb::kDefaultQueryId, txn_op_context,
    ReadOperationData::FromReadTime(read_time), projection));
  if (doc_from_rocksdb_opt) {
    *found_result = true;
    *result = *doc_from_rocksdb_opt;
  } else {
    *found_result = false;
    *result = SubDocument();
  }
}

void GetSubDocRedis(
      const DocDB& doc_db, const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context, const ReadHybridTime& read_time) {
  GetRedisSubDocumentData data = { subdoc_key, result, found_result };
  ASSERT_OK(GetRedisSubDocument(
      doc_db, data, rocksdb::kDefaultQueryId, txn_op_context,
      ReadOperationData::FromReadTime(read_time)));
}

// The list of types we want to test.
YB_DEFINE_ENUM(TestDocDb, (kQlReader)(kRedisReader));

class DocDBTestWrapper : public DocDBTest, public testing::WithParamInterface<TestDocDb>  {
 public:
  void GetSubDoc(
      const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context = TransactionOperationContext(),
      const ReadHybridTime& read_time = ReadHybridTime::Max()) override {
    switch (GetParam()) {
      case TestDocDb::kQlReader: {
        GetSubDocQl(doc_db(), subdoc_key, result, found_result, txn_op_context, read_time);
        break;
      }
      case TestDocDb::kRedisReader: {
        GetSubDocRedis(
            doc_db(), subdoc_key, result, found_result, txn_op_context, read_time);
        break;
      }
    }
  }

  Status SetHybridTimeFilter(std::optional<uint32_t> db_oid, HybridTime ht_filter) {
    CloseRocksDB();

    RocksDBPatcher patcher(rocksdb_dir_, regular_db_options_);

    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(db_oid, ht_filter));
    return OpenRocksDB();
  }

  Status ValidateRocksDbEntriesUnordered(std::unordered_set<std::string>* expected) {
    std::unordered_set<std::string> out;
    DocDBDebugDumpToContainer(&out);

    LOG(INFO) << "Output: " << AsString(out);
    if (*expected == out) {
      return Status::OK();
    }
    return STATUS(IllegalState, "Expected output does not match");
  }
};

class DocDBTestQl : public DocDBTest {
 public:
  void GetSubDoc(
      const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context = TransactionOperationContext(),
      const ReadHybridTime& read_time = ReadHybridTime::Max()) override {
    GetSubDocQl(doc_db(), subdoc_key, result, found_result, txn_op_context, read_time);
  }
 protected:
  template<typename T>
  void TestTableTombstone(T id);
  template<typename T>
  void TestTableTombstoneCompaction(T id);
};

class DocDBTestRedis : public DocDBTest {
 public:
  void GetSubDoc(
      const KeyBytes& subdoc_key, SubDocument* result, bool* found_result,
      const TransactionOperationContext& txn_op_context = TransactionOperationContext(),
      const ReadHybridTime& read_time = ReadHybridTime::Max()) override {
    GetSubDocRedis(
        doc_db(), subdoc_key, result, found_result, txn_op_context, read_time);
  }
};

// Static constant initialization should be completely independent (cannot initialize one using the
// other).
const DocKey DocDBTest::kDocdbTestDocKey1 = MakeDocKey("row1", 11111);
const DocKey DocDBTest::kDocdbTestDocKey2 = MakeDocKey("row2", 22222);
const KeyBytes DocDBTest::kEncodedDocdbTestDocKey1(MakeDocKey("row1", 11111).Encode());
const KeyBytes DocDBTest::kEncodedDocdbTestDocKey2(MakeDocKey("row2", 22222).Encode());

ValueControlFields Ttl(MonoDelta ttl) {
  return ValueControlFields {
    .ttl = ttl,
  };
}

std::string EncodeValue(const QLValuePB& value) {
  std::string result;
  dockv::AppendEncodedValue(value, &result);
  return result;
}

void Append(const char* a, const char* b, std::string* out) {
  out->append(a, b);
}

void PushBack(const std::string& value, std::vector<std::string>* out) {
  out->push_back(value);
}

void Append(const char* a, const char* b, faststring* out) {
  out->append(a, b - a);
}

void PushBack(const faststring& value, std::vector<std::string>* out) {
  out->emplace_back(value.c_str(), value.size());
}

void Append(const char* a, const char* b, boost::container::small_vector_base<char>* out) {
  out->insert(out->end(), a, b);
}

void PushBack(
    const boost::container::small_vector_base<char>& value, std::vector<std::string>* out) {
  out->emplace_back(value.begin(), value.end());
}

template <size_t SmallLen>
void Append(const char* a, const char* b, ByteBuffer<SmallLen>* out) {
  out->Append(a, b);
}

template <size_t SmallLen>
void PushBack(const ByteBuffer<SmallLen>& value, std::vector<std::string>* out) {
  out->push_back(value.ToStringBuffer());
}

constexpr size_t kSourceLen = 32;
const std::string kSource = RandomHumanReadableString(kSourceLen);

template <class T>
void TestKeyBytes(const char* title, std::vector<std::string>* out = nullptr) {
#ifdef NDEBUG
  constexpr size_t kIterations = 100000000;
#else
  constexpr size_t kIterations = RegularBuildVsSanitizers(10000000, 100000);
#endif
  const char* source_start = kSource.c_str();

  auto start = GetThreadCpuTimeMicros();
  T key_bytes;
  for (size_t i = kIterations; i-- > 0;) {
    key_bytes.clear();
    const char* a = source_start + ((i * 102191ULL) & (kSourceLen - 1ULL));
    const char* b = source_start + ((i * 99191ULL) & (kSourceLen - 1ULL));
    Append(std::min(a, b), std::max(a, b) + 1, &key_bytes);
    a = source_start + ((i * 88937ULL) & (kSourceLen - 1ULL));
    b = source_start + ((i * 74231ULL) & (kSourceLen - 1ULL));
    Append(std::min(a, b), std::max(a, b) + 1, &key_bytes);
    a = source_start + ((i * 75983ULL) & (kSourceLen - 1ULL));
    b = source_start + ((i * 72977ULL) & (kSourceLen - 1ULL));
    Append(std::min(a, b), std::max(a, b) + 1, &key_bytes);
    if (out) {
      PushBack(key_bytes, out);
    }
  }
  auto time = MonoDelta::FromMicroseconds(GetThreadCpuTimeMicros() - start);
  LOG(INFO) << title << ": " << time;
}

}  // namespace docdb
}  // namespace yb
