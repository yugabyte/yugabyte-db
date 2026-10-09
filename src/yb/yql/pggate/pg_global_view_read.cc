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

#include "yb/yql/pggate/pg_global_view_read.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/write_buffer.h"

#include "yb/yql/pggate/pg_client.h"

namespace yb::pggate {

namespace {

size_t ComputeRowSize(std::span<const std::optional<Slice>> cells) {
  size_t size = 0;
  for (const auto& cell : cells) {
    size += GvCellSize(cell);
  }
  return size;
}

}  // namespace

bool EncodeGvRow(
    std::span<const std::optional<Slice>> cells, size_t row_size, WriteBuffer* buffer,
    size_t max_size) {
  DCHECK_EQ(row_size, ComputeRowSize(cells));
  if (buffer->size() + row_size > max_size) {
    return false;
  }
  for (const auto& value : cells) {
    if (!value) {
      WriteNullColumn(buffer);
      continue;
    }
    DCHECK(!value->empty() && value->cend()[-1] == '\0') << "Cell is not NUL-terminated";
    WriteBinaryColumn(*value, buffer);
  }
  return true;
}

Status DecodeGvRowValues(Slice* cursor, std::span<const char*> values) {
  for (auto& value : values) {
    if (VERIFY_RESULT(PgDocData::CheckedReadHeaderIsNull(cursor))) {
      value = nullptr;
      continue;
    }
    const auto len = VERIFY_RESULT(PgDocData::CheckedReadNumber<uint64_t>(cursor));
    // The length covers the NUL terminator, so it is never 0.
    SCHECK_GE(len, uint64_t{1}, Corruption, "Global view value length is missing the terminator");
    SCHECK_GE(cursor->size(), len, Corruption, "Unexpected end of global view row data");
    SCHECK_EQ(
        cursor->cdata()[len - 1], '\0', Corruption, "Global view value is not NUL-terminated");
    value = cursor->cdata();
    cursor->remove_prefix(len);
  }
  return Status::OK();
}

void PgGlobalViewRead::SetParams(std::span<const char*> values) {
  params_.clear();
  params_.reserve(values.size());
  for (auto* v : values) {
    v ? params_.emplace_back(std::in_place, v) : params_.emplace_back();
  }
}

void PgGlobalViewRead::ResetScanData() {
  rows_data_ = {};
  cursor_ = {};
  remaining_rows_ = 0;
  num_cols_ = 0;
  decltype(last_error_)().swap(last_error_);
}

void PgGlobalViewRead::ClearScanState() {
  ResetScanData();
  // Safe today because SetParams re-populates params_ before the next
  // ExecScan (on both the first scan and every rescan). Revisit if pagination
  // (#30843) makes a later fetch reuse params_ without a fresh SetParams.
  decltype(params_)().swap(params_);
}

YbcPgGvScanResult PgGlobalViewRead::ExecScan(
    PgClient& client, std::string_view database_name, std::string_view query,
    std::string_view tserver_uuid) {
  ResetScanData();

  auto res = client.RemoteExec(query, database_name, tserver_uuid, params_);
  if (!res.ok()) {
    last_error_ = res.status().ToString();
    return {};
  }

  auto& resp = res->resp;
  if (!resp.error_message().empty()) {
    last_error_ = std::move(*resp.mutable_error_message());
    return {};
  }

  // A tserver too old to send a sidecar puts the rows in a response field this
  // build no longer reads, and leaves the row count unset. Report that instead
  // of silently returning zero rows.
  if (!resp.has_num_rows() || !resp.has_num_cols()) {
    last_error_ = "Global view response has no row count. The tserver may be running an "
                  "older version";
    return {};
  }

  if (resp.num_rows() < 0 || resp.num_cols() < 0) {
    last_error_ = Format("Invalid global view response: num_rows=$0 num_cols=$1",
        resp.num_rows(), resp.num_cols());
    return {};
  }

  // Validation is done, so the scan state can be set up.
  rows_data_ = std::move(res->rows_data);
  cursor_ = rows_data_.AsSlice();
  remaining_rows_ = resp.num_rows();
  num_cols_ = resp.num_cols();

  return {
      .num_rows = remaining_rows_,
      .num_cols = num_cols_,
      .reached_size_limit = resp.reached_size_limit()};
}

bool PgGlobalViewRead::NextRow(const char** values) {
  if (remaining_rows_ <= 0) {
    return false;
  }
  auto status = DecodeGvRowValues(&cursor_, {values, static_cast<size_t>(num_cols_)});
  if (!status.ok()) {
    last_error_ = status.ToString();
    remaining_rows_ = 0;
    return false;
  }
  --remaining_rows_;
  if (remaining_rows_ == 0 && !cursor_.empty()) {
    LOG(DFATAL) << "Trailing bytes after the last global view row: " << cursor_.size();
    last_error_ = Format("$0 trailing bytes after the last row", cursor_.size());
    return false;
  }
  return true;
}

const char* PgGlobalViewRead::GetError() const {
  return last_error_.empty() ? nullptr : last_error_.c_str();
}

}  // namespace yb::pggate
