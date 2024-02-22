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

#include "yb/master/catalog_manager.h"
#include "yb/master/master_test.service.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/test_async_rpc_manager.h"
#include "yb/master/ysql_backends_manager.h"

#include "yb/util/flags.h"

namespace yb {
namespace master {

namespace {

class MasterTestServiceImpl : public MasterServiceBase, public MasterTestIf {
 public:
  explicit MasterTestServiceImpl(Master* master)
      : MasterServiceBase(master), MasterTestIf(master->metric_entity()) {}

  MASTER_SERVICE_IMPL_ON_ALL_MASTERS(
      TestAsyncRpcManager,
      (TestRetry)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterTestService(Master* master) {
  return std::make_unique<MasterTestServiceImpl>(master);
}

} // namespace master
} // namespace yb
