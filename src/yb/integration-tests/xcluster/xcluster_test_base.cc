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

#include "yb/common/xcluster_util.h"

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/client/table_creator.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/colocated_util.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/thread.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/util/file_util.h"

using std::string;

DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_int32(replication_factor);
DECLARE_string(certs_for_cdc_dir);
DECLARE_string(certs_dir);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(ysql_legacy_colocated_database_creation);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);

namespace yb {

using client::YBClient;
using client::YBTableName;

namespace {

Result<master::MasterReplicationProxy> GetMasterProxy(XClusterTestBase::Cluster& cluster) {
  return master::MasterReplicationProxy(
      &cluster.client_->proxy_cache(),
      VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
}

}  // namespace

Status XClusterTestBase::PostSetUp() {
  if (!producer_tables_.empty()) {
    producer_table_ = producer_tables_.front();
  }
  if (!consumer_tables_.empty()) {
    consumer_table_ = consumer_tables_.front();
  }

  return WaitForLoadBalancersToStabilize();
}

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

  RETURN_NOT_OK(PreProducerCreate());
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    RETURN_NOT_OK(producer_cluster()->StartAsync());
  }
  RETURN_NOT_OK(PostProducerCreate());

  auto consumer_opts = opts;
  consumer_opts.cluster_id = "consumer";
  consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(consumer_opts);

  RETURN_NOT_OK(PreConsumerCreate());
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    RETURN_NOT_OK(consumer_cluster()->StartAsync());
  }
  RETURN_NOT_OK(PostConsumerCreate());

  RETURN_NOT_OK(RunOnBothClusters([](Cluster* cluster) {
    RETURN_NOT_OK(cluster->mini_cluster_->WaitForAllTabletServers());
    cluster->client_ = VERIFY_RESULT(cluster->mini_cluster_->CreateClient());
    return Status();
  }));

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
  if (!FLAGS_enable_load_balancing) {
    return Status::OK();
  }
  auto se = ScopeExit([catalog_manager_bg_task_wait_ms = FLAGS_catalog_manager_bg_task_wait_ms] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) =
        catalog_manager_bg_task_wait_ms;
  });
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_bg_task_wait_ms) = 10;

  RETURN_NOT_OK(WaitForLoadBalancerToStabilize(producer_cluster()));
  RETURN_NOT_OK(WaitForLoadBalancerToStabilize(consumer_cluster()));
  return Status::OK();
}

Status XClusterTestBase::WaitForLoadBalancerToStabilize(MiniCluster* cluster) {
  return cluster->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout));
}

Status XClusterTestBase::CreateDatabase(
    Cluster* cluster, const std::string& namespace_name, bool colocated) {
  auto conn = VERIFY_RESULT(cluster->Connect());
  return conn.ExecuteFormat(
      "CREATE DATABASE $0$1", namespace_name, colocated ? " COLOCATION = true" : "");
}

Status XClusterTestBase::DropDatabase(Cluster& cluster, const std::string& namespace_name) {
  auto conn = VERIFY_RESULT(cluster.Connect());
  return conn.ExecuteFormat("DROP DATABASE $0", namespace_name);
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
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      producer_table_ids);
}

Status XClusterTestBase::SetupUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId,
      producer_tables, bootstrap_ids, opts);
}

Status XClusterTestBase::SetupUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    SetupReplicationOptions opts) {
  return SetupUniverseReplication(kReplicationGroupId, producer_tables, opts);
}

Status XClusterTestBase::SetupUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
    SetupReplicationOptions opts) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), replication_group_id,
      producer_tables, {} /* bootstrap_ids */, opts);
}

Status XClusterTestBase::SetupReverseUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& producer_tables) {
  return SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kReplicationGroupId,
      producer_tables);
}

Status XClusterTestBase::SetupUniverseReplication() {
  return SetupUniverseReplication(producer_tables_);
}

Status XClusterTestBase::SetupUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const xcluster::ReplicationGroupId& replication_group_id,
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

Status XClusterTestBase::SetupCertificates(
    const xcluster::ReplicationGroupId& replication_group_id) {
  // If we have certs for encryption in FLAGS_certs_dir then we need to copy it over to the
  // replication_group_id subdirectory in FLAGS_certs_for_cdc_dir.
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

  return Status::OK();
}

Status XClusterTestBase::SetupUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<TableId>& producer_table_ids,
    const std::vector<xrepl::StreamId>& bootstrap_ids, SetupReplicationOptions opts) {
  RETURN_NOT_OK(SetupCertificates(replication_group_id));

  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;

  req.set_replication_group_id(replication_group_id.ToString());
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
  // Verify that replication was setup correctly.
  master::GetUniverseReplicationResponsePB verify_resp;
  RETURN_NOT_OK(VerifyUniverseReplication(
      consumer_cluster, consumer_client, replication_group_id, &verify_resp));
  SCHECK_EQ(
      verify_resp.entry().replication_group_id(), replication_group_id, IllegalState,
      "Producer id does not match passed in value");
  SCHECK_EQ(
      verify_resp.entry().tables_size(), producer_table_ids.size(), IllegalState,
      "Number of tables do not match");

  if (opts.transactional && FLAGS_TEST_xcluster_simulated_lag_ms >= 0) {
    std::unordered_set<NamespaceId> namespace_ids;
    for (const auto& [_, consumer_table_id] : verify_resp.entry().validated_tables()) {
      auto table = VERIFY_RESULT(consumer_client->OpenTable(consumer_table_id));
      namespace_ids.insert(table->name().namespace_id());
    }
    for (const auto& namespace_id : namespace_ids) {
      RETURN_NOT_OK(WaitForValidSafeTimeOnAllTServers(namespace_id, *consumer_cluster));
    }
  }
  return Status::OK();
}

Status XClusterTestBase::VerifyUniverseReplication(master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(kReplicationGroupId, resp);
}

Status XClusterTestBase::VerifyUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(
      consumer_cluster(), consumer_client(), replication_group_id, resp);
}

Status XClusterTestBase::VerifyUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const xcluster::ReplicationGroupId& replication_group_id,
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
        req.set_replication_group_id(replication_group_id.ToString());
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

Status XClusterTestBase::ToggleUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;

  req.set_replication_group_id(replication_group_id.ToString());
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

Result<master::GetUniverseReplicationResponsePB> XClusterTestBase::GetUniverseReplicationInfo(
    Cluster& cluster, const xcluster::ReplicationGroupId& replication_group_id) {
  master::GetUniverseReplicationRequestPB req;
  master::GetUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id.ToString());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &cluster.client_->proxy_cache(),
      VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

  RETURN_NOT_OK(master_proxy->GetUniverseReplication(req, &resp, &rpc));
  return resp;
}

Status XClusterTestBase::VerifyUniverseReplicationDeleted(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const xcluster::ReplicationGroupId& replication_group_id, int timeout) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::GetUniverseReplicationRequestPB req;
    master::GetUniverseReplicationResponsePB resp;
    req.set_replication_group_id(replication_group_id.ToString());

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
    const xcluster::ReplicationGroupId& replication_group_id,
    master::IsSetupUniverseReplicationDoneResponsePB* resp) {
  return LoggedWaitFor(
      [=]() -> Result<bool> {
        master::IsSetupUniverseReplicationDoneRequestPB req;
        req.set_replication_group_id(replication_group_id.ToString());
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
    const TableId& table_id, master::ListCDCStreamsResponsePB* resp) {
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
    tserver::XClusterConsumerIf* xcluster_consumer;
    if (tserver && (xcluster_consumer = tserver->GetXClusterConsumer())) {
      size += xcluster_consumer->TEST_GetNumSuccessfulWriteRpcs();
    }
  }
  return size;
}

Status XClusterTestBase::DeleteUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id) {
  return DeleteUniverseReplication(replication_group_id, consumer_client(), consumer_cluster());
}

Status XClusterTestBase::DeleteUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id, YBClient* client,
    MiniCluster* cluster) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;

  req.set_replication_group_id(replication_group_id.ToString());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &client->proxy_cache(),
      VERIFY_RESULT(cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->DeleteUniverseReplication(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  LOG(INFO) << "Delete universe succeeded";

  return Status::OK();
}

Status XClusterTestBase::AlterUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<std::shared_ptr<client::YBTable>>& tables, bool add_tables) {
  master::AlterUniverseReplicationRequestPB alter_req;
  master::AlterUniverseReplicationResponsePB alter_resp;
  rpc::RpcController rpc;
  alter_req.set_replication_group_id(replication_group_id.ToString());
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
  auto alter_replication_group_id = xcluster::GetAlterReplicationGroupId(replication_group_id);
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
    const xcluster::ReplicationGroupId& replication_group_id) {
  auto proxy = std::make_shared<master::MasterReplicationProxy>(
    &consumer_client()->proxy_cache(),
    VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::GetUniverseReplicationRequestPB req;
  master::GetUniverseReplicationResponsePB resp;
  return WaitFor(
      [proxy, &req, &resp, replication_group_id]() -> Result<bool> {
        req.set_replication_group_id(replication_group_id.ToString());
        rpc::RpcController rpc;
        Status s = proxy->GetUniverseReplication(req, &resp, &rpc);

        return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Waiting for universe to delete");
}

Status XClusterTestBase::WaitForValidSafeTimeOnAllTServers(
    const NamespaceId& namespace_id, MiniCluster& cluster,
    boost::optional<CoarseTimePoint> deadline) {
  if (!deadline) {
    deadline = PropagationDeadline();
  }
  const auto description = Format("Wait for safe_time of namespace $0 to be valid", namespace_id);
  for (auto& tserver : cluster.mini_tablet_servers()) {
    RETURN_NOT_OK(Wait(
        [&]() -> Result<bool> {
          auto safe_time_result = tserver->server()->GetXClusterContext().GetSafeTime(namespace_id);
          if (!safe_time_result || !*safe_time_result) {
            return false;
          }
          CHECK(safe_time_result.get()->is_valid());
          return true;
        },
        *deadline, description));
  }

  return Status::OK();
}

Status XClusterTestBase::WaitForValidSafeTimeOnAllTServers(
    const NamespaceId& namespace_id, Cluster* cluster, boost::optional<CoarseTimePoint> deadline) {
  if (!cluster) {
    cluster = &consumer_cluster_;
  }

  return WaitForValidSafeTimeOnAllTServers(namespace_id, *cluster->mini_cluster_.get(), deadline);
}

Status XClusterTestBase::WaitForReadOnlyModeOnAllTServers(
    const NamespaceId& namespace_id, bool is_read_only, Cluster* cluster,
    boost::optional<CoarseTimePoint> deadline) {
  if (!cluster) {
    cluster = &consumer_cluster_;
  }
  if (!deadline) {
    deadline = PropagationDeadline();
  }
  const auto description =
      Format("Wait for is_read_only of namespace $0 to reach $1", namespace_id, is_read_only);
  for (auto& tserver : cluster->mini_cluster_->mini_tablet_servers()) {
    RETURN_NOT_OK(Wait(
        [&]() -> Result<bool> {
          return tserver->server()->GetXClusterContext().IsReadOnlyMode(namespace_id) ==
                 is_read_only;
        },
        *deadline, description));
  }

  return Status::OK();
}

Result<std::vector<xrepl::StreamId>> XClusterTestBase::BootstrapProducer(
    MiniCluster* producer_cluster, YBClient* producer_client,
    const std::vector<std::shared_ptr<yb::client::YBTable>>& tables, int proxy_tserver_index) {
  std::vector<string> table_ids;
  for (const auto& table : tables) {
    table_ids.push_back(table->id());
  }

  return BootstrapProducer(producer_cluster, producer_client, table_ids, proxy_tserver_index);
}

Result<std::vector<xrepl::StreamId>> XClusterTestBase::BootstrapProducer(
    MiniCluster* producer_cluster, YBClient* producer_client, const std::vector<string>& table_ids,
    int proxy_tserver_index) {
  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &producer_client->proxy_cache(),
      HostPort::FromBoundEndpoint(
          producer_cluster->mini_tablet_server(proxy_tserver_index)->bound_rpc_addr()));
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
    int expected_num_nondrained, int timeout_secs, std::optional<uint64> target_time,
    std::vector<TableId> producer_table_ids) {
  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &producer_client()->proxy_cache(),
      VERIFY_RESULT(producer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::WaitForReplicationDrainRequestPB req;
  master::WaitForReplicationDrainResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(timeout_secs));

  if (producer_table_ids.empty()) {
    for (const auto& producer_table : producer_tables_) {
      producer_table_ids.push_back(producer_table->id());
    }
  }

  for (const auto& table_id : producer_table_ids) {
    master::ListCDCStreamsResponsePB list_resp;
    RETURN_NOT_OK(GetCDCStreamForTable(table_id, &list_resp));
    SCHECK_EQ(
        list_resp.streams_size(), 1, IllegalState,
        Format(
            "Producer table $0 has $1 streams which is unexpected", table_id,
            list_resp.streams_size()));
    SCHECK_EQ(
        list_resp.streams(0).table_id(0), table_id, IllegalState,
        "Producer table id does not match");
    req.add_stream_ids(list_resp.streams(0).stream_id());
  }

  if (target_time) {
    req.set_target_time(*target_time);
  }

  RETURN_NOT_OK(master_proxy->WaitForReplicationDrain(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.undrained_stream_info_size() != expected_num_nondrained) {
    return STATUS(
        IllegalState, Format(
                          "Mismatched number of non-drained streams. Expected $0, got $1.",
                          expected_num_nondrained, resp.undrained_stream_info_size()));
  }

  return Status::OK();
}

Status XClusterTestBase::VerifyReplicationError(
    const std::string& consumer_table_id, const xrepl::StreamId& stream_id,
    const std::optional<ReplicationErrorPb> expected_replication_error, int timeout_secs) {
  // 1. Verify that the RPC contains the expected error.
  master::GetReplicationStatusRequestPB req;
  master::GetReplicationStatusResponsePB resp;

  req.set_replication_group_id(kReplicationGroupId.ToString());

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client()->proxy_cache(),
      VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  RETURN_NOT_OK(WaitFor(
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
                                          resp.statuses()[0].stream_id() != stream_id.ToString())) {
          return false;
        }

        if (expected_replication_error) {
          return resp.statuses()[0].errors_size() == 1 &&
                 resp.statuses()[0].errors()[0].error() == *expected_replication_error;
        } else {
          return resp.statuses()[0].errors_size() == 0;
        }
      },
      MonoDelta::FromSeconds(timeout_secs), "Waiting for replication error"));

  // 2. Verify that the yb-admin output contains the expected error.
  auto admin_out =
      VERIFY_RESULT(CallAdmin(consumer_cluster(), "get_replication_status", kReplicationGroupId));
  if (expected_replication_error) {
    SCHECK_FORMAT(
        admin_out.find(Format("error: $0", ReplicationErrorPb_Name(*expected_replication_error))) !=
            std::string::npos,
        IllegalState, "Expected error '$0' not found in yb-admin output '$1'",
        expected_replication_error, admin_out);
  } else {
    SCHECK_FORMAT(
        admin_out.find("error:") == std::string::npos, IllegalState,
        "Unexpected error found in yb-admin output '$0'", admin_out);
  }

  return Status::OK();
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
          auto safe_time_result = tserver->server()->GetXClusterContext().GetSafeTime(namespace_id);
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

Status XClusterTestBase::WaitForSafeTimeToAdvanceToNow() {
  auto producer_master = VERIFY_RESULT(producer_cluster()->GetLeaderMiniMaster())->master();
  HybridTime now = producer_master->clock()->Now();
  for (auto ts : producer_cluster()->mini_tablet_servers()) {
    now.MakeAtLeast(ts->server()->clock()->Now());
  }

  master::GetNamespaceInfoResponsePB resp;
  RETURN_NOT_OK(consumer_client()->GetNamespaceInfo(
      std::string() /* namespace_id */, namespace_name, YQL_DATABASE_PGSQL, &resp));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  auto namespace_id = resp.namespace_().id();

  return WaitForSafeTime(namespace_id, now);
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

namespace {

/*
 * TODO (#11597): Given one is not able to get tablegroup ID by name, currently this works by
 * getting the first available tablegroup appearing in the namespace.
 */
Result<TableId> GetTablegroupParentTable(
    XClusterTestBase::Cluster* cluster, const std::string& namespace_name) {
  // Lookup the namespace id from the namespace name.
  std::string namespace_id;
  // Whether the database named namespace_name is a colocated database.
  bool colocated_database;
  master::MasterDdlProxy master_proxy(
      &cluster->client_->proxy_cache(),
      VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  {
    master::ListNamespacesRequestPB req;
    master::ListNamespacesResponsePB resp;

    RETURN_NOT_OK(master_proxy.ListNamespaces(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed to get namespace info");
    }

    // Find and return the namespace id.
    bool namespaceFound = false;
    for (const auto& entry : resp.namespaces()) {
      if (entry.name() == namespace_name) {
        namespaceFound = true;
        namespace_id = entry.id();
        break;
      }
    }

    if (!namespaceFound) {
      return STATUS(IllegalState, "Failed to find namespace");
    }
  }

  {
    master::GetNamespaceInfoRequestPB req;
    master::GetNamespaceInfoResponsePB resp;

    req.mutable_namespace_()->set_id(namespace_id);

    rpc.Reset();
    RETURN_NOT_OK(master_proxy.GetNamespaceInfo(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed to get namespace info");
    }
    colocated_database = resp.colocated();
  }

  master::ListTablegroupsRequestPB req;
  master::ListTablegroupsResponsePB resp;

  req.set_namespace_id(namespace_id);
  rpc.Reset();
  RETURN_NOT_OK(master_proxy.ListTablegroups(req, &resp, &rpc));
  if (resp.has_error()) {
    return STATUS(IllegalState, "Failed listing tablegroups");
  }

  // Find and return the tablegroup.
  if (resp.tablegroups().empty()) {
    return STATUS(
        IllegalState, Format("Unable to find tablegroup in namespace $0", namespace_name));
  }

  if (colocated_database) return GetColocationParentTableId(resp.tablegroups()[0].id());
  return GetTablegroupParentTableId(resp.tablegroups()[0].id());
}

}  // namespace

Result<TableId> XClusterTestBase::GetColocatedDatabaseParentTableId() {
  if (FLAGS_ysql_legacy_colocated_database_creation) {
    // Legacy colocated database
    master::GetNamespaceInfoResponsePB ns_resp;
    RETURN_NOT_OK(
        producer_client()->GetNamespaceInfo("", namespace_name, YQL_DATABASE_PGSQL, &ns_resp));
    return GetColocatedDbParentTableId(ns_resp.namespace_().id());
  }
  // Colocated database
  return GetTablegroupParentTable(&producer_cluster_, namespace_name);
}

Result<master::MasterReplicationProxy> XClusterTestBase::GetProducerMasterProxy() {
  return GetMasterProxy(producer_cluster_);
}

Status XClusterTestBase::ClearFailedUniverse(Cluster& cluster) {
  auto& catalog_manager =
      VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  return catalog_manager.ClearFailedUniverse(catalog_manager.GetLeaderEpochInternal());
}

} // namespace yb
