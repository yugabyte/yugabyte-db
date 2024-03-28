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
#include "yb/master/master_ddl.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"

namespace yb {
namespace master {

namespace {

class MasterDdlServiceImpl : public MasterServiceBase, public MasterDdlIf {
 public:
  explicit MasterDdlServiceImpl(Master* master)
      : MasterServiceBase(master), MasterDdlIf(master->metric_entity()) {}

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (AlterNamespace)
    (AlterTable)
    (BackfillIndex)
    (CreateNamespace)
    (CreateTable)
    (CreateTablegroup)
    (CreateUDType)
    (DeleteNamespace)
    (DeleteTable)
    (DeleteTablegroup)
    (DeleteUDType)
    (GetBackfillJobs)
    (GetBackfillStatus)
    (GetColocatedTabletSchema)
    (GetNamespaceInfo)
    (GetTableDiskSize)
    (GetTablegroupSchema)
    (GetTableSchema)
    (GetUDTypeInfo)
    (IsAlterTableDone)
    (IsCreateNamespaceDone)
    (IsCreateTableDone)
    (IsDeleteNamespaceDone)
    (IsDeleteTableDone)
    (IsTruncateTableDone)
    (IsYsqlDdlVerificationDone)
    (LaunchBackfillIndexForTable)
    (ListNamespaces)
    (ListTablegroups)
    (ListTables)
    (ListUDTypes)
    (ReportYsqlDdlTxnStatus)
    (TruncateTable)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterDdlService(Master* master) {
  return std::make_unique<MasterDdlServiceImpl>(master);
}

} // namespace master
} // namespace yb
