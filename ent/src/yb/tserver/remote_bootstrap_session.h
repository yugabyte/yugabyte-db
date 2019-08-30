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

#ifndef ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
#define ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_

#include "../../../../src/yb/tserver/remote_bootstrap_session.h"

namespace yb {
namespace tserver {
namespace enterprise {

class RemoteBootstrapSession : public yb::tserver::RemoteBootstrapSession {
  typedef yb::tserver::RemoteBootstrapSession super;
 public:
  RemoteBootstrapSession(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                         std::string session_id,
                         std::string requestor_uuid,
                         FsManager* fs_manager,
                         const std::atomic<int>* nsessions)
      : super(tablet_peer, session_id, requestor_uuid, fs_manager, nsessions) {}

  CHECKED_STATUS InitSnapshotFiles() override;

  // Get a piece of a snapshot file.
  CHECKED_STATUS GetSnapshotFilePiece(const std::string snapshot_id,
                                      const std::string file_name,
                                      uint64_t offset,
                                      int64_t client_maxlen,
                                      std::string* data,
                                      int64_t* log_file_size,
                                      RemoteBootstrapErrorPB::Code* error_code);

  virtual ~RemoteBootstrapSession() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapSession);
};

}  // namespace enterprise
}  // namespace tserver
}  // namespace yb

#endif // ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
