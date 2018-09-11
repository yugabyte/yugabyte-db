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

#include <boost/date_time/time_duration.hpp>
#include <boost/thread/locks.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/log.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/map-util.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/crc.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"

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

DEFINE_uint64(remote_bootstrap_idle_timeout_ms, 180000,
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

  scoped_refptr<RemoteBootstrapSessionClass> session;
  {
    std::lock_guard<simple_spinlock> l(sessions_lock_);
    if (!FindCopy(sessions_, session_id, &session)) {
      LOG(INFO) << "Beginning new remote bootstrap session on tablet " << tablet_id
                << " from peer " << requestor_uuid << " at " << context.requestor_string()
                << ": session id = " << session_id;
      session.reset(new RemoteBootstrapSessionClass(tablet_peer, session_id,
                                                    requestor_uuid, fs_manager_, &nsessions_));
      RPC_RETURN_NOT_OK(session->Init(),
                        RemoteBootstrapErrorPB::UNKNOWN_ERROR,
                        Substitute("Error initializing remote bootstrap session for tablet $0",
                                   tablet_id));
      InsertOrDie(&sessions_, session_id, session);
      auto new_nsessions = nsessions_.fetch_add(1, std::memory_order_acq_rel) + 1;
      if (new_nsessions != sessions_.size()) {
        LOG(DFATAL) << "nsessions_ " << new_nsessions
                    << " !=  number of sessions " << sessions_.size();
      }
    } else {
      LOG(INFO) << "Re-initializing existing remote bootstrap session on tablet " << tablet_id
                << " from peer " << requestor_uuid << " at " << context.requestor_string()
                << ": session id = " << session_id;
      RPC_RETURN_NOT_OK(session->Init(),
                        RemoteBootstrapErrorPB::UNKNOWN_ERROR,
                        Substitute("Error initializing remote bootstrap session for tablet $0",
                                   tablet_id));
    }
    ResetSessionExpirationUnlocked(session_id);
  }

  resp->set_session_id(session_id);
  resp->set_session_idle_timeout_millis(FLAGS_remote_bootstrap_idle_timeout_ms);
  resp->mutable_superblock()->CopyFrom(session->tablet_superblock());
  resp->mutable_initial_committed_cstate()->CopyFrom(session->initial_committed_cstate());

  for (const scoped_refptr<log::ReadableLogSegment>& segment : session->log_segments()) {
    resp->add_wal_segment_seqnos(segment->header().sequence_number());
  }

  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::CheckSessionActive(
        const CheckRemoteBootstrapSessionActiveRequestPB* req,
        CheckRemoteBootstrapSessionActiveResponsePB* resp,
        rpc::RpcContext context) {
  const string& session_id = req->session_id();

  // Look up and validate remote bootstrap session.
  scoped_refptr<RemoteBootstrapSessionClass> session;
  std::lock_guard<simple_spinlock> l(sessions_lock_);
  RemoteBootstrapErrorPB::Code app_error;
  Status status = FindSessionUnlocked(session_id, &app_error, &session);
  if (status.ok()) {
    if (req->keepalive()) {
      ResetSessionExpirationUnlocked(session_id);
    }
    resp->set_session_is_active(true);
    context.RespondSuccess();
    return;
  } else if (app_error == RemoteBootstrapErrorPB::NO_SESSION) {
    resp->set_session_is_active(false);
    context.RespondSuccess();
    return;
  } else {
    RPC_RETURN_NOT_OK(status, app_error,
                      Substitute("Error trying to check whether session $0 is active", session_id));
  }
}

Status RemoteBootstrapServiceImpl::GetDataFilePiece(
    const DataIdPB& data_id,
    const scoped_refptr<RemoteBootstrapSessionClass>& session,
    uint64_t offset,
    int64_t client_maxlen,
    string* data,
    int64_t* total_data_length,
    RemoteBootstrapErrorPB::Code* error_code) {

  switch (data_id.type()) {
    case DataIdPB::BLOCK: {
      // Fetching a data block chunk.
      const BlockId& block_id = BlockId::FromPB(data_id.block_id());
      RETURN_NOT_OK_PREPEND(session->GetBlockPiece(
                                block_id, offset, client_maxlen,
                                data, total_data_length, error_code),
                            "Unable to get piece of data block");
      break;
    }
    case DataIdPB::LOG_SEGMENT: {
      // Fetching a log segment chunk.
      const uint64_t segment_seqno = data_id.wal_segment_seqno();
      RETURN_NOT_OK_PREPEND(session->GetLogSegmentPiece(
                                segment_seqno, offset, client_maxlen,
                                data, total_data_length, error_code),
                            "Unable to get piece of log segment");
      break;
    }
    case DataIdPB::ROCKSDB_FILE: {
      // Fetching a RocksDB file chunk.
      const string file_name = data_id.file_name();
      RETURN_NOT_OK_PREPEND(session->GetRocksDBFilePiece(
                                file_name, offset, client_maxlen,
                                data, total_data_length, error_code),
                            "Unable to get piece of RocksDB file");
      break;
    }
    default:
      *error_code = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid request type $0", data_id.type());
  }
  DCHECK(client_maxlen == 0 || data->size() <= client_maxlen)
      << "client_maxlen: " << client_maxlen << ", data->size(): " << data->size();

  return Status::OK();
}

void RemoteBootstrapServiceImpl::FetchData(const FetchDataRequestPB* req,
                                           FetchDataResponsePB* resp,
                                           rpc::RpcContext context) {
  const string& session_id = req->session_id();

  // Look up and validate remote bootstrap session.
  scoped_refptr<RemoteBootstrapSessionClass> session;
  {
    std::lock_guard<simple_spinlock> l(sessions_lock_);
    RemoteBootstrapErrorPB::Code app_error;
    RPC_RETURN_NOT_OK(FindSessionUnlocked(session_id, &app_error, &session),
                      app_error, "No such session");
    ResetSessionExpirationUnlocked(session_id);
  }

  session->EnsureRateLimiterIsInitialized();

  MAYBE_FAULT(FLAGS_fault_crash_on_handle_rb_fetch_data);

  uint64_t offset = req->offset();
  VLOG(3) << " rate limiter max len: "  << session->rate_limiter().GetMaxSizeForNextTransmission();
  auto rate_limit = session->rate_limiter().GetMaxSizeForNextTransmission();
  int64_t client_maxlen = rate_limit == 0
      ? req->max_length() : std::min(static_cast<uint64_t>(req->max_length()), rate_limit);
  const DataIdPB& data_id = req->data_id();
  RemoteBootstrapErrorPB::Code error_code = RemoteBootstrapErrorPB::UNKNOWN_ERROR;
  RPC_RETURN_NOT_OK(ValidateFetchRequestDataId(data_id, &error_code, session),
                    error_code, "Invalid DataId");

  DataChunkPB* data_chunk = resp->mutable_chunk();
  string* data = data_chunk->mutable_data();
  int64_t total_data_length = 0;
  RPC_RETURN_NOT_OK(GetDataFilePiece(data_id, session, offset, client_maxlen, data,
                                     &total_data_length, &error_code),
                    error_code, "Unable to get piece of data file");

  data_chunk->set_total_data_length(total_data_length);
  session->rate_limiter().UpdateDataSizeAndMaybeSleep(data->size());
  data_chunk->set_offset(offset);

  // Calculate checksum.
  uint32_t crc32 = Crc32c(data->data(), data->length());
  data_chunk->set_crc32(crc32);
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::EndRemoteBootstrapSession(
        const EndRemoteBootstrapSessionRequestPB* req,
        EndRemoteBootstrapSessionResponsePB* resp,
        rpc::RpcContext context) {
  {
    std::lock_guard<simple_spinlock> l(sessions_lock_);
    RemoteBootstrapErrorPB::Code app_error;
    RPC_RETURN_NOT_OK(DoEndRemoteBootstrapSessionUnlocked(req->session_id(), req->is_success(),
                                                          &app_error),
                      app_error, "No such session");
    LOG(INFO) << "Request end of remote bootstrap session " << req->session_id()
              << " received from " << context.requestor_string();
  }
  context.RespondSuccess();
}

void RemoteBootstrapServiceImpl::Shutdown() {
  shutdown_latch_.CountDown();
  session_expiration_thread_->Join();

  // Destroy all remote bootstrap sessions.
  vector<string> session_ids;
  for (const MonoTimeMap::value_type& entry : session_expirations_) {
    session_ids.push_back(entry.first);
  }
  for (const string& session_id : session_ids) {
    LOG(INFO) << "Destroying remote bootstrap session " << session_id << " due to service shutdown";
    RemoteBootstrapErrorPB::Code app_error;
    CHECK_OK(DoEndRemoteBootstrapSessionUnlocked(session_id, false, &app_error));
  }
}

Status RemoteBootstrapServiceImpl::FindSessionUnlocked(
        const string& session_id,
        RemoteBootstrapErrorPB::Code* app_error,
        scoped_refptr<RemoteBootstrapSessionClass>* session) const {
  if (!FindCopy(sessions_, session_id, session)) {
    *app_error = RemoteBootstrapErrorPB::NO_SESSION;
    return STATUS(NotFound,
        Substitute("Remote bootstrap session with Session ID \"$0\" not found", session_id));
  }
  return Status::OK();
}

Status RemoteBootstrapServiceImpl::ValidateFetchRequestDataId(
        const DataIdPB& data_id,
        RemoteBootstrapErrorPB::Code* app_error,
        const scoped_refptr<RemoteBootstrapSessionClass>& session) const {
  int num_set = data_id.has_block_id() + data_id.has_wal_segment_seqno() +
      data_id.has_file_name();
  if (PREDICT_FALSE(num_set == 0 || num_set > 1)) {
    *app_error = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
    return STATUS(InvalidArgument,
        Substitute("Only one of BlockId, segment sequence number, and file name can be specified. "
                   "DataTypeID: $0", data_id.ShortDebugString()));
  }

  switch (data_id.type()) {
    case DataIdPB::BLOCK:
      if (PREDICT_FALSE(!data_id.has_block_id())) {
        return STATUS(InvalidArgument, "block_id must be specified for type == BLOCK",
                                       data_id.ShortDebugString());
      }
      return Status::OK();
    case DataIdPB::LOG_SEGMENT:
      if (PREDICT_FALSE(!data_id.wal_segment_seqno())) {
        return STATUS(InvalidArgument,
            "segment sequence number must be specified for type == LOG_SEGMENT",
            data_id.ShortDebugString());
      }
      return Status::OK();
    case DataIdPB::ROCKSDB_FILE:
      if (PREDICT_FALSE(data_id.file_name().empty())) {
        return STATUS(InvalidArgument,
            "file name must be specified for type == ROCKSDB_FILE",
            data_id.ShortDebugString());
      }
      return Status::OK();
    case DataIdPB::SNAPSHOT_FILE:
      return ValidateSnapshotFetchRequestDataId(data_id);
    case DataIdPB::UNKNOWN:
      return STATUS(InvalidArgument, "Type UNKNOWN not supported", data_id.ShortDebugString());
  }
  LOG(FATAL) << "Invalid data id type: " << data_id.type();
}

Status RemoteBootstrapServiceImpl::ValidateSnapshotFetchRequestDataId(
    const DataIdPB& data_id) const {
  return STATUS(InvalidArgument, "Type SNAPSHOT_FILE not supported", data_id.ShortDebugString());
}

void RemoteBootstrapServiceImpl::ResetSessionExpirationUnlocked(const std::string& session_id) {
  MonoTime expiration(MonoTime::Now());
  expiration.AddDelta(MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_idle_timeout_ms));
  InsertOrUpdate(&session_expirations_, session_id, expiration);
}

Status RemoteBootstrapServiceImpl::DoEndRemoteBootstrapSessionUnlocked(
        const std::string& session_id,
        bool session_succeeded,
        RemoteBootstrapErrorPB::Code* app_error) {
  scoped_refptr<RemoteBootstrapSessionClass> session;
  RETURN_NOT_OK(FindSessionUnlocked(session_id, app_error, &session));

  if (session_succeeded || session->Succeeded()) {
    session->SetSuccess();

    if (PREDICT_FALSE(FLAGS_inject_latency_before_change_role_secs)) {
      LOG(INFO) << "Injecting latency for test";
      SleepFor(MonoDelta::FromSeconds(FLAGS_inject_latency_before_change_role_secs));
    }

    if (PREDICT_FALSE(FLAGS_skip_change_role)) {
      LOG(INFO) << "Not changing role for " << session->requestor_uuid()
                << " because flag FLAGS_skip_change_role is set";
      return Status::OK();
    }

    MAYBE_FAULT(FLAGS_fault_crash_leader_before_changing_role);

    MonoTime deadline =
        MonoTime::Now() +
        MonoDelta::FromMilliseconds(FLAGS_remote_bootstrap_change_role_timeout_ms);
    for (;;) {
      Status status = session->ChangeRole();
      if (status.ok()) {
        LOG(INFO) << "ChangeRole succeeded for bootstrap session " << session_id;
        MAYBE_FAULT(FLAGS_fault_crash_leader_after_changing_role);
        break;
      }
      LOG(WARNING) << "ChangeRole failed for bootstrap session " << session_id
                   << ", error : " << status;
      if (!status.IsLeaderHasNoLease() || MonoTime::Now() >= deadline) {
        ResetSessionExpirationUnlocked(session_id);
        return Status::OK();
      }
    }
  } else {
    LOG(ERROR) << "Remote bootstrap session " << session_id << " on tablet " << session->tablet_id()
               << " with peer " << session->requestor_uuid() << " failed. session_succeeded = "
               << session_succeeded;
  }

  // Remove the session from the map.
  // It will get destroyed once there are no outstanding refs.
  LOG(INFO) << "Ending remote bootstrap session " << session_id << " on tablet "
            << session->tablet_id() << " with peer " << session->requestor_uuid();
  CHECK_EQ(1, sessions_.erase(session_id));
  nsessions_.fetch_sub(1, std::memory_order_acq_rel);
  CHECK_EQ(1, session_expirations_.erase(session_id));

  return Status::OK();
}

void RemoteBootstrapServiceImpl::EndExpiredSessions() {
  do {
    std::lock_guard<simple_spinlock> l(sessions_lock_);
    MonoTime now = MonoTime::Now();

    vector<string> expired_session_ids;
    for (const MonoTimeMap::value_type& entry : session_expirations_) {
      const string& session_id = entry.first;
      const MonoTime& expiration = entry.second;
      if (expiration.ComesBefore(now)) {
        expired_session_ids.push_back(session_id);
      }
    }
    for (const string& session_id : expired_session_ids) {
      LOG(INFO) << "Remote bootstrap session " << session_id
                << " has expired. Terminating session.";
      RemoteBootstrapErrorPB::Code app_error;
      CHECK_OK(DoEndRemoteBootstrapSessionUnlocked(session_id, false, &app_error));
    }
  } while (!shutdown_latch_.WaitFor(MonoDelta::FromMilliseconds(
                                    FLAGS_remote_bootstrap_timeout_poll_period_ms)));
}

} // namespace tserver
} // namespace yb
