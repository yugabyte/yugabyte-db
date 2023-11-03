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

#include "yb/common/wire_protocol.messages.h"
#include "yb/tablet/operations/operation.h"

namespace yb {
namespace tablet {

// Operation Context for the ChangeAutoFlagsConfig operation.
// Keeps track of the Operation states (request, result, ...).
// Executes the ChangeAutoFlagsConfig operation.
class ChangeAutoFlagsConfigOperation
    : public OperationBase<OperationType::kChangeAutoFlagsConfig, LWAutoFlagsConfigPB> {
 public:
  template <class... Args>
  explicit ChangeAutoFlagsConfigOperation(Args&&... args)
      : OperationBase(std::forward<Args>(args)...) {}

  Status Apply();

 private:
  Status Prepare(IsLeaderSide is_leader_side) override;
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;
  Status DoAborted(const Status& status) override;
};

}  // namespace tablet
}  // namespace yb
