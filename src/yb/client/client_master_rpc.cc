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

#include "yb/client/client_master_rpc.h"

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_header.pb.h"

#include "yb/util/logging.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_int64(reset_master_leader_timeout_ms, 15000,
             "Timeout to reset master leader in milliseconds.");

using namespace std::literals;
using namespace std::placeholders;

namespace yb {
namespace client {
namespace internal {

ClientMasterRpcBase::ClientMasterRpcBase(YBClient::Data* client_data, CoarseTimePoint deadline)
    : Rpc(deadline, client_data->messenger_, client_data->proxy_cache_.get()),
      client_data_(DCHECK_NOTNULL(client_data)),
      retained_self_(client_data->rpcs_.InvalidHandle()) {
}

void ClientMasterRpcBase::SendRpc() {
  DCHECK(retained_self_ != client_data_->rpcs_.InvalidHandle());

  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS_FORMAT(TimedOut, "Request $0 timed out after deadline expired", *this));
    return;
  }

  auto rpc_deadline = now + client_data_->default_rpc_timeout_;
  mutable_retrier()->mutable_controller()->set_deadline(
      std::min(rpc_deadline, retrier().deadline()));
  CallRemoteMethod();
}

void ClientMasterRpcBase::ResetMasterLeader(Retry retry) {
  client_data_->SetMasterServerProxyAsync(
      retry ? retrier().deadline()
            : CoarseMonoClock::now() + FLAGS_reset_master_leader_timeout_ms * 1ms,
      false /* skip_resolution */,
      true, /* wait for leader election */
      retry ? std::bind(&ClientMasterRpcBase::NewLeaderMasterDeterminedCb, this, _1)
            : StdStatusCallback([](auto){}));
}

void ClientMasterRpcBase::NewLeaderMasterDeterminedCb(const Status& status) {
  if (status.ok()) {
    mutable_retrier()->mutable_controller()->Reset();
    SendRpc();
  } else {
    LOG(WARNING) << "Failed to determine new Master: " << status.ToString();
    ScheduleRetry(status);
  }
}

void ClientMasterRpcBase::Finished(const Status& status) {
  ADOPT_TRACE(trace_.get());
  auto resp_status = ResponseStatus();
  if (status.ok() && !resp_status.ok()) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 1) << "Failed, got resp error: " << resp_status;
  } else if (!status.ok()) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 1) << "Failed: " << status;
  }

  Status new_status = status;
  if (new_status.ok() &&
      mutable_retrier()->HandleResponse(this, &new_status, rpc::RetryWhenBusy::kFalse)) {
    return;
  }

  if (new_status.ok() && !resp_status.ok()) {
    master::MasterError master_error(resp_status);
    if (master_error == master::MasterErrorPB::NOT_THE_LEADER ||
        master_error == master::MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED) {
      LOG(WARNING) << ToString() << ": Leader Master has changed ("
                   << client_data_->leader_master_hostport().ToString()
                   << " is no longer the leader), re-trying...";
      ResetMasterLeader(Retry::kTrue);
      return;
    }

    if (resp_status.IsLeaderNotReadyToServe() || resp_status.IsLeaderHasNoLease()) {
      LOG(WARNING) << ToString() << ": Leader Master "
                   << client_data_->leader_master_hostport().ToString()
                   << " does not have a valid exclusive lease: "
                   << resp_status << ", re-trying...";
      ResetMasterLeader(Retry::kTrue);
      return;
    }
    VLOG(2) << "resp.error().status()=" << resp_status;
    new_status = resp_status;
  }

  if (new_status.IsTimedOut()) {
    auto now = CoarseMonoClock::Now();
    if (now < retrier().deadline()) {
      LOG(WARNING) << ToString() << ": Leader Master ("
          << client_data_->leader_master_hostport().ToString()
          << ") timed out, " << MonoDelta(retrier().deadline() - now) << " left, re-trying...";
      ResetMasterLeader(Retry::kTrue);
      return;
    } else {
      // Operation deadline expired during this latest RPC.
      new_status = STATUS_FORMAT(
          TimedOut, "$0 timed out after deadline expired, passed $1 of $2",
          *this, now - retrier().start(), retrier().deadline() - retrier().start());
      // If RPC start time >= deadline, this RPC was doomed to timeout and timeout reason is not in
      // master.
      if (retrier().start() < retrier().deadline()) {
        ResetMasterLeader(Retry::kFalse);
      }
    }
  }

  if (new_status.IsNetworkError() || new_status.IsRemoteError()) {
    if (rpc::RpcError(new_status) != rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD) {
      LOG(WARNING) << ToString() << ": Encountered a network error from the Master("
                   << client_data_->leader_master_hostport().ToString()
                   << "): " << new_status.ToString() << ", retrying...";
      ResetMasterLeader(Retry::kTrue);
      return;
    }
  }

  if (ShouldRetry(new_status)) {
    if (CoarseMonoClock::Now() > retrier().deadline()) {
      if (new_status.ok()) {
        new_status = STATUS(TimedOut, ToString() + " timed out");
      }
      LOG(WARNING) << new_status;
    } else {
      auto backoff_strategy = rpc::BackoffStrategy::kLinear;
      if (rpc::RpcError(new_status) == rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY) {
        backoff_strategy = rpc::BackoffStrategy::kExponential;
      }
      new_status = mutable_retrier()->DelayedRetry(this, new_status, backoff_strategy);
      if (new_status.ok()) {
        return;
      }
    }
  }

  auto retained_self = client_data_->rpcs_.Unregister(&retained_self_);

  ProcessResponse(new_status);
}

std::string ClientMasterRpcBase::LogPrefix() const {
  return AsString(this) + ": ";
}

} // namespace internal
} // namespace client
} // namespace yb
