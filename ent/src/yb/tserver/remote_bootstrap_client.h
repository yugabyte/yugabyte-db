// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H
#define ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H

#include "../../../../src/yb/tserver/remote_bootstrap_client.h"

namespace yb {
namespace tserver {
namespace enterprise {

// Extends RemoteBootstrapClient to support snapshots downloading
// together with tablet RocksDB files.
class RemoteBootstrapClient : public yb::tserver::RemoteBootstrapClient {
  typedef yb::tserver::RemoteBootstrapClient super;
 public:
  RemoteBootstrapClient(std::string tablet_id,
                        FsManager* fs_manager,
                        std::shared_ptr<rpc::Messenger> messenger,
                        std::string client_permanent_uuid)
      : super(tablet_id, fs_manager, messenger, client_permanent_uuid) {}

  CHECKED_STATUS FetchAll(tablet::TabletStatusListener* status_listener) override;

  CHECKED_STATUS Finish() override;

 protected:
  CHECKED_STATUS CreateTabletDirectories(const string& db_dir, FsManager* fs) override;

 private:
  FRIEND_TEST(RemoteBootstrapRocksDBClientTest, TestDownloadSnapshotFiles);

  CHECKED_STATUS DownloadSnapshotFiles();

  bool downloaded_snapshot_files_ = false;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapClient);
};

} // namespace enterprise
} // namespace tserver
} // namespace yb
#endif // ENT_SRC_YB_TSERVER_REMOTE_BOOTSTRAP_CLIENT_H
