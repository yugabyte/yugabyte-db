// Copyright (c) YugabyteDB, Inc.
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

#include <map>
#include <optional>
#include <thread>
#include <vector>

#include "yb/common/transaction.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/docdb/docdb.h"

namespace yb {

class OneWayBitmap;
class RWOperationCounter;
class Synchronizer;
class Thread;

namespace tablet {

class TransactionStatusResolver;
struct TransactionalBatchData;

using ApplyStatesMap = std::unordered_map<
    TransactionId, docdb::ApplyStateWithCommitInfo, TransactionIdHash>;

class TransactionLoaderContext {
 public:
  virtual ~TransactionLoaderContext() = default;

  virtual TransactionStatusResolver& AddStatusResolver() = 0;
  virtual const std::string& LogPrefix() const = 0;
  virtual void LoadTransaction(
      TransactionMetadata&& metadata,
      TransactionalBatchData&& last_batch_data,
      OneWayBitmap&& replicated_batches,
      const docdb::ApplyStateWithCommitInfo* pending_apply,
      HybridTime first_write_ht) = 0;
  virtual void LoadFinished(Status load_status) = 0;
  virtual HybridTime MinReplayTxnFirstWriteTime() = 0;
};

YB_DEFINE_ENUM(TransactionLoaderState, (kNotStarted)(kLoading)(kCompleted)(kFailed));

class TransactionLoader {
 public:
  TransactionLoader(TransactionLoaderContext* context, const scoped_refptr<MetricEntity>& entity);
  ~TransactionLoader();

  void Start(
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start,
      const docdb::DocDB& db);

  bool Started() const {
    return state_ != TransactionLoaderState::kNotStarted;
  }

  // Returns false when the loader thread did not complete successfully i.e. it is still running
  // or has encountered a failure. On seeing false, the caller should check for the failure case
  // explicitly and access the failure status in 'load_status_'.
  //
  // Returns a bad status if the loader thread wasn't launched at the first place.
  Result<bool> Completed() const {
    // Read state_ with sequential consistency to prevent subtle bugs with operation reordering.
    switch (state_) {
      case TransactionLoaderState::kNotStarted:
        return STATUS_FORMAT(IllegalState, "Loader thread not started");
      case TransactionLoaderState::kCompleted:
        return true;
      case TransactionLoaderState::kLoading: [[fallthrough]];
      case TransactionLoaderState::kFailed:
        return false;
    }
    FATAL_INVALID_ENUM_VALUE(TransactionLoaderState, state_.load());
  }

  Status WaitLoaded(const TransactionId& id);
  Status WaitLoaded(const TransactionIdApplyOpIdMap& txns);
  Status WaitAllLoaded();

  std::optional<docdb::ApplyStateWithCommitInfo> GetPendingApply(const TransactionId& id) const
      EXCLUDES(pending_applies_mtx_);

  void StartShutdown() EXCLUDES(mutex_);
  void CompleteShutdown();

  // Moves the pending applies map to the result. Should only be called after the tablet has
  // started.
  ApplyStatesMap MovePendingApplies();

 private:
  class Executor;
  friend class Executor;

  void FinishLoad(Status status);

  // Removes and returns the waiters that 'last_loaded_' has reached. The caller releases them
  // after unlocking 'mutex_'.
  [[nodiscard]] std::vector<Synchronizer*> ExtractReachedWaiters() REQUIRES(mutex_);

  void SetFinalStateAndReleaseWaiters(TransactionLoaderState state, const Status& status)
      EXCLUDES(mutex_);

  TransactionLoaderContext& context_;
  const scoped_refptr<MetricEntity> entity_;

  std::unique_ptr<Executor> executor_;

  std::mutex mutex_;
  TransactionId last_loaded_ GUARDED_BY(mutex_) = TransactionId::Nil();
  // Points at Synchronizers owned by the stack frames blocked in WaitLoaded. The loader thread
  // releases every entry before it exits, and a waiter does not leave WaitLoaded until it is
  // released, so an entry never outlives its Synchronizer.
  std::multimap<TransactionId, Synchronizer*> waiters_ GUARDED_BY(mutex_);
  Status load_status_ GUARDED_BY(mutex_);
  std::atomic<TransactionLoaderState> state_{TransactionLoaderState::kNotStarted};
  std::atomic<bool> shutdown_requested_{false};
  scoped_refptr<Thread> load_thread_;

  mutable std::mutex pending_applies_mtx_;
  ApplyStatesMap pending_applies_ GUARDED_BY(pending_applies_mtx_);
  std::atomic<bool> pending_applies_removed_{false};
};

} // namespace tablet
} // namespace yb
