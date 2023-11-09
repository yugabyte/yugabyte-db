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
concept BasePGType =
    IsPGNonNeg<T> || IsPGIntType<T> || IsPGFloatType<T> ||
    std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_same_v<T, char> ||
    std::is_same_v<T, PGOid> || std::is_same_v<T, Uuid>;

template<class T>
concept OptionalPGType =
    std::is_same_v<T, std::optional<typename T::value_type>> && BasePGType<typename T::value_type>;

template<class T>
concept AllowedPGType = BasePGType<T> || OptionalPGType<T>;

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

template<OptionalPGType T>
struct PGTypeTraits<T> {
  using ReturnType = std::optional<typename PGTypeTraits<typename T::value_type>::ReturnType>;
};

using PGUint16 = PGNonNeg<int16_t>;
using PGUint32 = PGNonNeg<int32_t>;
using PGUint64 = PGNonNeg<int64_t>;

template<AllowedPGType T>
using GetValueResult = Result<typename PGTypeTraits<T>::ReturnType>;

template<BasePGType T>
GetValueResult<T> GetValue(const PGresult* result, int row, int column);

template<OptionalPGType T>
GetValueResult<T> GetValue(const PGresult* result, int row, int column) {
  if (PQgetisnull(result, row, column)) {
    return std::nullopt;
  }
  return GetValue<typename T::value_type>(result, row, column);
}

const std::string& DefaultColumnSeparator();
const std::string& DefaultRowSeparator();

// DEPRECATED: use FetchRows instead.
Result<std::string> ToString(PGresult* result, int row, int column);

std::string PqEscapeLiteral(const std::string& input);
std::string PqEscapeIdentifier(const std::string& input);

namespace libpq_utils::internal {

template <AllowedPGType... ColumnTypes>
class RowFetcher {
 public:
  static constexpr auto kNumColumns = sizeof...(ColumnTypes);
  static_assert(kNumColumns > 1);
  using RowType = std::tuple<typename PGTypeTraits<ColumnTypes>::ReturnType...>;

  static Result<RowType> Fetch(const PGresult* res, int row) {
    RowType result;
    RETURN_NOT_OK(Update<0>(&result, res, row));
    return result;
  }

 private:
  template <size_t ColumnIdx>
  static Status Update(RowType* dest, const PGresult* res, int row) {
    using ColumnType = std::tuple_element_t<ColumnIdx, std::tuple<ColumnTypes...>>;
    std::get<ColumnIdx>(*dest) = VERIFY_RESULT(GetValue<ColumnType>(res, row, ColumnIdx));
    constexpr auto kNextColumnIdx = ColumnIdx + 1;
    if constexpr (kNextColumnIdx < kNumColumns) {
      return Update<kNextColumnIdx>(dest, res, row);
    } else {
      return Status::OK();
    }
  }
};

template <AllowedPGType ColumnType>
class RowFetcher<ColumnType> {
 public:
  static constexpr auto kNumColumns = 1;
  using RowType = typename PGTypeTraits<ColumnType>::ReturnType;

  static Result<RowType> Fetch(const PGresult* res, int row) {
    return GetValue<ColumnType>(res, row, 0);
  }
};

template <AllowedPGType... Args>
class FetchHelper {
  using Fetcher = RowFetcher<Args...>;
  using RowType = typename Fetcher::RowType;
  using RowsType = std::vector<RowType>;
  using RowResult = Result<RowType>;
  using RowsResult = Result<RowsType>;

 public:
  static RowResult FetchRow(Result<PGResultPtr>&& source) {
    return FetchRow(VERIFY_RESULT(CheckSource(std::move(source))).get());
  }

  static RowsResult FetchRows(Result<PGResultPtr>&& source) {
    return FetchRows(VERIFY_RESULT(CheckSource(std::move(source))).get());
  }

 private:
  static RowResult FetchRow(const PGresult* source) {
    SCHECK_EQ(PQntuples(source), 1, RuntimeError, "Unexpected number of rows");
    return Fetcher::Fetch(source, 0);
  }

  static RowsResult FetchRows(const PGresult* source) {
    RowsType result;
    auto num_rows = PQntuples(source);
    result.reserve(num_rows);
    for (int row = 0; row < num_rows; ++row) {
      result.push_back(VERIFY_RESULT(Fetcher::Fetch(source, row)));
    }
    return result;
  }

  static Result<PGResultPtr> CheckSource(Result<PGResultPtr>&& source) {
    if (source.ok()) {
      SCHECK_EQ(
          PQnfields(source->get()), Fetcher::kNumColumns,
          RuntimeError, "Unexpected number of columns");
    }
    return source;
  }
};

} // namespace libpq_utils::internal

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
  requires(sizeof...(Args) > 0)
  Result<PGResultPtr> FetchFormat(const std::string& format, Args&&... args) {
    return Fetch(Format(format, std::forward<Args>(args)...));
  }

  // Fetches data matrix of specified size. I.e. exact number of rows and columns are expected.
  Result<PGResultPtr> FetchMatrix(const std::string& command, int rows, int columns);

  template <class... Args>
  auto FetchRow(const std::string& query) {
    return libpq_utils::internal::FetchHelper<Args...>::FetchRow(Fetch(query));
  }

  template <class... Args>
  auto FetchRows(const std::string& query) {
    return libpq_utils::internal::FetchHelper<Args...>::FetchRows(Fetch(query));
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
  const std::string& replication = std::string();
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
