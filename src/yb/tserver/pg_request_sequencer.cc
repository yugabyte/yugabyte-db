// Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/pg_request_sequencer.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <tuple>
#include <type_traits>
#include <variant>

#include "yb/gutil/map-util.h"

#include "yb/tserver/pg_client_session.h"
#include "yb/tserver/pg_session_guard.h"

#include "yb/util/debug-util.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/tostring.h"

namespace yb::tserver {
namespace {

using SerialNo = PgRequestSequencer::SequenceNum::SerialNo;

Status MakeRequestRejectStatus(SerialNo serial_no) {
  return STATUS_FORMAT(
      TimedOut, "Predecessor request for $0 was not applied in time", serial_no);
}

static const auto kShutdownStatus = STATUS(ShutdownInProgress, "Shutting down");

class AsyncExecution {
 public:
  AsyncExecution(PgRequestSequencer::Executor&& executor, CoarseTimePoint deadline)
      : executor_(std::move(executor)), deadline_(deadline) {}

  void Execute(PgClientSession& session, CoarseTimePoint now, SerialNo serial_no) {
    if (deadline_ <= now) {
      executor_(MakeRequestRejectStatus(serial_no));
      return;
    }
    executor_(session);
  }

  void Execute(const Status& status) {
    DCHECK(!status.ok());
    executor_(status);
  }

 private:
  PgRequestSequencer::Executor executor_;
  CoarseTimePoint deadline_;
};

struct PlaceholderState {
  explicit PlaceholderState(CoarseTimePoint deadline_) : deadline(deadline_) {}

  CoarseTimePoint deadline;
};

struct RequestState {
  explicit RequestState(std::optional<AsyncExecution>&& async_) : async(std::move(async_)) {}

  bool is_ready_to_process{false};
  std::optional<AsyncExecution> async;
};

class PendingRequest {
  template <class S, class Self>
  [[nodiscard]] auto* Get(this Self& self) {
    return std::get_if<S>(&self.state_);
  }

 public:
  explicit PendingRequest(CoarseTimePoint wait_deadline)
      : state_(std::in_place_type<PlaceholderState>, wait_deadline) {}

  [[nodiscard]] std::optional<CoarseTimePoint> PlaceholderExpiration() const {
    const auto* state = Get<PlaceholderState>();
    return state ? std::optional(state->deadline) : std::nullopt;
  }

  [[nodiscard]] bool HasDependentRequest() const {
    return dependent_request_is_ready_to_process_;
  }

  void ConvertToRequest(PendingRequest* predecessor, std::optional<AsyncExecution>&& async) {
    DCHECK(!Get<RequestState>());
    auto& state = state_.emplace<RequestState>(std::move(async));
    if (predecessor) {
      DCHECK(!predecessor->dependent_request_is_ready_to_process_);
      predecessor->dependent_request_is_ready_to_process_ = &state.is_ready_to_process;
    } else {
      state.is_ready_to_process = true;
    }
  }

  void ConvertRequestToFakeAsync(CoarseTimePoint deadline) {
    auto* state = Get<RequestState>();
    DCHECK(state && !state->async);
    state->async.emplace([](const auto&) {}, deadline);
  }

  [[nodiscard]] bool TryProcess(PgClientSession& session, CoarseTimePoint now, SerialNo serial_no) {
    if (const auto* state = Get<PlaceholderState>(); state) {
      return state->deadline <= now;
    }

    auto& state = *DCHECK_NOTNULL(Get<RequestState>());
    if (state.is_ready_to_process && state.async) {
      state.async->Execute(session, now, serial_no);
      return true;
    }
    return false;
  }

  [[nodiscard]] bool TryCompleteOnShutdown() {
    auto* state = Get<RequestState>();
    if (state) {
      if (!state->is_ready_to_process) {
        state->is_ready_to_process = true;
      }
      if (!state->async) {
        dependent_request_is_ready_to_process_ = nullptr;
        return false;
      }
      state->async->Execute(kShutdownStatus);
    }
    return true;
  }

  void OnCompleted() {
    [[maybe_unused]] const auto* state = Get<RequestState>();
    DCHECK(!state || state->is_ready_to_process);
    if (dependent_request_is_ready_to_process_) {
      DCHECK(!*dependent_request_is_ready_to_process_);
      *dependent_request_is_ready_to_process_ = true;
    }
  }

  [[nodiscard]] bool IsRequestReadyToProcess() const {
    const auto* state = Get<RequestState>();
    return state && state->is_ready_to_process;
  }

 private:
  bool* dependent_request_is_ready_to_process_{nullptr};
  std::variant<PlaceholderState, RequestState> state_;
};

std::ostream& operator<<(std::ostream& str, PgRequestSequencer::SequenceNum sequence_num) {
  str << "[serial_no: " << sequence_num.serial_no;
  if (sequence_num.predecessor_serial_no) {
    str << ", predecessor: " << *sequence_num.predecessor_serial_no;
  }
  return str << "]";
}

YB_DEFINE_ENUM(PendingRequestsState, (kEmpty)(kNonEmpty)(kHasReadyToProcess));

} // namespace

std::ostream& operator<<(std::ostream& str, PgRequestSequencer::LogPrefix prefix) {
  return str << "Session id " << prefix.session_id_ << ": ";
}

class PgRequestSequencer::Impl {
  using PendingRequests = std::map<SerialNo, PendingRequest>;

 public:
  Impl(CoarseDuration wait_duration, PgRequestSequencer::LogPrefix log_prefix)
      : log_prefix_(log_prefix), wait_duration_(wait_duration) {
    VLOG_WITH_PREFIX_AND_FUNC(3) << "wait_duration=" << AsString(wait_duration_);
  }

  Result<bool> TryRegisterForProcessing(SequenceNum sequence_num) {
    VLOG_WITH_PREFIX_AND_FUNC(3) << "sequence_num=" << sequence_num;
    return HandleNewRequest(&Impl::DoTryRegisterForProcessing, sequence_num);
  }

  Status RegisterForProcessing(
      SequenceNum sequence_num, PgSessionGuard& guard, CoarseTimePoint deadline) {
    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "sequence_num=" << sequence_num << " deadline=" << AsString(deadline);
    std::optional<PgSessionGuard::LostOwnership> ownership;
    return HandleNewRequest(
        &Impl::DoRegisterForProcessing, sequence_num, guard, deadline, ownership);
  }

  Status Enqueue(SequenceNum sequence_num, Executor&& executor, CoarseTimePoint deadline) {
    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "sequence_num=" << sequence_num << " deadline=" << AsString(deadline);
    return HandleNewRequest(&Impl::DoEnqueue, sequence_num, std::move(executor), deadline);
  }

  void ProcessPending(PgClientSession& session) {
    VLOG_WITH_PREFIX_AND_FUNC(3) << "pending request count=" << pending_.size();
    const auto now = CoarseMonoClock::Now();
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->second.TryProcess(session, now, it->first)) {
        VLOG_WITH_PREFIX(5) << "request with serial_no=" << it->first << " processed";
        it = DropCompleted(it);
        continue;
      }
      ++it;
    }
    UpdatePendingRequestsState();
  }

  [[nodiscard]] bool DoIsProcessingRequired(bool is_regular_check) const {
    const auto value = pending_requests_state_.load(std::memory_order_acquire);
    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "pending_requests_state=" << AsString(value)
        << " is_regular_check=" << AsString(is_regular_check);
    switch(value) {
      case PendingRequestsState::kEmpty:
        return false;
      case PendingRequestsState::kNonEmpty:
        return is_regular_check;
      case PendingRequestsState::kHasReadyToProcess:
        return true;
    }
    FATAL_INVALID_ENUM_VALUE(PendingRequestsState, value);
  }

  void Shutdown() {
    VLOG_WITH_PREFIX_AND_FUNC(3);
    is_active_ = false;
    pending_requests_state_changed_ = true;
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (it->second.TryCompleteOnShutdown()) {
        it = DropCompleted(it);
      } else {
        ++it;
      }
    }
    UpdatePendingRequestsState();
  }

 private:
  const PgRequestSequencer::LogPrefix& LogPrefix() const { return log_prefix_; }

  void UpdatePendingRequestsState() {
    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "pending_requests_state_changed_=" << pending_requests_state_changed_;
    if (!pending_requests_state_changed_) {
      return;
    }
    pending_requests_state_changed_ = false;
    auto new_state = PendingRequestsState::kEmpty;
    if (!pending_.empty()) {
      new_state = PendingRequestsState::kNonEmpty;
      for (const auto& [_, req] : pending_) {
        if (req.IsRequestReadyToProcess()) {
          new_state = PendingRequestsState::kHasReadyToProcess;
          sync_request_cond_.notify_all();
          break;
        }
      }
      if (!is_active_) {
        new_state = PendingRequestsState::kEmpty;
      }
    }
    pending_requests_state_.store(new_state, std::memory_order_release);
  }

  void MarkPendingRequestsStateChanged() {
    pending_requests_state_changed_ = true;
  }

  PendingRequests::iterator DropCompleted(PendingRequests::iterator it) {
    DCHECK(it != pending_.end());
    VLOG_WITH_PREFIX_AND_FUNC(3) << "serial_no=" << it->first;
    it->second.OnCompleted();
    MarkPendingRequestsStateChanged();
    return pending_.erase(it);
  }

  Result<PendingRequests::iterator> Register(
      SequenceNum sequence_num, std::optional<AsyncExecution>&& async = {}) {
    VLOG_WITH_PREFIX_AND_FUNC(3) << "sequence_num=" << sequence_num;
    const auto& serial_no = sequence_num.serial_no;
    const auto& predecessor = sequence_num.predecessor_serial_no;
    const auto deadline = CoarseMonoClock::Now() + wait_duration_;
    auto it = pending_.end();
    if (!last_seen_ || *last_seen_ < serial_no) {
      for (auto i : std::views::iota(last_seen_ ? (*last_seen_ + 1) : 0, serial_no + 1)) {
        it = AddRequestPlaceholder(i, deadline);
      }
      last_seen_ = serial_no;
    } else {
      it = pending_.find(serial_no);
    }
    DCHECK(it != pending_.end());
    it->second.ConvertToRequest(
        predecessor ? FindOrNull(pending_, *predecessor) : nullptr, std::move(async));
    MarkPendingRequestsStateChanged();
    return it;
  }

  [[nodiscard]] PendingRequests::iterator AddRequestPlaceholder(
      SerialNo serial_no, CoarseTimePoint deadline) {
    VLOG_WITH_PREFIX_AND_FUNC(3)
        << "serial_no: " << serial_no << ", deadline: " << ToString(deadline);
    auto ipair = pending_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(serial_no), std::forward_as_tuple(deadline));
    DCHECK(ipair.second);
    MarkPendingRequestsStateChanged();
    return ipair.first;
  }

  [[nodiscard]] bool IsAlreadyProcessed(SerialNo serial_no) const {
    return last_seen_ && serial_no <= *last_seen_ && !pending_.contains(serial_no);
  }

  Status DoEnqueue(SequenceNum sequence_num, Executor&& executor, CoarseTimePoint deadline) {
    return ResultToStatus(Register(sequence_num, AsyncExecution{std::move(executor), deadline}));
  }

  Result<bool> DoTryRegisterForProcessing(SequenceNum sequence_num) {
    if (sequence_num.predecessor_serial_no &&
        !IsAlreadyProcessed(*sequence_num.predecessor_serial_no)) {
      return false;
    }
    DropCompleted(VERIFY_RESULT(Register(sequence_num)));
    return true;
  }

  Status DoRegisterForProcessing(
      SequenceNum sequence_num, PgSessionGuard& guard, CoarseTimePoint deadline,
      std::optional<PgSessionGuard::LostOwnership>& ownership) {
    DCHECK(!ownership);
    if (VERIFY_RESULT(DoTryRegisterForProcessing(sequence_num))) {
      return Status::OK();
    }
    auto it = VERIFY_RESULT(Register(sequence_num));
    auto& req = it->second;
    const auto serial_no = sequence_num.serial_no;
    auto pred = [this, serial_no, &req] {
      DCHECK(pending_.contains(serial_no))
          << "serial_no " << serial_no << " unexpectedly gone";
      return req.IsRequestReadyToProcess();
    };
    UpdatePendingRequestsState();
    auto [result, lost_ownership] = guard.WaitUntil(sync_request_cond_, deadline, pred);
    if (lost_ownership.has_value()) [[unlikely]] {
      ownership.emplace(std::move(*lost_ownership));
    }
    DCHECK(pending_.contains(serial_no));
    if (!result) {
      DCHECK(!req.IsRequestReadyToProcess());
      // The predecessor is not yet completed, current request should not be completed. Because this
      // will allow further (dependent) requests to be processed, and this will break execution
      // sequence.
      // Convert current node to a fake async executor to wait for predecessor completion.
      req.ConvertRequestToFakeAsync(deadline);
    } else {
      DropCompleted(it);
    }
    if (!is_active_) {
      return kShutdownStatus;
    }
    if (!result) {
      return MakeRequestRejectStatus(sequence_num.serial_no);
    }
    SCHECK(!ownership, TimedOut, "Failed to restore owning on session");
    return Status::OK();
  }

  void DropExpiredPlaceholders() {
    VLOG_WITH_PREFIX_AND_FUNC(5)
        << "min_placeholder_expiration_=" << ToString(min_placeholder_expiration_);
    const auto now = CoarseMonoClock::Now();
    if (min_placeholder_expiration_ && *min_placeholder_expiration_ > now) {
      return;
    }
    min_placeholder_expiration_.reset();
    for (auto it = pending_.begin(); it != pending_.end(); ) {
      if (const auto expiration = it->second.PlaceholderExpiration(); expiration) {
        if (*expiration <= now) {
          it = DropCompleted(it);
          continue;
        }
        min_placeholder_expiration_ = *expiration;
        break;
      }
      ++it;
    }
  }

  template<class Member, class... Args>
  std::invoke_result_t<Member, Impl&, SequenceNum, Args...> HandleNewRequest(
      Member member, SequenceNum sequence_num, Args&&... args) {
    if (!is_active_) {
      return kShutdownStatus;
    }
    auto state_updater = ScopeExit([this] { UpdatePendingRequestsState(); });
    DropExpiredPlaceholders();
    const auto& [serial_no, predecessor] = sequence_num;
    SCHECK(!IsAlreadyProcessed(serial_no), Expired, "Too old request $0", serial_no);
    const auto* item = FindOrNull(pending_, serial_no);
    SCHECK(
        !item || item->PlaceholderExpiration(),
        InvalidArgument, "Request $0 is already registered", serial_no);
    if (predecessor) {
      SCHECK(
          *predecessor < serial_no,
          InvalidArgument,
          "Bad request serial_no $0, predecessor $1", serial_no, *predecessor);
      item = FindOrNull(pending_, *predecessor);
      SCHECK(
          !item || !item->HasDependentRequest(),
          InvalidArgument, "Predecessor $0 is already in use", *predecessor);
    }
    return (*this.*member)(sequence_num, std::forward<Args>(args)...);
  }

  const PgRequestSequencer::LogPrefix log_prefix_;
  const CoarseDuration wait_duration_;
  std::condition_variable sync_request_cond_;
  bool is_active_{true};
  std::optional<SerialNo> last_seen_;
  PendingRequests pending_;
  bool pending_requests_state_changed_{false};
  std::atomic<PendingRequestsState> pending_requests_state_{PendingRequestsState::kEmpty};
  std::optional<CoarseTimePoint> min_placeholder_expiration_;
};

PgRequestSequencer::PgRequestSequencer(CoarseDuration wait_duration, LogPrefix log_prefix)
    : impl_(new Impl(wait_duration, log_prefix)) {}

PgRequestSequencer::~PgRequestSequencer() = default;

void PgRequestSequencer::ProcessPending(PgClientSession& session) {
  impl_->ProcessPending(session);
}

bool PgRequestSequencer::DoIsProcessingRequired(bool is_regular_check) const {
  return impl_->DoIsProcessingRequired(is_regular_check);
}

Result<bool> PgRequestSequencer::TryRegisterForProcessing(SequenceNum req) {
  return impl_->TryRegisterForProcessing(req);
}

Status PgRequestSequencer::RegisterForProcessing(
    SequenceNum req, PgSessionGuard& guard, CoarseTimePoint deadline) {
  return impl_->RegisterForProcessing(req, guard, deadline);
}

Status PgRequestSequencer::Enqueue(SequenceNum req, Executor&& executor, CoarseTimePoint deadline) {
  return impl_->Enqueue(req, std::move(executor), deadline);
}

void PgRequestSequencer::Shutdown() {
  impl_->Shutdown();
}

} // namespace yb::tserver
