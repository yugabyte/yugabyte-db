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

#include "yb/integration-tests/twodc_test_base.h"

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
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/test_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(enable_load_balancing);
DECLARE_int32(replication_factor);

namespace yb {

using client::YBClient;
using client::YBTableName;
using tserver::enterprise::CDCConsumer;

namespace enterprise {

Status TwoDCTestBase::InitClusters(const MiniClusterOptions& opts) {
  FLAGS_replication_factor = static_cast<int>(opts.num_tablet_servers);
  auto producer_opts = opts;
  producer_opts.cluster_id = "producer";

  producer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(producer_opts);

  auto consumer_opts = opts;
  consumer_opts.cluster_id = "consumer";
  consumer_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(consumer_opts);

  RETURN_NOT_OK(producer_cluster()->StartSync());
  RETURN_NOT_OK(consumer_cluster()->StartSync());

  RETURN_NOT_OK(RunOnBothClusters([&opts](MiniCluster* cluster) {
    return cluster->WaitForTabletServerCount(opts.num_tablet_servers);
  }));

  producer_cluster_.client_ = VERIFY_RESULT(producer_cluster()->CreateClient());
  consumer_cluster_.client_ = VERIFY_RESULT(consumer_cluster()->CreateClient());

  return Status::OK();
}

void TwoDCTestBase::TearDown() {
  LOG(INFO) << "Destroying CDC Clusters";
  if (consumer_cluster()) {
    if (consumer_cluster_.pg_supervisor_) {
      consumer_cluster_.pg_supervisor_->Stop();
    }
    consumer_cluster_.mini_cluster_->Shutdown();
    consumer_cluster_.mini_cluster_.reset();
  }

  if (producer_cluster()) {
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

Status TwoDCTestBase::RunOnBothClusters(std::function<Status(MiniCluster*)> run_on_cluster) {
  auto producer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(producer_cluster()); });
  auto consumer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(consumer_cluster()); });

  auto producer_status = producer_future.get();
  auto consumer_status = consumer_future.get();

  RETURN_NOT_OK(producer_status);
  return consumer_status;
}

Status TwoDCTestBase::RunOnBothClusters(std::function<Status(Cluster*)> run_on_cluster) {
  auto producer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(&producer_cluster_); });
  auto consumer_future =
      std::async(std::launch::async, [&] { return run_on_cluster(&consumer_cluster_); });

  auto producer_status = producer_future.get();
  auto consumer_status = consumer_future.get();

  RETURN_NOT_OK(producer_status);
  return consumer_status;
}

Result<YBTableName> TwoDCTestBase::CreateTable(
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

Status TwoDCTestBase::SetupUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& tables, bool leader_only) {
  return SetupUniverseReplication(kUniverseId, tables, leader_only);
}

Status TwoDCTestBase::SetupUniverseReplication(
    const std::string& universe_id, const std::vector<std::shared_ptr<client::YBTable>>& tables,
    bool leader_only) {
  return SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), universe_id, tables, leader_only);
}

Status TwoDCTestBase::SetupReverseUniverseReplication(
    const std::vector<std::shared_ptr<client::YBTable>>& tables) {
  return SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kUniverseId, tables);
}

Status TwoDCTestBase::SetupUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const std::string& universe_id, const std::vector<std::shared_ptr<client::YBTable>>& tables,
    bool leader_only) {
  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;

  req.set_producer_id(universe_id);
  string master_addr = producer_cluster->GetMasterAddresses();
  if (leader_only) {
    master_addr = VERIFY_RESULT(producer_cluster->GetLeaderMiniMaster())->bound_rpc_addr_str();
  }
  auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
  HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(tables.size()));
  for (const auto& table : tables) {
    req.add_producer_table_ids(table->id());
  }

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &consumer_client->proxy_cache(),
      VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  return WaitFor([&] () -> Result<bool> {
    if (!master_proxy->SetupUniverseReplication(req, &resp, &rpc).ok()) {
      return false;
    }
    if (resp.has_error()) {
      return false;
    }
    return true;
  }, MonoDelta::FromSeconds(30), "Setup universe replication");
}

Status TwoDCTestBase::SetupNSUniverseReplication(
    MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
    const std::string& universe_id, const std::string& producer_ns_name,
    const YQLDatabase& producer_ns_type,
    bool leader_only) {
  master::SetupNSUniverseReplicationRequestPB req;
  master::SetupNSUniverseReplicationResponsePB resp;
  req.set_producer_id(universe_id);
  req.set_producer_ns_name(producer_ns_name);
  req.set_producer_ns_type(producer_ns_type);

  std::string master_addr = producer_cluster->GetMasterAddresses();
  if (leader_only) {
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

Status TwoDCTestBase::VerifyUniverseReplication(master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(kUniverseId, resp);
}

Status TwoDCTestBase::VerifyUniverseReplication(
    const std::string& universe_id, master::GetUniverseReplicationResponsePB* resp) {
  return VerifyUniverseReplication(consumer_cluster(), consumer_client(), universe_id, resp);
}

Status TwoDCTestBase::VerifyUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const std::string& universe_id, master::GetUniverseReplicationResponsePB* resp) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::GetUniverseReplicationRequestPB req;
    req.set_producer_id(universe_id);
    resp->Clear();

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client->proxy_cache(),
        VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    Status s = master_proxy->GetUniverseReplication(req, resp, &rpc);
    return s.ok() && !resp->has_error() &&
            resp->entry().state() == master::SysUniverseReplicationEntryPB::ACTIVE;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Verify universe replication");
}

Status TwoDCTestBase::VerifyNSUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, int num_expected_table) {
  return LoggedWaitFor([&]() -> Result<bool> {
    master::GetUniverseReplicationResponsePB resp;
    auto s = VerifyUniverseReplication(consumer_cluster, consumer_client, universe_id, &resp);
    return s.ok() &&
        resp.entry().producer_id() == universe_id &&
        resp.entry().is_ns_replication() &&
        resp.entry().tables_size() == num_expected_table;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Verify namespace-level universe replication");
}

Status TwoDCTestBase::ToggleUniverseReplication(
    MiniCluster* consumer_cluster, YBClient* consumer_client,
    const std::string& universe_id, bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;

  req.set_producer_id(universe_id);
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

Status TwoDCTestBase::VerifyUniverseReplicationDeleted(MiniCluster* consumer_cluster,
    YBClient* consumer_client, const std::string& universe_id, int timeout) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::GetUniverseReplicationRequestPB req;
    master::GetUniverseReplicationResponsePB resp;
    req.set_producer_id(universe_id);

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client->proxy_cache(),
        VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    Status s = master_proxy->GetUniverseReplication(req, &resp, &rpc);
    return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
  }, MonoDelta::FromMilliseconds(timeout), "Verify universe replication deleted");
}

Status TwoDCTestBase::VerifyUniverseReplicationFailed(MiniCluster* consumer_cluster,
    YBClient* consumer_client, const std::string& producer_id,
    master::IsSetupUniverseReplicationDoneResponsePB* resp) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::IsSetupUniverseReplicationDoneRequestPB req;
    req.set_producer_id(producer_id);
    resp->Clear();

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client->proxy_cache(),
        VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    Status s = master_proxy->IsSetupUniverseReplicationDone(req, resp, &rpc);

    if (!s.ok() || resp->has_error()) {
      LOG(WARNING) << "Encountered error while waiting for setup_universe_replication to complete: "
                   << (!s.ok() ? s.ToString() : "resp=" + resp->error().status().message());
    }
    return resp->has_done() && resp->done();
  }, MonoDelta::FromSeconds(kRpcTimeout), "Verify universe replication failed");
}

Status TwoDCTestBase::IsSetupUniverseReplicationDone(MiniCluster* consumer_cluster,
  YBClient* consumer_client, const std::string& universe_id,
    master::IsSetupUniverseReplicationDoneResponsePB* resp) {
  return LoggedWaitFor([=]() -> Result<bool> {
    master::IsSetupUniverseReplicationDoneRequestPB req;
    req.set_producer_id(universe_id);
    resp->Clear();

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client->proxy_cache(),
        VERIFY_RESULT(consumer_cluster->GetLeaderMiniMaster())->bound_rpc_addr());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    Status s = master_proxy->IsSetupUniverseReplicationDone(req, resp, &rpc);
    return s.ok() && resp->has_done() && resp->done();
  }, MonoDelta::FromSeconds(kRpcTimeout), "Is setup replication done");
}

Status TwoDCTestBase::GetCDCStreamForTable(
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

uint32_t TwoDCTestBase::GetSuccessfulWriteOps(MiniCluster* cluster) {
  uint32_t size = 0;
  for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
    auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(mini_tserver->server());
    CDCConsumer* cdc_consumer;
    if (tserver && (cdc_consumer = tserver->GetCDCConsumer())) {
      size += cdc_consumer->GetNumSuccessfulWriteRpcs();
    }
  }
  return size;
}

Status TwoDCTestBase::DeleteUniverseReplication(const std::string& universe_id) {
  return DeleteUniverseReplication(universe_id, consumer_client(), consumer_cluster());
}

Status TwoDCTestBase::DeleteUniverseReplication(
    const std::string& universe_id, YBClient* client, MiniCluster* cluster) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;

  req.set_producer_id(universe_id);

  auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
      &client->proxy_cache(),
      VERIFY_RESULT(cluster->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy->DeleteUniverseReplication(req, &resp, &rpc));
  LOG(INFO) << "Delete universe succeeded";
  return Status::OK();
}

Status TwoDCTestBase::CorrectlyPollingAllTablets(
    MiniCluster* cluster, uint32_t num_producer_tablets) {
  return cdc::CorrectlyPollingAllTablets(
      cluster, num_producer_tablets, MonoDelta::FromSeconds(kRpcTimeout));
}

Status TwoDCTestBase::WaitForSetupUniverseReplicationCleanUp(string producer_uuid) {
  auto proxy = std::make_shared<master::MasterReplicationProxy>(
    &consumer_client()->proxy_cache(),
    VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

  master::GetUniverseReplicationRequestPB req;
  master::GetUniverseReplicationResponsePB resp;
  return WaitFor([proxy, &req, &resp, producer_uuid]() -> Result<bool> {
    req.set_producer_id(producer_uuid);
    rpc::RpcController rpc;
    Status s = proxy->GetUniverseReplication(req, &resp, &rpc);

    return resp.has_error() && resp.error().code() == master::MasterErrorPB::OBJECT_NOT_FOUND;
  }, MonoDelta::FromSeconds(kRpcTimeout), "Waiting for universe to delete");
}

} // namespace enterprise
} // namespace yb
