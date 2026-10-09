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

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {

class WriteBuffer;

namespace pggate {

class PgClient;

// Global view rows travel in an RPC sidecar, in the format DocDB already uses
// for PG results (pg_doc_data.h). A row holds num_cols columns. A column is the
// PgWireDataHeader byte, then, for a non-NULL value,
// [uint64 length (network order)][value].
//
// Every value is in PG text format, whatever its SQL type, and keeps the NUL
// terminator libpq stores after it. The length covers that terminator, so
// readers use values as C strings in place. The row (int 1, text 'abc', float
// NULL) encodes as:
//   00  00 00 00 00 00 00 00 02  31        00  -- header, length 2, "1", NUL
//   00  00 00 00 00 00 00 00 04  61 62 63  00  -- header, length 4, "abc", NUL
//   01                                         -- header with the NULL bit set

// Bytes a cell adds to the row. Callers sum this over their cells and pass the
// total to EncodeGvRow as row_size.
inline size_t GvCellSize(const std::optional<Slice>& cell) {
  return PgWireDataHeader::kSerializedSize + (cell ? sizeof(uint64_t) + cell->size() : 0);
}

// Appends cells as one row. A nullopt cell means SQL NULL. Every other cell must
// end with a NUL terminator, which counts towards its size. row_size must be the
// sum of GvCellSize over cells. Does not retain the cells' bytes.
//
// Returns false and leaves the buffer untouched if the row would push
// buffer->size() past max_size. The buffer then holds only whole rows, and the
// caller must stop.
//
// The caller loops over the rows and reads its own cells, which keeps the
// encoder free of libpq.
bool EncodeGvRow(
    std::span<const std::optional<Slice>> cells, size_t row_size, WriteBuffer* buffer,
    size_t max_size);

// Decodes one row from *cursor into values. A nullptr entry means SQL NULL. The
// entries point into *cursor's buffer, which must outlive them.
Status DecodeGvRowValues(Slice* cursor, std::span<const char*> values);

// Per-scan state for federated YugabyteDB global view reads.
//
// Each ForeignScan targeting a single tserver gets its own instance.
// The tserver UUID is not owned here; it is passed in by the caller
// (from the plan's fdw_private) on each ExecScan call.
//
class PgGlobalViewRead : public PgMemctx::Registrable {
 public:
  // Set text-format parameter values; a nullptr entry means NULL.
  void SetParams(std::span<const char*> values);

  // Runs the query on the given tserver. YbcPgGvScanResult returns
  // num_rows, num_cols and reached_size_limit. num_rows is 0 on error
  // (see GetError) or empty result, and reached_size_limit says whether
  // the tserver dropped rows that did not fit in the RPC message.
  YbcPgGvScanResult ExecScan(
      PgClient& client, std::string_view database_name, std::string_view query,
      std::string_view tserver_uuid);

  // Reads the next row into values, which must hold the num_cols entries
  // reported by the last ExecScan; nullptr means SQL NULL. Values stay
  // valid until ClearScanState or the next ExecScan. Returns false when
  // no rows remain or on malformed data (GetError is set).
  bool NextRow(const char** values);

  // Error from the last ExecScan/NextRow; NULL if they succeeded.
  const char* GetError() const;

  // Releases the per-scan row buffer, error message, and params.
  void ClearScanState();

 private:
  void ResetScanData();

  RefCntSlice rows_data_;
  Slice cursor_;
  int remaining_rows_ = 0;
  int num_cols_ = 0;
  std::vector<std::optional<std::string>> params_;
  std::string last_error_;
};

}  // namespace pggate
}  // namespace yb
