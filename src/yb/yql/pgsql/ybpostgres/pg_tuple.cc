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
//
// The original code in PostgreSQL for this utility is "src/backend/access/common/printtup.c".
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ybpostgres/pg_tuple.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

namespace yb {
namespace pgapi {

//--------------------------------------------------------------------------------------------------
// Constructor.
PGTuple::PGTuple() {
}

PGTuple::~PGTuple() {
}

CHECKED_STATUS WriteTuple(const PgsqlResultSet& tuple, faststring* buffer) {
  return Status::OK();
}

CHECKED_STATUS WriteTupleDesc(const PgsqlRSRowDesc& tuple_desc, faststring* buffer) {
  // 'T': Tuple descriptor message type.
  PGPqFormatter formatter;
  formatter.pq_beginmessage('T');

  // Number of columns in tuples.
  uint16_t ncols = tuple_desc.rscol_count();
  formatter.pq_sendint(ncols, sizeof(ncols));

  return Status::OK();
}

}  // namespace pgapi
}  // namespace yb
