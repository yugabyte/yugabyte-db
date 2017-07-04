// Copyright (c) YugaByte, Inc.

#include "yb/master/async_ts_rpc_tasks.h"

#include "yb/util/logging.h"
#include "yb/master/master.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/tserver/backup.proxy.h"

namespace yb {
namespace master {
namespace enterprise {

using std::shared_ptr;

Status RetryingTSRpcTask::ResetTSProxy() {
  RETURN_NOT_OK(super::ResetTSProxy());

  shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy;
  RETURN_NOT_OK(target_ts_desc_->GetProxy(master_->messenger(), &ts_backup_proxy));
  ts_backup_proxy_.swap(ts_backup_proxy);

  return Status::OK();
}

} // namespace enterprise
} // namespace master
} // namespace yb
