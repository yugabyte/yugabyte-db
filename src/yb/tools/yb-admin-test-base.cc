// Copyright (c) YugaByte, Inc.
//
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

#include "yb/tools/yb-admin-test-base.h"

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/tools/yb-admin_util.h"
#include "yb/util/backoff_waiter.h"

namespace yb {
namespace tools {

Result<master::ListSnapshotsResponsePB> WaitForAllSnapshots(
    master::MasterBackupProxy* const proxy) {
  master::ListSnapshotsRequestPB req;
  master::ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(WaitFor(
      [proxy, &req, &resp]() -> Result<bool> {
        rpc::RpcController rpc;
        RETURN_NOT_OK(proxy->ListSnapshots(req, &resp, &rpc));
        for (auto const& snapshot : resp.snapshots()) {
          if (snapshot.entry().state() != master::SysSnapshotEntryPB::COMPLETE) {
            return false;
          }
        }
        return true;
      },
      30s, "Waiting for all snapshots to complete"));
  return resp;
}

Result<std::string> GetCompletedSnapshot(
    master::MasterBackupProxy* const proxy, int num_snapshots, int idx) {
  auto resp = VERIFY_RESULT(WaitForAllSnapshots(proxy));

  if (resp.snapshots_size() != num_snapshots) {
    return STATUS_FORMAT(Corruption, "Wrong snapshot count $0", resp.snapshots_size());
  }

  return SnapshotIdToString(resp.snapshots(idx).id());
}

}  // namespace tools
}  // namespace yb
