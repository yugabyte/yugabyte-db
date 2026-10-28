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

#pragma once

#include <atomic>
#include <memory>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/rpc/scheduler.h"

#include "yb/util/locks.h"
#include "yb/util/shared_lock.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::tserver {

// A client session trackable by SessionRegistry.
class ClientSession {
 public:
  virtual ~ClientSession() = default;

  virtual uint64_t id() const = 0;
  virtual CoarseTimePoint expiration() const = 0;
  // Postpones the session expiration.
  virtual void Touch() = 0;
  virtual void SetExpiration(CoarseTimePoint value) = 0;

  virtual void StartShutdown(bool service_shutting_down) = 0;
  virtual bool ReadyToShutdown() const = 0;
  virtual void CompleteShutdown() = 0;
};

using ClientSessionPtr = std::shared_ptr<ClientSession>;

// Implements the expiration related part of ClientSession; the lifetime is taken from the
// pg_client_session_expiration_ms flag.
class ClientSessionBase : public ClientSession {
 public:
  explicit ClientSessionBase(uint64_t id);

  uint64_t id() const override {
    return id_;
  }

  CoarseTimePoint expiration() const override {
    return expiration_.load(std::memory_order_acquire);
  }

  void Touch() override;
  void SetExpiration(CoarseTimePoint value) override;

 private:
  CoarseTimePoint NewExpiration() const;

  const uint64_t id_;
  const CoarseDuration lifetime_;
  std::atomic<CoarseTimePoint> expiration_;
};

// Environment of a SessionRegistry, customized by its owner.
class SessionRegistryContext {
 public:
  // Produces the error status for an unknown session id.
  virtual Status UnknownSessionStatus(uint64_t session_id);

  // Invoked for an expired session right after its shutdown was started.
  virtual void SessionShutdownStarted(const ClientSessionPtr& session) {}

  // Invoked with the ids of every non-empty batch of expired sessions.
  virtual void SessionsRemoved(std::vector<uint64_t>&& session_ids) {}

 protected:
  // The registry does not own the context.
  ~SessionRegistryContext() = default;
};

// Tracks client sessions by id, allocates their ids and expires sessions that have not been
// touched within the session expiration interval. Shared by PgClientService and
// ThinClientService.
// Whether looking a session up postpones its expiration. Request paths want kTrue; observability
// paths do not, so that polling cannot keep an otherwise idle session alive.
YB_STRONGLY_TYPED_BOOL(TouchSession);

class SessionRegistryBase {
 public:
  SessionRegistryBase(rpc::Scheduler* scheduler, SessionRegistryContext* context);
  ~SessionRegistryBase();

  uint64_t NewSessionId();

  Status Insert(const ClientSessionPtr& session);

  // Requests expiry of the given session at the next expiration check.
  void Expire(uint64_t session_id);

  size_t Count();

  // Drains all sessions; returns false when shutdown had already been started.
  bool Shutdown();

 protected:
  Result<ClientSessionPtr> DoGet(uint64_t session_id, TouchSession touch);

  // Fills a vector of the live sessions, casting each to the registry's session type, so the
  // typed facade below needs no second pass.
  template <class SessionPtr>
  std::vector<SessionPtr> DoSnapshot() {
    SharedLock lock(mutex_);
    std::vector<SessionPtr> result;
    result.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
      result.push_back(
          std::static_pointer_cast<typename SessionPtr::element_type>(session));
    }
    return result;
  }

 private:
  void ScheduleCheckExpiredSessions(CoarseTimePoint now) REQUIRES(mutex_);
  void CheckExpiredSessions();
  void CleanupSessions(std::vector<ClientSessionPtr>&& expired_sessions, CoarseTimePoint time);

  SessionRegistryContext& context_;

  std::atomic<uint64_t> session_serial_no_{0};
  rpc::ScheduledTaskTracker check_expired_sessions_;

  using ExpirationEntry = std::pair<CoarseTimePoint, uint64_t>;

  struct CompareExpiration {
    bool operator()(const ExpirationEntry& lhs, const ExpirationEntry& rhs) const {
      // Order is reversed, because std::priority_queue keeps track of the largest element.
      // This comparator is important for the cleanup logic.
      return rhs.first < lhs.first;
    }
  };

  rw_spinlock mutex_;
  std::priority_queue<ExpirationEntry,
                      std::vector<ExpirationEntry>,
                      CompareExpiration> session_expiration_queue_ GUARDED_BY(mutex_);
  std::unordered_map<uint64_t, ClientSessionPtr> sessions_ GUARDED_BY(mutex_);
  CoarseTimePoint check_expired_sessions_time_ GUARDED_BY(mutex_);
  std::vector<ClientSessionPtr> stopping_sessions_ GUARDED_BY(mutex_);
  bool shutting_down_ GUARDED_BY(mutex_) = false;
};

// Typed facade over SessionRegistryBase: Session must derive from ClientSession.
template <class Session>
class SessionRegistry : public SessionRegistryBase {
 public:
  using SessionPtr = std::shared_ptr<Session>;

  using SessionRegistryBase::SessionRegistryBase;

  Result<SessionPtr> Get(uint64_t session_id, TouchSession touch = TouchSession::kTrue) {
    return std::static_pointer_cast<Session>(VERIFY_RESULT(DoGet(session_id, touch)));
  }

  std::vector<SessionPtr> Snapshot() { return DoSnapshot<SessionPtr>(); }
};

}  // namespace yb::tserver
