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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/client/batcher.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/optional/optional_io.hpp>
#include <boost/range/adaptors.hpp>

#include "yb/client/async_rpc.h"
#include "yb/client/client-internal.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/error.h"
#include "yb/client/error_collector.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/rejection_score_source.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"

// When this flag is set to false and we have separate errors for operation, then batcher would
// report IO Error status. Otherwise we will try to combine errors from separate operation to
// status of batch. Useful in tests, when we don't need complex error analysis.
DEFINE_test_flag(bool, combine_batcher_errors, false,
                 "Whether combine errors into batcher status.");
DEFINE_test_flag(double, simulate_tablet_lookup_does_not_match_partition_key_probability, 0.0,
                 "Probability for simulating the error that happens when a key is not in the key "
                 "range of the resolved tablet's partition.");

using std::pair;
using std::set;
using std::unique_ptr;
using std::shared_ptr;
using std::unordered_map;
using strings::Substitute;

using namespace std::placeholders;

namespace yb {

using tserver::WriteResponsePB;
using tserver::WriteResponsePB_PerRowErrorPB;

namespace client {

namespace internal {

// TODO: instead of using a string error message, make Batcher return a status other than IOError.
// (https://github.com/YugaByte/yugabyte-db/issues/702)
const std::string Batcher::kErrorReachingOutToTServersMsg(
    "Errors occurred while reaching out to the tablet servers");

namespace {

const auto kGeneralErrorStatus = STATUS(IOError, Batcher::kErrorReachingOutToTServersMsg);

}  // namespace

// About lock ordering in this file:
// ------------------------------
// The locks must be acquired in the following order:
//   - Batcher::lock_
//   - InFlightOp::lock_
//
// It's generally important to release all the locks before either calling
// a user callback, or chaining to another async function, since that function
// may also chain directly to the callback. Without releasing locks first,
// the lock ordering may be violated, or a lock may deadlock on itself (these
// locks are non-reentrant).
// ------------------------------------------------------------

Batcher::Batcher(YBClient* client,
                 const YBSessionPtr& session,
                 YBTransactionPtr transaction,
                 ConsistentReadPoint* read_point,
                 bool force_consistent_read)
  : client_(client),
    weak_session_(session),
    async_rpc_metrics_(session->async_rpc_metrics()),
    transaction_(std::move(transaction)),
    read_point_(read_point),
    force_consistent_read_(force_consistent_read) {
}

Batcher::~Batcher() {
  LOG_IF_WITH_PREFIX(DFATAL, outstanding_rpcs_ != 0)
      << "Destroying batcher with running rpcs: " << outstanding_rpcs_;
  CHECK(
      state_ == BatcherState::kComplete || state_ == BatcherState::kAborted ||
      state_ == BatcherState::kGatheringOps)
      << "Bad state: " << state_;

  RequestsFinished();
}

void Batcher::Abort(const Status& status) {
  for (auto& op : ops_queue_) {
    error_collector_.AddError(op.yb_op, status);
  }
  combined_error_ = status;
  state_ = BatcherState::kAborted;
  FlushFinished();
}

void Batcher::SetDeadline(CoarseTimePoint deadline) {
  deadline_ = deadline;
}

bool Batcher::HasPendingOperations() const {
  return !ops_.empty();
}

size_t Batcher::CountBufferedOperations() const {
  if (state_ == BatcherState::kGatheringOps) {
    return ops_.size();
  } else {
    // If we've already started to flush, then the ops aren't
    // considered "buffered".
    return 0;
  }
}

void Batcher::FlushFinished() {
  if (state_ != BatcherState::kAborted) {
    state_ = BatcherState::kComplete;
  }

  YBSessionPtr session = weak_session_.lock();
  if (session) {
    // Important to do this outside of the lock so that we don't have
    // a lock inversion deadlock -- the session lock should always
    // come before the batcher lock.
    session->FlushFinished(shared_from_this());
  }

  if (combined_error_.ok() && error_collector_.CountErrors() != 0) {
    // In the general case, the user is responsible for fetching errors from the error collector.
    // TODO: use the Combined status here, so it is easy to recognize.
    // https://github.com/YugaByte/yugabyte-db/issues/702
    combined_error_ = kGeneralErrorStatus;
  }

  RunCallback();
}

void Batcher::Run() {
  flush_callback_(combined_error_);
  flush_callback_ = StatusFunctor();
}

void Batcher::RunCallback() {
  VLOG_WITH_PREFIX_AND_FUNC(4) << combined_error_;

  if (!client_->callback_threadpool() ||
      !client_->callback_threadpool()->Submit(shared_from_this()).ok()) {
    Run();
  }
}

void Batcher::FlushAsync(
    StatusFunctor callback, const IsWithinTransactionRetry is_within_transaction_retry) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << "is_within_transaction_retry: " << is_within_transaction_retry;

  CHECK_EQ(state_, BatcherState::kGatheringOps);
  state_ = BatcherState::kResolvingTablets;

  const auto operations_count = ops_.size();
  outstanding_lookups_ = operations_count;

  flush_callback_ = std::move(callback);
  auto session = weak_session_.lock();

  if (session) {
    // Important to do this outside of the lock so that we don't have
    // a lock inversion deadlock -- the session lock should always
    // come before the batcher lock.
    session->FlushStarted(shared_from_this());
  }

  auto transaction = this->transaction();
  // If YBSession retries previously failed ops within the same transaction, these ops are already
  // expected by transaction.
  if (transaction && !is_within_transaction_retry) {
    transaction->batcher_if().ExpectOperations(operations_count);
  }

  ops_queue_.reserve(ops_.size());
  for (auto& yb_op : ops_) {
    ops_queue_.emplace_back(yb_op, ops_queue_.size());
    auto& in_flight_op = ops_queue_.back();
    auto status = yb_op->GetPartitionKey(&in_flight_op.partition_key);

    if (status.ok() && yb_op->table()->partition_schema().IsHashPartitioning()) {
      if (in_flight_op.partition_key.empty()) {
        if (!yb_op->read_only()) {
          status = STATUS_FORMAT(IllegalState, "Hash partition key is empty for $0", yb_op);
        }
      } else {
        yb_op->SetHashCode(PartitionSchema::DecodeMultiColumnHashValue(in_flight_op.partition_key));
      }
    }

    if (!status.ok()) {
      combined_error_ = status;
      FlushFinished();
      return;
    }
  }

  for (auto& op : ops_queue_) {
    VLOG_WITH_PREFIX(4) << "Looking up tablet for " << op.ToString()
                        << " partition key: " << Slice(op.partition_key).ToDebugHexString();

    if (op.yb_op->tablet()) {
      TabletLookupFinished(&op, op.yb_op->tablet());
    } else {
      LookupTabletFor(&op);
    }
  }
}

bool Batcher::Has(const std::shared_ptr<YBOperation>& yb_op) const {
  for (const auto& op : ops_) {
    if (op == yb_op) {
      return true;
    }
  }
  return false;
}

void Batcher::Add(std::shared_ptr<YBOperation> op) {
  if (state_ != BatcherState::kGatheringOps) {
    LOG_WITH_PREFIX(DFATAL)
        << "Adding op to batcher in a wrong state: " << state_ << "\n" << GetStackTrace();
    return;
  }

  ops_.push_back(op);
}

void Batcher::CombineError(const InFlightOp& in_flight_op) {
  if (ClientError(in_flight_op.error) == ClientErrorCode::kTablePartitionListIsStale) {
    // MetaCache returns ClientErrorCode::kTablePartitionListIsStale error for tablet lookup request
    // in case GetTabletLocations from master returns newer version of table partitions.
    // Since MetaCache has no write access to YBTable, it just returns an error which we receive
    // here and mark the table partitions as stale, so they will be refetched on retry.
    in_flight_op.yb_op->MarkTablePartitionListAsStale();
  }

  error_collector_.AddError(in_flight_op.yb_op, in_flight_op.error);
  if (FLAGS_TEST_combine_batcher_errors) {
    if (combined_error_.ok()) {
      combined_error_ = in_flight_op.error.CloneAndPrepend(in_flight_op.ToString());
    } else if (!combined_error_.IsCombined() &&
               combined_error_.code() != in_flight_op.error.code()) {
      combined_error_ = STATUS(Combined, "Multiple failures");
    }
  }
}

void Batcher::LookupTabletFor(InFlightOp* op) {
  auto shared_this = shared_from_this();
  TracePtr trace(Trace::CurrentTrace());
  client_->data_->meta_cache_->LookupTabletByKey(
      op->yb_op->mutable_table(), op->partition_key, deadline_,
      [shared_this, op, trace](const auto& lookup_result) {
        ADOPT_TRACE(trace.get());
        shared_this->TabletLookupFinished(op, lookup_result);
      },
      FailOnPartitionListRefreshed::kTrue);
}

void Batcher::TabletLookupFinished(
    InFlightOp* op, Result<internal::RemoteTabletPtr> lookup_result) {
  VLOG_WITH_PREFIX_AND_FUNC(lookup_result.ok() ? 4 : 3)
      << "Op: " << op->ToString() << ", result: " << AsString(lookup_result);

  if (lookup_result.ok()) {
    op->tablet = *lookup_result;
  } else {
    auto status = lookup_result.status();
    if (ClientError(status) == ClientErrorCode::kTablePartitionListRefreshed) {
      status = client::IsTolerantToPartitionsChange(*op->yb_op) ?
          Status::OK() :
          op->yb_op->GetPartitionKey(&op->partition_key);
      if (status.ok()) {
        LookupTabletFor(op);
        return;
      }
    }
    op->error = std::move(status);
  }
  if (--outstanding_lookups_ == 0) {
    AllLookupsDone();
  }
}

void Batcher::TransactionReady(const Status& status) {
  if (status.ok()) {
    ExecuteOperations(Initial::kFalse);
  } else {
    Abort(status);
  }
}

std::pair<std::map<PartitionKey, Status>, std::map<RetryableRequestId, Status>>
    Batcher::CollectOpsErrors() {
  std::map<PartitionKey, Status> errors_by_partition_key;
  std::map<RetryableRequestId, Status> errors_by_request_id;
  for (auto& op : ops_queue_) {
    if (op.tablet) {
      const Partition& partition = op.tablet->partition();

      bool partition_contains_row = false;
      const auto& partition_key = op.partition_key;
      switch (op.yb_op->type()) {
        case YBOperation::QL_READ: FALLTHROUGH_INTENDED;
        case YBOperation::QL_WRITE: FALLTHROUGH_INTENDED;
        case YBOperation::PGSQL_READ: FALLTHROUGH_INTENDED;
        case YBOperation::PGSQL_WRITE: FALLTHROUGH_INTENDED;
        case YBOperation::REDIS_READ: FALLTHROUGH_INTENDED;
        case YBOperation::REDIS_WRITE: {
          partition_contains_row = partition.ContainsKey(partition_key);
          break;
        }
      }

      if (!partition_contains_row ||
          (PREDICT_FALSE(
              RandomActWithProbability(
                  FLAGS_TEST_simulate_tablet_lookup_does_not_match_partition_key_probability) &&
              op.yb_op->table()->name().namespace_name() == "yb_test"))) {
        const Schema& schema = GetSchema(op.yb_op->table()->schema());
        const PartitionSchema& partition_schema = op.yb_op->table()->partition_schema();
        const auto msg = Format(
            "Row $0 not in partition $1, partition key: $2, tablet: $3",
            op.yb_op->ToString(),
            partition_schema.PartitionDebugString(partition, schema),
            Slice(partition_key).ToDebugHexString(),
            op.tablet->tablet_id());
        LOG_WITH_PREFIX(DFATAL) << msg;
        op.error = STATUS(InternalError, msg);
      }
    }

    if (!op.error.ok()) {
      errors_by_partition_key.emplace(op.partition_key, op.error);
      // Write operations under retrying with the retry batcher should have a valid request_id
      // for de-duplication at server side. All operations in this batcher having same request
      // id should be executed by a single WriteRpc.
      if (op.yb_op->request_id().has_value()) {
        errors_by_request_id.emplace(*op.yb_op->request_id(), op.error);
      }
    }
  }

  return std::make_pair(errors_by_partition_key, errors_by_request_id);
}

void Batcher::AllLookupsDone() {
  // We're only ready to flush if both of the following conditions are true:
  // 1. The batcher is in the "resolving tablets" state (i.e. FlushAsync was called).
  // 2. All outstanding ops have finished lookup. Why? To avoid a situation
  //    where ops are flushed one by one as they finish lookup.

  if (state_ != BatcherState::kResolvingTablets) {
    LOG(DFATAL) << __func__ << " is invoked in wrong state: " << state_;
    return;
  }

  auto errors_pair = CollectOpsErrors();
  const auto& errors_by_partition_key = errors_pair.first;
  const auto& errors_by_request_id = errors_pair.second;

  state_ = BatcherState::kTransactionPrepare;

  VLOG_WITH_PREFIX_AND_FUNC(4) << "Number of partition keys with lookup errors: "
                               << errors_by_partition_key.size()
                               << ", ops queue: " << ops_queue_.size();

  if (!errors_by_partition_key.empty()) {
    // If some operation tablet lookup failed - set this error for all operations designated for
    // the same partition key. We are doing this to keep guarantee on the order of ops for the
    // same partition key (see InFlightOp::sequence_number_).
    // Also set this error for all operations with same request id. This happens when retrying
    // a batcher, we do this to avoid the request id is marked as replicated at server side
    // before all operations with this request id are all done.
    EraseIf([this, &errors_by_partition_key, &errors_by_request_id](auto& op) {
      if (op.error.ok()) {
        const auto lookup_error_it = errors_by_partition_key.find(op.partition_key);
        if (lookup_error_it != errors_by_partition_key.end()) {
          op.error = lookup_error_it->second;
        } else if (op.yb_op->request_id().has_value()) {
          const auto lookup_error_by_request_id_it =
              errors_by_request_id.find(*op.yb_op->request_id());
          if (lookup_error_by_request_id_it != errors_by_request_id.end()) {
            op.error = lookup_error_by_request_id_it->second;
          }
        }
      }
      if (!op.error.ok()) {
        this->CombineError(op);
        return true;
      }
      return false;
    }, &ops_queue_);
  }

  // Checking if ops_queue_ is empty after processing potential errors, because if some operation
  // tablet lookup failed, ops_queue_ could become empty inside `if (had_errors_) { ... }` block
  // above.
  if (ops_queue_.empty()) {
    FlushFinished();
    return;
  }

  // All operations were added, and tablets for them were resolved.
  // So we could sort them.
  std::sort(ops_queue_.begin(),
            ops_queue_.end(),
            [](const InFlightOp& lhs, const InFlightOp& rhs) {
    if (lhs.tablet.get() == rhs.tablet.get()) {
      auto lgroup = lhs.yb_op->group();
      auto rgroup = rhs.yb_op->group();
      if (lgroup != rgroup) {
        return lgroup < rgroup;
      }
      return lhs.sequence_number < rhs.sequence_number;
    }
    return lhs.tablet.get() < rhs.tablet.get();
  });

  auto group_start = ops_queue_.begin();
  auto current_group = (*group_start).yb_op->group();
  const auto* current_tablet = (*group_start).tablet.get();
  for (auto it = group_start; it != ops_queue_.end(); ++it) {
    const auto it_group = (*it).yb_op->group();
    const auto* it_tablet = (*it).tablet.get();
    const auto it_table_partition_list_version = (*it).yb_op->partition_list_version();
    if (it_table_partition_list_version.has_value() &&
        it_table_partition_list_version != it_tablet->partition_list_version()) {
      Abort(STATUS_EC_FORMAT(
          Aborted, ClientError(ClientErrorCode::kTablePartitionListVersionDoesNotMatch),
          "Operation $0 requested table partition list version $1, but ours is: $2",
          (*it).yb_op, it_table_partition_list_version.value(),
          it_tablet->partition_list_version()));
      return;
    }
    if (current_tablet != it_tablet || current_group != it_group) {
      ops_info_.groups.emplace_back(group_start, it);
      group_start = it;
      current_group = it_group;
      current_tablet = it_tablet;
    }
  }
  ops_info_.groups.emplace_back(group_start, ops_queue_.end());

  ExecuteOperations(Initial::kTrue);
}

void Batcher::ExecuteOperations(Initial initial) {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "initial: " << initial;
  auto transaction = this->transaction();
  ADOPT_TRACE(transaction ? transaction->trace() : Trace::CurrentTrace());
  if (transaction) {
    // If this Batcher is executed in context of transaction,
    // then this transaction should initialize metadata used by RPC calls.
    //
    // If transaction is not yet ready to do it, then it will notify as via provided when
    // it could be done.
    if (!transaction->batcher_if().Prepare(
        &ops_info_, force_consistent_read_, deadline_, initial,
        std::bind(&Batcher::TransactionReady, shared_from_this(), _1))) {
      return;
    }
  } else if (force_consistent_read_ &&
             ops_info_.groups.size() > 1 &&
             read_point_ &&
             !read_point_->GetReadTime()) {
    // Read time is not set but consistent read from multiple tablets without
    // transaction is required. Use current time as a read time.
    // Note: read_point_ is null in case of initdb. Nothing to do in this case.
    read_point_->SetCurrentReadTime();
    VLOG_WITH_PREFIX_AND_FUNC(3) << "Set current read time as a read time: "
                                 << read_point_->GetReadTime();
  }

  if (state_ != BatcherState::kTransactionPrepare) {
    // Batcher was aborted.
    LOG_IF(DFATAL, state_ != BatcherState::kAborted)
        << "Batcher in a wrong state at the moment the transaction became ready: " << state_;
    return;
  }
  state_ = BatcherState::kTransactionReady;

  const bool force_consistent_read = force_consistent_read_ || this->transaction();

  // Use big enough value for preallocated storage, to avoid unnecessary allocations.
  boost::container::small_vector<std::shared_ptr<AsyncRpc>,
                                 InFlightOpsGroupsWithMetadata::kPreallocatedCapacity> rpcs;
  rpcs.reserve(ops_info_.groups.size());

  // Now flush the ops for each group.
  // Consistent read is not required when whole batch fits into one command.
  const auto need_consistent_read = force_consistent_read || ops_info_.groups.size() > 1;

  auto self = shared_from_this();
  for (const auto& group : ops_info_.groups) {
    // Allow local calls for last group only.
    const auto allow_local_calls =
        allow_local_calls_in_curr_thread_ && (&group == &ops_info_.groups.back());
    rpcs.push_back(CreateRpc(
        self, group.begin->tablet.get(), group, allow_local_calls, need_consistent_read));
  }

  outstanding_rpcs_.store(rpcs.size());
  for (const auto& rpc : rpcs) {
    rpc->SendRpc();
  }
}

rpc::Messenger* Batcher::messenger() const {
  return client_->messenger();
}

rpc::ProxyCache& Batcher::proxy_cache() const {
  return client_->proxy_cache();
}

YBTransactionPtr Batcher::transaction() const {
  return transaction_;
}

const std::string& Batcher::proxy_uuid() const {
  return client_->proxy_uuid();
}

const ClientId& Batcher::client_id() const {
  return client_->id();
}

server::Clock* Batcher::Clock() const {
  return client_->Clock();
}

std::pair<RetryableRequestId, RetryableRequestId> Batcher::NextRequestIdAndMinRunningRequestId() {
  return client_->NextRequestIdAndMinRunningRequestId();
}

void Batcher::RequestsFinished() {
  client_->RequestsFinished(retryable_requests_ | boost::adaptors::map_keys);
}

void Batcher::MoveRequestDetailsFrom(const BatcherPtr& other, RetryableRequestId id) {
  auto it = other->retryable_requests_.find(id);
  if (it == other->retryable_requests_.end()) {
    // The request id has been moved.
    DCHECK(retryable_requests_.contains(id));
    return;
  }
  retryable_requests_.insert(std::move(*it));
  other->retryable_requests_.erase(it);
}

std::shared_ptr<AsyncRpc> Batcher::CreateRpc(
    const BatcherPtr& self, RemoteTablet* tablet, const InFlightOpsGroup& group,
    const bool allow_local_calls_in_curr_thread, const bool need_consistent_read) {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "tablet: " << tablet->tablet_id();

  CHECK(group.begin != group.end);

  // Create and send an RPC that aggregates the ops. The RPC is freed when
  // its callback completes.
  //
  // The RPC object takes ownership of the in flight ops.
  // The underlying YB OP is not directly owned, only a reference is kept.

  // Split the read operations according to consistency levels since based on consistency
  // levels the read algorithm would differ.
  const auto op_group = (*group.begin).yb_op->group();
  AsyncRpcData data {
    .batcher = self,
    .tablet = tablet,
    .allow_local_calls_in_curr_thread = allow_local_calls_in_curr_thread,
    .need_consistent_read = need_consistent_read,
    .ops = InFlightOps(group.begin, group.end),
    .need_metadata = group.need_metadata
  };

  switch (op_group) {
    case OpGroup::kWrite:
      return std::make_shared<WriteRpc>(data);
    case OpGroup::kLeaderRead:
      return std::make_shared<ReadRpc>(data, YBConsistencyLevel::STRONG);
    case OpGroup::kConsistentPrefixRead:
      return std::make_shared<ReadRpc>(data, YBConsistencyLevel::CONSISTENT_PREFIX);
  }
  FATAL_INVALID_ENUM_VALUE(OpGroup, op_group);
}

using tserver::ReadResponsePB;

void Batcher::AddOpCountMismatchError() {
  // TODO: how to handle this kind of error where the array of response PB's don't match
  //       the size of the array of requests. We don't have a specific YBOperation to
  //       create an error with, because there are multiple YBOps in one Rpc.
  LOG_WITH_PREFIX(DFATAL) << "Received wrong number of responses compared to request(s) sent.";
}

// Invoked when all RPCs are responded, so no other methods should be run in parallel to it.
void Batcher::Flushed(
    const InFlightOps& ops, const Status& status, FlushExtraResult flush_extra_result) {
  auto transaction = this->transaction();
  if (transaction) {
    const auto ops_will_be_retried = !status.ok() && ShouldSessionRetryError(status);
    if (!ops_will_be_retried) {
      // We don't call Transaction::Flushed for ops that will be retried within the same
      // transaction in order to keep transaction running until we finally retry all operations
      // successfully or decide to fail and abort the transaction.
      // We also don't call Transaction::Flushed for ops that have been retried, but failed during
      // the retry.
      // See comments for YBTransaction::Impl::running_requests_ and
      // YBSession::AddErrorsAndRunCallback.
      // https://github.com/yugabyte/yugabyte-db/issues/7984.
      transaction->batcher_if().Flushed(ops, flush_extra_result.used_read_time, status);
    }
  }
  if (status.ok() && read_point_) {
    read_point_->UpdateClock(flush_extra_result.propagated_hybrid_time);
  }

  if (--outstanding_rpcs_ == 0) {
    for (auto& op : ops_queue_) {
      if (!op.error.ok()) {
        CombineError(op);
      }
    }
    FlushFinished();
  }
}

void Batcher::ProcessRpcStatus(const AsyncRpc &rpc, const Status &s) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << "rpc: " << AsString(rpc) << ", status: " << s;

  if (state_ != BatcherState::kTransactionReady) {
    LOG_WITH_PREFIX(DFATAL) << "ProcessRpcStatus in wrong state " << ToString(state_) << ": "
                            << rpc.ToString() << ", " << s;
    return;
  }

  if (PREDICT_FALSE(!s.ok())) {
    // Mark each of the ops as failed, since the whole RPC failed.
    for (auto& in_flight_op : rpc.ops()) {
      in_flight_op.error = s;
    }
  }
}

void Batcher::ProcessReadResponse(const ReadRpc &rpc, const Status &s) {
  ProcessRpcStatus(rpc, s);
}

void Batcher::ProcessWriteResponse(const WriteRpc &rpc, const Status &s) {
  ProcessRpcStatus(rpc, s);

  if (s.ok() && rpc.resp().has_propagated_hybrid_time()) {
    client_->data_->UpdateLatestObservedHybridTime(rpc.resp().propagated_hybrid_time());
  }

  // Check individual row errors.
  for (const auto& err_pb : rpc.resp().per_row_errors()) {
    // TODO: handle case where we get one of the more specific TS errors
    // like the tablet not being hosted?

    size_t row_index = err_pb.row_index();
    if (row_index >= rpc.ops().size()) {
      LOG_WITH_PREFIX(ERROR) << "Received a per_row_error for an out-of-bound op index "
                             << row_index << " (sent only " << rpc.ops().size() << " ops)";
      LOG_WITH_PREFIX(ERROR) << "Response from tablet " << rpc.tablet().tablet_id() << ":\n"
                             << rpc.resp().DebugString();
      continue;
    }
    shared_ptr<YBOperation> yb_op = rpc.ops()[row_index].yb_op;
    VLOG_WITH_PREFIX(1) << "Error on op " << yb_op->ToString() << ": "
                        << err_pb.error().ShortDebugString();
    rpc.ops()[err_pb.row_index()].error = StatusFromPB(err_pb.error());
  }
}

double Batcher::RejectionScore(int attempt_num) {
  if (!rejection_score_source_) {
    return 0.0;
  }

  return rejection_score_source_->Get(attempt_num);
}

CollectedErrors Batcher::GetAndClearPendingErrors() {
  return error_collector_.GetAndClearErrors();
}

std::string Batcher::LogPrefix() const {
  const void* self = this;
  return Format(
      "Batcher ($0), session ($1): ", self, static_cast<void*>(weak_session_.lock().get()));
}

InFlightOpsGroup::InFlightOpsGroup(const Iterator& group_begin, const Iterator& group_end)
    : begin(group_begin), end(group_end) {
}

std::string InFlightOpsGroup::ToString() const {
  return Format("{items: $0 need_metadata: $1}",
                AsString(boost::make_iterator_range(begin, end)),
                need_metadata);
}

}  // namespace internal
}  // namespace client
}  // namespace yb
