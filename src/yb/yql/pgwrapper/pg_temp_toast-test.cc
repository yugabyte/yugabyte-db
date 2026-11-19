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

#include <string>

#include "gtest/gtest.h"

#include "yb/common/json_util.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/status_format.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

namespace yb::pgwrapper {

// This file contains tests that verify that we are properly detoasting data when inserting
// into a YB table from a temporary table, which may contain toasted values.

namespace {
constexpr size_t kVectorSize = 16000;
constexpr size_t kLargeTextSize = 8192 * 16;
constexpr size_t kWordMinSize = 1024;
constexpr size_t kWordMaxSize = 2048;

constexpr int kNumRows = 4;

const std::string kTempTableName = "tmp";
const std::string kTempTableName2 = "tmp2";
const std::string kTempTableName3 = "tmp3";
const std::string kPermanentTableName = "perm";
const std::string kPermanentTableDataPkName = "perm_data_pk";
const std::string kMatViewName = "mv";
}  // anonymous namespace

class PgToastTempTableTest : public PgMiniTestBase {
 public:
  // Represents a row of data used in these tests.
  class TestDataRow {
   public:
    int id;
    std::string data;
    rapidjson::Document json_data;
    std::string vector_data;

    // Creates a new TestDataRow with random data. Given the same id, the constructed
    // TestDataRow will always be identical.
    explicit TestDataRow(int id)
        : id(id),
          data(GenerateString(id, kLargeTextSize)),
          json_data(GenerateJsonFromString(data)),
          vector_data(GenerateVector(id, kVectorSize)) {}

    explicit TestDataRow(const std::tuple<int, std::string, std::string, std::string>& rhs)
        : id(std::get<0>(rhs)), data(std::get<1>(rhs)), vector_data(std::get<3>(rhs)) {
      json_data.Parse(std::get<2>(rhs).c_str());
    }

    TestDataRow(const TestDataRow& other)
        : id(other.id), data(other.data), vector_data(other.vector_data) {
      json_data.CopyFrom(other.json_data, json_data.GetAllocator());
    }

    TestDataRow& operator=(const TestDataRow& other) {
      if (this != &other) {
        id = other.id;
        data = other.data;
        vector_data = other.vector_data;
        json_data.CopyFrom(other.json_data, json_data.GetAllocator());
      }
      return *this;
    }

    bool operator==(const TestDataRow& rhs) const {
      return std::tie(id, data, json_data, vector_data) ==
             std::tie(rhs.id, rhs.data, rhs.json_data, rhs.vector_data);
    }

    // Convert the data string into a tsquery by joining the words with the & operator.
    // For example, "hello world" -> "hello & world".
    std::string ToTsQuery() const {
      std::istringstream iss(data);
      std::string word, query;
      while (iss >> word) {
        if (!query.empty()) {
          query += " & ";
        }
        query += word;
      }
      return query;
    }

    std::string ToString() const {
      return Format("TestDataRow(id=$0, data=$1, json_data=$2, vector_data=$3)",
                    id, data, common::WriteRapidJsonToString(json_data), vector_data);
    }
  };

 protected:
  // Generates a random string of a given size.
  // The string will be comprised of one or more words separated by spaces.
  // Each word is comprised of mixed characters to ensure that compression alone
  // will not get the string down to the desired size; Postgres will still need
  // to do out-of-line storage in addition to compression.
  static std::string GenerateString(unsigned int seed, size_t size) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static const size_t charset_size = sizeof(charset) - 1;

    std::string result;
    result.reserve(size);

    // Use seeded std::mt19937_64 for repeatable randomness.
    std::mt19937_64 rng(seed);

    while (result.size() < size) {
      size_t word_len = RandomUniformInt<size_t>(kWordMinSize, kWordMaxSize, &rng);

      // Generate a subword of random letters
      size_t repeat_count = 4;
      size_t subword_len = std::max(size_t(1), word_len / repeat_count);
      std::string subword;
      for (size_t i = 0; i < subword_len; ++i) {
        char c = charset[RandomUniformInt<size_t>(0, charset_size - 1, &rng)];
        subword += c;
      }

      // Repeat the subword to form the word
      std::string word;
      while (word.size() < word_len) {
        for (size_t i = 0; i < subword.size() && word.size() < word_len; ++i) {
          word += subword[i];
        }
      }

      // Add the word to result, ensuring we don't exceed the target size
      for (size_t i = 0; i < word.size() && result.size() < size; ++i) {
        result += word[i];
      }

      if (result.size() < size) {
        result += ' ';
      }
    }

    return result;
  }

  // Generates a vector of a given size, with the given element repeated.
  static std::string GenerateVector(int elem, size_t size) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < size; ++i) {
      if (i > 0) oss << ",";

      oss << elem;
    }
    oss << "]";
    return oss.str();
  }

  // Generates a JSON object from a string, where each word in the string is a key in the JSON
  // object. The value of each key is the sum of all of the indices where the word appears in the
  // string. For example, "hello world hello" -> {"hello": 2, "world": 1}
  static rapidjson::Document GenerateJsonFromString(const std::string& str) {
    rapidjson::Document json_obj;
    json_obj.SetObject();
    rapidjson::Document::AllocatorType& allocator = json_obj.GetAllocator();
    std::istringstream iss(str);
    std::string word;
    size_t word_idx = 1;
    while (iss >> word) {
      rapidjson::Value key(word.c_str(), allocator);
      if (json_obj.HasMember(key)) {
        // If key already exists, add the index to the current value.
        rapidjson::Value& val = json_obj[key];
        val.SetInt(val.GetInt() + static_cast<int>(word_idx));
      } else {
        json_obj.AddMember(key, rapidjson::Value(static_cast<int>(word_idx)), allocator);
      }
      ++word_idx;
    }
    return json_obj;
  }

  // Creates tables and inserts data used in all of the tests in this file.
  void SetUp() override {
    PgMiniTestBase::SetUp();

    conn_ = EXPECT_RESULT(Connect());

    EXPECT_OK(conn_->Execute("CREATE EXTENSION IF NOT EXISTS vector"));

    expected_table_rows_.clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE TEMP TABLE $0 ("
        "  id INT PRIMARY KEY, "
        "  data TEXT, "
        "  json_data JSONB, "
        "  vector_data VECTOR($1))",
        kTempTableName, kVectorSize));

    expected_table_rows_[kTempTableName].clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE TEMP TABLE $0 ("
        "  id INT PRIMARY KEY, "
        "  data TEXT, "
        "  json_data JSONB, "
        "  vector_data VECTOR($1))",
        kTempTableName2, kVectorSize));

    expected_table_rows_[kTempTableName2].clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE TEMP TABLE $0 ("
        "  id INT PRIMARY KEY, "
        "  data TEXT, "
        "  json_data JSONB, "
        "  vector_data VECTOR($1))",
        kTempTableName3, kVectorSize));

    expected_table_rows_[kTempTableName3].clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0 ("
        "  id INT PRIMARY KEY, "
        "  data TEXT, "
        "  json_data JSONB, "
        "  vector_data VECTOR($1))",
        kPermanentTableName, kVectorSize));

    expected_table_rows_[kPermanentTableName].clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0 ("
        "  id INT, "
        "  data TEXT PRIMARY KEY, "
        "  json_data JSONB, "
        "  vector_data VECTOR($1))",
        kPermanentTableDataPkName, kVectorSize));

    expected_table_rows_[kPermanentTableDataPkName].clear();

    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1", kMatViewName, kPermanentTableName));
    EXPECT_OK(conn_->ExecuteFormat("CREATE UNIQUE INDEX ON $0 (data)", kMatViewName));

    expected_table_rows_[kMatViewName].clear();

    EXPECT_OK(conn_->ExecuteFormat("CREATE INDEX ON $0 (data)", kPermanentTableName));
    EXPECT_OK(conn_->ExecuteFormat(
        "CREATE INDEX ON $0 USING ybgin (to_tsvector('english', data))", kPermanentTableName));
    EXPECT_OK(
        conn_->ExecuteFormat("CREATE INDEX ON $0 USING ybgin (json_data)", kPermanentTableName));
    EXPECT_OK(
        conn_->ExecuteFormat("CREATE INDEX ON $0 USING ybhnsw (vector_data)", kPermanentTableName));

    for (int i = 0; i < kNumRows; i++) {
      TestDataRow row(i);
      EXPECT_OK(conn_->ExecuteFormat(
          "INSERT INTO $0 (id, data, json_data, vector_data) VALUES "
          "($1, '$2', '$3', '$4')",
          kTempTableName, i, row.data, common::WriteRapidJsonToString(row.json_data),
          row.vector_data));
      expected_table_rows_[kTempTableName].push_back(std::move(row));
    }

    for (int i = kNumRows; i < kNumRows * 2; i++) {
      TestDataRow row(i);
      EXPECT_OK(conn_->ExecuteFormat(
          "INSERT INTO $0 (id, data, json_data, vector_data) VALUES "
          "($1, '$2', '$3', '$4')",
          kPermanentTableName, i, row.data, common::WriteRapidJsonToString(row.json_data),
          row.vector_data));
      expected_table_rows_[kPermanentTableName].emplace_back(i);

      EXPECT_OK(conn_->ExecuteFormat(
          "INSERT INTO $0 (id, data, json_data, vector_data) VALUES "
          "($1, '$2', '$3', '$4')",
          kPermanentTableDataPkName, i, row.data, common::WriteRapidJsonToString(row.json_data),
          row.vector_data));
      expected_table_rows_[kPermanentTableDataPkName].emplace_back(i);

      EXPECT_OK(conn_->ExecuteFormat(
          "INSERT INTO $0 (id, data, json_data, vector_data) VALUES "
          "($1, '$2', '$3', '$4')",
          kTempTableName3, i, row.data, common::WriteRapidJsonToString(row.json_data),
          row.vector_data));
      expected_table_rows_[kTempTableName3].emplace_back(i);
    }
  }

  // Verifies that the data in the tables matche the data in the expected_table_rows_
  // member variable.
  Status VerifyData() {
    for (const auto& [table_name, expected_rows] : expected_table_rows_) {
      auto actual_table_size =
          VERIFY_RESULT((conn_->FetchRow<PGUint64>(Format("SELECT COUNT(*) FROM $0", table_name))));
      SCHECK_EQ(actual_table_size, expected_rows.size(), IllegalState,
          "table has the wrong number of rows");

      for (const auto& expected_row : expected_rows) {
        // Check that the primary key index is working.
        auto actual_row_pk =
            VERIFY_RESULT((conn_->FetchRow<int, std::string, std::string, std::string>(Format(
                "SELECT id, data, json_data::text, vector_data::text FROM $0 WHERE id = $1",
                table_name, expected_row.id))));
        SCHECK_EQ(TestDataRow(actual_row_pk), expected_row, IllegalState,
            "Primary key index returned unexpected row");

        // Check that the index on the data column is working.

        auto actual_row_data =
            VERIFY_RESULT((conn_->FetchRow<int, std::string, std::string, std::string>(Format(
                "SELECT id, data, json_data::text, vector_data::text FROM $0 WHERE data = '$1'",
                table_name, expected_row.data))));
        SCHECK_EQ(TestDataRow(actual_row_data), expected_row, IllegalState,
            "Data column index returned unexpected row");

        // Check that the GIN index on the to_tsvector(data) column is working.
        auto actual_row_tsv = VERIFY_RESULT((conn_->FetchRow<
                                             int, std::string, std::string, std::string>(Format(
            "SELECT id, data, json_data::text, vector_data::text FROM $0 WHERE to_tsvector(data) "
            "@@ to_tsquery('$1')",
            table_name, expected_row.ToTsQuery()))));
        SCHECK_EQ(TestDataRow(actual_row_tsv), expected_row, IllegalState,
            "GIN index on to_tsvector(data) returned unexpected row");

        // Check that the GIN index on the json_data column is working.
        auto actual_row_json = VERIFY_RESULT((conn_->FetchRow<
                                              int, std::string, std::string, std::string>(Format(
            "SELECT id, data, json_data::text, vector_data::text FROM $0 WHERE json_data @> '$1'",
            table_name, common::WriteRapidJsonToString(expected_row.json_data)))));
        SCHECK_EQ(TestDataRow(actual_row_json), expected_row, IllegalState,
            "GIN index on json_data returned unexpected row");

        // Check that the vector index is working.
        auto actual_row_vector =
            VERIFY_RESULT((conn_->FetchRow<int, std::string, std::string, std::string>(Format(
                "SELECT id, data, json_data::text, vector_data::text FROM $0 ORDER BY vector_data "
                "<-> '$1' LIMIT 1",
                table_name, expected_row.vector_data))));
        SCHECK_EQ(TestDataRow(actual_row_vector), expected_row, IllegalState,
            "Vector index returned unexpected row");
      }
    }

    // Check that the attributes in the temp tables are still toasted.
    // We verify this by reading the pg_column_size of each attribute.
    for (const auto& table_name : {kTempTableName, kTempTableName2, kTempTableName3}) {
      if (expected_table_rows_[table_name].empty()) {
        continue;
      }
      for (const auto& row : expected_table_rows_[table_name]) {
        auto [data_size, json_size, vector_size] = VERIFY_RESULT((conn_->FetchRow<
                                                                  PGUint32, PGUint32,
                                                                  PGUint32>(Format(
            "SELECT pg_column_size(data), pg_column_size(json_data), pg_column_size(vector_data) "
            "FROM $0 WHERE id = $1",
            table_name, row.id))));

        SCHECK_LT(data_size, kLargeTextSize, IllegalState,
            "data column was not toasted");

        SCHECK_LT(
            json_size,
            common::WriteRapidJsonToString(row.json_data).size(),
            IllegalState,
            "json_data column was not toasted");

        // Assuming vector elements are stored as 4-byte floats
        SCHECK_LT(vector_size, kVectorSize * 4, IllegalState,
            "vector_data column was not toasted");
      }
    }
    return Status::OK();
  }

  // Erases half of the rows from the given expected rows vector.
  // Used in delete tests.
  static void EraseHalfRows(std::vector<TestDataRow>* rows) {
    rows->erase(
        std::remove_if(
            rows->begin(), rows->end(),
            [](const auto& row) { return row.id < (kNumRows * 3) / 2; }),
        rows->end());
  }

  std::optional<PGConn> conn_;

  // Stores the expected rows for each table. Whenever we modify the rows in a table, we update this
  // map. At the end of each test, we verify that the actual rows in the table match the expected
  // rows in this map.
  std::map<std::string, std::vector<TestDataRow>> expected_table_rows_;
};

// Helper function for gtest to print out the TestDataRow object.
void PrintTo(const PgToastTempTableTest::TestDataRow& row, std::ostream* os) {
  *os << "TestDataRow(id=" << row.id << ", data=" << row.data
      << ", json_data=" << common::WriteRapidJsonToString(row.json_data)
      << ", vector_data=" << row.vector_data << ")";
}

// Insert a large value into a temporary table and then copy it to a permanent table
// using a simple INSERT ... SELECT statement.
TEST_F(PgToastTempTableTest, InsertSelect) {
  // Copy data from temporary to permanent table
  ASSERT_OK(
      conn_->ExecuteFormat("INSERT INTO $0 SELECT * FROM $1", kPermanentTableName, kTempTableName));

  expected_table_rows_[kPermanentTableName].insert(
      expected_table_rows_[kPermanentTableName].end(),
      expected_table_rows_[kTempTableName].begin(),
      expected_table_rows_[kTempTableName].end());
  ASSERT_OK(VerifyData());
}

// Check that we aren't detoasting data when inserting into a temp table
TEST_F(PgToastTempTableTest, NegativeTest) {
  // Copy all of the data from the first temp table to the second temp table.
  ASSERT_OK(
      conn_->ExecuteFormat("INSERT INTO $0 SELECT * FROM $1", kTempTableName2, kTempTableName));

  expected_table_rows_[kTempTableName2] = expected_table_rows_[kTempTableName];
  ASSERT_OK(VerifyData());
}

// Test UPDATE ... SET x = (SELECT ... FROM temp_table)
TEST_F(PgToastTempTableTest, UpdateSetFromTemp) {
  ASSERT_OK(conn_->ExecuteFormat(
      "UPDATE $0 "
      "SET data = (SELECT data FROM $1 WHERE $1.id = $0.id), "
      "    json_data = (SELECT json_data FROM $1 WHERE $1.id = $0.id), "
      "    vector_data = (SELECT vector_data FROM $1 WHERE $1.id = $0.id)",
      kPermanentTableName, kTempTableName3));

  expected_table_rows_[kPermanentTableName] = expected_table_rows_[kTempTableName3];
  ASSERT_OK(VerifyData());
}

// Test UPDATE ... SET ... using a CTE (WITH w AS (SELECT ... FROM temp_table) UPDATE ...)
TEST_F(PgToastTempTableTest, UpdateSetWithCTEFromTemp) {
  ASSERT_OK(conn_->ExecuteFormat(
      "WITH w AS (SELECT id, data, json_data, vector_data FROM $0) "
      "UPDATE $1 "
      "SET data = (SELECT data FROM w WHERE $1.id = w.id), "
      "    json_data = (SELECT json_data FROM w WHERE $1.id = w.id), "
      "    vector_data = (SELECT vector_data FROM w WHERE $1.id = w.id)",
      kTempTableName3, kPermanentTableName));

  expected_table_rows_[kPermanentTableName] = expected_table_rows_[kTempTableName3];
  ASSERT_OK(VerifyData());
}

// Test DELETE ... WHERE x IN (SELECT ... FROM temp_table)
// using the data index.
TEST_F(PgToastTempTableTest, DeleteWhereInTempData) {
  std::string query = Format(
      "DELETE /*+ IndexScan($0 $0_data_idx) */ FROM $0 "
      "WHERE data IN ("
      "    SELECT data "
      "    FROM $1 "
      "    WHERE $1.id < $2 * 3 / 2 "
      ")",
      kPermanentTableName, kTempTableName3, kNumRows);
  ASSERT_OK(conn_->Execute(query));
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test DELETE ... WHERE x IN (SELECT ... FROM temp_table)
// using the data index. Uses point deletes rather than a range delete.
TEST_F(PgToastTempTableTest, DeleteWhereDataPoint) {
  for (int i = kNumRows; i < (kNumRows * 3) / 2; i++) {
    std::string query = Format(
        "DELETE FROM $0 WHERE data = (SELECT data FROM $1 WHERE $1.id = $3)",
        kPermanentTableName, kTempTableName3, kNumRows, i);
    ASSERT_OK(conn_->Execute(query));
    ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));
  }

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test DELETE ... WHERE x IN (SELECT ... FROM temp_table)
// using the json index.
TEST_F(PgToastTempTableTest, DeleteWhereInTempJson) {
  std::string query = Format(
      "DELETE FROM $0 perm WHERE perm.json_data @> (SELECT json_data FROM $1 WHERE perm.id = $1.id "
      "AND $1.id < $2 * 3 / 2)",
      kPermanentTableName, kTempTableName3, kNumRows);
  ASSERT_OK(conn_->Execute(query));
  ASSERT_TRUE(ASSERT_RESULT(conn_->HasIndexScan(query)));

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test DELETE ... using a CTE (WITH w AS (SELECT ... FROM temp_table) DELETE ...)
// using the primary key index.
TEST_F(PgToastTempTableTest, DeleteWithCTEFromTempPk) {
  ASSERT_OK(conn_->ExecuteFormat(
      "WITH w AS (SELECT id FROM $0) "
      "DELETE FROM $1 WHERE id IN (SELECT id FROM w WHERE id < $2 * 3 / 2)",
      kTempTableName3, kPermanentTableName, kNumRows));

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test DELETE ... using a CTE (WITH w AS (SELECT ... FROM temp_table) DELETE ...)
// using the data index.
TEST_F(PgToastTempTableTest, DeleteWithCTEFromTempData) {
  ASSERT_OK(conn_->ExecuteFormat(
      "WITH w AS (SELECT data FROM $0) "
      "DELETE FROM $1 WHERE data IN (SELECT data FROM w WHERE id < $2 * 3 / 2)",
      kTempTableName3, kPermanentTableName, kNumRows));

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test DELETE ... using a CTE (WITH w AS (SELECT ... FROM temp_table) DELETE ...)
// using the json index.
TEST_F(PgToastTempTableTest, DeleteWithCTEFromTempJson) {
  for (int i = kNumRows; i < (kNumRows * 3) / 2; i++) {
  ASSERT_OK(conn_->ExecuteFormat(
      "WITH w AS (SELECT id, json_data FROM $0) "
      "DELETE FROM $1 perm WHERE perm.json_data @> (SELECT json_data FROM w WHERE w.id = $3)",
      kTempTableName3, kPermanentTableName, kNumRows, i));
  }

  EraseHalfRows(&expected_table_rows_[kPermanentTableName]);
  ASSERT_OK(VerifyData());
}

// Test updating one column at a time.
TEST_F(PgToastTempTableTest, SingleColumnUpdate) {
  ASSERT_OK(conn_->ExecuteFormat(
      "UPDATE $0 SET data = (SELECT data FROM $1 WHERE $1.id = $0.id - $2)", kPermanentTableName,
      kTempTableName, kNumRows));

  ASSERT_OK(conn_->ExecuteFormat(
      "UPDATE $0 SET json_data = (SELECT json_data FROM $1 WHERE $1.id = $0.id - $2)",
      kPermanentTableName, kTempTableName, kNumRows));

  ASSERT_OK(conn_->ExecuteFormat(
      "UPDATE $0 SET vector_data = (SELECT vector_data FROM $1 WHERE $1.id = $0.id - $2)",
      kPermanentTableName, kTempTableName, kNumRows));

  // Modify the expected data to include the updated values
  for (auto& row : expected_table_rows_[kPermanentTableName]) {
    row.data = GenerateString(row.id - kNumRows, kLargeTextSize);
    row.json_data = GenerateJsonFromString(row.data);
    row.vector_data = GenerateVector(row.id - kNumRows, kVectorSize);
  }
  ASSERT_OK(VerifyData());
}

// Tests deleting by a large toastable column when it is being used
// as the primary key.
TEST_F(PgToastTempTableTest, DeleteFromDataPk) {
  ASSERT_OK(conn_->ExecuteFormat(
      "DELETE FROM $1 WHERE data IN (SELECT data FROM $0 WHERE id < $2 * 3 / 2)",
      kTempTableName3, kPermanentTableDataPkName, kNumRows));

  EraseHalfRows(&expected_table_rows_[kPermanentTableDataPkName]);
  ASSERT_OK(VerifyData());
}

// Check that refreshing a materialized view (permanent table -> temp table -> permanent table)
// works correctly.
TEST_F(PgToastTempTableTest, RefreshMaterializedView) {
  ASSERT_OK(conn_->ExecuteFormat("REFRESH MATERIALIZED VIEW CONCURRENTLY $0", kMatViewName));

  expected_table_rows_[kMatViewName] = expected_table_rows_[kPermanentTableName];
  ASSERT_OK(VerifyData());
}

}  // namespace yb::pgwrapper
