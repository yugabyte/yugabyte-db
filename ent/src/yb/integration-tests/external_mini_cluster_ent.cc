// Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/test_util.h"

DECLARE_string(certs_dir);

namespace yb {

void StartSecure(
    std::unique_ptr<ExternalMiniCluster>* cluster,
    std::unique_ptr<rpc::SecureContext>* secure_context,
    std::unique_ptr<rpc::Messenger>* messenger,
    const std::vector<std::string>& master_flags) {
  rpc::MessengerBuilder messenger_builder("test_client");
  *secure_context = ASSERT_RESULT(server::SetupSecureContext(
      "", "", server::SecureContextType::kClientToServer, &messenger_builder));
  *messenger = ASSERT_RESULT(messenger_builder.Build());
  (**messenger).TEST_SetOutboundIpBase(ASSERT_RESULT(HostToAddress("127.0.0.1")));

  ExternalMiniClusterOptions opts;
  opts.extra_tserver_flags = {
      "--use_node_to_node_encryption=true", "--allow_insecure_connections=false",
      "--certs_dir=" + FLAGS_certs_dir};
  opts.extra_master_flags = opts.extra_tserver_flags;
  opts.extra_master_flags.insert(
      opts.extra_master_flags.end(), master_flags.begin(), master_flags.end());
  opts.num_tablet_servers = 3;
  opts.use_even_ips = true;
  *cluster = std::make_unique<ExternalMiniCluster>(opts);
  ASSERT_OK((**cluster).Start(messenger->get()));
}

} // namespace yb
