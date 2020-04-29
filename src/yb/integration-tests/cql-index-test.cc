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

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/rpc/messenger.h"

#include "yb/tserver/heartbeater.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/random_util.h"
#include "yb/util/test_util.h"

#include "yb/yql/cql/cqlserver/cql_server.h"

using namespace std::literals;

DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(rpc_workers_limit);
DECLARE_uint64(transaction_manager_workers_limit);
DECLARE_uint64(TEST_inject_txn_get_status_delay_ms);

namespace yb {

class CqlIndexTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase<MiniCluster>::SetUp();

    MiniClusterOptions options;
    options.num_tablet_servers = 3;
    cluster_ = std::make_unique<MiniCluster>(env_.get(), options);
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(CreateClient());

    auto* mini_tserver = cluster_->mini_tablet_server(0);
    auto* tserver = mini_tserver->server();

    const auto& tserver_options = tserver->options();
    cqlserver::CQLServerOptions cql_server_options;
    cql_server_options.fs_opts = tserver_options.fs_opts;
    cql_server_options.master_addresses_flag = tserver_options.master_addresses_flag;
    cql_server_options.SetMasterAddresses(tserver_options.GetMasterAddresses());

    auto cql_port = cluster_->AllocateFreePort();
    auto cql_host = mini_tserver->bound_rpc_addr().address().to_string();
    cql_server_options.rpc_opts.rpc_bind_addresses = Format("$0:$1", cql_host, cql_port);

    cql_server_ = std::make_unique<cqlserver::CQLServer>(
        cql_server_options, &client_->messenger()->io_service(), tserver);

    ASSERT_OK(cql_server_->Start());

    driver_ = std::make_unique<CppCassandraDriver>(
        std::vector<std::string>{ cql_host }, cql_port, false);
  }

 protected:
  void DoTearDown() override {
    WARN_NOT_OK(cluster_->mini_tablet_server(0)->server()->heartbeater()->Stop(),
                "Failed to stop heartbeater");
    cql_server_->Shutdown();
    MiniClusterTestWithClient<MiniCluster>::DoTearDown();
  }

  std::unique_ptr<CppCassandraDriver> driver_;
  std::unique_ptr<cqlserver::CQLServer> cql_server_;
};

CHECKED_STATUS CreateIndexedTable(CassandraSession* session) {
  RETURN_NOT_OK(session->ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, value INT) WITH transactions = { 'enabled' : true }"));
  return session->ExecuteQuery("CREATE INDEX idx ON T (value)");
}

TEST_F(CqlIndexTest, Simple) {
  constexpr int kKey = 1;
  constexpr int kValue = 2;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(CreateIndexedTable(&session));

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, value) VALUES (1, 2)"));
  auto result = ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t WHERE value = 2"));
  auto iter = result.CreateIterator();
  ASSERT_TRUE(iter.Next());
  auto row = iter.Row();
  ASSERT_EQ(row.Value(0).As<cass_int32_t>(), kKey);
  ASSERT_EQ(row.Value(1).As<cass_int32_t>(), kValue);
  ASSERT_FALSE(iter.Next());
}

class CqlIndexSmallWorkersTest : public CqlIndexTest {
 public:
  void SetUp() override {
    FLAGS_rpc_workers_limit = 4;
    FLAGS_transaction_manager_workers_limit = 4;
    CqlIndexTest::SetUp();
  }
};

TEST_F_EX(CqlIndexTest, ConcurrentIndexUpdate, CqlIndexSmallWorkersTest) {
  constexpr int kThreads = 8;
  constexpr cass_int32_t kKeys = kThreads / 2;
  constexpr cass_int32_t kValues = kKeys;
  constexpr int kNumInserts = kThreads * 5;

  FLAGS_client_read_write_timeout_ms = 10000;
  SetAtomicFlag(1000, &FLAGS_TEST_inject_txn_get_status_delay_ms);

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(CreateIndexedTable(&session));

  TestThreadHolder thread_holder;
  std::atomic<int> inserts(0);
  for (int i = 0; i != kThreads; ++i) {
    thread_holder.AddThreadFunctor([this, &inserts, &stop = thread_holder.stop_flag()] {
      auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
      auto prepared = ASSERT_RESULT(session.Prepare("INSERT INTO t (key, value) VALUES (?, ?)"));
      while (!stop.load(std::memory_order_acquire)) {
        auto stmt = prepared.Bind();
        stmt.Bind(0, RandomUniformInt<cass_int32_t>(1, kKeys));
        stmt.Bind(1, RandomUniformInt<cass_int32_t>(1, kValues));
        auto status = session.Execute(stmt);
        if (status.ok()) {
          ++inserts;
        } else {
          LOG(INFO) << "Insert failed: " << status;
        }
      }
    });
  }

  while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
    if (inserts.load(std::memory_order_acquire) >= kNumInserts) {
      break;
    }
  }

  thread_holder.Stop();

  SetAtomicFlag(0, &FLAGS_TEST_inject_txn_get_status_delay_ms);
}

} // namespace yb
