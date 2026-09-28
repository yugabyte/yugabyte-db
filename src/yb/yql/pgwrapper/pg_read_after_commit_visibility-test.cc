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

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/util/logging_test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

#include "yb/common/pgsql_error.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/server/skewed_clock.h"

#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/yb_pg_errcodes.h"

using std::string;

using namespace std::literals;

DECLARE_string(time_source);
DECLARE_int32(replication_factor);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_concurrent_ddl);
DECLARE_uint64(ysql_lease_refresher_interval_ms);
DECLARE_bool(ysql_serializable_isolation_for_ddl_txn);

namespace yb::pgwrapper {

// Helper class to test the semantics of yb_read_after_commit_visibility option.
//
// Additional infrastructure was required for the test.
//
// The test requires us to simulate two connections to separate postmaster
//   processes on different tservers. Usually, we could get away with
//   ExternalMiniCluster if we required two different postmaster processes.
// However, the test also requires that we register skewed clocks and jump
//   the clocks as necessary.
//
// Here, we take the easier approach of using a MiniCluster (that supports
// skewed clocks out of the box) and then simulate multiple postmaster
// processes by explicitly spawning PgSupervisor processes for each tserver.
//
// Typical setup:
// 1. MiniCluster with 2 tservers.
// 2. One server hosts a test table with single tablet and RF 1.
// 3. The other server, proxy, is blacklisted to control hybrid propagation.
//    This is the node that the external client connects to, for the read
//    query and "expects" to the see the recent commit.
// 4. Ensure that the proxy is also not on the master node.
// 5. Pre-populate the catalog cache so there are no surprise communications
//    between the servers.
// 6. Register skewed clocks. We only jump the clock on the node that hosts
//    the data.
//
// Additional considerations/caveats:
// - Register a thread prefix for each supervisor.
//   Otherwise, the callback registration fails with name conflicts.
// - Do NOT use PgPickTabletServer. It does not work. Moreover, it is
//   insufficient for our usecase even if it did work as intended.
class PgReadAfterCommitVisibilityTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
    // Support DDL concurrency with object locks.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = true;
    PgMiniTestBase::SetUp();
    SpawnSupervisors();
  }

  void DoTearDown() override {
    // Exit supervisors cleanly ...
    // Risk of false positive segfaults otherwise ...
    for (auto&& supervisor : pg_supervisors_) {
      if (supervisor) {
        supervisor->Stop();
      }
    }
    PgMiniTestBase::DoTearDown();
  }

  size_t NumTabletServers() override {
    // One server for a proxy and the other server to host the data.
    return 2;
  }

  void BeforePgProcessStart() override {
    // Identify the tserver index that hosts the MiniCluster postmaster
    // process so that we do NOT spawn a PgSupervisor for that tserver.
    auto connParams = MakeConnSettings();
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());
    for (int idx = 0; idx < ntservers; idx++) {
      auto server = cluster_->mini_tablet_server(idx);
      if (server->bound_rpc_addr().address().to_string() == connParams.host) {
        conn_idx_ = idx;
        break;
      }
    }
  }

  Result<PGConn> ConnectToIdx(int idx) const {
    // postmaster hosted by PgMiniTestBase itself.
    if (idx == conn_idx_) {
      return Connect();
    }

    // We own the postmaster process for this tserver idx.
    // Use the appropriate postmaster process to setup a pg connection.
    auto connParams = PGConnSettings {
      .host = pg_host_ports_[idx].host(),
      .port = pg_host_ports_[idx].port()
    };

    auto result = VERIFY_RESULT(PGConnBuilder(connParams).Connect());
    RETURN_NOT_OK(SetupConnection(&result));
    return result;
  }

  // Called for the first connection.
  // Use ConnectToIdx() directly for subsequent connections.
  Result<PGConn> ConnectToProxy() {
    // Avoid proxy on the node that hosts the master because
    //   tservers and masters regularly exchange heartbeats with each other.
    //   This means there is constant hybrid time propagation between
    //   the master and the tservers.
    //   We wish to avoid hyrbid time from propagating to the proxy node.
    if (static_cast<int>(cluster_->LeaderMasterIdx()) == proxy_idx_) {
      return STATUS(IllegalState, "Proxy cannot be on the master node ...");
    }

    // Add proxy to the blacklist to limit hybrid time prop.
    auto res = cluster_->AddTServerToBlacklist(proxy_idx_);
    if (!res.ok()) {
      return res;
    }

    // Now, we are ready to connect to the proxy.
    return ConnectToIdx(proxy_idx_);
  }

  Result<PGConn> ConnectToDataHost() {
    return ConnectToIdx(host_idx_);
  }

  // Jump the clocks of the nodes hosting the data.
  std::vector<server::SkewedClockDeltaChanger> JumpClockDataNodes(
      std::chrono::milliseconds skew) {
    std::vector<server::SkewedClockDeltaChanger> changers;
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());
    for (int idx = 0; idx < ntservers; idx++) {
      if (idx == proxy_idx_) {
        continue;
      }
      changers.push_back(JumpClock(cluster_->mini_tablet_server(idx), skew));
    }
    return changers;
  }

 protected:
  // Setup to create the postmaster process corresponding to the tserver idx.
  Status CreateSupervisor(int idx) {
    auto pg_ts = cluster_->mini_tablet_server(idx);
    auto port = pg_ts->server()->pgsql_proxy_bind_address().port();
    PgProcessConf pg_process_conf = VERIFY_RESULT(PgProcessConf::CreateValidateAndRunInitDb(
        AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
        pg_ts->options()->fs_opts.data_paths.front() + "/pg_data"));

    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    pg_host_ports_[idx] = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);

    pg_supervisors_[idx] = std::make_unique<PgSupervisor>(pg_process_conf, pg_ts->server());

    return Status::OK();
  }

  void SpawnSupervisors() {
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());

    // Allocate space for the host ports and supervisors.
    pg_host_ports_.resize(ntservers);
    pg_supervisors_.resize(ntservers);

    // Create and start the PgSupervisors.
    for (int idx = 0; idx < ntservers; idx++) {
      if (idx == conn_idx_) {
        // Postmaster already started for this tserver.
        continue;
      }
      // PgMiniTestBase starts pg supervisor on just one node (by default, tserver with idx 0).
      // Manually start the YSQL lease poller on other tservers as pg connections are being spawned.
      auto log_waiter = StringWaiterLogSink("PG restarter callback not registered");
      ASSERT_OK(cluster_->mini_tablet_server(idx)->server()->StartYSQLLeaseRefresher());
      ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromMilliseconds(
          5 * kTimeMultiplier * FLAGS_ysql_lease_refresher_interval_ms)));
      // Prefix registered to avoid name clash among callback
      // registrations.
      TEST_SetThreadPrefixScoped prefix_se(std::to_string(idx));
      ASSERT_OK(CreateSupervisor(idx));
      ASSERT_OK(pg_supervisors_[idx]->Start());
    }
  }

  enum class Visibility {
    STRICT,
    RELAXED,
    DEFERRED,
  };

  struct Config {
    bool same_node = false;
    bool same_conn = false;
    bool is_dml = false;
    bool has_dup_key = false;
    bool wait_for_skew = false;
    bool is_hidden_dml = false;
    Visibility visibility = Visibility::RELAXED;
  };

  // General framework to observe the behavior of reads in different scenarios
  // with yb_read_after_commit_visibility option.
  //
  // Test Setup:
  // 1. Cluster with RF 1, skewed clocks, 2 tservers.
  // 2. Add a pg process on tserver that does not have one.
  //    This is done since MiniCluster only creates one postmaster process
  //    on some random tserver.
  //    We wish to use the minicluster and not external minicluster since
  //    there is better test infrastructure to manipulate clock skew.
  //    This approach is the less painful one at the moment.
  // 3. Add a tablet server to the blacklist
  //    so we can ensure hybrid time propagation doesn't occur between
  //    the data host node and the proxy
  // 4. Connect to the proxy tserver that does not host the data.
  //    We simulate this by blacklisting the target tserver.
  // 5. Create a table with a single tablet and a single row.
  // 6. Populate the catalog cache on the pg backend so that
  //    catalog cache misses does not interfere with hybrid time propagation.
  // 7. Jump the clock of the tserver hosting the table to the future.
  // 8. Insert a row using the setup conn and a fast path txn.
  //    Commit ts for the insert is picked on the server whose clock is ahead.
  // 9. Read the table from the proxy connection.
  //    Does the read observe the commit that is ahead because of clock skew?
  void RunTest(Config const &config, std::string const &query) {
    ASSERT_TRUE(!config.is_hidden_dml || config.is_dml)
        << "Hidden DMLs are still DMLs. So, is_hidden_dml => is_dml.";
    // Connect to local proxy.
    auto proxyConn = ASSERT_RESULT(ConnectToProxy());
    // Not calling ConnectToProxy() again since we already added
    // the proxy to the blacklist.
    auto proxyConn2 = ASSERT_RESULT(ConnectToIdx(proxy_idx_));
    // Connect to the data node.
    auto hostConn = ASSERT_RESULT(ConnectToDataHost());

    auto &setupConn = !config.same_node ? hostConn : (!config.same_conn ? proxyConn2 : proxyConn);
    auto &readConn = proxyConn;

    // Create table tokens.
    ASSERT_OK(setupConn.Execute(
      "CREATE TABLE kv (k INT, v INT, PRIMARY KEY(k HASH)) SPLIT INTO 1 TABLETS"));

    // Populate catalog cache.
    if (!config.is_dml) {
      ASSERT_RESULT(readConn.FetchRows<int32_t>(query));
    } else {
      ASSERT_OK(readConn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      if (!config.is_hidden_dml) {
        ASSERT_OK(readConn.Execute(query));
      } else {
        ASSERT_RESULT(readConn.FetchRows<int32_t>(query));
      }
      ASSERT_OK(readConn.RollbackTransaction());
    }

    // Jump the clock on the tserver hosting the table.
    auto skew = 100ms;
    auto changers = JumpClockDataNodes(skew);

    // Perform a fast path insert that picks the commit time
    // on the data node.
    ASSERT_OK(setupConn.Execute("INSERT INTO kv(k) VALUES (1)"));

    if (config.wait_for_skew) {
      SleepFor(skew);
    }

    // Perform a select using the the relaxed yb_read_after_commit_visibility option.
    auto visibility = [](auto visibility) {
      switch (visibility) {
        case Visibility::STRICT:
          return "strict";
        case Visibility::RELAXED:
          return "relaxed";
        case Visibility::DEFERRED:
          return "deferred";
      }
      return "<unknown>"; // keep gcc happy
    }(config.visibility);
    ASSERT_OK(readConn.ExecuteFormat(
      "SET yb_read_after_commit_visibility = $0", visibility));

    if (!config.is_dml) {
      auto rows = ASSERT_RESULT(readConn.FetchRows<int32_t>(query));

      // Observe the recent insert despite the clock skew when on the same node.
      if (config.visibility != Visibility::RELAXED || config.same_node || config.wait_for_skew) {
        ASSERT_EQ(rows.size(), 1);
      } else {
        ASSERT_EQ(rows.size(), 0);
      }
    } else {
      auto status = [&]() -> Status {
        if (!config.is_hidden_dml) {
          return readConn.Execute(query);
        } else {
          auto res = readConn.FetchRows<int32_t>(query);
          return res.ok() ? Status::OK() : res.status();
        }
      }();
      if (!config.has_dup_key) {
        ASSERT_OK(status);
      } else {
        ASSERT_NOK(status);
        auto pg_err_ptr = status.ErrorData(PgsqlError::kCategory);
        ASSERT_NE(pg_err_ptr, nullptr);
        YBPgErrorCode error_code = PgsqlErrorTag::Decode(pg_err_ptr);
        ASSERT_TRUE(
            error_code == YBPgErrorCode::YB_PG_UNIQUE_VIOLATION ||
            error_code == YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE);
      }
    }
  }

  int conn_idx_ = 0;
  int proxy_idx_ = 1;
  int host_idx_ = 0;
  std::vector<HostPort> pg_host_ports_;
  std::vector<std::unique_ptr<PgSupervisor>> pg_supervisors_;
};

// Ensures that clock skew does not affect read-after-commit guarantees on same
// session with relaxed yb_read_after_commit_visibility option.
TEST_F(PgReadAfterCommitVisibilityTest, SameSessionRecency) {
  RunTest(Config{
    .same_node = true,
    .same_conn = true,
  }, "SELECT k FROM kv");
}

// Similar to SameSessionRecency except we have
// two connections instead of one to the same node.
//
// This property is necessary to maintain same session guarantees even in the
// presence of server-side connection pooling.
TEST_F(PgReadAfterCommitVisibilityTest, SamePgNodeRecency) {
  RunTest(Config{
    .same_node = true,
    .same_conn = false,
  }, "SELECT k FROM kv");
}

// Demonstrate that read from a connection to a different node
// (than the one which had the Pg connection to write data) may miss the
// commit when using the relaxed yb_read_after_commit_visibility option.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeStaleRead) {
  RunTest(Config{
    .same_node = false,
  }, "SELECT k FROM kv");
}

// REFERSH MATERIALIZED VIEW honors the strict read after commit visibility guarantee.
// This is despite using a clamped uncertainty window for table reads.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeMatView) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create the base table on the data node.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE kv (k INT, v INT, PRIMARY KEY(k HASH)) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(hostConn.Execute("CREATE MATERIALIZED VIEW kv_mv AS SELECT k, v FROM kv"));
  ASSERT_OK(proxyConn.Execute("REFRESH MATERIALIZED VIEW kv_mv"));
  auto count = ASSERT_RESULT(proxyConn.FetchRow<int64_t>("SELECT COUNT(1) FROM kv_mv"));
  ASSERT_EQ(count, 0);

  // Jump the data node's clock into the future.
  auto skew = 100ms;
  auto changers = JumpClockDataNodes(skew);

  // Insert a row via the host connection (fast-path single-shard txn).
  // The commit timestamp is picked on the data node whose clock is ahead.
  ASSERT_OK(hostConn.Execute("INSERT INTO kv(k, v) VALUES (1, 1)"));

  // Issue REFRESH MATERIALIZED VIEW from the proxy.
  // The DDL's internal read timestamp is picked above the max hybrid time
  // observed across nodes, so it can use a clamped uncertainty window.
  // Therefore the read does not hit a restart despite clock skew.
  ASSERT_OK(proxyConn.Execute("REFRESH MATERIALIZED VIEW kv_mv"));
  count = ASSERT_RESULT(proxyConn.FetchRow<int64_t>("SELECT COUNT(1) FROM kv_mv"));
  ASSERT_EQ(count, 1);
}

// Same test as SessionOnDifferentNodeStaleRead
// except we verify that the staleness is bounded
// by waiting out the clock skew.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeBoundedStaleness) {
  RunTest(Config{
    .same_node = false,
    .wait_for_skew = true,
  }, "SELECT k FROM kv");
}

// Inserts should not ignore duplicate key violations when using relaxed mode.
TEST_F(PgReadAfterCommitVisibilityTest, RelaxedModeDuplicateKeyInsert) {
  RunTest(Config{
    .is_dml = true,
    .has_dup_key = true,
  }, "INSERT INTO kv(k) VALUES (1)");
}

// Ensure that relaxed mode doesn't apply to fast-path writes.
// However, relaxed mode is applied to distributed txn writes.
// So, read-after-write visibility is not guaranteed for distributed txn writes.
TEST_F(PgReadAfterCommitVisibilityTest, RelaxedModeFastPathUpdate) {
  RunTest(Config{
    .is_dml = true,
  }, "UPDATE kv SET v = 2 WHERE k = 1");

  // Ensure that the update happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto row = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT v FROM kv"));
  ASSERT_EQ(row, 2);
}

// Relaxed mode is not applied to fast path deletes.
TEST_F(PgReadAfterCommitVisibilityTest, RelaxedModeFastPathDelete) {
  RunTest(Config{
    .is_dml = true,
  }, "DELETE FROM kv WHERE k = 1");

  // Ensure that the delete happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT k FROM kv"));
  ASSERT_EQ(rows.size(), 0);
}

// Hidden DMLs are still DMLs. So, relaxed mode is applied to them.
// However, duplicate key violations are not ignored.
TEST_F(PgReadAfterCommitVisibilityTest, RelaxedModeHiddenDmlDuplicateKey) {
  RunTest(
    Config{
      .is_dml = true,
      .has_dup_key = true,
      .is_hidden_dml = true,
    },
    "WITH new_kv AS ("
    "INSERT INTO kv(k) VALUES (1) RETURNING k"
    ") SELECT k FROM new_kv"
  );
}

TEST_F(PgReadAfterCommitVisibilityTest, DifferentNodeDeferredRead) {
  RunTest(Config{
    .visibility = Visibility::DEFERRED,
  }, "SELECT k FROM kv");
}

TEST_F(PgReadAfterCommitVisibilityTest, DeferredModeInsert) {
  RunTest(Config{
    .is_dml = true,
    .has_dup_key = true,
    .visibility = Visibility::DEFERRED,
  }, "INSERT INTO kv(k) VALUES (1)");
}

TEST_F(PgReadAfterCommitVisibilityTest, DeferredModeDistributedTxnUpdate) {
  RunTest(Config{
    .is_dml = true,
    .visibility = Visibility::DEFERRED,
  }, "UPDATE kv SET k = 2 WHERE k = 1");

  // Ensure that the update happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto row = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT k FROM kv"));
  ASSERT_EQ(row, 2);
}

TEST_F(PgReadAfterCommitVisibilityTest, DeferredModeFastPathUpdate) {
  RunTest(Config{
    .is_dml = true,
    .visibility = Visibility::DEFERRED,
  }, "UPDATE kv SET v = 2 WHERE k = 1");

  // Ensure that the update happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto row = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT v FROM kv"));
  ASSERT_EQ(row, 2);
}

TEST_F(PgReadAfterCommitVisibilityTest, DeferredModeHiddenDml) {
  RunTest(
    Config{
      .is_dml = true,
      .has_dup_key = true,
      .is_hidden_dml = true,
      .visibility = Visibility::DEFERRED,
    },
    "WITH new_kv AS ("
    "INSERT INTO kv(k) VALUES (1) RETURNING k"
    ") SELECT k FROM new_kv"
  );
}

YB_STRONGLY_TYPED_BOOL(DdlInTxnBlock);

// Mode each DDL test runs in. `serializable` is applied via
// FLAGS_ysql_serializable_isolation_for_ddl_txn for an autonomous DDL, which picks its own
// isolation level, and via the enclosing transaction for a transactional DDL, which inherits it.
struct DdlTestParam {
  bool transactional_ddl = false;
  bool serializable = false;

  IsolationLevel TxnIsolation() const {
    return serializable ? IsolationLevel::SERIALIZABLE_ISOLATION
                        : IsolationLevel::SNAPSHOT_ISOLATION;
  }

  std::string Name() const {
    if (transactional_ddl) {
      return serializable ? "TransactionalDdlInSerializableTxn" : "TransactionalDdlInSnapshotTxn";
    }
    return serializable ? "SerializableDdlTxn" : "SnapshotDdlTxn";
  }
};

// Test fixture for the DDL flavors that read user tables.
//
// The test parameter selects between autonomous DDLs (a separate transaction per DDL, even inside
// a transaction block) and transactional DDLs, and for autonomous DDLs whether the transaction is
// created with serializable or snapshot isolation.
//
// Deferring only has an effect on autonomous DDLs in snapshot isolation. Serializable transactions
// don't face read restart errors in the first place, and transactional DDLs that read user tables
// take an object lock that visits all nodes in the cluster, which clamps the uncertainty window.
class PgReadAfterCommitVisibilityDdlTest
    : public PgReadAfterCommitVisibilityTest,
      public ::testing::WithParamInterface<DdlTestParam> {
 public:
  void SetUp() override {
    const auto transactional_ddl = GetParam().transactional_ddl;
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = transactional_ddl;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = transactional_ddl;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = transactional_ddl;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_serializable_isolation_for_ddl_txn) =
        GetParam().serializable;
    PgMiniTestBase::SetUp();
    SpawnSupervisors();
  }

 protected:
  // Creates the table read by the DDLs under test, plus a reference table for the foreign key
  // test. Every row inserted by InsertRowOnHost() satisfies both `v > 0` and the foreign key
  // kv(v) -> kv_ref(k), so a DDL that validates data only fails if it cannot read that data.
  Status SetupTables(PGConn& conn) {
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE kv (k INT, v INT, PRIMARY KEY(k HASH)) SPLIT INTO 1 TABLETS"));
    RETURN_NOT_OK(conn.Execute("CREATE TABLE kv_ref (k INT PRIMARY KEY)"));
    return conn.Execute("INSERT INTO kv_ref(k) VALUES (1)");
  }

  Status InsertRowOnHost(PGConn& conn) {
    return conn.ExecuteFormat("INSERT INTO kv(k, v) VALUES ($0, 1)", ++next_key_);
  }

  // Whether the autonomous DDL transaction runs in serializable isolation.
  bool IsSerializableDdlTxn() const { return GetParam().serializable; }

  // Only an autonomous DDL in snapshot isolation reads at the proxy's (behind) clock.
  bool ExpectsReadRestartWithoutDeferredMode() const {
    return !GetParam().transactional_ddl && !IsSerializableDdlTxn();
  }

  // Core assertion shared by all the DDL flavors below. The proxy's clock lags the data host's, so
  // a row just committed on the data host falls into the proxy's uncertainty window.
  //
  // In snapshot isolation, a DDL without deferred mode reads at the proxy's (behind) clock, hits a
  // read restart, and fails -- read restarts are not retried for DDLs. With deferred mode the read
  // point is set to global_limit, which is above the commit timestamp, so the DDL sees all
  // committed data.
  //
  // In serializable isolation no read time is picked, and a transactional DDL clamps the
  // uncertainty window via object locks, so the DDL never faces a read restart and always sees the
  // committed row. Deferred mode is a no-op there; we only verify that enabling it does not change
  // the outcome.
  //
  // Every DDL is checked both outside and inside an explicit transaction block; it runs in its own
  // autonomous transaction either way.
  struct DdlCheck {
    std::string ddl;

    // Undoes a successful `ddl` so that it can be run again.
    std::string undo = "";

    // Optional query returning the number of rows the DDL should have seen. Checked after every
    // successful run of the DDL.
    std::string row_count_query = "";
  };

  void CheckDdlSeesCommittedData(PGConn& proxy_conn, PGConn& host_conn, const DdlCheck& check) {
    // The DDL runs in autocommit in one phase and in an explicit transaction block in the other.
    // Set the session default so both phases use the isolation level under test.
    ASSERT_OK(proxy_conn.ExecuteFormat(
        "SET default_transaction_isolation = '$0'",
        GetParam().serializable ? "serializable" : "repeatable read"));

    // Run the DDL once before skewing the clocks. A catalog cache miss during the DDL would talk
    // to the master and propagate the data host's hybrid time to the proxy, closing the very
    // uncertainty window the test relies on.
    ASSERT_OK(RunDdlAndUndo(proxy_conn, check));

    auto changers = JumpClockDataNodes(200ms);

    for (auto in_txn_block : {DdlInTxnBlock::kFalse, DdlInTxnBlock::kTrue}) {
      SCOPED_TRACE(Format("ddl: $0, in_txn_block: $1", check.ddl, in_txn_block));
      ASSERT_OK(proxy_conn.Execute("RESET yb_read_after_commit_visibility"));
      ASSERT_OK(InsertRowOnHost(host_conn));

      if (ExpectsReadRestartWithoutDeferredMode()) {
        const auto status = ExecuteDdl(proxy_conn, check.ddl, in_txn_block);
        ASSERT_NOK(status) << "DDL unexpectedly succeeded without deferred mode";
        LOG(INFO) << "Expected read restart error for DDL without deferred mode: " << status;
        ASSERT_EQ(PgsqlError(status), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) << status;

        // The failed attempt propagated the data host's hybrid time to the proxy, so commit one
        // more row to put the proxy's clock behind again.
        ASSERT_OK(InsertRowOnHost(host_conn));
      }

      ASSERT_OK(proxy_conn.Execute("SET yb_read_after_commit_visibility = 'deferred'"));
      ASSERT_OK(ExecuteDdl(proxy_conn, check.ddl, in_txn_block));

      if (!check.row_count_query.empty()) {
        const auto count = ASSERT_RESULT(proxy_conn.FetchRow<int64_t>(check.row_count_query));
        ASSERT_EQ(count, NumRowsInserted());
      }
      if (!check.undo.empty()) {
        ASSERT_OK(proxy_conn.Execute(check.undo));
      }
    }
    ASSERT_OK(proxy_conn.Execute("RESET yb_read_after_commit_visibility"));
  }

  Status RunDdlAndUndo(PGConn& conn, const DdlCheck& check) {
    RETURN_NOT_OK(conn.Execute(check.ddl));
    return check.undo.empty() ? Status::OK() : conn.Execute(check.undo);
  }

  // Runs the DDL, optionally wrapped in an explicit transaction block. The DDL runs in its own
  // autonomous transaction either way. Returns the status of the DDL itself.
  Status ExecuteDdl(PGConn& conn, const std::string& ddl, DdlInTxnBlock in_txn_block) {
    if (!in_txn_block) {
      return conn.Execute(ddl);
    }
    RETURN_NOT_OK(conn.StartTransaction(GetParam().TxnIsolation()));
    const auto status = conn.Execute(ddl);
    RETURN_NOT_OK(status.ok() ? conn.CommitTransaction() : conn.RollbackTransaction());
    return status;
  }

  // Number of rows committed on the data host so far.
  int NumRowsInserted() const { return next_key_; }

 private:
  int next_key_ = 0;
};

INSTANTIATE_TEST_SUITE_P(
    DdlMode, PgReadAfterCommitVisibilityDdlTest,
    ::testing::Values(
        DdlTestParam{.transactional_ddl = false, .serializable = false},
        DdlTestParam{.transactional_ddl = false, .serializable = true},
        DdlTestParam{.transactional_ddl = true, .serializable = false},
        DdlTestParam{.transactional_ddl = true, .serializable = true}),
    [](const auto& info) { return info.param.Name(); });

// Verify that REFRESH MATERIALIZED VIEW sees committed data when using deferred
// read-after-commit visibility in autonomous DDL mode.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeDdl) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_OK(hostConn.Execute("CREATE MATERIALIZED VIEW kv_mv AS SELECT k, v FROM kv"));
  ASSERT_OK(proxyConn.Execute("REFRESH MATERIALIZED VIEW kv_mv"));
  auto count = ASSERT_RESULT(proxyConn.FetchRow<int64_t>("SELECT COUNT(1) FROM kv_mv"));
  ASSERT_EQ(count, 0);

  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "REFRESH MATERIALIZED VIEW kv_mv",
      .row_count_query = "SELECT COUNT(1) FROM kv_mv"}));

  // In deferred mode, a DML in the enclosing transaction block sees the committed data too.
  ASSERT_OK(InsertRowOnHost(hostConn));
  ASSERT_OK(proxyConn.Execute("SET yb_read_after_commit_visibility = 'deferred'"));
  ASSERT_OK(proxyConn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  const auto rows = ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT k FROM kv"));
  ASSERT_EQ(rows.size(), NumRowsInserted());
  ASSERT_OK(proxyConn.CommitTransaction());
  ASSERT_OK(proxyConn.Execute("RESET yb_read_after_commit_visibility"));
}

TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeCtas) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "CREATE TABLE kv_ctas AS SELECT k, v FROM kv",
      .undo = "DROP TABLE kv_ctas",
      .row_count_query = "SELECT COUNT(1) FROM kv_ctas"}));
}

// ALTER TABLE ... VALIDATE CONSTRAINT scans the table to validate an existing NOT VALID
// constraint.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeValidateConstraint) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_OK(hostConn.Execute(
      "ALTER TABLE kv ADD CONSTRAINT kv_v_positive CHECK (v > 0) NOT VALID"));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "ALTER TABLE kv VALIDATE CONSTRAINT kv_v_positive",
      // Reset the constraint back to NOT VALID so it can be validated again.
      .undo = "ALTER TABLE kv DROP CONSTRAINT kv_v_positive;"
              "ALTER TABLE kv ADD CONSTRAINT kv_v_positive CHECK (v > 0) NOT VALID"}));
}

// ALTER TABLE ... ADD CONSTRAINT ... CHECK validates the existing rows as part of the DDL.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeAddCheckConstraint) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "ALTER TABLE kv ADD CONSTRAINT kv_v_positive CHECK (v > 0)",
      .undo = "ALTER TABLE kv DROP CONSTRAINT kv_v_positive"}));
}

// ALTER TABLE ... ADD FOREIGN KEY scans the referencing table to verify every row has a match.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeAddForeignKey) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "ALTER TABLE kv ADD CONSTRAINT kv_v_fk FOREIGN KEY (v) REFERENCES kv_ref(k)",
      .undo = "ALTER TABLE kv DROP CONSTRAINT kv_v_fk"}));
}

// ATTACH PARTITION scans the table being attached to verify it satisfies the partition bounds.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeAddPartition) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_OK(hostConn.Execute("CREATE TABLE kv_part (k INT, v INT) PARTITION BY RANGE (k)"));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "ALTER TABLE kv_part ATTACH PARTITION kv FOR VALUES FROM (0) TO (1000000)",
      .undo = "ALTER TABLE kv_part DETACH PARTITION kv",
      .row_count_query = "SELECT COUNT(1) FROM kv_part"}));
}

// Altering a column type rewrites the table, i.e. the DDL reads every row of the old table.
TEST_P(PgReadAfterCommitVisibilityDdlTest, DeferredModeAlterTableRewrite) {
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  ASSERT_OK(SetupTables(hostConn));
  ASSERT_NO_FATALS(CheckDdlSeesCommittedData(proxyConn, hostConn, {
      .ddl = "ALTER TABLE kv ALTER COLUMN v TYPE BIGINT",
      // Rewrite the column back to INT so the next phase rewrites the table again.
      .undo = "ALTER TABLE kv ALTER COLUMN v TYPE INT",
      .row_count_query = "SELECT COUNT(1) FROM kv"}));
}

} // namespace yb::pgwrapper
