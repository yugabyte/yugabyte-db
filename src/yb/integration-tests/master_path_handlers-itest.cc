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
#include "yb/master/mini_master.h"
#include "yb/util/curl_util.h"
#include "yb/util/net/sockaddr.h"

namespace yb {
namespace master {

class MasterPathHandlersItest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    Endpoint master_http_endpoint = cluster_->leader_mini_master()->bound_http_addr();
    master_http_url_ = "http://" + ToString(master_http_endpoint);
  }

  void DoTearDown() override {
    cluster_->Shutdown();
  }

 protected:
  void TestUrl(const std::string& query_path) {
    const std::string tables_url = master_http_url_ + query_path;
    faststring result;
    EasyCurl curl;
    ASSERT_OK(curl.FetchURL(tables_url, &result));
  }

 private:
  std::string master_http_url_;
};

TEST_F(MasterPathHandlersItest, TestMasterPathHandlers) {
  TestUrl("/table?id=1");
  TestUrl("/tablet-servers");
  TestUrl("/tables");
  TestUrl("/dump-entities");
  TestUrl("/cluster-config");
}

} // namespace master
} // namespace yb
