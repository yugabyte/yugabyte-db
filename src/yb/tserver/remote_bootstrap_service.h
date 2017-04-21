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
#ifndef YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
#define YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_

#include <string>
#include <unordered_map>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/tserver/remote_bootstrap.service.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

namespace yb {
class FsManager;

namespace log {
class ReadableLogSegment;
} // namespace log

namespace tserver {

class RemoteBootstrapSession;
class TabletPeerLookupIf;

class RemoteBootstrapServiceImpl : public RemoteBootstrapServiceIf {
 public:
  RemoteBootstrapServiceImpl(FsManager* fs_manager,
                             TabletPeerLookupIf* tablet_peer_lookup,
                             const scoped_refptr<MetricEntity>& metric_entity);

  virtual void BeginRemoteBootstrapSession(const BeginRemoteBootstrapSessionRequestPB* req,
                                           BeginRemoteBootstrapSessionResponsePB* resp,
                                           rpc::RpcContext* context) override;

  virtual void CheckSessionActive(const CheckRemoteBootstrapSessionActiveRequestPB* req,
                                  CheckRemoteBootstrapSessionActiveResponsePB* resp,
                                  rpc::RpcContext* context) override;

  virtual void FetchData(const FetchDataRequestPB* req,
                         FetchDataResponsePB* resp,
                         rpc::RpcContext* context) override;

  virtual void EndRemoteBootstrapSession(const EndRemoteBootstrapSessionRequestPB* req,
                                         EndRemoteBootstrapSessionResponsePB* resp,
                                         rpc::RpcContext* context) override;

  virtual void Shutdown() override;

 private:
  typedef std::unordered_map<std::string, scoped_refptr<RemoteBootstrapSession> > SessionMap;
  typedef std::unordered_map<std::string, MonoTime> MonoTimeMap;

  // Look up session in session map.
  CHECKED_STATUS FindSessionUnlocked(const std::string& session_id,
                             RemoteBootstrapErrorPB::Code* app_error,
                             scoped_refptr<RemoteBootstrapSession>* session) const;

  // Validate the data identifier in a FetchData request.
  CHECKED_STATUS ValidateFetchRequestDataId(const DataIdPB& data_id,
                                    RemoteBootstrapErrorPB::Code* app_error,
                                    const scoped_refptr<RemoteBootstrapSession>& session) const;

  // Take note of session activity; Re-update the session timeout deadline.
  void ResetSessionExpirationUnlocked(const std::string& session_id);

  // Destroy the specified remote bootstrap session.
  CHECKED_STATUS DoEndRemoteBootstrapSessionUnlocked(const std::string& session_id,
                                             bool session_suceeded,
                                             RemoteBootstrapErrorPB::Code* app_error);

  // The timeout thread periodically checks whether sessions are expired and
  // removes them from the map.
  void EndExpiredSessions();

  FsManager* fs_manager_;
  TabletPeerLookupIf* tablet_peer_lookup_;

  // Protects sessions_ and session_expirations_ maps.
  mutable simple_spinlock sessions_lock_;
  SessionMap sessions_;
  MonoTimeMap session_expirations_;

  // Session expiration thread.
  // TODO: this is a hack, replace with some kind of timer impl. See KUDU-286.
  CountDownLatch shutdown_latch_;
  scoped_refptr<Thread> session_expiration_thread_;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_REMOTE_BOOTSTRAP_SERVICE_H_
