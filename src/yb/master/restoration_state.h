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

#ifndef YB_MASTER_RESTORATION_STATE_H
#define YB_MASTER_RESTORATION_STATE_H

#include "yb/common/hybrid_time.h"
#include "yb/common/snapshot.h"

#include "yb/master/state_with_tablets.h"

namespace yb {
namespace master {

class RestorationState : public StateWithTablets {
 public:
  RestorationState(
      SnapshotCoordinatorContext* context, const TxnSnapshotRestorationId& restoration_id,
      SnapshotState* snapshot);

  const TxnSnapshotRestorationId& restoration_id() const {
    return restoration_id_;
  }

  const TxnSnapshotId& snapshot_id() const {
    return snapshot_id_;
  }

  HybridTime complete_time() const {
    return complete_time_;
  }

  void set_complete_time(HybridTime value) {
    complete_time_ = value;
  }

  CHECKED_STATUS ToPB(RestorationInfoPB* out);

  TabletInfos PrepareOperations();

 private:
  bool IsTerminalFailure(const Status& status) override;

  const TxnSnapshotRestorationId restoration_id_;
  const TxnSnapshotId snapshot_id_;
  HybridTime complete_time_;
};

} // namespace master
} // namespace yb

#endif  // YB_MASTER_RESTORATION_STATE_H
