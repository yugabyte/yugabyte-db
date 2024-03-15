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
//

#include "yb/integration-tests/cql_test_base.h"

#include <memory>

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/heartbeater.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/status_log.h"

using std::string;
using std::vector;

namespace yb {

template <class MiniClusterType>
void CqlTestBase<MiniClusterType>::SetupClusterOpt() {
  mini_cluster_opt_.num_masters = num_masters();
  mini_cluster_opt_.num_tablet_servers = num_tablet_servers();
}

template <>
void CqlTestBase<ExternalMiniCluster>::SetUp() {
  YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
  SetupClusterOpt();
  SetUpFlags();
  cluster_.reset(new ExternalMiniCluster(mini_cluster_opt_));
  ASSERT_OK(cluster_->Start());

  ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

  std::vector<std::string> hosts;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    hosts.push_back(cluster_->tablet_server(i)->bind_host());
  }

  driver_ = std::make_unique<CppCassandraDriver>(
      hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kFalse);
}

template <>
std::unique_ptr<cqlserver::CQLServer> CqlTestBase<MiniCluster>::MakeCQLServerForTServer(
    MiniCluster* cluster, int idx, client::YBClient* client, std::string* cql_host,
    uint16_t* cql_port) {
  auto* mini_tserver = cluster->mini_tablet_server(idx);
  auto* tserver = mini_tserver->server();

  const auto& tserver_options = tserver->options();
  cqlserver::CQLServerOptions cql_server_options;
  cql_server_options.fs_opts = tserver_options.fs_opts;
  cql_server_options.master_addresses_flag = tserver_options.master_addresses_flag;
  cql_server_options.SetMasterAddresses(tserver_options.GetMasterAddresses());

  if (*cql_port == 0) {
    *cql_port = cluster->AllocateFreePort();
  }
  if (cql_host->empty()) {
    *cql_host = mini_tserver->bound_rpc_addr().address().to_string();
  }
  cql_server_options.rpc_opts.rpc_bind_addresses = Format("$0:$1", *cql_host, *cql_port);

  return std::make_unique<cqlserver::CQLServer>(
      cql_server_options, &client->messenger()->io_service(), tserver);
}

template <>
Status CqlTestBase<MiniCluster>::StartCQLServer() {
  cql_server_ = MakeCQLServerForTServer(cluster_.get(), 0, client_.get(), &cql_host_, &cql_port_);
  return cql_server_->Start();
}

template <>
void CqlTestBase<MiniCluster>::SetUp() {
  YBMiniClusterTestBase<MiniCluster>::SetUp();
  SetupClusterOpt();
  SetUpFlags();
  cluster_ = std::make_unique<MiniCluster>(mini_cluster_opt_);
  ASSERT_OK(cluster_->Start());
  ASSERT_OK(MiniClusterTestWithClient<MiniCluster>::CreateClient());

  ASSERT_OK(StartCQLServer());

  driver_ = std::make_unique<CppCassandraDriver>(
      std::vector<std::string>{ cql_host_ }, cql_port_, UsePartitionAwareRouting::kTrue);
}

template <>
void CqlTestBase<MiniCluster>::DoTearDown() {
  WARN_NOT_OK(cluster_->mini_tablet_server(0)->server()->heartbeater()->Stop(),
              "Failed to stop heartbeater");
  cql_server_->Shutdown();
  MiniClusterTestWithClient<MiniCluster>::DoTearDown();
}

template <>
void CqlTestBase<ExternalMiniCluster>::DoTearDown() {
  MiniClusterTestWithClient<ExternalMiniCluster>::DoTearDown();
}

template <>
Status CqlTestBase<MiniCluster>::RestartCluster() {
  cql_server_->Shutdown();
  cql_server_.reset();
  RETURN_NOT_OK(cluster_->RestartSync());
  return StartCQLServer();
}

template <>
void CqlTestBase<MiniCluster>::ShutdownCluster() {
  cql_server_->Shutdown();
  cql_server_.reset();
  cluster_->Shutdown();
}

template <>
Status CqlTestBase<MiniCluster>::StartCluster() {
  RETURN_NOT_OK(cluster_->StartSync());
  return StartCQLServer();
}

template <>
Status CqlTestBase<MiniCluster>::RunBackupCommand(const vector<string>& args) {
  if (UseYbController()) {
    return tools::RunYbControllerCommand(cluster_.get(), *tmp_dir_, args);
  }
  return tools::RunBackupCommand(
      HostPort(), // Not used YSQL host/port.
      cluster_->GetMasterAddresses(), cluster_->GetTserverHTTPAddresses(), *tmp_dir_, args);
}

} // namespace yb
