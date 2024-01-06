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

#include "yb/client/table.h"
#include "yb/common/wire_protocol.h"
#include "yb/integration-tests/xcluster/xcluster_external_mini_cluster_base.h"
#include "yb/integration-tests/xcluster/xcluster_ycql_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/mini_master.h"
#include "yb/util/flags/auto_flags_util.h"

DECLARE_int32(replication_factor);
DECLARE_int32(limit_auto_flag_promote_for_new_universe);
DECLARE_bool(enable_xcluster_auto_flag_validation);
DECLARE_uint32(auto_flags_apply_delay_ms);
DECLARE_uint32(replication_failure_delay_exponent);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 20s * kTimeMultiplier;
constexpr auto kBatchSize = 10;
constexpr auto kMasterProcessName = "yb-master";
constexpr auto kExternalAutoFlagName = "enable_tablet_split_of_xcluster_replicated_tables";

class XClusterUpgradeTest : public XClusterYcqlTestBase {
 public:
  virtual Status SetUpWithParams() override {
    RETURN_NOT_OK(XClusterYcqlTestBase::SetUpWithParams());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_auto_flag_validation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_failure_delay_exponent) = 10;  // 1s max backoff.
    return Status::OK();
  }

  virtual Status PreProducerCreate() override {
    if (start_producer_without_auto_flags_) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_limit_auto_flag_promote_for_new_universe) = 0;
    } else {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_limit_auto_flag_promote_for_new_universe) =
          to_underlying(AutoFlagClass::kExternal);
    }
    return Status::OK();
  }

  virtual Status PreConsumerCreate() override {
    if (start_consumer_without_auto_flags_) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_limit_auto_flag_promote_for_new_universe) = 0;
    } else {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_limit_auto_flag_promote_for_new_universe) =
          to_underlying(AutoFlagClass::kExternal);
    }
    return Status::OK();
  }

  Result<master::PromoteAutoFlagsResponsePB> PromoteAutoFlags(
      MiniCluster& cluster, AutoFlagClass flag_class = AutoFlagClass::kExternal,
      bool force = false) {
    master::PromoteAutoFlagsRequestPB req;
    req.set_max_flag_class(ToString(flag_class));
    req.set_promote_non_runtime_flags(false);
    req.set_force(force);
    auto leader_master = VERIFY_RESULT(cluster.GetLeaderMiniMaster())->master();

    master::PromoteAutoFlagsResponsePB resp;
    RETURN_NOT_OK(leader_master->catalog_manager_impl()->PromoteAutoFlags(&req, &resp));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp;
  }

  Status RollbackAutoFlags(MiniCluster& cluster, uint32_t rollback_config_version) {
    master::RollbackAutoFlagsRequestPB req;
    req.set_rollback_version(rollback_config_version);
    auto leader_master = VERIFY_RESULT(cluster.GetLeaderMiniMaster())->master();

    master::RollbackAutoFlagsResponsePB resp;
    RETURN_NOT_OK(leader_master->catalog_manager_impl()->RollbackAutoFlags(&req, &resp));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status DemoteSingleAutoFlag(
      MiniCluster& cluster, const std::string& process_name, const std::string& flag_name) {
    master::DemoteSingleAutoFlagRequestPB req;
    req.set_process_name(process_name);
    req.set_auto_flag_name(flag_name);

    master::DemoteSingleAutoFlagResponsePB resp;
    auto leader_master = VERIFY_RESULT(cluster.GetLeaderMiniMaster())->master();
    RETURN_NOT_OK(leader_master->catalog_manager_impl()->DemoteSingleAutoFlag(&req, &resp));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    SCHECK(resp.flag_demoted(), InvalidArgument, "Flag not demoted");

    return Status::OK();
  }

  Status RollbackUniverseTest(Cluster& cluster_to_rollback) {
    start_consumer_without_auto_flags_ = true;
    start_producer_without_auto_flags_ = true;

    RETURN_NOT_OK(SetUpWithParams());
    RETURN_NOT_OK(SetupUniverseReplication());
    RETURN_NOT_OK(InsertRowsInProducer(0, kBatchSize));
    RETURN_NOT_OK(VerifyRowsMatch());

    auto stream_id = VERIFY_RESULT(GetCDCStreamID(producer_table_->id()));
    auto& consumer_table_id = consumer_table_->id();

    auto& mini_cluster = *cluster_to_rollback.mini_cluster_;
    auto resp = VERIFY_RESULT(PromoteAutoFlags(mini_cluster, AutoFlagClass::kLocalVolatile));
    RETURN_NOT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
    RETURN_NOT_OK(VerifyRowsMatch());
    RETURN_NOT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

    LOG(WARNING) << "Rolling back auto flags to config version " << resp.new_config_version() - 1;
    RETURN_NOT_OK(RollbackAutoFlags(mini_cluster, resp.new_config_version() - 1));

    RETURN_NOT_OK(InsertRowsInProducer(2 * kBatchSize, 3 * kBatchSize));
    RETURN_NOT_OK(VerifyRowsMatch());
    RETURN_NOT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

    return Status::OK();
  }

  void DemoteUniverseFlagTest(MiniCluster& cluster_to_rollback) {
    ASSERT_OK(SetupUniverseReplication());
    ASSERT_OK(InsertRowsInProducer(0, kBatchSize));
    ASSERT_OK(VerifyRowsMatch());

    auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
    auto& consumer_table_id = consumer_table_->id();

    auto resp = ASSERT_RESULT(PromoteAutoFlags(cluster_to_rollback, AutoFlagClass::kLocalVolatile));
    ASSERT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
    ASSERT_OK(VerifyRowsMatch());
    ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

    ASSERT_OK(RollbackAutoFlags(cluster_to_rollback, resp.new_config_version() - 1));

    ASSERT_OK(InsertRowsInProducer(2 * kBatchSize, 3 * kBatchSize));
    ASSERT_OK(VerifyRowsMatch());
    ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));
  }

  bool start_producer_without_auto_flags_ = false;
  bool start_consumer_without_auto_flags_ = false;
};

// Verify xCluster Setup works when Source universe is on a lower version (subset of AutoFlags) than
// the Target universe.
TEST_F(XClusterUpgradeTest, SetupWithLowerSourceUniverse) {
  start_producer_without_auto_flags_ = true;

  ASSERT_OK(SetUpWithParams());
  ASSERT_OK(SetupUniverseReplication());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyRowsMatch());
}

// Verify xCluster Setup does not work when Target universe is on a lower version (subset of
// AutoFlags) than the Source universe. Once Target universe is upgraded replication Setup should
// start working.
TEST_F(XClusterUpgradeTest, SetupWithLowerTargetUniverse) {
  start_consumer_without_auto_flags_ = true;

  ASSERT_OK(SetUpWithParams());

  const auto kAutoFlagsMismatchError = "AutoFlags between the universes are not compatible";

  xcluster::ReplicationGroupId replication_group_id1("rg1"), replication_group_id2("rg2"),
      replication_group_id3("rg3");
  auto s = SetupUniverseReplication(replication_group_id1, producer_tables_);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), kAutoFlagsMismatchError);
  master::GetUniverseReplicationResponsePB resp;
  s = VerifyUniverseReplication(replication_group_id1, &resp);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), kAutoFlagsMismatchError);

  ASSERT_OK(DeleteUniverseReplication(replication_group_id1));

  // Without AutoFlag validation enabled it should work. SourceWithoutAutoFlagCompatiblity and
  // TargetWithoutAutoFlagCompatiblity tests validate this more thoroughly.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_auto_flag_validation) = false;
  ASSERT_OK(SetupUniverseReplication(replication_group_id2, producer_tables_));
  ASSERT_OK(DeleteUniverseReplication(replication_group_id2));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_xcluster_auto_flag_validation) = true;

  auto promote_resp = ASSERT_RESULT(PromoteAutoFlags(*consumer_cluster()));
  ASSERT_OK(SetupUniverseReplication(replication_group_id3, producer_tables_));

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyRowsMatch());
}

// Verify upgrading Target universe before Source universe on an already setup xCluster works.
TEST_F(XClusterUpgradeTest, UpgradeTargetBeforeSource) {
  start_consumer_without_auto_flags_ = true;
  start_producer_without_auto_flags_ = true;

  ASSERT_OK(SetUpWithParams());
  ASSERT_OK(SetupUniverseReplication());
  ASSERT_OK(InsertRowsInProducer(0, kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(PromoteAutoFlags(*consumer_cluster()));
  ASSERT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
  ASSERT_OK(VerifyRowsMatch());
}

// Verify upgrading Source universe before Target universe on an already setup xCluster causes
// replication to error out and pause. Once Target universe is upgraded replication should start
// working.
TEST_F(XClusterUpgradeTest, UpgradeSourceBeforeTarget) {
  start_consumer_without_auto_flags_ = true;
  start_producer_without_auto_flags_ = true;

  ASSERT_OK(SetUpWithParams());
  ASSERT_OK(SetupUniverseReplication());
  ASSERT_OK(InsertRowsInProducer(0, kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  auto& consumer_table_id = consumer_table_->id();
  ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

  ASSERT_OK(PromoteAutoFlags(*producer_cluster()));
  ASSERT_OK(VerifyReplicationError(
      consumer_table_id, stream_id, REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH));

  ASSERT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
  // Make sure rows are not replicated.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_NOK(VerifyRowsMatch(producer_table_, kTimeout.ToSeconds()));
  ASSERT_OK(VerifyNumRecords(consumer_table_, consumer_client(), kBatchSize));

  ASSERT_OK(PromoteAutoFlags(*consumer_cluster()));
  ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

  ASSERT_OK(VerifyRowsMatch());
}

// Verify upgrade and rollback of the Target universe does not cause any issues.
TEST_F(XClusterUpgradeTest, RollbackTargetUniverse) {
  ASSERT_OK(RollbackUniverseTest(consumer_cluster_));
}

// Verify upgrade and rollback of the Source universe does not cause any issues.
TEST_F(XClusterUpgradeTest, RollbackSourceUniverse) {
  ASSERT_OK(RollbackUniverseTest(producer_cluster_));
}

// Verify demoting a single AutoFlag on the Source universe does not cause any issues.
TEST_F(XClusterUpgradeTest, DemoteSourceUniverseFlag) {
  ASSERT_OK(SetUpWithParams());
  ASSERT_OK(SetupUniverseReplication());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  auto& consumer_table_id = consumer_table_->id();

  ASSERT_OK(InsertRowsInProducer(0, kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DemoteSingleAutoFlag(*producer_cluster(), kMasterProcessName, kExternalAutoFlagName));
  SleepFor(FLAGS_auto_flags_apply_delay_ms * 1ms);

  ASSERT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
  ASSERT_OK(VerifyRowsMatch(producer_table_, kTimeout.ToSeconds()));
  ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));

  ASSERT_OK(PromoteAutoFlags(*consumer_cluster()));
  ASSERT_OK(InsertRowsInProducer(2 * kBatchSize, 3 * kBatchSize));
  ASSERT_OK(VerifyRowsMatch(producer_table_, kTimeout.ToSeconds()));
  ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));
}

// Verify demoting a single AutoFlag on the Target universe does cause replication to error out and
// pause. Promoting the flag again should cause replication to work again.
TEST_F(XClusterUpgradeTest, DemoteTargetUniverseFlag) {
  ASSERT_OK(SetUpWithParams());
  ASSERT_OK(SetupUniverseReplication());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  auto& consumer_table_id = consumer_table_->id();

  ASSERT_OK(InsertRowsInProducer(0, kBatchSize));
  ASSERT_OK(VerifyRowsMatch());

  ASSERT_OK(DemoteSingleAutoFlag(*consumer_cluster(), kMasterProcessName, kExternalAutoFlagName));

  ASSERT_OK(VerifyReplicationError(
      consumer_table_id, stream_id, REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH));

  ASSERT_OK(InsertRowsInProducer(kBatchSize, 2 * kBatchSize));
  // Make sure rows are not replicated.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_NOK(VerifyRowsMatch(producer_table_, kTimeout.ToSeconds()));
  ASSERT_OK(VerifyNumRecords(consumer_table_, consumer_client(), kBatchSize));

  ASSERT_OK(PromoteAutoFlags(*consumer_cluster()));
  ASSERT_OK(VerifyRowsMatch(producer_table_, kTimeout.ToSeconds()));
  ASSERT_OK(VerifyReplicationError(consumer_table_id, stream_id, std::nullopt));
}

class XClusterUpgradeExternalMiniClusterTest : public XClusterExternalMiniClusterBase {
 public:
  static constexpr auto kAutoFlagValidationFlag = "enable_xcluster_auto_flag_validation";
  const std::string kDisableAutoFlagValidation = Format("--$0=false", kAutoFlagValidationFlag);
  const std::string kLimitLocalAutoFlagsOnCreate = Format(
      "--limit_auto_flag_promote_for_new_universe=$0",
      to_underlying(AutoFlagClass::kLocalPersisted));
  const std::string kLimitExternalAutoFlagsOnCreate = Format(
      "--limit_auto_flag_promote_for_new_universe=$0", to_underlying(AutoFlagClass::kExternal));

  void AddCommonOptions() override {
    XClusterExternalMiniClusterBase::AddCommonOptions();

    setup_opts_.tserver_flags.push_back(
        "--replication_failure_delay_exponent=10");  // 1s max backoff.
  }
};

// Verify xCluster Setup and replication works when Source universe is on a version that does not
// support AutoFlag validation.
TEST_F_EX(
    XClusterUpgradeTest, SourceWithoutAutoFlagCompatiblity,
    XClusterUpgradeExternalMiniClusterTest) {
  source_cluster_.setup_opts_.master_flags.emplace_back(kLimitLocalAutoFlagsOnCreate);
  source_cluster_.setup_opts_.master_flags.emplace_back(kDisableAutoFlagValidation);
  source_cluster_.setup_opts_.tserver_flags.emplace_back(kDisableAutoFlagValidation);

  ASSERT_OK(SetupClustersAndReplicationGroup());

  auto stream_id = ASSERT_RESULT(GetStreamId());
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));

  // Insert rows and validate
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));

  ASSERT_OK(SourceCluster()->SetFlagOnMasters(kAutoFlagValidationFlag, "true"));
  ASSERT_OK(SourceCluster()->SetFlagOnTServers(kAutoFlagValidationFlag, "true"));
  ASSERT_OK(PromoteAutoFlags(SourceCluster(), AutoFlagClass::kLocalPersisted, /* force */ true));

  // Insert rows and validate
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));

  ASSERT_OK(PromoteAutoFlags(SourceCluster()));
  // Insert rows and validate
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));
}

// Verify xCluster Setup and replication works when Target universe is on a version that does not
// support AutoFlag validation.
TEST_F_EX(
    XClusterUpgradeTest, TargetWithoutAutoFlagCompatiblity,
    XClusterUpgradeExternalMiniClusterTest) {
  // We dont want kNewInstallsOnly AutoFlags as those never get promoted.
  source_cluster_.setup_opts_.master_flags.emplace_back(kLimitExternalAutoFlagsOnCreate);

  target_cluster_.setup_opts_.master_flags.emplace_back(kLimitLocalAutoFlagsOnCreate);
  target_cluster_.setup_opts_.master_flags.emplace_back(kDisableAutoFlagValidation);
  target_cluster_.setup_opts_.tserver_flags.emplace_back(kDisableAutoFlagValidation);

  ASSERT_OK(SetupClustersAndReplicationGroup());

  auto stream_id = ASSERT_RESULT(GetStreamId());
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));

  // Insert rows and validate
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));

  ASSERT_OK(TargetCluster()->SetFlagOnMasters(kAutoFlagValidationFlag, "true"));
  ASSERT_OK(TargetCluster()->SetFlagOnTServers(kAutoFlagValidationFlag, "true"));

  // Force promote flags so that AutoFlag validation runs.
  ASSERT_OK(PromoteAutoFlags(TargetCluster(), AutoFlagClass::kLocalPersisted, /* force */ true));

  ASSERT_OK(VerifyReplicationError(
      TargetTable(), stream_id, REPLICATION_AUTO_FLAG_CONFIG_VERSION_MISMATCH));

  ASSERT_OK(PromoteAutoFlags(TargetCluster(), AutoFlagClass::kExternal, /* force */ true));
  ASSERT_OK(VerifyReplicationError(TargetTable(), stream_id, std::nullopt));
}

}  // namespace yb
