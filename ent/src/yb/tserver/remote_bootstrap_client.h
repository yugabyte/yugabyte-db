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
                        std::string client_permanent_uuid)
      : super(tablet_id, fs_manager, client_permanent_uuid) {}

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
