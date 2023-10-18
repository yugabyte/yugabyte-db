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

#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/util/monotime.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_hang_on_namespace_transition);

using std::string;
using std::vector;
using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgNamespaceMasterRestartTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    pgwrapper::PgMiniTestBase::SetUp();
  }

  void RestartMaster() {
    LOG(INFO) << "Restarting Master";
    auto mini_master_ = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
    ASSERT_OK(mini_master_->Restart());
    ASSERT_OK(mini_master_->master()->WaitUntilCatalogManagerIsLeaderAndReadyForTests());
  }
};

#ifndef NDEBUG
TEST_F(PgNamespaceMasterRestartTest, YB_DISABLE_TEST_IN_TSAN(CreateNamespaceWithDelay)) {
  SyncPoint::GetInstance()->LoadDependency(
      {{"CatalogManager::ProcessPendingNamespace:Fail",
        "PgNamespaceMasterRestartTest::CreateNamespaceWithDelay:WaitForFail"}});
  SyncPoint::GetInstance()->EnableProcessing();

  auto conn = ASSERT_RESULT(Connect());

  TestThreadHolder thread_holder;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = true;

  // Create databse
  thread_holder.AddThreadFunctor([ &stop = thread_holder.stop_flag(), &conn] {
    auto status = conn.Execute("CREATE DATABASE test_db");
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(),
        "Namespace Create Failed: The namespace is in process of deletion due to internal error.");
  });

  TEST_SYNC_POINT("PgNamespaceMasterRestartTest::CreateNamespaceWithDelay:WaitForFail");

  // Restart master
  RestartMaster();

  // Stop threads
  thread_holder.JoinAll();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_hang_on_namespace_transition) = false;

  // Verify that database is not connectable and does not exist in pg_database table
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_NOK(ConnectToDB("test_db"));
  auto res = ASSERT_RESULT(conn2.Fetch(
      "SELECT * FROM pg_database where datname = 'test_db'"));
  ASSERT_EQ(0, PQntuples(res.get()));
}
#endif // NDEBUG

} // namespace pgwrapper
} // namespace yb
