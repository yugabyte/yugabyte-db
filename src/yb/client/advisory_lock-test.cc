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

#include "yb/client/yb_table_name.h"
#include "yb/master/master_defaults.h"
#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/client/meta_cache.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_uint32(num_advisory_locks_tablets);
DECLARE_bool(yb_enable_advisory_lock);

namespace yb {
namespace client {

const int kNumAdvisoryLocksTablets = 1;

class AdvisoryLockTest: public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    MiniClusterTestWithClient::SetUp();

    SetFlags();
    auto opts = MiniClusterOptions();
    opts.num_tablet_servers = 3;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(CreateClient());
    if (ANNOTATE_UNPROTECTED_READ(FLAGS_yb_enable_advisory_lock)) {
      ASSERT_OK(WaitForCreateTableToFinish());
    }
  }

  Status WaitForCreateTableToFinish() {
    YBTableName table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kPgAdvisoryLocksTableName);
    return client_->WaitForCreateTableToFinish(
        table_name, CoarseMonoClock::Now() + 10s * kTimeMultiplier);
  }

  Status CheckNumTablets(const YBTablePtr& table) {
    auto future = client_->LookupAllTabletsFuture(table, CoarseMonoClock::Now() + 10s);
    SCHECK_EQ(VERIFY_RESULT(future.get()).size(),
             ANNOTATE_UNPROTECTED_READ(FLAGS_num_advisory_locks_tablets),
             IllegalState, "tablet number mismatch");
    return Status::OK();
  }

  std::unique_ptr<YsqlAdvisoryLocksTable> GetYsqlAdvisoryLocksTable() {
    return std::make_unique<YsqlAdvisoryLocksTable>(*client_.get());
  }

 protected:
  virtual void SetFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_advisory_lock) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = kNumAdvisoryLocksTablets;
  }
};

TEST_F(AdvisoryLockTest, TestAdvisoryLockTableCreated) {
  auto table = GetYsqlAdvisoryLocksTable();
  ASSERT_OK(CheckNumTablets(ASSERT_RESULT(table->GetTable())));
}

class AdvisoryLocksDisabledTest : public AdvisoryLockTest {
 protected:
  void SetFlags() override {
    AdvisoryLockTest::SetFlags();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_advisory_lock) = false;
  }
};

TEST_F(AdvisoryLocksDisabledTest, ToggleAdvisoryLockFlag) {
  auto table = GetYsqlAdvisoryLocksTable();
  // Wait for the background task to run a few times.
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * kTimeMultiplier * 3ms);
  auto res = table->GetTable();
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsNotSupported());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_advisory_lock) = true;
  ASSERT_OK(WaitForCreateTableToFinish());
  ASSERT_OK(CheckNumTablets(ASSERT_RESULT(table->GetTable())));
}

} // namespace client
} // namespace yb
