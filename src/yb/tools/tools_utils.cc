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

DEFINE_NON_RUNTIME_string(certs_dir_name, "",
    "Directory with certificates to use for secure server connection.");
DECLARE_string(certs_dir);
DEFINE_NON_RUNTIME_string(client_node_name, "", "Client node name.");

namespace yb::tools {

// Return a secure context if needed, otherwise nullptr.
Result<std::unique_ptr<rpc::SecureContext>> CreateSecureContextIfNeeded(
    rpc::MessengerBuilder& messenger_builder) {
  auto certs_dir_name = FLAGS_certs_dir_name;
  if (certs_dir_name.empty()) {
    certs_dir_name = FLAGS_certs_dir;
  }
  if (certs_dir_name.empty()) {
    return nullptr;
  }
  const auto& cert_name = FLAGS_client_node_name;
  auto secure_context = VERIFY_RESULT(rpc::CreateSecureContext(
      certs_dir_name, rpc::UseClientCerts(!cert_name.empty()), cert_name));
  rpc::ApplySecureContext(secure_context.get(), &messenger_builder);
  return secure_context;
}

} // namespace yb::tools
