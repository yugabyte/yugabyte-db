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

#include "yb/yql/process_wrapper/common_config.h"

#include "yb/rpc/secure.h"

DECLARE_string(certs_dir);
DECLARE_string(certs_for_client_dir);
DECLARE_string(cert_node_filename);
DECLARE_bool(use_client_to_server_encryption);

namespace yb {

Status ProcessWrapperCommonConfig::SetSslConf(
    const server::ServerBaseOptions& options, FsManager& fs_manager) {
  this->certs_dir =
      FLAGS_certs_dir.empty() ? rpc::GetCertsDir(fs_manager.GetDefaultRootDir()) : FLAGS_certs_dir;
  this->certs_for_client_dir =
      FLAGS_certs_for_client_dir.empty() ? this->certs_dir : FLAGS_certs_for_client_dir;
  this->enable_tls = FLAGS_use_client_to_server_encryption;

  // Follow the same logic as elsewhere, check FLAGS_cert_node_filename then
  // server_broadcast_addresses then rpc_bind_addresses.
  if (!FLAGS_cert_node_filename.empty()) {
    this->cert_base_name = FLAGS_cert_node_filename;
  } else {
    const auto server_broadcast_addresses =
        HostPort::ParseStrings(options.server_broadcast_addresses, 0);
    RETURN_NOT_OK(server_broadcast_addresses);
    const auto rpc_bind_addresses = HostPort::ParseStrings(options.rpc_opts.rpc_bind_addresses, 0);
    RETURN_NOT_OK(rpc_bind_addresses);
    this->cert_base_name = !server_broadcast_addresses->empty()
                               ? server_broadcast_addresses->front().host()
                               : rpc_bind_addresses->front().host();
  }

  return Status::OK();
}

}  // namespace yb
