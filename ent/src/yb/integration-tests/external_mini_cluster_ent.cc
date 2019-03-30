// Copyright (c) YugaByte, Inc.

#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/master/master_backup.proxy.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/test_util.h"

DECLARE_string(certs_dir);

namespace yb {

using yb::master::MasterBackupServiceProxy;

std::shared_ptr<MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster) {
  CHECK_EQ(cluster->num_masters(), 1);
  return master_backup_proxy(cluster, 0);
}

std::shared_ptr<MasterBackupServiceProxy> master_backup_proxy(
    ExternalMiniCluster* cluster, int idx) {
  CHECK_LT(idx, cluster->num_masters());
  return std::make_shared<MasterBackupServiceProxy>(
      &cluster->proxy_cache(), CHECK_NOTNULL(cluster->master(idx))->bound_rpc_addr());
}

void StartSecure(
    std::unique_ptr<ExternalMiniCluster>* cluster,
    std::unique_ptr<rpc::SecureContext>* secure_context,
    rpc::MessengerPtr* messenger,
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
  ASSERT_OK((**cluster).Start(*messenger));
}

} // namespace yb
