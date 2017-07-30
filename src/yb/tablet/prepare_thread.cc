// Copyright (c) YugaByte, Inc.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/lockfree/queue.hpp>

#include <gflags/gflags.h>

#include "yb/util/logging.h"
#include "yb/tablet/prepare_thread.h"
#include "yb/tablet/transactions/transaction_driver.h"

DEFINE_int32(max_group_replicate_batch_size, 16,
             "Maximum number of transactions to submit to consensus for replication in a batch.");

// We have to make the queue length really long. Otherwise we risk crashes on followers when they
// fail to append entries to the queue, as we try to cancel the transaction in that case, and it
// is not possible to cancel an already-replicated transaction. The proper way to handle that would
// probably be to implement backpressure in UpdateReplica.
//
// Note that the lock-free queue seems to be preallocating memory proportional to the queue size
// (about 64 bytes per entry for 8-byte pointer keys) -- something to keep in mind with a large
// number of tablets.
DEFINE_int32(prepare_queue_max_size, 100000,
             "Maximum number of transactions waiting in the per-tablet prepare queue.");

using std::vector;

namespace yb {
namespace tablet {

// ------------------------------------------------------------------------------------------------
// PrepareThreadImpl

class PrepareThreadImpl {
 public:
  explicit PrepareThreadImpl(consensus::Consensus* consensus);
  ~PrepareThreadImpl();
  CHECKED_STATUS Start();
  void Stop();

  CHECKED_STATUS Submit(TransactionDriver* txn_driver);

 private:
  using TransactionDrivers = std::vector<TransactionDriver*>;

  scoped_refptr<yb::Thread> thread_;

  consensus::Consensus* const consensus_;

  // We set this to true to to tell the thread to shut down. No new tasks will be accepted, but
  // existing tasks will still be processed.
  std::atomic<bool> stop_requested_{false};

  // This is set to true immediately before the thread exits.
  std::atomic<bool> stopped_{false};

  std::atomic<bool> processing_{false};

  boost::lockfree::queue<TransactionDriver*> queue_;
  std::mutex mtx_;
  std::condition_variable cond_;

  std::mutex stop_mtx_;
  std::condition_variable stop_cond_;

  TransactionDrivers leader_side_batch_;

  // A temporary buffer of rounds to replicate, used to reduce reallocation.
  consensus::ConsensusRounds rounds_to_replicate_;

  // @return true if it is OK to block and wait for new items to be appended
  bool can_block() {
    return !processing_.load(std::memory_order_acquire) && queue_.empty();
  }

  void Run();
  void ProcessItem(TransactionDriver* item);

  // @return true if at least one item was processed.
  bool ProcessAndClearLeaderSideBatch();

  void ReplicateSubBatch(TransactionDrivers::iterator begin, TransactionDrivers::iterator end);
};

PrepareThreadImpl::PrepareThreadImpl(consensus::Consensus* consensus)
    : consensus_(consensus),
      queue_(FLAGS_prepare_queue_max_size) {
}

PrepareThreadImpl::~PrepareThreadImpl() {
  Stop();
}

Status PrepareThreadImpl::Start() {
  return Thread::Create("prepare", "prepare", &PrepareThreadImpl::Run, this, &thread_);
}

void PrepareThreadImpl::Stop() {
  // It is OK if multiple threads call this method at once. At worst, they will all notify the
  // condition variable and wait.
  if (stopped_.load(std::memory_order_acquire)) {
    return;
  }

  stop_requested_.store(true, std::memory_order_release);

  // We need this to unblock waiting for new transactions to arrive. Since nothing has been added,
  // we'll just go through the loop one more time and exit at the next iteration.
  processing_.store(true, std::memory_order_release);
  cond_.notify_one();

  std::unique_lock<decltype(mtx_)> stop_lock(stop_mtx_);
  stop_cond_.wait(stop_lock, [this] { return stopped_.load(std::memory_order_acquire); });
}

Status PrepareThreadImpl::Submit(TransactionDriver* txnd) {
  if (stop_requested_.load(std::memory_order_acquire)) {
    return STATUS(IllegalState, "Prepare thread is shutting down");
  }
  if (!queue_.bounded_push(txnd)) {
    return STATUS_FORMAT(ServiceUnavailable,
                         "Prepare queue is full (max capacity $0)",
                         FLAGS_prepare_queue_max_size);
  }

  bool old_processing = false;
  if (processing_.compare_exchange_strong(old_processing, true)) {
    // processing_ was false and we switched it to true. That means we were in the section between
    // processing_.store(false, ...) and processing_.store(true, ...) in Run at the time of
    // the compare_exchange_strong above. In that case there are two options:
    // - The item we've appended has already been processed at the time of the queue_.empty() check.
    //   Then the inner loop will break into the the outer loop, where we'll wait for processing_ to
    //   be true (a no-op) and immediately try to pop another item, which will fail if no other
    //   items have been appended yet. In that case we'll simply reset processing_ to false and go
    //   into another iteration of waiting.
    // - The item we've appended has not yet been processed at that time. Then we'll immediately
    //   store true into processing_ once again (a no-op since it's already true) and go into
    //   another iteration of the inner loop, which will process the newly added item.
    std::unique_lock<decltype(mtx_)> lock(mtx_);
    cond_.notify_one();
  }

  // In case the compare-and-exchange fails above because processing_ is already true (and we
  // don't update it or notify the condition), but then processing_ gets immediately changed to
  // false as we enter the processing_.store(false, ...); ...; processing_.store(true, ...) section
  // below, there are two options:
  // - The processing thread has already processed the item we've just added, and the queue is
  //   empty. Then the processing thread will exit the inner loop and wait on the condition, as
  //   expected.
  // - The processing thread has not yet processed the new item, and it is in the queue. Then it
  //   will go for another iteration of the inner loop and process the new item.

  return Status::OK();
}

void PrepareThreadImpl::Run() {
  for (;;) {
    {
      std::unique_lock<decltype(mtx_)> lock(mtx_);

      if (can_block()) {
        if (stop_requested_.load(std::memory_order_acquire)) {
          ProcessAndClearLeaderSideBatch();

          stopped_.store(true, std::memory_order_release);
          stop_cond_.notify_one();
          VLOG(1) << "Prepare thread's Run() function is exiting";

          // This is the only place this function returns. If that ever changes, we'll need to move
          // the cleanup logic above so that it gets executed on every return path.
          return;
        }

        // If we end up processing at least one accumulated leader-side item here, we need to check
        // the can_block() condition again. Otherwise, we don't need to re-check.
        if (!ProcessAndClearLeaderSideBatch() || can_block()) {
          cond_.wait(lock, [this] { return processing_.load(std::memory_order_acquire); });
        }
      }
    }

    for (;;) {
      TransactionDriver* item = nullptr;
      while (queue_.pop(item)) {
        ProcessItem(item);
      }
      processing_.store(false, std::memory_order_release);
      if (queue_.empty()) break;
      processing_.store(true, std::memory_order_release);
    }
  }
}

void PrepareThreadImpl::ProcessItem(TransactionDriver* item) {
  CHECK_NOTNULL(item);

  if (item->is_leader_side()) {
    const int64_t bound_term = item->consensus_round()->bound_term();

    // AlterSchemaTransaction::Prepare calls Tablet::CreatePreparedAlterSchema, which acquires the
    // schema lock. Because of this, we must not attempt to process two AlterSchemaTransactions in
    // one batch, otherwise we'll deadlock. Furthermore, for simplicity, we choose to process each
    // AlterSchemaTransaction in a batch of its own.
    const bool is_alter = item->tx_type() == Transaction::ALTER_SCHEMA_TXN;

    // Don't add more than the max number of transactions to a batch, and also don't add
    // transactions bound to different terms, so as not to fail unrelated transactions
    // unnecessarily in case of a bound term mismatch.
    if (leader_side_batch_.size() >= FLAGS_max_group_replicate_batch_size ||
        !leader_side_batch_.empty() &&
            bound_term != leader_side_batch_.back()->consensus_round()->bound_term() ||
        is_alter) {
      ProcessAndClearLeaderSideBatch();
    }
    leader_side_batch_.push_back(item);
    if (is_alter) {
      ProcessAndClearLeaderSideBatch();
    }
  } else {
    // We found a non-leader-side transaction. We need to process the accumulated batch of
    // leader-side transactions first, and then process this other transaction.
    ProcessAndClearLeaderSideBatch();
    item->PrepareAndStartTask();
  }
}

bool PrepareThreadImpl::ProcessAndClearLeaderSideBatch() {
  if (leader_side_batch_.empty()) {
    return false;
  }

  VLOG(1) << "Preparing a batch of " << leader_side_batch_.size() << " leader-side txns";

  auto iter = leader_side_batch_.begin();
  auto replication_subbatch_begin = iter;
  auto replication_subbatch_end = iter;

  // PrepareAndStart does not call Consensus::Replicate anymore as of 07/07/2017, and it is our
  // responsibility to do so in case of success. We call Consensus::ReplicateBatch for batches
  // of consecutive successfully prepared transactions.

  while (iter != leader_side_batch_.end()) {
    auto* txnd = *iter;

    Status s = txnd->PrepareAndStart();

    if (PREDICT_TRUE(s.ok())) {
      replication_subbatch_end = ++iter;
    } else {
      // Replicate the accumulated batch so far, not including this transaction. This is a no-op in
      // case of an empty batch.
      ReplicateSubBatch(replication_subbatch_begin, replication_subbatch_end);

      // Handle failure for this transaction itself.
      txnd->HandleFailure(s);

      // Now we'll start accumulating a new batch.
      replication_subbatch_begin = replication_subbatch_end = ++iter;
    }
  }

  // Replicate the remaining batch. No-op for an empty batch.
  ReplicateSubBatch(replication_subbatch_begin, replication_subbatch_end);

  leader_side_batch_.clear();
  return true;
}

void PrepareThreadImpl::ReplicateSubBatch(
    TransactionDrivers::iterator txnd_begin,
    TransactionDrivers::iterator txnd_end) {
  DCHECK_GE(std::distance(txnd_begin, txnd_end), 0);
  if (txnd_begin == txnd_end) {
    return;
  }
  VLOG(1) << "Replicating a sub-batch of " << std::distance(txnd_begin, txnd_end)
          << " leader-side txns";
  if (VLOG_IS_ON(2)) {
    for (auto txnd_iter = txnd_begin; txnd_iter != txnd_end; ++txnd_iter) {
      VLOG(2) << "Leader-side transaction to be replicated: " << (*txnd_iter)->ToString();
    }
  }

  rounds_to_replicate_.clear();
  rounds_to_replicate_.reserve(std::distance(txnd_begin, txnd_end));
  for (auto txnd_iter = txnd_begin; txnd_iter != txnd_end; ++txnd_iter) {
    DCHECK_ONLY_NOTNULL(*txnd_iter);
    DCHECK_ONLY_NOTNULL((*txnd_iter)->consensus_round());
    rounds_to_replicate_.push_back((*txnd_iter)->consensus_round());
  }
  const Status s = consensus_->ReplicateBatch(rounds_to_replicate_);
  rounds_to_replicate_.clear();

  if (PREDICT_FALSE(!s.ok())) {
    VLOG(1) << "ReplicateBatch failed with status " << s.ToString()
            << ", treating all " << std::distance(txnd_begin, txnd_end) << " txns as failed "
            << "with that status";
    // Treat all the transactions in the batch as failed.
    for (auto txnd_iter = txnd_begin; txnd_iter != txnd_end; ++txnd_iter) {
      (*txnd_iter)->SetReplicationFailed(s);
      (*txnd_iter)->HandleFailure(s);
    }
  }
}

// ------------------------------------------------------------------------------------------------
// PrepareThread

PrepareThread::PrepareThread(consensus::Consensus* consensus)
    : impl_(std::make_unique<PrepareThreadImpl>(consensus)) {
}

PrepareThread::~PrepareThread() = default;

Status PrepareThread::Start() {
  VLOG(1) << "Starting the prepare thread";
  return impl_->Start();
}

void PrepareThread::Stop() {
  VLOG(1) << "Stopping the prepare thread";
  impl_->Stop();
  VLOG(1) << "The prepare thread has stopped";
}

Status PrepareThread::Submit(TransactionDriver* txnd) {
  return impl_->Submit(txnd);
}

}  // namespace tablet
}  // namespace yb
