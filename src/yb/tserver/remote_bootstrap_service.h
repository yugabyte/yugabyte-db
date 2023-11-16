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
#pragma once

#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "yb/gutil/ref_counted.h"

#include "yb/tserver/remote_bootstrap.service.h"
#include "yb/tserver/remote_bootstrap_session.h"

#include "yb/util/status_fwd.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"

namespace yb {

class FsManager;
class Thread;

namespace tserver {

class TabletPeerLookupIf;

class RemoteBootstrapServiceImpl : public RemoteBootstrapServiceIf {
 public:
  RemoteBootstrapServiceImpl(FsManager* fs_manager,
                             TabletPeerLookupIf* tablet_peer_lookup,
                             const scoped_refptr<MetricEntity>& metric_entity,
                             CloudInfoPB cloud_info,
                             rpc::ProxyCache* proxy_cache);

  ~RemoteBootstrapServiceImpl();

  void BeginRemoteBootstrapSession(const BeginRemoteBootstrapSessionRequestPB* req,
                                   BeginRemoteBootstrapSessionResponsePB* resp,
                                   rpc::RpcContext context) override;

  void BeginRemoteSnapshotTransferSession(
      const BeginRemoteSnapshotTransferSessionRequestPB* req,
      BeginRemoteSnapshotTransferSessionResponsePB* resp, rpc::RpcContext context) override;

  void CheckRemoteBootstrapSessionActive(
      const CheckRemoteBootstrapSessionActiveRequestPB* req,
      CheckRemoteBootstrapSessionActiveResponsePB* resp,
      rpc::RpcContext context) override;

  void FetchData(const FetchDataRequestPB* req,
                 FetchDataResponsePB* resp,
                 rpc::RpcContext context) override;

  void EndRemoteBootstrapSession(const EndRemoteBootstrapSessionRequestPB* req,
                                 EndRemoteBootstrapSessionResponsePB* resp,
                                 rpc::RpcContext context) override;

  void RemoveRemoteBootstrapSession(
      const RemoveRemoteBootstrapSessionRequestPB* req,
      RemoveRemoteBootstrapSessionResponsePB* resp,
      rpc::RpcContext context) override;

  void Shutdown() override;

  void RegisterLogAnchor(
      const RegisterLogAnchorRequestPB* req,
      RegisterLogAnchorResponsePB* resp,
      rpc::RpcContext context) override;

  void UpdateLogAnchor(
      const UpdateLogAnchorRequestPB* req,
      UpdateLogAnchorResponsePB* resp,
      rpc::RpcContext context) override;

  void UnregisterLogAnchor(
      const UnregisterLogAnchorRequestPB* req,
      UnregisterLogAnchorResponsePB* resp,
      rpc::RpcContext context) override;

  void KeepLogAnchorAlive(
      const KeepLogAnchorAliveRequestPB* req,
      KeepLogAnchorAliveResponsePB* resp,
      rpc::RpcContext context) override;

  void ChangePeerRole(
      const ChangePeerRoleRequestPB* req,
      ChangePeerRoleResponsePB* resp,
      rpc::RpcContext context) override;

  void DumpStatusHtml(std::ostream& out);

 private:
  struct SessionData {
    scoped_refptr<RemoteBootstrapSession> session;
    CoarseTimePoint expiration;

    Status ResetExpiration(RemoteBootstrapErrorPB::Code* app_error);
  };

  class LogAnchorSessionData {
   public:
    LogAnchorSessionData(
        const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
        const std::shared_ptr<log::LogAnchor>& log_anchor_ptr);

    ~LogAnchorSessionData();

    void ResetExpiration();

    std::shared_ptr<tablet::TabletPeer> tablet_peer_;

    std::shared_ptr<log::LogAnchor> log_anchor_ptr_;

    CoarseTimePoint expiration_;
  };

  typedef std::unordered_map<std::string, SessionData> SessionMap;

  typedef std::unordered_map<std::string, std::shared_ptr<LogAnchorSessionData>> LogAnchorsMap;

  template <typename Request>
  Result<scoped_refptr<RemoteBootstrapSession>> CreateRemoteSession(
      const Request* req, const ServerRegistrationPB* tablet_leader_conn_info,
      const std::string& requestor_string, RemoteBootstrapErrorPB::Code* error_code);

  // Validate the data identifier in a FetchData request.
  Status ValidateFetchRequestDataId(
      const DataIdPB& data_id,
      RemoteBootstrapErrorPB::Code* app_error,
      const scoped_refptr<RemoteBootstrapSession>& session) const;

  // Destroy the specified remote bootstrap session.
  Status DoEndRemoteBootstrapSession(
      const std::string& session_id,
      bool session_suceeded,
      RemoteBootstrapErrorPB::Code* app_error)  REQUIRES(sessions_mutex_);

  // Destroy the specified Log Anchor session.
  Status DoEndLogAnchorSession(
      const std::string& session_id, RemoteBootstrapErrorPB::Code* app_error)
      REQUIRES(log_anchors_mutex_);

  void RemoveRemoteBootstrapSession(const std::string& session_id) REQUIRES(sessions_mutex_);

  void RemoveLogAnchorSession(const std::string& session_id) REQUIRES(log_anchors_mutex_);

  void EndExpiredRemoteBootstrapSessions();

  void EndExpiredLogAnchorSessions();

  // The timeout thread periodically checks whether RBS/LogAnchor sessions are expired and
  // removes them from the sessions_/log_anchors_map_. Calls EndExpiredRemoteBootstrapSessions
  // and EndExpiredLogAnchorSessions
  void EndExpiredSessions();

  FsManager* fs_manager_;
  TabletPeerLookupIf* tablet_peer_lookup_;
  CloudInfoPB local_cloud_info_pb_;
  rpc::ProxyCache* proxy_cache_;

  // Protects sessions_ and session_expirations_ maps.
  mutable std::mutex sessions_mutex_;
  SessionMap sessions_ GUARDED_BY(sessions_mutex_);
  std::atomic<int32> nsessions_ GUARDED_BY(sessions_mutex_) = {0};

  mutable std::mutex log_anchors_mutex_;
  LogAnchorsMap log_anchors_map_ GUARDED_BY(log_anchors_mutex_);

  // Session expiration thread.
  // TODO: this is a hack, replace with some kind of timer impl. See KUDU-286.
  CountDownLatch shutdown_latch_;
  scoped_refptr<Thread> session_expiration_thread_;
};

} // namespace tserver
} // namespace yb
