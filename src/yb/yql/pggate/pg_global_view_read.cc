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

YbcRemotePgExecResult PgGlobalViewRead::ExecScan(
    PgClient& client, std::string_view database_name, std::string_view query,
    std::string_view tserver_uuid) {
  auto res = client.RemoteExec(
      query, database_name, tserver_uuid, params_);
  if (!res.ok()) {
    last_error_ = res.status().ToString();
    return {nullptr, 0, last_error_.c_str()};
  }

  auto& pb = *res->mutable_pg_result();

  if (!pb.error_message().empty()) {
    last_error_ = std::move(*pb.mutable_error_message());
    return {nullptr, 0, last_error_.c_str()};
  }

  if (pb.rows_size() == 0) {
    return {nullptr, 0, nullptr};
  }

  const auto pb_size = pb.ByteSizeLong();
  DCHECK_GT(pb_size, 0) << "Received protobuf size should be positive, got " << pb_size;

  serialized_result_.resize(pb_size);
  auto* buf = serialized_result_.data();
  pb.SerializeWithCachedSizesToArray(buf);
  return {buf, serialized_result_.size(), nullptr};
}

}  // namespace yb::pggate
