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

#include "yb/master/master_dcl.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/permissions_manager.h"

namespace yb {
namespace master {

namespace {

class MasterDclServiceImpl : public MasterServiceBase, public MasterDclIf {
 public:
  explicit MasterDclServiceImpl(Master* master)
      : MasterServiceBase(master), MasterDclIf(master->metric_entity()) {}

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    PermissionsManager,
    (AlterRole)
    (CreateRole)
    (DeleteRole)
    (GetPermissions)
    (GrantRevokePermission)
    (GrantRevokeRole)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterDclService(Master* master) {
  return std::make_unique<MasterDclServiceImpl>(master);
}

} // namespace master
} // namespace yb
