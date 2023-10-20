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

#include "yb/master/async_flush_tablets_task.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/flush_manager.h"
#include "yb/master/master.h"

#include "yb/tserver/tserver_admin.proxy.h"

namespace yb {
namespace master {

using std::string;
using std::vector;
using tserver::TabletServerErrorPB;

////////////////////////////////////////////////////////////
// AsyncFlushTablets
////////////////////////////////////////////////////////////
AsyncFlushTablets::AsyncFlushTablets(Master *master,
                                     ThreadPool* callback_pool,
                                     const TabletServerId& ts_uuid,
                                     const scoped_refptr<TableInfo>& table,
                                     const vector<TabletId>& tablet_ids,
                                     const FlushRequestId& flush_id,
                                     bool is_compaction,
                                     bool regular_only,
                                     LeaderEpoch epoch)
: RetrySpecificTSRpcTask(master, callback_pool, ts_uuid, table, std::move(epoch),
                             /* async_task_throttler */ nullptr),
      tablet_ids_(tablet_ids),
      flush_id_(flush_id),
      is_compaction_(is_compaction),
      regular_only_(regular_only) {
}

string AsyncFlushTablets::description() const {
  return Format("$0 Flush Tablets RPC", permanent_uuid());
}

TabletServerId AsyncFlushTablets::permanent_uuid() const {
  return permanent_uuid_;
}

void AsyncFlushTablets::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error.
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG(WARNING) << "TS " << permanent_uuid() << ": flush tablets failed because tablet "
                     << resp_.failed_tablet_id() << " was not found. "
                     << "No further retry: " << status.ToString();
        TransitionToCompleteState();
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": flush tablets failed: "
                     << status.ToString();
    }
  } else {
    TransitionToCompleteState();
    VLOG(1) << "TS " << permanent_uuid() << ": flush tablets complete";
  }

  if (state() == server::MonitoredTaskState::kComplete) {
    // TODO: this class should not know CatalogManager API,
    //       remove circular dependency between classes.
    master_->flush_manager()->HandleFlushTabletsResponse(
        flush_id_, permanent_uuid_,
        resp_.has_error() ? StatusFromPB(resp_.error().status()) : Status::OK());
  } else {
    VLOG(1) << "FlushTablets task is not completed";
  }
}

bool AsyncFlushTablets::SendRequest(int attempt) {
  tserver::FlushTabletsRequestPB req;
  req.set_dest_uuid(permanent_uuid_);
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());
  req.set_operation(is_compaction_ ? tserver::FlushTabletsRequestPB::COMPACT
                                   : tserver::FlushTabletsRequestPB::FLUSH);

  for (const TabletId& id : tablet_ids_) {
    req.add_tablet_ids(id);
  }
  req.set_regular_only(regular_only_);

  ts_admin_proxy_->FlushTabletsAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send flush tablets request to " << permanent_uuid_
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

} // namespace master
} // namespace yb
