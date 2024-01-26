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

#include "yb/client/async_rpc.h"
#include "yb/client/batcher.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/error.h"
#include "yb/client/error_collector.h"
#include "yb/client/yb_op.h"

#include "yb/common/consistent_read_point.h"

#include "yb/consensus/consensus_error.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/sync_point.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_int32(client_read_write_timeout_ms, 60000,
    "Timeout for client read and write operations.");

namespace yb::client {

YBSession::YBSession(YBClient* client, const scoped_refptr<ClockBase>& clock) {
  batcher_config_.client = client;
  batcher_config_.non_transactional_read_point =
      clock ? std::make_unique<ConsistentReadPoint>(clock) : nullptr;
  const auto metric_entity = client->metric_entity();
  async_rpc_metrics_ =
      metric_entity ? std::make_shared<internal::AsyncRpcMetrics>(metric_entity) : nullptr;
}

YBSession::YBSession(YBClient* client, MonoDelta delta, const scoped_refptr<ClockBase>& clock)
    : YBSession(client, clock) {
  SetTimeout(delta);
}

YBSession::YBSession(
    YBClient* client, CoarseTimePoint deadline, const scoped_refptr<ClockBase>& clock)
    : YBSession(client, clock) {
  SetDeadline(deadline);
}

void YBSession::RestartNonTxnReadPoint(const Restart restart) {
  const auto& read_point = batcher_config_.non_transactional_read_point;
  DCHECK_NOTNULL(read_point.get());
  if (restart && read_point->IsRestartRequired()) {
    read_point->Restart();
  } else {
    read_point->SetCurrentReadTime();
  }
}

void YBSession::SetReadPoint(const ReadHybridTime& read_time, const TabletId& tablet_id) {
  ConsistentReadPoint::HybridTimeMap local_limits;
  if (!tablet_id.empty()) {
    local_limits.emplace(tablet_id, read_time.local_limit);
  }
  read_point()->SetReadTime(read_time, std::move(local_limits));
}

bool YBSession::IsRestartRequired() {
  auto rp = read_point();
  return rp && rp->IsRestartRequired();
}

void YBSession::SetTransaction(YBTransactionPtr transaction) {
  batcher_config_.transaction = std::move(transaction);
  internal::BatcherPtr old_batcher;
  old_batcher.swap(batcher_);
  LOG_IF(DFATAL, old_batcher) << "SetTransaction with non empty batcher";
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
    batcher_.reset();
  }
}

Status YBSession::Close(bool force) {
  if (batcher_) {
    if (batcher_->HasPendingOperations() && !force) {
      return STATUS(IllegalState, "Could not close. There are pending operations.");
    }
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

namespace {

internal::BatcherPtr CreateBatcher(const YBSession::BatcherConfig& config) {
  auto batcher = std::make_shared<internal::Batcher>(
      config.client, config.session.lock(), config.transaction, config.read_point(),
      config.force_consistent_read, config.leader_term);
  batcher->SetRejectionScoreSource(config.rejection_score_source);
  return batcher;
}

void FlushBatcherAsync(
    const internal::BatcherPtr& batcher, FlushCallback callback, YBSession::BatcherConfig config,
    const internal::IsWithinTransactionRetry is_within_transaction_retry);

void MoveErrorsAndRunCallback(
    const internal::BatcherPtr& done_batcher, CollectedErrors errors, FlushCallback callback,
    const Status& status) {
  const auto vlog_level = status.ok() ? 4 : 3;
  VLOG_WITH_FUNC(vlog_level) << "Invoking callback, batcher: " << done_batcher->LogPrefix()
                             << ", num_errors: " << errors.size() << " status: " << status;
  if (VLOG_IS_ON(5)) {
    for (auto& error : errors) {
      VLOG(5) << "Operation " << AsString(error->failed_op())
              << " failed with: " << AsString(error->status());
    }
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
    MoveErrorsAndRunCallback(done_batcher, std::move(errors), std::move(callback), s);
    return;
  }

  VLOG_WITH_FUNC(3) << "Retrying " << errors.size() << " operations from batcher "
                    << done_batcher->LogPrefix() << " due to: " << s
                    << ": (first op error: " << errors[0]->status() << ")";

  internal::BatcherPtr retry_batcher = CreateBatcher(batcher_config);
  retry_batcher->SetDeadline(done_batcher->deadline());

  for (auto& error : errors) {
    VLOG_WITH_FUNC(5) << "Retrying " << AsString(error->failed_op())
                      << " due to: " << error->status();
    const auto op = error->shared_failed_op();
    op->ResetTablet();
    // Transmit failed request id to retry_batcher.
    if (op->request_id()) {
      retry_batcher->MoveRequestDetailsFrom(done_batcher, *op->request_id());
    }
    retry_batcher->Add(op);
  }

  DEBUG_ONLY_TEST_SYNC_POINT("BatcherFlushDone:Retry:1");

  FlushBatcherAsync(retry_batcher, std::move(callback), batcher_config,
      internal::IsWithinTransactionRetry::kTrue);
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

YBClient* YBSession::client() const {
  return batcher_config_.client;
}

void YBSession::FlushStarted(internal::BatcherPtr batcher) {
  std::lock_guard l(lock_);
  flushed_batchers_.insert(batcher);
}

void YBSession::FlushFinished(internal::BatcherPtr batcher) {
  std::lock_guard l(lock_);
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

internal::Batcher& YBSession::Batcher() {
  if (!batcher_) {
    batcher_config_.session = shared_from_this();
    batcher_ = CreateBatcher(batcher_config_);
    if (deadline_ != CoarseTimePoint()) {
      batcher_->SetDeadline(deadline_);
    } else {
      auto timeout = timeout_;
      // In retail mode default to 60s.
      if (PREDICT_FALSE(!timeout.Initialized())) {
        DCHECK(false) << "Session deadline or timeout must always be set.\n" << GetStackTrace();
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

namespace {
void PrepareAndApplyYbOp(internal::Batcher* batcher, YBOperationPtr yb_op) {
  VLOG(5) << "YBSession Apply yb_op: " << yb_op->ToString();
  yb_op->reset_request_id();
  batcher->Add(std::move(yb_op));
}
}  // namespace

void YBSession::Apply(YBOperationPtr yb_op) {
  PrepareAndApplyYbOp(&Batcher(), std::move(yb_op));
}

void YBSession::Apply(const std::vector<YBOperationPtr>& ops) {
  if (ops.empty()) {
    return;
  }
  auto& batcher = Batcher();
  for (const auto& yb_op : ops) {
    PrepareAndApplyYbOp(&batcher, yb_op);
  }
}

bool YBSession::IsInProgress(YBOperationPtr yb_op) const {
  if (batcher_ && batcher_->Has(yb_op)) {
    return true;
  }
  std::lock_guard l(lock_);
  for (const auto& b : flushed_batchers_) {
    if (b->Has(yb_op)) {
      return true;
    }
  }
  return false;
}

FlushStatus YBSession::TEST_FlushAndGetOpsErrors() {
  return FlushFuture().get();
}

Status YBSession::TEST_Flush() {
  auto flush_status = TEST_FlushAndGetOpsErrors();
  if (VLOG_IS_ON(2)) {
    for (auto& error : flush_status.errors) {
      VLOG(2) << "Flush of operation " << error->failed_op().ToString()
              << " failed: " << error->status();
    }
  }
  return std::move(flush_status.status);
}

Status YBSession::TEST_ApplyAndFlush(YBOperationPtr yb_op) {
  Apply(std::move(yb_op));
  return TEST_Flush();
}

Status YBSession::TEST_ApplyAndFlush(const std::vector<YBOperationPtr>& ops) {
  Apply(ops);
  return TEST_Flush();
}

size_t YBSession::TEST_CountBufferedOperations() const {
  return batcher_ ? batcher_->CountBufferedOperations() : 0;
}

bool YBSession::HasNotFlushedOperations() const {
  return batcher_ != nullptr && batcher_->HasPendingOperations();
}

bool YBSession::TEST_HasPendingOperations() const {
  if (batcher_ && batcher_->HasPendingOperations()) {
    return true;
  }
  std::lock_guard l(lock_);
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

} // namespace yb::client
