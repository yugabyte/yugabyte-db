// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/remote_bootstrap_service.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/log_util.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_peer_lookup.h"

#include "yb/util/crc.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using namespace std::literals;

// Note, this macro assumes the existence of a local var named 'context'.
#define RPC_RETURN_APP_ERROR(app_err, message, s) \
  do { \
    SetupErrorAndRespond(&context, app_err, message, s); \
    return; \
  } while (false)

#define RPC_RETURN_NOT_OK(expr, app_err, message) \
  do { \
    Status s = (expr); \
    if (!s.ok()) { \
      RPC_RETURN_APP_ERROR(app_err, message, s); \
    } \
  } while (false)

DEFINE_uint64(remote_bootstrap_idle_timeout_ms, 2 * yb::MonoTime::kMillisecondsPerHour,
              "Amount of time without activity before a remote bootstrap "
              "session will expire, in millis");
TAG_FLAG(remote_bootstrap_idle_timeout_ms, hidden);

DEFINE_uint64(remote_bootstrap_timeout_poll_period_ms, 10000,
              "How often the remote_bootstrap service polls for expired "
              "remote bootstrap sessions, in millis");
TAG_FLAG(remote_bootstrap_timeout_poll_period_ms, hidden);

DEFINE_test_flag(double, fault_crash_on_handle_rb_fetch_data, 0.0,
                 "Fraction of the time when the tablet will crash while "
                 "servicing a RemoteBootstrapService FetchData() RPC call.");

DEFINE_test_flag(uint64, inject_latency_before_change_role_secs, 0,
                 "Number of seconds to sleep before we call ChangeRole.");

DEFINE_test_flag(bool, skip_change_role, false,
                 "When set, we don't call ChangeRole after successfully finishing a remote "
                 "bootstrap.");

DEFINE_test_flag(uint64, inject_latency_before_fetch_data_secs, 0,
                 "Number of seconds to sleep before we call FetchData.");

DEFINE_test_flag(double, fault_crash_leader_before_changing_role, 0.0,
                 "The leader will crash before changing the role (from PRE_VOTER or PRE_OBSERVER "
                 "to VOTER or OBSERVER respectively) of the tablet server it is remote "
                 "bootstrapping.");

DEFINE_test_flag(double, fault_crash_leader_after_changing_role, 0.0,
                 "The leader will crash after successfully sending a ChangeConfig (CHANGE_ROLE "
                 "from PRE_VOTER or PRE_OBSERVER to VOTER or OBSERVER respectively) for the tablet "
                 "server it is remote bootstrapping, but before it sends a success response.");

DEFINE_uint64(remote_bootstrap_change_role_timeout_ms, 15000,
              "Timeout for change role operation during remote bootstrap.");

namespace yb {
namespace tserver {

using crc::Crc32c;
using strings::Substitute;
using tablet::TabletPeer;

static void SetupErrorAndRespond(rpc::RpcContext* context,
                                 RemoteBootstrapErrorPB::Code code,
                                 const string& message,
                                 const Status& s) {
  LOG(WARNING) << "Error handling RemoteBootstrapService RPC request from "
               << context->requestor_string() << ": "
               << s.ToString();
  RemoteBootstrapErrorPB error;
  StatusToPB(s, error.mutable_status());
  error.set_code(code);
  context->RespondApplicationError(RemoteBootstrapErrorPB::remote_bootstrap_error_ext.number(),
                                   message, error);
}

RemoteBootstrapServiceImpl::RemoteBootstrapServiceImpl(
    FsManager* fs_manager,
    TabletPeerLookupIf* tablet_peer_lookup,
    const scoped_refptr<MetricEntity>& metric_entity)
    : RemoteBootstrapServiceIf(metric_entity),
      fs_manager_(CHECK_NOTNULL(fs_manager)),
      tablet_peer_lookup_(CHECK_NOTNULL(tablet_peer_lookup)),
      shutdown_latch_(1) {
  CHECK_OK(Thread::Create("remote-bootstrap", "rb-session-exp",
                          &RemoteBootstrapServiceImpl::EndExpiredSessions, this,
                          &session_expiration_thread_));
}

RemoteBootstrapServiceImpl::~RemoteBootstrapServiceImpl() {
}

void RemoteBootstrapServiceImpl::BeginRemoteBootstrapSession(
        const BeginRemoteBootstrapSessionRequestPB* req,
        BeginRemoteBootstrapSessionResponsePB* resp,
        rpc::RpcContext context) {
  const string& requestor_uuid = req->requestor_uuid();
  const string& tablet_id = req->tablet_id();
  // For now, we use the requestor_uuid with the tablet id as the session id,
  // but there is no guarantee this will not change in the future.
  MonoTime now = MonoTime::Now();
  const string session_id = Substitute("$0-$1-$2", requestor_uuid, tablet_id, now.ToString());

  std::shared_ptr<TabletPeer> tablet_peer;
  RPC_RETURN_NOT_OK(tablet_peer_lookup_->GetTabletPeer(tablet_id, &tablet_peer),
                    RemoteBootstrapErrorPB::TABLET_NOT_FOUND,
                    Substitute("Unable to find specified tablet: $0", tablet_id));
  RPC_RETURN_NOT_OK(tablet_peer->CheckRunning(),
                    RemoteBootstrapErrorPB::TABLET_NOT_FOUND,
                    Substitute("Tablet is not running yet: $0", tablet_id));

  scoped_refptr<RemoteBootstrapSession> session;
  {
    std::lock_guard<std::mutex> l(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      LOG(INFO) << "Beginning new remote bootstrap session on tablet " << tablet_id
                << " from peer " << requestor_uuid << " at " << context.requestor_string()
                << ": session id = " << session_id;
      session.reset(new RemoteBootstrapSession(
          tablet_peer, session_id, requestor_uuid, &nsessions_));
      it = sessions_.emplace(session_id, SessionData{session, CoarseTimePoint()}).first;
      auto new_nsessions = nsessions_.fetch_add(1, std::memory_order_acq_rel) + 1;
      LOG_IF(DFATAL, implicit_cast<size_t>(new_nsessions) != sessions_.size())
          << "nsessions_ " << new_nsessions << " !=  number of sessions " << sessions_.size();
    } else {
      session = it->second.session;
      LOG(INFO) << "Re-initializing existing remote bootstrap session on tablet " << tablet_id
                << " from peer " << requestor_uuid << " at " << context.requestor_string()
                << ": session id = " << session_id;
    }
    it->second.ResetExpiration();
  }

  RPC_RETURN_NOT_OK(session->Init(),
                    RemoteBootstrapErrorPB::UNKNOWN_ERROR,
                    Substitute("Error initializing remote bootstrap session for tablet $0",
                               tablet_id));

  resp->set_session_id(session_id);
  resp->set_session_idle_timeout_millis(FLAGS_remote_bootstrap_idle_timeout_ms);
  resp->mutable_superblock()->CopyFrom(session->tablet_superblock());
  resp->mutable_initial_committed_cstate()->CopyFrom(session->initial_committed_cstate());

  auto const& log_segments = session->log_segments();
  resp->mutable_deprecated_wal_segment_seqnos()->Reserve(narrow_cast<int>(log_segments.size()));
  for (const scoped_refptr<log::ReadableLogSegment>& segment : log_segments) {
    resp->add_deprecated_wal_segment_seqnos(segment->header().sequence_number());
  }
  if (!log_segments.empty()) {
    resp->set_first_wal_segment_seqno(log_segments.front()->header().sequence_number());
  }

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::CheckSessionActive(
        const CheckRemoteBootstrapSessionActiveRequestPB* req,
        CheckRemoteBootstrapSessionActiveResponsePB* resp,
        rpc::RpcContext context) {
  // Look up and validate remote bootstrap session.
  std::lock_guard<std::mutex> l(sessions_mutex_);
  auto it = sessions_.find(req->session_id());
  if (it != sessions_.end()) {
    if (req->keepalive()) {
      it->second.ResetExpiration();
    }
    resp->set_session_is_active(true);
    context.RespondSuccess();
  } else {
    resp->set_session_is_active(false);
    context.RespondSuccess();
  }
}

void RemoteBootstrapServiceImpl::FetchData(const FetchDataRequestPB* req,
                                           FetchDataResponsePB* resp,
                                           rpc::RpcContext context) {
  if (PREDICT_FALSE(FLAGS_TEST_inject_latency_before_fetch_data_secs)) {
    LOG(INFO) << "Injecting FetchData latency for test";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_inject_latency_before_fetch_data_secs));
  }

  const string& session_id = req->session_id();

  // Look up and validate remote bootstrap session.
  scoped_refptr<RemoteBootstrapSession> session;
  {
    std::lock_guard<std::mutex> l(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      RPC_RETURN_APP_ERROR(
          RemoteBootstrapErrorPB::NO_SESSION, "No such session",
          STATUS_FORMAT(NotFound, "Fetch data for unknown sessions id: $0", session_id));
    }
    it->second.ResetExpiration();
    session = it->second.session;
  }

  session->EnsureRateLimiterIsInitialized();

  MAYBE_FAULT(FLAGS_TEST_fault_crash_on_handle_rb_fetch_data);

  int64_t rate_limit = session->rate_limiter().GetMaxSizeForNextTransmission();
  VLOG(3) << " rate limiter max len: " << rate_limit;
  GetDataPieceInfo info = {
    .offset = req->offset(),
    .client_maxlen = rate_limit == 0 ? req->max_length() : std::min(req->max_length(), rate_limit),
    .data = std::string(),
    .data_size = 0,
    .error_code = RemoteBootstrapErrorPB::UNKNOWN_ERROR,
  };
  const DataIdPB& data_id = req->data_id();
  RPC_RETURN_NOT_OK(ValidateFetchRequestDataId(data_id, &info.error_code, session),
                    info.error_code, "Invalid DataId");

  RPC_RETURN_NOT_OK(session->GetDataPiece(data_id, &info),
                    info.error_code, "Unable to get piece of data file");

  session->rate_limiter().UpdateDataSizeAndMaybeSleep(info.data.size());
  uint32_t crc32 = Crc32c(info.data.data(), info.data.length());

  DataChunkPB* data_chunk = resp->mutable_chunk();
  *data_chunk->mutable_data() = std::move(info.data);
  data_chunk->set_total_data_length(info.data_size);
  data_chunk->set_offset(info.offset);

  // Calculate checksum.
  data_chunk->set_crc32(crc32);
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::EndRemoteBootstrapSession(
        const EndRemoteBootstrapSessionRequestPB* req,
        EndRemoteBootstrapSessionResponsePB* resp,
        rpc::RpcContext context) {
  {
    std::lock_guard<std::mutex> l(sessions_mutex_);
    RemoteBootstrapErrorPB::Code app_error;
    RPC_RETURN_NOT_OK(DoEndRemoteBootstrapSession(
                          req->session_id(), req->is_success(), &app_error),
                      app_error, "No such session");
    LOG(INFO) << "Request end of remote bootstrap session " << req->session_id()
              << " received from " << context.requestor_string();

    if (!req->keep_session()) {
      RemoveSession(req->session_id());
    } else {
      resp->set_session_kept(true);
    }
  }
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::RemoveSession(
        const RemoveSessionRequestPB* req,
        RemoveSessionResponsePB* resp,
        rpc::RpcContext context) {
  {
    std::lock_guard<std::mutex> l(sessions_mutex_);
    RemoveSession(req->session_id());
  }
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::RemoveSession(const std::string& session_id) {
  // Remove the session from the map.
  // It will get destroyed once there are no outstanding refs.
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    LOG(WARNING) << "Attempt to remove session with unknown id: " << session_id;
    return;
  }
  LOG(INFO) << "Removing remote bootstrap session " << session_id << " on tablet "
            << session_id << " with peer " << it->second.session->requestor_uuid();
  sessions_.erase(it);
  nsessions_.fetch_sub(1, std::memory_order_acq_rel);
}

void RemoteBootstrapServiceImpl::Shutdown() {
  shutdown_latch_.CountDown();
  session_expiration_thread_->Join();

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  // Destroy all remote bootstrap sessions.
  std::vector<string> session_ids;
  session_ids.reserve(sessions_.size());
  for (const auto& entry : sessions_) {
    session_ids.push_back(entry.first);
  }
  for (const string& session_id : session_ids) {
    LOG(INFO) << "Destroying remote bootstrap session " << session_id << " due to service shutdown";
    RemoteBootstrapErrorPB::Code app_error;
    CHECK_OK(DoEndRemoteBootstrapSession(session_id, false, &app_error));
  }
}

Status RemoteBootstrapServiceImpl::ValidateFetchRequestDataId(
        const DataIdPB& data_id,
        RemoteBootstrapErrorPB::Code* app_error,
        const scoped_refptr<RemoteBootstrapSession>& session) const {
  int num_set = data_id.has_wal_segment_seqno() + data_id.has_file_name();
  if (PREDICT_FALSE(num_set != 1)) {
    *app_error = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
    return STATUS(InvalidArgument,
        Substitute("Only one of segment sequence number, and file name can be specified. "
                   "DataTypeID: $0", data_id.ShortDebugString()));
  }

  return session->ValidateDataId(data_id);
}

void RemoteBootstrapServiceImpl::SessionData::ResetExpiration() {
  expiration = CoarseMonoClock::now() + FLAGS_remote_bootstrap_idle_timeout_ms * 1ms;
}

Status RemoteBootstrapServiceImpl::DoEndRemoteBootstrapSession(
        const std::string& session_id,
        bool session_succeeded,
        RemoteBootstrapErrorPB::Code* app_error) {
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    *app_error = RemoteBootstrapErrorPB::NO_SESSION;
    return STATUS_FORMAT(NotFound, "End of unknown session id: $0", session_id);
  }
  auto session = it->second.session;

  if (session_succeeded || session->Succeeded()) {
    session->SetSuccess();

    if (PREDICT_FALSE(FLAGS_TEST_inject_latency_before_change_role_secs)) {
      LOG(INFO) << "Injecting latency for test";
      SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_inject_latency_before_change_role_secs));
    }

    if (PREDICT_FALSE(FLAGS_TEST_skip_change_role)) {
      LOG(INFO) << "Not changing role for " << session->requestor_uuid()
                << " because flag FLAGS_TEST_skip_change_role is set";
      return Status::OK();
    }

    MAYBE_FAULT(FLAGS_TEST_fault_crash_leader_before_changing_role);

    MonoTime deadline =
        MonoTime::Now() +
        MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_change_role_timeout_ms);
    for (;;) {
      Status status = session->ChangeRole();
      if (status.ok()) {
        LOG(INFO) << "ChangeRole succeeded for bootstrap session " << session_id;
        MAYBE_FAULT(FLAGS_TEST_fault_crash_leader_after_changing_role);
        break;
      }
      LOG(WARNING) << "ChangeRole failed for bootstrap session " << session_id
                   << ", error : " << status;
      if (!status.IsLeaderHasNoLease() || MonoTime::Now() >= deadline) {
        it->second.ResetExpiration();
        return Status::OK();
      }
    }
  } else {
    LOG(ERROR) << "Remote bootstrap session " << session_id << " on tablet " << session->tablet_id()
               << " with peer " << session->requestor_uuid() << " failed. session_succeeded = "
               << session_succeeded;
  }

  return Status::OK();
}

void RemoteBootstrapServiceImpl::EndExpiredSessions() {
  do {
    std::lock_guard<std::mutex> l(sessions_mutex_);
    auto now = CoarseMonoClock::Now();

    std::vector<string> expired_session_ids;
    for (const auto& entry : sessions_) {
      if (entry.second.expiration < now) {
        expired_session_ids.push_back(entry.first);
      }
    }
    for (const string& session_id : expired_session_ids) {
      LOG(INFO) << "Remote bootstrap session " << session_id
                << " has expired. Terminating session.";
      RemoteBootstrapErrorPB::Code app_error;
      CHECK_OK(DoEndRemoteBootstrapSession(session_id, false, &app_error));
      RemoveSession(session_id);
    }
  } while (!shutdown_latch_.WaitFor(MonoDelta::FromMilliseconds(
                                    FLAGS_remote_bootstrap_timeout_poll_period_ms)));
}

} // namespace tserver
} // namespace yb
