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

#include "yb/client/session.h"

#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/error.h"
#include "yb/client/error_collector.h"
#include "yb/client/yb_op.h"

#include "yb/common/consistent_read_point.h"

#include "yb/consensus/consensus_error.h"
#include "yb/tserver/tserver_error.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_int32(client_read_write_timeout_ms, 60000, "Timeout for client read and write operations.");

namespace yb {
namespace client {

using internal::AsyncRpcMetrics;
using internal::Batcher;
using internal::ErrorCollector;

using std::shared_ptr;

YBSession::YBSession(YBClient* client, const scoped_refptr<ClockBase>& clock) {
  batcher_config_.client = client;
  batcher_config_.non_transactional_read_point =
      clock ? std::make_unique<ConsistentReadPoint>(clock) : nullptr;
  batcher_config_.hybrid_time_for_write = HybridTime::kInvalid;
  const auto metric_entity = client->metric_entity();
  async_rpc_metrics_ = metric_entity ? std::make_shared<AsyncRpcMetrics>(metric_entity) : nullptr;
}

void YBSession::SetReadPoint(const Restart restart) {
  const auto& read_point = batcher_config_.non_transactional_read_point;
  DCHECK_NOTNULL(read_point.get());
  if (restart && read_point->IsRestartRequired()) {
    read_point->Restart();
  } else {
    read_point->SetCurrentReadTime();
  }
}

void YBSession::SetReadPoint(const ReadHybridTime& read_time) {
  batcher_config_.non_transactional_read_point->SetReadTime(read_time, {} /* local_limits */);
}

bool YBSession::IsRestartRequired() {
  auto rp = read_point();
  return rp && rp->IsRestartRequired();
}

void YBSession::DeferReadPoint() {
  batcher_config_.non_transactional_read_point->Defer();
}

void YBSession::SetTransaction(YBTransactionPtr transaction) {
  batcher_config_.transaction = std::move(transaction);
  internal::BatcherPtr old_batcher;
  old_batcher.swap(batcher_);
  if (old_batcher) {
    LOG_IF(DFATAL, old_batcher->HasPendingOperations()) << "SetTransaction with non empty batcher";
    old_batcher->Abort(STATUS(Aborted, "Transaction changed"));
  }
}

void YBSession::SetRejectionScoreSource(RejectionScoreSourcePtr rejection_score_source) {
  if (batcher_) {
    batcher_->SetRejectionScoreSource(rejection_score_source);
  }
  batcher_config_.rejection_score_source = std::move(rejection_score_source);
}

YBSession::~YBSession() {
  WARN_NOT_OK(Close(true), "Closed Session with pending operations.");
}

void YBSession::Abort() {
  if (batcher_ && batcher_->HasPendingOperations()) {
    batcher_->Abort(STATUS(Aborted, "Batch aborted"));
    batcher_.reset();
  }
}

Status YBSession::Close(bool force) {
  if (batcher_) {
    if (batcher_->HasPendingOperations() && !force) {
      return STATUS(IllegalState, "Could not close. There are pending operations.");
    }
    batcher_->Abort(STATUS(Aborted, "Batch aborted"));
    batcher_.reset();
  }
  return Status::OK();
}

void YBSession::SetTimeout(MonoDelta timeout) {
  CHECK_GE(timeout, MonoDelta::kZero);
  deadline_ = CoarseTimePoint();
  timeout_ = timeout;
  if (batcher_) {
    batcher_->SetDeadline(CoarseMonoClock::now() + timeout_);
  }
}

void YBSession::SetDeadline(CoarseTimePoint deadline) {
  timeout_ = MonoDelta();
  deadline_ = deadline;
  if (batcher_) {
    batcher_->SetDeadline(deadline);
  }
}

Status YBSession::Flush() {
  return FlushFuture().get().status;
}

FlushStatus YBSession::FlushAndGetOpsErrors() {
  return FlushFuture().get();
}

namespace {

internal::BatcherPtr CreateBatcher(const YBSession::BatcherConfig& config) {
  internal::BatcherPtr batcher(new internal::Batcher(
      config.client, config.session.lock(), config.transaction, config.read_point(),
      config.force_consistent_read));
  batcher->SetRejectionScoreSource(config.rejection_score_source);
  if (config.hybrid_time_for_write.is_valid()) {
    batcher->SetHybridTimeForWrite(config.hybrid_time_for_write);
  }
  return batcher;
}

void FlushBatcherAsync(
    const internal::BatcherPtr& batcher, FlushCallback callback, YBSession::BatcherConfig config,
    const internal::IsWithinTransactionRetry is_within_transaction_retry);

void MoveErrorsAndRunCallback(
    CollectedErrors errors, FlushCallback callback, const Status& status) {
  for (auto& error : errors) {
    VLOG(4) << "Operation " << AsString(error->failed_op())
            << " failed with: " << AsString(error->status());
  }
  // TODO: before enabling transaction sealing we might need to call Transaction::Flushed
  // for ops that we have retried, failed again and decided not to retry due to deadline.
  // See comments for YBTransaction::Impl::running_requests_ and
  // Batcher::RemoveInFlightOpsAfterFlushing.
  // https://github.com/yugabyte/yugabyte-db/issues/7984.
  FlushStatus flush_status{status, std::move(errors)};
  callback(&flush_status);
}

void BatcherFlushDone(
    const internal::BatcherPtr& done_batcher, const Status& s,
    FlushCallback callback, YBSession::BatcherConfig batcher_config) {
  auto errors = done_batcher->GetAndClearPendingErrors();
  size_t retriable_errors_count = 0;
  for (auto& error : errors) {
    retriable_errors_count += ShouldSessionRetryError(error->status());
  }
  if (errors.size() > retriable_errors_count || errors.empty()) {
    // We only retry failed ops if all of them failed with retriable errors.
    MoveErrorsAndRunCallback(std::move(errors), std::move(callback), s);
    return;
  }

  internal::BatcherPtr retry_batcher;
  const auto deadline = done_batcher->deadline();
  while (CoarseMonoClock::now() < deadline) {
    retry_batcher = CreateBatcher(batcher_config);
    retry_batcher->SetDeadline(deadline);
    Status batcher_add_status = Status::OK();
    for (auto& error : errors) {
      VLOG(4) << "Retrying " << AsString(error->failed_op())
              << " due to: " << AsString(error->status());
      const auto op = error->shared_failed_op();
      op->ResetTablet();
      batcher_add_status = retry_batcher->Add(op);
      if (!batcher_add_status.ok()) {
        LOG(INFO) << Format(
            "Failed to add operation $0 to batcher for retry: $1", op, batcher_add_status);
        if (ShouldSessionRetryError(batcher_add_status)) {
          continue;
        } else {
          MoveErrorsAndRunCallback(std::move(errors), std::move(callback), batcher_add_status);
          return;
        }
      }
    }

    FlushBatcherAsync(retry_batcher, std::move(callback), batcher_config,
        internal::IsWithinTransactionRetry::kTrue);
    return;
  }

  const auto timed_out = STATUS_FORMAT(
      TimedOut, "Timed out when retrying, now: $0, deadline: $1", CoarseMonoClock::now(), deadline);
  LOG(INFO) << timed_out;
  MoveErrorsAndRunCallback(std::move(errors), std::move(callback), timed_out);
}

void FlushBatcherAsync(
    const internal::BatcherPtr& batcher, FlushCallback callback,
    YBSession::BatcherConfig batcher_config,
    const internal::IsWithinTransactionRetry is_within_transaction_retry) {
  batcher->set_allow_local_calls_in_curr_thread(
      batcher_config.allow_local_calls_in_curr_thread);
  batcher->FlushAsync(
      std::bind(
          &BatcherFlushDone, batcher, _1, std::move(callback), batcher_config),
      is_within_transaction_retry);
}

} // namespace

void YBSession::FlushAsync(FlushCallback callback) {
  // Swap in a new batcher to start building the next batch.
  // Save off the old batcher.
  //
  // Send off any buffered data. Important to do this outside of the lock
  // since the callback may itself try to take the lock, in the case that
  // the batch fails "inline" on the same thread.

  internal::BatcherPtr old_batcher;
  old_batcher.swap(batcher_);
  if (old_batcher) {
    FlushBatcherAsync(
        old_batcher, std::move(callback), batcher_config_,
        internal::IsWithinTransactionRetry::kFalse);
  } else {
    FlushStatus ok;
    callback(&ok);
  }
}

std::future<FlushStatus> YBSession::FlushFuture() {
  auto promise = std::make_shared<std::promise<FlushStatus>>();
  auto future = promise->get_future();
  FlushAsync([promise](FlushStatus* status) {
      promise->set_value(std::move(*status));
  });
  return future;
}

Status YBSession::ReadSync(std::shared_ptr<YBOperation> yb_op) {
  CHECK(yb_op->read_only());
  return ApplyAndFlush(std::move(yb_op));
}

YBClient* YBSession::client() const {
  return batcher_config_.client;
}

void YBSession::FlushStarted(internal::BatcherPtr batcher) {
  std::lock_guard<simple_spinlock> l(lock_);
  flushed_batchers_.insert(batcher);
}

void YBSession::FlushFinished(internal::BatcherPtr batcher) {
  std::lock_guard<simple_spinlock> l(lock_);
  CHECK_EQ(flushed_batchers_.erase(batcher), 1);
}

bool YBSession::allow_local_calls_in_curr_thread() const {
  return batcher_config_.allow_local_calls_in_curr_thread;
}

void YBSession::set_allow_local_calls_in_curr_thread(bool flag) {
  batcher_config_.allow_local_calls_in_curr_thread = flag;
}

void YBSession::SetInTxnLimit(HybridTime value) {
  auto* rp = read_point();
  LOG_IF(DFATAL, rp == nullptr)
      << __FUNCTION__ << "(" << value << ") called on YBSession " << this
      << " but read point is null";
  if (rp) {
    rp->SetInTxnLimit(value);
  }
}

ConsistentReadPoint* YBSession::BatcherConfig::read_point() const {
  return transaction ? &transaction->read_point() : non_transactional_read_point.get();
}


ConsistentReadPoint* YBSession::read_point() {
  return batcher_config_.read_point();
}

void YBSession::SetHybridTimeForWrite(const HybridTime ht) {
  batcher_config_.hybrid_time_for_write = ht;
  if (batcher_) {
    batcher_->SetHybridTimeForWrite(batcher_config_.hybrid_time_for_write);
  }
}

internal::Batcher& YBSession::Batcher() {
  if (!batcher_) {
    batcher_config_.session = shared_from_this();
    batcher_ = CreateBatcher(batcher_config_);
    if (deadline_ != CoarseTimePoint()) {
      batcher_->SetDeadline(deadline_);
    } else {
      auto timeout = timeout_;
      if (PREDICT_FALSE(!timeout.Initialized())) {
        YB_LOG_EVERY_N(WARNING, 100000)
            << "Client writing with no deadline set, using 60 seconds.\n"
            << GetStackTrace();
        timeout = MonoDelta::FromSeconds(60);
      }

      batcher_->SetDeadline(CoarseMonoClock::now() + timeout);
    }
  }
  return *batcher_;
}

Status YBSession::Apply(YBOperationPtr yb_op) {
  return Batcher().Add(yb_op);
}

Status YBSession::ApplyAndFlush(YBOperationPtr yb_op) {
  RETURN_NOT_OK(Apply(std::move(yb_op)));

  return FlushFuture().get().status;
}

bool YBSession::IsInProgress(YBOperationPtr yb_op) const {
  if (batcher_ && batcher_->Has(yb_op)) {
    return true;
  }
  std::lock_guard<simple_spinlock> l(lock_);
  for (const auto& b : flushed_batchers_) {
    if (b->Has(yb_op)) {
      return true;
    }
  }
  return false;
}

Status YBSession::Apply(const std::vector<YBOperationPtr>& ops) {
  auto& batcher = Batcher();
  for (const auto& op : ops) {
    RETURN_NOT_OK(batcher.Add(op));
  }
  return Status::OK();
}

Status YBSession::ApplyAndFlush(const std::vector<YBOperationPtr>& ops) {
  RETURN_NOT_OK(Apply(ops));
  return FlushFuture().get().status;
}

int YBSession::TEST_CountBufferedOperations() const {
  return batcher_ ? batcher_->CountBufferedOperations() : 0;
}

int YBSession::GetAddedNotFlushedOperationsCount() const {
  return batcher_ ? batcher_->GetAddedNotFlushedOperationsCount() : 0;
}

bool YBSession::TEST_HasPendingOperations() const {
  if (batcher_ && batcher_->HasPendingOperations()) {
    return true;
  }
  std::lock_guard<simple_spinlock> l(lock_);
  for (const auto& b : flushed_batchers_) {
    if (b->HasPendingOperations()) {
      return true;
    }
  }
  return false;
}

void YBSession::SetForceConsistentRead(ForceConsistentRead value) {
  batcher_config_.force_consistent_read = value;
  if (batcher_) {
    batcher_->SetForceConsistentRead(value);
  }
}

bool ShouldSessionRetryError(const Status& status) {
  return IsRetryableClientError(status) ||
         tserver::TabletServerError(status) == tserver::TabletServerErrorPB::TABLET_SPLIT ||
         consensus::ConsensusError(status) == consensus::ConsensusErrorPB::TABLET_SPLIT;
}

} // namespace client
} // namespace yb
