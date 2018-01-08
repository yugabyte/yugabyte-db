//
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
//

#include "yb/client/tablet_rpc.h"

#include "yb/common/wire_protocol.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"

#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace client {
namespace internal {

TabletInvoker::TabletInvoker(bool consistent_prefix,
                             YBClient* client,
                             rpc::RpcCommand* command,
                             TabletRpc* rpc,
                             RemoteTablet* tablet,
                             rpc::RpcRetrier* retrier,
                             Trace* trace)
      : client_(client),
        command_(command),
        rpc_(rpc),
        tablet_(tablet),
        tablet_id_(tablet != nullptr ? tablet->tablet_id() : std::string()),
        retrier_(retrier),
        trace_(trace),
        consistent_prefix_(consistent_prefix) {}

void TabletInvoker::SelectTabletServerWithConsistentPrefix() {
  std::vector<RemoteTabletServer*> candidates;
  current_ts_ = client_->data_->SelectTServer(tablet_.get(),
                                              YBClient::ReplicaSelection::CLOSEST_REPLICA, {},
                                              &candidates);
  VLOG(1) << "Using tserver: " << yb::ToString(current_ts_);
}

void TabletInvoker::SelectTabletServer()  {
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
    VLOG(2) << "Tablet " << tablet_id_ << ": We have a follower for a leader: "
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
      VLOG(1) << "Tablet " << tablet_id_ << ": Previous leader failed. "
              << "Preemptively marking tserver " << current_ts_->ToString()
              << " as leader in the meta cache.";
      tablet_->MarkTServerAsLeader(current_ts_);
    }
  }
}

void TabletInvoker::Execute(const std::string& tablet_id) {
  if (tablet_id_.empty()) {
    if (!tablet_id.empty()) {
      tablet_id_ = tablet_id;
    } else {
      tablet_id_ = CHECK_NOTNULL(tablet_.get())->tablet_id();
    }
  }

  if (!tablet_) {
    client_->LookupTabletById(tablet_id_, retrier_->deadline(), &tablet_,
                              Bind(&TabletInvoker::InitialLookupTabletDone, Unretained(this)));
    return;
  }

  // Sets current_ts_.
  if (!consistent_prefix_) {
    SelectTabletServer();
  } else {
    SelectTabletServerWithConsistentPrefix();
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
    client_->LookupTabletById(tablet_id_,
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

  VLOG(2) << "Tablet " << tablet_id_ << ": Writing batch to replica "
          << current_ts_->ToString();

  rpc_->SendRpcToTserver();
}

void TabletInvoker::FailToNewReplica(const Status& reason) {
  VLOG(1) << "Failing " << command_->ToString() << " to a new replica: " << reason.ToString();

  bool found = !tablet_ || tablet_->MarkReplicaFailed(current_ts_, reason);
  if (!found) {
    // Its possible that current_ts_ is not part of replicas if RemoteTablet.Refresh() is invoked
    // which updates the set of replicas.
    LOG(WARNING) << "Tablet " << tablet_id_ << ": Unable to mark replica "
                 << current_ts_->ToString()
                 << " as failed. Replicas: " << tablet_->ReplicasAsString();
  }

  auto status = retrier_->DelayedRetry(command_, reason);
  LOG_IF(DFATAL, !status.ok()) << "Retry failed: " << status;
}

bool TabletInvoker::Done(Status* status) {
  TRACE_TO(trace_, "Done($0)", status->ToString(false));
  ADOPT_TRACE(trace_);

  if (status->IsAborted() || retrier_->finished()) {
    return true;
  }

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
  if (status->IsIllegalState() || status->IsServiceUnavailable() || status->IsAborted() ||
      status->IsLeaderNotReadyToServe() || status->IsLeaderHasNoLease() ||
      TabletNotFoundOnTServer(rpc_->response_error(), *status)) {
    const bool leader_is_not_ready =
        ErrorCode(rpc_->response_error()) ==
            tserver::TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE ||
        status->IsLeaderNotReadyToServe();

    // If the leader just is not ready - let's retry the same tserver.
    // Else the leader became a follower and must be reset on retry.
    if (!leader_is_not_ready) {
      followers_.insert(current_ts_);
    }

    if (status->IsIllegalState() || TabletNotFoundOnTServer(rpc_->response_error(), *status)) {
      FailToNewReplica(*status);
    } else {
      auto retry_status = retrier_->DelayedRetry(command_, *status);
      LOG_IF(DFATAL, !retry_status.ok()) << "Retry failed: " << retry_status;
    }
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
               tablet_id_,
               current_ts_string,
               retrier_->attempt_num()));
    LOG(WARNING) << status->ToString();
    rpc_->Failed(old_status);
  }

  return true;
}

void TabletInvoker::InitialLookupTabletDone(const Status& status) {
  VLOG(1) << "InitialLookupTabletDone(" << status << ")";

  if (status.ok()) {
    Execute(std::string());
  } else {
    command_->Finished(status);
  }
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
  VLOG(1) << "LookupTabletCb(" << status << ")";

  TRACE_TO(trace_, "LookupTabletCb($0)", status.ToString(false));

  // We should retry the RPC regardless of the outcome of the lookup, as
  // leader election doesn't depend on the existence of a master at all.
  // Unless we know that this status is persistent.
  // For instance if tablet was deleted, we would always receive "Not found".
  if (status.IsNotFound()) {
    command_->Finished(status);
    return;
  }

  // Retry() imposes a slight delay, which is desirable in a lookup loop,
  // but unnecessary the first time through. Seeing as leader failures are
  // rare, perhaps this doesn't matter.
  followers_.clear();
  auto retry_status = retrier_->DelayedRetry(command_, status);
  if (!retry_status.ok()) {
    command_->Finished(status);
  }
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
