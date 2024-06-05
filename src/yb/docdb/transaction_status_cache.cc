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
#include "yb/docdb/transaction_status_cache.h"

#include <future>

#include <boost/optional/optional.hpp>

#include "yb/ash/wait_state.h"

#include "yb/common/hybrid_time.h"

#include "yb/docdb/transaction_dump.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

using namespace std::literals;

DEFINE_UNKNOWN_bool(TEST_transaction_allow_rerequest_status, true,
            "Allow rerequest transaction status when TryAgain is received.");

namespace yb {
namespace docdb {

namespace {

Status StatusWaitTimedOut(const TransactionId& transaction_id) {
  return STATUS_FORMAT(
      TimedOut, "Timed out waiting for transaction status: $0", transaction_id);
}

YB_DEFINE_ENUM(CommitTimeSource,
               ((kLocalBefore, 0)) // Transaction was committed locally before the remote check.
               ((kNoMetadata, 1)) // Transaction metadata not present
               ((kLocalAfter, 2)) // Transaction was committed locally after the remote check.
               ((kRemoteAborted, 3)) // Coordinator responded that transaction was aborted.
               ((kRemoteCommitted, 4)) // Coordinator responded that transaction was committed.
               ((kRemotePending, 5))); // Coordinator responded that transaction is pending.

} // namespace

struct TransactionStatusCache::GetCommitDataResult {
  TransactionLocalState transaction_local_state;
  CommitTimeSource source = CommitTimeSource();
  HybridTime status_time;
  HybridTime safe_time;
};

// For locally committed transactions returns commit time if committed at specified time or
// HybridTime::kMin otherwise. For other transactions returns boost::none.
boost::optional<TransactionLocalState> TransactionStatusCache::GetLocalCommitData(
    const TransactionId& transaction_id) {
  auto local_commit_data_opt = txn_context_opt_.txn_status_manager->LocalTxnData(transaction_id);
  if (local_commit_data_opt == boost::none || !local_commit_data_opt->commit_ht.is_valid()) {
    return boost::none;
  }

  if (local_commit_data_opt->commit_ht > read_time_.global_limit) {
    local_commit_data_opt->commit_ht = HybridTime::kMin;
  }

  return local_commit_data_opt;
}

Result<TransactionLocalState> TransactionStatusCache::GetTransactionLocalState(
    const TransactionId& transaction_id) {
  auto it = cache_.find(transaction_id);
  if (it != cache_.end()) {
    return it->second;
  }

  auto result = VERIFY_RESULT(DoGetCommitData(transaction_id));
  YB_TRANSACTION_DUMP(
      Status, txn_context_opt_ ? txn_context_opt_.transaction_id : TransactionId::Nil(),
      read_time_, transaction_id, result.transaction_local_state.commit_ht,
      static_cast<uint8_t>(result.source), result.status_time, result.safe_time,
      result.transaction_local_state.aborted_subtxn_set.ToString());
  cache_.emplace(transaction_id, result.transaction_local_state);
  return result.transaction_local_state;
}

Result<TransactionStatusCache::GetCommitDataResult> TransactionStatusCache::DoGetCommitData(
    const TransactionId& transaction_id) {
  auto local_commit_data_opt = GetLocalCommitData(transaction_id);
  if (local_commit_data_opt != boost::none) {
    return GetCommitDataResult {
      .transaction_local_state = std::move(*local_commit_data_opt),
      .source = CommitTimeSource::kLocalBefore,
      .status_time = {},
      .safe_time = {},
    };
  }

  // Since TransactionStatusResult does not have default ctor we should init it somehow.
  TransactionStatusResult txn_status(TransactionStatus::ABORTED, HybridTime());
  const auto kMaxWait = 50ms * kTimeMultiplier;
  const auto kRequestTimeout = kMaxWait;
  bool TEST_retry_allowed = FLAGS_TEST_transaction_allow_rerequest_status;
  CoarseBackoffWaiter waiter(deadline_, kMaxWait);
  static const std::string kRequestReason = "get commit time"s;
  for(;;) {
    auto txn_status_promise = std::make_shared<std::promise<Result<TransactionStatusResult>>>();
    auto future = txn_status_promise->get_future();
    auto callback = [txn_status_promise](Result<TransactionStatusResult> result) {
      txn_status_promise->set_value(std::move(result));
    };
    txn_context_opt_.txn_status_manager->RequestStatusAt(
        {&transaction_id, read_time_.read, read_time_.global_limit, read_time_.serial_no,
              &kRequestReason,
              TransactionLoadFlags{TransactionLoadFlag::kCleanup},
              callback});
    auto wait_start = CoarseMonoClock::now();
    SCOPED_WAIT_STATUS(TransactionStatusCache_DoGetCommitData);
    auto future_status = future.wait_until(
        TEST_retry_allowed ? wait_start + kRequestTimeout : deadline_);
    if (future_status == std::future_status::ready) {
      auto txn_status_result = future.get();
      if (txn_status_result.ok()) {
        txn_status = *txn_status_result;
        break;
      }
      if (txn_status_result.status().IsNotFound()) {
        // We have intent w/o metadata, that means that transaction was already cleaned up or
        // is being retained for CDC.
        VLOG(4) << "Intent for transaction w/o metadata: " << transaction_id;
        return GetCommitDataResult{
            .transaction_local_state =
                TransactionLocalState{.commit_ht = HybridTime::kMin, .aborted_subtxn_set = {}},
            .source = CommitTimeSource::kNoMetadata,
            .status_time = {},
            .safe_time = {},
        };
      }
      LOG(WARNING)
          << "Failed to request transaction " << transaction_id << " status: "
          <<  txn_status_result.status();
      if (!txn_status_result.status().IsTryAgain()) {
        return std::move(txn_status_result.status());
      }
      if (!waiter.Wait()) {
        return StatusWaitTimedOut(transaction_id);
      }
    } else {
      LOG(INFO) << "TXN: " << transaction_id << ": Timed out waiting txn status, waited: "
                << MonoDelta(CoarseMonoClock::now() - wait_start)
                << ", future status: " << to_underlying(future_status)
                << ", left to deadline: " << MonoDelta(deadline_ - CoarseMonoClock::now());
      if (waiter.ExpiredNow()) {
        return StatusWaitTimedOut(transaction_id);
      }
      waiter.NextAttempt();
    }
    DCHECK(TEST_retry_allowed);
  }
  VLOG(4) << "Transaction_id " << transaction_id << " at " << read_time_
          << ": status: " << TransactionStatus_Name(txn_status.status)
          << ", status_time: " << txn_status.status_time;
  // There could be case when transaction was committed and applied between previous call to
  // GetLocalCommitTime, in this case coordinator does not know transaction and will respond
  // with ABORTED status. So we recheck whether it was committed locally.
  if (txn_status.status == TransactionStatus::ABORTED) {
    HybridTime safe_time;
    if (txn_status.status_time && txn_status.status_time != HybridTime::kMax) {
      // It is possible that this node not yet received APPLY, so it is possible that
      // we would not have local commit time even for committed transaction.
      // Waiting for safe time to be sure that we APPLY was processed if present.
      // See https://github.com/YugaByte/yugabyte-db/issues/7729 for details.
      safe_time = VERIFY_RESULT(txn_context_opt_.txn_status_manager->WaitForSafeTime(
          txn_status.status_time, deadline_));
    }
    local_commit_data_opt = GetLocalCommitData(transaction_id);
    if (local_commit_data_opt != boost::none) {
      return GetCommitDataResult {
        .transaction_local_state = std::move(*local_commit_data_opt),
        .source = CommitTimeSource::kLocalAfter,
        .status_time = txn_status.status_time,
        .safe_time = safe_time,
      };
    }

    return GetCommitDataResult{
        .transaction_local_state =
            TransactionLocalState {.commit_ht = HybridTime::kMin, .aborted_subtxn_set = {}},
        .source = CommitTimeSource::kRemoteAborted,
        .status_time = txn_status.status_time,
        .safe_time = safe_time,
    };
  }

  if (txn_status.status == TransactionStatus::COMMITTED) {
    return GetCommitDataResult {
      .transaction_local_state = TransactionLocalState {
        .commit_ht = txn_status.status_time,
        .aborted_subtxn_set = txn_status.aborted_subtxn_set
      },
      .source = CommitTimeSource::kRemoteCommitted,
      .status_time = {},
      .safe_time = {},
    };
  }

  return GetCommitDataResult{
      // TODO(savepoints) - surface aborted subtxn data for pending transactions.
      .transaction_local_state =
          TransactionLocalState {.commit_ht = HybridTime::kMin, .aborted_subtxn_set = {}},
      .source = CommitTimeSource::kRemotePending,
      .status_time = {},
      .safe_time = {},
  };
}

} // namespace docdb
} // namespace yb
