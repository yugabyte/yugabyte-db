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

#include "yb/consensus/consensus.messages.h"

#include "yb/tablet/operations.messages.h"
#include "yb/tablet/operations/operation.h"

namespace yb {
namespace tablet {

class HistoryCutoffOperation
    : public OperationBase<OperationType::kHistoryCutoff, consensus::LWHistoryCutoffPB> {
 public:
  template <class... Args>
  explicit HistoryCutoffOperation(Args&&... args) : OperationBase(std::forward<Args>(args)...) {}

  Status Apply(int64_t leader_term);

 private:
  Status Prepare(IsLeaderSide is_leader_side) override;
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;
  Status DoAborted(const Status& status) override;
};

} // namespace tablet
} // namespace yb
