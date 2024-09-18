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

#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using namespace std::chrono_literals;
using namespace std::literals;

namespace yb::pgwrapper {

namespace {

const auto kDatabaseName = "yugabyte"s;

}  // namespace

class PgAshTest : public LibPqTestBase {
 public:
  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = std::make_unique<PGConn>(ASSERT_RESULT(ConnectToDB(kDatabaseName)));
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_ash_enable_infra,ysql_yb_enable_ash");
    options->extra_tserver_flags.push_back("--ysql_yb_ash_enable_infra=true");
    options->extra_tserver_flags.push_back("--ysql_yb_enable_ash=true");
    options->extra_tserver_flags.push_back("--ysql_yb_ash_sampling_interval_ms=50");
  }

 protected:
  std::unique_ptr<PGConn> conn_;
  TestThreadHolder thread_holder_;
  const int kTabletsPerServer = 1;
};

TEST_F(PgAshTest, NoMemoryLeaks) {
  ASSERT_OK(conn_->Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));

  thread_holder_.AddThreadFunctor([this, &stop = thread_holder_.stop_flag()] {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; !stop; i++) {
      ASSERT_OK(conn.Execute(yb::Format("INSERT INTO t (key, value) VALUES ($0, 'v-$0')", i)));
    }
  });
  thread_holder_.AddThreadFunctor([this, &stop = thread_holder_.stop_flag()] {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; !stop; i++) {
      auto values = ASSERT_RESULT(
          conn.FetchRows<std::string>(yb::Format("SELECT value FROM t where key = $0", i)));
    }
  });

  SleepFor(10s);
  thread_holder_.Stop();

  {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; i < 100; i++) {
      auto values = ASSERT_RESULT(conn.FetchRows<std::string>(
          yb::Format("SELECT wait_event FROM yb_active_session_history")));
      SleepFor(10ms);
    }
  }
}

}  // namespace yb::pgwrapper
