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

#pragma once

#include <string>

#include <boost/optional.hpp>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_types.h"

#include "yb/client/transaction_manager.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master_replication.fwd.h"

#include "yb/util/string_util.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(TEST_allow_ycql_transactional_xcluster);
DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_int32(cdc_write_rpc_timeout_ms);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_int32(xcluster_safe_time_update_interval_secs);

namespace yb {

using client::YBClient;
using YBTables = std::vector<std::shared_ptr<client::YBTable>>;

constexpr int kRpcTimeout = NonTsanVsTsan(60, 120);
static const std::string kUniverseId = "test_universe";
static const cdc::ReplicationGroupId kReplicationGroupId("test_replication_group");
static const std::string kKeyColumnName = "key";
static const uint32_t kRangePartitionInterval = 500;

template <typename TabletServer>
auto GetSafeTime(const TabletServer* tserver, const NamespaceId& namespace_id) {
  return tserver->GetXClusterSafeTimeMap().GetSafeTime(namespace_id);
}

class XClusterTestBase : public YBTest {
 public:
  std::string namespace_name = "yugabyte";

  XClusterTestBase()
      : producer_tables_(producer_cluster_.tables_), consumer_tables_(consumer_cluster_.tables_) {}

  class Cluster {
   public:
    std::unique_ptr<MiniCluster> mini_cluster_;
    std::unique_ptr<YBClient> client_;
    std::unique_ptr<yb::pgwrapper::PgSupervisor> pg_supervisor_;
    HostPort pg_host_port_;
    boost::optional<client::TransactionManager> txn_mgr_;
    size_t pg_ts_idx_;
    YBTables tables_;

    Result<pgwrapper::PGConn> Connect() {
      return ConnectToDB(std::string() /* dbname */);
    }

    Result<pgwrapper::PGConn> ConnectToDB(
        const std::string& dbname, bool simple_query_protocol = false) {
      return pgwrapper::PGConnBuilder({
        .host = pg_host_port_.host(),
        .port = pg_host_port_.port(),
        .dbname = dbname
      }).Connect(simple_query_protocol);
    }
  };

  YB_STRONGLY_TYPED_BOOL(LeaderOnly);
  YB_STRONGLY_TYPED_BOOL(Transactional);

  struct SetupReplicationOptions {
    SetupReplicationOptions() {}
    SetupReplicationOptions(LeaderOnly leader_only_, Transactional transactional_) {
      leader_only = leader_only_;
      transactional = transactional_;
    }
    LeaderOnly leader_only = LeaderOnly::kTrue;
    // Support consistent transactions for the replication group.
    Transactional transactional = Transactional::kFalse;
  };

  void SetUp() override {
    HybridTime::TEST_SetPrettyToString(true);

    google::SetVLOGLevel("xcluster*", 4);
    google::SetVLOGLevel("cdc*", 4);
    YBTest::SetUp();

    // We normally disable setting up transactional replication for CQL tables because the
    // implementation isn't quite complete yet.  It's fine to use it in tests, however.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_ycql_transactional_xcluster) = true;

    // Allow for one-off network instability by ensuring a single CDC RPC timeout << test timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_read_rpc_timeout_ms) = (kRpcTimeout / 2) * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_write_rpc_timeout_ms) = (kRpcTimeout / 2) * 1000;
    // Not a useful test for us. It's testing Public+Private IP NW errors and we're only public
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_update_interval_secs) = 1;
    propagation_timeout_ = MonoDelta::FromSeconds(30 * kTimeMultiplier);
  }

  Status PostSetUp();

  Result<std::unique_ptr<Cluster>> CreateCluster(
      const std::string& cluster_id, const std::string& cluster_short_name,
      uint32_t num_tservers = 1, uint32_t num_masters = 1);

  virtual Status InitClusters(const MiniClusterOptions& opts);

  void TearDown() override;

  Status RunOnBothClusters(std::function<Status(MiniCluster*)> run_on_cluster);
  Status RunOnBothClusters(std::function<Status(Cluster*)> run_on_cluster);

  Status WaitForLoadBalancersToStabilize();

  Status WaitForLoadBalancerToStabilize(MiniCluster* cluster);

  Status CreateDatabase(
      Cluster* cluster, const std::string& namespace_name, bool colocated = false);

  static Result<client::YBTableName> CreateTable(
      YBClient* client, const std::string& namespace_name, const std::string& table_name,
      uint32_t num_tablets, const client::YBSchema* schema);

  virtual Status SetupUniverseReplication(const std::vector<std::string>& producer_table_ids);

  virtual Status SetupUniverseReplication(
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status SetupUniverseReplication(
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
      const std::vector<xrepl::StreamId>& bootstrap_ids,
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status SetupUniverseReplication(
      const cdc::ReplicationGroupId& replication_group_id,
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status SetupReverseUniverseReplication(
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables);

  Status SetupUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id,
      const std::vector<std::shared_ptr<client::YBTable>>& producer_tables,
      const std::vector<xrepl::StreamId>& bootstrap_ids = {},
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status SetupUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id,
      const std::vector<TableId>& producer_table_ids,
      const std::vector<xrepl::StreamId>& bootstrap_ids = {},
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status SetupNSUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id, const std::string& producer_ns_name,
      const YQLDatabase& producer_ns_type,
      SetupReplicationOptions opts = SetupReplicationOptions());

  Status VerifyUniverseReplication(master::GetUniverseReplicationResponsePB* resp);

  Status VerifyUniverseReplication(
      const cdc::ReplicationGroupId& replication_group_id,
      master::GetUniverseReplicationResponsePB* resp);

  Status VerifyUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id,
      master::GetUniverseReplicationResponsePB* resp);

  Status VerifyNSUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id, int num_expected_table);

  Status ChangeXClusterRole(const cdc::XClusterRole role, Cluster* cluster = nullptr);

  Status ToggleUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id, bool is_enabled);

  Status VerifyUniverseReplicationDeleted(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id, int timeout);

  // Wait for SetupUniverseReplication to complete. resp will contain the errors if any.
  Status WaitForSetupUniverseReplication(
      MiniCluster* consumer_cluster, YBClient* consumer_client,
      const cdc::ReplicationGroupId& replication_group_id,
      master::IsSetupUniverseReplicationDoneResponsePB* resp);

  Status GetCDCStreamForTable(const TableId& table_id, master::ListCDCStreamsResponsePB* resp);

  uint32_t GetSuccessfulWriteOps(MiniCluster* cluster);

  Status DeleteUniverseReplication(
      const cdc::ReplicationGroupId& replication_group_id = kReplicationGroupId);

  Status DeleteUniverseReplication(
      const cdc::ReplicationGroupId& replication_group_id, YBClient* client, MiniCluster* cluster);

  Status AlterUniverseReplication(
      const cdc::ReplicationGroupId& replication_group_id,
      const std::vector<std::shared_ptr<client::YBTable>>& tables,
      bool add_tables);

  Status CorrectlyPollingAllTablets(uint32_t num_producer_tablets);
  Status CorrectlyPollingAllTablets(MiniCluster* cluster, uint32_t num_producer_tablets);

  Status WaitForSetupUniverseReplicationCleanUp(
      const cdc::ReplicationGroupId& replication_group_id);

  Status WaitForValidSafeTimeOnAllTServers(
      const NamespaceId& namespace_id, Cluster* cluster = nullptr,
      boost::optional<CoarseTimePoint> deadline = boost::none);

  Status WaitForRoleChangeToPropogateToAllTServers(
      cdc::XClusterRole expected_xcluster_role, Cluster* cluster = nullptr,
      boost::optional<CoarseTimePoint> deadline = boost::none);

  Result<std::vector<xrepl::StreamId>> BootstrapProducer(
      MiniCluster* producer_cluster, YBClient* producer_client,
      const std::vector<std::shared_ptr<yb::client::YBTable>>& tables, int proxy_tserver_index = 0);

  Result<std::vector<xrepl::StreamId>> BootstrapProducer(
      MiniCluster* producer_cluster, YBClient* producer_client,
      const std::vector<std::string>& table_ids, int proxy_tserver_index = 0);

  // Wait for replication drain on a list of tables.
  Status WaitForReplicationDrain(
      int expected_num_nondrained = 0, int timeout_secs = kRpcTimeout,
      std::optional<uint64> target_time = std::nullopt,
      std::vector<TableId> producer_table_ids = {});

  YBClient* producer_client() {
    return producer_cluster_.client_.get();
  }

  YBClient* consumer_client() {
    return consumer_cluster_.client_.get();
  }

  MiniCluster* producer_cluster() {
    return producer_cluster_.mini_cluster_.get();
  }

  MiniCluster* consumer_cluster() {
    return consumer_cluster_.mini_cluster_.get();
  }

  client::TransactionManager* producer_txn_mgr() {
    return producer_cluster_.txn_mgr_.get_ptr();
  }

  client::TransactionManager* consumer_txn_mgr() {
    return consumer_cluster_.txn_mgr_.get_ptr();
  }

  std::string GetAdminToolPath() {
    const std::string kAdminToolName = "yb-admin";
    return GetToolPath(kAdminToolName);
  }

  template <class... Args>
  Result<std::string> CallAdmin(MiniCluster* cluster, Args&&... args) {
    return CallAdminVec(ToStringVector(
        GetAdminToolPath(), "-master_addresses", cluster->GetMasterAddresses(),
        std::forward<Args>(args)...));
  }

  Result<std::string> CallAdminVec(const std::vector<std::string>& args) {
    std::string result;
    LOG(INFO) << "Execute: " << AsString(args);
    auto status = Subprocess::Call(args, &result, StdFdTypes{StdFdType::kOut, StdFdType::kErr});
    if (!status.ok()) {
      return status.CloneAndAppend(result);
    }
    return result;
  }

  Status WaitForSafeTime(const NamespaceId& namespace_id, const HybridTime& min_safe_time);

  void VerifyReplicationError(
      const std::string& consumer_table_id,
      const std::string& stream_id,
      const boost::optional<ReplicationErrorPb>
          expected_replication_error);

  Result<xrepl::StreamId> GetCDCStreamID(const TableId& producer_table_id);

  Status PauseResumeXClusterProducerStreams(
      const std::vector<xrepl::StreamId>& stream_ids, bool is_paused);

  Result<TableId> GetColocatedDatabaseParentTableId();

 protected:
  CoarseTimePoint PropagationDeadline() const {
    return CoarseMonoClock::Now() + propagation_timeout_;
  }

  Cluster producer_cluster_;
  Cluster consumer_cluster_;
  MonoDelta propagation_timeout_;
  YBTables &producer_tables_, &consumer_tables_;
  // The first table in producer_tables_ and consumer_tables_ is the default table.
  std::shared_ptr<client::YBTable> producer_table_, consumer_table_;
};

} // namespace yb
