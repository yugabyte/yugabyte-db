//
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
//

#ifndef YB_CLIENT_TRANSACTION_H
#define YB_CLIENT_TRANSACTION_H

#include <future>
#include <memory>
#include <unordered_set>

#include "yb/common/common.pb.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/client/client_fwd.h"

#include "yb/util/status.h"

namespace yb {

class HybridTime;
struct TransactionMetadata;

namespace client {

typedef std::function<void(const Status&)> Waiter;
typedef std::function<void(const Status&)> CommitCallback;

// When Batch plans to execute some operations in context of a transaction, it asks
// that transaction to make some preparations. This struct contains results of those preparations.
struct TransactionPrepareData {
  // Transaction metadata that should be sent to tserver with those operations.
  TransactionMetadata metadata;

  // Propagated hybrid time.
  HybridTime propagated_ht;

  // Read time.
  ReadHybridTime read_time;
};

// YBTransaction is a representation of a single transaction.
// After YBTransaction is created, it could be used during construction of YBSession,
// to indicate that this session will send commands related to this transaction.
class YBTransaction : public std::enable_shared_from_this<YBTransaction> {
 public:
  YBTransaction(TransactionManager* manager, IsolationLevel isolation);
  ~YBTransaction();

  // This function is used to init metadata of Write/Read request.
  // If we don't have enough information, then the function returns false and stores
  // the waiter, which will be invoked when we obtain such information.
  bool Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
               Waiter waiter,
               TransactionPrepareData* prepare_data);

  // Notifies transaction that specified ops were flushed with some status.
  void Flushed(
      const internal::InFlightOps& ops, const Status& status, HybridTime propagated_hybrid_time);

  // Commits this transaction.
  void Commit(CommitCallback callback);

  // Utility function for Commit.
  std::future<Status> CommitFuture();

  // Aborts this transaction.
  void Abort();

  // Returns transaction ID.
  const TransactionId& id() const;

  std::shared_future<TransactionMetadata> TEST_GetMetadata() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_H
