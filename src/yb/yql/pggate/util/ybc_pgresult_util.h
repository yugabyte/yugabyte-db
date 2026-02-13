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

#include <stdint.h>

#ifdef __cplusplus

#include <cstddef>

struct pg_result;

namespace yb {

class PgResultPB;

// Serializes a PGresult into a PgResultPB. Sets exec_status to PGRES_TUPLES_OK and
// encodes rows/columns. Stops early if the encoded size exceeds max_resp_size bytes.
// Returns true if all rows were encoded, false if truncated due to size limit.
bool PgResultToPB(pg_result* result, PgResultPB* result_pb, size_t max_resp_size);

// Reconstructs a PGresult from a PgResultPB. If the protobuf carries an error status,
// the returned PGresult will have the error message set. Caller owns the returned object
// and must free it with PQclear().
pg_result* PgResultFromPB(const PgResultPB& result_pb);

} // namespace yb

extern "C" {
#endif

// Deserializes a serialized PgResultPB byte buffer into a PGresult*.
// Caller owns the returned PGresult and must free it with PQclear().
struct pg_result* YBCPgResultFromPB(const uint8_t* buf, size_t size);

#ifdef __cplusplus
}  // extern "C"
#endif
