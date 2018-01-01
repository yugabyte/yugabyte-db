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
#include "yb/docdb/doc_key.h"
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
class TruncateOperationState : public OperationState {
 public:
  explicit TruncateOperationState(Tablet* tablet,
                                  const tserver::TruncateRequestPB* request = nullptr)
      : OperationState(tablet), request_(request) {}
  ~TruncateOperationState() {}

  const tserver::TruncateRequestPB* request() const override { return request_; }

  void UpdateRequestFromConsensusRound() override {
    request_ = consensus_round()->replicate_msg()->mutable_truncate_request();
  }

  virtual std::string ToString() const override;

 private:
  // The original RPC request.
  const tserver::TruncateRequestPB *request_;

  DISALLOW_COPY_AND_ASSIGN(TruncateOperationState);
};

// Executes the truncate transaction.
class TruncateOperation : public Operation {
 public:
  TruncateOperation(std::unique_ptr<TruncateOperationState> operation_state,
                    consensus::DriverType type);

  TruncateOperationState* state() override {
    return down_cast<TruncateOperationState*>(Operation::state());
  }

  const TruncateOperationState* state() const override {
    return down_cast<const TruncateOperationState*>(Operation::state());
  }

  consensus::ReplicateMsgPtr NewReplicateMsg() override;

  virtual CHECKED_STATUS Prepare() override { return Status::OK(); }

  // Starts the TruncateOperation by assigning it a timestamp.
  virtual void Start() override;

  // Executes an Apply for the truncate transaction.
  virtual CHECKED_STATUS Apply() override;

  virtual std::string ToString() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(TruncateOperation);
};

}  // namespace tablet
}  // namespace yb

#endif  // YB_TABLET_OPERATIONS_TRUNCATE_OPERATION_H
