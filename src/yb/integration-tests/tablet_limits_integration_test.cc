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

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/tablet_creation_limits.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/size_literals.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

const std::string kCoreLimitFlagName = "tablet_replicas_per_core_limit";
const std::string kMemoryLimitFlagName = "tablet_replicas_per_gib_limit";
const std::string kCpusFlagName = "num_cpus";
const std::string kBlockSplittingFlagName = "split_respects_tablet_replica_limits";
const std::string kErrorMessageFragment = "to exceed the safe system maximum";
}  // namespace

std::string DDLToCreateNTabletTable(const std::string& name, int num_tablets = 1);

Status IsTabletLimitErrorStatus(const Status& status);

using StringAssocVec = std::vector<std::pair<std::string, std::string>>;

class CreateTableLimitTestBase : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() override;

  virtual ExternalMiniClusterOptions CreateMiniClusterOptions() = 0;

  Result<int32_t> GetTServerTabletLiveReplicasCount();

  template <typename DaemonType>
  void UpdateStartupFlags(
      const StringAssocVec& additional_flags,
      const std::vector<DaemonType*>& daemons);

  Status UpdateStartupFlagsAndRestart(StringAssocVec tserver_flags,
                                      StringAssocVec master_flags);

  void UpdateStartupTServerFlags(StringAssocVec additional_flags);

  void UpdateStartupMasterFlags(StringAssocVec additional_flags);

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string());

  Status SplitTablet(const TabletId& tablet_id);

  Result<master::GetTableLocationsResponsePB> GetTableLocations(const TableId& table_id);

  Result<master::ListTablesResponsePB> ListTables(const TableName& table_name);
};

class CreateTableLimitTestRF1 : public CreateTableLimitTestBase {
 public:
  ExternalMiniClusterOptions CreateMiniClusterOptions() override {
    ExternalMiniClusterOptions options;
    options.enable_ysql = true;
    options.num_tablet_servers = 1;
    options.num_masters = 1;
    options.replication_factor = 1;
    options.extra_master_flags = {
        "--enable_load_balancing=false",
        "--initial_tserver_registration_duration_secs=0",
        "--enforce_tablet_replica_limits=true"};
    return options;
  }
};

TEST_F(CreateTableLimitTestRF1, CoreLimit) {
  auto conn = ASSERT_RESULT(PgConnect());
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("warmup")));
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */ {{kCpusFlagName, "1"}}, /* master_flags */ {
          {kCoreLimitFlagName,
           std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))}}));
  const std::string final_table_ddl = DDLToCreateNTabletTable("t");
  conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
  ASSERT_OK(cluster_->SetFlagOnMasters(kCoreLimitFlagName, "0"));
  ASSERT_OK(conn.Execute(final_table_ddl));
}

TEST_F(CreateTableLimitTestRF1, MemoryLimit) {
  auto conn = ASSERT_RESULT(PgConnect("yugabyte"));
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("warmup")));
  // Apply a limit of 1 GiB for tablet overheads on the tserver by setting overall memory to 2 GiB
  // and the percentage of memory for tablet overheads to 50% of the total.
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags*/
      {{"memory_limit_hard_bytes", std::to_string(2_GB)},
       {"tablet_overhead_size_percentage", std::to_string(50)}},
      /* master_flags*/ {
          {kMemoryLimitFlagName,
           std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))}}));
  conn = ASSERT_RESULT(PgConnect("yugabyte"));
  const std::string final_table_ddl = DDLToCreateNTabletTable("t_final");
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
  ASSERT_OK(cluster_->SetFlagOnMasters(kMemoryLimitFlagName, "0"));
  ASSERT_OK(conn.Execute(final_table_ddl));
}

TEST_F(CreateTableLimitTestRF1, MultipleTablets) {
  auto conn = ASSERT_RESULT(PgConnect());
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("warmup")));
  // Set the limit so we can add one more tablet replica.
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */ {{kCpusFlagName, "1"}},
      /* master_flags */ {
          {kCoreLimitFlagName,
           std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()) + 1)}}));
  conn = ASSERT_RESULT(PgConnect());
  // Request 3 tablets, so 3 tablet replicas at RF1. Should fail since we only have room for one
  // more tablet replica.
  ASSERT_OK(
      IsTabletLimitErrorStatus(conn.Execute(DDLToCreateNTabletTable("t", /* num_tablets */ 3))));
  // When creating just a single tablet we should succeed.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("t", 1)));
}

TEST_F(CreateTableLimitTestRF1, DeadTServer) {
  auto conn = ASSERT_RESULT(PgConnect());
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("warmup")));
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */ {{kCpusFlagName, "1"}},
      /* master_flags */ {
          {kCoreLimitFlagName, std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))},
          {"tserver_unresponsive_timeout_ms", std::to_string(3000)}}));
  const std::string final_table_ddl = DDLToCreateNTabletTable("t");
  conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(conn.Execute(final_table_ddl));
  ASSERT_OK(conn.Execute("DROP TABLE t"));
  auto* new_tserver = cluster_->tablet_server(1);
  ASSERT_OK(new_tserver->Pause());
  SleepFor(MonoDelta::FromMilliseconds(4000));
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
  ASSERT_OK(new_tserver->Resume());
}

TEST_F(CreateTableLimitTestRF1, BlacklistTServer) {
  auto conn = ASSERT_RESULT(PgConnect());
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable("warmup")));
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */ {{kCpusFlagName, "1"}},
      /* master_flags */ {
          {kCoreLimitFlagName, std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))},
          {"tserver_unresponsive_timeout_ms", std::to_string(3000)}}));
  const std::string final_table_ddl = DDLToCreateNTabletTable("t");
  conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
  // need to fix this. AddTabletServer.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(conn.Execute(final_table_ddl));
  ASSERT_OK(conn.Execute("DROP TABLE t"));
  ASSERT_OK(cluster_->AddTServerToBlacklist(cluster_->master(0), cluster_->tablet_server(1)));
  ASSERT_OK(IsTabletLimitErrorStatus(conn.Execute(final_table_ddl)));
}

TEST_F(CreateTableLimitTestRF1, BlockTabletSplitting) {
  std::string table_name = "test_table";
  auto conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(conn.Execute(DDLToCreateNTabletTable(table_name)));
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */ {{kCpusFlagName, "1"}},
      /* master_flags */ {
          {kCoreLimitFlagName, std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))},
          {kMemoryLimitFlagName, "0"},
          {kBlockSplittingFlagName, "true"}}));
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());
  auto list_tables_resp = ASSERT_RESULT(ListTables(table_name));
  ASSERT_EQ(list_tables_resp.tables_size(), 1);
  auto table_id = list_tables_resp.tables(0).id();
  auto locations = ASSERT_RESULT(GetTableLocations(table_id));
  ASSERT_GT(locations.tablet_locations_size(), 0);
  auto tablet_id = locations.tablet_locations(0).tablet_id();
  auto status = SplitTablet(tablet_id);
  ASSERT_OK(IsTabletLimitErrorStatus(status));
  ASSERT_OK(cluster_->SetFlagOnMasters(kCoreLimitFlagName, "0"));
  ASSERT_OK(SplitTablet(tablet_id));
}

TEST_F(CreateTableLimitTestRF1, CanAddColocatedTableAtLimit) {
  auto yugabyte_conn = ASSERT_RESULT(PgConnect("yugabyte"));
  ASSERT_OK(yugabyte_conn.Execute("CREATE DATABASE colodb with COLOCATED = true"));
  auto conn = ASSERT_RESULT(PgConnect("colodb"));
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", "warmup")));
  // Configure flags so the tablet replica limit is equal to the number of tablet replicas currently
  // in the universe.
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */
      {{"memory_limit_hard_bytes", std::to_string(2_GB)},
       {"tablet_overhead_size_percentage", std::to_string(50)}},
      /* master_flags */ {
          {kMemoryLimitFlagName,
           std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))}}));
  conn = ASSERT_RESULT(PgConnect("colodb"));
  const std::string final_table_ddl =
      Format("CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", "final_table");
  ASSERT_OK(conn.Execute(final_table_ddl));
}

TEST_F(CreateTableLimitTestRF1, CannotCreateFirstTableInColocatedDatabaseAtLimit) {
  auto yugabyte_conn = ASSERT_RESULT(PgConnect("yugabyte"));
  // Some system tablets are created lazily on first user table creation.
  // Create an initial table before we count tablet limits.
  ASSERT_OK(yugabyte_conn.Execute(DDLToCreateNTabletTable("warmup")));
  // Apply a limit of 1 GiB for tablet overheads on the tserver by setting overall memory to 2 GiB
  // and the percentage of memory for tablet overheads to 50% of the total.
  ASSERT_OK(UpdateStartupFlagsAndRestart(
      /* tserver_flags */
      {{"memory_limit_hard_bytes", std::to_string(2_GB)},
       {"tablet_overhead_size_percentage", std::to_string(50)}},
      /* master_flags */ {
          {kMemoryLimitFlagName,
           std::to_string(ASSERT_RESULT(GetTServerTabletLiveReplicasCount()))}}));

  yugabyte_conn = ASSERT_RESULT(PgConnect("yugabyte"));
  // The tablet backing all colocated tables in a database is not created at database creation time.
  // Instead it is created lazily when the first colocated user table in the database is created.
  // Therefore despite the fact we are at the tablet replica limit the create database DDL should
  // succeed. It is the create table DDL that we expect to fail.
  ASSERT_OK(yugabyte_conn.Execute("CREATE DATABASE colodb with COLOCATED = true"));
  auto conn = ASSERT_RESULT(PgConnect("colodb"));
  auto final_table_ddl = Format("CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", "warmup");
  auto status = conn.Execute(final_table_ddl);
  ASSERT_OK(IsTabletLimitErrorStatus(status));
  ASSERT_OK(cluster_->SetFlagOnMasters(kMemoryLimitFlagName, "0"));
  ASSERT_OK(conn.Execute(final_table_ddl));
}

std::string DDLToCreateNTabletTable(const std::string& name, int num_tablets) {
  return Format("CREATE TABLE $0 (key INT PRIMARY KEY, value INT) SPLIT INTO $1 TABLETS",
                name, num_tablets);
}

Status IsTabletLimitErrorStatus(const Status& status) {
  if (status.ok()) {
    return STATUS(IllegalState, "Is OK status");
  }
  if (status.message().ToBuffer().find(kErrorMessageFragment) == std::string::npos) {
    return STATUS_FORMAT(
        IllegalState, "Status message doesn't contain limit exceed string, instead is: $0",
        status.message().ToBuffer());
  }
  return Status::OK();
}

void CreateTableLimitTestBase::SetUp() {
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    cluster_ = std::make_unique<ExternalMiniCluster>(CreateMiniClusterOptions());
    ASSERT_OK(cluster_->Start());
}

Result<int32_t> CreateTableLimitTestBase::GetTServerTabletLiveReplicasCount() {
    int32_t result = 0;
    for (const auto& tserver : cluster_->tserver_daemons()) {
    auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(tserver);
    tserver::ListTabletsForTabletServerRequestPB req;
    tserver::ListTabletsForTabletServerResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(MonoDelta::FromSeconds(30));
    RETURN_NOT_OK(proxy.ListTabletsForTabletServer(req, &resp, &controller));
    for (const auto& entry : resp.entries()) {
      if (entry.state() == tablet::RaftGroupStatePB::RUNNING ||
          entry.state() == tablet::RaftGroupStatePB::BOOTSTRAPPING) {
        ++result;
      }
    }
    }
    return result;
}

template <typename DaemonType>
void CreateTableLimitTestBase::UpdateStartupFlags(const StringAssocVec& additional_flags,
                                                  const std::vector<DaemonType*>& daemons) {
  for (const auto daemon : daemons) {
    for (const auto& [name, value] : additional_flags) {
      daemon->mutable_flags()->push_back(Format("--$0=$1", name, value));
    }
  }
}

Status CreateTableLimitTestBase::UpdateStartupFlagsAndRestart(
    StringAssocVec tserver_flags, StringAssocVec master_flags) {
  UpdateStartupTServerFlags(tserver_flags);
  UpdateStartupMasterFlags(master_flags);
  cluster_->Shutdown();
  return cluster_->Restart();
}

void CreateTableLimitTestBase::UpdateStartupTServerFlags(StringAssocVec additional_flags) {
  UpdateStartupFlags(additional_flags, cluster_->tserver_daemons());
}

void CreateTableLimitTestBase::UpdateStartupMasterFlags(StringAssocVec additional_flags) {
  UpdateStartupFlags(additional_flags, cluster_->master_daemons());
}

Result<pgwrapper::PGConn> CreateTableLimitTestBase::PgConnect(const std::string& db_name) {
  auto* ts =
      cluster_->tablet_server(RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1));
  return pgwrapper::PGConnBuilder(
             {.host = ts->bind_host(),
              .port = ts->ysql_port(),
              .dbname = db_name})
      .Connect();
}

Status CreateTableLimitTestBase::SplitTablet(const TabletId& tablet_id) {
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  req.set_split_factor(cluster_->GetSplitFactor());
  master::SplitTabletResponsePB resp;
  rpc::RpcController controller;
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  RETURN_NOT_OK(proxy.SplitTablet(req, &resp, &controller));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Result<master::GetTableLocationsResponsePB> CreateTableLimitTestBase::GetTableLocations(
    const TableId& table_id) {
  master::GetTableLocationsRequestPB req;
  req.mutable_table()->set_table_id(table_id);
  req.set_max_returned_locations(1000);
  master::GetTableLocationsResponsePB resp;
  rpc::RpcController controller;
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterClientProxy>();
  RETURN_NOT_OK(proxy.GetTableLocations(req, &resp, &controller));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return resp;
}

Result<master::ListTablesResponsePB> CreateTableLimitTestBase::ListTables(
    const TableName& table_name) {
  master::ListTablesRequestPB req;
  req.set_name_filter(table_name);
  req.set_exclude_system_tables(true);
  master::ListTablesResponsePB resp;
  rpc::RpcController controller;
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
  RETURN_NOT_OK(proxy.ListTables(req, &resp, &controller));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return resp;
}

}  // namespace yb
