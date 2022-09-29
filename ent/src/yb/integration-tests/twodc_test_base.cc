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
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/thread.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_int32(replication_factor);
DECLARE_int32(pgsql_proxy_webserver_port);

namespace yb {

using client::YBClient;
using client::YBTableName;
using tserver::enterprise::CDCConsumer;

namespace enterprise {

Status TwoDCTestBase::InitClusters(const MiniClusterOptions& opts) {
  FLAGS_replication_factor = static_cast<int>(opts.num_tablet_servers);
  // Disable tablet split for regular tests, see xcluster-tablet-split-itest for those tests.
  FLAGS_enable_tablet_split_of_xcluster_replicated_tables = false;
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

Status TwoDCTestBase::InitPostgres(Cluster* cluster) {
  RETURN_NOT_OK(WaitForInitDb(cluster->mini_cluster_.get()));
  auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
  auto port = cluster->mini_cluster_->AllocateFreePort();
  yb::pgwrapper::PgProcessConf pg_process_conf =
      VERIFY_RESULT(yb::pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
          yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
          pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
          pg_ts->server()->GetSharedMemoryFd()));
  pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
  pg_process_conf.force_disable_log_file = true;
  FLAGS_pgsql_proxy_webserver_port = cluster->mini_cluster_->AllocateFreePort();

  LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses << ":"
            << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
            << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
  cluster->pg_supervisor_ =
      std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf, nullptr /* tserver */);
  RETURN_NOT_OK(cluster->pg_supervisor_->Start());

  cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
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

Status TwoDCTestBase::RunOnBothClusters(std::function<Status(Cluster*)> run_on_cluster) {
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

Status TwoDCTestBase::WaitForLoadBalancersToStabilize() {
  RETURN_NOT_OK(
      producer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));
  return consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout));
}

Status TwoDCTestBase::CreateDatabase(
    Cluster* cluster, const std::string& namespace_name, bool colocated) {
  auto conn = EXPECT_RESULT(cluster->Connect());
  EXPECT_OK(conn.ExecuteFormat(
      "CREATE DATABASE $0$1", namespace_name, colocated ? " colocated = true" : ""));
  return Status::OK();
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

Result<YBTableName> TwoDCTestBase::CreateYsqlTable(
    Cluster* cluster,
    const std::string& namespace_name,
    const std::string& schema_name,
    const std::string& table_name,
    const boost::optional<std::string>& tablegroup_name,
    uint32_t num_tablets,
    bool colocated,
    const ColocationId colocation_id) {
  auto conn = EXPECT_RESULT(cluster->ConnectToDB(namespace_name));
  std::string colocation_id_string = "";
  if (colocation_id > 0) {
    colocation_id_string = Format("colocation_id = $0", colocation_id);
  }
  if (!schema_name.empty()) {
    EXPECT_OK(conn.Execute(Format("CREATE SCHEMA IF NOT EXISTS $0;", schema_name)));
  }
  std::string full_table_name =
      schema_name.empty() ? table_name : Format("$0.$1", schema_name, table_name);
  std::string query =
      Format("CREATE TABLE $0($1 int PRIMARY KEY) ", full_table_name, kKeyColumnName);
  // One cannot use tablegroup together with split into tablets.
  if (tablegroup_name.has_value()) {
    std::string with_clause =
        colocation_id_string.empty() ? "" : Format("WITH ($0) ", colocation_id_string);
    std::string tablegroup_clause = Format("TABLEGROUP $0", tablegroup_name.value());
    query += Format("$0$1", with_clause, tablegroup_clause);
  } else {
    std::string colocated_clause = Format("colocated = $0", colocated);
    std::string with_clause = colocation_id_string.empty()
                                  ? colocated_clause
                                  : Format("$0, $1", colocation_id_string, colocated_clause);
    query += Format("WITH ($0)", with_clause);
    if (!colocated) {
      query += Format(" SPLIT INTO $0 TABLETS", num_tablets);
    }
  }
  EXPECT_OK(conn.Execute(query));
  return GetYsqlTable(
      cluster, namespace_name, schema_name, table_name, true /* verify_table_name */,
      !schema_name.empty() /* verify_schema_name*/);
}

Status TwoDCTestBase::CreateYsqlTable(
    uint32_t idx, uint32_t num_tablets, Cluster* cluster, std::vector<YBTableName>* table_names,
    const boost::optional<std::string>& tablegroup_name, bool colocated) {
  // Generate colocation_id based on index so that we have the same colocation_id for
  // producer/consumer.
  const int colocation_id = (tablegroup_name.has_value() || colocated) ? (idx + 1) * 111111 : 0;
  auto table = VERIFY_RESULT(CreateYsqlTable(
      cluster, kNamespaceName, "" /* schema_name */, Format("test_table_$0", idx), tablegroup_name,
      num_tablets, colocated, colocation_id));
  table_names->push_back(table);
  return Status::OK();
}

Result<YBTableName> TwoDCTestBase::GetYsqlTable(
    Cluster* cluster,
    const std::string& namespace_name,
    const std::string& schema_name,
    const std::string& table_name,
    bool verify_table_name,
    bool verify_schema_name,
    bool exclude_system_tables) {
  master::ListTablesRequestPB req;
  master::ListTablesResponsePB resp;

  req.set_name_filter(table_name);
  req.mutable_namespace_()->set_name(namespace_name);
  req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  if (!exclude_system_tables) {
    req.set_exclude_system_tables(true);
    req.add_relation_type_filter(master::USER_TABLE_RELATION);
  }

  master::MasterDdlProxy master_proxy(
      &cluster->client_->proxy_cache(),
      VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(master_proxy.ListTables(req, &resp, &rpc));
  if (resp.has_error()) {
    return STATUS(IllegalState, "Failed listing tables");
  }

  // Now need to find the table and return it.
  for (const auto& table : resp.tables()) {
    // If !verify_table_name, just return the first table.
    if (!verify_table_name ||
        (table.name() == table_name && table.namespace_().name() == namespace_name)) {
      // In case of a match, further check for match in schema_name.
      if (!verify_schema_name || (!table.has_pgschema_name() && schema_name.empty()) ||
          (table.has_pgschema_name() && table.pgschema_name() == schema_name)) {
        YBTableName yb_table;
        yb_table.set_table_id(table.id());
        yb_table.set_table_name(table_name);
        yb_table.set_namespace_id(table.namespace_().id());
        yb_table.set_namespace_name(namespace_name);
        yb_table.set_pgschema_name(table.has_pgschema_name() ? table.pgschema_name() : "");
        return yb_table;
      }
    }
  }
  return STATUS(
      IllegalState,
      strings::Substitute("Unable to find table $0 in namespace $1", table_name, namespace_name));
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
