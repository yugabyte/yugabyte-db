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

#include "yb/client/xcluster_client.h"

#include "yb/client/client.h"
#include "yb/master/master_defaults.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/secure.h"
#include "yb/util/path_util.h"

DECLARE_bool(use_node_to_node_encryption);

namespace yb::client {

XClusterRemoteClient::XClusterRemoteClient(const std::string& certs_for_cdc_dir, MonoDelta timeout)
    : certs_for_cdc_dir_(certs_for_cdc_dir), timeout_(timeout) {}

XClusterRemoteClient::~XClusterRemoteClient() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status XClusterRemoteClient::Init(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& remote_masters) {
  SCHECK(!remote_masters.empty(), InvalidArgument, "No master addresses provided");
  const auto master_addrs = HostPort::ToCommaSeparatedString(remote_masters);

  rpc::MessengerBuilder messenger_builder("xcluster-remote");
  std::string certs_dir;

  if (FLAGS_use_node_to_node_encryption) {
    if (!certs_for_cdc_dir_.empty()) {
      certs_dir = JoinPathSegments(certs_for_cdc_dir_, replication_group_id.ToString());
    }
    secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        certs_dir, /*root_dir=*/"", /*name=*/"", server::SecureContextType::kInternal,
        &messenger_builder));
  }
  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  yb_client_ = VERIFY_RESULT(YBClientBuilder()
                                 .add_master_server_addr(master_addrs)
                                 .default_admin_operation_timeout(timeout_)
                                 .Build(messenger_.get()));

  return Status::OK();
}

Result<UniverseUuid> XClusterRemoteClient::SetupUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& source_master_addresses,
    const std::vector<NamespaceName>& namespace_names, const std::vector<TableId>& source_table_ids,
    const std::vector<xrepl::StreamId>& bootstrap_ids, Transactional transactional) {
  master::SetupUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_transactional(transactional);

  HostPortsToPBs(source_master_addresses, req.mutable_producer_master_addresses());

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(source_table_ids.size()));
  for (const auto& table_id : source_table_ids) {
    req.add_producer_table_ids(table_id);
  }

  for (const auto& bootstrap_id : bootstrap_ids) {
    req.add_producer_bootstrap_ids(bootstrap_id.ToString());
  }

  auto resp = VERIFY_RESULT(yb_client_->SetupUniverseReplication(req));

  if (resp.has_error()) {
    auto status = StatusFromPB(resp.error().status());
    if (!status.ok() && !status.IsAlreadyPresent()) {
      return status;
    }
  }

  // universe_uuid will be set even when IsAlreadyPresent error is returned.
  SCHECK(!resp.universe_uuid().empty(), IllegalState, "Universe UUID is empty");

  return UniverseUuid::FromString(resp.universe_uuid());
}

Result<master::IsOperationDoneResult> XClusterRemoteClient::IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id) {
  master::IsSetupUniverseReplicationDoneRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto resp = VERIFY_RESULT(yb_client_->IsSetupUniverseReplicationDone(req));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  Status status;
  if (resp.has_replication_error()) {
    // IsSetupUniverseReplicationDoneRequestPB will contain an OK status on success.
    status = StatusFromPB(resp.replication_error());
  }

  return master::IsOperationDoneResult(resp.done(), std::move(status));
}

}  // namespace yb::client
