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

#include "yb/integration-tests/cdcsdk_test_base.h"

#include <string>

#include "yb/cdc/cdc_service.h"

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/integration-tests/cdc_test_util.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master-test-util.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/test_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

namespace yb {
using client::YBClient;

namespace cdc {
namespace enterprise {

void CDCSDKTestBase::TearDown() {
  YBTest::TearDown();

  LOG(INFO) << "Destroying cluster for CDCSDK";

  if (test_cluster()) {
    if (test_cluster_.pg_supervisor_) {
      test_cluster_.pg_supervisor_->Stop();
    }
    test_cluster_.mini_cluster_->Shutdown();
    test_cluster_.mini_cluster_.reset();
  }
  test_cluster_.client_.reset();
}

std::unique_ptr<CDCServiceProxy> CDCSDKTestBase::GetCdcProxy() {
  YBClient *client_ = test_client();
  const auto mini_server = test_cluster()->mini_tablet_servers().front();
  std::unique_ptr<CDCServiceProxy> proxy = std::make_unique<CDCServiceProxy>(
      &client_->proxy_cache(), HostPort::FromBoundEndpoint(mini_server->bound_rpc_addr()));
  return proxy;

}
} // namespace enterprise
} // namespace cdc
} // namespace yb
