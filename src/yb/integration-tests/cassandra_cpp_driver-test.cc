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

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/util/metrics.h"
#include "yb/util/jsonreader.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"

using namespace std::literals;

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

DECLARE_int64(external_mini_cluster_max_log_bytes);

namespace yb {

//------------------------------------------------------------------------------

class CassandraBatch;
class CassandraSession;
class CassandraStatement;
struct cass_json_t;

// Cassandra CPP driver has his own functions to release objects, so we should use them for it.
template <class T, void (*Func)(T*)>
class FuncDeleter {
 public:
  void operator()(T* t) const {
    if (t) {
      Func(t);
    }
  }
};

class CassandraValue {
 public:
  explicit CassandraValue(const CassValue* value) : value_(value) {}

  template <class Out>
  void Get(Out* out) const;

  template <class Out>
  Out As() const {
    Out result;
    Get(&result);
    return result;
  }

  std::string ToString() const;

 private:
  const CassValue* value_;
};

typedef std::unique_ptr<
    CassIterator, FuncDeleter<CassIterator, &cass_iterator_free>> CassIteratorPtr;

class CassandraRowIterator {
 public:
  explicit CassandraRowIterator(CassIterator* iterator) : cass_iterator_(iterator) {}

  bool Next() {
    return cass_iterator_next(cass_iterator_.get()) != cass_false;
  }

  template <class Out>
  void Get(Out* out) const {
    Value().Get(out);
  }

  CassandraValue Value() const {
    return CassandraValue(cass_iterator_get_column(cass_iterator_.get()));
  }

 private:
  CassIteratorPtr cass_iterator_;
};

class CassandraRow {
 public:
  explicit CassandraRow(const CassRow* row) : cass_row_(row) {}

  template <class Out>
  void Get(size_t index, Out* out) const {
    return Value(index).Get(out);
  }

  CassandraValue Value(size_t index) const {
    return CassandraValue(cass_row_get_column(cass_row_, index));
  }

  CassandraRowIterator CreateIterator() const {
    return CassandraRowIterator(cass_iterator_from_row(cass_row_));
  }

 private:
  const CassRow* cass_row_; // owned by iterator
};

class CassandraIterator {
 public:
  explicit CassandraIterator(CassIterator* iterator) : cass_iterator_(iterator) {}

  bool Next() {
    return cass_iterator_next(cass_iterator_.get()) != cass_false;
  }

  CassandraRow Row() {
    return CassandraRow(cass_iterator_get_row(cass_iterator_.get()));
  }

 private:
  CassIteratorPtr cass_iterator_;
};

typedef std::unique_ptr<
    const CassResult, FuncDeleter<const CassResult, &cass_result_free>> CassResultPtr;

class CassandraResult {
 public:
  explicit CassandraResult(const CassResult* result) : cass_result_(result) {}

  CassandraIterator CreateIterator() const {
    return CassandraIterator(cass_iterator_from_result(cass_result_.get()));
  }

 private:
  CassResultPtr cass_result_;
};

typedef std::unique_ptr<
    const CassPrepared, FuncDeleter<const CassPrepared, &cass_prepared_free>> CassPreparedPtr;

class CassandraPrepared {
 public:
  explicit CassandraPrepared(const CassPrepared* prepared) : prepared_(prepared) {}

  CassandraStatement Bind();

 private:
  CassPreparedPtr prepared_;
};

typedef std::unique_ptr<
    CassFuture, FuncDeleter<CassFuture, &cass_future_free>> CassFuturePtr;

class CassandraFuture {
 public:
  explicit CassandraFuture(CassFuture* future) : future_(future) {}

  CHECKED_STATUS Wait() {
    cass_future_wait(future_.get());
    return CheckErrorCode();
  }

  CHECKED_STATUS WaitFor(MonoDelta duration) {
    if (!cass_future_wait_timed(future_.get(), duration.ToMicroseconds())) {
      return STATUS(TimedOut, "Future timed out");
    }

    return CheckErrorCode();
  }

  CassandraResult Result() {
    return CassandraResult(cass_future_get_result(future_.get()));
  }

  CassandraPrepared Prepared() {
    return CassandraPrepared(cass_future_get_prepared(future_.get()));
  }

 private:
  CHECKED_STATUS CheckErrorCode() {
    const CassError rc = cass_future_error_code(future_.get());
    VLOG(2) << "Last operation RC: " << rc;

    if (rc != CASS_OK) {
      const char* message = nullptr;
      size_t message_sz = 0;
      cass_future_error_message(future_.get(), &message, &message_sz);
      if (rc == CASS_ERROR_LIB_REQUEST_TIMED_OUT) {
        return STATUS(TimedOut, Slice(message, message_sz));
      }
      return STATUS(RuntimeError, Slice(message, message_sz));
    }

    return Status::OK();
  }

  CassFuturePtr future_;
};

typedef std::unique_ptr<
    CassStatement, FuncDeleter<CassStatement, &cass_statement_free>> CassStatementPtr;

class CassandraStatement {
 public:
  explicit CassandraStatement(CassStatement* statement)
      : cass_statement_(statement) {}

  explicit CassandraStatement(const std::string& query, size_t parameter_count = 0)
      : cass_statement_(cass_statement_new(query.c_str(), parameter_count)) {}

  void Bind(size_t index, const string& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_string(cass_statement_.get(), index, v.c_str()));
  }

  void Bind(size_t index, const cass_bool_t& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_bool(cass_statement_.get(), index, v));
  }

  void Bind(size_t index, const cass_float_t& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_float(cass_statement_.get(), index, v));
  }

  void Bind(size_t index, const cass_double_t& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_double(cass_statement_.get(), index, v));
  }

  void Bind(size_t index, const cass_int32_t& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_int32(cass_statement_.get(), index, v));
  }

  void Bind(size_t index, const cass_int64_t& v) {
    CHECK_EQ(CASS_OK, cass_statement_bind_int64(cass_statement_.get(), index, v));
  }

  void Bind(size_t index, const cass_json_t& v);

  CassStatement* get() const {
    return cass_statement_.get();
  }

 private:
  friend class CassandraBatch;
  friend class CassandraSession;

  CassStatementPtr cass_statement_;
};

typedef std::unique_ptr<CassBatch, FuncDeleter<CassBatch, &cass_batch_free>> CassBatchPtr;

class CassandraBatch {
 public:
  explicit CassandraBatch(CassBatchType type) : cass_batch_(cass_batch_new(type)) {}

  void Add(CassandraStatement* statement) {
    cass_batch_add_statement(cass_batch_.get(), statement->cass_statement_.get());
  }

 private:
  friend class CassandraSession;

  CassBatchPtr cass_batch_;
};

struct DeleteSession {
  void operator()(CassSession* session) const {
    if (session != nullptr) {
      WARN_NOT_OK(CassandraFuture(cass_session_close(session)).Wait(), "Close session");
      cass_session_free(session);
    }
  }
};

typedef std::unique_ptr<CassSession, DeleteSession> CassSessionPtr;

class CassandraSession {
 public:
  CassandraSession() = default;

  CHECKED_STATUS Connect(CassCluster* cluster) {
    cass_session_.reset(CHECK_NOTNULL(cass_session_new()));
    return CassandraFuture(cass_session_connect(cass_session_.get(), cluster)).Wait();
  }

  static Result<CassandraSession> Create(CassCluster* cluster) {
    LOG(INFO) << "Create new session ...";
    CassandraSession result;
    RETURN_NOT_OK(result.Connect(cluster));
    LOG(INFO) << "Create new session - DONE";
    return result;
  }

  CHECKED_STATUS Execute(const CassandraStatement& statement) {
    CassandraFuture future(cass_session_execute(
        cass_session_.get(), statement.cass_statement_.get()));
    return future.Wait();
  }

  Result<CassandraResult> ExecuteWithResult(const CassandraStatement& statement) {
    CassandraFuture future(cass_session_execute(
        cass_session_.get(), statement.cass_statement_.get()));
    RETURN_NOT_OK(future.Wait());
    return future.Result();
  }

  CHECKED_STATUS ExecuteQuery(const string& query) {
    LOG(INFO) << "Execute query: " << query;
    return Execute(CassandraStatement(query));
  }

  CHECKED_STATUS ExecuteBatch(const CassandraBatch& batch) {
    return SubmitBatch(batch).Wait();
  }

  CassandraFuture SubmitBatch(const CassandraBatch& batch) {
    return CassandraFuture(
        cass_session_execute_batch(cass_session_.get(), batch.cass_batch_.get()));
  }

  Result<CassandraPrepared> Prepare(const string& prepare_query) {
    VLOG(2) << "Execute prepare request: " << prepare_query;
    CassandraFuture future(cass_session_prepare(cass_session_.get(), prepare_query.c_str()));
    RETURN_NOT_OK(future.Wait());
    return future.Prepared();
  }

 private:
  CassSessionPtr cass_session_;
};

CassandraStatement CassandraPrepared::Bind() {
  return CassandraStatement(cass_prepared_bind(prepared_.get()));
}

const MonoDelta kTimeOut = RegularBuildVsSanitizers(12s, 60s);

class CppCassandraDriver {
 public:
  explicit CppCassandraDriver(
      const ExternalMiniCluster& mini_cluster, bool use_partition_aware_routing) {

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
    if (VLOG_IS_ON(4)) {
      cass_log_set_level(CASS_LOG_TRACE);
    }

    LOG(INFO) << "Create Cassandra cluster to " << hosts << " :" << port << " ...";
    cass_cluster_ = CHECK_NOTNULL(cass_cluster_new());
    CHECK_EQ(CASS_OK, cass_cluster_set_contact_points(cass_cluster_, hosts.c_str()));
    CHECK_EQ(CASS_OK, cass_cluster_set_port(cass_cluster_, port));
    cass_cluster_set_request_timeout(cass_cluster_, kTimeOut.ToMilliseconds());

    // Setup cluster configuration: partitions metadata refresh timer = 3 seconds.
    cass_cluster_set_partition_aware_routing(
        cass_cluster_, use_partition_aware_routing ? cass_true : cass_false, 3);
  }

  ~CppCassandraDriver() {
    LOG(INFO) << "Terminating driver...";

    if (cass_cluster_) {
      cass_cluster_free(cass_cluster_);
      cass_cluster_ = nullptr;
    }

    LOG(INFO) << "Terminating driver - DONE";
  }

  Result<CassandraSession> CreateSession() {
    return CassandraSession::Create(cass_cluster_);
  }

 private:
  CassCluster* cass_cluster_ = nullptr;
};

//------------------------------------------------------------------------------

Result<CassandraSession> EstablishSession(CppCassandraDriver* driver) {
  auto session = VERIFY_RESULT(driver->CreateSession());
  RETURN_NOT_OK(session.ExecuteQuery("USE test;"));
  return session;
}

class CppCassandraDriverTest : public ExternalMiniClusterITestBase {
 public:
  void SetUp() override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUp());

    LOG(INFO) << "Starting YB ExternalMiniCluster...";
    // Start up with 3 (default) tablet servers.
    ASSERT_NO_FATALS(StartCluster(ExtraTServerFlags(), {}, 3, NumMasters()));

    driver_.reset(CHECK_NOTNULL(new CppCassandraDriver(*cluster_, UsePartitionAwareRouting())));

    // Create and use default keyspace.
    session_ = ASSERT_RESULT(driver_->CreateSession());
    ASSERT_OK(session_.ExecuteQuery("CREATE KEYSPACE IF NOT EXISTS test;"));
    ASSERT_OK(session_.ExecuteQuery("USE test;"));
  }

  void SetUpCluster(ExternalMiniClusterOptions* opts) override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUpCluster(opts));

    opts->bind_to_unique_loopback_addresses = true;
    opts->use_same_ts_ports = true;
  }

  void TearDown() override {
    ExternalMiniClusterITestBase::cluster_->AssertNoCrashes();

    driver_.reset();
    LOG(INFO) << "Stopping YB ExternalMiniCluster...";
    ExternalMiniClusterITestBase::TearDown();
  }

  virtual std::vector<std::string> ExtraTServerFlags() {
    return {};
  }

  virtual int NumMasters() {
    return 1;
  }

  virtual bool UsePartitionAwareRouting() {
    return true;
  }

 protected:
  unique_ptr<CppCassandraDriver> driver_;
  CassandraSession session_;
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

void CassandraStatement::Bind(size_t index, const cass_json_t& v) {
  CHECK_EQ(CASS_OK, cass_statement_bind_string(cass_statement_.get(), index, v.str_.c_str()));
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

// Supported types - read value:
void read(const CassValue* val, string* v) {
  const char* s = nullptr;
  size_t sz = 0;
  CHECK_EQ(CASS_OK, cass_value_get_string(val, &s, &sz));
  *v = string(s, sz);
}

void read(const CassValue* val, Slice* v) {
  const cass_byte_t* data = nullptr;
  size_t size = 0;
  CHECK_EQ(CASS_OK, cass_value_get_bytes(val, &data, &size));
  *v = Slice(data, size);
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

void read(const CassValue* val, CassUuid* v) {
  CHECK_EQ(CASS_OK, cass_value_get_uuid(val, v));
}

void read(const CassValue* val, CassInet* v) {
  CHECK_EQ(CASS_OK, cass_value_get_inet(val, v));
}

} // namespace util

template <class Out>
void CassandraValue::Get(Out* out) const {
  util::read(value_, out);
}

std::string CassandraValue::ToString() const {
  auto value_type = cass_value_type(value_);
  switch (value_type) {
    case CASS_VALUE_TYPE_BLOB:
      return As<Slice>().ToDebugHexString();
    case CASS_VALUE_TYPE_VARCHAR:
      return As<std::string>();
    case CASS_VALUE_TYPE_UUID: {
      char buffer[CASS_UUID_STRING_LENGTH];
      cass_uuid_string(As<CassUuid>(), buffer);
      return buffer;
    }
    case CASS_VALUE_TYPE_INET: {
      char buffer[CASS_INET_STRING_LENGTH];
      cass_inet_string(As<CassInet>(), buffer);
      return buffer;
    }
    case CASS_VALUE_TYPE_MAP: {
      std::string result = "{";
      CassIteratorPtr iterator(cass_iterator_from_map(value_));
      bool first = true;
      while (cass_iterator_next(iterator.get())) {
        if (first) {
          first = false;
        } else {
          result += ", ";
        }
        result += CassandraValue(cass_iterator_get_map_key(iterator.get())).ToString();
        result += " => ";
        result += CassandraValue(cass_iterator_get_map_value(iterator.get())).ToString();
      }
      result += "}";
      return result;
    }
    default:
      return "Not supported: " + std::to_string(to_underlying(value_type));
  }
}

//------------------------------------------------------------------------------

template <typename... ColumnsTypes>
class TestTable {
 public:
  typedef vector<string> StringVec;
  typedef tuple<ColumnsTypes...> ColumnsTuple;

  CHECKED_STATUS CreateTable(
      CassandraSession* session, const string& table, const StringVec& columns,
      const StringVec& keys, bool transactional = false) {
    table_name_ = table;
    column_names_ = columns;
    key_names_ = keys;

    for (string& k : key_names_) {
      TrimString(&k, "()"); // Cut parentheses if available.
    }

    const string query = create_table_str(table, columns, keys, transactional);
    return session->ExecuteQuery(query);
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

  void BindInsert(CassandraStatement* statement, const ColumnsTuple& data) const {
    DoBindValues(statement, /* keys_only = */ false, /* values_first = */ false, data);
  }

  void Insert(CassandraSession* session, const ColumnsTuple& data) const {
    const string query = insert_with_bindings_str(table_name_, column_names_);
    Print("Execute: '" + query + "' with data", data);

    CassandraStatement statement(cass_statement_new(query.c_str(), column_names_.size()));
    BindInsert(&statement, data);
    ASSERT_OK(session->Execute(statement));
  }

  Result<CassandraPrepared> PrepareInsert(CassandraSession* session) const {
    return session->Prepare(insert_with_bindings_str(table_name_, column_names_));
  }

  void Update(CassandraSession* session, const ColumnsTuple& data) const {
    const string query = update_with_bindings_str(table_name_, column_names_, key_names_);
    Print("Execute: '" + query + "' with data", data);

    CassandraStatement statement(cass_statement_new(query.c_str(), column_names_.size()));
    DoBindValues(&statement, /* keys_only = */ false, /* values_first = */ true, data);

    ASSERT_OK(session->Execute(statement));
  }

  void SelectOneRow(CassandraSession* session, ColumnsTuple* data) {
    const string query = select_with_bindings_str(table_name_, key_names_);
    Print("Execute: '" + query + "' with data", *data);

    CassandraStatement statement(cass_statement_new(query.c_str(), key_names_.size()));
    DoBindValues(&statement, /* keys_only = */ true, /* values_first = */ false, *data);
    ExecuteAndReadOneRow(session, statement, data);
  }

  void SelectByToken(CassandraSession* session, ColumnsTuple* data, int64_t token) {
    const string query = select_by_token_str(table_name_, key_names_);
    Print("Execute: '" + query + "' with data", *data);

    CassandraStatement statement(query, 1);
    statement.Bind(0, token);
    ExecuteAndReadOneRow(session, statement, data);
  }

  void ExecuteAndReadOneRow(
      CassandraSession* session, const CassandraStatement& statement, ColumnsTuple* data) {
    auto result = ASSERT_RESULT(session->ExecuteWithResult(statement));
    auto iterator = result.CreateIterator();
    ASSERT_TRUE(iterator.Next());
    auto row = iterator.Row();
    DoReadValues(row, data);

    ASSERT_FALSE(iterator.Next());
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
  typename std::enable_if<I >= std::tuple_size<ColumnsTuple>::value, void>::type
  BindValues(CassandraStatement*, size_t*, bool, bool, const ColumnsTuple&) const {}

  template<size_t I>
  typename std::enable_if<I < std::tuple_size<ColumnsTuple>::value, void>::type
  BindValues(
      CassandraStatement* statement, size_t* index, bool use_values, bool use_keys,
      const ColumnsTuple& data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if (this_is_key ? use_keys : use_values) {
      statement->Bind((*index)++, get<I>(data));
    }

    BindValues<I + 1>(statement, index, use_values, use_keys, data);
  }

  void DoBindValues(
      CassandraStatement* statement, bool keys_only, bool values_first,
      const ColumnsTuple& data) const {
    size_t i = 0;
    if (keys_only) {
      BindValues<0>(statement, &i, false /* use_values */, true /* use_keys */, data);
    } else if (values_first) {
      // Bind values.
      BindValues<0>(statement, &i, true /* use_values */, false /* use_keys */, data);
      // Bind keys.
      BindValues<0>(statement, &i, false /* use_values */, true /* use_keys */, data);
    } else {
      BindValues<0>(statement, &i, true /* use_values */, true /* use_keys */, data);
    }
  }

  template<size_t I>
  typename std::enable_if<I >= std::tuple_size<ColumnsTuple>::value, void>::type
  ReadValues(const CassandraRow& row, size_t*, bool, ColumnsTuple*) const {}

  template<size_t I>
  typename std::enable_if<I < std::tuple_size<ColumnsTuple>::value, void>::type
  ReadValues(
      const CassandraRow& row, size_t* index, bool use_keys,
      ColumnsTuple* data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if (this_is_key == use_keys) {
      row.Get(*index, &get<I>(*data));
      ++*index;
    }

    ReadValues<I + 1>(row, index, use_keys, data);
  }

  void DoReadValues(const CassandraRow& row, ColumnsTuple* data) const {
    size_t i = 0;
    // Read keys.
    ReadValues<0>(row, &i, true /* use_keys */, data);
    // Read values.
    ReadValues<0>(row, &i, false /* use_keys */, data);
  }

  // Strings for CQL requests.

  static string create_table_str(
      const string& table, const StringVec& columns, const StringVec& keys, bool transactional) {
    CHECK_GT(columns.size(), 0);
    CHECK_GT(keys.size(), 0);
    CHECK_GE(columns.size(), keys.size());

    StringVec types;
    do_get_type_names(&types, ColumnsTuple());
    CHECK_EQ(types.size(), columns.size());

    for (int i = 0; i < columns.size(); ++i) {
      types[i] = columns[i] + ' ' + types[i];
    }


    return Format("CREATE TABLE IF NOT EXISTS $0 ($1, PRIMARY KEY ($2))$3;",
                  table, JoinStrings(types, ", "), JoinStrings(keys, ", "),
                  transactional ? " WITH transactions = { 'enabled' : true }" : "");
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
  string table_name_;
  StringVec column_names_;
  StringVec key_names_;
};

//------------------------------------------------------------------------------

template<size_t I, class Tuple>
typename std::enable_if<I >= std::tuple_size<Tuple>::value, void>::type
ExpectEqualTuplesHelper(const Tuple&, const Tuple&) {}

template<size_t I, class Tuple>
typename std::enable_if<I < std::tuple_size<Tuple>::value, void>::type
ExpectEqualTuplesHelper(const Tuple& t1, const Tuple& t2) {
  EXPECT_EQ(get<I>(t1), get<I>(t2));
  LOG(INFO) << "COMPARE: " << get<I>(t1) << " == " << get<I>(t2);
  ExpectEqualTuplesHelper<I + 1>(t1, t2);
}

template <class Tuple>
void ExpectEqualTuples(const Tuple& t1, const Tuple& t2) {
  ExpectEqualTuplesHelper<0>(t1, t2);
}

void LogResult(const CassandraResult& result) {
  auto iterator = result.CreateIterator();
  int i = 0;
  while (iterator.Next()) {
    ++i;
    std::string line;
    auto row = iterator.Row();
    auto row_iterator = row.CreateIterator();
    bool first = true;
    while (row_iterator.Next()) {
      if (first) {
        first = false;
      } else {
        line += ", ";
      }
      line += row_iterator.Value().ToString();
    }
    LOG(INFO) << i << ") " << line;
  }
}

//------------------------------------------------------------------------------

TEST_F(CppCassandraDriverTest, TestBasicTypes) {
  typedef TestTable<
      string, cass_bool_t, cass_float_t, cass_double_t, cass_int32_t, cass_int64_t, string> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, "test.basic", {"key", "bln", "flt", "dbl", "i32", "i64", "str"}, {"key"}));

  const MyTable::ColumnsTuple input("test", cass_true, 11.01f, 22.002, 3, 4, "text");
  table.Insert(&session_, input);

  MyTable::ColumnsTuple output("test", cass_false, 0.f, 0., 0, 0, "");
  table.SelectOneRow(&session_, &output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);
}

TEST_F(CppCassandraDriverTest, TestJsonBType) {
  typedef TestTable<string, cass_json_t> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.json", {"key", "json"}, {"key"}));

  MyTable::ColumnsTuple input("test", "{\"a\":1}");
  table.Insert(&session_, input);

  MyTable::ColumnsTuple output("test", "");
  table.SelectOneRow(&session_, &output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);

  get<1>(input) = "{\"b\":1}"; // 'json'
  table.Update(&session_, input);

  MyTable::ColumnsTuple updated_output("test", "");
  table.SelectOneRow(&session_, &updated_output);
  table.Print("UPDATED RESULT OUTPUT", updated_output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, updated_output);
}

void VerifyLongJson(const string& json) {
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
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "basic", {"key", "json"}, {"key"}));

  MyTable::ColumnsTuple input("test", long_json);
  table.Insert(&session_, input);

  ASSERT_OK(session_.ExecuteQuery(
      "INSERT INTO basic(key, json) values ('test0', '" + long_json + "');"));
  ASSERT_OK(session_.ExecuteQuery(
      "INSERT INTO basic(key, json) values ('test1', '{ \"a\" : 1 }');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test2', '\"abc\"');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test3', '3');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test4', 'true');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test5', 'false');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test6', 'null');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test7', '2.0');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test8', '{\"b\" : 1}');"));

  for (const string& key : {"test", "test0"} ) {
    MyTable::ColumnsTuple output(key, "");
    table.SelectOneRow(&session_, &output);
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

    VerifyLongJson(json);
  }
}

TEST_F(CppCassandraDriverTest, TestPrepare) {
  typedef TestTable<cass_bool_t, cass_int32_t, string, cass_int32_t, string> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, "test.basic", {"b", "val", "key", "int_key", "str"}, {"key", "int_key"}));

  const MyTable::ColumnsTuple input(cass_true, 0xAABBCCDD, "key1test", 0xDEADBEAF, "mystr");
  {
    auto prepared = ASSERT_RESULT(table.PrepareInsert(&session_));
    auto statement = prepared.Bind();
    // Prepared object can now be used to create new statement.

    table.Print("Execute prepared INSERT with INPUT", input);
    table.BindInsert(&statement, input);
    ASSERT_OK(session_.Execute(statement));
  }

  MyTable::ColumnsTuple output(cass_false, 0, "key1test", 0xDEADBEAF, "");
  table.SelectOneRow(&session_, &output);
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);
}

template <typename... ColumnsTypes>
void TestTokenForTypes(
    CassandraSession* session,
    const vector<string>& columns,
    const vector<string>& keys,
    const tuple<ColumnsTypes...>& input_data,
    const tuple<ColumnsTypes...>& input_keys,
    const tuple<ColumnsTypes...>& input_empty,
    int64_t exp_token = 0) {
  typedef TestTable<ColumnsTypes...> MyTable;
  typedef typename MyTable::ColumnsTuple ColumnsTuple;

  MyTable table;
  ASSERT_OK(table.CreateTable(session, "test.basic", columns, keys));

  auto prepared = ASSERT_RESULT(table.PrepareInsert(session));
  auto statement = prepared.Bind();

  const ColumnsTuple input(input_data);
  table.Print("Execute prepared INSERT with INPUT", input);
  table.BindInsert(&statement, input);

  int64_t token = 0;
  bool token_available = cass_partition_aware_policy_get_yb_hash_code(
      statement.get(), &token);
  LOG(INFO) << "Got token: " << (token_available ? "OK" : "ERROR") << " token=" << token
            << " (0x" << std::hex << token << ")";
  ASSERT_TRUE(token_available);

  if (exp_token > 0) {
    ASSERT_EQ(exp_token, token);
  }

  ASSERT_OK(session->Execute(statement));

  ColumnsTuple output_by_key(input_keys);
  table.SelectOneRow(session, &output_by_key);
  table.Print("RESULT OUTPUT", output_by_key);
  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output_by_key);

  ColumnsTuple output(input_empty);
  table.SelectByToken(session, &output, token);
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected by TOKEN values...";
  ExpectEqualTuples(input, output);
}

template <typename KeyType>
void TestTokenForType(
    CassandraSession* session, const KeyType& key, int64_t exp_token = 0) {
  typedef tuple<KeyType, cass_double_t> Tuple;
  TestTokenForTypes(session,
                    {"key", "value"}, // column names
                    {"(key)"}, // key names
                    Tuple(key, 0.56789), // data
                    Tuple(key, 0.), // only keys
                    Tuple((KeyType()), 0.), // empty
                    exp_token);
}

TEST_F(CppCassandraDriverTest, TestTokenForText) {
  TestTokenForType<string>(&session_, "test", 0x8753000000000000);
}

TEST_F(CppCassandraDriverTest, TestTokenForInt) {
  TestTokenForType<int32_t>(&session_, 0xDEADBEAF);
}

TEST_F(CppCassandraDriverTest, TestTokenForBigInt) {
  TestTokenForType<int64_t>(&session_, 0xDEADBEAFDEADBEAFULL);
}

TEST_F(CppCassandraDriverTest, TestTokenForBoolean) {
  TestTokenForType<cass_bool_t>(&session_, cass_true);
}

TEST_F(CppCassandraDriverTest, TestTokenForFloat) {
  TestTokenForType<cass_float_t>(&session_, 0.123f);
}

TEST_F(CppCassandraDriverTest, TestTokenForDouble) {
  TestTokenForType<cass_double_t>(&session_, 0.12345);
}

TEST_F(CppCassandraDriverTest, TestTokenForDoubleKey) {
  typedef tuple<string, cass_int32_t, cass_double_t> Tuple;
  TestTokenForTypes(&session_,
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
    const auto result = ts.GetInt64CQLMetric(
        &METRIC_ENTITY_server, "yb.cqlserver", CHECK_NOTNULL(metric_proto),
        "total_count");

    if (!result.ok()) {
      LOG(ERROR) << "Failed to get metric " << metric_proto->name() << " from TS"
          << ts_index << ": " << ts.bind_host() << ":" << ts.cql_http_port()
          << " with error " << result.status();
    }
    ASSERT_OK(result);
    *CHECK_NOTNULL(value) = *result;
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

  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.basic", {"id", "data"}, {"(id)"}));

  LOG(INFO) << "Wait 5 sec to refresh metadata in driver by time";
  SleepFor(MonoDelta::FromMicroseconds(5*1000000));

  IOMetrics pre_metrics(*cluster_);

  auto prepared = ASSERT_RESULT(table.PrepareInsert(&session_));
  const int total_keys = 100;
  ColumnsTuple input("", "test_value");

  for (int i = 0; i < total_keys; ++i) {
    get<0>(input) = Substitute("key_$0", i);

    // Prepared object can now be used to create new statement.
    auto statement = prepared.Bind();
    table.BindInsert(&statement, input);
    ASSERT_OK(session_.Execute(statement));
  }

  IOMetrics post_metrics(*cluster_);
  const IOMetrics delta_metrics = post_metrics - pre_metrics;
  LOG(INFO) << "DELTA Metrics: " << delta_metrics;

  // Expect minimum 70% of all requests to be local.
  ASSERT_GT(delta_metrics.local_write*10, total_keys*7);
}

class CppCassandraDriverLowSoftLimitTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--memory_limit_soft_percentage=0"s};
  }

  bool UsePartitionAwareRouting() override {
    // TODO: Disable partition aware routing in this test because of TSAN issue (#1837).
    // Should be reenabled when issue is fixed.
    return false;
  }
};

TEST_F_EX(CppCassandraDriverTest, BatchWriteDuringSoftMemoryLimit,
          CppCassandraDriverLowSoftLimitTest) {
  constexpr int kBatchSize = 500;
  constexpr int kWriters = 4;
  constexpr int kNumMetrics = 5;

  typedef TestTable<std::string, int64_t, std::string> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, "test.batch_ts_metrics_raw", {"metric_id", "ts", "value"},
      {"(metric_id, ts)"}));

  TestThreadHolder thread_holder;
  std::array<std::atomic<int>, kNumMetrics> metric_ts;
  std::atomic<int> total_writes(0);
  for (int i = 0; i != kNumMetrics; ++i) {
    metric_ts[i].store(0);
  }

  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &table, &metric_ts, &total_writes] {
      SetFlagOnExit set_flag_on_exit(&stop);
      auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
      std::vector<CassandraFuture> futures;
      while (!stop.load()) {
        CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
        auto prepared = table.PrepareInsert(&session);
        if (!prepared.ok()) {
          // Prepare could be failed because cluster has heavy load.
          // It is ok to just retry in this case, because we expect total number of writes.
          continue;
        }
        int metric_idx = RandomUniformInt(1, kNumMetrics);
        auto metric = "metric_" + std::to_string(metric_idx);
        int ts = metric_ts[metric_idx - 1].fetch_add(kBatchSize);
        for (int i = 0; i != kBatchSize; ++i) {
          ColumnsType tuple(metric, ts, "value_" + std::to_string(ts));
          auto statement = prepared->Bind();
          table.BindInsert(&statement, tuple);
          batch.Add(&statement);
          ++ts;
        }
        futures.push_back(session.SubmitBatch(batch));
        ++total_writes;
      }
    });
  }

  thread_holder.WaitAndStop(30s);
  auto total_writes_value = total_writes.load();
  LOG(INFO) << "Total writes: " << total_writes_value;
  ASSERT_GE(total_writes_value, RegularBuildVsSanitizers(1500, 100));
}

class CppCassandraDriverBackpressureTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--tablet_server_svc_queue_length=10"s, "--max_time_in_queue_ms=-1"s};
  }

  bool UsePartitionAwareRouting() override {
    // TODO: Disable partition aware routing in this test because of TSAN issue (#1837).
    // Should be reenabled when issue is fixed.
    return false;
  }
};

TEST_F_EX(CppCassandraDriverTest, LocalCallBackpressure, CppCassandraDriverBackpressureTest) {
  constexpr int kBatchSize = 30;
  constexpr int kNumBatches = 300;

  typedef TestTable<int64_t, int64_t> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.key_value", {"key", "value"}, {"(key)"}));

  std::vector<CassandraFuture> futures;

  for (int batch_idx = 0; batch_idx != kNumBatches; ++batch_idx) {
    CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
    auto prepared = table.PrepareInsert(&session_);
    if (!prepared.ok()) {
      // Prepare could be failed because cluster has heavy load.
      // It is ok to just retry in this case, because we check that process did not crash.
      continue;
    }
    for (int i = 0; i != kBatchSize; ++i) {
      ColumnsType tuple(batch_idx * kBatchSize + i, -1);
      auto statement = prepared->Bind();
      table.BindInsert(&statement, tuple);
      batch.Add(&statement);
    }
    futures.push_back(session_.SubmitBatch(batch));
  }

  for (auto& future : futures) {
    WARN_NOT_OK(future.Wait(), "Write failed");
  }
}

class CppCassandraDriverTransactionalWriteTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--TEST_transaction_inject_flushed_delay_ms=10"s};
  }

  bool UsePartitionAwareRouting() override {
    // TODO: Disable partition aware routing in this test because of TSAN issue (#1837).
    // Should be reenabled when issue is fixed.
    return false;
  }
};

TEST_F_EX(CppCassandraDriverTest, TransactionalWrite, CppCassandraDriverTransactionalWriteTest) {
  const std::string kTableName = "test.key_value";
  typedef TestTable<int32_t, int32_t> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, kTableName, {"key", "value"}, {"(key)"}, true /* transactional */));

  constexpr int kIterations = 20;
  auto prepared = ASSERT_RESULT(session_.Prepare(Format(
      "BEGIN TRANSACTION"
      "  INSERT INTO $0 (key, value) VALUES (?, ?);"
      "  INSERT INTO $0 (key, value) VALUES (?, ?);"
      "END TRANSACTION;", kTableName)));
  for (int i = 1; i <= kIterations; ++i) {
    auto statement = prepared.Bind();
    statement.Bind(0, i);
    statement.Bind(1, i * 3);
    statement.Bind(2, -i);
    statement.Bind(3, i * -4);
    ASSERT_OK(session_.Execute(statement));
  }
}

class CppCassandraDriverTestThreeMasters : public CppCassandraDriverTest {
 private:
  int NumMasters() override {
    return 3;
  }

  bool UsePartitionAwareRouting() override {
    // TODO: Disable partition aware routing in this test because of TSAN issue (#1837).
    // Should be reenabled when issue is fixed.
    return false;
  }
};

TEST_F_EX(CppCassandraDriverTest, ManyTables, CppCassandraDriverTestThreeMasters) {
  FLAGS_external_mini_cluster_max_log_bytes = 512_MB;

  constexpr int kThreads = RegularBuildVsSanitizers(5, 2);
  constexpr int kTables = RegularBuildVsSanitizers(15, 5);
  constexpr int kReads = 20;

  const std::string kTableNameFormat = "test.key_value_$0_$1";
  typedef TestTable<int32_t, int32_t> MyTable;

  TestThreadHolder thread_holder;
  std::atomic<int> tables(0);

  for (int i = 0; i != kThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), thread = i, &kTableNameFormat, &tables] {
          SetFlagOnExit set_flag_on_exit(&stop);
          auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
          int idx = 0;
          while (!stop.load(std::memory_order_acquire)) {
            MyTable table;
            auto status = table.CreateTable(
                &session, Format(kTableNameFormat, thread, idx), {"key", "value"}, {"(key)"});
            if (status.ok()) {
              LOG(INFO) << "Created table " << thread << ", " << idx;
              // We need at least kTables tables.
              if (tables.fetch_add(1, std::memory_order_acq_rel) >= kTables) {
                break;
              }
            } else {
              LOG(INFO) << "Failed to create table " << thread << ", " << idx << ": " << status;
            }
            ++idx;
          }
        });
  }

  thread_holder.WaitAndStop(180s);

  ASSERT_GE(tables.load(std::memory_order_acquire), kTables);

  CassandraStatement statement("SELECT * FROM system.partitions");
  std::vector<MonoDelta> read_times;
  read_times.reserve(kReads);
  int i = 0;
  for (;;) {
    auto start = MonoTime::Now();
    auto result = session_.ExecuteWithResult(statement);
    auto finish = MonoTime::Now();
    if (!result.ok()) {
      LOG(INFO) << "Read failed: " << result.status();
      continue;
    }
    read_times.push_back(finish - start);
    ++i;
    if (i == kReads) {
      LogResult(*result);
      break;
    }
  }

  LOG(INFO) << "Read times: " << AsString(read_times);
  std::sort(read_times.begin(), read_times.end());

  if (!IsSanitizer()) {
    ASSERT_LE(read_times.front() * 2, read_times.back()); // Check that cache works
  }
}

class CppCassandraDriverRejectionTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--TEST_write_rejection_percentage=15"s,
            "--linear_backoff_ms=10"};
  }

  bool UsePartitionAwareRouting() override {
    // Disable partition aware routing in this test because of TSAN issue (#1837).
    // Should be reenabled when issue is fixed.
    return false;
  }
};

TEST_F_EX(CppCassandraDriverTest, Rejection, CppCassandraDriverRejectionTest) {
  constexpr int kBatchSize = 50;
  constexpr int kWriters = 21;

  typedef TestTable<int64_t, int64_t> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.key_value", {"key", "value"}, {"(key)"}));

  TestThreadHolder thread_holder;
  std::atomic<int64_t> key(0);
  std::atomic<int> pending_writes(0);
  std::atomic<int> max_pending_writes(0);

  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &table, &key, &pending_writes,
         &max_pending_writes] {
      SetFlagOnExit set_flag_on_exit(&stop);
      auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
      while (!stop.load()) {
        CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
        auto prepared = table.PrepareInsert(&session);
        if (!prepared.ok()) {
          // Prepare could be failed because cluster has heavy load.
          // It is ok to just retry in this case, because we expect total number of writes.
          continue;
        }
        for (int i = 0; i != kBatchSize; ++i) {
          auto current_key = key++;
          ColumnsType tuple(current_key, -current_key);
          auto statement = prepared->Bind();
          table.BindInsert(&statement, tuple);
          batch.Add(&statement);
        }
        auto future = session.SubmitBatch(batch);
        auto status = future.WaitFor(kTimeOut / 2);
        if (status.IsTimedOut()) {
          auto pw = ++pending_writes;
          auto mpw = max_pending_writes.load();
          while (pw > mpw) {
            if (max_pending_writes.compare_exchange_weak(mpw, pw)) {
              // Assert that we don't have too many pending writers.
              ASSERT_LE(pw, kWriters / 3);
              break;
            }
          }
          auto wait_status = future.Wait();
          ASSERT_TRUE(wait_status.ok() || wait_status.IsTimedOut()) << wait_status;
          --pending_writes;
        } else {
          ASSERT_OK(status);
        }
      }
    });
  }

  thread_holder.WaitAndStop(30s);
  LOG(INFO) << "Max pending writes: " << max_pending_writes.load();
}

}  // namespace yb
