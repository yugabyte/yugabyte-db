// Copyright (c) YugaByte, Inc.

#include "yb/master/async_snapshot_tasks.h"

#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/master/master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/catalog_manager.h"
#include "yb/rpc/messenger.h"

#include "yb/tserver/backup.proxy.h"

namespace yb {
namespace master {

using std::string;
using tserver::TabletServerErrorPB;

AsyncCreateTabletSnapshot::AsyncCreateTabletSnapshot(Master *master,
                                                     ThreadPool* callback_pool,
                                                     const scoped_refptr<TabletInfo>& tablet,
                                                     const string& snapshot_id)
  : enterprise::RetryingTSRpcTask(master,
                                  callback_pool,
                                  new PickLeaderReplica(tablet),
                                  tablet->table().get()),
    tablet_(tablet),
    snapshot_id_(snapshot_id) {
}

string AsyncCreateTabletSnapshot::description() const {
  return tablet_->ToString() + " Create Tablet Snapshot RPC";
}

string AsyncCreateTabletSnapshot::tablet_id() const {
  return tablet_->tablet_id();
}

string AsyncCreateTabletSnapshot::permanent_uuid() const {
  return target_ts_desc_ != nullptr ? target_ts_desc_->permanent_uuid() : "";
}

void AsyncCreateTabletSnapshot::HandleResponse(int attempt) {
  server::UpdateClock(resp_, master_->clock());

  if (resp_.has_error()) {
    Status status = StatusFromPB(resp_.error().status());

    // Do not retry on a fatal error
    switch (resp_.error().code()) {
      case TabletServerErrorPB::TABLET_NOT_FOUND:
        LOG(WARNING) << "TS " << permanent_uuid() << ": snapshot failed for tablet "
                     << tablet_->ToString() << " no further retry: " << status.ToString();
        PerformStateTransition(kStateRunning, kStateComplete);
        break;
      default:
        LOG(WARNING) << "TS " << permanent_uuid() << ": snapshot failed for tablet "
                     << tablet_->ToString() << ": " << status.ToString();
    }
  } else {
    PerformStateTransition(kStateRunning, kStateComplete);
    VLOG(1) << "TS " << permanent_uuid() << ": snapshot complete on tablet "
            << tablet_->ToString();
  }

  if (state() == kStateComplete) {
    CHECK_OK(master_->catalog_manager()->HandleCreateTabletSnapshotResponse(
        tablet_.get(), resp_.has_error()));
  } else {
    VLOG(1) << "CreateTabletSnapshot task is not completed";
  }
}

bool AsyncCreateTabletSnapshot::SendRequest(int attempt) {
  tserver::CreateTabletSnapshotRequestPB req;
  {
    auto l = tablet_->LockForRead();
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_snapshot_id(snapshot_id_);
  }
  req.set_propagated_hybrid_time(master_->clock()->Now().ToUint64());

  ts_backup_proxy_->CreateTabletSnapshotAsync(req, &resp_, &rpc_, BindRpcCallback());
  VLOG(1) << "Send create tablet snapshot request to " << permanent_uuid()
          << " (attempt " << attempt << "):\n"
          << req.DebugString();
  return true;
}

} // namespace master
} // namespace yb
