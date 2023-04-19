// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include <vector>

#include "yb/tablet/operations/operation.h"
#include "yb/tablet/operations.messages.h"

namespace yb {
namespace tablet {

// An operation for a batch of inserts/mutates. This class holds and
// owns most everything related to a transaction, including the Replicate and Commit PB messages
//
// All the transaction related pointers are owned by this class
// and destroyed on Reset() or by the destructor.
//
// IMPORTANT: All the acquired locks will not be released unless the OperationState
// is either destroyed or Reset() or release_locks() is called. Beware of this
// or else there will be lock leaks.
//
// Used when logging to WAL in that we keep track of where inserts/updates
// were applied and add that information to the commit message that is stored
// on the WAL.
//
// NOTE: this class isn't thread safe.
class WriteOperation : public OperationBase<OperationType::kWrite, LWWritePB>  {
 public:
  template <class... Args>
  explicit WriteOperation(Args&&... args)
      : OperationBase(std::forward<Args>(args)...) {}

  bool use_mvcc() const override {
    return true;
  }

 private:
  // Executes a Prepare for a write transaction
  //
  // Decodes the operations in the request PB and acquires row locks for each of the
  // affected rows.
  Status Prepare(IsLeaderSide is_leader_side) override;

  // Executes an Apply for a write transaction.
  //
  // Actually applies inserts/mutates into the tablet. After these start being
  // applied, the transaction must run to completion as there is currently no
  // means of undoing an update.
  //
  // After completing the inserts/mutates, the row locks and the mvcc transaction
  // can be released, allowing other transactions to update the same rows.
  // However the component lock must not be released until the commit msg, which
  // indicates where each of the inserts/mutates were applied, is persisted to
  // stable storage. Because of this ApplyTask must enqueue a CommitTask before
  // releasing both the row locks and deleting the MvccTransaction as we need to
  // make sure that Commits that touch the same set of rows are persisted in
  // order, for recovery.
  // This, of course, assumes that commits are executed in the same order they
  // are placed in the queue (but not necessarily in the same order of the
  // original requests) which is already a requirement of the consensus
  // algorithm.
  // Commits the mvcc transaction and updates the metrics.
  Status DoReplicated(int64_t leader_term, Status* complete_status) override;

  // Aborts the mvcc transaction.
  Status DoAborted(const Status& status) override;

  HybridTime WriteHybridTime() const override;
};

}  // namespace tablet
}  // namespace yb
