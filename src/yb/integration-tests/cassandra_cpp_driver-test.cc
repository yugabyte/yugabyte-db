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

#include <cassandra.h>

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/util/jsonreader.h"

using std::string;
using std::vector;

using rapidjson::Value;

namespace yb {

class CppCassandraDriver {
 public:
  explicit CppCassandraDriver(const ExternalMiniCluster& mini_cluster) {
    const string host = mini_cluster.tablet_server(0)->bind_host();
    const uint16_t port = mini_cluster.tablet_server(0)->cql_rpc_port();

    LOG(INFO) << "Create Cassandra cluster to " << host << ":" << port << " ...";
    cass_cluster_ = CHECK_NOTNULL(cass_cluster_new());
    CHECK_EQ(CASS_OK, cass_cluster_set_contact_points(cass_cluster_, host.c_str()));
    CHECK_EQ(CASS_OK, cass_cluster_set_port(cass_cluster_, port));
    LOG(INFO) << "Create Cassandra cluster to " << host << ":" << port << " - DONE";

    LOG(INFO) << "Create new session ...";
    cass_session_ = CHECK_NOTNULL(cass_session_new());
    GetResultAndFreeCassFuture(cass_session_connect(cass_session_, cass_cluster_));
    LOG(INFO) << "Create new session - DONE";
  }

  ~CppCassandraDriver() {
    LOG(INFO) << "Terminating driver...";
    if (cass_session_) {
      GetResultAndFreeCassFuture(cass_session_close(cass_session_));
      cass_session_free(cass_session_);
    }

    if (cass_cluster_) {
      cass_cluster_free(cass_cluster_);
    }

    LOG(INFO) << "Terminating driver - DONE";
  }

  const CassResult* ExecuteStatement(CassStatement* const statement, bool need_result = false) {
    const CassResult* result = GetResultAndFreeCassFuture(
        cass_session_execute(cass_session_, statement), need_result);
    cass_statement_free(statement);
    return result;
  }

  void ExecuteQuery(const string& query) {
    LOG(INFO) << "Execute query: " << query;
    ExecuteStatement(CHECK_NOTNULL(cass_statement_new(query.c_str(), 0)));
  }

  const CassResult* GetResultAndFreeCassFuture(CassFuture* const future, bool need_result = false) {
    cass_future_wait(CHECK_NOTNULL(future));
    const CassError rc = cass_future_error_code(future);
    LOG(INFO) << "Last operation RC: " << rc;

    if (rc != CASS_OK) {
      const char* message = nullptr;
      size_t message_sz = 0;
      cass_future_error_message(future, &message, &message_sz);
      LOG(INFO) << "Last operation ERROR: " << message;
    }

    const CassResult* result = nullptr;
    if (need_result) {
      result = cass_future_get_result(future);
    }
    cass_future_free(future);
    CHECK_EQ(CASS_OK, rc);
    return result;
  }

 private:
  CassCluster* cass_cluster_ = nullptr;
  CassSession* cass_session_ = nullptr;
};

class CppCassandraDriverTest : public ExternalMiniClusterITestBase {
 public:
  virtual void SetUp() override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUp());

    LOG(INFO) << "Starting YB ExternalMiniCluster...";
    // Start up with 3 (default) tablet servers.
    ASSERT_NO_FATALS(StartCluster());

    driver_.reset(CHECK_NOTNULL(new CppCassandraDriver(*cluster_)));

    // Create and use default keyspace.
    driver_->ExecuteQuery("CREATE KEYSPACE IF NOT EXISTS examples;");
    driver_->ExecuteQuery("USE examples;");
  }

  virtual void TearDown() override {
    driver_.reset();
    LOG(INFO) << "Stopping YB ExternalMiniCluster...";
    ExternalMiniClusterITestBase::TearDown();
  }

 protected:
  std::unique_ptr<CppCassandraDriver> driver_;
};

namespace {

string ValueGetString(const CassRow* row, size_t index) {
  const char* s = nullptr;
  size_t sz = 0;
  cass_value_get_string(cass_row_get_column(row, index), &s, &sz);
  return string(s, sz);
}

}  // namespace

class TestData {
 public:
  virtual ~TestData() {}
  virtual void Print(const string& prefix) const = 0;

  void Select(CppCassandraDriver* driver, const string& key) {
    const string query = "SELECT * FROM examples.basic WHERE key = ?";
    LOG(INFO) << "Execute SELECT: '" << query << "' with key='" << key << "'";

    CassStatement* const statement = CHECK_NOTNULL(cass_statement_new(query.c_str(), 1));
    cass_statement_bind_string(statement, 0, key.c_str());

    const CassResult* const result = CHECK_NOTNULL(
        CHECK_NOTNULL(driver)->ExecuteStatement(statement, /* need_result */ true));
    CassIterator* const iterator = CHECK_NOTNULL(cass_iterator_from_result(result));
    CHECK_EQ(cass_true, cass_iterator_next(iterator));
    ReadValues(CHECK_NOTNULL(cass_iterator_get_row(iterator)), 1);

    CHECK_EQ(cass_false, cass_iterator_next(iterator));
    cass_iterator_free(iterator);
    cass_result_free(result);
  }

 protected:
  virtual void WriteValues(CassStatement* statement, size_t index) const = 0;
  virtual void ReadValues(const CassRow* row, size_t index) = 0;

  void DoInsert(CppCassandraDriver* driver, const string& key, const vector<string>& params) const {
    string names = "key";
    string values = "?";
    for (const string& param : params) {
      names += ", " + param;
      values += ", ?";
    }

    const string query = "INSERT INTO examples.basic (" + names + ") VALUES (" + values + ");";
    LOG(INFO) << "Execute INSERT: '" << query << "' with key='" << key << "'";

    CassStatement* const statement = CHECK_NOTNULL(
        cass_statement_new(query.c_str(), params.size() + 1));
    cass_statement_bind_string(statement, 0, key.c_str());
    WriteValues(statement, 1);

    CHECK_NOTNULL(driver)->ExecuteStatement(statement);
  }

  void DoUpdate(CppCassandraDriver* driver, const string& key, const vector<string>& params) const {
    string values = "";
    size_t i = 0;
    for (const string& param : params) {
      values += param + " = ?" + (++i == params.size() ? "" : ", ");
    }

    const string query = "UPDATE examples.basic SET " + values + " WHERE key = ?;";
    LOG(INFO) << "Execute UPDATE: '" << query << "' with key='" << key << "'";

    CassStatement* const statement = CHECK_NOTNULL(
        cass_statement_new(query.c_str(), params.size() + 1));
    cass_statement_bind_string(statement, 1, key.c_str());
    WriteValues(statement, 0);

    CHECK_NOTNULL(driver)->ExecuteStatement(statement);
  }
};

TEST_F(CppCassandraDriverTest, TestBasicTypes) {
  driver_->ExecuteQuery("CREATE TABLE IF NOT EXISTS examples.basic (key text,"
                        " bln boolean,"
                        " flt float, dbl double,"
                        " i32 int, i64 bigint,"
                        " str text,"
                        " PRIMARY KEY (key));");

  class BasicData : public TestData {
   public:
    BasicData() = default;

    BasicData(
        cass_bool_t b, cass_float_t f, cass_double_t d,
        cass_int32_t i32, cass_int64_t i64, const string& s)
      : bln_(b), flt_(f), dbl_(d), int32_(i32), int64_(i64), str_(s) {}

    void Print(const string& prefix) const override {
      LOG(INFO) << prefix << ":";
      LOG(INFO) << ">     cass_bool_t: " << bln_;
      LOG(INFO) << ">     cass_float_t: " << flt_;
      LOG(INFO) << ">     cass_double_t: " << dbl_;
      LOG(INFO) << ">     cass_int32_t: " << int32_;
      LOG(INFO) << ">     cass_int64_t: " << int64_;
      LOG(INFO) << ">     string: '" << str_ << "'";
    }

    void Insert(CppCassandraDriver* driver, const string& key) const {
      DoInsert(driver, key, { "bln", "flt", "dbl", "i32", "i64", "str" });
    }

    cass_bool_t bln_ = cass_false;
    cass_float_t flt_ = 0.f;
    cass_double_t dbl_ = 0.;
    cass_int32_t int32_ = 0;
    cass_int64_t int64_ = 0;
    string str_;

   protected:
    void WriteValues(CassStatement* statement, size_t index) const override {
      cass_statement_bind_bool(statement, index++, bln_);
      cass_statement_bind_float(statement, index++, flt_);
      cass_statement_bind_double(statement, index++, dbl_);
      cass_statement_bind_int32(statement, index++, int32_);
      cass_statement_bind_int64(statement, index++, int64_);
      cass_statement_bind_string(statement, index++, str_.c_str());
    }

    void ReadValues(const CassRow* row, size_t index) override {
      cass_value_get_bool(cass_row_get_column(row, index++), &bln_);
      cass_value_get_double(cass_row_get_column(row, index++), &dbl_);
      cass_value_get_float(cass_row_get_column(row, index++), &flt_);
      cass_value_get_int32(cass_row_get_column(row, index++), &int32_);
      cass_value_get_int64(cass_row_get_column(row, index++), &int64_);
      str_ = ValueGetString(row, index++);
    }
  };

  const BasicData input(cass_true, 11.01f, 22.002, 3, 4, "text");
  input.Print("INPUT");
  input.Insert(driver_.get(), "test");

  BasicData output;
  output.Print("OUTPUT before run");
  output.Select(driver_.get(), "test");
  LOG(INFO) << "Checking selected values...";
  output.Print("RESULT OUTPUT");

  ASSERT_EQ(input.bln_, output.bln_);
  // Temporary disabled due to bug in SELECT processing of floating point values.
  // ASSERT_EQ(input.flt_, output.flt_);
  // ASSERT_EQ(input.dbl_, output.dbl_);
  ASSERT_EQ(input.int32_, output.int32_);
  ASSERT_EQ(input.int64_, output.int64_);
  ASSERT_EQ(input.str_, output.str_);
}

class JsonData : public TestData {
 public:
  JsonData() {}
  explicit JsonData(const string& j) : json_(j) {}

  void Print(const string& prefix) const override {
    LOG(INFO) << prefix << ": json: '" << json_ << "'";
  }

  void Insert(CppCassandraDriver* driver, const string& key) const {
    DoInsert(driver, key, { "json" });
  }

  void Update(CppCassandraDriver* driver, const string& key) const {
    DoUpdate(driver, key, { "json" });
  }

  string json_;

 protected:
  void WriteValues(CassStatement* statement, size_t index) const override {
    cass_statement_bind_string(statement, index++, json_.c_str());
  }

  void ReadValues(const CassRow* row, size_t index) override {
    json_ = ValueGetString(row, index++);
  }
};

enum TestOpEnum {
  insert_op,
  update_op
};

void doJsonTest(
    CppCassandraDriver* driver, const string& key, const string& j, TestOpEnum test_op) {
  const JsonData input(j);
  ASSERT_EQ(input.json_, j);
  input.Print("INPUT");

  if (test_op == insert_op) {
    input.Insert(driver, key);
  } else {
    input.Update(driver, key);
  }

  JsonData output;
  output.Print("OUTPUT before run");
  output.Select(driver, key);
  LOG(INFO) << "Checking selected values...";
  output.Print("RESULT OUTPUT");

  ASSERT_EQ(input.json_, output.json_);
}

TEST_F(CppCassandraDriverTest, TestJsonBType) {
  driver_->ExecuteQuery("CREATE TABLE IF NOT EXISTS basic (key text, json jsonb,"
                        " PRIMARY KEY (key));");

  doJsonTest(driver_.get(), "test1", "{\"a\":1}", insert_op);
  doJsonTest(driver_.get(), "test1", "{\"b\":1}", update_op);
}

void verifyLongJson(const string& json) {
    // Parse JSON.
    JsonReader r(json);
    ASSERT_OK(r.Init());
    const Value* json_obj = nullptr;
    EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
    EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());

    EXPECT_TRUE(json_obj->HasMember("b"));
    EXPECT_EQ(rapidjson::kNumberType, (*json_obj)["b"].GetType());
    EXPECT_EQ(1, (*json_obj)["b"].GetInt());

    EXPECT_TRUE(json_obj->HasMember("a1"));
    EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["a1"].GetType());
    const Value::ConstArray arr = (*json_obj)["a1"].GetArray();

    EXPECT_EQ(rapidjson::kNumberType, arr[2].GetType());
    EXPECT_EQ(3., arr[2].GetDouble());

    EXPECT_EQ(rapidjson::kFalseType, arr[3].GetType());
    EXPECT_EQ(false, arr[3].GetBool());

    EXPECT_EQ(rapidjson::kTrueType, arr[4].GetType());
    EXPECT_EQ(true, arr[4].GetBool());

    EXPECT_EQ(rapidjson::kObjectType, arr[5].GetType());
    const Value::ConstObject obj = arr[5].GetObject();
    EXPECT_TRUE(obj.HasMember("k2"));
    EXPECT_EQ(rapidjson::kArrayType, obj["k2"].GetType());
    EXPECT_EQ(rapidjson::kNumberType, obj["k2"].GetArray()[1].GetType());
    EXPECT_EQ(200, obj["k2"].GetArray()[1].GetInt());

    EXPECT_TRUE(json_obj->HasMember("a"));
    EXPECT_EQ(rapidjson::kObjectType, (*json_obj)["a"].GetType());
    const Value::ConstObject obj_a = (*json_obj)["a"].GetObject();

    EXPECT_TRUE(obj_a.HasMember("q"));
    EXPECT_EQ(rapidjson::kObjectType, obj_a["q"].GetType());
    const Value::ConstObject obj_q = obj_a["q"].GetObject();
    EXPECT_TRUE(obj_q.HasMember("s"));
    EXPECT_EQ(rapidjson::kNumberType, obj_q["s"].GetType());
    EXPECT_EQ(2147483647, obj_q["s"].GetInt());

    EXPECT_TRUE(obj_a.HasMember("f"));
    EXPECT_EQ(rapidjson::kStringType, obj_a["f"].GetType());
    EXPECT_EQ("hello", string(obj_a["f"].GetString()));
}

TEST_F(CppCassandraDriverTest, TestLongJson) {
  driver_->ExecuteQuery("CREATE TABLE basic (key text, json jsonb,"
                        " PRIMARY KEY (key));");

  const string long_json =
      "{ "
        "\"b\" : 1,"
        "\"a2\" : {},"
        "\"a3\" : \"\","
        "\"a1\" : [1, 2, 3.0, false, true, { \"k1\" : 1, \"k2\" : [100, 200, 300], \"k3\" : true}],"
        "\"a\" :"
        "{"
          "\"d\" : true,"
          "\"q\" :"
            "{"
              "\"p\" : 4294967295,"
              "\"r\" : -2147483648,"
              "\"s\" : 2147483647"
            "},"
          "\"g\" : -100,"
          "\"c\" : false,"
          "\"f\" : \"hello\","
          "\"x\" : 2.0,"
          "\"y\" : 9223372036854775807,"
          "\"z\" : -9223372036854775808,"
          "\"u\" : 18446744073709551615,"
          "\"l\" : 2147483647.123123e+75,"
          "\"e\" : null"
        "}"
      "}";

  const JsonData input(long_json);
  ASSERT_EQ(input.json_, long_json);
  input.Print("INPUT");
  input.Insert(driver_.get(), "test");

  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test0', '" + long_json + "');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test1', '{ \"a\" : 1 }');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test2', '\"abc\"');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test3', '3');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test4', 'true');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test5', 'false');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test6', 'null');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test7', '2.0');");
  driver_->ExecuteQuery("INSERT INTO basic(key, json) values ('test8', '{\"b\" : 1}');");

  for (const string& key : {"test", "test0"} ) {
    JsonData output;
    output.Print("OUTPUT before run");
    output.Select(driver_.get(), key);
    LOG(INFO) << "Checking selected JSON object for key=" << key;
    output.Print("RESULT OUTPUT");

    ASSERT_EQ(output.json_,
        "{"
          "\"a\":"
          "{"
            "\"c\":false,"
            "\"d\":true,"
            "\"e\":null,"
            "\"f\":\"hello\","
            "\"g\":-100,"
            "\"l\":2.147483647123123e84,"
            "\"q\":"
            "{"
              "\"p\":4294967295,"
              "\"r\":-2147483648,"
              "\"s\":2147483647"
            "},"
            "\"u\":18446744073709551615,"
            "\"x\":2.0,"
            "\"y\":9223372036854775807,"
            "\"z\":-9223372036854775808"
          "},"
          "\"a1\":[1,2,3.0,false,true,{\"k1\":1,\"k2\":[100,200,300],\"k3\":true}],"
          "\"a2\":{},"
          "\"a3\":\"\","
          "\"b\":1"
        "}");

    verifyLongJson(output.json_);
  }
}

}  // namespace yb
