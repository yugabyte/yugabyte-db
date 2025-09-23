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

#pragma once

#include <cassandra.h>

#include <string>

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

namespace yb {

class CassandraJson;
class CassandraStatement;

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

template <class Out>
struct GetCassandraValue {
  static CassError Apply(const CassValue* value, Out* out);
};

class CassandraValue {
 public:
  explicit CassandraValue(const CassValue* value) : value_(value) {}

  template <class Out>
  void Get(Out* out) const {
    CHECK_EQ(CASS_OK, GetCassandraValue<Out>::Apply(value_, out));
  }

  template <class Out>
  Out As() const {
    Out result;
    Get(&result);
    return result;
  }

  bool IsNull() const;

  std::string ToString() const;

 private:
  const CassValue* value_;
};

typedef std::unique_ptr<
    CassIterator, FuncDeleter<CassIterator, &cass_iterator_free>> CassIteratorPtr;

class CassandraRowIterator {
 public:
  explicit CassandraRowIterator(CassIterator* iterator) : cass_iterator_(iterator) {}

  bool Next();

  template <class Out>
  void Get(Out* out) const {
    Value().Get(out);
  }

  CassandraValue Value() const;

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

  CassandraValue Value(size_t index) const;

  CassandraRowIterator CreateIterator() const;

  std::string RenderToString(const std::string& separator = ",");

  void TakeIterator(CassIteratorPtr iterator);

 private:
  const CassRow* cass_row_; // owned by iterator
  CassIteratorPtr cass_iterator_;
};

class CassandraIterator {
 public:
  explicit CassandraIterator(CassIterator* iterator) : cass_iterator_(iterator) {}

  bool Next();

  CassandraRow Row();

  void MoveToRow(CassandraRow* row);

 private:
  CassIteratorPtr cass_iterator_;
};

typedef std::unique_ptr<
    const CassResult, FuncDeleter<const CassResult, &cass_result_free>> CassResultPtr;

class CassandraResult {
 public:
  explicit CassandraResult(const CassResult* result) : cass_result_(result) {}

  CassandraIterator CreateIterator() const;

  std::string RenderToString(const std::string& line_separator = ";",
                             const std::string& value_separator = ",") const;

  bool HasMorePages() const;

  const CassResult* get() const { return cass_result_.get(); }

 private:
  CassResultPtr cass_result_;
};

typedef std::unique_ptr<
    const CassPrepared, FuncDeleter<const CassPrepared, &cass_prepared_free>> CassPreparedPtr;

class CassandraPrepared {
 public:
  CassandraPrepared() = default;
  explicit CassandraPrepared(const CassPrepared* prepared) : prepared_(prepared) {}

  CassandraStatement Bind();

  bool operator!() const {
    return !prepared_;
  }

  explicit operator bool() const {
    return prepared_ != nullptr;
  }

 private:
  CassPreparedPtr prepared_;
};

typedef std::unique_ptr<
    CassFuture, FuncDeleter<CassFuture, &cass_future_free>> CassFuturePtr;

class CassandraFuture {
 public:
  explicit CassandraFuture(CassFuture* future) : future_(future) {}

  bool Ready() const;

  Status Wait();

  Status WaitFor(MonoDelta duration);

  CassandraResult Result();

  CassandraPrepared Prepared();

 private:
  Status CheckErrorCode();

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

  void SetKeyspace(const std::string& keyspace);
  void SetPageSize(int page_size);
  void SetPagingState(const CassandraResult& result);

  CassandraStatement& Bind(size_t index, const std::string& v);
  CassandraStatement& Bind(size_t index, const cass_bool_t& v);
  CassandraStatement& Bind(size_t index, const cass_float_t& v);
  CassandraStatement& Bind(size_t index, const cass_double_t& v);
  CassandraStatement& Bind(size_t index, const cass_int32_t& v);
  CassandraStatement& Bind(size_t index, const cass_int64_t& v);
  CassandraStatement& Bind(size_t index, const CassandraJson& v);

  CassStatement* get() const;

 private:
  friend class CassandraBatch;
  friend class CassandraSession;

  CassStatementPtr cass_statement_;
};

typedef std::unique_ptr<CassBatch, FuncDeleter<CassBatch, &cass_batch_free>> CassBatchPtr;

class CassandraBatch {
 public:
  explicit CassandraBatch(CassBatchType type) : cass_batch_(cass_batch_new(type)) {}

  void Add(CassandraStatement* statement);

 private:
  friend class CassandraSession;

  CassBatchPtr cass_batch_;
};

struct DeleteSession {
  void operator()(CassSession* session) const;
};

typedef std::unique_ptr<CassSession, DeleteSession> CassSessionPtr;

class CassandraSession {
 public:
  CassandraSession() = default;

  Status Connect(CassCluster* cluster);

  static Result<CassandraSession> Create(CassCluster* cluster);

  // if timeout_ms == 0 - default CASS_DEFAULT_REQUEST_TIMEOUT_MS = 12000 is used.
  Status Execute(const CassandraStatement& statement, uint32_t timeout_ms = 0);

  Result<CassandraResult> ExecuteWithResult(const CassandraStatement& statement);

  CassandraFuture ExecuteGetFuture(const CassandraStatement& statement);

  CassandraFuture ExecuteGetFuture(const std::string& query);

  // if timeout_ms == 0 - default CASS_DEFAULT_REQUEST_TIMEOUT_MS = 12000 is used.
  Status ExecuteQuery(const std::string& query, uint32_t timeout_ms = 0);

  template <class... Args>
  Status ExecuteQueryFormat(const std::string& query, Args&&... args) {
    return ExecuteQuery(Format(query, std::forward<Args>(args)...));
  }

  Result<CassandraResult> ExecuteWithResult(const std::string& query);

  Result<std::string> ExecuteAndRenderToString(const std::string& statement);

  template <class Action>
  Status ExecuteAndProcessOneRow(
      const CassandraStatement& statement, const Action& action) {
    auto result = VERIFY_RESULT(ExecuteWithResult(statement));
    auto iterator = result.CreateIterator();
    if (!iterator.Next()) {
      return STATUS(IllegalState, "Row does not exists");
    }
    auto row = iterator.Row();
    action(row);
    if (iterator.Next()) {
      return STATUS(IllegalState, "Multiple rows returned");
    }
    return Status::OK();
  }

  template <class Action>
  Status ExecuteAndProcessOneRow(const std::string& query, const Action& action) {
    return ExecuteAndProcessOneRow(CassandraStatement(query), action);
  }

  template <class T>
  Result<T> FetchValue(const std::string& query) {
    T result = T();
    RETURN_NOT_OK(ExecuteAndProcessOneRow(query, [&result](const CassandraRow& row) {
      result = row.Value(0).As<T>();
    }));
    return result;
  }

  Status ExecuteBatch(const CassandraBatch& batch);

  CassandraFuture SubmitBatch(const CassandraBatch& batch);

  // If 'local_keyspace' is not empty, creating temporary CassStatement and setting keyspace
  // for this statement only. Result CassPrepared will be based on this temporary statement.
  Result<CassandraPrepared> Prepare(const std::string& prepare_query,
                                    MonoDelta timeout = MonoDelta::kZero,
                                    const std::string& local_keyspace = std::string());

  void Reset();

 private:
  CassSessionPtr cass_session_;
};

YB_STRONGLY_TYPED_BOOL(UsePartitionAwareRouting);

class CppCassandraDriver {
 public:
  CppCassandraDriver(
      const std::vector<std::string>& hosts, uint16_t port,
      UsePartitionAwareRouting use_partition_aware_routing);

  ~CppCassandraDriver();

  Result<CassandraSession> CreateSession();

  void EnableTLS(const std::vector<std::string>& ca_certs);
  void SetCredentials(const std::string& username, const std::string& password);

 private:
  CassCluster* cass_cluster_ = nullptr;
};

class CassandraJson {
 public:
  CassandraJson() = default;
  explicit CassandraJson(const std::string& s) : value_(s) {}
  explicit CassandraJson(std::string&& s) : value_(std::move(s)) {}
  explicit CassandraJson(const char* s) : value_(s) {}

  const std::string& value() const {
    return value_;
  }

 private:
  std::string value_;
};

inline std::ostream& operator<<(std::ostream& out, const CassandraJson& value) {
  return out << value.value();
}

inline bool operator==(const CassandraJson& lhs, const CassandraJson& rhs) {
  return lhs.value() == rhs.value();
}

extern const MonoDelta kCassandraTimeOut;
extern const std::string kCqlTestKeyspace;

Result<CassandraSession> EstablishSession(CppCassandraDriver* driver);

} // namespace yb
