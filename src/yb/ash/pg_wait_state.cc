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
#include "yb/ash/pg_wait_state.h"

#include <arpa/inet.h>

#include <cstring>

#include "yb/util/status_format.h"

namespace yb::ash {

namespace {

HostPort GetHostPort(const YbcAshMetadata& metadata) {
  if (metadata.addr_family == AF_INET || metadata.addr_family == AF_INET6) {
    char host[INET6_ADDRSTRLEN];
    if (inet_ntop(metadata.addr_family, metadata.client_addr, host, sizeof(host))) {
      return HostPort(host, metadata.client_port);
    }
  }
  return HostPort();
}

#ifndef NDEBUG

template<typename T>
Status VerifyField(const T& actual, const T& expected, const std::string& field_name) {
  SCHECK_EQ(actual, expected, IllegalState, Format("$0 mismatch", field_name));
  return Status::OK();
}

Status VerifyMetadata(const AshMetadata& metadata, const YbcPgAshConfig& config) {
  Uuid root_request_id = Uuid::TryFullyDecode(
      Slice(config.metadata->root_request_id, sizeof(config.metadata->root_request_id)));
  Uuid top_level_node_id = Uuid::TryFullyDecode(
      Slice(config.top_level_node_id, sizeof(config.top_level_node_id)));

  SCHECK(metadata.query_id != 0, IllegalState, "Query ID is zero, which is not expected.");
  RETURN_NOT_OK(VerifyField(metadata.root_request_id, root_request_id, "Root request ID"));
  RETURN_NOT_OK(VerifyField(metadata.top_level_node_id, top_level_node_id, "Top level node ID"));
  RETURN_NOT_OK(VerifyField(metadata.query_id, config.metadata->qp.query_id, "Query ID"));
  RETURN_NOT_OK(VerifyField(metadata.plan_id, config.metadata->qp.plan_id, "Plan ID"));
  RETURN_NOT_OK(VerifyField(metadata.pid, config.metadata->pid, "PID"));
  RETURN_NOT_OK(VerifyField(metadata.database_id, config.metadata->database_id, "Database ID"));
  RETURN_NOT_OK(VerifyField(metadata.user_id, config.metadata->user_id, "User ID"));
  RETURN_NOT_OK(VerifyField(
      metadata.client_host_port, GetHostPort(*config.metadata), "Client host port"));
  RETURN_NOT_OK(VerifyField(metadata.addr_family, config.metadata->addr_family, "Address family"));

  return Status::OK();
}

#endif

} // namespace

PgWaitStateInfo::PgWaitStateInfo(std::reference_wrapper<const YbcPgAshConfig> config)
    : config_(config),
      cached_metadata_({
        .top_level_node_id = Uuid::TryFullyDecode(
            Slice(config.get().top_level_node_id, sizeof(config.get().top_level_node_id))),
        .query_id = config.get().metadata->qp.query_id,
        .plan_id = config.get().metadata->qp.plan_id,
        .pid = config.get().metadata->pid,
        .database_id = config.get().metadata->database_id,
        .user_id = config.get().metadata->user_id,
      }) {
  UpdateClientAddress();
}

AshMetadata PgWaitStateInfo::metadata() {
  cached_metadata_.query_id = config_.metadata->qp.query_id;
  cached_metadata_.plan_id = config_.metadata->qp.plan_id;
  cached_metadata_.root_request_id = Uuid::TryFullyDecode(
      Slice(config_.metadata->root_request_id, sizeof(config_.metadata->root_request_id)));
  cached_metadata_.database_id = config_.metadata->database_id;
  cached_metadata_.user_id = config_.metadata->user_id;
  // With Ysql Connection Manager, the backend serves a different logical client from one request
  // to the next, so the client address can change.
  if (config_.metadata->addr_family != cached_metadata_.addr_family ||
      config_.metadata->client_port != client_port_ ||
      std::memcmp(config_.metadata->client_addr, client_addr_, sizeof(client_addr_)) != 0) {
    UpdateClientAddress();
  }
#ifndef NDEBUG
  const auto s = VerifyMetadata(cached_metadata_, config_);
  LOG_IF(DFATAL, !s.ok()) << "ASH metadata verification failed: " << s;
#endif

  return cached_metadata_;
}

void PgWaitStateInfo::UpdateClientAddress() {
  const auto& metadata = *config_.metadata;
  client_port_ = metadata.client_port;
  std::memcpy(client_addr_, metadata.client_addr, sizeof(client_addr_));
  cached_metadata_.client_host_port = GetHostPort(metadata);
  cached_metadata_.addr_family = metadata.addr_family;
}

} // namespace yb::ash
