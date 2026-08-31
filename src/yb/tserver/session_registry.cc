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

#include "yb/tserver/session_registry.h"

#include <algorithm>

#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_uint64(TEST_delay_before_complete_expired_pg_sessions_shutdown_ms);

namespace yb::tserver {

ClientSessionBase::ClientSessionBase(uint64_t id)
    : id_(id), lifetime_(FLAGS_pg_client_session_expiration_ms * 1ms),
      expiration_(NewExpiration()) {}

void ClientSessionBase::Touch() {
  auto new_expiration = NewExpiration();
  auto old_expiration = expiration_.load(std::memory_order_acquire);
  while (new_expiration > old_expiration) {
    if (expiration_.compare_exchange_weak(
        old_expiration, new_expiration, std::memory_order_acq_rel)) {
      break;
    }
  }
}

void ClientSessionBase::SetExpiration(CoarseTimePoint value) {
  expiration_.store(value, std::memory_order_release);
}

CoarseTimePoint ClientSessionBase::NewExpiration() const {
  return CoarseMonoClock::now() + lifetime_;
}

Status SessionRegistryContext::UnknownSessionStatus(uint64_t session_id) {
  return STATUS_FORMAT(InvalidArgument, "Unknown session $0", session_id);
}

SessionRegistryBase::SessionRegistryBase(
    rpc::Scheduler* scheduler, SessionRegistryContext* context)
    : context_(*context),
      check_expired_sessions_("check_expired_sessions", scheduler) {
  std::lock_guard lock(mutex_);
  ScheduleCheckExpiredSessions(CoarseMonoClock::now());
}

SessionRegistryBase::~SessionRegistryBase() {
  Shutdown();
}

uint64_t SessionRegistryBase::NewSessionId() {
  return ++session_serial_no_;
}

Status SessionRegistryBase::Insert(const ClientSessionPtr& session) {
  const auto session_id = session->id();
  std::lock_guard lock(mutex_);
  if (shutting_down_) {
    return STATUS(ShutdownInProgress, "Shutting down");
  }
  sessions_.emplace(session_id, session);
  session_expiration_queue_.emplace(session->expiration(), session_id);
  return Status::OK();
}

Result<ClientSessionPtr> SessionRegistryBase::DoGet(uint64_t session_id, TouchSession touch) {
  RSTATUS_DCHECK_NE(session_id, 0ULL, InvalidArgument, "Bad session id");
  SharedLock lock(mutex_);
  auto it = sessions_.find(session_id);
  if (PREDICT_FALSE(it == sessions_.end())) {
    return context_.UnknownSessionStatus(session_id);
  }
  if (touch) {
    it->second->Touch();
  }
  return it->second;
}

void SessionRegistryBase::Expire(uint64_t session_id) {
  auto now = CoarseMonoClock::now();
  std::lock_guard lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return;
  }
  VLOG(2) << "Requesting session expiry for session " << session_id;
  it->second->SetExpiration(now);
  session_expiration_queue_.emplace(now, session_id);
  ScheduleCheckExpiredSessions(now);
}

size_t SessionRegistryBase::Count() {
  SharedLock lock(mutex_);
  return sessions_.size();
}

bool SessionRegistryBase::Shutdown() {
  std::vector<ClientSessionPtr> sessions;
  {
    std::lock_guard lock(mutex_);
    if (shutting_down_) {
      return false;
    }
    shutting_down_ = true;
    sessions.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
      sessions.push_back(session);
    }
    sessions_.clear();
  }
  for (const auto& session : sessions) {
    session->StartShutdown(/* service_shutting_down= */ true);
  }
  for (const auto& session : sessions) {
    session->CompleteShutdown();
  }
  check_expired_sessions_.StartShutdown();
  check_expired_sessions_.CompleteShutdown();
  return true;
}

void SessionRegistryBase::ScheduleCheckExpiredSessions(CoarseTimePoint now) {
  if (shutting_down_) {
    return;
  }
  auto time = session_expiration_queue_.empty()
      ? CoarseTimePoint(now + FLAGS_pg_client_session_expiration_ms * 1ms)
      : session_expiration_queue_.top().first + 100ms;
  if (!stopping_sessions_.empty()) {
    time = std::min(time, now + 1s);
  }
  if (check_expired_sessions_time_ != CoarseTimePoint() && check_expired_sessions_time_ < time) {
    return;
  }
  check_expired_sessions_time_ = time;
  check_expired_sessions_.Schedule([this](const Status& status) {
    if (!status.ok()) {
      return;
    }
    CheckExpiredSessions();
  }, time - now);
}

void SessionRegistryBase::CheckExpiredSessions() {
  auto now = CoarseMonoClock::now();
  std::vector<ClientSessionPtr> expired_sessions;
  std::vector<ClientSessionPtr> ready_sessions;
  {
    std::lock_guard lock(mutex_);
    if (shutting_down_) {
      return;
    }
    check_expired_sessions_time_ = CoarseTimePoint();
    while (!session_expiration_queue_.empty()) {
      auto& top = session_expiration_queue_.top();
      if (top.first > now) {
        break;
      }
      auto id = top.second;
      session_expiration_queue_.pop();
      auto it = sessions_.find(id);
      if (it != sessions_.end()) {
        auto current_expiration = it->second->expiration();
        if (current_expiration > now) {
          session_expiration_queue_.emplace(current_expiration, id);
        } else {
          expired_sessions.push_back(std::move(it->second));
          sessions_.erase(it);
        }
      }
    }
    auto filter = [&ready_sessions](const auto& session) {
      if (session->ReadyToShutdown()) {
        ready_sessions.push_back(session);
        return true;
      }
      return false;
    };
    stopping_sessions_.erase(
        std::remove_if(stopping_sessions_.begin(), stopping_sessions_.end(), filter),
        stopping_sessions_.end());
    if (expired_sessions.empty()) {
      ScheduleCheckExpiredSessions(now);
    }
  }
  for (const auto& session : ready_sessions) {
    session->CompleteShutdown();
  }
  CleanupSessions(std::move(expired_sessions), now);
}

void SessionRegistryBase::CleanupSessions(
    std::vector<ClientSessionPtr>&& expired_sessions, CoarseTimePoint time) {
  if (expired_sessions.empty()) {
    return;
  }
  std::vector<ClientSessionPtr> not_ready_sessions;
  for (const auto& session : expired_sessions) {
    VLOG(1) << "Starting shutdown for expired session ID: " << session->id();
    session->StartShutdown(/* service_shutting_down= */ false);
    context_.SessionShutdownStarted(session);
  }
  AtomicFlagSleepMs(&FLAGS_TEST_delay_before_complete_expired_pg_sessions_shutdown_ms);
  for (const auto& session : expired_sessions) {
    if (session->ReadyToShutdown()) {
      session->CompleteShutdown();
    } else {
      not_ready_sessions.push_back(session);
    }
  }
  {
    std::lock_guard lock(mutex_);
    stopping_sessions_.insert(
        stopping_sessions_.end(), not_ready_sessions.begin(), not_ready_sessions.end());
    ScheduleCheckExpiredSessions(time);
  }
  std::vector<uint64_t> expired_session_ids;
  expired_session_ids.reserve(expired_sessions.size());
  for (const auto& session : expired_sessions) {
    expired_session_ids.push_back(session->id());
  }
  expired_sessions.clear();
  context_.SessionsRemoved(std::move(expired_session_ids));
}

}  // namespace yb::tserver
