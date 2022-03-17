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

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(cleanup_split_tablets_interval_sec);

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgTabletSplitTest : public PgMiniTestBase {

 protected:
  CHECKED_STATUS SplitSingleTablet(const TableId& table_id) {
    auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    if (tablets.size() != 1) {
      return STATUS_FORMAT(InternalError, "Expected single tablet, found $0.", tablets.size());
    }
    auto tablet_id = tablets.at(0)->tablet_id();

    return master->catalog_manager().SplitTablet(tablet_id, true);
  }

 private:
  virtual size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitDuringLongRunningTransaction)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

  auto conn = ASSERT_RESULT(Connect());

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT) SPLIT INTO 1 TABLETS;"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) t2;"));

  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  for (int i = 0; i < 10; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));

  ASSERT_OK(SplitSingleTablet(table_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
  }, 15s * kTimeMultiplier, "Wait for split completion."));

  SleepFor(FLAGS_cleanup_split_tablets_interval_sec * 10s * kTimeMultiplier);

  for (int i = 10; i < 20; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  ASSERT_OK(conn.CommitTransaction());
}

} // namespace pgwrapper
} // namespace yb
