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

#include "yb/tserver/mini_tablet_server.h"

#include "yb/yql/cql/cqlserver/cql_server.h"

namespace yb {

class CqlIndexTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase<MiniCluster>::SetUp();

    MiniClusterOptions options;
    options.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), options));
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
    cql_server_->Shutdown();
    MiniClusterTestWithClient<MiniCluster>::DoTearDown();
  }

  std::unique_ptr<CppCassandraDriver> driver_;
  std::unique_ptr<cqlserver::CQLServer> cql_server_;
};

TEST_F(CqlIndexTest, Simple) {
  constexpr int kKey = 1;
  constexpr int kValue = 2;

  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));

  ASSERT_OK(session.ExecuteQuery(
      "CREATE TABLE t (key INT PRIMARY KEY, value INT) WITH transactions = { 'enabled' : true }"));
  ASSERT_OK(session.ExecuteQuery("CREATE INDEX idx ON T (value)"));

  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (key, value) VALUES (1, 2)"));
  auto result = ASSERT_RESULT(session.ExecuteWithResult("SELECT * FROM t WHERE value = 2"));
  auto iter = result.CreateIterator();
  ASSERT_TRUE(iter.Next());
  auto row = iter.Row();
  ASSERT_EQ(row.Value(0).As<cass_int32_t>(), kKey);
  ASSERT_EQ(row.Value(1).As<cass_int32_t>(), kValue);
  ASSERT_FALSE(iter.Next());
}

} // namespace yb
