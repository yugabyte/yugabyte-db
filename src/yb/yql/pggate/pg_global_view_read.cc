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

#include "yb/yql/pggate/pg_client.h"

namespace yb::pggate {

void PgGlobalViewRead::SetParams(std::span<const char*> values) {
  params_.clear();
  params_.reserve(values.size());
  for (auto* v : values) {
    v ? params_.emplace_back(std::in_place, v) : params_.emplace_back();
  }
}

void PgGlobalViewRead::ClearScanState() {
  decltype(result_pb_)().Swap(&result_pb_);
  decltype(last_error_)().swap(last_error_);
  // Safe today because SetParams re-populates params_ before the next
  // ExecScan (on both the first scan and every rescan). Revisit if pagination
  // (#30843) makes a later fetch reuse params_ without a fresh SetParams.
  decltype(params_)().swap(params_);
}

YbcPgResultPB PgGlobalViewRead::ExecScan(
    PgClient& client, std::string_view database_name, std::string_view query,
    std::string_view tserver_uuid) {
  auto res = client.RemoteExec(
      query, database_name, tserver_uuid, params_);
  if (!res.ok()) {
    last_error_ = res.status().ToString();
    return nullptr;
  }

  auto& pb = *res->mutable_pg_result();

  if (!pb.error_message().empty()) {
    last_error_ = std::move(*pb.mutable_error_message());
    return nullptr;
  }

  if (pb.rows_size() == 0) {
    return nullptr;
  }

  result_pb_.Swap(&pb);
  return &result_pb_;
}

const char* PgGlobalViewRead::GetError() const {
  return last_error_.empty() ? nullptr : last_error_.c_str();
}

}  // namespace yb::pggate
