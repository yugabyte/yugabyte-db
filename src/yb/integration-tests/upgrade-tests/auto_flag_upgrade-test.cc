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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

namespace yb {

const MonoDelta kRpcTimeout = 5s * kTimeMultiplier;

// Test upgrade and rollback with a simple workload with updates and selects.
class AutoFlagUpgradeTest : public UpgradeTestBase {
 public:
  AutoFlagUpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}

  Result<master::PromoteAutoFlagsResponsePB> PromoteAutoFlags(AutoFlagClass flag_class) {
    LOG(INFO) << "Promoting AutoFlags " << flag_class;

    master::PromoteAutoFlagsRequestPB req;
    master::PromoteAutoFlagsResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    req.set_max_flag_class(ToString(flag_class));
    req.set_promote_non_runtime_flags(false);
    req.set_force(false);
    RETURN_NOT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().PromoteAutoFlags(
        req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return resp;
  }

  Status RollbackAutoFlags(uint32 old_version) {
    master::RollbackAutoFlagsRequestPB req;
    master::RollbackAutoFlagsResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    req.set_rollback_version(old_version);
    RETURN_NOT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().RollbackAutoFlags(
        req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return Status::OK();
  }
};

TEST_F(AutoFlagUpgradeTest, TestUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());

  // These should all be no-ops since we deployed a new cluster.
  {
    auto validate_no_flags_promoted = [this](AutoFlagClass flag_class) {
      auto resp = ASSERT_RESULT(PromoteAutoFlags(flag_class));
      ASSERT_FALSE(resp.flags_promoted());
    };
    ASSERT_NO_FATAL_FAILURE(validate_no_flags_promoted(AutoFlagClass::kLocalVolatile));
    ASSERT_NO_FATAL_FAILURE(validate_no_flags_promoted(AutoFlagClass::kLocalPersisted));
    ASSERT_NO_FATAL_FAILURE(validate_no_flags_promoted(AutoFlagClass::kExternal));
  }

  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  // We should not be allowed to promote any AutoFlags before all yb-tservers have been upgraded.
  {
    auto validate_no_promote = [this](AutoFlagClass flag_class) {
      ASSERT_NOK_STR_CONTAINS(
          PromoteAutoFlags(flag_class),
          "Cannot promote AutoFlags before all yb-tservers have been upgraded to the current "
          "version");
    };
    ASSERT_NO_FATAL_FAILURE(validate_no_promote(AutoFlagClass::kLocalVolatile));
    ASSERT_NO_FATAL_FAILURE(validate_no_promote(AutoFlagClass::kLocalPersisted));
    ASSERT_NO_FATAL_FAILURE(validate_no_promote(AutoFlagClass::kExternal));
  }

  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(PromoteAutoFlags(AutoFlagClass::kLocalVolatile));

  // Promote all AutoFlags.
  auto resp = ASSERT_RESULT(PromoteAutoFlags(AutoFlagClass::kExternal));
  ASSERT_TRUE(resp.flags_promoted());
  ASSERT_GT(resp.new_config_version(), 1);

  // No more rollbacks.
  ASSERT_NOK_STR_CONTAINS(
      RollbackAutoFlags(resp.new_config_version() - 1), "not eligible for rollback");
}

}  // namespace yb
