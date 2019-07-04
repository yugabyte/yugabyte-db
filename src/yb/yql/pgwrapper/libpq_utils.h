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

#ifndef YB_YQL_PGWRAPPER_LIBPQ_UTILS_H
#define YB_YQL_PGWRAPPER_LIBPQ_UTILS_H

#include <libpq-fe.h>

#include <memory>

#include "yb/util/result.h"

namespace yb {
namespace pgwrapper {

struct PGConnClose {
  void operator()(PGconn* conn) const;
};

struct PGResultClear {
  void operator()(PGresult* result) const;
};

typedef std::unique_ptr<PGconn, PGConnClose> PGConnPtr;
typedef std::unique_ptr<PGresult, PGResultClear> PGResultPtr;

CHECKED_STATUS Execute(PGconn* conn, const std::string& command);

Result<PGResultPtr> Fetch(PGconn* conn, const std::string& command);

// Fetches data matrix of specified size. I.e. exact number of rows and columns are expected.
Result<PGResultPtr> FetchMatrix(PGconn* conn, const std::string& command, int rows, int columns);

Result<int32_t> GetInt32(PGresult* result, int row, int column);

Result<int64_t> GetInt64(PGresult* result, int row, int column);

Result<std::string> GetString(PGresult* result, int row, int column);

Result<int32_t> GetValueImpl(PGresult* result, int row, int column, int32_t*) {
  return GetInt32(result, row, column);
}

Result<int64_t> GetValueImpl(PGresult* result, int row, int column, int64_t*) {
  return GetInt64(result, row, column);
}

Result<std::string> GetValueImpl(PGresult* result, int row, int column, std::string*) {
  return GetString(result, row, column);
}

template<class T>
Result<T> GetValue(PGresult* result, int row, int column) {
  // static_cast<T*>(nullptr) is a trick to use function overload from template.
  return GetValueImpl(result, row, column, static_cast<T*>(nullptr));
}

template <class T>
Result<T> FetchValue(PGconn* conn, const std::string& command) {
  auto res = VERIFY_RESULT(FetchMatrix(conn, command, 1, 1));
  return GetValue<T>(res.get(), 0, 0);
}

} // namespace pgwrapper
} // namespace yb

#endif // YB_YQL_PGWRAPPER_LIBPQ_UTILS_H
