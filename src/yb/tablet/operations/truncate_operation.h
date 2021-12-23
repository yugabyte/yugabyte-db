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

#ifndef YB_TABLET_OPERATIONS_TRUNCATE_OPERATION_H
#define YB_TABLET_OPERATIONS_TRUNCATE_OPERATION_H

#include <mutex>
#include <string>

#include "yb/gutil/macros.h"
#include "yb/tablet/operations.pb.h"
#include "yb/tablet/operations/operation.h"
#include "yb/util/locks.h"

namespace yb {

class Schema;

namespace consensus {
class Consensus;
}

namespace tablet {

// Operation Context for the Truncate operation.
// Keeps track of the Operation states (request, result, ...)
// Executes the truncate transaction.
class TruncateOperation : public OperationBase<OperationType::kTruncate, TruncatePB> {
 public:
  template <class... Args>
  explicit TruncateOperation(Args&&... args)
      : OperationBase(std::forward<Args>(args)...) {}

  CHECKED_STATUS Prepare() override { return Status::OK(); }

 private:
  // Starts the TruncateOperation by assigning it a timestamp.
  CHECKED_STATUS DoReplicated(int64_t leader_term, Status* complete_status) override;
  CHECKED_STATUS DoAborted(const Status& status) override;
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_OPERATIONS_TRUNCATE_OPERATION_H
