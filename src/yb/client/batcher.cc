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

#include <glog/logging.h>

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/error_collector.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/session-internal.h"
#include "yb/client/transaction.h"

#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/join.h"
#include "yb/util/debug-util.h"
#include "yb/util/logging.h"

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
                 ErrorCollector* error_collector,
                 const std::shared_ptr<YBSessionData>& session_data,
                 yb::client::YBSession::ExternalConsistencyMode consistency_mode)
  : state_(kGatheringOps),
    client_(client),
    weak_session_data_(session_data),
    consistency_mode_(consistency_mode),
    error_collector_(error_collector),
    had_errors_(false),
    read_only_(session_data->is_read_only()),
    flush_callback_(NULL),
    next_op_sequence_number_(0),
    outstanding_lookups_(0),
    max_buffer_size_(7 * 1024 * 1024),
    buffer_bytes_used_(0) {
  const auto metric_entity = client_->data_->messenger_->metric_entity();
  async_rpc_metrics_ = metric_entity ? std::make_shared<AsyncRpcMetrics>(metric_entity) : nullptr;
}

void Batcher::Abort(const Status& status) {
  std::unique_lock<simple_spinlock> l(lock_);
  state_ = kAborted;

  InFlightOps to_abort;
  for (auto& op : ops_) {
    std::lock_guard<simple_spinlock> l(op->lock_);
    if (op->state == InFlightOpState::kBufferedToTabletServer) {
      to_abort.push_back(op);
    }
  }

  for (auto& op : to_abort) {
    VLOG(1) << "Aborting op: " << op->ToString();
    MarkInFlightOpFailedUnlocked(op, status);
  }

  if (flush_callback_) {
    l.unlock();

    flush_callback_->Run(status);
  }
}

Batcher::~Batcher() {
  if (PREDICT_FALSE(!ops_.empty())) {
    for (auto& op : ops_) {
      LOG(ERROR) << "Orphaned op: " << op->ToString();
    }
    LOG(FATAL) << "ops_ not empty";
  }
  CHECK(state_ == kFlushed || state_ == kAborted) << "Bad state: " << state_;
}

void Batcher::SetTimeoutMillis(int millis) {
  CHECK_GE(millis, 0);
  std::lock_guard<simple_spinlock> l(lock_);
  timeout_ = MonoDelta::FromMilliseconds(millis);
}


bool Batcher::HasPendingOperations() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !ops_.empty();
}

int Batcher::CountBufferedOperations() const {
  std::lock_guard<simple_spinlock> l(lock_);
  if (state_ == kGatheringOps) {
    return ops_.size();
  } else {
    // If we've already started to flush, then the ops aren't
    // considered "buffered".
    return 0;
  }
}

void Batcher::CheckForFinishedFlush() {
  std::shared_ptr<YBSessionData> session_data;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    if (state_ != kFlushing || !ops_.empty()) {
      return;
    }

    session_data = weak_session_data_.lock();
    state_ = kFlushed;
  }

  if (session_data) {
    // Important to do this outside of the lock so that we don't have
    // a lock inversion deadlock -- the session lock should always
    // come before the batcher lock.
    session_data->FlushFinished(this);
  }

  Status s;
  if (had_errors_) {
    // User is responsible for fetching errors from the error collector.
    s = STATUS(IOError, "Some errors occurred");
  }

  flush_callback_->Run(s);
}

MonoTime Batcher::ComputeDeadlineUnlocked() const {
  MonoDelta timeout = timeout_;
  if (PREDICT_FALSE(!timeout.Initialized())) {
    YB_LOG_EVERY_N(WARNING, 100000) << "Client writing with no timeout set, using 60 seconds.\n"
                                    << GetStackTrace();
    timeout = MonoDelta::FromSeconds(60);
  }
  MonoTime ret = MonoTime::Now(MonoTime::FINE);
  ret.AddDelta(timeout);
  return ret;
}

void Batcher::FlushAsync(YBStatusCallback* cb) {
  {
    std::lock_guard<simple_spinlock> l(lock_);
    CHECK_EQ(state_, kGatheringOps);
    state_ = kFlushing;
    flush_callback_ = cb;
    deadline_ = ComputeDeadlineUnlocked();
  }

  // In the case that we have nothing buffered, just call the callback
  // immediately. Otherwise, the callback will be called by the last callback
  // when it sees that the ops_ list has drained.
  CheckForFinishedFlush();

  // Trigger flushing of all of the buffers. Some of these may already have
  // been flushed through an async path, but it's idempotent - a second call
  // to flush would just be a no-op.
  //
  // If some of the operations are still in-flight, then they'll get sent
  // when they hit 'per_tablet_ops', since our state is now kFlushing.
  FlushBuffersIfReady();
}

Status Batcher::Add(shared_ptr<YBOperation> yb_op) {
  int64_t required_size = yb_op->SizeInBuffer();
  int64_t size_after_adding = buffer_bytes_used_.IncrementBy(required_size);
  if (PREDICT_FALSE(size_after_adding > max_buffer_size_)) {
    buffer_bytes_used_.IncrementBy(-required_size);
    int64_t size_before_adding = size_after_adding - required_size;
    return STATUS(Incomplete, Substitute(
        "not enough space remaining in buffer for op (required $0, "
        "$1 already used",
        HumanReadableNumBytes::ToString(required_size),
        HumanReadableNumBytes::ToString(size_before_adding)));
  }


  // As soon as we get the op, start looking up where it belongs,
  // so that when the user calls Flush, we are ready to go.
  auto in_flight_op = std::make_shared<InFlightOp>();
  RETURN_NOT_OK(yb_op->GetPartitionKey(&in_flight_op->partition_key));
  in_flight_op->yb_op = yb_op;
  in_flight_op->state = InFlightOpState::kLookingUpTablet;

  if (yb_op->type() == YBOperation::Type::QL_READ) {
    if (!in_flight_op->partition_key.empty()) {
      down_cast<YBqlOp *>(yb_op.get())->SetHashCode(
        PartitionSchema::DecodeMultiColumnHashValue(in_flight_op->partition_key));
    }
  } else if (yb_op->type() == YBOperation::Type::QL_WRITE) {
    down_cast<YBqlOp*>(yb_op.get())->SetHashCode(
      PartitionSchema::DecodeMultiColumnHashValue(in_flight_op->partition_key));
  } else if (yb_op->type() == YBOperation::Type::REDIS_READ) {
    down_cast<YBRedisReadOp*>(yb_op.get())->SetHashCode(
      PartitionSchema::DecodeMultiColumnHashValue(in_flight_op->partition_key));
  } else if (yb_op->type() == YBOperation::Type::REDIS_WRITE) {
    down_cast<YBRedisWriteOp*>(yb_op.get())->SetHashCode(
      PartitionSchema::DecodeMultiColumnHashValue(in_flight_op->partition_key));
  }

  AddInFlightOp(in_flight_op);
  VLOG(3) << "Looking up tablet for " << in_flight_op->yb_op->ToString();

  // Increment our reference count for the outstanding callback.
  base::RefCountInc(&outstanding_lookups_);
  if (yb_op->tablet()) {
    in_flight_op->tablet = yb_op->tablet();
    TabletLookupFinished(std::move(in_flight_op), Status::OK());
  } else {
    // deadline_ is set in FlushAsync(), after all Add() calls are done, so
    // here we're forced to create a new deadline.
    MonoTime deadline = ComputeDeadlineUnlocked();
    client_->data_->meta_cache_->LookupTabletByKey(
        in_flight_op->yb_op->table(), in_flight_op->partition_key, deadline, &in_flight_op->tablet,
        Bind(&Batcher::TabletLookupFinished, this, in_flight_op));
  }
  return Status::OK();
}

void Batcher::AddInFlightOp(const InFlightOpPtr& op) {
  DCHECK_EQ(op->state, InFlightOpState::kLookingUpTablet);

  std::lock_guard<simple_spinlock> l(lock_);
  CHECK_EQ(state_, kGatheringOps);
  CHECK(ops_.insert(op).second);
  op->sequence_number_ = next_op_sequence_number_++;
}

bool Batcher::IsAbortedUnlocked() const {
  return state_ == kAborted;
}

void Batcher::MarkHadErrors() {
  std::lock_guard<simple_spinlock> l(lock_);
  had_errors_ = true;
}

void Batcher::MarkInFlightOpFailed(const InFlightOpPtr& op, const Status& s) {
  std::lock_guard<simple_spinlock> l(lock_);
  MarkInFlightOpFailedUnlocked(op, s);
}

void Batcher::MarkInFlightOpFailedUnlocked(const InFlightOpPtr& in_flight_op, const Status& s) {
  CHECK_EQ(1, ops_.erase(in_flight_op)) << "Could not remove op " << in_flight_op->ToString()
                                        << " from in-flight list";
  error_collector_->AddError(in_flight_op->yb_op, s);
  had_errors_ = true;
}

void Batcher::TabletLookupFinished(InFlightOpPtr op, const Status& s) {
  base::RefCountDec(&outstanding_lookups_);

  // Acquire the batcher lock early to atomically:
  // 1. Test if the batcher was aborted, and
  // 2. Change the op state.
  std::unique_lock<simple_spinlock> l(lock_);

  if (IsAbortedUnlocked()) {
    VLOG(1) << "Aborted batch: TabletLookupFinished for " << op->yb_op->ToString();
    MarkInFlightOpFailedUnlocked(op, STATUS(Aborted, "Batch aborted"));
    // 'op' is deleted by above function.
    return;
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "TabletLookupFinished for " << op->yb_op->ToString() << ": " << s.ToString();
    if (s.ok()) {
      VLOG(3) << "Result: tablet_id = " << op->tablet->tablet_id();
    }
  }

  if (!s.ok()) {
    MarkInFlightOpFailedUnlocked(op, s);
    l.unlock();
    CheckForFinishedFlush();

    // Even if we failed our lookup, it's possible that other requests were still
    // pending waiting for our pending lookup to complete. So, we have to let them
    // proceed.
    FlushBuffersIfReady();
    return;
  }

  {
    std::lock_guard<simple_spinlock> l2(op->lock_);
    CHECK_EQ(op->state, InFlightOpState::kLookingUpTablet);
    CHECK(op->tablet != NULL);

    op->state = InFlightOpState::kBufferedToTabletServer;

    InFlightOps& to_ts = per_tablet_ops_[op->tablet.get()];
    to_ts.push_back(std::move(op));

    // "Reverse bubble sort" the operation into the right spot in the tablet server's
    // buffer, based on the sequence numbers of the ops.
    //
    // There is a rare race (KUDU-743) where two operations in the same batch can get
    // their order inverted with respect to the order that the user originally performed
    // the operations. This loop re-sequences them back into the correct order. In
    // the common case, it will break on the first iteration, so we expect the loop to be
    // constant time, with worst case O(n). This is usually much better than something
    // like a priority queue which would have O(lg n) in every case and a more complex
    // code path.
    for (size_t i = to_ts.size() - 1; i > 0; --i) {
      if (to_ts[i]->sequence_number_ < to_ts[i - 1]->sequence_number_) {
        std::swap(to_ts[i], to_ts[i - 1]);
      } else {
        break;
      }
    }
  }

  l.unlock();

  FlushBuffersIfReady();
}

void Batcher::TransactionReady(const Status& status, const BatcherPtr& self) {
  if (status.ok()) {
    FlushBuffersIfReady();
  } else {
    Abort(status);
  }
}

void Batcher::FlushBuffersIfReady() {
  std::unordered_map<RemoteTablet*, InFlightOps> ops_copy;

  // We're only ready to flush if:
  // 1. The batcher is in the flushing state (i.e. FlushAsync was called).
  // 2. All outstanding ops have finished lookup. Why? To avoid a situation
  //    where ops are flushed one by one as they finish lookup.
  {
    std::lock_guard<simple_spinlock> l(lock_);
    if (state_ != kFlushing) {
      VLOG(3) << "FlushBuffersIfReady: batcher not yet in flushing state";
      return;
    }

    if (!base::RefCountIsZero(&outstanding_lookups_)) {
      VLOG(3) << "FlushBuffersIfReady: "
              << base::subtle::NoBarrier_Load(&outstanding_lookups_)
              << " ops still in lookup";
      return;
    }

    auto transaction = this->transaction();
    if (transaction) {
      // If this Batcher is executed in context of transaction,
      // then this transaction should initialize metadata used by RPC calls.
      //
      // If transaction is not yet ready to do it, then it will notify as via provided when
      // it could be done.
      if (!transaction->Prepare(ops_,
                                std::bind(&Batcher::TransactionReady,
                                          this,
                                          _1,
                                          BatcherPtr(this)),
                                &transaction_metadata_)) {
        return;
      }
    }

    // Take ownership of the ops while we're under the lock.
    ops_copy.swap(per_tablet_ops_);
  }

  // Now flush the ops for each tablet.
  for (const OpsMap::value_type& e : ops_copy) {
    RemoteTablet* tablet = e.first;
    const InFlightOps& ops = e.second;

    VLOG(3) << "FlushBuffersIfReady: already in flushing state, immediately flushing to "
            << tablet->tablet_id();
    FlushBuffer(tablet, ops);
  }
}

const std::shared_ptr<rpc::Messenger>& Batcher::messenger() const {
  return client_->messenger();
}

YBTransactionPtr Batcher::transaction() const {
  auto session_data = weak_session_data_.lock();
  if (session_data) {
    return session_data->transaction_;
  }
  return nullptr;
}

void Batcher::FlushBuffer(RemoteTablet* tablet, const InFlightOps& ops) {
  CHECK(!ops.empty());

  // Create and send an RPC that aggregates the ops. The RPC is freed when
  // its callback completes.
  //
  // The RPC object takes ownership of the in flight ops.
  // The underlying YB OP is not directly owned, only a reference is kept.
  if (read_only_) {
    // Split the read operations according to consistency levels since based on consistency
    // levels the read algorithm would differ.
    InFlightOps leader_only_reads;
    InFlightOps consistent_prefix_reads;
    for (InFlightOpPtr op : ops) {
      if (op->yb_op->type() == YBOperation::Type::QL_READ &&
          std::static_pointer_cast<YBqlReadOp>(op->yb_op)->yb_consistency_level() ==
              YBConsistencyLevel::CONSISTENT_PREFIX) {
        consistent_prefix_reads.push_back(op);
      } else {
        leader_only_reads.push_back(op);
      }
    }

    if (!leader_only_reads.empty()) {
      ReadRpc* rpc = new ReadRpc(this, tablet, leader_only_reads);
      rpc->SendRpc();
    }

    if (!consistent_prefix_reads.empty()) {
      ReadRpc* consistent_prefix_rpc =
          new ReadRpc(this, tablet, consistent_prefix_reads, YBConsistencyLevel::CONSISTENT_PREFIX);
      consistent_prefix_rpc->SendRpc();
    }
  } else {
    AsyncRpc* rpc = new WriteRpc(this, tablet, ops);
    rpc->SendRpc();
  }
}


using tserver::ReadResponsePB;

void Batcher::AddOpCountMismatchError() {
  // TODO: how to handle this kind of error where the array of response PB's don't match
  //       the size of the array of requests. We don't have a specific YBOperation to
  //       create an error with, because there are multiple YBOps in one Rpc.
  LOG(ERROR) << "Received wrong number of responses compared to request(s) sent.";
  DCHECK(false);
}

void Batcher::RemoveInFlightOpsAfterFlushing(const InFlightOps& ops, const Status& status) {
  {
    std::lock_guard<simple_spinlock> l(lock_);
    for (auto& op : ops) {
      CHECK_EQ(1, ops_.erase(op))
        << "Could not remove op " << op->ToString() << " from in-flight list";
    }
  }
  auto transaction = this->transaction();
  if (transaction) {
    transaction->Flushed(ops, status);
  }
}

void Batcher::ProcessRpcStatus(const AsyncRpc &rpc, const Status &s) {
  // TODO: there is a potential race here -- if the Batcher gets destructed while
  // RPCs are in-flight, then accessing state_ will crash. We probably need to keep
  // track of the in-flight RPCs, and in the destructor, change each of them to an
  // "aborted" state.
  CHECK_EQ(state_, kFlushing);

  if (PREDICT_FALSE(!s.ok())) {
    // Mark each of the ops as failed, since the whole RPC failed.
    for (auto& in_flight_op : rpc.ops()) {
      error_collector_->AddError(in_flight_op->yb_op, s);
    }
    MarkHadErrors();
  }
}

void Batcher::ProcessReadResponse(const ReadRpc &rpc, const Status &s) {
  ProcessRpcStatus(rpc, s);
}

void Batcher::ProcessWriteResponse(const WriteRpc &rpc, const Status &s) {
  ProcessRpcStatus(rpc, s);

  if (s.ok() && rpc.resp().has_hybrid_time()) {
    client_->data_->UpdateLatestObservedHybridTime(rpc.resp().hybrid_time());
  }

  // Check individual row errors.
  for (const WriteResponsePB_PerRowErrorPB& err_pb : rpc.resp().per_row_errors()) {
    // TODO: handle case where we get one of the more specific TS errors
    // like the tablet not being hosted?

    if (err_pb.row_index() >= rpc.ops().size()) {
      LOG(ERROR) << "Received a per_row_error for an out-of-bound op index "
                 << err_pb.row_index() << " (sent only "
                 << rpc.ops().size() << " ops)";
      LOG(ERROR) << "Response from tablet " << rpc.tablet().tablet_id() << ":\n"
                 << rpc.resp().DebugString();
      continue;
    }
    shared_ptr<YBOperation> yb_op = rpc.ops()[err_pb.row_index()]->yb_op;
    VLOG(1) << "Error on op " << yb_op->ToString() << ": " << err_pb.error().ShortDebugString();
    Status op_status = StatusFromPB(err_pb.error());
    error_collector_->AddError(yb_op, op_status);
    MarkHadErrors();
  }
}

}  // namespace internal
}  // namespace client
}  // namespace yb
