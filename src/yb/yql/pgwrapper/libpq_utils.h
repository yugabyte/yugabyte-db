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

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "libpq-fe.h" // NOLINT

#include "yb/common/transaction.pb.h"

#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/result.h"
#include "yb/util/subprocess.h"
#include "yb/util/uuid.h"

namespace yb::pgwrapper {

struct PGConnClose {
  void operator()(PGconn* conn) const;
};

struct PGResultClear {
  void operator()(PGresult* result) const;
};

typedef std::unique_ptr<PGconn, PGConnClose> PGConnPtr;
typedef std::unique_ptr<PGresult, PGResultClear> PGResultPtr;

struct PGOid {};

template<class T>
inline constexpr bool IsPGFloatType =
    std::is_same_v<T, float> || std::is_same_v<T, double>;

template<class T>
inline constexpr bool IsPGIntType =
    std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>;

template<class T>
requires(IsPGIntType<T>)
struct PGNonNeg {
  using Type = T;
};

template<class T>
struct IsPGNonNegImpl : std::false_type {};

template<class T>
requires(std::is_same_v<T, PGNonNeg<typename T::Type>>)
struct IsPGNonNegImpl<T> : std::true_type {};

template<class T>
inline constexpr bool IsPGNonNeg = IsPGNonNegImpl<T>::value;

template<class T>
concept AllowedPGType =
    IsPGNonNeg<T> || IsPGIntType<T> || IsPGFloatType<T> ||
    std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_same_v<T, char> ||
    std::is_same_v<T, PGOid> || std::is_same_v<T, Uuid>;

template<AllowedPGType T>
struct PGTypeTraits {
  using ReturnType = T;
};

template<AllowedPGType T>
requires(IsPGNonNeg<T>)
struct PGTypeTraits<T> {
  using ReturnType = std::make_unsigned_t<typename T::Type>;
};

template<AllowedPGType T>
requires(std::is_same_v<T, PGOid>)
struct PGTypeTraits<T> {
  using ReturnType = Oid;
};

using PGUint16 = PGNonNeg<int16_t>;
using PGUint32 = PGNonNeg<int32_t>;
using PGUint64 = PGNonNeg<int64_t>;

template<class T>
using GetValueResult = Result<typename PGTypeTraits<T>::ReturnType>;

template<class T>
GetValueResult<T> GetValue(const PGresult* result, int row, int column);

template<class T>
requires(std::is_same_v<T, std::optional<typename T::value_type>>)
Result<std::optional<typename PGTypeTraits<typename T::value_type>::ReturnType>> GetValue(
    const PGresult* result, int row, int column) {
  if (PQgetisnull(result, row, column)) {
    return std::nullopt;
  }
  return GetValue<typename T::value_type>(result, row, column);
}

const std::string& DefaultColumnSeparator();
const std::string& DefaultRowSeparator();

Result<std::string> ToString(PGresult* result, int row, int column);
Result<std::string> RowToString(
    PGresult* result, int row, const std::string& sep = DefaultColumnSeparator());
void LogResult(PGresult* result);

std::string PqEscapeLiteral(const std::string& input);
std::string PqEscapeIdentifier(const std::string& input);

template <class... Args>
class FetchAllHelper {
  using Tuple = std::tuple<Args...>;
  using TupleVector = std::vector<Tuple>;

 public:
  static Result<TupleVector> Fetch(const PGresult* res) {
    constexpr auto kExpectedColumns = sizeof...(Args);
    SCHECK_EQ(PQnfields(res), kExpectedColumns, RuntimeError, "Unexpected number of columns");
    TupleVector result(PQntuples(res));
    auto row = 0;
    for (auto& tuple : result) {
      RETURN_NOT_OK(Update<0>(&tuple, res, row++));
    }
    return result;
  }

 private:
  template <size_t ElIdx>
  static Status Update(Tuple* dest, const PGresult* res, int row) {
    auto& element = std::get<ElIdx>(*dest);
    element = VERIFY_RESULT(GetValue<std::remove_cvref_t<decltype(element)>>(res, row, ElIdx));
    constexpr auto kNextElIdx = ElIdx + 1;
    if constexpr (kNextElIdx < sizeof...(Args)) {
      return Update<kNextElIdx>(dest, res, row);
    } else {
      return Status::OK();
    }
  }
};

class PGConn {
 public:
  ~PGConn();

  PGConn(PGConn&& rhs);
  PGConn& operator=(PGConn&& rhs);

  // Pass in an optional conn_str_for_log for logging purposes. This is used in case
  // conn_str contains sensitive information (e.g. password).
  static Result<PGConn> Connect(
      const std::string& conn_str,
      bool simple_query_protocol,
      const std::string& conn_str_for_log) {
    return Connect(conn_str,
                   CoarseMonoClock::Now() + MonoDelta::FromSeconds(60) /* deadline */,
                   simple_query_protocol,
                   conn_str_for_log);
  }
  static Result<PGConn> Connect(
      const std::string& conn_str,
      CoarseTimePoint deadline,
      bool simple_query_protocol,
      const std::string& conn_str_for_log);

  // Reconnect.
  void Reset();

  Status Execute(const std::string& command, bool show_query_in_error = true);

  template <class... Args>
  Status ExecuteFormat(const std::string& format, Args&&... args) {
    return Execute(Format(format, std::forward<Args>(args)...));
  }

  bool IsBusy();

  Result<PGResultPtr> Fetch(const std::string& command);

  template <class... Args>
  Result<PGResultPtr> FetchFormat(const std::string& format, Args&&... args) {
    return Fetch(Format(format, std::forward<Args>(args)...));
  }

  // Fetches data matrix of specified size. I.e. exact number of rows and columns are expected.
  Result<PGResultPtr> FetchMatrix(const std::string& command, int rows, int columns);
  Result<std::string> FetchRowAsString(
      const std::string& command, const std::string& sep = DefaultColumnSeparator());
  Result<std::string> FetchAllAsString(const std::string& command,
      const std::string& column_sep = DefaultColumnSeparator(),
      const std::string& row_sep = DefaultRowSeparator());

  template <class T>
  auto FetchValue(const std::string& command) -> decltype(GetValue<T>(nullptr, 0, 0)) {
    auto res = VERIFY_RESULT(FetchMatrix(command, 1, 1));
    return GetValue<T>(res.get(), 0, 0);
  }

  template <class... Args>
  auto FetchAll(const std::string& query) -> decltype(FetchAllHelper<Args...>::Fetch(nullptr)) {
    return FetchAllHelper<Args...>::Fetch(VERIFY_RESULT(Fetch(query)).get());
  }

  Status StartTransaction(IsolationLevel isolation_level);
  Status CommitTransaction();
  Status RollbackTransaction();

  Status TestFailDdl(const std::string& ddl_to_fail);

  // Would this query use an index [only] scan?
  Result<bool> HasIndexScan(const std::string& query);
  Result<bool> HasScanType(const std::string& query, const std::string expected_scan_type);

  Status CopyBegin(const std::string& command);
  Result<PGResultPtr> CopyEnd();

  void CopyStartRow(int16_t columns);

  void CopyPutInt16(int16_t value);
  void CopyPutInt32(int32_t value);
  void CopyPutInt64(int64_t value);
  void CopyPut(const char* value, size_t len);

  void CopyPutString(const std::string& value) {
    CopyPut(value.c_str(), value.length());
  }

  PGconn* get() {
    return impl_.get();
  }

 private:
  struct CopyData;

  PGConn(PGConnPtr ptr, bool simple_query_protocol);

  bool CopyEnsureBuffer(size_t len);
  bool CopyFlushBuffer();

  PGConnPtr impl_;
  bool simple_query_protocol_;
  std::unique_ptr<CopyData> copy_data_;
};

// Settings to pass to PGConnBuilder.
struct PGConnSettings {
  constexpr static const char* kDefaultUser = "postgres";

  const std::string& host;
  uint16_t port;
  const std::string& dbname = std::string();
  const std::string& user = kDefaultUser;
  const std::string& password = std::string();
  size_t connect_timeout = 0;
};

class PGConnBuilder {
 public:
  explicit PGConnBuilder(const PGConnSettings& settings);
  Result<PGConn> Connect(bool simple_query_protocol = false) const;

 private:
  const std::string conn_str_;
  const std::string conn_str_for_log_;
  const size_t connect_timeout_;
};

Result<PGConn> Execute(Result<PGConn> connection, const std::string& query);
Result<PGConn> SetHighPriTxn(Result<PGConn> connection);
Result<PGConn> SetLowPriTxn(Result<PGConn> connection);
Status SetMaxBatchSize(PGConn* conn, size_t max_batch_size);

class PGConnPerf {
 public:
  explicit PGConnPerf(PGConn* conn);
  ~PGConnPerf();
 private:
  Subprocess process_;
};

} // namespace yb::pgwrapper
