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

}  // namespace yb
