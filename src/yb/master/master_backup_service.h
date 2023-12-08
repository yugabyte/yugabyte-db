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

#pragma once

#include "yb/master/master_backup.service.h"
#include "yb/master/master_service_base.h"

namespace yb {
namespace master {

#define YB_MASTER_BACKUP_SERVICE_METHODS \
  (CreateSnapshot)(ListSnapshots)(ListSnapshotRestorations)(RestoreSnapshot)(DeleteSnapshot) \
  (AbortSnapshotRestore)(ImportSnapshotMeta)(CreateSnapshotSchedule)(ListSnapshotSchedules) \
  (DeleteSnapshotSchedule)(EditSnapshotSchedule)(RestoreSnapshotSchedule)

#define YB_MASTER_BACKUP_SERVICE_METHOD_DECLARE(r, data, elem) \
  void elem( \
      const BOOST_PP_CAT(elem, RequestPB)* req, BOOST_PP_CAT(elem, ResponsePB)* resp, \
      rpc::RpcContext rpc) override;

// Implementation of the master backup service. See master_backup.proto.
class MasterBackupServiceImpl : public MasterBackupIf,
                                public MasterServiceBase {
 public:
  explicit MasterBackupServiceImpl(Master* server);

  BOOST_PP_SEQ_FOR_EACH(
      YB_MASTER_BACKUP_SERVICE_METHOD_DECLARE, ~, YB_MASTER_BACKUP_SERVICE_METHODS)

 private:
  DISALLOW_COPY_AND_ASSIGN(MasterBackupServiceImpl);
};

} // namespace master
} // namespace yb
