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

#include "kudu/client/batcher.h"

#include <algorithm>
#include <boost/bind.hpp>
#include <glog/logging.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kudu/client/callbacks.h"
#include "kudu/client/client.h"
#include "kudu/client/client-internal.h"
#include "kudu/client/error_collector.h"
#include "kudu/client/meta_cache.h"
#include "kudu/client/session-internal.h"
#include "kudu/client/write_op.h"
#include "kudu/client/write_op-internal.h"
#include "kudu/common/encoded_key.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/rpc.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/util/debug-util.h"
#include "kudu/util/logging.h"

using std::pair;
using std::set;
using std::shared_ptr;
using std::unordered_map;
using strings::Substitute;

namespace kudu {

using rpc::ErrorStatusPB;
using rpc::Messenger;
using rpc::Rpc;
using rpc::RpcController;
using tserver::WriteRequestPB;
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

// An operation which has been submitted to the batcher and not yet completed.
// The operation goes through a state machine as it progress through the
// various stages of a request. See the State enum for details.
//
// Note that in-flight ops *conceptually* hold a reference to the Batcher object.
// However, since there might be millions of these objects floating around,
// we can save a pointer per object by manually incrementing the Batcher ref-count
// when we create the object, and decrementing when we delete it.
struct InFlightOp {
  InFlightOp() : state(kNew) {
  }

  // Lock protecting the internal state of the op.
  // This is necessary since callbacks may fire from IO threads
  // concurrent with the user trying to abort/delete the batch.
  // See comment above about lock ordering.
  simple_spinlock lock_;

  enum State {
    // Newly created op.
    //
    // OWNERSHIP: The op is only in this state when in local function scope (Batcher::Add)
    kNew = 0,

    // Waiting for the MetaCache to determine which tablet ID hosts the row associated
    // with this operation. In the case that the relevant tablet's key range was
    // already cached, this state will be passed through immediately. Otherwise,
    // the op may sit in this state for some amount of time while waiting on the
    // MetaCache to perform an RPC to the master and find the correct tablet.
    //
    // OWNERSHIP: the op is present in the 'ops_' set, and also referenced by the
    // in-flight callback provided to MetaCache.
    kLookingUpTablet,

    // Once the correct tablet has been determined, and the tablet locations have been
    // refreshed, we are ready to send the operation to the server.
    //
    // In MANUAL_FLUSH mode, the operations wait in this state until Flush has been called.
    //
    // In AUTO_FLUSH_BACKGROUND mode, the operations may wait in this state for one of
    // two reasons:
    //
    //   1) There are already too many outstanding RPCs to the given tablet server.
    //
    //      We restrict the number of concurrent RPCs from one client to a given TS
    //      to achieve better batching and throughput.
    //      TODO: not implemented yet
    //
    //   2) Batching delay.
    //
    //      In order to achieve better batching, we do not immediately send a request
    //      to a TS as soon as we have one pending. Instead, we can wait for a configurable
    //      number of milliseconds for more requests to enter the queue for the same TS.
    //      This makes it likely that if a caller simply issues a small number of requests
    //      to the same tablet in AUTO_FLUSH_BACKGROUND mode that we'll batch all of the
    //      requests together in a single RPC.
    //      TODO: not implemented yet
    //
    // OWNERSHIP: When the operation is in this state, it is present in the 'ops_' set
    // and also in the 'per_tablet_ops' map.
    kBufferedToTabletServer,

    // Once the operation has been flushed (either due to explicit Flush() or background flush)
    // it will enter this state.
    //
    // OWNERSHIP: when entering this state, the op is removed from 'per_tablet_ops' map
    // and ownership is transfered to a WriteRPC's 'ops_' vector. The op still
    // remains in the 'ops_' set.
    kRequestSent
  };
  State state;

  // The actual operation.
  gscoped_ptr<KuduWriteOperation> write_op;

  string partition_key;

  // The tablet the operation is destined for.
  // This is only filled in after passing through the kLookingUpTablet state.
  scoped_refptr<RemoteTablet> tablet;

  // Each operation has a unique sequence number which preserves the user's intended
  // order of operations. This is important when multiple operations act on the same row.
  int sequence_number_;

  string ToString() const {
    return strings::Substitute("op[state=$0, write_op=$1]",
                               state, write_op->ToString());
  }
};

// A Write RPC which is in-flight to a tablet. Initially, the RPC is sent
// to the leader replica, but it may be retried with another replica if the
// leader fails.
//
// Keeps a reference on the owning batcher while alive.
class WriteRpc : public Rpc {
 public:
  WriteRpc(const scoped_refptr<Batcher>& batcher,
           RemoteTablet* const tablet,
           vector<InFlightOp*> ops,
           const MonoTime& deadline,
           const shared_ptr<Messenger>& messenger);
  virtual ~WriteRpc();
  virtual void SendRpc() OVERRIDE;
  virtual string ToString() const OVERRIDE;

  const KuduTable* table() const {
    // All of the ops for a given tablet obviously correspond to the same table,
    // so we'll just grab the table from the first.
    return ops_[0]->write_op->table();
  }
  const RemoteTablet* tablet() const { return tablet_; }
  const vector<InFlightOp*>& ops() const { return ops_; }
  const WriteResponsePB& resp() const { return resp_; }

 private:
  // Called when we finish a lookup (to find the new consensus leader). Retries
  // the rpc after a short delay.
  void LookupTabletCb(const Status& status);

  // Called when we finish initializing a TS proxy.
  // Sends the RPC, provided there was no error.
  void InitTSProxyCb(const Status& status);

  // Marks all replicas on current_ts_ as failed and retries the write on a
  // new replica.
  void FailToNewReplica(const Status& reason);

  virtual void SendRpcCb(const Status& status) OVERRIDE;

  // Pointer back to the batcher. Processes the write response when it
  // completes, regardless of success or failure.
  scoped_refptr<Batcher> batcher_;

  // The tablet that should receive this write.
  RemoteTablet* const tablet_;

  // The TS receiving the write. May change if the write is retried.
  RemoteTabletServer* current_ts_;

  // TSes that refused the write because they were followers at the time.
  // Cleared when new consensus configuration information arrives from the master.
  set<RemoteTabletServer*> followers_;

  // Request body.
  WriteRequestPB req_;

  // Response body.
  WriteResponsePB resp_;

  // Operations which were batched into this RPC.
  // These operations are in kRequestSent state.
  vector<InFlightOp*> ops_;
};

WriteRpc::WriteRpc(const scoped_refptr<Batcher>& batcher,
                   RemoteTablet* const tablet,
                   vector<InFlightOp*> ops,
                   const MonoTime& deadline,
                   const shared_ptr<Messenger>& messenger)
    : Rpc(deadline, messenger),
      batcher_(batcher),
      tablet_(tablet),
      current_ts_(NULL),
      ops_(std::move(ops)) {
  const Schema* schema = table()->schema().schema_;

  req_.set_tablet_id(tablet->tablet_id());
  switch (batcher->external_consistency_mode()) {
    case kudu::client::KuduSession::CLIENT_PROPAGATED:
      req_.set_external_consistency_mode(kudu::CLIENT_PROPAGATED);
      break;
    case kudu::client::KuduSession::COMMIT_WAIT:
      req_.set_external_consistency_mode(kudu::COMMIT_WAIT);
      break;
    default:
      LOG(FATAL) << "Unsupported consistency mode: " << batcher->external_consistency_mode();

  }

  // Set up schema
  CHECK_OK(SchemaToPB(*schema, req_.mutable_schema(),
                      SCHEMA_PB_WITHOUT_STORAGE_ATTRIBUTES | SCHEMA_PB_WITHOUT_IDS));

  RowOperationsPB* requested = req_.mutable_row_operations();

  // Add the rows
  int ctr = 0;
  RowOperationsPBEncoder enc(requested);
  for (InFlightOp* op : ops_) {
    const Partition& partition = op->tablet->partition();
    const PartitionSchema& partition_schema = table()->partition_schema();
    const KuduPartialRow& row = op->write_op->row();

#ifndef NDEBUG
    bool partition_contains_row;
    CHECK(partition_schema.PartitionContainsRow(partition, row, &partition_contains_row).ok());
    CHECK(partition_contains_row)
        << "Row " << partition_schema.RowDebugString(row)
        << "not in partition " << partition_schema.PartitionDebugString(partition, *schema);
#endif

    enc.Add(ToInternalWriteType(op->write_op->type()), op->write_op->row());

    // Set the state now, even though we haven't yet sent it -- at this point
    // there is no return, and we're definitely going to send it. If we waited
    // until after we sent it, the RPC callback could fire before we got a chance
    // to change its state to 'sent'.
    op->state = InFlightOp::kRequestSent;
    VLOG(4) << ++ctr << ". Encoded row " << op->write_op->ToString();
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "Created batch for " << tablet->tablet_id() << ":\n"
        << req_.ShortDebugString();
  }
}

WriteRpc::~WriteRpc() {
  STLDeleteElements(&ops_);
}

void WriteRpc::SendRpc() {
  // Choose a destination TS according to the following algorithm:
  // 1. Select the leader, provided:
  //    a. One exists, and
  //    b. It hasn't failed, and
  //    c. It isn't currently marked as a follower.
  // 2. If there's no good leader select another replica, provided:
  //    a. It hasn't failed, and
  //    b. It hasn't rejected our write due to being a follower.
  // 3. Preemptively mark the replica we selected in step 2 as "leader" in the
  //    meta cache, so that our selection remains sticky until the next Master
  //    metadata refresh.
  // 4. If we're out of appropriate replicas, force a lookup to the master
  //    to fetch new consensus configuration information.
  // 5. When the lookup finishes, forget which replicas were followers and
  //    retry the write (i.e. goto 1).
  // 6. If we issue the write and it fails because the destination was a
  //    follower, remember that fact and retry the write (i.e. goto 1).
  // 7. Repeat steps 1-6 until the write succeeds, fails for other reasons,
  //    or the write's deadline expires.
  current_ts_ = tablet_->LeaderTServer();
  if (current_ts_ && ContainsKey(followers_, current_ts_)) {
    VLOG(2) << "Tablet " << tablet_->tablet_id() << ": We have a follower for a leader: "
            << current_ts_->ToString();

    // Mark the node as a follower in the cache so that on the next go-round,
    // LeaderTServer() will not return it as a leader unless a full metadata
    // refresh has occurred. This also avoids LookupTabletByKey() going into
    // "fast path" mode and not actually performing a metadata refresh from the
    // Master when it needs to.
    tablet_->MarkTServerAsFollower(current_ts_);
    current_ts_ = NULL;
  }
  if (!current_ts_) {
    // Try to "guess" the next leader.
    vector<RemoteTabletServer*> replicas;
    tablet_->GetRemoteTabletServers(&replicas);
    for (RemoteTabletServer* ts : replicas) {
      if (!ContainsKey(followers_, ts)) {
        current_ts_ = ts;
        break;
      }
    }
    if (current_ts_) {
      // Mark this next replica "preemptively" as the leader in the meta cache,
      // so we go to it first on the next write if writing was successful.
      VLOG(1) << "Tablet " << tablet_->tablet_id() << ": Previous leader failed. "
              << "Preemptively marking tserver " << current_ts_->ToString()
              << " as leader in the meta cache.";
      tablet_->MarkTServerAsLeader(current_ts_);
    }
  }

  // If we've tried all replicas, force a lookup to the master to find the
  // new leader. This relies on some properties of LookupTabletByKey():
  // 1. The fast path only works when there's a non-failed leader (which we
  //    know is untrue here).
  // 2. The slow path always fetches consensus configuration information and updates the
  //    looked-up tablet.
  // Put another way, we don't care about the lookup results at all; we're
  // just using it to fetch the latest consensus configuration information.
  //
  // TODO: When we support tablet splits, we should let the lookup shift
  // the write to another tablet (i.e. if it's since been split).
  if (!current_ts_) {
    batcher_->client_->data_->meta_cache_->LookupTabletByKey(table(),
                                                             tablet_->partition()
                                                                     .partition_key_start(),
                                                             retrier().deadline(),
                                                             NULL,
                                                             Bind(&WriteRpc::LookupTabletCb,
                                                                  Unretained(this)));
    return;
  }

  // Make sure we have a working proxy before sending out the RPC.
  current_ts_->InitProxy(batcher_->client_,
                         Bind(&WriteRpc::InitTSProxyCb, Unretained(this)));
}

string WriteRpc::ToString() const {
  return Substitute("Write(tablet: $0, num_ops: $1, num_attempts: $2)",
                    tablet_->tablet_id(), ops_.size(), num_attempts());
}

void WriteRpc::LookupTabletCb(const Status& status) {
  // We should retry the RPC regardless of the outcome of the lookup, as
  // leader election doesn't depend on the existence of a master at all.
  //
  // Retry() imposes a slight delay, which is desirable in a lookup loop,
  // but unnecessary the first time through. Seeing as leader failures are
  // rare, perhaps this doesn't matter.
  followers_.clear();
  mutable_retrier()->DelayedRetry(this, status);
}

void WriteRpc::InitTSProxyCb(const Status& status) {
  // Fail to a replica in the event of a DNS resolution failure.
  if (!status.ok()) {
    FailToNewReplica(status);
    return;
  }

  VLOG(2) << "Tablet " << tablet_->tablet_id() << ": Writing batch to replica "
          << current_ts_->ToString();
  current_ts_->proxy()->WriteAsync(req_, &resp_,
                                   mutable_retrier()->mutable_controller(),
                                   boost::bind(&WriteRpc::SendRpcCb, this, Status::OK()));
}

void WriteRpc::FailToNewReplica(const Status& reason) {
  VLOG(1) << "Failing " << ToString() << " to a new replica: "
          << reason.ToString();
  bool found = tablet_->MarkReplicaFailed(current_ts_, reason);
  DCHECK(found)
      << "Tablet " << tablet_->tablet_id() << ": Unable to mark replica " << current_ts_->ToString()
      << " as failed. Replicas: " << tablet_->ReplicasAsString();

  mutable_retrier()->DelayedRetry(this, reason);
}

void WriteRpc::SendRpcCb(const Status& status) {
  // Prefer early failures over controller failures.
  Status new_status = status;
  if (new_status.ok() && mutable_retrier()->HandleResponse(this, &new_status)) {
    return;
  }

  // Failover to a replica in the event of any network failure.
  //
  // TODO: This is probably too harsh; some network failures should be
  // retried on the current replica.
  if (new_status.IsNetworkError()) {
    FailToNewReplica(new_status);
    return;
  }

  // Prefer controller failures over response failures.
  if (new_status.ok() && resp_.has_error()) {
    new_status = StatusFromPB(resp_.error().status());
  }

  // Oops, we failed over to a replica that wasn't a LEADER. Unlikely as
  // we're using consensus configuration information from the master, but still possible
  // (e.g. leader restarted and became a FOLLOWER). Try again.
  //
  // TODO: IllegalState is obviously way too broad an error category for
  // this case.
  if (new_status.IsIllegalState() || new_status.IsAborted()) {
    followers_.insert(current_ts_);
    mutable_retrier()->DelayedRetry(this, new_status);
    return;
  }

  if (!new_status.ok()) {
    string current_ts_string;
    if (current_ts_) {
      current_ts_string = Substitute("on tablet server $0", current_ts_->ToString());
    } else {
      current_ts_string = "(no tablet server available)";
    }
    new_status = new_status.CloneAndPrepend(
        Substitute("Failed to write batch of $0 ops to tablet $1 "
                   "$2 after $3 attempt(s)",
                   ops_.size(), tablet_->tablet_id(),
                   current_ts_string, num_attempts()));
    LOG(WARNING) << new_status.ToString();
  }
  batcher_->ProcessWriteResponse(*this, new_status);
  delete this;
}

Batcher::Batcher(KuduClient* client,
                 ErrorCollector* error_collector,
                 const sp::shared_ptr<KuduSession>& session,
                 kudu::client::KuduSession::ExternalConsistencyMode consistency_mode)
  : state_(kGatheringOps),
    client_(client),
    weak_session_(session),
    consistency_mode_(consistency_mode),
    error_collector_(error_collector),
    had_errors_(false),
    flush_callback_(NULL),
    next_op_sequence_number_(0),
    outstanding_lookups_(0),
    max_buffer_size_(7 * 1024 * 1024),
    buffer_bytes_used_(0) {
}

void Batcher::Abort() {
  unique_lock<simple_spinlock> l(&lock_);
  state_ = kAborted;

  vector<InFlightOp*> to_abort;
  for (InFlightOp* op : ops_) {
    lock_guard<simple_spinlock> l(&op->lock_);
    if (op->state == InFlightOp::kBufferedToTabletServer) {
      to_abort.push_back(op);
    }
  }

  for (InFlightOp* op : to_abort) {
    VLOG(1) << "Aborting op: " << op->ToString();
    MarkInFlightOpFailedUnlocked(op, Status::Aborted("Batch aborted"));
  }

  if (flush_callback_) {
    l.unlock();

    flush_callback_->Run(Status::Aborted(""));
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
  lock_guard<simple_spinlock> l(&lock_);
  timeout_ = MonoDelta::FromMilliseconds(millis);
}


bool Batcher::HasPendingOperations() const {
  lock_guard<simple_spinlock> l(&lock_);
  return !ops_.empty();
}

int Batcher::CountBufferedOperations() const {
  lock_guard<simple_spinlock> l(&lock_);
  if (state_ == kGatheringOps) {
    return ops_.size();
  } else {
    // If we've already started to flush, then the ops aren't
    // considered "buffered".
    return 0;
  }
}

void Batcher::CheckForFinishedFlush() {
  sp::shared_ptr<KuduSession> session;
  {
    lock_guard<simple_spinlock> l(&lock_);
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
    s = Status::IOError("Some errors occurred");
  }

  flush_callback_->Run(s);
}

MonoTime Batcher::ComputeDeadlineUnlocked() const {
  MonoDelta timeout = timeout_;
  if (PREDICT_FALSE(!timeout.Initialized())) {
    KLOG_EVERY_N(WARNING, 1000) << "Client writing with no timeout set, using 60 seconds.\n"
                                << GetStackTrace();
    timeout = MonoDelta::FromSeconds(60);
  }
  MonoTime ret = MonoTime::Now(MonoTime::FINE);
  ret.AddDelta(timeout);
  return ret;
}

void Batcher::FlushAsync(KuduStatusCallback* cb) {
  {
    lock_guard<simple_spinlock> l(&lock_);
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

Status Batcher::Add(KuduWriteOperation* write_op) {
  int64_t required_size = write_op->SizeInBuffer();
  int64_t size_after_adding = buffer_bytes_used_.IncrementBy(required_size);
  if (PREDICT_FALSE(size_after_adding > max_buffer_size_)) {
    buffer_bytes_used_.IncrementBy(-required_size);
    int64_t size_before_adding = size_after_adding - required_size;
    return Status::Incomplete(Substitute(
        "not enough space remaining in buffer for op (required $0, "
        "$1 already used",
        HumanReadableNumBytes::ToString(required_size),
        HumanReadableNumBytes::ToString(size_before_adding)));
  }


  // As soon as we get the op, start looking up where it belongs,
  // so that when the user calls Flush, we are ready to go.
  gscoped_ptr<InFlightOp> op(new InFlightOp());
  RETURN_NOT_OK(write_op->table_->partition_schema()
                .EncodeKey(write_op->row(), &op->partition_key));
  op->write_op.reset(write_op);
  op->state = InFlightOp::kLookingUpTablet;

  AddInFlightOp(op.get());
  VLOG(3) << "Looking up tablet for " << op->write_op->ToString();

  // Increment our reference count for the outstanding callback.
  //
  // deadline_ is set in FlushAsync(), after all Add() calls are done, so
  // here we're forced to create a new deadline.
  MonoTime deadline = ComputeDeadlineUnlocked();
  base::RefCountInc(&outstanding_lookups_);
  client_->data_->meta_cache_->LookupTabletByKey(
      op->write_op->table(),
      op->partition_key,
      deadline,
      &op->tablet,
      Bind(&Batcher::TabletLookupFinished, this, op.get()));
  IgnoreResult(op.release());
  return Status::OK();
}

void Batcher::AddInFlightOp(InFlightOp* op) {
  DCHECK_EQ(op->state, InFlightOp::kLookingUpTablet);

  lock_guard<simple_spinlock> l(&lock_);
  CHECK_EQ(state_, kGatheringOps);
  InsertOrDie(&ops_, op);
  op->sequence_number_ = next_op_sequence_number_++;
}

bool Batcher::IsAbortedUnlocked() const {
  return state_ == kAborted;
}

void Batcher::MarkHadErrors() {
  lock_guard<simple_spinlock> l(&lock_);
  had_errors_ = true;
}

void Batcher::MarkInFlightOpFailed(InFlightOp* op, const Status& s) {
  lock_guard<simple_spinlock> l(&lock_);
  MarkInFlightOpFailedUnlocked(op, s);
}

void Batcher::MarkInFlightOpFailedUnlocked(InFlightOp* op, const Status& s) {
  CHECK_EQ(1, ops_.erase(op))
    << "Could not remove op " << op->ToString() << " from in-flight list";
  gscoped_ptr<KuduError> error(new KuduError(op->write_op.release(), s));
  error_collector_->AddError(error.Pass());
  had_errors_ = true;
  delete op;
}

void Batcher::TabletLookupFinished(InFlightOp* op, const Status& s) {
  base::RefCountDec(&outstanding_lookups_);

  // Acquire the batcher lock early to atomically:
  // 1. Test if the batcher was aborted, and
  // 2. Change the op state.
  unique_lock<simple_spinlock> l(&lock_);

  if (IsAbortedUnlocked()) {
    VLOG(1) << "Aborted batch: TabletLookupFinished for " << op->write_op->ToString();
    MarkInFlightOpFailedUnlocked(op, Status::Aborted("Batch aborted"));
    // 'op' is deleted by above function.
    return;
  }

  if (VLOG_IS_ON(3)) {
    VLOG(3) << "TabletLookupFinished for " << op->write_op->ToString()
            << ": " << s.ToString();
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
    lock_guard<simple_spinlock> l2(&op->lock_);
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
    lock_guard<simple_spinlock> l(&lock_);
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
  // The RPC object takes ownership of the ops.
  WriteRpc* rpc = new WriteRpc(this,
                               tablet,
                               ops,
                               deadline_,
                               client_->data_->messenger_);
  rpc->SendRpc();
}

void Batcher::ProcessWriteResponse(const WriteRpc& rpc,
                                   const Status& s) {
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
    for (InFlightOp* op : rpc.ops()) {
      gscoped_ptr<KuduError> error(new KuduError(op->write_op.release(), s));
      error_collector_->AddError(error.Pass());
    }

    MarkHadErrors();
  }


  // Remove all the ops from the "in-flight" list.
  {
    lock_guard<simple_spinlock> l(&lock_);
    for (InFlightOp* op : rpc.ops()) {
      CHECK_EQ(1, ops_.erase(op))
            << "Could not remove op " << op->ToString()
            << " from in-flight list";
    }
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
    gscoped_ptr<KuduWriteOperation> op = rpc.ops()[err_pb.row_index()]->write_op.Pass();
    VLOG(1) << "Error on op " << op->ToString() << ": "
            << err_pb.error().ShortDebugString();
    Status op_status = StatusFromPB(err_pb.error());
    gscoped_ptr<KuduError> error(new KuduError(op.release(), op_status));
    error_collector_->AddError(error.Pass());
    MarkHadErrors();
  }

  CheckForFinishedFlush();
}

} // namespace internal
} // namespace client
} // namespace kudu
