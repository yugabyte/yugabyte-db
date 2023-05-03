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

#include "yb/master/master_backup_service.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/master.h"
#include "yb/master/master_service_base-internal.h"

#define YB_MASTER_BACKUP_SERVICE_FORWARD_METHOD(r, data, method) \
  void MasterBackupServiceImpl::method( \
      const BOOST_PP_CAT(method, RequestPB)* req, \
      BOOST_PP_CAT(method, ResponsePB)* resp, \
      rpc::RpcContext rpc) { \
    HandleIn(req, resp, &rpc, &CatalogManager::method, \
             __FILE__, __LINE__, __func__, HoldCatalogLock::kTrue); \
  }

namespace yb {
namespace master {

MasterBackupServiceImpl::MasterBackupServiceImpl(Master* server)
    : MasterBackupIf(server->metric_entity()),
      MasterServiceBase(server) {
}

BOOST_PP_SEQ_FOR_EACH(YB_MASTER_BACKUP_SERVICE_FORWARD_METHOD, ~, YB_MASTER_BACKUP_SERVICE_METHODS)

} // namespace master
} // namespace yb
