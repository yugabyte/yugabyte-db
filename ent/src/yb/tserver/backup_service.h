// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_BACKUP_SERVICE_H
#define ENT_SRC_YB_TSERVER_BACKUP_SERVICE_H

#include "yb/tserver/backup.service.h"

namespace yb {
namespace tserver {

class TSTabletManager;

class TabletServiceBackupImpl : public TabletServerBackupServiceIf {
 public:
  TabletServiceBackupImpl(TSTabletManager* tablet_manager,
                          const scoped_refptr<MetricEntity>& metric_entity);

  virtual void CreateTabletSnapshot(const CreateTabletSnapshotRequestPB* req,
                                    CreateTabletSnapshotResponsePB* resp,
                                    rpc::RpcContext context) override;
 private:
  TSTabletManager* tablet_manager_;
};

}  // namespace tserver
}  // namespace yb

#endif  // ENT_SRC_YB_TSERVER_BACKUP_SERVICE_H
