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

#include <tuple>

// Include driver internal headers first.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-local-typedefs"

#include "partition_aware_policy.hpp"
#include "statement.hpp"

#pragma GCC diagnostic pop
// Undefine conflicting macros.
#undef HAVE_LONG_LONG
#undef DECLARE_POD
#undef PROPAGATE_POD_FROM_TEMPLATE_ARGUMENT
#undef ENFORCE_POD

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/util/metrics.h"
#include "yb/util/jsonreader.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::vector;
using std::ostringstream;
using std::unique_ptr;
using std::tuple;
using std::get;

using rapidjson::Value;
using strings::Substitute;

METRIC_DECLARE_entity(server);
METRIC_DECLARE_histogram(handler_latency_yb_client_write_remote);
METRIC_DECLARE_histogram(handler_latency_yb_client_read_remote);
METRIC_DECLARE_histogram(handler_latency_yb_client_write_local);
METRIC_DECLARE_histogram(handler_latency_yb_client_read_local);

namespace yb {

//------------------------------------------------------------------------------

class CppCassandraDriver {
 public:
  explicit CppCassandraDriver(const ExternalMiniCluster& mini_cluster) {

#define USE_LOCAL_CLUSTER 0
#if USE_LOCAL_CLUSTER // Local cluster for testing
    const uint16_t port = 9042;
    const string hosts = "127.0.0.1,127.0.0.2,127.0.0.3";
#else
    const uint16_t port = mini_cluster.tablet_server(0)->cql_rpc_port();
    string hosts = mini_cluster.tablet_server(0)->bind_host();

    for (int i = 1; i < mini_cluster.num_tablet_servers(); ++i) {
      hosts += ',' + mini_cluster.tablet_server(i)->bind_host();
    }
#endif

    // Enable detailed tracing inside driver.
    cass_log_set_level(CASS_LOG_TRACE);

    LOG(INFO) << "Create Cassandra cluster to " << hosts << " :" << port << " ...";
    cass_cluster_ = CHECK_NOTNULL(cass_cluster_new());
    CHECK_EQ(CASS_OK, cass_cluster_set_contact_points(cass_cluster_, hosts.c_str()));
    CHECK_EQ(CASS_OK, cass_cluster_set_port(cass_cluster_, port));

    // Setup cluster configuration: partitions metadata refresh timer = 3 seconds.
    cass_cluster_set_partition_aware_routing(cass_cluster_, cass_true, 3);

    LOG(INFO) << "Create new session ...";
    cass_session_ = CHECK_NOTNULL(cass_session_new());
    CheckAndFreeCassFuture(cass_session_connect(cass_session_, cass_cluster_));
    LOG(INFO) << "Create new session - DONE";
  }

  ~CppCassandraDriver() {
    LOG(INFO) << "Terminating driver...";
    if (cass_session_) {
      CheckAndFreeCassFuture(cass_session_close(cass_session_));
      cass_session_free(cass_session_);
      cass_session_ = nullptr;
    }

    if (cass_cluster_) {
      cass_cluster_free(cass_cluster_);
      cass_cluster_ = nullptr;
    }

    LOG(INFO) << "Terminating driver - DONE";
  }

  void ExecuteStatement(CassStatement* const statement) {
    CheckAndFreeCassFuture(cass_session_execute(cass_session_, CHECK_NOTNULL(statement)));
    cass_statement_free(statement);
  }

  void ExecuteQuery(const string& query) {
    LOG(INFO) << "Execute query: " << query;
    ExecuteStatement(cass_statement_new(query.c_str(), 0));
  }

  const CassResult* ExecuteStatementWithResult(CassStatement* const statement) {
    const CassResult* result = GetResultAndFreeCassFuture(
        cass_session_execute(cass_session_, CHECK_NOTNULL(statement)));
    cass_statement_free(statement);
    return result;
  }

  const CassPrepared* ExecutePrepare(const string& prepare_query) {
    LOG(INFO) << "Execute prepare request: " << prepare_query;
    return GetPreparedAndFreeCassFuture(cass_session_prepare(cass_session_, prepare_query.c_str()));
  }

  void CheckAndFreeCassFuture(CassFuture* const future) {
    WaitAndCheckCassFuture(future);
    cass_future_free(future);
  }

  const CassResult* GetResultAndFreeCassFuture(CassFuture* const future) {
    WaitAndCheckCassFuture(future);

    const CassResult* const result = CHECK_NOTNULL(cass_future_get_result(future));
    cass_future_free(future);
    return result;
  }

  const CassPrepared* GetPreparedAndFreeCassFuture(CassFuture* const future) {
    WaitAndCheckCassFuture(future);

    const CassPrepared* const prepared = CHECK_NOTNULL(cass_future_get_prepared(future));
    cass_future_free(future);
    return prepared;
  }

 protected:
  void WaitAndCheckCassFuture(CassFuture* const future) {
    cass_future_wait(CHECK_NOTNULL(future));
    const CassError rc = cass_future_error_code(future);
    LOG(INFO) << "Last operation RC: " << rc;

    if (rc != CASS_OK) {
      const char* message = nullptr;
      size_t message_sz = 0;
      cass_future_error_message(future, &message, &message_sz);
      LOG(INFO) << "Last operation ERROR: " << message;
    }

    CHECK_EQ(CASS_OK, rc);
  }

 private:
  CassCluster* cass_cluster_ = nullptr;
  CassSession* cass_session_ = nullptr;
};

//------------------------------------------------------------------------------

class CppCassandraDriverTest : public ExternalMiniClusterITestBase {
 public:
  void SetUp() override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUp());

    LOG(INFO) << "Starting YB ExternalMiniCluster...";
    // Start up with 3 (default) tablet servers.
    ASSERT_NO_FATALS(StartCluster());

    driver_.reset(CHECK_NOTNULL(new CppCassandraDriver(*cluster_)));

    // Create and use default keyspace.
    driver_->ExecuteQuery("CREATE KEYSPACE IF NOT EXISTS examples;");
    driver_->ExecuteQuery("USE examples;");
  }

  void SetUpCluster(ExternalMiniClusterOptions* opts) override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUpCluster(opts));

    opts->bind_to_unique_loopback_addresses = true;
    opts->use_same_ts_ports = true;
  }

  void TearDown() override {
    driver_.reset();
    LOG(INFO) << "Stopping YB ExternalMiniCluster...";
    ExternalMiniClusterITestBase::TearDown();
  }

 protected:
  unique_ptr<CppCassandraDriver> driver_;
};

//------------------------------------------------------------------------------

struct cass_json_t {
  cass_json_t() = default;
  cass_json_t(const string& s) : str_(s) {} // NOLINT
  cass_json_t(const char* s) : str_(s) {} // NOLINT

  cass_json_t& operator =(const string& s) {
    str_ = s;
    return *this;
  }
  cass_json_t& operator =(const char* s) {
    return operator =(string(s));
  }

  bool operator ==(const cass_json_t& j) const { return str_ == j.str_; }
  bool operator !=(const cass_json_t& j) const { return !operator ==(j); }
  bool operator <(const cass_json_t& j) const { return str_ < j.str_; }

  string str_;
};

std::ostream& operator <<(std::ostream& s, const cass_json_t& v) {
  return s << v.str_;
}

//------------------------------------------------------------------------------

namespace util {

template<class T>
string type_name() { return "unknown"; } // COMPILATION ERROR: Specialize it for your type!
// Supported types - get type name:
template<> string type_name<string>() { return "text"; }
template<> string type_name<cass_bool_t>() { return "boolean"; }
template<> string type_name<cass_float_t>() { return "float"; }
template<> string type_name<cass_double_t>() { return "double"; }
template<> string type_name<cass_int32_t>() { return "int"; }
template<> string type_name<cass_int64_t>() { return "bigint"; }
template<> string type_name<cass_json_t>() { return "jsonb"; }

// Supported types - bind value:
void bind(CassStatement* statement, size_t index, const string& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_string(statement, index, v.c_str()));
}

void bind(CassStatement* statement, size_t index, const cass_bool_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_bool(statement, index, v));
}

void bind(CassStatement* statement, size_t index, const cass_float_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_float(statement, index, v));
}

void bind(CassStatement* statement, size_t index, const cass_double_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_double(statement, index, v));
}

void bind(CassStatement* statement, size_t index, const cass_int32_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_int32(statement, index, v));
}

void bind(CassStatement* statement, size_t index, const cass_int64_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_int64(statement, index, v));
}

void bind(CassStatement* statement, size_t index, const cass_json_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_string(statement, index, v.str_.c_str()));
}

// Supported types - read value:
void read(const CassValue* val, string* v) {
  const char* s = nullptr;
  size_t sz = 0;
  CHECK_EQ(CASS_OK, cass_value_get_string(val, &s, &sz));
  *v = string(s, sz);
}

void read(const CassValue* val, cass_bool_t* v) {
  CHECK_EQ(CASS_OK, cass_value_get_bool(val, v));
}

void read(const CassValue* val, cass_float_t* v) {
  CHECK_EQ(CASS_OK, cass_value_get_float(val, v));
}

void read(const CassValue* val, cass_double_t* v) {
  CHECK_EQ(CASS_OK, cass_value_get_double(val, v));
}

void read(const CassValue* val, cass_int32_t* v) {
  CHECK_EQ(CASS_OK, cass_value_get_int32(val, v));
}

void read(const CassValue* val, cass_int64_t* v) {
  CHECK_EQ(CASS_OK, cass_value_get_int64(val, v));
}

void read(const CassValue* val, cass_json_t* v) {
  read(val, &v->str_);
}

} // namespace util

//------------------------------------------------------------------------------

template <typename... ColumnsTypes>
class TestTable {
 public:
  typedef vector<string> StringVec;
  typedef tuple<ColumnsTypes...> ColumnsTuple;

  explicit TestTable(const unique_ptr<CppCassandraDriver>& driver)
      : driver_(CHECK_NOTNULL(driver.get())) {}

  void CreateTable(const string& table, const StringVec& columns, const StringVec& keys) {
    table_name_ = table;
    column_names_ = columns;
    key_names_ = keys;

    for (string& k : key_names_) {
      TrimString(&k, "()"); // Cut parentheses if available.
    }

    const string query = create_table_str(table, columns, keys);
    CHECK_NOTNULL(driver_)->ExecuteQuery(query);
  }

  void Print(const string& prefix, const ColumnsTuple& data) const {
    LOG(INFO) << prefix << ":";

    StringVec types;
    do_get_type_names(&types, data);
    CHECK_EQ(types.size(), column_names_.size());

    StringVec values;
    do_get_values(&values, data);
    CHECK_EQ(values.size(), column_names_.size());

    for (int i = 0; i < column_names_.size(); ++i) {
      LOG(INFO) << ">     " << column_names_[i] << ' ' << types[i] << ": " << values[i];
    }
  }

  void BindInsert(CassStatement* statement, const ColumnsTuple& data) const {
    do_bind_values(
        CHECK_NOTNULL(statement), /* keys_only = */ false, /* values_first = */ false, data);
  }

  void Insert(const ColumnsTuple& data) const {
    const string query = insert_with_bindings_str(table_name_, column_names_);
    Print("Execute: '" + query + "' with data", data);

    CassStatement* const statement = cass_statement_new(query.c_str(), column_names_.size());
    BindInsert(statement, data);
    CHECK_NOTNULL(driver_)->ExecuteStatement(statement);
  }

  const CassPrepared* PrepareInsert() const {
    return CHECK_NOTNULL(driver_)->ExecutePrepare(
        insert_with_bindings_str(table_name_, column_names_));
  }

  void Update(const ColumnsTuple& data) const {
    const string query = update_with_bindings_str(table_name_, column_names_, key_names_);
    Print("Execute: '" + query + "' with data", data);

    CassStatement* const statement = CHECK_NOTNULL(
        cass_statement_new(query.c_str(), column_names_.size()));
    do_bind_values(statement, /* keys_only = */ false, /* values_first = */ true, data);

    CHECK_NOTNULL(driver_)->ExecuteStatement(statement);
  }

  void SelectOneRow(ColumnsTuple* data) {
    const string query = select_with_bindings_str(table_name_, key_names_);
    Print("Execute: '" + query + "' with data", *data);

    CassStatement* const statement = CHECK_NOTNULL(
        cass_statement_new(query.c_str(), key_names_.size()));
    do_bind_values(statement, /* keys_only = */ true, /* values_first = */ false, *data);
    ExecuteAndReadOneRow(statement, data);
  }

  void SelectByToken(ColumnsTuple* data, int64_t token) {
    const string query = select_by_token_str(table_name_, key_names_);
    Print("Execute: '" + query + "' with data", *data);

    CassStatement* const statement = CHECK_NOTNULL(cass_statement_new(query.c_str(), 1));
    util::bind(statement, 0, token);
    ExecuteAndReadOneRow(statement, data);
  }

  void ExecuteAndReadOneRow(CassStatement* statement, ColumnsTuple* data) {
    const CassResult* const result = CHECK_NOTNULL(
        CHECK_NOTNULL(driver_)->ExecuteStatementWithResult(CHECK_NOTNULL(statement)));
    CassIterator* const iterator = CHECK_NOTNULL(cass_iterator_from_result(result));
    CHECK_EQ(cass_true, cass_iterator_next(iterator));
    const CassRow* const row = CHECK_NOTNULL(cass_iterator_get_row(iterator));
    do_read_values(row, data);

    CHECK_EQ(cass_false, cass_iterator_next(iterator));
    cass_iterator_free(iterator);
    cass_result_free(result);
  }

 protected:
  // Tuple unrolling methods.
  static void get_type_names(StringVec*, const tuple<>&) {}

  template<typename T, typename... A>
  static void get_type_names(StringVec* types, const tuple<T, A...>& t) {
    types->push_back(util::type_name<T>());
    get_type_names(types, tuple<A...>());
  }

  static void do_get_type_names(StringVec* types, const ColumnsTuple& t) {
    get_type_names<ColumnsTypes...>(types, t);
  }

  template<size_t I>
  static void get_values(StringVec*, const ColumnsTuple&) {}

  template<size_t I, typename T, typename... A>
  static void get_values(StringVec* values, const ColumnsTuple& data) {
    ostringstream ss;
    ss << get<I>(data);
    values->push_back(ss.str());
    get_values<I + 1, A...>(values, data);
  }

  static void do_get_values(StringVec* values, const ColumnsTuple& data) {
    get_values<0, ColumnsTypes...>(values, data);
  }

  template<size_t I>
  void bind_values(CassStatement*, size_t*, bool, bool, const ColumnsTuple&) const {}

  template<size_t I, typename T, typename... A>
  void bind_values(
      CassStatement* statement, size_t* index, bool use_values, bool use_keys,
      const ColumnsTuple& data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if ((this_is_key && use_keys) || (!this_is_key && use_values)) {
      util::bind(statement, (*index)++, get<I>(data));
    }

    bind_values<I + 1, A...>(statement, index, use_values, use_keys, data);
  }

  void do_bind_values(
      CassStatement* statement, bool keys_only, bool values_first, const ColumnsTuple& data) const {
    size_t i = 0;
    if (keys_only) {
      bind_values<0, ColumnsTypes...>(
          statement, &i, false /* use_values */, true /* use_keys */, data);
    } else if (values_first) {
      // Bind values.
      bind_values<0, ColumnsTypes...>(
          statement, &i, true /* use_values */, false /* use_keys */, data);
      // Bind keys.
      bind_values<0, ColumnsTypes...>(
          statement, &i, false /* use_values */, true /* use_keys */, data);
    } else {
      bind_values<0, ColumnsTypes...>(
          statement, &i, true /* use_values */, true /* use_keys */, data);
    }
  }

  template<size_t I>
  void read_values(const CassRow*, size_t*, bool, bool, ColumnsTuple*) const {}

  template<size_t I, typename T, typename... A>
  void read_values(
      const CassRow* row, size_t* index, bool use_values, bool use_keys, ColumnsTuple* data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if ((this_is_key && use_keys) || (!this_is_key && use_values)) {
      const CassValue* val = cass_row_get_column(row, (*index)++);
      util::read(val, &get<I>(*data));
    }

    read_values<I + 1, A...>(row, index, use_values, use_keys, data);
  }

  void do_read_values(const CassRow* row, ColumnsTuple* data) const {
    size_t i = 0;
    // Read keys.
    read_values<0, ColumnsTypes...>(row, &i, false /* use_values */, true /* use_keys */, data);
    // Read values.
    read_values<0, ColumnsTypes...>(row, &i, true /* use_values */, false /* use_keys */, data);
  }

  // Strings for CQL requests.

  static string create_table_str(
      const string& table, const StringVec& columns, const StringVec& keys) {
    CHECK_GT(columns.size(), 0);
    CHECK_GT(keys.size(), 0);
    CHECK_GE(columns.size(), keys.size());

    StringVec types;
    do_get_type_names(&types, ColumnsTuple());
    CHECK_EQ(types.size(), columns.size());

    for (int i = 0; i < columns.size(); ++i) {
      types[i] = columns[i] + ' ' + types[i];
    }

    return "CREATE TABLE IF NOT EXISTS " + table + " (" +
        JoinStrings(types, ", ") + ", PRIMARY KEY (" + JoinStrings(keys, ", ") + "));";
  }

  static string insert_with_bindings_str(const string& table, const StringVec& columns) {
    CHECK_GT(columns.size(), 0);
    const StringVec values(columns.size(), "?");

    return "INSERT INTO " + table + " (" +
        JoinStrings(columns, ", ") + ") VALUES (" + JoinStrings(values, ", ") + ");";
  }

  static string update_with_bindings_str(
      const string& table, const StringVec& columns, const StringVec& keys) {
    CHECK_GT(columns.size(), 0);
    CHECK_GT(keys.size(), 0);
    CHECK_GE(columns.size(), keys.size());
    StringVec values, key_values;

    for (const string& col : columns) {
      (is_key(col, keys) ? key_values : values).push_back(col + " = ?");
    }

    return "UPDATE " + table + " SET " +
        JoinStrings(values, ", ") + " WHERE " + JoinStrings(key_values, ", ") + ";";
  }

  static string select_with_bindings_str(const string& table, const StringVec& keys) {
    CHECK_GT(keys.size(), 0);
    StringVec key_values = keys;

    for (string& k : key_values) {
       k += " = ?";
    }

    return "SELECT * FROM " + table + " WHERE " + JoinStrings(key_values, " AND ") + ";";
  }

  static string select_by_token_str(const string& table, const StringVec& keys) {
    CHECK_GT(keys.size(), 0);
    return "SELECT * FROM " + table + " WHERE TOKEN(" + JoinStrings(keys, ", ") + ") = ?;";
  }

  static bool is_key(const string& name, const StringVec& keys) {
    return find(keys.begin(), keys.end(), name) != keys.end();
  }

 protected:
  CppCassandraDriver* driver_;
  string table_name_;
  StringVec column_names_;
  StringVec key_names_;
};

//------------------------------------------------------------------------------

template <typename... Types>
struct TupleComparer {
  typedef tuple<Types...> TupleType;

  template<size_t I>
  static void expect_eq(const TupleType&, const TupleType&) {}

  template<size_t I, typename T, typename... A>
  static void expect_eq(const TupleType& t1, const TupleType& t2) {
    EXPECT_EQ(get<I>(t1), get<I>(t2));
    LOG(INFO) << "COMPARE: " << get<I>(t1) << " == " << get<I>(t2);
    expect_eq<I + 1, A...>(t1, t2);
  }

  static void do_expect_eq(const TupleType& t1, const TupleType& t2) {
    expect_eq<0, Types...>(t1, t2);
  }
};

template <typename... Types>
void check_equal_tuples(const tuple<Types...>& t1, const tuple<Types...>& t2) {
  TupleComparer<Types...>::do_expect_eq(t1, t2);
}

//------------------------------------------------------------------------------

TEST_F(CppCassandraDriverTest, TestBasicTypes) {
  typedef TestTable<
      string, cass_bool_t, cass_float_t, cass_double_t, cass_int32_t, cass_int64_t, string> MyTable;
  MyTable table(driver_);
  table.CreateTable("examples.basic", {"key", "bln", "flt", "dbl", "i32", "i64", "str"}, {"key"});

  const MyTable::ColumnsTuple input("test", cass_true, 11.01f, 22.002, 3, 4, "text");
  table.Insert(input);

  MyTable::ColumnsTuple output("test", cass_false, 0.f, 0., 0, 0, "");
  table.SelectOneRow(&output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  check_equal_tuples(input, output);
}

TEST_F(CppCassandraDriverTest, TestJsonBType) {
  typedef TestTable<string, cass_json_t> MyTable;
  MyTable table(driver_);
  table.CreateTable("examples.json", {"key", "json"}, {"key"});

  MyTable::ColumnsTuple input("test", "{\"a\":1}");
  table.Insert(input);

  MyTable::ColumnsTuple output("test", "");
  table.SelectOneRow(&output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  check_equal_tuples(input, output);

  get<1>(input) = "{\"b\":1}"; // 'json'
  table.Update(input);

  MyTable::ColumnsTuple updated_output("test", "");
  table.SelectOneRow(&updated_output);
  table.Print("UPDATED RESULT OUTPUT", updated_output);

  LOG(INFO) << "Checking selected values...";
  check_equal_tuples(input, updated_output);
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


  typedef TestTable<string, cass_json_t> MyTable;
  MyTable table(driver_);
  table.CreateTable("basic", {"key", "json"}, {"key"});

  MyTable::ColumnsTuple input("test", long_json);
  table.Insert(input);

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
    MyTable::ColumnsTuple output(key, "");
    table.SelectOneRow(&output);
    table.Print("RESULT OUTPUT", output);

    LOG(INFO) << "Checking selected JSON object for key=" << key;
    const string json = get<1>(output).str_; // 'json'

    ASSERT_EQ(json,
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

    verifyLongJson(json);
  }
}

TEST_F(CppCassandraDriverTest, TestPrepare) {
  typedef TestTable<cass_bool_t, cass_int32_t, string, cass_int32_t, string> MyTable;
  MyTable table(driver_);
  table.CreateTable("examples.basic", {"b", "val", "key", "int_key", "str"}, {"key", "int_key"});

  const CassPrepared* const prepared = table.PrepareInsert();
  // Prepared object can now be used to create new statement.
  CassStatement* const statement = cass_prepared_bind(prepared);

  const MyTable::ColumnsTuple input(cass_true, 0xAABBCCDD, "key1test", 0xDEADBEAF, "mystr");
  table.Print("Execute prepared INSERT with INPUT", input);
  table.BindInsert(statement, input);
  driver_->ExecuteStatement(statement); // It deletes statement.

  // Prepared object must be freed.
  cass_prepared_free(prepared);

  MyTable::ColumnsTuple output(cass_false, 0, "key1test", 0xDEADBEAF, "");
  table.SelectOneRow(&output);
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected values...";
  check_equal_tuples(input, output);
}

template <typename... ColumnsTypes>
void testTokenForTypes(
    const unique_ptr<CppCassandraDriver>& driver,
    const vector<string>& columns,
    const vector<string>& keys,
    const tuple<ColumnsTypes...>& input_data,
    const tuple<ColumnsTypes...>& input_keys,
    const tuple<ColumnsTypes...>& input_empty,
    int64_t exp_token = 0) {
  typedef TestTable<ColumnsTypes...> MyTable;
  typedef typename MyTable::ColumnsTuple ColumnsTuple;

  MyTable table(driver);
  table.CreateTable("examples.basic", columns, keys);

  const CassPrepared* const prepared = table.PrepareInsert();
  // Prepared object can now be used to create new statement.
  CassStatement* const statement = cass_prepared_bind(prepared);

  const ColumnsTuple input(input_data);
  table.Print("Execute prepared INSERT with INPUT", input);
  table.BindInsert(statement, input);

  int64_t token = 0;
  string full_table_name;
  bool token_available = cass::PartitionAwarePolicy::get_yb_hash_code(
      statement->from(), &token, &full_table_name);
  LOG(INFO) << "Got token: " << (token_available ? "OK" : "ERROR") << " token=" << token
            << " (0x" << std::hex << token << ")" << " table=" << full_table_name;
  ASSERT_TRUE(token_available);

  if (exp_token > 0) {
    ASSERT_EQ(exp_token, token);
  }

  driver->ExecuteStatement(statement); // It deletes statement.

  // Prepared object must be freed.
  cass_prepared_free(prepared);

  ColumnsTuple output_by_key(input_keys);
  table.SelectOneRow(&output_by_key);
  table.Print("RESULT OUTPUT", output_by_key);
  LOG(INFO) << "Checking selected values...";
  check_equal_tuples(input, output_by_key);

  ColumnsTuple output(input_empty);
  table.SelectByToken(&output, token);
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected by TOKEN values...";
  check_equal_tuples(input, output);
}

template <typename KeyType>
void testTokenForType(
    const unique_ptr<CppCassandraDriver>& driver, const KeyType& key, int64_t exp_token = 0) {
  typedef tuple<KeyType, cass_double_t> Tuple;
  testTokenForTypes(driver,
                    {"key", "value"}, // column names
                    {"(key)"}, // key names
                    Tuple(key, 0.56789), // data
                    Tuple(key, 0.), // only keys
                    Tuple((KeyType()), 0.), // empty
                    exp_token);
}

TEST_F(CppCassandraDriverTest, TestTokenForText) {
  testTokenForType<string>(driver_, "test", 0x8753000000000000);
}

TEST_F(CppCassandraDriverTest, TestTokenForInt) {
  testTokenForType<int32_t>(driver_, 0xDEADBEAF);
}

TEST_F(CppCassandraDriverTest, TestTokenForBigInt) {
  testTokenForType<int64_t>(driver_, 0xDEADBEAFDEADBEAFULL);
}

TEST_F(CppCassandraDriverTest, TestTokenForBoolean) {
  testTokenForType<cass_bool_t>(driver_, cass_true);
}

TEST_F(CppCassandraDriverTest, TestTokenForFloat) {
  testTokenForType<cass_float_t>(driver_, 0.123f);
}

TEST_F(CppCassandraDriverTest, TestTokenForDouble) {
  testTokenForType<cass_double_t>(driver_, 0.12345);
}

TEST_F(CppCassandraDriverTest, TestTokenForDoubleKey) {
  typedef tuple<string, cass_int32_t, cass_double_t> Tuple;
  testTokenForTypes(driver_,
                    {"key", "int_key", "value"}, // column names
                    {"(key", "int_key)"}, // key names
                    Tuple("test", 0xDEADBEAF, 0.123), // data
                    Tuple("test", 0xDEADBEAF, 0.), // only keys
                    Tuple("", 0, 0.)); // empty
}

//------------------------------------------------------------------------------

struct IOMetrics {
  IOMetrics() = default;
  IOMetrics(const IOMetrics&) = default;

  explicit IOMetrics(const ExternalMiniCluster& cluster) : IOMetrics() {
    load(cluster);
  }

  static void load_value(
      const ExternalMiniCluster& cluster, int ts_index,
      const MetricPrototype* metric_proto, int64_t* value) {
    const ExternalTabletServer& ts = *CHECK_NOTNULL(cluster.tablet_server(ts_index));
    const Status s = ts.GetInt64CQLMetric(
        &METRIC_ENTITY_server, "yb.cqlserver", CHECK_NOTNULL(metric_proto),
        "total_count", CHECK_NOTNULL(value));

    if (!s.ok()) {
      LOG(ERROR) << "Failed to get metric " << metric_proto->name() << " from TS"
          << ts_index << ": " << ts.bind_host() << ":" << ts.cql_http_port()
          << " with error " << s.CodeAsString();
    }
    ASSERT_OK(s);
  }

  void load(const ExternalMiniCluster& cluster) {
    *this = IOMetrics();
    for (int i = 0; i < cluster.num_tablet_servers(); ++i) {
      IOMetrics m;
      load_value(cluster, i, &METRIC_handler_latency_yb_client_write_remote, &m.remote_write);
      load_value(cluster, i, &METRIC_handler_latency_yb_client_read_remote, &m.remote_read);
      load_value(cluster, i, &METRIC_handler_latency_yb_client_write_local, &m.local_write);
      load_value(cluster, i, &METRIC_handler_latency_yb_client_read_local, &m.local_read);
      *this += m;
    }
  }

  IOMetrics& operator +=(const IOMetrics& m) {
    local_read += m.local_read;
    local_write += m.local_write;
    remote_read += m.remote_read;
    remote_write += m.remote_write;
    return *this;
  }

  IOMetrics& operator -=(const IOMetrics& m) {
    local_read -= m.local_read;
    local_write -= m.local_write;
    remote_read -= m.remote_read;
    remote_write -= m.remote_write;
    return *this;
  }

  IOMetrics operator +(const IOMetrics& m) const {
    return IOMetrics(*this) += m;
  }

  IOMetrics operator -(const IOMetrics& m) const {
    return IOMetrics(*this) -= m;
  }

  int64_t local_read = 0;
  int64_t local_write = 0;
  int64_t remote_read = 0;
  int64_t remote_write = 0;
};

std::ostream& operator <<(std::ostream& s, const IOMetrics& m) {
  return s << "LocalRead=" << m.local_read << " LocalWrite=" << m.local_write
      << " RemoteRead=" << m.remote_read << " RemoteWrite=" << m.remote_write;
}

//------------------------------------------------------------------------------

TEST_F(CppCassandraDriverTest, TestInsertLocality) {
  typedef TestTable<string, string> MyTable;
  typedef typename MyTable::ColumnsTuple ColumnsTuple;

  MyTable table(driver_);
  table.CreateTable("examples.basic", {"id", "data"}, {"(id)"});

  LOG(INFO) << "Wait 5 sec to refresh metadata in driver by time";
  SleepFor(MonoDelta::FromMicroseconds(5*1000000));

  IOMetrics pre_metrics(*cluster_);

  const CassPrepared* const prepared = table.PrepareInsert();
  const int total_keys = 100;
  ColumnsTuple input("", "test_value");

  for (int i = 0; i < total_keys; ++i) {
    get<0>(input) = Substitute("key_$0", i);

    // Prepared object can now be used to create new statement.
    CassStatement* const statement = cass_prepared_bind(prepared);
    table.BindInsert(statement, input);
    driver_->ExecuteStatement(statement); // It deletes statement.
  }

  // Prepared object must be freed.
  cass_prepared_free(prepared);

  IOMetrics post_metrics(*cluster_);
  const IOMetrics delta_metrics = post_metrics - pre_metrics;
  LOG(INFO) << "DELTA Metrics: " << delta_metrics;

  // Expect minimum 70% of all requests to be local.
  ASSERT_GT(delta_metrics.local_write*10, total_keys*7);
}

}  // namespace yb
