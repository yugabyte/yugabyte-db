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

#include <condition_variable>
#include <optional>
#include <thread>

#include "yb/common/transaction.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/docdb/docdb.h"

namespace yb {

class OneWayBitmap;
class RWOperationCounter;
class Thread;

namespace tablet {

class TransactionStatusResolver;
struct TransactionalBatchData;

struct ApplyStateWithCommitHt {
  docdb::ApplyTransactionState state;
  HybridTime commit_ht;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(state, commit_ht);
  }
};

using ApplyStatesMap = std::unordered_map<
    TransactionId, ApplyStateWithCommitHt, TransactionIdHash>;

class TransactionLoaderContext {
 public:
  virtual ~TransactionLoaderContext() = default;

  virtual TransactionStatusResolver& AddStatusResolver() = 0;
  virtual const std::string& LogPrefix() const = 0;
  virtual void CompleteLoad(const std::function<void()>& functor) = 0;
  virtual void LoadTransaction(
      TransactionMetadata&& metadata,
      TransactionalBatchData&& last_batch_data,
      OneWayBitmap&& replicated_batches,
      const ApplyStateWithCommitHt* pending_apply) = 0;
  virtual void LoadFinished() = 0;
};

YB_DEFINE_ENUM(TransactionLoaderState, (kLoadNotFinished)(kLoadCompleted)(kLoadFailed));

class TransactionLoader {
 public:
  TransactionLoader(TransactionLoaderContext* context, const scoped_refptr<MetricEntity>& entity);
  ~TransactionLoader();

  void Start(
      RWOperationCounter* pending_op_counter_blocking_rocksdb_shutdown_start,
      const docdb::DocDB& db);

  bool complete() const {
    return state_.load(std::memory_order_acquire) == TransactionLoaderState::kLoadCompleted;
  }

  Status WaitLoaded(const TransactionId& id);
  Status WaitAllLoaded();

  std::optional<ApplyStateWithCommitHt> GetPendingApply(const TransactionId& id) const
      EXCLUDES(pending_applies_mtx_);

  void Shutdown();

  // Moves the pending applies map to the result. Should only be called after the tablet has
  // started.
  ApplyStatesMap MovePendingApplies();

 private:
  class Executor;
  friend class Executor;

  void FinishLoad(Status status);

  TransactionLoaderContext& context_;
  const scoped_refptr<MetricEntity> entity_;

  std::unique_ptr<Executor> executor_;

  std::mutex mutex_;
  std::condition_variable load_cond_;
  TransactionId last_loaded_ GUARDED_BY(mutex_) = TransactionId::Nil();
  Status load_status_ GUARDED_BY(mutex_);
  std::atomic<TransactionLoaderState> state_{TransactionLoaderState::kLoadNotFinished};
  scoped_refptr<Thread> load_thread_;

  mutable std::mutex pending_applies_mtx_;
  ApplyStatesMap pending_applies_ GUARDED_BY(pending_applies_mtx_);
  std::atomic<bool> pending_applies_removed_{false};
};

} // namespace tablet
} // namespace yb
