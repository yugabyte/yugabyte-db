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
#include <boost/bind.hpp>

#include "yb/client/async_rpc.h"
#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/client/error_collector.h"
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/session-internal.h"
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
                 const std::shared_ptr<YBSession>& session,
                 yb::client::YBSession::ExternalConsistencyMode consistency_mode)
  : state_(kGatheringOps),
    client_(client),
    weak_session_(session),
    consistency_mode_(consistency_mode),
    error_collector_(error_collector),
    had_errors_(false),
    read_only_(session->is_read_only()),
    flush_callback_(NULL),
    next_op_sequence_number_(0),
    outstanding_lookups_(0),
    max_buffer_size_(7 * 1024 * 1024),
    buffer_bytes_used_(0) {
  const auto metric_entity = client_->data_->messenger_->metric_entity();
  async_rpc_metrics_ = metric_entity ? std::make_shared<AsyncRpcMetrics>(metric_entity) : nullptr;
}

void Batcher::Abort() {
  std::unique_lock<simple_spinlock> l(lock_);
  state_ = kAborted;

  vector<InFlightOp*> to_abort;
  for (InFlightOp* op : ops_) {
    std::lock_guard<simple_spinlock> l(op->lock_);
    if (op->state == InFlightOp::kBufferedToTabletServer) {
      to_abort.push_back(op);
    }
  }

  for (InFlightOp* op : to_abort) {
    VLOG(1) << "Aborting op: " << op->ToString();
    MarkInFlightOpFailedUnlocked(op, STATUS(Aborted, "Batch aborted"));
  }

  if (flush_callback_) {
    l.unlock();

    flush_callback_->Run(STATUS(Aborted, ""));
  }
}

Batcher::~Batcher() {
  if (PREDICT_FALSE(!ops_.empty())) {
    for (InFlightOp* op : ops_) {
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
  std::shared_ptr<YBSession> session;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    if (state_ != kFlushing || !ops_.empty()) {
      return;
    }

    session = weak_session_.lock();
    state_ = kFlushed;
  }

  if (session) {
    // Important to do this outside of the lock so that we don't have
    // a lock inversion deadlock -- the session lock should always
    // come before the batcher lock.
    session->data_->FlushFinished(this);
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
    YB_LOG_EVERY_N(WARNING, 1000) << "Client writing with no timeout set, using 60 seconds.\n"
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
  unique_ptr<InFlightOp> in_flight_op(new InFlightOp());
  RETURN_NOT_OK(
      yb_op->table_->partition_schema().EncodeKey(yb_op->row(), &in_flight_op->partition_key));
  in_flight_op->yb_op = yb_op;
  in_flight_op->state = InFlightOp::kLookingUpTablet;

  AddInFlightOp(in_flight_op.get());
  VLOG(3) << "Looking up tablet for " << in_flight_op->yb_op->ToString();

  // Increment our reference count for the outstanding callback.
  //
  // deadline_ is set in FlushAsync(), after all Add() calls are done, so
  // here we're forced to create a new deadline.
  MonoTime deadline = ComputeDeadlineUnlocked();
  base::RefCountInc(&outstanding_lookups_);
  client_->data_->meta_cache_->LookupTabletByKey(
      in_flight_op->yb_op->table(), in_flight_op->partition_key, deadline, &in_flight_op->tablet,
      Bind(&Batcher::TabletLookupFinished, this, in_flight_op.get()));
  IgnoreResult(in_flight_op.release());
  return Status::OK();
}

void Batcher::AddInFlightOp(InFlightOp* op) {
  DCHECK_EQ(op->state, InFlightOp::kLookingUpTablet);

  std::lock_guard<simple_spinlock> l(lock_);
  CHECK_EQ(state_, kGatheringOps);
  InsertOrDie(&ops_, op);
  op->sequence_number_ = next_op_sequence_number_++;
}

bool Batcher::IsAbortedUnlocked() const {
  return state_ == kAborted;
}

void Batcher::MarkHadErrors() {
  std::lock_guard<simple_spinlock> l(lock_);
  had_errors_ = true;
}

void Batcher::MarkInFlightOpFailed(InFlightOp* op, const Status& s) {
  std::lock_guard<simple_spinlock> l(lock_);
  MarkInFlightOpFailedUnlocked(op, s);
}

void Batcher::MarkInFlightOpFailedUnlocked(InFlightOp* in_flight_op, const Status& s) {
  CHECK_EQ(1, ops_.erase(in_flight_op)) << "Could not remove op " << in_flight_op->ToString()
                                        << " from in-flight list";
  gscoped_ptr<YBError> error(new YBError(in_flight_op->yb_op, s));
  error_collector_->AddError(error.Pass());
  had_errors_ = true;
  delete in_flight_op;
}

void Batcher::TabletLookupFinished(InFlightOp* op, const Status& s) {
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
    CHECK_EQ(op->state, InFlightOp::kLookingUpTablet);
    CHECK(op->tablet != NULL);

    op->state = InFlightOp::kBufferedToTabletServer;

    vector<InFlightOp*>& to_ts = per_tablet_ops_[op->tablet.get()];
    to_ts.push_back(op);

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
    for (int i = to_ts.size() - 1; i > 0; --i) {
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

void Batcher::FlushBuffersIfReady() {
  unordered_map<RemoteTablet*, vector<InFlightOp*> > ops_copy;

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
    // Take ownership of the ops while we're under the lock.
    ops_copy.swap(per_tablet_ops_);
  }

  // Now flush the ops for each tablet.
  for (const OpsMap::value_type& e : ops_copy) {
    RemoteTablet* tablet = e.first;
    const vector<InFlightOp*>& ops = e.second;

    VLOG(3) << "FlushBuffersIfReady: already in flushing state, immediately flushing to "
            << tablet->tablet_id();
    FlushBuffer(tablet, ops);
  }
}

void Batcher::FlushBuffer(RemoteTablet* tablet, const vector<InFlightOp*>& ops) {
  CHECK(!ops.empty());

  // Create and send an RPC that aggregates the ops. The RPC is freed when
  // its callback completes.
  //
  // The RPC object takes ownership of the in flight ops.
  // The underlying YB OP is not directly owned, only a reference is kept.
  if (read_only_) {
    ReadRpc* rpc = new ReadRpc(this, tablet, ops, deadline_, client_->data_->messenger_,
                               async_rpc_metrics_);
    rpc->SendRpc();
  } else {
    WriteRpc* rpc = new WriteRpc(this, tablet, ops, deadline_, client_->data_->messenger_,
                                 async_rpc_metrics_);
    rpc->SendRpc();
  }
}

using tserver::ReadResponsePB;

void Batcher::AddOpCountMismatchError() {
  // TODO: how to handle this kind of error where the array of response PB's don't match
  //       the size of the array of requests. We don't have a specific YBOperation to
  //       create an error with, because there are multiple YBOps in one Rpc.
  LOG(FATAL) << "Server sent wrong number of redis responses compared to request";
}

void Batcher::RemoveInFlightOps(const vector<InFlightOp*>& ops) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (InFlightOp* op : ops) {
    CHECK_EQ(1, ops_.erase(op))
      << "Could not remove op " << op->ToString()
      << " from in-flight list";
  }
}

void Batcher::ProcessKuduWriteResponse(const WriteRpc &rpc,
    const Status &s) {
  // TODO: there is a potential race here -- if the Batcher gets destructed while
  // RPCs are in-flight, then accessing state_ will crash. We probably need to keep
  // track of the in-flight RPCs, and in the destructor, change each of them to an
  // "aborted" state.
  CHECK_EQ(state_, kFlushing);

  if (s.ok()) {
    if (rpc.resp().has_timestamp()) {
      client_->data_->UpdateLatestObservedTimestamp(rpc.resp().timestamp());
    }
  } else {
    // Mark each of the rows in the write op as failed, since the whole RPC failed.
    for (InFlightOp* in_flight_op : rpc.ops()) {
      gscoped_ptr<YBError> error(new YBError(in_flight_op->yb_op, s));
      error_collector_->AddError(error.Pass());
    }

    MarkHadErrors();
  }

  // Check individual row errors.
  for (const WriteResponsePB_PerRowErrorPB& err_pb : rpc.resp().per_row_errors()) {
    // TODO: handle case where we get one of the more specific TS errors
    // like the tablet not being hosted?

    if (err_pb.row_index() >= rpc.ops().size()) {
      LOG(ERROR) << "Received a per_row_error for an out-of-bound op index "
                 << err_pb.row_index() << " (sent only "
                 << rpc.ops().size() << " ops)";
      LOG(ERROR) << "Response from tablet " << rpc.tablet()->tablet_id() << ":\n"
                 << rpc.resp().DebugString();
      continue;
    }
    shared_ptr<YBOperation> yb_op = rpc.ops()[err_pb.row_index()]->yb_op;
    VLOG(1) << "Error on op " << yb_op->ToString() << ": " << err_pb.error().ShortDebugString();
    Status op_status = StatusFromPB(err_pb.error());
    gscoped_ptr<YBError> error(new YBError(yb_op, op_status));
    error_collector_->AddError(error.Pass());
    MarkHadErrors();
  }
}

}  // namespace internal
}  // namespace client
}  // namespace yb
