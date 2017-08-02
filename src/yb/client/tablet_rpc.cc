//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/tablet_rpc.h"

#include "yb/common/wire_protocol.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"

#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace client {
namespace internal {

void TabletInvoker::Execute() {
  // Choose a destination TS according to the following algorithm:
  // 1. Select the leader, provided:
  //    a. One exists, and
  //    b. It hasn't failed, and
  //    c. It isn't currently marked as a follower.
  // 2. If there's no good leader select another replica, provided:
  //    a. It hasn't failed, and
  //    b. It hasn't rejected our write due to being a follower.
  // 3. Preemptively mark the replica we selected in step 2 as "leader" in the
  //    meta cache, so that our selection remains sticky until the next Master
  //    metadata refresh.
  // 4. If we're out of appropriate replicas, force a lookup to the master
  //    to fetch new consensus configuration information.
  // 5. When the lookup finishes, forget which replicas were followers and
  //    retry the write (i.e. goto 1).
  // 6. If we issue the write and it fails because the destination was a
  //    follower, remember that fact and retry the write (i.e. goto 1).
  // 7. Repeat steps 1-6 until the write succeeds, fails for other reasons,
  //    or the write's deadline expires.
  current_ts_ = tablet_->LeaderTServer();
  if (current_ts_ && ContainsKey(followers_, current_ts_)) {
    VLOG(2) << "Tablet " << tablet_->tablet_id() << ": We have a follower for a leader: "
            << current_ts_->ToString();

    // Mark the node as a follower in the cache so that on the next go-round,
    // LeaderTServer() will not return it as a leader unless a full metadata
    // refresh has occurred. This also avoids LookupTabletByKey() going into
    // "fast path" mode and not actually performing a metadata refresh from the
    // Master when it needs to.
    tablet_->MarkTServerAsFollower(current_ts_);
    current_ts_ = NULL;
  }
  if (!current_ts_) {
    // Try to "guess" the next leader.
    vector<RemoteTabletServer*> replicas;
    tablet_->GetRemoteTabletServers(&replicas);
    for (RemoteTabletServer* ts : replicas) {
      if (!ContainsKey(followers_, ts)) {
        current_ts_ = ts;
        break;
      }
    }
    if (current_ts_) {
      // Mark this next replica "preemptively" as the leader in the meta cache,
      // so we go to it first on the next write if writing was successful.
      VLOG(1) << "Tablet " << tablet_->tablet_id() << ": Previous leader failed. "
              << "Preemptively marking tserver " << current_ts_->ToString()
              << " as leader in the meta cache.";
      tablet_->MarkTServerAsLeader(current_ts_);
    }
  }

  // If we've tried all replicas, force a lookup to the master to find the
  // new leader. This relies on some properties of LookupTabletByKey():
  // 1. The fast path only works when there's a non-failed leader (which we
  //    know is untrue here).
  // 2. The slow path always fetches consensus configuration information and
  //    updates the looked-up tablet.
  // Put another way, we don't care about the lookup results at all; we're
  // just using it to fetch the latest consensus configuration information.
  //
  // TODO: When we support tablet splits, we should let the lookup shift
  // the write to another tablet (i.e. if it's since been split).
  if (!current_ts_) {
    client_->LookupTabletById(tablet_->tablet_id(),
                              retrier_->deadline(),
                              nullptr /* remote_tablet */,
                              Bind(&TabletInvoker::LookupTabletCb, Unretained(this)));
    return;
  }

  // Make sure we have a working proxy before sending out the RPC.
  current_ts_->InitProxy(client_, Bind(&TabletInvoker::InitTSProxyCb, Unretained(this)));
}

void TabletInvoker::InitTSProxyCb(const Status& status) {
  TRACE_TO(trace_, "InitTSProxyCb($0)", status.ToString(false));

  // Fail to a replica in the event of a DNS resolution failure.
  if (!status.ok()) {
    FailToNewReplica(status);
    return;
  }

  VLOG(2) << "Tablet " << tablet_->tablet_id() << ": Writing batch to replica "
          << current_ts_->ToString();

  rpc_->SendRpcToTserver();
}

void TabletInvoker::FailToNewReplica(const Status& reason) {
  VLOG(1) << "Failing " << command_->ToString() << " to a new replica: " << reason.ToString();

  bool found = tablet_->MarkReplicaFailed(current_ts_, reason);
  if (!found) {
    // Its possible that current_ts_ is not part of replicas if RemoteTablet.Refresh() is invoked
    // which updates the set of replicas.
    LOG(WARNING) << "Tablet " << tablet_->tablet_id() << ": Unable to mark replica "
                 << current_ts_->ToString()
                 << " as failed. Replicas: " << tablet_->ReplicasAsString();
  }

  retrier_->DelayedRetry(command_, reason);
}

bool TabletInvoker::Done(Status* status) {
  TRACE_TO(trace_, "Done($0)", status->ToString(false));
  ADOPT_TRACE(trace_);

  // Prefer early failures over controller failures.
  if (status->ok() && retrier_->HandleResponse(command_, status)) {
    return false;
  }

  // Failover to a replica in the event of any network failure.
  //
  // TODO: This is probably too harsh; some network failures should be
  // retried on the current replica.
  if (status->IsNetworkError()) {
    FailToNewReplica(*status);
    return false;
  }

  // Prefer controller failures over response failures.
  Status resp_error_status = ErrorStatus(rpc_->response_error());
  if (status->ok() && !resp_error_status.ok()) {
    *status = resp_error_status;
  }

  // Oops, we failed over to a replica that wasn't a LEADER. Unlikely as
  // we're using consensus configuration information from the master, but still possible
  // (e.g. leader restarted and became a FOLLOWER). Try again.
  //
  // TODO: IllegalState is obviously way too broad an error category for
  // this case.
  if (status->IsIllegalState() || status->IsServiceUnavailable() || status->IsAborted()) {
    const bool leader_is_not_ready = ErrorCode(rpc_->response_error()) ==
        tserver::TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE;

    // If the leader just is not ready - let's retry the same tserver.
    // Else the leader became a follower and must be reset on retry.
    if (!leader_is_not_ready) {
      followers_.insert(current_ts_);
    }

    retrier_->DelayedRetry(command_, *status);
    return false;
  }

  if (!status->ok()) {
    std::string current_ts_string;
    if (current_ts_) {
      current_ts_string = Format("on tablet server $0", *current_ts_);
    } else {
      current_ts_string = "(no tablet server available)";
    }
    Status old_status = std::move(*status);
    *status = old_status.CloneAndPrepend(
        Format("Failed $0 to tablet $1 $2 after $3 attempt(s)",
               command_->ToString(),
               tablet_->tablet_id(),
               current_ts_string,
               retrier_->attempt_num()));
    LOG(WARNING) << status->ToString();
    rpc_->Failed(old_status);
  }

  return true;
}

bool TabletInvoker::IsLocalCall() const {
  return (current_ts_ != nullptr &&
          current_ts_->proxy() != nullptr &&
          current_ts_->proxy()->IsServiceLocal());
}

std::shared_ptr<tserver::TabletServerServiceProxy> TabletInvoker::proxy() const {
  return current_ts_->proxy();
}

void TabletInvoker::LookupTabletCb(const Status& status) {
  TRACE_TO(trace_, "LookupTabletCb($0)", status.ToString(false));
  // We should retry the RPC regardless of the outcome of the lookup, as
  // leader election doesn't depend on the existence of a master at all.
  //
  // Retry() imposes a slight delay, which is desirable in a lookup loop,
  // but unnecessary the first time through. Seeing as leader failures are
  // rare, perhaps this doesn't matter.
  followers_.clear();
  retrier_->DelayedRetry(command_, status);
}

Status ErrorStatus(const tserver::TabletServerErrorPB* error) {
  return error == nullptr ? Status::OK()
                          : StatusFromPB(error->status());
}

tserver::TabletServerErrorPB_Code ErrorCode(const tserver::TabletServerErrorPB* error) {
  return error == nullptr ? tserver::TabletServerErrorPB::UNKNOWN_ERROR
                          : error->code();
}

} // namespace internal
} // namespace client
} // namespace yb
