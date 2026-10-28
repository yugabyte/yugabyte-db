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

#include "yb/tools/tools_utils.h"

#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_NON_RUNTIME_string(certs_dir_name, "",
    "Directory with certificates to use for secure server connection. Falls back to "
    "--certs_dir when empty.");
DEFINE_NON_RUNTIME_string(client_node_name, "",
    "Name of the client certificate to present to the server: node.<name>.crt and "
    "node.<name>.key from the certificates directory. Required when the cluster runs with "
    "--node_to_node_encryption_use_client_certificates. Falls back to --cert_node_filename "
    "when empty.");

DECLARE_string(cert_node_filename);
DECLARE_string(certs_dir);
DECLARE_bool(node_to_node_encryption_use_client_certificates);

namespace yb::tools {

// Return a secure context if needed, otherwise nullptr.
Result<std::unique_ptr<rpc::SecureContext>> CreateSecureContextIfNeeded(
    rpc::MessengerBuilder& messenger_builder, const std::string& certs_dir) {
  // The certificate flags describe the universe this tool was pointed at, so when certs_dir names
  // a second universe neither the directory nor the node name beside it applies. Leaving the node
  // name empty asks only for ca.crt, matching the cross-universe clients in client/xcluster_client
  // and master/xcluster_rpc_tasks, which is why <certs_for_cdc_dir>/<group> holds nothing else.
  auto certs_dir_name = certs_dir;
  std::string cert_name;
  if (certs_dir_name.empty()) {
    certs_dir_name = FLAGS_certs_dir_name;
    if (certs_dir_name.empty()) {
      certs_dir_name = FLAGS_certs_dir;
    }
    // --cert_node_filename and --node_to_node_encryption_use_client_certificates come in when the
    // tool is run with --flagfile pointing at a server's server.conf.
    cert_name = FLAGS_client_node_name;
    if (cert_name.empty()) {
      cert_name = FLAGS_cert_node_filename;
    }
  }
  if (certs_dir_name.empty()) {
    return nullptr;
  }
  // Without a client certificate the server rejects the TLS handshake and the tool only sees a
  // connection timeout, so fail early with an actionable message instead.
  SCHECK(
      !cert_name.empty() || !certs_dir.empty() ||
          !FLAGS_node_to_node_encryption_use_client_certificates,
      InvalidArgument,
      "--node_to_node_encryption_use_client_certificates is set, but no client certificate was "
      "specified. Pass --client_node_name=<name> to use node.<name>.crt and node.<name>.key "
      "from $0", certs_dir_name);
  auto secure_context = VERIFY_RESULT(rpc::CreateSecureContext(
      certs_dir_name, rpc::UseClientCerts(!cert_name.empty()), cert_name));
  rpc::ApplySecureContext(secure_context.get(), &messenger_builder);
  return secure_context;
}

} // namespace yb::tools
