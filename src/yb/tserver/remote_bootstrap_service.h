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
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
#define YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_

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
                             const scoped_refptr<MetricEntity>& metric_entity);

  ~RemoteBootstrapServiceImpl();

  void BeginRemoteBootstrapSession(const BeginRemoteBootstrapSessionRequestPB* req,
                                   BeginRemoteBootstrapSessionResponsePB* resp,
                                   rpc::RpcContext context) override;

  void CheckSessionActive(const CheckRemoteBootstrapSessionActiveRequestPB* req,
                          CheckRemoteBootstrapSessionActiveResponsePB* resp,
                          rpc::RpcContext context) override;

  void FetchData(const FetchDataRequestPB* req,
                 FetchDataResponsePB* resp,
                 rpc::RpcContext context) override;

  void EndRemoteBootstrapSession(const EndRemoteBootstrapSessionRequestPB* req,
                                 EndRemoteBootstrapSessionResponsePB* resp,
                                 rpc::RpcContext context) override;


  void RemoveSession(
          const RemoveSessionRequestPB* req,
          RemoveSessionResponsePB* resp,
          rpc::RpcContext context) override;

  void Shutdown() override;

 private:
  struct SessionData {
    scoped_refptr<RemoteBootstrapSession> session;
    CoarseTimePoint expiration;

    void ResetExpiration();
  };

  typedef std::unordered_map<std::string, SessionData> SessionMap;

  // Validate the data identifier in a FetchData request.
  CHECKED_STATUS ValidateFetchRequestDataId(
      const DataIdPB& data_id,
      RemoteBootstrapErrorPB::Code* app_error,
      const scoped_refptr<RemoteBootstrapSession>& session) const;

  // Destroy the specified remote bootstrap session.
  CHECKED_STATUS DoEndRemoteBootstrapSession(
      const std::string& session_id,
      bool session_suceeded,
      RemoteBootstrapErrorPB::Code* app_error)  REQUIRES(sessions_mutex_);

  void RemoveSession(const std::string& session_id) REQUIRES(sessions_mutex_);

  // The timeout thread periodically checks whether sessions are expired and
  // removes them from the map.
  void EndExpiredSessions();

  FsManager* fs_manager_;
  TabletPeerLookupIf* tablet_peer_lookup_;

  // Protects sessions_ and session_expirations_ maps.
  mutable std::mutex sessions_mutex_;
  SessionMap sessions_ GUARDED_BY(sessions_mutex_);
  std::atomic<int32> nsessions_ GUARDED_BY(sessions_mutex_) = {0};

  // Session expiration thread.
  // TODO: this is a hack, replace with some kind of timer impl. See KUDU-286.
  CountDownLatch shutdown_latch_;
  scoped_refptr<Thread> session_expiration_thread_;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
