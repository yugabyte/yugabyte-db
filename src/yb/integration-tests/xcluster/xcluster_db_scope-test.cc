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

#include "yb/client/table.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/util/backoff_waiter.h"

DECLARE_bool(TEST_enable_xcluster_api_v2);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDBScopedTest : public XClusterYsqlTestBase {
 public:
  struct SetupParams {
    std::vector<uint32_t> num_consumer_tablets = {3};
    std::vector<uint32_t> num_producer_tablets = {3};
    uint32_t replication_factor = 3;
    uint32_t num_masters = 1;
    bool ranged_partitioned = false;
  };

  XClusterDBScopedTest() = default;
  ~XClusterDBScopedTest() = default;

  virtual void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_xcluster_api_v2) = true;
  }

  Status SetUpClusters() {
    static const SetupParams kDefaultParams;
    return SetUpClusters(kDefaultParams);
  }

  Status SetUpClusters(const SetupParams& params) {
    return XClusterYsqlTestBase::SetUpWithParams(
        params.num_consumer_tablets, params.num_producer_tablets, params.replication_factor,
        params.num_masters, params.ranged_partitioned);
  }

  Result<master::MasterReplicationProxy> GetMasterProxy(Cluster& cluster) {
    return master::MasterReplicationProxy(
        &cluster.client_->proxy_cache(),
        VERIFY_RESULT(cluster.mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr());
  }

  Result<master::MasterReplicationProxy> GetProducerMasterProxy() {
    return GetMasterProxy(producer_cluster_);
  }

  Status CheckpointReplicationGroup() {
    auto producer_namespace_id = VERIFY_RESULT(GetNamespaceId(producer_client()));
    auto namespace_id_out = VERIFY_RESULT(producer_client()->XClusterCreateOutboundReplicationGroup(
        kReplicationGroupId, {namespace_name}));
    SCHECK_EQ(namespace_id_out.size(), 1, IllegalState, "Namespace count does not match");
    SCHECK_EQ(
        namespace_id_out[0], producer_namespace_id, IllegalState, "NamespaceId does not match");

    std::promise<Result<bool>> promise;
    auto future = promise.get_future();
    RETURN_NOT_OK(producer_client()->IsXClusterBootstrapRequired(
        CoarseMonoClock::now() + kTimeout, kReplicationGroupId, producer_namespace_id,
        [&promise](Result<bool> res) { promise.set_value(res); }));
    auto bootstrap_required = VERIFY_RESULT(future.get());
    SCHECK(!bootstrap_required, IllegalState, "Bootstrap should not be required");

    return Status::OK();
  }

  Result<bool> IsCreateXClusterReplicationDone() {
    master::IsCreateXClusterReplicationDoneRequestPB req;
    master::IsCreateXClusterReplicationDoneResponsePB resp;
    req.set_replication_group_id(kReplicationGroupId.ToString());
    auto master_addr = consumer_cluster()->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

    auto master_proxy = VERIFY_RESULT(GetProducerMasterProxy());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    RETURN_NOT_OK(master_proxy.IsCreateXClusterReplicationDone(req, &resp, &rpc));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return resp.done();
  }

  Status WaitForCreateReplicationToFinish() {
    return WaitFor([this]() { return IsCreateXClusterReplicationDone(); }, kTimeout, __func__);
  }

  Status CreateReplicationFromCheckpoint() {
    RETURN_NOT_OK(SetupCertificates(kReplicationGroupId));

    master::CreateXClusterReplicationRequestPB req;
    master::CreateXClusterReplicationResponsePB resp;
    req.set_replication_group_id(kReplicationGroupId.ToString());
    auto master_addr = consumer_cluster()->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

    auto master_proxy = VERIFY_RESULT(GetProducerMasterProxy());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    RETURN_NOT_OK(master_proxy.CreateXClusterReplication(req, &resp, &rpc));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return WaitForCreateReplicationToFinish();
  }
};

TEST_F(XClusterDBScopedTest, TestCreateWithCheckpoint) {
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  ASSERT_OK(InsertRowsInProducer(0, 50));

  ASSERT_OK(VerifyWrittenRecords());
}

}  // namespace yb
