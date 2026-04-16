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

#ifdef __linux__

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_cgroup_manager.h"

#include "yb/gutil/sysinfo.h"

#include "yb/util/cgroups.h"
#include "yb/util/flags.h"
#include "yb/util/test_util.h"

DECLARE_bool(enable_qos);
DECLARE_double(qos_max_db_cpu_percent);
DECLARE_int32(qos_evaluation_window_us);

namespace yb::tserver {

class TServerCgroupManagerTest : public TabletServerTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_qos) = true;
    ASSERT_OK(SetupCgroupManagement(ClearChildCgroups::kTrue));

    TabletServerTestBase::SetUp();
    StartTabletServer();
    auto& server = *mini_server_->server();
    manager_ = CHECK_NOTNULL(server.cgroup_manager());
  }

  TServerCgroupManager* manager_;
};

TEST_F(TServerCgroupManagerTest, TestDbCpuLimits) {
  constexpr PgOid db1_oid = 1234;
  constexpr PgOid db2_oid = 5678;
  constexpr PgOid db3_oid = 4321;

  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 100.0));

  auto& cgroup1 = ASSERT_RESULT_REF(manager_->CgroupForDb(db1_oid));
  auto& cgroup2 = ASSERT_RESULT_REF(manager_->CgroupForDb(db2_oid));

  // SET_FLAG calls validators.
  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 50.0));
  ASSERT_DOUBLE_EQ(cgroup1.cpu_max_fraction(), 0.5);
  ASSERT_DOUBLE_EQ(cgroup2.cpu_max_fraction(), 0.5);

  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 300'000));
  ASSERT_DOUBLE_EQ(cgroup1.cpu_max_fraction(), 0.5);
  ASSERT_DOUBLE_EQ(cgroup2.cpu_max_fraction(), 0.5);
  ASSERT_EQ(cgroup1.cpu_period_us(), 300'000);
  ASSERT_EQ(cgroup2.cpu_period_us(), 300'000);

  auto& cgroup3 = ASSERT_RESULT_REF(manager_->CgroupForDb(db3_oid));
  ASSERT_DOUBLE_EQ(cgroup3.cpu_max_fraction(), 0.5);
  ASSERT_EQ(cgroup3.cpu_period_us(), 300'000);
}

TEST_F(TServerCgroupManagerTest, TestBadDbCpuLimits) {
  const auto num_cpu = base::NumCPUs();

  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 2'000));

  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, -1.0));
  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, 101.0));
  // Must be at least 50% / num_cpu, since minimum quota is 1ms and period is 2ms.
  ASSERT_NOK(SET_FLAG(qos_max_db_cpu_percent, 49.9 / num_cpu));
  ASSERT_OK(SET_FLAG(qos_max_db_cpu_percent, 50.0 / num_cpu));

  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 999));
  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 1'000'001));
  // Must be at least 1999, since minimum quota is 1ms and quota is 50% / num_cpu
  // (and because we round 999.5 to 1000).
  ASSERT_NOK(SET_FLAG(qos_evaluation_window_us, 1998));
  ASSERT_OK(SET_FLAG(qos_evaluation_window_us, 1999));
}

} // namespace yb::tserver

#endif // __linux__
