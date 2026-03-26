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

PgGlobalViewRead::PgGlobalViewRead(
    PgClient& pg_client, const char* query, std::vector<std::string> tserver_uuids)
    : pg_client_(pg_client), query_(query), tserver_uuids_(std::move(tserver_uuids)) {}

void PgGlobalViewRead::ResetScan() {
  // params_ is not cleared here: postgresReScanForeignScan sets cursor_exists to false,
  // forcing create_cursor to run again on the next iterate, which refreshes params via
  // SetParams before the scan begins.
  next_tserver_idx_ = 0;
}

void PgGlobalViewRead::SetParams(int num_params, const char** values) {
  DCHECK(values != nullptr);
  params_.resize(num_params);
  for (int i = 0; i < num_params; ++i) {
    if (values[i]) {
      params_[i] = values[i];
    } else {
      params_[i] = std::nullopt;
    }
  }
}

YbcRemotePgExecResult PgGlobalViewRead::ExecScan() {
  YbcRemotePgExecResult result = { nullptr, 0 };
  while (next_tserver_idx_ < tserver_uuids_.size()) {
    auto res = pg_client_.RemoteExec(query_, tserver_uuids_[next_tserver_idx_++], params_);
    if (!res.ok()) {
      LOG(WARNING) << "Failed to execute remote pg query: " << res.status();
      continue;
    }

    // Trying to create the PGresult object here causes a circular dependency issue.
    // So we keep the protobuf response and construct the PGresult from postgres_fdw
    // layer.
    const auto& pb = res->pg_result();

    if (!pb.error_message().empty()) {
      LOG(WARNING) << "Remote pg query failed on tserver "
                   << tserver_uuids_[next_tserver_idx_ - 1] << ": " << pb.error_message();
      continue;
    }

    if (pb.rows_size() == 0) {
      continue;
    }

    const auto pb_size = pb.ByteSizeLong();
    DCHECK_GT(pb_size, 0) << "Received protobuf size should be positive, got " << pb_size;

    serialized_result_.resize(pb_size);
    auto* buf = reinterpret_cast<uint8_t*>(serialized_result_.data());
    pb.SerializeWithCachedSizesToArray(buf);
    result.pgresult = buf;
    result.pgresult_size = serialized_result_.size();
    break;
  }

  return result;
}

}  // namespace yb::pggate
