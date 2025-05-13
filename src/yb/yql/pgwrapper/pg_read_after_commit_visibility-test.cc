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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
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
    auto port = cluster_->AllocateFreePort();
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
      // Prefix registered to avoid name clash among callback
      // registrations.
      TEST_SetThreadPrefixScoped prefix_se(std::to_string(idx));
      ASSERT_OK(CreateSupervisor(idx));
      ASSERT_OK(pg_supervisors_[idx]->Start());
    }
  }

  struct Config {
    bool same_node = false;
    bool same_conn = false;
    bool is_dml = false;
    bool has_dup_key = false;
    bool wait_for_skew = false;
    bool is_hidden_dml = false;
    std::string visibility = "relaxed";
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
    ASSERT_OK(readConn.ExecuteFormat(
      "SET yb_read_after_commit_visibility = $0", config.visibility));

    if (!config.is_dml) {
      auto rows = ASSERT_RESULT(readConn.FetchRows<int32_t>(query));

      // Observe the recent insert despite the clock skew when on the same node.
      if (config.same_node || config.wait_for_skew) {
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
        ASSERT_EQ(error_code, YBPgErrorCode::YB_PG_UNIQUE_VIOLATION);
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

// Same test as SessionOnDifferentNodeStaleRead
// except we verify that the staleness is bounded
// by waiting out the clock skew.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeBoundedStaleness) {
  RunTest(Config{
    .same_node = false,
    .wait_for_skew = true,
  }, "SELECT k FROM kv");
}

// Duplicate insert check.
//
// Inserts should not miss other recent inserts to
// avoid missing duplicate key violations. This is guaranteed because
// we don't apply "relaxed" to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeDuplicateInsertCheck) {
  RunTest(Config{
    .is_dml = true,
    .has_dup_key = true,
  }, "INSERT INTO kv(k) VALUES (1)");
}

// Updates should not miss recent DMLs either. This is guaranteed
// because we don't apply "relaxed" to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeUpdateKeyCheck) {
  RunTest(Config{
    .is_dml = true,
  }, "UPDATE kv SET k = 2 WHERE k = 1");

  // Ensure that the update happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto row = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT k FROM kv"));
  ASSERT_EQ(row, 2);
}

// Same for DELETEs. They should not miss recent DMLs.
// Otherwise, DELETE FROM table would not delete all the rows.
// This is guaranteed because we don't apply "relaxed" to non-read
// transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeDeleteKeyCheck) {
  RunTest(Config{
    .is_dml = true,
  }, "DELETE FROM kv WHERE k = 1");

  // Ensure that the delete happened.
  auto conn = ASSERT_RESULT(ConnectToIdx(host_idx_));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT k FROM kv"));
  ASSERT_EQ(rows.size(), 0);
}

// We also consider the case where the query looks like
// a SELECT but there is an insert hiding underneath.
// We are guaranteed read-after-commit-visibility in this case
// since "relaxed" is not applied to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeDmlHidden) {
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

} // namespace yb::pgwrapper
