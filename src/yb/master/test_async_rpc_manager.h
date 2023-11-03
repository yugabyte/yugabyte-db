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
#pragma once


#include "yb/master/async_rpc_tasks.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/master_util.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/service_util.h"
#include "yb/util/status_callback.h"

namespace yb {
namespace master {

class Master;
class CatalogManager;

class TestAsyncRpcManager {
 public:
  explicit TestAsyncRpcManager(Master* master, CatalogManagerIf* catalog_manager)
      : master_(DCHECK_NOTNULL(master)),
        catalog_manager_(DCHECK_NOTNULL(catalog_manager)) {}


  Status SendMasterTestRetryRequest(
    const consensus::RaftPeerPB& peer, const int32_t num_retries, StdStatusCallback callback);

  Status SendTsTestRetryRequest(
      const PeerId& ts_id, int32_t num_retries, StdStatusCallback callback);

  Status TestRetry(const master::TestRetryRequestPB* req, master::TestRetryResponsePB* resp);

 private:
  Master* master_;
  CatalogManagerIf* catalog_manager_;
  std::atomic<int32_t> num_test_retry_calls_{0};

  DISALLOW_COPY_AND_ASSIGN(TestAsyncRpcManager);
};

} // namespace master
} // namespace yb
