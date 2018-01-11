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
#include <unordered_map>
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
typedef std::function<void(const Result<ChildTransactionDataPB>&)> PrepareChildCallback;

// When Batch plans to execute some operations in context of a transaction, it asks
// that transaction to make some preparations. This struct contains results of those preparations.
struct TransactionPrepareData {
  // Transaction metadata that should be sent to tserver with those operations.
  TransactionMetadata metadata;

  // Propagated hybrid time.
  HybridTime propagated_ht;

  // Read time.
  ReadHybridTime read_time;

  // Local limits for separate tablets, pointed object alive while transaction is alive.
  const std::unordered_map<TabletId, HybridTime>* local_limits;
};

struct ChildTransactionData {
  TransactionMetadata metadata;
  ReadHybridTime read_time;
  std::unordered_map<TabletId, HybridTime> local_limits;

  static Result<ChildTransactionData> FromPB(const ChildTransactionDataPB& data);
};

// YBTransaction is a representation of a single transaction.
// After YBTransaction is created, it could be used during construction of YBSession,
// to indicate that this session will send commands related to this transaction.
class YBTransaction : public std::enable_shared_from_this<YBTransaction> {
 public:
  YBTransaction(TransactionManager* manager, IsolationLevel isolation);

  // Creates "child" transaction.
  // Child transaction shares same metadata as parent transaction, so all writes are done
  // as part of parent transaction.
  // But lifetime is controlled by parent transaction.
  // I.e. only parent transaction could be committed or aborted, also only parent transaction
  // sends heartbeats.
  YBTransaction(TransactionManager* manager, ChildTransactionData data);

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

  void RestartRequired(const TabletId& tablet, const ReadHybridTime& restart_time);

  YBTransactionPtr CreateRestartedTransaction();

  // Prepares child data, so child transaction could be started in another server.
  // Should be async because status tablet could be not ready yet.
  void PrepareChild(PrepareChildCallback callback);

  std::future<Result<ChildTransactionDataPB>> PrepareChildFuture();

  // After we finish all child operations, we should finish child and send result to parent.
  Result<ChildTransactionResultPB> FinishChild();

  // Apply results from child to this parent transaction.
  // `result` should be prepared with FinishChild of child transaction.
  CHECKED_STATUS ApplyChildResult(const ChildTransactionResultPB& result);

  std::shared_future<TransactionMetadata> TEST_GetMetadata() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_H
