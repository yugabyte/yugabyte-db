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
//

#include "yb/integration-tests/mini_cluster.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_enable_documentdb);
DECLARE_bool(enable_pg_cron);

namespace yb {
class DocumentDBTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
#ifndef YB_ENABLE_YSQL_DOCUMENTDB_EXT
    GTEST_SKIP() << "DocumentDB extension is not available in build type";
#endif

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_documentdb) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_pg_cron) = true;

    TEST_SETUP_SUPER(pgwrapper::PgMiniTestBase);

    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
    ASSERT_OK(conn_->ExecuteFormat("CREATE EXTENSION documentdb CASCADE"));
    ASSERT_OK(conn_->Execute("SET search_path TO documentdb_api, documentdb_core"));
    ASSERT_OK(conn_->Execute("SET documentdb_core.bsonUseEJson TO TRUE"));
  }

  std::unique_ptr<pgwrapper::PGConn> conn_;
};

TEST_F(DocumentDBTest, SimpleCollection) {
  const auto db_name = "documentdb";
  const auto collection_name = "patient";
  const auto patient_1 = "P001";
  const auto patient_2 = "P002";

  // Insert 5 documents into patient.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "patient_id": "$2", "name": "Alice Smith", "age": 30, "phone_number": "555-0123",
        "registration_year": "2023","conditions": ["Diabetes", "Hypertension"]},
    { "patient_id": "$3", "name": "Bob Johnson", "age": 45, "phone_number": "555-0456",
        "registration_year": "2023", "conditions": ["Asthma"]},
    { "patient_id": "P003", "name": "Charlie Brown", "age": 29, "phone_number": "555-0789",
        "registration_year": "2024", "conditions": ["Allergy", "Anemia"]},
    { "patient_id": "P004", "name": "Diana Prince", "age": 40, "phone_number": "555-0987",
        "registration_year": "2024", "conditions": ["Migraine"]},
    { "patient_id": "P005", "name": "Edward Norton", "age": 55, "phone_number": "555-1111",
        "registration_year": "2025", "conditions": ["Hypertension", "Heart Disease"]}]}');
  )",
      db_name, collection_name, patient_1, patient_2));

  auto get_document_count = [&]() {
    return CHECK_RESULT(conn_->FetchRow<int64_t>(Format(
        "SELECT count(*) FROM documentdb_api.collection('$0','$1')", db_name, collection_name)));
  };

  ASSERT_EQ(get_document_count(), 5);

  // Update 1 document.
  auto get_patient_age = [&](const std::string& patient_id) {
    auto age_str = CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT (((cursorpage->>'cursor')::bson->>'firstBatch')::bson->>'0')::bson->>'age'
        FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
          "filter" : {"patient_id":"$2"}}');
      )",
        db_name, collection_name, patient_id)));

    return std::stoi(age_str);
  };

  ASSERT_EQ(get_patient_age(patient_1), 30);
  ASSERT_EQ(get_patient_age(patient_2), 45);

  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.update('$0', '{"update":"$1",
      "updates":[{"q":{"patient_id":"$2"},"u":{"$$set":{"age":14}}}]}')
  )",
      db_name, collection_name, patient_1));

  ASSERT_EQ(get_patient_age(patient_1), 14);
  ASSERT_EQ(get_patient_age(patient_2), 45);

  // Update all documents.
  ASSERT_OK(conn_->FetchFormat(
      R"(
    SELECT documentdb_api.update('$0', '{"update":"$1",
      "updates":[{"q":{},"u":{"$$set":{"age":24}},"multi":true}]}')
    )",
      db_name, collection_name));

  ASSERT_EQ(get_patient_age(patient_1), 24);
  ASSERT_EQ(get_patient_age(patient_2), 24);

  // Delete one documents.
  ASSERT_OK(conn_->FetchFormat(
      R"(
    SELECT documentdb_api.delete('$0', '{"delete": "$1",
      "deletes": [{"q": {"patient_id": "$2"}, "limit": 1}]}')
    )",
      db_name, collection_name, patient_2));

  ASSERT_EQ(get_document_count(), 4);
}

TEST_F(DocumentDBTest, DropCollectionAndDatabase) {
  const auto db_name = "testdropdb";
  const auto collection_name = "dropcoll";

  // Insert a document to create the collection.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "key": "value1" }]}');
  )",
      db_name, collection_name));

  // Verify the collection has 1 document.
  auto count = ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
      "SELECT count(*) FROM documentdb_api.collection('$0','$1')", db_name, collection_name)));
  ASSERT_EQ(count, 1);

  // Drop the collection.
  auto drop_result = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      "SELECT documentdb_api.drop_collection('$0', '$1')", db_name, collection_name)));
  ASSERT_TRUE(drop_result);

  // Recreate the same collection with different data and verify only new data is returned.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "key": "new_value1" },
    { "key": "new_value2" }]}');
  )",
      db_name, collection_name));

  count = ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
      "SELECT count(*) FROM documentdb_api.collection('$0','$1')", db_name, collection_name)));
  ASSERT_EQ(count, 2);

  // Verify old data ("value1") is gone and only new data exists.
  auto get_key_value = [&](const std::string& filter_key) {
    return CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT (((cursorpage->>'cursor')::bson->>'firstBatch')::bson->>'0')::bson->>'key'
        FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
          "filter" : {"key":"$2"}}');
      )",
        db_name, collection_name, filter_key)));
  };

  // Old data should not be found.
  auto old_data_found = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      R"(
    SELECT jsonb_array_length(((cursorpage->>'cursor')::bson->>'firstBatch')::jsonb) > 0
      FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
        "filter" : {"key":"value1"}}');
    )",
      db_name, collection_name)));
  ASSERT_FALSE(old_data_found);

  // New data should be present.
  ASSERT_EQ(get_key_value("new_value1"), "new_value1");
  ASSERT_EQ(get_key_value("new_value2"), "new_value2");

  // Drop the recreated collection before the drop_database test.
  drop_result = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      "SELECT documentdb_api.drop_collection('$0', '$1')", db_name, collection_name)));
  ASSERT_TRUE(drop_result);

  // Insert documents into two collections for drop_database test.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"coll_a", "documents":[
    { "a": 1 }]}');
  )",
      db_name));
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"coll_b", "documents":[
    { "b": 2 }]}');
  )",
      db_name));

  // Drop the entire database.
  ASSERT_OK(conn_->FetchFormat(
      "SELECT documentdb_api.drop_database('$0')", db_name));

  // Verify both collections are gone after drop_database.
  auto coll_a_exists = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      R"(
    SELECT jsonb_array_length(((cursorpage->>'cursor')::bson->>'firstBatch')::jsonb) > 0
      FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "coll_a"}');
    )",
      db_name)));
  ASSERT_FALSE(coll_a_exists);

  auto coll_b_exists = ASSERT_RESULT(conn_->FetchRow<bool>(Format(
      R"(
    SELECT jsonb_array_length(((cursorpage->>'cursor')::bson->>'firstBatch')::jsonb) > 0
      FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "coll_b"}');
    )",
      db_name)));
  ASSERT_FALSE(coll_b_exists);
}

// Validates that primary key (_id) range queries with $gt and $lt return correct results.
// This exercises the BSON comparison logic on the primary key, which is stored as a range
// key in DocDB. Numeric _id values including negative numbers must sort correctly
// (e.g., -10 < -1 < 0 < 5 < 42), which requires proper BSON comparison rather than
// byte-wise ordering of little-endian integers.
TEST_F(DocumentDBTest, PrimaryKeyRangeQuery) {
  const auto db_name = "pktest";
  const auto collection_name = "numbers";

  // Insert documents with numeric _id values, including negatives.
  // BSON stores int32 as little-endian, so byte-wise comparison would give wrong order
  // for negative values (e.g., -1 = 0xFFFFFFFF would sort after 1 = 0x01000000).
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": -10, "label": "neg10" },
    { "_id": -1,  "label": "neg1" },
    { "_id": 0,   "label": "zero" },
    { "_id": 5,   "label": "five" },
    { "_id": 42,  "label": "fortytwo" },
    { "_id": 100, "label": "hundred" }]}');
  )",
      db_name, collection_name));

  // Helper: get the label of the single document matching a filter.
  auto get_label = [&](const std::string& filter) {
    return CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT (((cursorpage->>'cursor')::bson->>'firstBatch')::bson->>'0')::bson->>'label'
        FROM documentdb_api.find_cursor_first_page('$0',
          '{ "find": "$1", "filter": $2 }');
      )",
        db_name, collection_name, filter)));
  };

  // Helper: count documents matching a filter using documentdb_api.count_query.
  auto count_matching = [&](const std::string& filter) {
    auto n_str = CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT document->>'n'
        FROM documentdb_api.count_query('$0',
          '{ "count": "$1", "query": $2 }');
      )",
        db_name, collection_name, filter)));
    return std::stol(n_str);
  };

  // Verify total count.
  ASSERT_EQ(
      ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
          "SELECT count(*) FROM documentdb_api.collection('$0','$1')", db_name, collection_name))),
      6);

  // Test exact _id lookup for a negative value.
  EXPECT_EQ(get_label(R"({"_id": -1})"), "neg1");
  EXPECT_EQ(get_label(R"({"_id": 0})"), "zero");
  EXPECT_EQ(get_label(R"({"_id": 42})"), "fortytwo");

  // Test $gt: _id > 0 should return 3 documents (5, 42, 100).
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": 0}})"), 3);

  // Test $lt: _id < 0 should return 2 documents (-10, -1).
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": 0}})"), 2);

  // Test $gt with negative boundary: _id > -5 should return 5 documents (-1, 0, 5, 42, 100).
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": -5}})"), 5);

  // Test $lt with positive boundary: _id < 42 should return 4 documents (-10, -1, 0, 5).
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": 42}})"), 4);

  // Test combined $gt and $lt: -1 < _id < 42 should return 2 documents (0, 5).
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": -1, "$lt": 42}})"), 2);

  // Test $gte and $lte: -1 <= _id <= 5 should return 3 documents (-1, 0, 5).
  EXPECT_EQ(count_matching(R"({"_id": {"$gte": -1, "$lte": 5}})"), 3);

  // Test $gte at lower bound: _id >= -10 should return all 6 documents.
  EXPECT_EQ(count_matching(R"({"_id": {"$gte": -10}})"), 6);

  // Test $lte at upper bound: _id <= 100 should return all 6 documents.
  EXPECT_EQ(count_matching(R"({"_id": {"$lte": 100}})"), 6);

  // Test range that excludes everything: _id > 100 should return 0.
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": 100}})"), 0);

  // Test range that excludes everything: _id < -10 should return 0.
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": -10}})"), 0);
}

// Validates that cross-type comparisons on _id work correctly with $gt/$lt.
// BSON comparison must handle type coercion: int32, int64, and double values
// with the same numeric value should compare as equal, and range queries across
// types must return correct results. Binary comparison would fail because:
// - int32(42) = 0x2A000000 (4 bytes) vs int64(42) = 0x2A00000000000000 (8 bytes)
//   have different binary representations
// - double(-1.5) = 0x000000000000F8BF vs int32(0) = 0x00000000 have incompatible
//   binary layouts
// - Different BSON type codes (0x10 for int32, 0x12 for int64, 0x01 for double)
//   would sort by type code byte-wise rather than by numeric value
TEST_F(DocumentDBTest, CrossTypePrimaryKeyComparison) {
  const auto db_name = "crosstype";
  const auto collection_name = "mixed";

  // Insert documents with mixed numeric _id types.
  // Note: DocumentDB/MongoDB infers the type from the JSON representation.
  // Integers without decimals become int32/int64, values with decimals become double.
  // We use $numberInt, $numberLong, $numberDouble for explicit types via EJSON.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": {"$$numberDouble": "-1.5"}, "label": "double_neg" },
    { "_id": {"$$numberInt": "-1"},      "label": "int32_neg" },
    { "_id": {"$$numberInt": "0"},       "label": "int32_zero" },
    { "_id": {"$$numberLong": "1"},      "label": "int64_one" },
    { "_id": {"$$numberDouble": "2.5"},  "label": "double_pos" },
    { "_id": {"$$numberInt": "10"},      "label": "int32_ten" },
    { "_id": {"$$numberLong": "100"},    "label": "int64_hundred" }]}');
  )",
      db_name, collection_name));

  auto get_label = [&](const std::string& filter) {
    return CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT (((cursorpage->>'cursor')::bson->>'firstBatch')::bson->>'0')::bson->>'label'
        FROM documentdb_api.find_cursor_first_page('$0',
          '{ "find": "$1", "filter": $2 }');
      )",
        db_name, collection_name, filter)));
  };

  auto count_matching = [&](const std::string& filter) {
    auto n_str = CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT document->>'n'
        FROM documentdb_api.count_query('$0',
          '{ "count": "$1", "query": $2 }');
      )",
        db_name, collection_name, filter)));
    return std::stol(n_str);
  };

  // Verify total count.
  ASSERT_EQ(
      ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
          "SELECT count(*) FROM documentdb_api.collection('$0','$1')", db_name, collection_name))),
      7);

  // Exact match using the same type as stored.
  EXPECT_EQ(get_label(R"({"_id": {"$numberLong": "1"}})"), "int64_one");
  EXPECT_EQ(get_label(R"({"_id": {"$numberInt": "10"}})"), "int32_ten");

  // $gt with double boundary across int types:
  // _id > 0.5 should return int64(1), double(2.5), int32(10), int64(100) = 4 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": 0.5}})"), 4);

  // $lt with negative double boundary:
  // _id < -1.0 should return double(-1.5) = 1 doc.
  // Binary comparison would fail: double(-1.5) bytes > int32(-1) bytes.
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": -1.0}})"), 1);

  // $gt with int boundary should include double values:
  // _id > 2 should return double(2.5), int32(10), int64(100) = 3 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": 2}})"), 3);

  // $lt with int boundary should include double values:
  // _id < 1 should return double(-1.5), int32(-1), int32(0) = 3 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": 1}})"), 3);

  // Range query spanning mixed types:
  // -1 < _id < 10 should return int32(0), int64(1), double(2.5) = 3 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": -1, "$lt": 10}})"), 3);

  // $gte/$lte with exact type boundaries:
  // -1.5 <= _id <= 2.5 should return double(-1.5), int32(-1), int32(0), int64(1), double(2.5)
  // = 5 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$gte": -1.5, "$lte": 2.5}})"), 5);

  // All negative: _id < 0 should return double(-1.5), int32(-1) = 2 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$lt": 0}})"), 2);

  // All positive: _id > 0 should return int64(1), double(2.5), int32(10), int64(100) = 4 docs.
  EXPECT_EQ(count_matching(R"({"_id": {"$gt": 0}})"), 4);
}

// Exercises MongoDB $gt / $lt semantics on the _id primary key for documents whose
// _id values span BSON canonical types (MinKey, int32 across the sign and byte-width
// boundaries, string, boolean, MaxKey). MongoDB's range operators are type-bracketed:
// $gt / $lt only match documents whose _id has the same BSON type as the query value,
// with MinKey and MaxKey as the cross-type exceptions ($gt: MinKey matches every value
// strictly greater than MinKey across all types; $lt: MaxKey, symmetrically).
TEST_F(DocumentDBTest, BsonPrimaryKeyBinaryComparator) {
  const auto db_name = "documentdb";
  const auto collection_name = "pk_cmp";

  // Insert 8 docs whose _id values span BSON types and the int32 sign/byte-boundary
  // boundaries. $$ escapes to $ through yb::Format's $N substitution.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": { "$$minKey": 1 } },
    { "_id": -1 },
    { "_id": 0 },
    { "_id": 1 },
    { "_id": 256 },
    { "_id": "z_string" },
    { "_id": true },
    { "_id": { "$$maxKey": 1 } }]}');
  )",
      db_name, collection_name));

  auto match_count = [&](const std::string& filter) {
    return CHECK_RESULT(conn_->FetchRow<int32_t>(Format(
        R"(
      SELECT jsonb_array_length(((cursorpage->>'cursor')::bson->>'firstBatch')::jsonb)
        FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
          "filter" : $2}');
      )",
        db_name, collection_name, filter)));
  };

  // Sanity: all 8 docs are present.
  ASSERT_EQ(match_count("{}"), 8);

  // Type-bracketed: _id > -1 (int32) matches int32 values > -1 = { 0, 1, 256 } = 3 docs.
  ASSERT_EQ(match_count(R"({"_id": {"$gt": -1}})"), 3);

  // Type-bracketed: _id < 0 (int32) matches int32 values < 0 = { -1 } = 1 doc.
  ASSERT_EQ(match_count(R"({"_id": {"$lt": 0}})"), 1);

  // MinKey cross-type: _id > MinKey matches every doc except MinKey itself = 7 docs.
  ASSERT_EQ(match_count(R"({"_id": {"$gt": {"$minKey": 1}}})"), 7);

  // MaxKey cross-type: _id < MaxKey matches every doc except MaxKey itself = 7 docs.
  ASSERT_EQ(match_count(R"({"_id": {"$lt": {"$maxKey": 1}}})"), 7);

  // Type-bracketed: _id < "" matches strings < "" — there are none = 0 docs.
  ASSERT_EQ(match_count(R"({"_id": {"$lt": ""}})"), 0);

  // Type-bracketed: _id < true matches booleans < true — false is the only such value
  // and is not present in the data = 0 docs.
  ASSERT_EQ(match_count(R"({"_id": {"$lt": true}})"), 0);
}

// ObjectId is the default _id type in MongoDB collections. ObjectIds compare
// byte-wise via bson_oid_compare, the only built-in comparator libbson exposes.
// This test inserts five ObjectId _id values in non-sorted order, then exercises
// exact-match lookup, range filters, and total count to confirm the byte-wise
// ObjectId codepath in CompareBsonValues is reached and returns correct results.
TEST_F(DocumentDBTest, BsonObjectIdPrimaryKey) {
  const auto db_name = "documentdb";
  const auto collection_name = "oid_pk";

  // Inserted out of order; expected sort order is by lexicographic OID hex.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": { "$$oid": "5fffffffffffffffffffffff" } },
    { "_id": { "$$oid": "100000000000000000000000" } },
    { "_id": { "$$oid": "000000000000000000000001" } },
    { "_id": { "$$oid": "ffffffffffffffffffffffff" } },
    { "_id": { "$$oid": "7f7f7f7f7f7f7f7f7f7f7f7f" } }]}');
  )",
      db_name, collection_name));

  auto match_count = [&](const std::string& filter) {
    return CHECK_RESULT(conn_->FetchRow<int32_t>(Format(
        R"(
      SELECT jsonb_array_length(((cursorpage->>'cursor')::bson->>'firstBatch')::jsonb)
        FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
          "filter" : $2}');
      )",
        db_name, collection_name, filter)));
  };

  // Total docs.
  ASSERT_EQ(match_count("{}"), 5);

  // Exact match on each OID.
  EXPECT_EQ(match_count(R"({"_id": {"$oid": "5fffffffffffffffffffffff"}})"), 1);
  EXPECT_EQ(match_count(R"({"_id": {"$oid": "000000000000000000000001"}})"), 1);
  EXPECT_EQ(match_count(R"({"_id": {"$oid": "ffffffffffffffffffffffff"}})"), 1);

  // Exact match against a value that is not present.
  EXPECT_EQ(match_count(R"({"_id": {"$oid": "000000000000000000000002"}})"), 0);

  // $gt: > "5fffffffffffffffffffffff" matches {"7f...", "ff...", "10..." -> no, "10..." < "5f..."}.
  // Lexicographically greater: "7f7f7f...", "ffff...". = 2 docs.
  EXPECT_EQ(match_count(R"({"_id": {"$gt": {"$oid": "5fffffffffffffffffffffff"}}})"), 2);

  // $lt: < "5fffffffffffffffffffffff" matches {"00...01", "10...", and not "5f..."}. = 2 docs.
  EXPECT_EQ(match_count(R"({"_id": {"$lt": {"$oid": "5fffffffffffffffffffffff"}}})"), 2);

  // $gte at the lower extreme: matches every doc.
  EXPECT_EQ(match_count(R"({"_id": {"$gte": {"$oid": "000000000000000000000000"}}})"), 5);

  // $lte at the upper extreme: matches every doc.
  EXPECT_EQ(match_count(R"({"_id": {"$lte": {"$oid": "ffffffffffffffffffffffff"}}})"), 5);
}

// Match counts alone don't prove ordering — the comparator could return the
// right set of rows in the wrong order. This test inserts negative and positive
// int32 _id values out of order, queries with sort: {_id: 1} (and -1), and
// asserts the returned documents come back in BSON canonical order.
TEST_F(DocumentDBTest, PrimaryKeySortOrder) {
  const auto db_name = "sorttest";
  const auto collection_name = "sortcoll";

  // Inserted unsorted; expected ascending order by _id: -100, -2, 0, 7, 42, 999.
  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": 42,   "label": "fortytwo" },
    { "_id": -2,   "label": "negtwo"   },
    { "_id": 999,  "label": "ninehundred" },
    { "_id": 0,    "label": "zero" },
    { "_id": -100, "label": "negonehundred" },
    { "_id": 7,    "label": "seven" }]}');
  )",
      db_name, collection_name));

  // Returns the `label` field of the document at the given index in the sorted
  // firstBatch. Index 0 is the smallest under ascending sort, the largest under
  // descending.
  auto label_at = [&](int sort_direction, int index) {
    return CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT (((cursorpage->>'cursor')::bson->>'firstBatch')::bson->>'$2')::bson->>'label'
        FROM documentdb_api.find_cursor_first_page('$0', '{ "find" : "$1",
          "sort" : { "_id" : $3 } }');
      )",
        db_name, collection_name, index, sort_direction)));
  };

  // Ascending: -100, -2, 0, 7, 42, 999.
  EXPECT_EQ(label_at(1, 0), "negonehundred");
  EXPECT_EQ(label_at(1, 1), "negtwo");
  EXPECT_EQ(label_at(1, 2), "zero");
  EXPECT_EQ(label_at(1, 3), "seven");
  EXPECT_EQ(label_at(1, 4), "fortytwo");
  EXPECT_EQ(label_at(1, 5), "ninehundred");

  // Descending: 999, 42, 7, 0, -2, -100.
  EXPECT_EQ(label_at(-1, 0), "ninehundred");
  EXPECT_EQ(label_at(-1, 1), "fortytwo");
  EXPECT_EQ(label_at(-1, 2), "seven");
  EXPECT_EQ(label_at(-1, 3), "zero");
  EXPECT_EQ(label_at(-1, 4), "negtwo");
  EXPECT_EQ(label_at(-1, 5), "negonehundred");
}

// The comparator must produce correct results after a memtable flush and SST
// compaction, not just for in-memory data. Compaction merges sorted runs using
// the comparator, so a bug there can pass memtable-only tests and fail once
// the data hits disk. This test inserts negative int32 _ids, forces a flush
// plus compaction, then re-queries to confirm range filters still return the
// same set of rows.
TEST_F(DocumentDBTest, PrimaryKeyCompactionRoundTrip) {
  const auto db_name = "compacttest";
  const auto collection_name = "compactcoll";

  ASSERT_OK(conn_->FetchFormat(
      R"(
  SELECT documentdb_api.insert('$0', '{"insert":"$1", "documents":[
    { "_id": -10 }, { "_id": -1 }, { "_id": 0 }, { "_id": 5 },
    { "_id": 42 }, { "_id": 100 }]}');
  )",
      db_name, collection_name));

  auto match_count = [&](const std::string& filter) {
    auto n_str = CHECK_RESULT(conn_->FetchRow<std::string>(Format(
        R"(
      SELECT document->>'n'
        FROM documentdb_api.count_query('$0',
          '{ "count": "$1", "query": $2 }');
      )",
        db_name, collection_name, filter)));
    return std::stol(n_str);
  };

  // Baseline (memtable only).
  ASSERT_EQ(match_count(R"({"_id": {"$lt": 0}})"), 2);
  ASSERT_EQ(match_count(R"({"_id": {"$gt": 0}})"), 3);
  ASSERT_EQ(match_count(R"({"_id": {"$gte": -1, "$lte": 42}})"), 4);

  // Force flush + compaction so the comparator is exercised on SSTs.
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());

  // Post-compaction: same counts.
  EXPECT_EQ(match_count(R"({"_id": {"$lt": 0}})"), 2);
  EXPECT_EQ(match_count(R"({"_id": {"$gt": 0}})"), 3);
  EXPECT_EQ(match_count(R"({"_id": {"$gte": -1, "$lte": 42}})"), 4);
}

}  // namespace yb
