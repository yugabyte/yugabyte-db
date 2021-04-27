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
//

#ifndef YB_CLIENT_SNAPSHOT_TEST_BASE_H
#define YB_CLIENT_SNAPSHOT_TEST_BASE_H

#include "yb/client/txn-test-base.h"

#include "yb/master/master_backup.proxy.h"

namespace yb {
namespace client {

using Snapshots = google::protobuf::RepeatedPtrField<master::SnapshotInfoPB>;
constexpr auto kWaitTimeout = std::chrono::seconds(15);

class SnapshotTestBase : public TransactionTestBase<MiniCluster> {
 protected:
  master::MasterBackupServiceProxy MakeBackupServiceProxy();

  Result<master::SysSnapshotEntryPB::State> SnapshotState(const TxnSnapshotId& snapshot_id);
  Result<bool> IsSnapshotDone(const TxnSnapshotId& snapshot_id);
  Result<Snapshots> ListSnapshots(
      const TxnSnapshotId& snapshot_id = TxnSnapshotId::Nil(), bool list_deleted = true);
  CHECKED_STATUS VerifySnapshot(
      const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state);
  CHECKED_STATUS WaitSnapshotInState(
      const TxnSnapshotId& snapshot_id, master::SysSnapshotEntryPB::State state,
      MonoDelta duration = kWaitTimeout);
  CHECKED_STATUS WaitSnapshotDone(
      const TxnSnapshotId& snapshot_id, MonoDelta duration = kWaitTimeout);

  Result<TxnSnapshotRestorationId> StartRestoration(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at = HybridTime());
  Result<bool> IsRestorationDone(const TxnSnapshotRestorationId& restoration_id);
  CHECKED_STATUS RestoreSnapshot(
      const TxnSnapshotId& snapshot_id, HybridTime restore_at = HybridTime());
};

} // namespace client
} // namespace yb

#endif  // YB_CLIENT_SNAPSHOT_TEST_BASE_H
