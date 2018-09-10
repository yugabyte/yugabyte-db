// Copyright (c) YugaByte, Inc.

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
 private:
  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapSession);
};

}  // namespace enterprise
}  // namespace tserver
}  // namespace yb

#endif // ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
