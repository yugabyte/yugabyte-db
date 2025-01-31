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
#include "yb/master/master_admin.service.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/test_async_rpc_manager.h"
#include "yb/master/ysql/ysql_manager.h"
#include "yb/master/ysql_backends_manager.h"

#include "yb/util/flags.h"

DEFINE_test_flag(bool, timeout_non_leader_master_rpcs, false,
                 "Timeout all master requests to non leader.");

namespace yb {
namespace master {

namespace {

class MasterAdminServiceImpl : public MasterServiceBase, public MasterAdminIf {
 public:
  explicit MasterAdminServiceImpl(Master* master)
      : MasterServiceBase(master), MasterAdminIf(master->metric_entity()) {}

  void IsInitDbDone(const IsInitDbDoneRequestPB* req,
                    IsInitDbDoneResponsePB* resp,
                    rpc::RpcContext rpc) override {
    HANDLE_ON_LEADER_WITHOUT_LOCK(CatalogManager, IsInitDbDone);
  }

  void DumpSysCatalogEntries(
      const DumpSysCatalogEntriesRequestPB* req, DumpSysCatalogEntriesResponsePB* resp,
      rpc::RpcContext rpc) override {
    // We cannot use HANDLE_ON_LEADER since leader_status may be bad.

    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
      return;
    }
    auto s = server_->catalog_manager_impl()->DumpSysCatalogEntries(req, resp, &rpc);
    CheckRespErrorOrSetUnknown(s, resp);
    rpc.RespondSuccess();
  }

  void WriteSysCatalogEntry(
      const WriteSysCatalogEntryRequestPB* req, WriteSysCatalogEntryResponsePB* resp,
      rpc::RpcContext rpc) override {
    // We cannot use HANDLE_ON_LEADER since leader_status may be bad.

    SCOPED_LEADER_SHARED_LOCK(l, server_->catalog_manager_impl());
    if (!l.CheckIsInitializedOrRespond(resp, &rpc)) {
      return;
    }
    auto s = server_->catalog_manager_impl()->WriteSysCatalogEntry(req, resp, &rpc);
    CheckRespErrorOrSetUnknown(s, resp);
    rpc.RespondSuccess();
  }

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      CatalogManager,
      (AddTransactionStatusTablet)
      (CheckIfPitrActive)
      (CompactSysCatalog)
      (GetCompactionStatus)
      (CreateTransactionStatusTable)
      (DdlLog)
      (DeleteNotServingTablet)
      (FlushSysCatalog)
      (SplitTablet)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      TabletSplitManager,
      (DisableTabletSplitting)
      (IsTabletSplittingComplete)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      FlushManager,
      (FlushTables)
      (IsFlushTablesDone)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      TabletHealthManager,
      (AreNodesSafeToTakeDown)
      (GetMasterHeartbeatDelays)
  )

  MASTER_SERVICE_IMPL_ON_ALL_MASTERS(
      YsqlBackendsManager,
      (AccessYsqlBackendsManagerTestRegister)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITHOUT_LOCK(
      YsqlBackendsManager,
      (WaitForYsqlBackendsCatalogVersion)
  )

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
      YsqlManager,
      (StartYsqlMajorCatalogUpgrade)
      (IsYsqlMajorCatalogUpgradeDone)
      (FinalizeYsqlMajorCatalogUpgrade)
      (RollbackYsqlMajorCatalogVersion)
      (GetYsqlMajorCatalogUpgradeState)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterAdminService(Master* master) {
  return std::make_unique<MasterAdminServiceImpl>(master);
}

} // namespace master
} // namespace yb
