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
#include <mutex>
#include <vector>
#include <boost/functional/hash.hpp>
#include <gflags/gflags.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"
#include "yb/rpc/constants.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/serialization.h"
#include "yb/util/flag_tags.h"
#include "yb/util/kernel_stack_watchdog.h"
#include "yb/util/trace.h"

METRIC_DEFINE_histogram(
    server, handler_latency_outbound_call_queue_time, "Time taken to queue the request ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue the request to the reactor",
    60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_outbound_call_send_time, "Time taken to send the request ",
    yb::MetricUnit::kMicroseconds, "Microseconds spent to queue and write the request to the wire",
    60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_outbound_call_time_to_response, "Time taken to get the response ",
    yb::MetricUnit::kMicroseconds,
    "Microseconds spent to send the request and get a response on the wire", 60000000LU, 2);

// 100M cycles should be about 50ms on a 2Ghz box. This should be high
// enough that involuntary context switches don't trigger it, but low enough
// that any serious blocking behavior on the reactor would.
DEFINE_int64(
    rpc_callback_max_cycles, 100 * 1000 * 1000,
    "The maximum number of cycles for which an RPC callback "
    "should be allowed to run without emitting a warning."
    " (Advanced debugging option)");
TAG_FLAG(rpc_callback_max_cycles, advanced);
TAG_FLAG(rpc_callback_max_cycles, runtime);
DECLARE_bool(rpc_dump_all_traces);

namespace yb {
namespace rpc {

using strings::Substitute;
using google::protobuf::Message;
using google::protobuf::io::CodedOutputStream;

static const double kMicrosPerSecond = 1000000.0;

OutboundCallMetrics::OutboundCallMetrics(const scoped_refptr<MetricEntity>& entity)
    : queue_time(METRIC_handler_latency_outbound_call_queue_time.Instantiate(entity)),
      send_time(METRIC_handler_latency_outbound_call_send_time.Instantiate(entity)),
      time_to_response(METRIC_handler_latency_outbound_call_time_to_response.Instantiate(entity)) {
}

namespace {

std::atomic<int32_t> call_id_ = {0};

int32_t NextCallId() {
  for (;;) {
    auto result = ++call_id_;
    if (result > 0) {
      return result;
    }
    // When call id overflows, we reset it to zero.
    call_id_.compare_exchange_weak(result, 0);
  }
}

} // namespace

///
/// OutboundCall
///

OutboundCall::OutboundCall(
    const ConnectionId& conn_id, const RemoteMethod& remote_method,
    const std::shared_ptr<OutboundCallMetrics>& outbound_call_metrics,
    google::protobuf::Message* response_storage, RpcController* controller,
    ResponseCallback callback)
    : conn_id_(conn_id),
      start_(MonoTime::Now(MonoTime::FINE)),
      controller_(DCHECK_NOTNULL(controller)),
      response_(DCHECK_NOTNULL(response_storage)),
      state_(READY),
      remote_method_(remote_method),
      callback_(std::move(callback)),
      trace_(new Trace),
      outbound_call_metrics_(outbound_call_metrics) {
  if (PREDICT_FALSE(VLOG_IS_ON(1))) {
    TRACE_TO(trace_, "Outbound Call initiated to $0", conn_id.ToString());
  } else {
    // Avoid expensive conn_id.ToString() in production.
    TRACE_TO(trace_, "Outbound Call initiated.");
  }
  if (Trace::CurrentTrace()) {
    Trace::CurrentTrace()->AddChildTrace(trace_.get());
  }

  DVLOG(4) << "OutboundCall " << this << " constructed with state_: " << StateName(state_)
           << " and RPC timeout: "
           << (controller->timeout().Initialized() ? controller->timeout().ToString() : "none");
  header_.set_call_id(NextCallId());
  remote_method.ToPB(header_.mutable_remote_method());
  start_ = MonoTime::Now(MonoTime::FINE);
}

OutboundCall::~OutboundCall() {
  DCHECK(IsFinished());
  DVLOG(4) << "OutboundCall " << this << " destroyed with state_: " << StateName(state_);

  if (PREDICT_FALSE(FLAGS_rpc_dump_all_traces)) {
    LOG(INFO) << ToString() << " took "
              << MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_).ToMicroseconds()
              << "us. Trace:";
    trace_->Dump(&LOG(INFO), true);
  }
}

void OutboundCall::NotifyTransferred(const Status& status) {
  // TODO: would be better to cancel the transfer while it is still on the queue if we
  // timed out before the transfer started, but there is still a race in the case of
  // a partial send that we have to handle here
  if (IsFinished()) {
    DCHECK(IsTimedOut());
  } else {
    if (status.ok()) {
      SetSent();
    } else {
      VLOG(1) << "Connection torn down before " << ToString()
              << " could send its call: " << status.ToString();

      SetFailed(status);
    }
  }
}

void OutboundCall::Serialize(std::deque<util::RefCntBuffer>* output) const {
  output->push_back(buffer_);
}

Status OutboundCall::SetRequestParam(const Message& message) {
  using serialization::SerializeHeader;
  using serialization::SerializeMessage;

  const MonoDelta &timeout = controller_->timeout();
  if (timeout.Initialized()) {
    header_.set_timeout_millis(timeout.ToMilliseconds());
  }

  size_t message_size = 0;
  auto status = SerializeMessage(message,
                                 /* param_buf */ nullptr,
                                 /* additional_size */ 0,
                                 /* use_cached_size */ false,
                                 /* offset */ 0,
                                 &message_size);
  if (!status.ok()) {
    return status;
  }
  size_t header_size = 0;
  status = SerializeHeader(header_, message_size, &buffer_, message_size, &header_size);
  if (!status.ok()) {
    return status;
  }
  return SerializeMessage(message,
                          &buffer_,
                          /* additional_size */ 0,
                          /* use_cached_size */ true,
                          header_size);
}

Status OutboundCall::status() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return status_;
}

const ErrorStatusPB* OutboundCall::error_pb() const {
  std::lock_guard<simple_spinlock> l(lock_);
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
  std::lock_guard<simple_spinlock> l(lock_);
  set_state_unlocked(new_state);
}

OutboundCall::State OutboundCall::state() const {
  std::lock_guard<simple_spinlock> l(lock_);
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
      DCHECK(state_ == SENT || state_ == ON_OUTBOUND_QUEUE) << "Real state: " << state_;
      break;
    case FINISHED_SUCCESS:
      DCHECK_EQ(state_, SENT);
      break;
    case FINISHED_ERROR:
      DCHECK(state_ == SENT || state_ == ON_OUTBOUND_QUEUE || state_ == READY)
          << "Real state: " << state_;
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
  TRACE_TO(trace_, "Response received.");
  // Track time taken to be responded.

  if (outbound_call_metrics_) {
    outbound_call_metrics_->time_to_response->Increment(
        MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_).ToMicroseconds());
  }
  call_response_ = resp.Pass();
  Slice r(call_response_->serialized_response());

  if (call_response_->is_success()) {
    // TODO: here we're deserializing the call response within the reactor thread,
    // which isn't great, since it would block processing of other RPCs in parallel.
    // Should look into a way to avoid this.
    if (!response_->ParseFromArray(r.data(), r.size())) {
      SetFailed(STATUS(IOError, "Invalid response, missing fields",
                                response_->InitializationErrorString()));
      return;
    }
    set_state(FINISHED_SUCCESS);
    CallCallback();
    TRACE_TO(trace_, "Callback called.");
  } else {
    // Error
    gscoped_ptr<ErrorStatusPB> err(new ErrorStatusPB());
    if (!err->ParseFromArray(r.data(), r.size())) {
      SetFailed(STATUS(IOError, "Was an RPC error but could not parse error response",
                                err->InitializationErrorString()));
      return;
    }
    ErrorStatusPB* err_raw = err.release();
    SetFailed(STATUS(RemoteError, err_raw->message()), err_raw);
  }
}

void OutboundCall::SetQueued() {
  // Track time taken to be queued.
  if (outbound_call_metrics_) {
    auto end_time = MonoTime::Now(MonoTime::FINE);
    outbound_call_metrics_->queue_time->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  }
  set_state(ON_OUTBOUND_QUEUE);
  TRACE_TO(trace_, "Queued.");
}

void OutboundCall::SetSent() {
  // Track time taken to be sent
  if (outbound_call_metrics_) {
    auto end_time = MonoTime::Now(MonoTime::FINE);
    outbound_call_metrics_->send_time->Increment(end_time.GetDeltaSince(start_).ToMicroseconds());
  }
  set_state(SENT);
  TRACE_TO(trace_, "Call Sent.");
}

void OutboundCall::SetFinished() {
  // Track time taken to be responded.
  if (outbound_call_metrics_) {
    outbound_call_metrics_->time_to_response->Increment(
        MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_).ToMicroseconds());
  }
  set_state(FINISHED_SUCCESS);
  CallCallback();
  TRACE_TO(trace_, "Callback called.");
}

void OutboundCall::SetFailed(const Status &status,
                             ErrorStatusPB* err_pb) {
  TRACE_TO(trace_, "Call Failed.");
  {
    std::lock_guard<simple_spinlock> l(lock_);
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
  TRACE_TO(trace_, "Call TimedOut.");
  {
    std::lock_guard<simple_spinlock> l(lock_);
    status_ = STATUS_SUBSTITUTE(TimedOut,
                                "$0 RPC to $1 timed out after $2",
                                remote_method_.method_name(),
                                yb::ToString(conn_id_.remote()),
                                controller_->timeout().ToString());
    set_state_unlocked(TIMED_OUT);
  }
  CallCallback();
}

bool OutboundCall::IsTimedOut() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return state_ == TIMED_OUT;
}

bool OutboundCall::IsFinished() const {
  std::lock_guard<simple_spinlock> l(lock_);
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

Status OutboundCall::GetSidecar(int idx, Slice* sidecar) const {
  return call_response_->GetSidecar(idx, sidecar);
}

string OutboundCall::ToString() const {
  return Substitute("RPC call $0 -> $1 , state=$2.",
                    remote_method_.ToString(), conn_id_.ToString(), StateName(state_));
}

void OutboundCall::DumpPB(const DumpRunningRpcsRequestPB& req,
                          RpcCallInProgressPB* resp) {
  std::lock_guard<simple_spinlock> l(lock_);
  resp->mutable_header()->CopyFrom(header_);
  resp->set_micros_elapsed(MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_).ToMicroseconds());
  if (req.include_traces() && trace_) {
    resp->set_trace_buffer(trace_->DumpToString(true));
  }
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

ConnectionId::ConnectionId(const Endpoint& remote, const UserCredentials& user_credentials) {
  remote_ = remote;
  user_credentials_.CopyFrom(user_credentials);
}

void ConnectionId::set_remote(const Endpoint& remote) {
  remote_ = remote;
}

void ConnectionId::set_user_credentials(const UserCredentials& user_credentials) {
  user_credentials_.CopyFrom(user_credentials);
}

void ConnectionId::set_idx(uint8_t idx) { idx_ = idx; }

void ConnectionId::CopyFrom(const ConnectionId& other) {
  DoCopyFrom(other);
}

string ConnectionId::ToString() const {
  // Does not print the password.
  return Substitute("{remote=$0, user_credentials=$1, idx=$2}",
                    yb::ToString(remote_),
                    user_credentials_.ToString(),
                    idx_);
}

void ConnectionId::DoCopyFrom(const ConnectionId& other) {
  remote_ = other.remote_;
  idx_ = other.idx_;
  user_credentials_.CopyFrom(other.user_credentials_);
}

size_t ConnectionId::HashCode() const {
  size_t seed = 0;
  boost::hash_combine(seed, hash_value(remote_));
  boost::hash_combine(seed, user_credentials_.HashCode());
  boost::hash_combine(seed, idx_);
  return seed;
}

bool ConnectionId::Equals(const ConnectionId& other) const {
  return (remote() == other.remote()
       && user_credentials().Equals(other.user_credentials()))
       && idx_ == other.idx_;
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
    return STATUS(InvalidArgument, strings::Substitute(
        "Index $0 does not reference a valid sidecar", idx));
  }
  *sidecar = sidecar_slices_[idx];
  return Status::OK();
}

Status CallResponse::ParseFrom(Slice source) {
  CHECK(!parsed_);
  Slice entire_message;

  response_data_.assign(source.data(), source.end());
  source = Slice(response_data_.data(), response_data_.size());
  RETURN_NOT_OK(serialization::ParseYBMessage(source, &header_, &entire_message));

  // Use information from header to extract the payload slices.
  const size_t sidecars = header_.sidecar_offsets_size();

  if (sidecars > kMaxSidecarSlices) {
    return STATUS(Corruption, strings::Substitute(
        "Received $0 additional payload slices, expected at most $1",
        sidecars, kMaxSidecarSlices));
  }

  if (sidecars > 0) {
    serialized_response_ = Slice(entire_message.data(),
                                 header_.sidecar_offsets(0));
    for (size_t i = 0; i < sidecars; ++i) {
      size_t begin_offset = header_.sidecar_offsets(i);
      size_t end_offset = i + 1 == sidecars ? entire_message.size()
                                            : header_.sidecar_offsets(i + 1);
      if (end_offset > entire_message.size() || end_offset < begin_offset) {
        return STATUS(Corruption, strings::Substitute(
            "Invalid sidecar offsets; sidecar $0 apparently starts at $1,"
            " ends at $2, but the entire message has length $3",
            i, begin_offset, end_offset, entire_message.size()));
      }
      sidecar_slices_[i] = Slice(entire_message.data() + begin_offset,
                                 entire_message.data() + end_offset);
    }
  } else {
    serialized_response_ = entire_message;
  }

  parsed_ = true;
  return Status::OK();
}

}  // namespace rpc
}  // namespace yb
