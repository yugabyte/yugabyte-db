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

#include <algorithm>
#include <string>
#include <vector>
#include <boost/functional/hash.hpp>
#include <gflags/gflags.h>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/walltime.h"
#include "kudu/rpc/outbound_call.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/rpc/rpc_introspection.pb.h"
#include "kudu/rpc/serialization.h"
#include "kudu/rpc/transfer.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/kernel_stack_watchdog.h"

namespace kudu {
namespace rpc {

using strings::Substitute;
using google::protobuf::Message;
using google::protobuf::io::CodedOutputStream;

static const double kMicrosPerSecond = 1000000.0;

// 100M cycles should be about 50ms on a 2Ghz box. This should be high
// enough that involuntary context switches don't trigger it, but low enough
// that any serious blocking behavior on the reactor would.
DEFINE_int64(rpc_callback_max_cycles, 100 * 1000 * 1000,
             "The maximum number of cycles for which an RPC callback "
             "should be allowed to run without emitting a warning."
             " (Advanced debugging option)");
TAG_FLAG(rpc_callback_max_cycles, advanced);
TAG_FLAG(rpc_callback_max_cycles, runtime);

///
/// OutboundCall
///

OutboundCall::OutboundCall(const ConnectionId& conn_id,
                           const RemoteMethod& remote_method,
                           google::protobuf::Message* response_storage,
                           RpcController* controller, ResponseCallback callback)
    : state_(READY),
      remote_method_(remote_method),
      conn_id_(conn_id),
      callback_(std::move(callback)),
      controller_(DCHECK_NOTNULL(controller)),
      response_(DCHECK_NOTNULL(response_storage)) {
  DVLOG(4) << "OutboundCall " << this << " constructed with state_: " << StateName(state_)
           << " and RPC timeout: "
           << (controller->timeout().Initialized() ? controller->timeout().ToString() : "none");
  header_.set_call_id(kInvalidCallId);
  remote_method.ToPB(header_.mutable_remote_method());
  start_time_ = MonoTime::Now(MonoTime::FINE);
}

OutboundCall::~OutboundCall() {
  DCHECK(IsFinished());
  DVLOG(4) << "OutboundCall " << this << " destroyed with state_: " << StateName(state_);
}

Status OutboundCall::SerializeTo(vector<Slice>* slices) {
  size_t param_len = request_buf_.size();
  if (PREDICT_FALSE(param_len == 0)) {
    return Status::InvalidArgument("Must call SetRequestParam() before SerializeTo()");
  }

  const MonoDelta &timeout = controller_->timeout();
  if (timeout.Initialized()) {
    header_.set_timeout_millis(timeout.ToMilliseconds());
  }

  CHECK_OK(serialization::SerializeHeader(header_, param_len, &header_buf_));

  // Return the concatenated packet.
  slices->push_back(Slice(header_buf_));
  slices->push_back(Slice(request_buf_));
  return Status::OK();
}

Status OutboundCall::SetRequestParam(const Message& message) {
  return serialization::SerializeMessage(message, &request_buf_);
}

Status OutboundCall::status() const {
  lock_guard<simple_spinlock> l(&lock_);
  return status_;
}

const ErrorStatusPB* OutboundCall::error_pb() const {
  lock_guard<simple_spinlock> l(&lock_);
  return error_pb_.get();
}


string OutboundCall::StateName(State state) {
  switch (state) {
    case READY:
      return "READY";
    case ON_OUTBOUND_QUEUE:
      return "ON_OUTBOUND_QUEUE";
    case SENT:
      return "SENT";
    case TIMED_OUT:
      return "TIMED_OUT";
    case FINISHED_ERROR:
      return "FINISHED_ERROR";
    case FINISHED_SUCCESS:
      return "FINISHED_SUCCESS";
    default:
      LOG(DFATAL) << "Unknown state in OutboundCall: " << state;
      return StringPrintf("UNKNOWN(%d)", state);
  }
}

void OutboundCall::set_state(State new_state) {
  lock_guard<simple_spinlock> l(&lock_);
  set_state_unlocked(new_state);
}

OutboundCall::State OutboundCall::state() const {
  lock_guard<simple_spinlock> l(&lock_);
  return state_;
}

void OutboundCall::set_state_unlocked(State new_state) {
  // Sanity check state transitions.
  DVLOG(3) << "OutboundCall " << this << " (" << ToString() << ") switching from " <<
    StateName(state_) << " to " << StateName(new_state);
  switch (new_state) {
    case ON_OUTBOUND_QUEUE:
      DCHECK_EQ(state_, READY);
      break;
    case SENT:
      DCHECK_EQ(state_, ON_OUTBOUND_QUEUE);
      break;
    case TIMED_OUT:
      DCHECK(state_ == SENT || state_ == ON_OUTBOUND_QUEUE);
      break;
    case FINISHED_SUCCESS:
      DCHECK_EQ(state_, SENT);
      break;
    default:
      // No sanity checks for others.
      break;
  }

  state_ = new_state;
}

void OutboundCall::CallCallback() {
  int64_t start_cycles = CycleClock::Now();
  {
    SCOPED_WATCH_STACK(100);
    callback_();
    // Clear the callback, since it may be holding onto reference counts
    // via bound parameters. We do this inside the timer because it's possible
    // the user has naughty destructors that block, and we want to account for that
    // time here if they happen to run on this thread.
    callback_ = NULL;
  }
  int64_t end_cycles = CycleClock::Now();
  int64_t wait_cycles = end_cycles - start_cycles;
  if (PREDICT_FALSE(wait_cycles > FLAGS_rpc_callback_max_cycles)) {
    double micros = static_cast<double>(wait_cycles) / base::CyclesPerSecond()
      * kMicrosPerSecond;

    LOG(WARNING) << "RPC callback for " << ToString() << " blocked reactor thread for "
                 << micros << "us";
  }
}

void OutboundCall::SetResponse(gscoped_ptr<CallResponse> resp) {
  call_response_ = resp.Pass();
  Slice r(call_response_->serialized_response());

  if (call_response_->is_success()) {
    // TODO: here we're deserializing the call response within the reactor thread,
    // which isn't great, since it would block processing of other RPCs in parallel.
    // Should look into a way to avoid this.
    if (!response_->ParseFromArray(r.data(), r.size())) {
      SetFailed(Status::IOError("Invalid response, missing fields",
                                response_->InitializationErrorString()));
      return;
    }
    set_state(FINISHED_SUCCESS);
    CallCallback();
  } else {
    // Error
    gscoped_ptr<ErrorStatusPB> err(new ErrorStatusPB());
    if (!err->ParseFromArray(r.data(), r.size())) {
      SetFailed(Status::IOError("Was an RPC error but could not parse error response",
                                err->InitializationErrorString()));
      return;
    }
    ErrorStatusPB* err_raw = err.release();
    SetFailed(Status::RemoteError(err_raw->message()), err_raw);
  }
}

void OutboundCall::SetQueued() {
  set_state(ON_OUTBOUND_QUEUE);
}

void OutboundCall::SetSent() {
  set_state(SENT);

  // This method is called in the reactor thread, so free the header buf,
  // which was also allocated from this thread. tcmalloc's thread caching
  // behavior is a lot more efficient if memory is freed from the same thread
  // which allocated it -- this lets it keep to thread-local operations instead
  // of taking a mutex to put memory back on the global freelist.
  delete [] header_buf_.release();

  // request_buf_ is also done being used here, but since it was allocated by
  // the caller thread, we would rather let that thread free it whenever it
  // deletes the RpcController.
}

void OutboundCall::SetFailed(const Status &status,
                             ErrorStatusPB* err_pb) {
  {
    lock_guard<simple_spinlock> l(&lock_);
    status_ = status;
    if (status_.IsRemoteError()) {
      CHECK(err_pb);
      error_pb_.reset(err_pb);
    } else {
      CHECK(!err_pb);
    }
    set_state_unlocked(FINISHED_ERROR);
  }
  CallCallback();
}

void OutboundCall::SetTimedOut() {
  {
    lock_guard<simple_spinlock> l(&lock_);
    status_ = Status::TimedOut(Substitute(
        "$0 RPC to $1 timed out after $2",
        remote_method_.method_name(),
        conn_id_.remote().ToString(),
        controller_->timeout().ToString()));
    set_state_unlocked(TIMED_OUT);
  }
  CallCallback();
}

bool OutboundCall::IsTimedOut() const {
  lock_guard<simple_spinlock> l(&lock_);
  return state_ == TIMED_OUT;
}

bool OutboundCall::IsFinished() const {
  lock_guard<simple_spinlock> l(&lock_);
  switch (state_) {
    case READY:
    case ON_OUTBOUND_QUEUE:
    case SENT:
      return false;
    case TIMED_OUT:
    case FINISHED_ERROR:
    case FINISHED_SUCCESS:
      return true;
    default:
      LOG(FATAL) << "Unknown call state: " << state_;
      return false;
  }
}

string OutboundCall::ToString() const {
  return Substitute("RPC call $0 -> $1", remote_method_.ToString(), conn_id_.ToString());
}

void OutboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcCallInProgressPB* resp) {
  lock_guard<simple_spinlock> l(&lock_);
  resp->mutable_header()->CopyFrom(header_);
  resp->set_micros_elapsed(
    MonoTime::Now(MonoTime::FINE) .GetDeltaSince(start_time_).ToMicroseconds());
}

///
/// UserCredentials
///

UserCredentials::UserCredentials() {}

bool UserCredentials::has_effective_user() const {
  return !eff_user_.empty();
}

void UserCredentials::set_effective_user(const string& eff_user) {
  eff_user_ = eff_user;
}

bool UserCredentials::has_real_user() const {
  return !real_user_.empty();
}

void UserCredentials::set_real_user(const string& real_user) {
  real_user_ = real_user;
}

bool UserCredentials::has_password() const {
  return !password_.empty();
}

void UserCredentials::set_password(const string& password) {
  password_ = password;
}

void UserCredentials::CopyFrom(const UserCredentials& other) {
  eff_user_ = other.eff_user_;
  real_user_ = other.real_user_;
  password_ = other.password_;
}

string UserCredentials::ToString() const {
  // Does not print the password.
  return StringPrintf("{real_user=%s, eff_user=%s}", real_user_.c_str(), eff_user_.c_str());
}

size_t UserCredentials::HashCode() const {
  size_t seed = 0;
  if (has_effective_user()) {
    boost::hash_combine(seed, effective_user());
  }
  if (has_real_user()) {
    boost::hash_combine(seed, real_user());
  }
  if (has_password()) {
    boost::hash_combine(seed, password());
  }
  return seed;
}

bool UserCredentials::Equals(const UserCredentials& other) const {
  return (effective_user() == other.effective_user()
       && real_user() == other.real_user()
       && password() == other.password());
}

///
/// ConnectionId
///

ConnectionId::ConnectionId() {}

ConnectionId::ConnectionId(const ConnectionId& other) {
  DoCopyFrom(other);
}

ConnectionId::ConnectionId(const Sockaddr& remote, const UserCredentials& user_credentials) {
  remote_ = remote;
  user_credentials_.CopyFrom(user_credentials);
}

void ConnectionId::set_remote(const Sockaddr& remote) {
  remote_ = remote;
}

void ConnectionId::set_user_credentials(const UserCredentials& user_credentials) {
  user_credentials_.CopyFrom(user_credentials);
}

void ConnectionId::CopyFrom(const ConnectionId& other) {
  DoCopyFrom(other);
}

string ConnectionId::ToString() const {
  // Does not print the password.
  return StringPrintf("{remote=%s, user_credentials=%s}",
      remote_.ToString().c_str(),
      user_credentials_.ToString().c_str());
}

void ConnectionId::DoCopyFrom(const ConnectionId& other) {
  remote_ = other.remote_;
  user_credentials_.CopyFrom(other.user_credentials_);
}

size_t ConnectionId::HashCode() const {
  size_t seed = 0;
  boost::hash_combine(seed, remote_.HashCode());
  boost::hash_combine(seed, user_credentials_.HashCode());
  return seed;
}

bool ConnectionId::Equals(const ConnectionId& other) const {
  return (remote() == other.remote()
       && user_credentials().Equals(other.user_credentials()));
}

size_t ConnectionIdHash::operator() (const ConnectionId& conn_id) const {
  return conn_id.HashCode();
}

bool ConnectionIdEqual::operator() (const ConnectionId& cid1, const ConnectionId& cid2) const {
  return cid1.Equals(cid2);
}

///
/// CallResponse
///

CallResponse::CallResponse()
 : parsed_(false) {
}

Status CallResponse::GetSidecar(int idx, Slice* sidecar) const {
  DCHECK(parsed_);
  if (idx < 0 || idx >= header_.sidecar_offsets_size()) {
    return Status::InvalidArgument(strings::Substitute(
        "Index $0 does not reference a valid sidecar", idx));
  }
  *sidecar = sidecar_slices_[idx];
  return Status::OK();
}

Status CallResponse::ParseFrom(gscoped_ptr<InboundTransfer> transfer) {
  CHECK(!parsed_);
  Slice entire_message;
  RETURN_NOT_OK(serialization::ParseMessage(transfer->data(), &header_,
                                            &entire_message));

  // Use information from header to extract the payload slices.
  int last = header_.sidecar_offsets_size() - 1;

  if (last >= OutboundTransfer::kMaxPayloadSlices) {
    return Status::Corruption(strings::Substitute(
        "Received $0 additional payload slices, expected at most %d",
        last, OutboundTransfer::kMaxPayloadSlices));
  }

  if (last >= 0) {
    serialized_response_ = Slice(entire_message.data(),
                                 header_.sidecar_offsets(0));
    for (int i = 0; i < last; ++i) {
      uint32_t next_offset = header_.sidecar_offsets(i);
      int32_t len = header_.sidecar_offsets(i + 1) - next_offset;
      if (next_offset + len > entire_message.size() || len < 0) {
        return Status::Corruption(strings::Substitute(
            "Invalid sidecar offsets; sidecar $0 apparently starts at $1,"
            " has length $2, but the entire message has length $3",
            i, next_offset, len, entire_message.size()));
      }
      sidecar_slices_[i] = Slice(entire_message.data() + next_offset, len);
    }
    uint32_t next_offset = header_.sidecar_offsets(last);
    if (next_offset > entire_message.size()) {
        return Status::Corruption(strings::Substitute(
            "Invalid sidecar offsets; the last sidecar ($0) apparently starts "
            "at $1, but the entire message has length $3",
            last, next_offset, entire_message.size()));
    }
    sidecar_slices_[last] = Slice(entire_message.data() + next_offset,
                                  entire_message.size() - next_offset);
  } else {
    serialized_response_ = entire_message;
  }

  transfer_.swap(transfer);
  parsed_ = true;
  return Status::OK();
}

} // namespace rpc
} // namespace kudu
