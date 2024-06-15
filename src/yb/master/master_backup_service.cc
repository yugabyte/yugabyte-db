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

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.service.h"
#include "yb/master/master_service_base-internal.h"


namespace yb {
namespace master {

namespace {

// Implementation of the master backup service. See master_backup.proto.
class MasterBackupServiceImpl : public MasterBackupIf, public MasterServiceBase {
 public:
  explicit MasterBackupServiceImpl(Master* master)
      : MasterBackupIf(master->metric_entity()), MasterServiceBase(master) {}

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      CatalogManager,
      (CreateSnapshot)
      (ListSnapshots)
      (ListSnapshotRestorations)
      (RestoreSnapshot)
      (DeleteSnapshot)
      (AbortSnapshotRestore)
      (ImportSnapshotMeta)
      (CreateSnapshotSchedule)
      (ListSnapshotSchedules)
      (DeleteSnapshotSchedule)
      (EditSnapshotSchedule)
      (RestoreSnapshotSchedule)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      CloneStateManager,
      (CloneNamespace)
      (ListClones)
  )

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterBackupServiceImpl);
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterBackupService(Master* master) {
  return std::make_unique<MasterBackupServiceImpl>(master);
}

} // namespace master
} // namespace yb
