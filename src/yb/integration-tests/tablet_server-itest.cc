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

#include <glog/logging.h>

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/ts_itest-base.h"

#include "yb/util/status_log.h"

namespace yb {
namespace tserver {

class TabletServerITest : public TabletServerIntegrationTestBase {
};

// Given a host:port string, return the host part as a string.
std::string GetHost(const std::string& val) {
  return CHECK_RESULT(HostPort::FromString(val, 0)).host();
}

TEST_F(TabletServerITest, TestProxyAddrs) {
  CreateCluster("tablet_server-itest-cluster");

  for (int i = 0; i < FLAGS_num_tablet_servers; i++) {
    auto tserver = cluster_->tablet_server(i);
    auto rpc_host = GetHost(ASSERT_RESULT(tserver->GetFlag("rpc_bind_addresses")));
    for (auto flag : {"cql_proxy_bind_address",
                      "redis_proxy_bind_address",
                      "pgsql_proxy_bind_address"}) {
      auto host = GetHost(ASSERT_RESULT(tserver->GetFlag(flag)));
      ASSERT_EQ(host, rpc_host);
    }
  }
}

TEST_F(TabletServerITest, TestProxyAddrsNonDefault) {
  std::vector<string> ts_flags, master_flags;
  ts_flags.push_back("--cql_proxy_bind_address=127.0.0.1${index}");
  std::unique_ptr<FileLock> redis_port_file_lock;
  auto redis_port = GetFreePort(&redis_port_file_lock);
  ts_flags.push_back("--redis_proxy_bind_address=127.0.0.2${index}:" + std::to_string(redis_port));
  std::unique_ptr<FileLock> pgsql_port_file_lock;
  auto pgsql_port = GetFreePort(&pgsql_port_file_lock);
  ts_flags.push_back("--pgsql_proxy_bind_address=127.0.0.3${index}:" + std::to_string(pgsql_port));

  CreateCluster("ts-itest-cluster-nd", ts_flags, master_flags);

  for (int i = 0; i < FLAGS_num_tablet_servers; i++) {
    auto tserver = cluster_->tablet_server(i);
    auto rpc_host = GetHost(ASSERT_RESULT(tserver->GetFlag("rpc_bind_addresses")));

    auto res = GetHost(ASSERT_RESULT(tserver->GetFlag("cql_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.1$0", i));
    ASSERT_NE(res, rpc_host);

    res = GetHost(ASSERT_RESULT(tserver->GetFlag("redis_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.2$0", i));
    ASSERT_NE(res, rpc_host);

    res = GetHost(ASSERT_RESULT(tserver->GetFlag("pgsql_proxy_bind_address")));
    ASSERT_EQ(res, Format("127.0.0.3$0", i));
    ASSERT_NE(res, rpc_host);
  }
}

}  // namespace tserver
}  // namespace yb
