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

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/test_util.h"

namespace yb::master {

class MasterTabletServiceTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  virtual void SetUp() override;

  virtual MiniClusterOptions GetMiniClusterOptions();

  virtual void DoTearDown() override;

  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<rpc::Messenger> messenger_;
};

TEST_F(MasterTabletServiceTest, ListMasterServers) {
  tserver::TabletServerServiceProxy proxy(proxy_cache_.get(),
                                          cluster_->mini_master()->bound_rpc_addr());
  tserver::ListMasterServersRequestPB req;
  tserver::ListMasterServersResponsePB resp;
  rpc::RpcController rpc;
  auto status = proxy.ListMasterServers(req, &resp, &rpc);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Not implemented");
}

void MasterTabletServiceTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  cluster_ = std::make_unique<MiniCluster>(GetMiniClusterOptions());
  ASSERT_OK(cluster_->Start());
  messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("master_tablet_service_test").Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
}

void MasterTabletServiceTest::DoTearDown() {
  messenger_->Shutdown();
  YBMiniClusterTestBase::DoTearDown();
}

MiniClusterOptions MasterTabletServiceTest::GetMiniClusterOptions() { return MiniClusterOptions(); }

}  // namespace yb::master
