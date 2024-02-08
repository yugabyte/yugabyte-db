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
#include <iomanip>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/ref_counted.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_peer_lookup.h"

#include "yb/util/crc.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flags.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using std::string;

using namespace std::literals;

// Note, this macro assumes the existence of a local var named 'context'.
#define RPC_RETURN_APP_ERROR(app_err, message, s) \
  do { \
    SetupErrorAndRespond(&context, app_err, message, s); \
    return; \
  } while (false)

#define RPC_RETURN_NOT_OK(expr, app_err, message) \
  do { \
    auto&& s = (expr); \
    if (!s.ok()) { \
      RPC_RETURN_APP_ERROR(app_err, message, MoveStatus(s)); \
    } \
  } while (false)

DEFINE_RUNTIME_uint64(remote_bootstrap_idle_timeout_ms, 2 * yb::MonoTime::kMillisecondsPerHour,
              "Amount of time without activity before a remote bootstrap "
              "session will expire, in millis");
TAG_FLAG(remote_bootstrap_idle_timeout_ms, hidden);

DEFINE_UNKNOWN_uint64(remote_bootstrap_timeout_poll_period_ms, 10000,
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

DEFINE_test_flag(
    double, fault_crash_on_rbs_anchor_register, 0.0,
    "Fraction of the time when the peer will crash while "
    "servicing a RemoteBootstrapServiceImpl::RegisterLogAnchor() RPC call.");

DEFINE_UNKNOWN_uint64(remote_bootstrap_change_role_timeout_ms, 15000,
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
    const scoped_refptr<MetricEntity>& metric_entity,
    CloudInfoPB cloud_info,
    rpc::ProxyCache* proxy_cache)
    : RemoteBootstrapServiceIf(metric_entity),
      fs_manager_(CHECK_NOTNULL(fs_manager)),
      tablet_peer_lookup_(CHECK_NOTNULL(tablet_peer_lookup)),
      local_cloud_info_pb_(cloud_info),
      proxy_cache_(proxy_cache),
      shutdown_latch_(1) {
  CHECK_OK(Thread::Create("remote-bootstrap", "rb-session-exp",
                          &RemoteBootstrapServiceImpl::EndExpiredSessions, this,
                          &session_expiration_thread_));
}

RemoteBootstrapServiceImpl::~RemoteBootstrapServiceImpl() {
}

void RemoteBootstrapServiceImpl::BeginRemoteSnapshotTransferSession(
    const BeginRemoteSnapshotTransferSessionRequestPB* req,
    BeginRemoteSnapshotTransferSessionResponsePB* resp, rpc::RpcContext context) {
  RemoteBootstrapErrorPB::Code error_code;
  auto session_result = CreateRemoteSession(
      req,
      /* tablet_leader_conn_info = */ nullptr, context.requestor_string(), &error_code);

  RPC_RETURN_NOT_OK(session_result, error_code, session_result.status().message().ToString());
  auto session = *session_result;

  RPC_RETURN_NOT_OK(
      session->InitSnapshotTransferSession(), RemoteBootstrapErrorPB::UNKNOWN_ERROR,
      Substitute(
          "Error initializing remote snapshot transfer session for tablet $0", req->tablet_id()));

  resp->set_session_id(session->session_id());
  resp->set_session_idle_timeout_millis(FLAGS_remote_bootstrap_idle_timeout_ms);
  resp->mutable_superblock()->CopyFrom(session->tablet_superblock());

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::BeginRemoteBootstrapSession(
        const BeginRemoteBootstrapSessionRequestPB* req,
        BeginRemoteBootstrapSessionResponsePB* resp,
        rpc::RpcContext context) {
  RemoteBootstrapErrorPB::Code error_code;
  auto tablet_leader_conn_info =
      req->has_tablet_leader_conn_info() ? &req->tablet_leader_conn_info() : nullptr;
  auto session_result =
      CreateRemoteSession(req, tablet_leader_conn_info, context.requestor_string(), &error_code);

  RPC_RETURN_NOT_OK(session_result, error_code, session_result.status().message().ToString());
  auto session = *session_result;

  RPC_RETURN_NOT_OK(
      session->InitBootstrapSession(), RemoteBootstrapErrorPB::UNKNOWN_ERROR,
      Substitute("Error initializing remote bootstrap session for tablet $0", req->tablet_id()));

  resp->set_session_id(session->session_id());
  resp->set_session_idle_timeout_millis(FLAGS_remote_bootstrap_idle_timeout_ms);
  resp->mutable_superblock()->CopyFrom(session->tablet_superblock());
  resp->mutable_initial_committed_cstate()->CopyFrom(session->initial_committed_cstate());
  resp->set_retryable_requests_file_flushed(session->has_retryable_requests_file());

  auto const& log_segments = session->log_segments();
  resp->mutable_deprecated_wal_segment_seqnos()->Reserve(narrow_cast<int>(log_segments.size()));
  for (const scoped_refptr<log::ReadableLogSegment>& segment : log_segments) {
    resp->add_deprecated_wal_segment_seqnos(segment->header().sequence_number());
  }
  if (!log_segments.empty()) {
    const log::ReadableLogSegmentPtr& first_segment = CHECK_RESULT(log_segments.front());
    resp->set_first_wal_segment_seqno(first_segment->header().sequence_number());
  }

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::CheckRemoteBootstrapSessionActive(
    const CheckRemoteBootstrapSessionActiveRequestPB* req,
    CheckRemoteBootstrapSessionActiveResponsePB* resp,
    rpc::RpcContext context) {
  // Look up and validate remote bootstrap session.
  std::lock_guard l(sessions_mutex_);
  auto it = sessions_.find(req->session_id());
  if (it != sessions_.end()) {
    if (req->keepalive()) {
      RemoteBootstrapErrorPB::Code app_error;
      WARN_NOT_OK(it->second.ResetExpiration(&app_error),
                  Substitute("Refresh Log Anchor session failed"));
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
  const string& session_id = req->session_id();

  // Look up and validate remote bootstrap session.
  scoped_refptr<RemoteBootstrapSession> session;
  {
    std::lock_guard l(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      RPC_RETURN_APP_ERROR(
          RemoteBootstrapErrorPB::NO_SESSION, "No such session",
          STATUS_FORMAT(NotFound, "Fetch data for unknown sessions id: $0", session_id));
    }
    RemoteBootstrapErrorPB::Code app_error;
    RPC_RETURN_NOT_OK(it->second.ResetExpiration(&app_error),
                      app_error,
                      Substitute("Refresh Log Anchor session failed"));
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

  session->data_read_timer().resume();
  RPC_RETURN_NOT_OK(session->GetDataPiece(data_id, &info),
                    info.error_code, "Unable to get piece of data file");
  session->data_read_timer().stop();

  session->rate_limiter().UpdateDataSizeAndMaybeSleep(info.data.size());
  session->crc_compute_timer().resume();
  uint32_t crc32 = Crc32c(info.data.data(), info.data.length());
  session->crc_compute_timer().stop();

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
    std::lock_guard l(sessions_mutex_);
    RemoteBootstrapErrorPB::Code app_error;
    RPC_RETURN_NOT_OK(DoEndRemoteBootstrapSession(
                          req->session_id(), req->is_success(), &app_error),
                      app_error, "No such session");
    LOG(INFO) << "Request end of remote bootstrap session " << req->session_id()
              << " received from " << context.requestor_string();

    if (!req->keep_session()) {
      RemoveRemoteBootstrapSession(req->session_id());
    } else {
      resp->set_session_kept(true);
    }
  }
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::RemoveRemoteBootstrapSession(
    const RemoveRemoteBootstrapSessionRequestPB* req,
    RemoveRemoteBootstrapSessionResponsePB* resp,
    rpc::RpcContext context) {
  {
    std::lock_guard l(sessions_mutex_);
    RemoveRemoteBootstrapSession(req->session_id());
  }
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::RemoveRemoteBootstrapSession(const std::string& session_id) {
  // Remove the session from the map.
  // It will get destroyed once there are no outstanding refs.
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    LOG(WARNING) << "Attempt to remove session with unknown id: " << session_id;
    return;
  }
  LOG(INFO) << "Removing remote bootstrap session " << session_id << " on tablet " << session_id
            << " with peer " << it->second.session->requestor_uuid();
  sessions_.erase(it);
  nsessions_.fetch_sub(1, std::memory_order_acq_rel);
}

void RemoteBootstrapServiceImpl::Shutdown() {
  shutdown_latch_.CountDown();
  session_expiration_thread_->Join();

  {
    std::lock_guard lock(sessions_mutex_);
    // Destroy all remote bootstrap sessions.
    std::vector<string> session_ids;
    session_ids.reserve(sessions_.size());
    for (const auto& entry : sessions_) {
      session_ids.push_back(entry.first);
    }
    for (const string& session_id : session_ids) {
      LOG(INFO) << "Destroying remote bootstrap session " << session_id
                << " due to service shutdown";
      RemoteBootstrapErrorPB::Code app_error;
      CHECK_OK(DoEndRemoteBootstrapSession(session_id, false, &app_error));
    }
  }

  {
    std::lock_guard l(log_anchors_mutex_);
    std::vector<string> session_ids;
    session_ids.reserve(log_anchors_map_.size());
    for (const auto& entry : log_anchors_map_) {
      session_ids.push_back(entry.first);
    }

    for (const string& session_id : session_ids) {
      LOG(INFO) << "Destroying Remote Bootstrap Log Anchor session " << session_id
                << " due to service shutdown";
      RemoteBootstrapErrorPB::Code app_error;
      CHECK_OK(DoEndLogAnchorSession(session_id, &app_error));
    }
  }
}

template <typename Request>
Result<scoped_refptr<RemoteBootstrapSession>> RemoteBootstrapServiceImpl::CreateRemoteSession(
    const Request* req, const ServerRegistrationPB* tablet_leader_conn_info,
    const std::string& requestor_string, RemoteBootstrapErrorPB::Code* error_code) {
  const string& requestor_uuid = req->requestor_uuid();
  const TabletId& tablet_id = req->tablet_id();

  // For now, we use the requestor_uuid with the tablet id as the session id,
  // but there is no guarantee this will not change in the future.
  MonoTime now = MonoTime::Now();
  const string session_id = Substitute("$0-$1-$2", requestor_uuid, tablet_id, now.ToString());

  scoped_refptr<RemoteBootstrapAnchorClient> rbs_anchor_client(nullptr);
  if (tablet_leader_conn_info != nullptr) {
    rbs_anchor_client.reset(new RemoteBootstrapAnchorClient(
        requestor_uuid, session_id, proxy_cache_,
        HostPortFromPB(DesiredHostPort(
            tablet_leader_conn_info->broadcast_addresses(),
            tablet_leader_conn_info->private_rpc_addresses(), tablet_leader_conn_info->cloud_info(),
            local_cloud_info_pb_))));
  }

  auto tablet_peer_result = tablet_peer_lookup_->GetServingTablet(tablet_id);
  if (!tablet_peer_result.ok()) {
    *error_code = RemoteBootstrapErrorPB::TABLET_NOT_FOUND;
    return STATUS(NotFound, Substitute("Unable to find specified tablet: $0", tablet_id));
  }
  auto tablet_peer = std::move(*tablet_peer_result);
  auto s = tablet_peer->CheckRunning();
  if (!s.ok()) {
    *error_code = RemoteBootstrapErrorPB::TABLET_NOT_FOUND;
    return STATUS(NotFound, Substitute("Tablet is not running yet: $0", tablet_id));
  }

  scoped_refptr<RemoteBootstrapSession> session;
  {
    std::lock_guard l(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      LOG(INFO) << "Beginning new remote bootstrap session on tablet " << tablet_id << " from peer "
                << requestor_uuid << " at " << requestor_string << ": session id = " << session_id;
      session.reset(new RemoteBootstrapSession(
          tablet_peer, session_id, requestor_uuid, &nsessions_, rbs_anchor_client));
      it = sessions_.emplace(session_id, SessionData{session, CoarseTimePoint()}).first;
      auto new_nsessions = nsessions_.fetch_add(1, std::memory_order_acq_rel) + 1;
      LOG_IF(DFATAL, implicit_cast<size_t>(new_nsessions) != sessions_.size())
          << "nsessions_ " << new_nsessions << " !=  number of sessions " << sessions_.size();
    } else {
      session = it->second.session;
      LOG(INFO) << "Re-initializing existing remote bootstrap session on tablet " << tablet_id
                << " from peer " << requestor_uuid << " at " << requestor_string
                << ": session id = " << session_id;
    }

    s = it->second.ResetExpiration(error_code);
    if (!s.ok()) {
      return STATUS(RuntimeError, "Refresh Log Anchor session failed");
    }
  }

  return session;
}

Status RemoteBootstrapServiceImpl::ValidateFetchRequestDataId(
        const DataIdPB& data_id,
        RemoteBootstrapErrorPB::Code* app_error,
        const scoped_refptr<RemoteBootstrapSession>& session) const {
  int num_set = (data_id.type() == DataIdPB::RETRYABLE_REQUESTS)
      + data_id.has_wal_segment_seqno() + data_id.has_file_name();
  if (PREDICT_FALSE(num_set != 1)) {
    *app_error = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
    return STATUS(InvalidArgument,
        Substitute("Only one of segment sequence number, and file name can be specified. "
                   "DataTypeID: $0", data_id.ShortDebugString()));
  }

  return session->ValidateDataId(data_id);
}

Status RemoteBootstrapServiceImpl::SessionData::ResetExpiration(
    RemoteBootstrapErrorPB::Code* app_error) {
  expiration = CoarseMonoClock::now() + FLAGS_remote_bootstrap_idle_timeout_ms * 1ms;
  auto status = session->RefreshRemoteLogAnchorSessionAsync();
  if (!status.ok()) {
    *app_error = RemoteBootstrapErrorPB::REMOTE_LOG_ANCHOR_FAILURE;
  }
  return status;
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
    if(!session->Succeeded()) {
      session->SetSuccess();

      const auto total_bytes = session->rate_limiter().total_bytes();
      LOG(INFO) << std::fixed << std::setprecision(3) << "Remote bootstrap session with id "
        << session_id << " completed. Stats: Transmission rate: "
        << session->rate_limiter().GetRate() << ", RateLimiter total time slept: "
        << session->rate_limiter().total_time_slept() << ", Total bytes: "
        << total_bytes << ", Read rate "
        << (total_bytes / session->data_read_timer().elapsed().wall_millis())
        << " bytes/msec (Total ms: " << session->data_read_timer().elapsed().wall_millis()
        << "), CRC computation rate: "
        << (total_bytes / session->crc_compute_timer().elapsed().wall_millis()) << " bytes/msec"
        << "(Total ms: " << session->crc_compute_timer().elapsed().wall_millis() << ")";
    }

    if (PREDICT_FALSE(FLAGS_TEST_inject_latency_before_change_role_secs)) {
      LOG(INFO) << "Injecting latency for test";
      SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_inject_latency_before_change_role_secs));
    }

    if (PREDICT_FALSE(FLAGS_TEST_skip_change_role)) {
      LOG(INFO) << "Not changing role for " << session->requestor_uuid()
                << " because flag FLAGS_TEST_skip_change_role is set";
      return Status::OK();
    }

    if (session->ShouldChangeRole()) {
      MonoTime deadline = MonoTime::Now() + MonoDelta::FromMilliseconds(
                                                FLAGS_remote_bootstrap_change_role_timeout_ms);
      for (;;) {
        Status status = session->ChangeRole();
        if (status.ok()) {
          LOG(INFO) << "ChangeRole succeeded for bootstrap session " << session_id;
          break;
        }
        LOG(WARNING) << "ChangeRole failed for bootstrap session " << session_id
                     << ", error : " << status;
        if (!status.IsLeaderHasNoLease() || MonoTime::Now() >= deadline) {
          RemoteBootstrapErrorPB::Code app_error;
          return it->second.ResetExpiration(&app_error);
        }
      }
    }
  } else {
    LOG(ERROR) << "Remote bootstrap session " << session_id << " on tablet " << session->tablet_id()
               << " with peer " << session->requestor_uuid() << " failed. session_succeeded = "
               << session_succeeded;
  }

  return Status::OK();
}

RemoteBootstrapServiceImpl::LogAnchorSessionData::LogAnchorSessionData(
    const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
    const std::shared_ptr<log::LogAnchor>& log_anchor_ptr)
    : tablet_peer_(tablet_peer), log_anchor_ptr_(log_anchor_ptr) {
  // the timeout period for remote bootstrap anchor session should be >> remote bootstrap session
  // as we need to account for the additional latency for communication between the source serving
  // the rbs requests and the tablet leader peer.
  expiration_ = CoarseMonoClock::now() + FLAGS_remote_bootstrap_idle_timeout_ms * 2 * 1ms;
}

RemoteBootstrapServiceImpl::LogAnchorSessionData::~LogAnchorSessionData() {}

void RemoteBootstrapServiceImpl::LogAnchorSessionData::ResetExpiration() {
  // the timeout period for remote bootstrap anchor session should be >> remote bootstrap session
  // as we need to account for the additional latency for communication between the source serving
  // the rbs requests and the tablet leader peer.
  expiration_ = CoarseMonoClock::now() + FLAGS_remote_bootstrap_idle_timeout_ms * 2 * 1ms;
}

void RemoteBootstrapServiceImpl::RegisterLogAnchor(
    const RegisterLogAnchorRequestPB* req,
    RegisterLogAnchorResponsePB* resp,
    rpc::RpcContext context) {
  VLOG_WITH_FUNC(4) << req->ShortDebugString();

  auto tablet_peer_result = tablet_peer_lookup_->GetServingTablet(req->tablet_id());
  RPC_RETURN_NOT_OK(
      tablet_peer_result,
      RemoteBootstrapErrorPB::TABLET_NOT_FOUND,
      Substitute("Unable to find specified tablet: $0", req->tablet_id()));
  auto tablet_peer = std::move(*tablet_peer_result);
  RPC_RETURN_NOT_OK(
      tablet_peer->CheckRunning(),
      RemoteBootstrapErrorPB::TABLET_NOT_FOUND,
      Substitute("Tablet is not running yet: $0", req->tablet_id()));

  const auto requested_log_index = req->op_id().index();
  int64_t min_available_log_index = tablet_peer->log()->GetLogReader()->GetMinReplicateIndex();
  if (min_available_log_index == -1) {
    min_available_log_index = tablet_peer->log()->GetMinReplicateIndex();
  }

  if (requested_log_index < min_available_log_index) {
    RPC_RETURN_APP_ERROR(
        RemoteBootstrapErrorPB::REMOTE_LOG_ANCHOR_FAILURE,
        Substitute("Cannot register LogAnchor"),
        STATUS_FORMAT(NotFound, "Requested LogAnchor index($0) < min_available_log_index($1)",
                      requested_log_index, min_available_log_index));
  }

  MAYBE_FAULT(FLAGS_TEST_fault_crash_on_rbs_anchor_register);

  {
    std::lock_guard l(log_anchors_mutex_);
    auto it = log_anchors_map_.find(req->owner_info());
    if (it == log_anchors_map_.end()) {
      std::shared_ptr<log::LogAnchor> log_anchor_ptr(new log::LogAnchor());
      tablet_peer->log_anchor_registry()->Register(
          requested_log_index, req->owner_info(), log_anchor_ptr.get());
      std::shared_ptr<LogAnchorSessionData> anchor_session_data(
          new LogAnchorSessionData(tablet_peer, log_anchor_ptr));
      log_anchors_map_[req->owner_info()] = anchor_session_data;
      LOG(INFO) << "Beginning new remote log anchor session on tablet " << req->tablet_id()
                << " with session id = " << req->owner_info();
    } else {
      tablet_peer.reset(it->second->tablet_peer_.get());
      std::shared_ptr<log::LogAnchor> log_anchor_ptr(it->second->log_anchor_ptr_);
      it->second->ResetExpiration();
      RPC_RETURN_NOT_OK(
          tablet_peer->log_anchor_registry()->UpdateRegistration(
              requested_log_index, log_anchor_ptr.get()),
          RemoteBootstrapErrorPB::REMOTE_LOG_ANCHOR_FAILURE,
          Substitute(
              "Cannot Update LogAnchor for tablet $0 to index $1", tablet_peer->tablet_id(),
              requested_log_index));
      LOG(INFO) << "Re-initializing existing remote log anchor session on tablet "
                << req->tablet_id() << " with session id = " << req->owner_info();
    }
  }

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::UpdateLogAnchor(
    const UpdateLogAnchorRequestPB* req, UpdateLogAnchorResponsePB* resp, rpc::RpcContext context) {
  std::lock_guard l(log_anchors_mutex_);
  auto it = log_anchors_map_.find(req->owner_info());
  if (it == log_anchors_map_.end()) {
    RPC_RETURN_APP_ERROR(
        RemoteBootstrapErrorPB::NO_SESSION,
        Substitute("Log Anchor session not found."),
        STATUS_FORMAT(IllegalState, "Couldn't find Log Anchor session: $0", req->owner_info()));
  }

  const auto requested_log_index = req->op_id().index();
  std::shared_ptr<tablet::TabletPeer> tablet_peer(it->second->tablet_peer_);
  std::shared_ptr<log::LogAnchor> log_anchor_ptr(it->second->log_anchor_ptr_);
  it->second->ResetExpiration();
  RPC_RETURN_NOT_OK(
      tablet_peer->log_anchor_registry()->UpdateRegistration(
          requested_log_index, log_anchor_ptr.get()),
      RemoteBootstrapErrorPB::REMOTE_LOG_ANCHOR_FAILURE,
      Substitute(
          "Cannot Update LogAnchor for tablet $0 to $1", tablet_peer->tablet_id(),
          requested_log_index));

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::KeepLogAnchorAlive(
    const KeepLogAnchorAliveRequestPB* req,
    KeepLogAnchorAliveResponsePB* resp,
    rpc::RpcContext context) {
  std::lock_guard l(log_anchors_mutex_);
  auto it = log_anchors_map_.find(req->owner_info());
  if (it == log_anchors_map_.end()) {
    RPC_RETURN_APP_ERROR(
        RemoteBootstrapErrorPB::NO_SESSION,
        Substitute("Log Anchor session not found."),
        STATUS_FORMAT(IllegalState, "Couldn't find Log Anchor session: $0", req->owner_info()));
  }
  std::shared_ptr<tablet::TabletPeer> tablet_peer(it->second->tablet_peer_);
  std::shared_ptr<log::LogAnchor> log_anchor_ptr(it->second->log_anchor_ptr_);
  it->second->ResetExpiration();
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::UnregisterLogAnchor(
    const UnregisterLogAnchorRequestPB* req,
    UnregisterLogAnchorResponsePB* resp,
    rpc::RpcContext context) {
  VLOG_WITH_FUNC(4) << req->ShortDebugString();

  RemoteBootstrapErrorPB::Code app_error;
  std::lock_guard l(log_anchors_mutex_);
  RPC_RETURN_NOT_OK(
      DoEndLogAnchorSession(req->owner_info(), &app_error),
      app_error,
      Substitute("No existing Log Anchor session with id $0", req->owner_info())
  );
  LOG(INFO) << "Request end of remote log anchor session " << req->owner_info();
  RemoveLogAnchorSession(req->owner_info());
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::ChangePeerRole(
    const ChangePeerRoleRequestPB* req, ChangePeerRoleResponsePB* resp, rpc::RpcContext context) {
  VLOG_WITH_FUNC(4) << req->ShortDebugString();

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  {
    std::lock_guard l(log_anchors_mutex_);
    auto it = log_anchors_map_.find(req->owner_info());
    if (it == log_anchors_map_.end()) {
      RPC_RETURN_APP_ERROR(
          RemoteBootstrapErrorPB::NO_SESSION,
          Substitute("Log Anchor session not found."),
          STATUS_FORMAT(IllegalState, "Couldn't find Log Anchor session: $0", req->owner_info()));
    }

    tablet_peer = it->second->tablet_peer_;
    it->second->ResetExpiration();
  }

  RPC_RETURN_NOT_OK(
      tablet_peer->ChangeRole(req->requestor_uuid()),
      RemoteBootstrapErrorPB::REMOTE_CHANGE_PEER_ROLE_FAILURE,
      Substitute("Cannot ChangePeerRole for peer $0", req->requestor_uuid()));
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::RemoveLogAnchorSession(const std::string& session_id) {
  auto it = log_anchors_map_.find(session_id);
  if (it == log_anchors_map_.end()) {
    LOG(WARNING) << "Attempt to remove Log Anchor session with unknown id: " << session_id;
    return;
  }
  log_anchors_map_.erase(it);
}

Status RemoteBootstrapServiceImpl::DoEndLogAnchorSession(
    const std::string& session_id, RemoteBootstrapErrorPB::Code* app_error) {
  auto it = log_anchors_map_.find(session_id);
  if (it == log_anchors_map_.end()) {
    *app_error = RemoteBootstrapErrorPB::NO_SESSION;
    return STATUS_FORMAT(NotFound, "End of unknown Log Anchor session id: $0", session_id);
  }

  std::shared_ptr<tablet::TabletPeer> tablet_peer(it->second->tablet_peer_);
  std::shared_ptr<log::LogAnchor> log_anchor_ptr(it->second->log_anchor_ptr_);
  auto s = tablet_peer->log_anchor_registry()->UnregisterIfAnchored(log_anchor_ptr.get());
  if (!s.ok()) {
    *app_error = RemoteBootstrapErrorPB::REMOTE_LOG_ANCHOR_FAILURE;
    return STATUS_FORMAT(NotFound, "Couldn't unregister LogAnchor for session: $0", session_id);
  }

  return Status::OK();
}

void RemoteBootstrapServiceImpl::EndExpiredRemoteBootstrapSessions() {
  std::lock_guard l(sessions_mutex_);
  auto now = CoarseMonoClock::Now();

  std::vector<string> expired_session_ids;
  for (const auto& entry : sessions_) {
    if (entry.second.expiration < now) {
      expired_session_ids.push_back(entry.first);
    }
  }
  for (const string& session_id : expired_session_ids) {
    LOG(INFO) << "Remote bootstrap session " << session_id << " has expired. Terminating session.";
    RemoteBootstrapErrorPB::Code app_error;
    CHECK_OK(DoEndRemoteBootstrapSession(session_id, false, &app_error));
    RemoveRemoteBootstrapSession(session_id);
  }
}

void RemoteBootstrapServiceImpl::EndExpiredLogAnchorSessions() {
  std::lock_guard l(log_anchors_mutex_);
  auto now = CoarseMonoClock::Now();

  std::vector<string> expired_session_ids;
  for (const auto& entry : log_anchors_map_) {
    if (entry.second->expiration_ < now) {
      expired_session_ids.push_back(entry.first);
    }
  }
  for (const string& session_id : expired_session_ids) {
    LOG(INFO) << "Remote Bootstrap Log Anchor session " << session_id
              << " has expired. Terminating session.";
    RemoteBootstrapErrorPB::Code app_error;
    CHECK_OK(DoEndLogAnchorSession(session_id, &app_error));
    RemoveLogAnchorSession(session_id);
  }
}

void RemoteBootstrapServiceImpl::EndExpiredSessions() {
  do {
    EndExpiredRemoteBootstrapSessions();
    EndExpiredLogAnchorSessions();
  } while (!shutdown_latch_.WaitFor(MonoDelta::FromMilliseconds(
                                    FLAGS_remote_bootstrap_timeout_poll_period_ms)));
}

void RemoteBootstrapServiceImpl::DumpStatusHtml(std::ostream& out) {
  out << "<h1>Remote Bootstrap Sessions</h1>" << std::endl;
  out << "<table class='table table-striped'>" << std::endl;
  out << "<tr><th> Tablet ID </th><th> Peer ID </th></tr>" << std::endl;
  {
    std::lock_guard l(sessions_mutex_);
    for (const auto& [_, session_data] : sessions_) {
      auto& session = session_data.session;
      out << "<tr>"
            << "<td>" << session->tablet_id() << "</td>"
            << "<td>" << session->requestor_uuid() << "</td>"
          << "</tr>";
    }
  }
  out << "</table>" << std::endl;

  out << "<h1>Remote Log Anchor Sessions</h1>" << std::endl;
  out << "<table class='table table-striped'>" << std::endl;
  out << "<tr>"
        << "<th> Owner Info [requestor_uuid-tablet_id-start_timestamp] </th>"
        << "<th> Anchored at Log Index </th>"
      << "</tr>" << std::endl;
  {
    std::lock_guard l(log_anchors_mutex_);
    for (const auto& [id, session_data] : log_anchors_map_) {
      auto& log_anchor = session_data->log_anchor_ptr_;
      out << "<tr>"
            << "<td>" << id << "</td>"
            << "<td>" << log_anchor->index() << "</td>"
          << "</tr>";
    }
  }
  out << "</table>" << std::endl;
}

} // namespace tserver
} // namespace yb
