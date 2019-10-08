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

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/initial_sys_catalog_snapshot.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

#include "yb/common/pgsql_error.h"
#include "yb/util/random_util.h"

using namespace std::literals;

DECLARE_bool(enable_ysql);
DECLARE_bool(hide_pg_catalog_table_creation_logs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_double(respond_write_failed_probability);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(pggate_rpc_timeout_secs);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_int64(retryable_rpc_single_call_timeout_ms);

namespace yb {
namespace pgwrapper {

class PgMiniTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    constexpr int kNumTabletServers = 3;
    constexpr int kNumMasters = 1;

    FLAGS_client_read_write_timeout_ms = 120000;
    FLAGS_enable_ysql = true;
    FLAGS_hide_pg_catalog_table_creation_logs = true;
    FLAGS_master_auto_run_initdb = true;
    FLAGS_retryable_rpc_single_call_timeout_ms = NonTsanVsTsan(10000, 30000);
    FLAGS_yb_client_admin_operation_timeout_sec = 120;
    FLAGS_pggate_rpc_timeout_secs = 120;
    FLAGS_ysql_num_shards_per_tserver = 1;

    master::SetDefaultInitialSysCatalogSnapshotFlags();
    YBMiniClusterTestBase::SetUp();

    MiniClusterOptions mini_cluster_opt(kNumMasters, kNumTabletServers);
    cluster_ = std::make_unique<MiniCluster>(env_.get(), mini_cluster_opt);
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(WaitForInitDb(cluster_.get()));

    auto pg_ts = RandomElement(cluster_->mini_tablet_servers());
    auto port = cluster_->AllocateFreePort();
    PgProcessConf pg_process_conf = ASSERT_RESULT(PgProcessConf::CreateValidateAndRunInitDb(
        yb::ToString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
        pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
        pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;

    LOG(INFO) << "Starting PostgreSQL server listening on "
              << pg_process_conf.listen_addresses << ":" << pg_process_conf.pg_port << ", data: "
              << pg_process_conf.data_dir;

    pg_supervisor_ = std::make_unique<PgSupervisor>(pg_process_conf);
    ASSERT_OK(pg_supervisor_->Start());

    pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);

    DontVerifyClusterBeforeNextTearDown();
  }

  void DoTearDown() override {
    pg_supervisor_->Stop();
    YBMiniClusterTestBase::DoTearDown();
  }

  Result<PGConn> Connect() {
    return PGConn::Connect(pg_host_port_);
  }

 private:
  std::unique_ptr<PgSupervisor> pg_supervisor_;
  HostPort pg_host_port_;
};

TEST_F(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(Simple)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto value = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(value, "hello");
}

TEST_F(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(WriteRetry)) {
  constexpr int kKeys = 100;
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));

  SetAtomicFlag(0.25, &FLAGS_respond_write_failed_probability);

  LOG(INFO) << "Insert " << kKeys << " keys";
  for (int key = 0; key != kKeys; ++key) {
    auto status = conn.ExecuteFormat("INSERT INTO t (key) VALUES ($0)", key);
    ASSERT_TRUE(status.ok() || PgsqlError(status) == YBPgErrorCode::YB_PG_UNIQUE_VIOLATION ||
                status.ToString().find("Already present: Duplicate request") != std::string::npos)
        << status;
  }

  SetAtomicFlag(0, &FLAGS_respond_write_failed_probability);

  auto result = ASSERT_RESULT(conn.FetchMatrix("SELECT * FROM t ORDER BY key", kKeys, 1));
  for (int key = 0; key != kKeys; ++key) {
    auto fetched_key = ASSERT_RESULT(GetInt32(result.get(), key, 0));
    ASSERT_EQ(fetched_key, key);
  }

  LOG(INFO) << "Insert duplicate key";
  auto status = conn.Execute("INSERT INTO t (key) VALUES (1)");
  ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_UNIQUE_VIOLATION) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "duplicate key value violates unique constraint");
}

TEST_F(PgMiniTest, YB_DISABLE_TEST_IN_SANITIZERS(With)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (k int PRIMARY KEY, v int)"));

  ASSERT_OK(conn.Execute(
      "WITH test2 AS (UPDATE test SET v = 2 WHERE k = 1) "
      "UPDATE test SET v = 3 WHERE k = 1"));
}

} // namespace pgwrapper
} // namespace yb
