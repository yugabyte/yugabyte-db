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

#include <glog/logging.h>
#include "kudu/tablet/transaction_order_verifier.h"

namespace kudu {
namespace tablet {

TransactionOrderVerifier::TransactionOrderVerifier()
  : prev_idx_(0),
    prev_prepare_phys_timestamp_(0) {
}

TransactionOrderVerifier::~TransactionOrderVerifier() {
}

void TransactionOrderVerifier::CheckApply(int64_t op_idx,
                                          MicrosecondsInt64 prepare_phys_timestamp) {
  DFAKE_SCOPED_LOCK(fake_lock_);

  if (prev_idx_ != 0) {
    // We need to allow skips because certain ops (like NO_OP) don't have an
    // Apply() phase and are not managed by Transactions.
    CHECK_GE(op_idx, prev_idx_ + 1) << "Should apply operations in monotonic index order";
    CHECK_GE(prepare_phys_timestamp, prev_prepare_phys_timestamp_)
      << "Prepare phases should have executed in the same order as the op indexes. "
      << "op_idx=" << op_idx;
  }
  prev_idx_ = op_idx;
  prev_prepare_phys_timestamp_ = prepare_phys_timestamp;
}

} // namespace tablet
} // namespace kudu
