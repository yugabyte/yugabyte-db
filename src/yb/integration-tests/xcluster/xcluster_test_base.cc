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

#include "yb/integration-tests/xcluster/xcluster_test_base.h"

#include <string>

#include "yb/cdc/cdc_service.h"

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/thread.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/util/file_util.h"

using std::string;

DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_int32(replication_factor);
DECLARE_string(certs_for_cdc_dir);
DECLARE_string(certs_dir);

namespace yb {

using client::YBClient;
using client::YBTableName;
using tserver::XClusterConsumer;

Result<std::unique_ptr<XClusterTestBase::Cluster>> XClusterTestBase::CreateCluster(
    const std::string& cluster_id, const std::string& cluster_short_name, uint32_t num_tservers,
    uint32_t num_masters) {
  MiniClusterOptions opts;
  opts.num_tablet_servers = num_tservers;
  opts.num_masters = num_masters;
  opts.cluster_id = cluster_id;
  TEST_SetThreadPrefixScoped prefix_se(cluster_short_name);
  std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
  cluster->mini_cluster_ = std::make_unique<MiniCluster>(opts);
  RETURN_NOT_OK(cluster->mini_cluster_.get()->StartSync());
  cluster->client_ = VERIFY_RESULT(cluster->mini_cluster_.get()->CreateClient());
  return cluster;
}

Status XClusterTestBase::InitClusters(const MiniClusterOptions& opts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = static_cast<int>(opts.num_tablet_servers);
  // Disable tablet split for regular tests, see xcluster-tablet-split-itest for those tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = false;

  auto producer_opts = opts;
  producer_opts.cluster_id = "producer";

  producer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(producer_opts);

  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    RETURN_NOT_OK(producer_cluster()->StartSync());
  }

  auto consumer_opts = opts;
  consumer_opts.cluster_id = "consumer";
  consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(consumer_opts);

  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    RETURN_NOT_OK(consumer_cluster()->StartSync());
  }

  RETURN_NOT_OK(RunOnBothClusters([&opts](MiniCluster* cluster) {
    return cluster->WaitForTabletServerCount(opts.num_tablet_servers);
  }));

  producer_cluster_.client_ = VERIFY_RESULT(producer_cluster()->CreateClient());
  consumer_cluster_.client_ = VERIFY_RESULT(consumer_cluster()->CreateClient());

  return Status::OK();
}

void XClusterTestBase::TearDown() {
  LOG(INFO) << "Destroying CDC Clusters";
  if (consumer_cluster()) {
    TEST_SetThreadPrefixScoped prefix_se("C");
    if (consumer_cluster_.pg_supervisor_) {
      consumer_cluster_.pg_supervisor_->Stop();
    }
    consumer_cluster_.mini_cluster_->Shutdown();
    consumer_cluster_.mini_cluster_.reset();
  }

  if (producer_cluster()) {
    TEST_SetThreadPrefixScoped prefix_se("P");
    if (producer_cluster_.pg_supervisor_) {
      producer_cluster_.pg_supervisor_->Stop();
    }
    producer_cluster_.mini_cluster_->Shutdown();
    producer_cluster_.mini_cluster_.reset();
  }

  producer_cluster_.client_.reset();
  consumer_cluster_.client_.reset();

  YBTest::TearDown();
}

Status XClusterTestBase::RunOnBothClusters(std::function<Status(MiniCluster*)> run_on_cluster) {
  auto producer_future = std::async(std::launch::async, [&] {
    CDSAttacher attacher;
    return run_on_cluster(producer_cluster());
  });
  auto consumer_future = std::async(std::launch::async, [&] {
    CDSAttacher attacher;
    return run_on_cluster(consumer_cluster());
  });

  auto producer_status = producer_future.get();
  auto consumer_status = consumer_future.get();

  RETURN_NOT_OK(producer_status);
  return consumer_status;
}

Status XClusterTestBase::RunOnBothClusters(std::function<Status(Cluster*)> run_on_cluster) {
  auto producer_future = std::async(std::launch::async, [&] {
    CDSAttacher attacher;
    return run_on_cluster(&producer_cluster_);
  });
  auto consumer_future = std::async(std::launch::async, [&] {
    CDSAttacher attacher;
    return run_on_cluster(&consumer_cluster_);
  });

  auto producer_status = producer_future.get();
  auto consumer_status = consumer_future.get();

  RETURN_NOT_OK(producer_status);
  return consumer_status;
}

Status XClusterTestBase::WaitForLoadBalancersToStabilize() {
  RETURN_NOT_OK(
      producer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));
  return consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout));
}

Status XClusterTestBase::WaitForLoadBalancersToStabilize(MiniCluster* cluster) {
  return cluster->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout));
}

Status XClusterTestBase::CreateDatabase(
    Cluster* cluster, const std::string& namespace_name, bool colocated) {
  auto conn = VERIFY_RESULT(cluster->Connect());
  return conn.ExecuteFormat(
      "CREATE DATABASE $0$1", namespace_name, colocated ? " colocated = true" : "");
}

Result<YBTableName> XClusterTestBase::CreateTable(
    YBClient* client, const std::string& namespace_name, const std::string& table_name,
    uint32_t num_tablets, const client::YBSchema* schema) {
  YBTableName table(YQL_DATABASE_CQL, namespace_name, table_name);
  RETURN_NOT_OK(client->CreateNamespaceIfNotExists(table.namespace_name(), table.namespace_type()));

  // Add a table, make sure it reports itself.
  std::unique_ptr<client::YBTableCreator> table_creator(client->NewTableCreator());
  RETURN_NOT_OK(table_creator->table_name(table)
                    .schema(schema)
                    .table_type(client::YBTableType::YQL_TABLE_TYPE)
                    .num_tablets(num_tablets)
                    .Create());
  return table;
}

Status XClusterTestBase::SetupUniverseReplication(const std::vector<string>& producer_table_ids) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId, producer_table_ids);
}

Status XClusterTestBase::SetupUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId, producer_tables,
      bootstrap_ids, opts);
}

Status XClusterTestBase::SetupUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables, SetupReplicationOptions opts) {
  return SetupUniverseReplication(kReplicationGroupId, producer_tables, opts);
}

Status XClusterTestBase::SetupUniverseReplication(
    const cdc::ReplicationGroupId& replication_group_id,
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables, SetupReplicationOptions opts) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), replication_group_id, producer_tables,
      {} /* bootstrap_ids */, opts);
}

Status XClusterTestBase::SetupReverseUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables) {
  return SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kReplicationGroupId, producer_tables);
}

Status XClusterTestBase::SetupUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id,
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) {
  std::vector<string> producer_table_ids;
  for (const auto& table : producer_tables) {
    producer_table_ids.push_back(table->id());
  }

  return SetupUniverseReplication(
      producer_cluster, consumer_cluster, consumer_client, replication_group_id, producer_table_ids,
      bootstrap_ids, opts);
}

Status XClusterTestBase::SetupUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id, const std::vector<TableId>& producer_table_ids,
    const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) {
  // If we have certs for encryption in FLAGS_certs_dir then we need to copy it over to the
  // universe_id subdirectory in FLAGS_certs_for_cdc_dir.
  if (!FLAGS_certs_for_cdc_dir.empty() && !FLAGS_certs_dir.empty()) {
    auto* env = Env::Default();
    if (!env->DirExists(FLAGS_certs_for_cdc_dir)) {
      RETURN_NOT_OK(env->CreateDir(FLAGS_certs_for_cdc_dir));
    }
    const auto universe_sub_dir =
        JoinPathSegments(FLAGS_certs_for_cdc_dir, replication_group_id.ToString());
    RETURN_NOT_OK(CopyDirectory(
        env, FLAGS_certs_dir, universe_sub_dir, UseHardLinks::kFalse, CreateIfMissing::kTrue,
        RecursiveCopy::kFalse));
    LOG(INFO) << "Copied certs from " << FLAGS_certs_dir << " to " << universe_sub_dir;
  }

  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;

  req.set_producer_id(replication_group_id.ToString());
  string master_addr = producer_cluster->GetMasterAddresses();
  if (opts.leader_only) {
    master_addr = VERIFY_RESULT(producer_cluster->GetLeaderMiniMaster())->bound_rpc_addr_str();
  }
  auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(producer_table_ids.size()));
  for (const auto& table_id : producer_table_ids) {
    req.add_producer_table_ids(table_id);
  }

  req.set_transactional(opts.transactional);

  SCHECK(
      bootstrap_ids.empty() || bootstrap_ids.size() == producer_table_ids.size(), InvalidArgument,
      "Bootstrap Ids for all tables should be provided");

  for (const auto& bootstrap_id : bootstrap_ids) {
    req.add_producer_bootstrap_ids(bootstrap_id.ToString());
  }

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client->proxy_cache(),
      VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

  RETURN_NOT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
  {
    // Verify that replication was setup correctly.
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(replication_group_id, &resp));
    SCHECK_EQ(
        resp.entry().producer_id(), replication_group_id, IllegalState,
        "Producer id does not match passed in value");
    SCHECK_EQ(resp.entry().tables_size(), producer_table_ids.size(), IllegalState,
              "Number of tables do not match");
  }
  return Status::OK();
}

Status XClusterTestBase::SetupNSUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id, const std::string& producer_ns_name,
    const YQLDatabase& producer_ns_type, SetupReplicationOptions opts) {
  master::SetupNSUniverseReplicationRequestPB req;
  master::SetupNSUniverseReplicationResponsePB resp;
  req.set_producer_id(replication_group_id.ToString());
  req.set_producer_ns_name(producer_ns_name);
  req.set_producer_ns_type(producer_ns_type);

  std::string master_addr = producer_cluster->GetMasterAddresses();
  if (opts.leader_only) {
    master_addr = VERIFY_RESULT(producer_cluster->GetLeaderMiniMaster())->bound_rpc_addr_str();
  }
  auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client->proxy_cache(),
      VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  return WaitFor([&] () -> Result<bool> {
    if (!master_proxy->SetupNSUniverseReplication(req, &resp, &rpc).ok()) {
      return false;
    } else if (resp.has_error()) {
      return false;
    }
    return true;
  }, MonoDelta::FromSeconds(30), "Setup namespace-level universe replication");
}

Status XClusterTestBase::VerifyUniverseReplication(master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(kReplicationGroupId, resp);
}

Status XClusterTestBase::VerifyUniverseReplication(
    const cdc::ReplicationGroupId& replication_group_id,
    master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(
      consumer_cluster(), consumer_client(), replication_group_id, resp);
}

Status XClusterTestBase::VerifyUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id,
    master::GetUniverseReplicationResponsePB* resp) {
  master::IsSetupUniverseReplicationDoneResponsePB setup_resp;
  RETURN_NOT_OK(WaitForSetupUniverseReplication(
      consumer_cluster, consumer_client, replication_group_id, &setup_resp));
  if (setup_resp.has_replication_error()) {
    RETURN_NOT_OK(StatusFromPB(setup_resp.replication_error()));
  }

  return LoggedWaitFor(
      [=]() -> Result<bool> {
        master::GetUniverseReplicationRequestPB req;
        req.set_producer_id(replication_group_id.ToString());
        resp->Clear();

        auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
            &consumer_client->proxy_cache(),
            VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
        rpc::RpcController rpc;
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

        Status s = master_proxy->GetUniverseReplication(req, resp, &rpc);
        return s.ok() && !resp->has_error() &&
               resp->entry().state() == master::SysUniverseReplicationEntryPB::ACTIVE;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Verify universe replication");
}

Status XClusterTestBase::VerifyNSUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id, int num_expected_table) {
  return LoggedWaitFor([&]() -> Result<bool> {
    master::GetUniverseReplicationResponsePB resp;
    auto s =
        VerifyUniverseReplication(consumer_cluster, consumer_client, replication_group_id, &resp);
    return s.ok() && resp.entry().producer_id() == replication_group_id &&
           resp.entry().is_ns_replication() && resp.entry().tables_size() == num_expected_table;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Verify namespace-level universe replication");
}

Status XClusterTestBase::ToggleUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id, bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;

  req.set_producer_id(replication_group_id.ToString());
  req.set_is_enabled(is_enabled);

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client->proxy_cache(),
      VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->SetUniverseReplicationEnabled(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterTestBase::ChangeXClusterRole(const cdc::XClusterRole role, Cluster* cluster) {
  if (!cluster) {
    cluster = &consumer_cluster_;
  }

  master::ChangeXClusterRoleRequestPB req;
  master::ChangeXClusterRoleResponsePB resp;

  req.set_role(role);

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &cluster->client_->proxy_cache(),
      VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->ChangeXClusterRole(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterTestBase::VerifyUniverseReplicationDeleted(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id, int timeout) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::GetUniverseReplicationRequestPB req;
    master::GetUniverseReplicationResponsePB resp;
    req.set_producer_id(replication_group_id.ToString());

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client->proxy_cache(),
        VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    Status s = master_proxy->GetUniverseReplication(req, &resp, &rpc);
    return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
  }, MonoDelta::FromMilliseconds(timeout), "Verify universe replication deleted");
}

Status XClusterTestBase::WaitForSetupUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const cdc::ReplicationGroupId& replication_group_id,
    master::IsSetupUniverseReplicationDoneResponsePB* resp) {
  return LoggedWaitFor(
      [=]() -> Result<bool> {
        master::IsSetupUniverseReplicationDoneRequestPB req;
        req.set_producer_id(replication_group_id.ToString());
        resp->Clear();

        auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
            &consumer_client->proxy_cache(),
            VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
        rpc::RpcController rpc;
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

        RETURN_NOT_OK(master_proxy->IsSetupUniverseReplicationDone(req, resp, &rpc));
        if (resp->has_error()) {
          return StatusFromPB(resp->error().status());
        }

        return resp->has_done() && resp->done();
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Is setup replication done");
}

Status XClusterTestBase::GetCDCStreamForTable(
    const std::string& table_id, master::ListCDCStreamsResponsePB* resp) {
  return LoggedWaitFor([this, table_id, resp]() -> Result<bool> {
    master::ListCDCStreamsRequestPB req;
    req.set_table_id(table_id);
    resp->Clear();

    auto leader_mini_master = producer_cluster()->GetLeaderMiniMaster();
    if (!leader_mini_master.ok()) {
      return false;
    }
    Status s = (*leader_mini_master)->catalog_manager().ListCDCStreams(&req, resp);
    return s.ok() && !resp->has_error() && resp->streams_size() == 1;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Get CDC stream for table");
}

uint32_t XClusterTestBase::GetSuccessfulWriteOps(MiniCluster* cluster) {
  uint32_t size = 0;
  for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
    auto* tserver = mini_tserver->server();
    XClusterConsumer* xcluster_consumer;
    if (tserver && (xcluster_consumer = tserver->GetXClusterConsumer())) {
      size += xcluster_consumer->GetNumSuccessfulWriteRpcs();
    }
  }
  return size;
}

Status XClusterTestBase::DeleteUniverseReplication(
    const cdc::ReplicationGroupId& replication_group_id) {
  return DeleteUniverseReplication(replication_group_id, consumer_client(), consumer_cluster());
}

Status XClusterTestBase::DeleteUniverseReplication(
    const cdc::ReplicationGroupId& replication_group_id, YBClient* client, MiniCluster* cluster) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;

  req.set_producer_id(replication_group_id.ToString());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &client->proxy_cache(),
      VERIFY_RESULT(cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->DeleteUniverseReplication(req, &resp, &rpc));
  LOG(INFO) << "Delete universe succeeded";
  return Status::OK();
}

Status XClusterTestBase::AlterUniverseReplication(
    const cdc::ReplicationGroupId& replication_group_id,
    const std::vector<std::shared_ptr<client::YBTable>>& tables,
    bool add_tables) {
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  rpc::RpcController rpc;
  alter_req.set_producer_id(replication_group_id.ToString());
  for (const auto& table : tables) {
    if (add_tables) {
      alter_req.add_producer_table_ids_to_add(table->id());
    } else {
      alter_req.add_producer_table_ids_to_remove(table->id());
    }
  }

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
  SCHECK(!alter_resp.has_error(), IllegalState, alter_resp.ShortDebugString());
  master::IsSetupUniverseReplicationDoneResponsePB setup_resp;
  auto alter_replication_group_id = cdc::GetAlterReplicationGroupId(replication_group_id);
  RETURN_NOT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), alter_replication_group_id, &setup_resp));
  SCHECK(!setup_resp.has_error(), IllegalState, alter_resp.ShortDebugString());
  RETURN_NOT_OK(WaitForSetupUniverseReplicationCleanUp(alter_replication_group_id));
  master::GetUniverseReplicationResponsePB get_resp;
  return VerifyUniverseReplication(replication_group_id, &get_resp);
}

Status XClusterTestBase::CorrectlyPollingAllTablets(uint32_t num_producer_tablets) {
  return cdc::CorrectlyPollingAllTablets(
      consumer_cluster(), num_producer_tablets, MonoDelta::FromSeconds(kRpcTimeout));
}

Status XClusterTestBase::CorrectlyPollingAllTablets(
    MiniCluster* cluster, uint32_t num_producer_tablets) {
  return cdc::CorrectlyPollingAllTablets(
      cluster, num_producer_tablets, MonoDelta::FromSeconds(kRpcTimeout));
}

Status XClusterTestBase::WaitForSetupUniverseReplicationCleanUp(
    const cdc::ReplicationGroupId& replication_group_id) {
  auto proxy = std::make_shared<master::MasterReplicationProxy>(
    &consumer_client()->proxy_cache(),
    VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::GetUniverseReplicationRequestPB req;
  master::GetUniverseReplicationResponsePB resp;
  return WaitFor(
      [proxy, &req, &resp, replication_group_id]() -> Result<bool> {
        req.set_producer_id(replication_group_id.ToString());
        rpc::RpcController rpc;
        Status s = proxy->GetUniverseReplication(req, &resp, &rpc);

        return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Waiting for universe to delete");
}

Status XClusterTestBase::WaitForValidSafeTimeOnAllTServers(
    const NamespaceId& namespace_id, Cluster* cluster, boost::optional<CoarseTimePoint> deadline) {
  if (!cluster) {
    cluster = &consumer_cluster_;
  }
  if (!deadline) {
    deadline = PropagationDeadline();
  }
  const auto description = Format("Wait for safe_time of namespace $0 to be valid", namespace_id);
  RETURN_NOT_OK(
      WaitForRoleChangeToPropogateToAllTServers(cdc::XClusterRole::STANDBY, cluster, deadline));
  for (auto& tserver : cluster->mini_cluster_->mini_tablet_servers()) {
    RETURN_NOT_OK(Wait(
        [&]() -> Result<bool> {
          auto safe_time_result =
              tserver->server()->GetXClusterSafeTimeMap().GetSafeTime(namespace_id);
          if (!safe_time_result || !*safe_time_result) {
            return false;
          }
          CHECK(safe_time_result.get()->is_valid());
          return true;
        },
        *deadline,
        description));
  }

  return Status::OK();
}

Status XClusterTestBase::WaitForRoleChangeToPropogateToAllTServers(
    cdc::XClusterRole expected_role, Cluster* cluster, boost::optional<CoarseTimePoint> deadline) {
  if (!cluster) {
    cluster = &consumer_cluster_;
  }
  if (!deadline) {
    deadline = PropagationDeadline();
  }
  const auto description = Format("Wait for cluster to be in $0", XClusterRole_Name(expected_role));
  for (auto& tserver : cluster->mini_cluster_->mini_tablet_servers()) {
    RETURN_NOT_OK(Wait(
        [&]() -> Result<bool> {
          auto xcluster_role = tserver->server()->TEST_GetXClusterRole();
          return xcluster_role && xcluster_role.get() == expected_role;
        },
        *deadline,
        description));
  }

  return Status::OK();
}

Result<std::vector<xrepl::StreamId>> XClusterTestBase::BootstrapProducer(
    MiniCluster* producer_cluster, YBClient* producer_client,
    const std::vector<std::shared_ptr<yb::client::YBTable>>& tables) {
  std::vector<string> table_ids;
  for (const auto& table : tables) {
    table_ids.push_back(table->id());
  }

  return BootstrapProducer(producer_cluster, producer_client, table_ids);
}

Result<std::vector<xrepl::StreamId>> XClusterTestBase::BootstrapProducer(
    MiniCluster* producer_cluster, YBClient* producer_client,
    const std::vector<string>& table_ids) {
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &producer_client->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster->mini_tablet_server(0)->bound_rpc_addr()));
  cdc::BootstrapProducerRequestPB req;
  cdc::BootstrapProducerResponsePB resp;

  for (const auto& table_id : table_ids) {
    req.add_table_ids(table_id);
  }

  rpc::RpcController rpc;
  RETURN_NOT_OK(producer_cdc_proxy->BootstrapProducer(req, &resp, &rpc));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }

  std::vector<xrepl::StreamId> stream_ids;
  for (auto& bootstrap_id : resp.cdc_bootstrap_ids()) {
    stream_ids.emplace_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
  }
  SCHECK_EQ(
      stream_ids.size(), table_ids.size(), IllegalState,
      "Number of bootstrap ids do not match number of tables");

  return stream_ids;
}

Status XClusterTestBase::WaitForReplicationDrain(
    const std::shared_ptr<master::MasterReplicationProxy>& master_proxy,
    const master::WaitForReplicationDrainRequestPB& req,
    int expected_num_nondrained,
    int timeout_secs) {
  master::WaitForReplicationDrainResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(timeout_secs));
  auto s = master_proxy->WaitForReplicationDrain(req, &resp, &rpc);
  return SetupWaitForReplicationDrainStatus(s, resp, expected_num_nondrained);
}

void XClusterTestBase::PopulateWaitForReplicationDrainRequest(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    master::WaitForReplicationDrainRequestPB* req) {
  for (const auto& producer_table : producer_tables) {
    master::ListCDCStreamsResponsePB list_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_table->id(), &list_resp));
    ASSERT_EQ(list_resp.streams_size(), 1);
    ASSERT_EQ(list_resp.streams(0).table_id(0), producer_table->id());
    req->add_stream_ids(list_resp.streams(0).stream_id());
  }
}

Status XClusterTestBase::SetupWaitForReplicationDrainStatus(
    Status api_status,
    const master::WaitForReplicationDrainResponsePB& api_resp,
    int expected_num_nondrained) {
  if (!api_status.ok()) {
    return api_status;
  }
  if (api_resp.has_error()) {
    return STATUS(IllegalState,
        Format("WaitForReplicationDrain returned error: $0", api_resp.error().DebugString()));
  }
  if (api_resp.undrained_stream_info_size() != expected_num_nondrained) {
    return STATUS(IllegalState,
        Format("Mismatched number of non-drained streams. Expected $0, got $1.",
               expected_num_nondrained, api_resp.undrained_stream_info_size()));
  }
  return Status::OK();
}

void XClusterTestBase::VerifyReplicationError(
    const std::string& consumer_table_id,
    const std::string& stream_id,
    const boost::optional<ReplicationErrorPb>
        expected_replication_error) {
  // 1. Verify that the RPC contains the expected error.
  master::GetReplicationStatusRequestPB req;
  master::GetReplicationStatusResponsePB resp;

  req.set_universe_id(kReplicationGroupId.ToString());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        rpc.Reset();
        rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
        if (!master_proxy->GetReplicationStatus(req, &resp, &rpc).ok()) {
          return false;
        }

        if (resp.has_error()) {
          return false;
        }

        if (resp.statuses_size() == 0 || (resp.statuses()[0].table_id() != consumer_table_id &&
                                          resp.statuses()[0].stream_id() != stream_id)) {
          return false;
        }

        if (expected_replication_error) {
          return resp.statuses()[0].errors_size() == 1 &&
                 resp.statuses()[0].errors()[0].error() == *expected_replication_error;
        } else {
          return resp.statuses()[0].errors_size() == 0;
        }
      },
      MonoDelta::FromSeconds(30), "Waiting for replication error"));

  // 2. Verify that the yb-admin output contains the expected error.
  auto admin_out =
      ASSERT_RESULT(CallAdmin(consumer_cluster(), "get_replication_status", kReplicationGroupId));
  if (expected_replication_error) {
    ASSERT_TRUE(
        admin_out.find(Format("error: $0", ReplicationErrorPb_Name(*expected_replication_error))) !=
        std::string::npos);
  } else {
    ASSERT_TRUE(admin_out.find("error:") == std::string::npos);
  }
}

Result<xrepl::StreamId> XClusterTestBase::GetCDCStreamID(const TableId& producer_table_id) {
  master::ListCDCStreamsResponsePB stream_resp;
  RETURN_NOT_OK(GetCDCStreamForTable(producer_table_id, &stream_resp));

  SCHECK_EQ(
      stream_resp.streams_size(), 1, IllegalState,
      Format("Expected 1 stream, have $0", stream_resp.streams_size()));

  SCHECK_EQ(
      stream_resp.streams(0).table_id().Get(0), producer_table_id, IllegalState,
      Format(
          "Expected table id $0, have $1", producer_table_id,
          stream_resp.streams(0).table_id().Get(0)));

  return xrepl::StreamId::FromString(stream_resp.streams(0).stream_id());
}

Status XClusterTestBase::WaitForSafeTime(
    const NamespaceId& namespace_id, const HybridTime& min_safe_time) {
  for (auto tserver : consumer_cluster()->mini_tablet_servers()) {
    if (!tserver->is_started()) {
      continue;
    }
    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_result =
              tserver->server()->GetXClusterSafeTimeMap().GetSafeTime(namespace_id);
          if (!safe_time_result) {
            CHECK(safe_time_result.status().IsTryAgain());

            return false;
          }
          auto safe_time = safe_time_result.get();
          return safe_time && safe_time->is_valid() && *safe_time > min_safe_time;
        },
        propagation_timeout_,
        Format("Wait for safe_time to move above $0", min_safe_time.ToDebugString())));
  }

  return Status::OK();
}

Status XClusterTestBase::PauseResumeXClusterProducerStreams(
    const std::vector<xrepl::StreamId>& stream_ids, bool is_paused) {
  master::PauseResumeXClusterProducerStreamsRequestPB req;
  master::PauseResumeXClusterProducerStreamsResponsePB resp;

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      VERIFY_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  for (const auto& stream_id : stream_ids) {
    req.add_stream_ids(stream_id.ToString());
  }
  req.set_is_paused(is_paused);
  RETURN_NOT_OK(master_proxy->PauseResumeXClusterProducerStreams(req, &resp, &rpc));
  SCHECK(
      !resp.has_error(), IllegalState,
      Format("PauseResumeXClusterProducerStreams returned error: $0", resp.error().DebugString()));
  return Status::OK();
}

} // namespace yb
