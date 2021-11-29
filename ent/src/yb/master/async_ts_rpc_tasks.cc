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

#include "yb/master/async_ts_rpc_tasks.h"

#include "yb/util/logging.h"
#include "yb/master/master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/tserver/backup.proxy.h"

namespace yb {
namespace master {
namespace enterprise {

using std::shared_ptr;

Status RetryingTSRpcTask::ResetTSProxy() {
  RETURN_NOT_OK(super::ResetTSProxy());

  shared_ptr<tserver::TabletServerBackupServiceProxy> ts_backup_proxy;
  RETURN_NOT_OK(target_ts_desc_->GetProxy(&ts_backup_proxy));
  ts_backup_proxy_.swap(ts_backup_proxy);

  return Status::OK();
}

} // namespace enterprise
} // namespace master
} // namespace yb
