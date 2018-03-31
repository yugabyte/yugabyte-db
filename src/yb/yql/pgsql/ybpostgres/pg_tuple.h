//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_TUPLE_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_TUPLE_H_

#include "yb/common/pgsql_resultset.h"

#include "yb/yql/pgsql/ybpostgres/pg_defs.h"
#include "yb/yql/pgsql/ybpostgres/pg_instr.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqformat.h"

namespace yb {
namespace pgapi {

class PGTuple {
 public:
  PGTuple();
  virtual ~PGTuple();

  CHECKED_STATUS WriteTuple(const PgsqlResultSet& result_set, faststring* buffer);
  CHECKED_STATUS WriteTupleDesc(const PgsqlResultSet& result_set, faststring* buffer);
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_TUPLE_H_
