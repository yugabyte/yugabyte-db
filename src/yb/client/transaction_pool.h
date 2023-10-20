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

#include "yb/client/client_fwd.h"

#include "yb/common/transaction.pb.h"
#include "yb/common/read_hybrid_time.h"

namespace yb {

class MetricEntity;

namespace client {

// Pool that maintains set of preallocated ready transactions.
// The size of the pool is auto adjusted, i.e. the more transactions we request - the more
// transactions will be allocated.
// Preallocated transactions live for transaction_idle_lifetime_ms milliseconds,
// then are aborted. So pool is trimmed back when the load is decreased.
class TransactionPool {
 public:
  TransactionPool(TransactionManager* manager, MetricEntity* metric_entity);
  ~TransactionPool();

  // Tries to take a new ready transaction from the pool.
  // If pool is empty a newly created transaction is returned.
  // If force_global_transaction is true, the transaction will always use a global transaction
  // status tablet.
  //
  // Ready means that transaction is registered at status tablet and intents could be written
  // immediately.
  YBTransactionPtr Take(ForceGlobalTransaction force_global_transaction, CoarseTimePoint deadline);

  // Takes and initializes a transaction from the pool. See Take for details.
  Result<YBTransactionPtr> TakeAndInit(
      IsolationLevel isolation, CoarseTimePoint deadline,
      const ReadHybridTime& read_time = ReadHybridTime());

  // Takes a transaction from the pool and sets it up as a restart of the original transaction.
  // See Take for details.
  Result<YBTransactionPtr> TakeRestarted(const YBTransactionPtr& source, CoarseTimePoint deadline);

  // Gets the last transaction returned by the pool. Only for testing, returns nullptr unless the
  // TEST_track_last_transaction gflag is set.
  YBTransactionPtr TEST_GetLastTransaction();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb
