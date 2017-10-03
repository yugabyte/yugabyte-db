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
#ifndef KUDU_TABLET_TRANSACTION_ORDER_VERIFIER_H
#define KUDU_TABLET_TRANSACTION_ORDER_VERIFIER_H

#include "kudu/gutil/macros.h"
#include "kudu/gutil/walltime.h"
#include "kudu/gutil/threading/thread_collision_warner.h"

namespace kudu {
namespace tablet {

// Simple class which verifies the invariant that we eventually submit
// each operation to be applied in increasing operation index order, and that
// these operations have gone through the Prepare() phase in that same
// order.
//
// This currently runs even in release builds. If we ever find it to be a
// bottleneck, we could run it only in DEBUG builds, but it is extraordinarily
// simple and thus should not be problematic.
//
// NOTE ON SYNCHRONIZATION
// ------------------------
// This class is not thread-safe, because the synchronization is handled externally. It is
// always called for an operation after both its PREPARE and REPLICATE phases are complete
// (i.e before it is submitted to be applied).  This may occur on a number of different
// threads -- eg the prepare pool thread, an RPC handler handling UpdateConsensus() calls
// on a replica, or another thread when the leader receives a response from one of its
// replicas. However, we can ensure that there are no concurrent calls into this class
// based on the following logic:
// - CheckApply(N) only runs after both Prepare(N) and Replicate(N) are complete, on either
//   the thread that called Prepare(N) or Replicate(N).
// - Prepare(N-1) always completes before Prepare(N) because Prepare is single-threaded.
// - Replicate(N-1) always completes before Replicate(N), as ensured by the consensus
//   implementation.
// - Therefore, both Prepare(N-1) and Replicate(N-1) have completed, and therefore CheckApply(N-1)
//   has completed.
// - Therefore, CheckApply(N-1) is not concurrent with CheckApply(N).
//
// Note that some of the assumptions above are implementation-dependent, not algorithmic
// properties of Raft. In particular, we currently trigger ReplicateFinished() in strict
// order, but it would still be correct to do it from a threadpool. If we change the
// implementation, the implementation of this verifier class will need to change
// accordingly.
//
// Because the above reasoning is somewhat complex, and the assumptions may change in the
// future, this class uses a DFAKE_MUTEX. This way, if we break any assumptions, we'll
// hopefully see the bug with an assertion error if not from a TSAN failure.
class TransactionOrderVerifier {
 public:
  TransactionOrderVerifier();
  ~TransactionOrderVerifier();

  // Verify that it would be correct to apply an operation with the given
  // index and prepare timestamp. This ensures that the indexes are increasing
  // one by one (with no gaps) and that the prepare timestamps are also increasing.
  //
  // NOTE: the 'timestamp' here is a local system monotonic timestamp, not
  // a Kudu Timestamp. We are enforcing/verifying a local ordering property,
  // so local real time is what matters.
  //
  // If the checks fail, the server is FATALed.
  void CheckApply(int64_t op_idx,
                  MicrosecondsInt64 prepare_phys_timestamp);

 private:
  DFAKE_MUTEX(fake_lock_);

  int64_t prev_idx_;
  MicrosecondsInt64 prev_prepare_phys_timestamp_;

  DISALLOW_COPY_AND_ASSIGN(TransactionOrderVerifier);
};

} // namespace tablet
} // namespace kudu
#endif /* KUDU_TABLET_TRANSACTION_ORDER_VERIFIER_H */
