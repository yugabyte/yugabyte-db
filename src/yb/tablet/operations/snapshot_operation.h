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

#pragma once

#include <mutex>
#include <string>

#include "yb/gutil/macros.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/operation_filter.h"
#include "yb/tablet/operations.messages.h"
#include "yb/tablet/operations/operation.h"

#include "yb/tserver/backup.messages.h"
#include "yb/util/locks.h"

namespace yb {
namespace tablet {

// Operation Context for the TabletSnapshot operation.
// Keeps track of the Operation states (request, result, ...)
// Executes the TabletSnapshotOp operation.
class SnapshotOperation :
    public ExclusiveSchemaOperation<OperationType::kSnapshot, tserver::LWTabletSnapshotOpRequestPB>,
    public OperationFilter {
 public:
  template <class... Args>
  explicit SnapshotOperation(Args&&... args)
      : ExclusiveSchemaOperation(std::forward<Args>(args)...) {
  }

  tserver::TabletSnapshotOpRequestPB::Operation operation() const {
    return request() == nullptr ?
        tserver::TabletSnapshotOpRequestPB::UNKNOWN : request()->operation();
  }

  // Returns the snapshot directory, based on the tablet's top directory for all snapshots, and any
  // overrides for the snapshot directory this operation might have.
  Result<std::string> GetSnapshotDir() const;

  bool CheckOperationRequirements();

  static bool ShouldAllowOpDuringRestore(consensus::OperationType op_type);

  static Status RejectionStatus(OpId rejected_op_id, consensus::OperationType op_type);

  Status Prepare(IsLeaderSide is_leader_side) override;

 private:
  // Starts the TabletSnapshotOp operation by assigning it a timestamp.
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;
  Status DoAborted(const Status& status) override;
  Status Apply(int64_t leader_term, Status* complete_status);

  void AddedAsPending(const TabletPtr& tablet) override;
  void RemovedFromPending(const TabletPtr& tablet) override;

  bool NeedOperationFilter() const;

  Status CheckOperationAllowed(
      const OpId& id, consensus::OperationType op_type) const override;

  Status DoCheckOperationRequirements();
};

}  // namespace tablet
}  // namespace yb
