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

#include "yb/integration-tests/xcluster/xcluster_external_mini_cluster_base.h"

#include "yb/client/schema.h"
#include "yb/common/hybrid_time.h"
#include "yb/client/client.h"
#include "yb/util/backoff_waiter.h"

#include "yb/master/master_cluster.proxy.h"

namespace yb {

static constexpr auto kValueColumnName = "value";

static constexpr auto kNamespaceName = "yugabyte";
static constexpr auto kTableName = "my_table";
static const client::YBTableName kYBTableName(YQL_DATABASE_CQL, kNamespaceName, kTableName);

void XClusterExternalMiniClusterBase::SetUp() {
  HybridTime::TEST_SetPrettyToString(true);

  YBTest::SetUp();
}

Status XClusterExternalMiniClusterBase::SetupClusters() {
  AddCommonOptions();

  auto source_master_flags = setup_opts_.master_flags;
  std::move(
      source_cluster_.setup_opts_.master_flags.begin(),
      source_cluster_.setup_opts_.master_flags.end(), std::back_inserter(source_master_flags));
  auto source_tserver_flags = setup_opts_.tserver_flags;
  std::move(
      source_cluster_.setup_opts_.tserver_flags.begin(),
      source_cluster_.setup_opts_.tserver_flags.end(), std::back_inserter(source_tserver_flags));

  source_cluster_ = VERIFY_RESULT(CreateCluster(
      "source", "S", setup_opts_.num_masters, setup_opts_.num_tservers, source_master_flags,
      source_tserver_flags));

  auto target_master_flags = setup_opts_.master_flags;
  std::move(
      target_cluster_.setup_opts_.master_flags.begin(),
      target_cluster_.setup_opts_.master_flags.end(), std::back_inserter(target_master_flags));
  auto target_tserver_flags = setup_opts_.tserver_flags;
  std::move(
      target_cluster_.setup_opts_.tserver_flags.begin(),
      target_cluster_.setup_opts_.tserver_flags.end(), std::back_inserter(target_tserver_flags));

  target_cluster_ = VERIFY_RESULT(CreateCluster(
      "target", "T", setup_opts_.num_masters, setup_opts_.num_tservers, target_master_flags,
      target_tserver_flags));

  RETURN_NOT_OK(RunOnBothClusters([&](Cluster* cluster) -> Status {
    RETURN_NOT_OK(CreateTable(setup_opts_.num_tservers, cluster->client_.get(), kYBTableName));
    std::shared_ptr<client::YBTable> table;
    RETURN_NOT_OK(cluster->client_->OpenTable(kYBTableName, &table));
    cluster->tables_.push_back(std::move(table));
    return Status::OK();
  }));

  return Status::OK();
}

Status XClusterExternalMiniClusterBase::SetupClustersAndReplicationGroup() {
  RETURN_NOT_OK(SetupClusters());
  return SetupReplication();
}

void XClusterExternalMiniClusterBase::AddCommonOptions() {
  // Because this test performs a lot of alter tables, we end up flushing
  // and rewriting metadata files quite a bit. Globally disabling fsync
  // speeds the test runtime up dramatically.
  setup_opts_.tserver_flags.push_back("--never_fsync");

  auto vmodule = "--vmodule=xcluster*=4,xrepl*=4,cdc*=4";
  setup_opts_.master_flags.push_back(vmodule);
  setup_opts_.tserver_flags.push_back(vmodule);
}

Result<XClusterExternalMiniClusterBase::Cluster> XClusterExternalMiniClusterBase::CreateCluster(
    const std::string& cluster_id, const std::string& cluster_short_name, uint32_t num_masters,
    uint32_t num_tservers, const std::vector<std::string>& master_flags,
    const std::vector<std::string>& tserver_flags) {
  ExternalMiniClusterOptions setup_opts_;
  setup_opts_.num_tablet_servers = num_tservers;
  setup_opts_.num_masters = num_masters;
  setup_opts_.cluster_id = cluster_id;

  for (const auto& flag : master_flags) {
    setup_opts_.extra_master_flags.push_back(flag);
  }
  setup_opts_.extra_master_flags.emplace_back(Format("--replication_factor=$0", num_tservers));

  for (const auto& flag : tserver_flags) {
    setup_opts_.extra_tserver_flags.push_back(flag);
  }

  XClusterExternalMiniClusterBase::Cluster cluster;
  cluster.cluster_.reset(new ExternalMiniCluster(setup_opts_));
  RETURN_NOT_OK(cluster.cluster_->Start());

  cluster.client_ = VERIFY_RESULT(cluster.cluster_->CreateClient());
  cluster.master_proxy_ = std::make_unique<master::MasterReplicationProxy>(
      cluster.cluster_->GetLeaderMasterProxy<master::MasterReplicationProxy>());

  return cluster;
}

Status XClusterExternalMiniClusterBase::RunOnBothClusters(
    std::function<Status(Cluster*)> run_on_cluster) {
  auto source_future =
      std::async(std::launch::async, [&] { return run_on_cluster(&source_cluster_); });
  auto target_future =
      std::async(std::launch::async, [&] { return run_on_cluster(&target_cluster_); });

  auto source_status = source_future.get();
  auto target_status = target_future.get();

  RETURN_NOT_OK(source_status);
  return target_status;
}

Result<client::TableHandle> XClusterExternalMiniClusterBase::CreateTable(
    int num_tablets, client::YBClient* client, const client::YBTableName& table_name) {
  client::TableHandle table;
  RETURN_NOT_OK(
      client->CreateNamespaceIfNotExists(table_name.namespace_name(), table_name.namespace_type()));

  client::YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumnName)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumnName)->Type(DataType::INT32);
  TableProperties table_properties;
  table_properties.SetTransactional(true);
  builder.SetTableProperties(table_properties);

  RETURN_NOT_OK(table.Create(table_name, num_tablets, client, &builder));

  return table;
}

Status XClusterExternalMiniClusterBase::SetupReplication(
    cdc::ReplicationGroupId replication_group_id,
    std::vector<std::shared_ptr<client::YBTable>> source_tables) {
  std::string table_ids;
  if (source_tables.empty()) {
    source_tables = SourceTables();
  }
  for (const auto& table : source_tables) {
    if (!table_ids.empty()) {
      table_ids += ",";
    }
    table_ids += table->id();
  }

  auto result = VERIFY_RESULT(RunYbAdmin(
      &target_cluster_, "setup_universe_replication", replication_group_id.ToString(),
      SourceCluster()->GetMasterAddresses(), table_ids));

  SCHECK_NE(
      result.find("Replication setup successfully"), std::string::npos, IllegalState,
      Format("Failed to setup replication: $0", result));

  return Status::OK();
}

Result<xrepl::StreamId> XClusterExternalMiniClusterBase::GetStreamId(client::YBTable* table) {
  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;
  req.set_id_type(yb::master::IdTypePB::TABLE_ID);
  if (!table) {
    table = SourceTable();
  }
  req.set_table_id(table->id());

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(source_cluster_.master_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  SCHECK_EQ(resp.streams_size(), 1, IllegalState, "Expected only one stream");

  return xrepl::StreamId::FromString(resp.streams(0).stream_id());
}

Status XClusterExternalMiniClusterBase::VerifyReplicationError(
    const client::YBTable* consumer_table, const xrepl::StreamId& stream_id,
    const std::optional<ReplicationErrorPb> expected_replication_error) {
  const auto consumer_table_id = consumer_table->id();
  master::GetReplicationStatusRequestPB req;
  master::GetReplicationStatusResponsePB resp;

  req.set_universe_id(kReplicationGroupId.ToString());

  rpc::RpcController rpc;
  std::optional<ReplicationErrorPb> last_error = std::nullopt;
  RETURN_NOT_OK_PREPEND(
      LoggedWaitFor(
          [&]() -> Result<bool> {
            rpc.Reset();
            rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
            if (!target_cluster_.master_proxy_->GetReplicationStatus(req, &resp, &rpc).ok()) {
              return false;
            }

            if (resp.has_error()) {
              return false;
            }

            if (resp.statuses_size() == 0 ||
                (resp.statuses()[0].table_id() != consumer_table_id &&
                 resp.statuses()[0].stream_id() != stream_id.ToString())) {
              return false;
            }

            if (resp.statuses()[0].errors_size() == 1) {
              last_error = resp.statuses()[0].errors()[0].error();
            } else {
              last_error = std::nullopt;
            }

            return expected_replication_error == last_error;
          },
          MonoDelta::FromSeconds(30 * kTimeMultiplier), "Waiting for replication error"),
      Format("Last Replication error: $0,", (last_error ? ToString(*last_error) : "none")));

  return Status::OK();
}

Result<uint32> XClusterExternalMiniClusterBase::PromoteAutoFlags(
    ExternalMiniCluster* cluster, AutoFlagClass flag_class, bool force) {
  auto proxy = cluster->GetLeaderMasterProxy<master::MasterClusterProxy>();

  master::PromoteAutoFlagsRequestPB req;
  master::PromoteAutoFlagsResponsePB resp;
  req.set_max_flag_class(ToString(flag_class));
  req.set_promote_non_runtime_flags(false);
  req.set_force(force);

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
  RETURN_NOT_OK(proxy.PromoteAutoFlags(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return resp.new_config_version();
}

}  // namespace yb
