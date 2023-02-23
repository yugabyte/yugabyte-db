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

#include "yb/tserver/backup.service.h"

namespace yb {
namespace tserver {

class TSTabletManager;

class TabletServiceBackupImpl : public TabletServerBackupServiceIf {
 public:
  TabletServiceBackupImpl(TSTabletManager* tablet_manager,
                          const scoped_refptr<MetricEntity>& metric_entity);

  virtual void TabletSnapshotOp(const TabletSnapshotOpRequestPB* req,
                                TabletSnapshotOpResponsePB* resp,
                                rpc::RpcContext context) override;
 private:
  TSTabletManager* tablet_manager_;
};

}  // namespace tserver
}  // namespace yb
