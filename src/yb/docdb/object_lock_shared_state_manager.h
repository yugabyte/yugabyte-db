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

#include <stddef.h>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <span>
#include <string>
#include <utility>
#include <functional>

#include "yb/common/transaction.h"
#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"
#include "yb/docdb/object_lock_shared_fwd.h"
#include "yb/docdb/object_lock_shared_state.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/util/lw_function.h"
#include "yb/util/tostring.h"
#include "yb/common/entity_ids_types.h"

namespace yb {
class ObjectLockTracker;

namespace docdb {
class ObjectLockManager;
}  // namespace docdb
}  // namespace yb

namespace yb::docdb {

struct ObjectSharedLockRequest {
  ObjectLockOwner owner;
  TabletId status_tablet;
  LockBatchEntry<ObjectLockManager> entry;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(owner, entry);
  }
};

using LockRequestConsumer = LWFunction<void(ObjectSharedLockRequest)>;

class ObjectLockOwnerRegistry {
  class Impl;

 public:
  class [[nodiscard]] RegistrationGuard {
   public:
    RegistrationGuard(Impl& registry, SessionLockOwnerTag tag) : registry_(registry), tag_(tag) {}
    ~RegistrationGuard();

    [[nodiscard]] SessionLockOwnerTag tag() const { return tag_; }

   private:
    DISALLOW_COPY_AND_ASSIGN(RegistrationGuard);

    Impl& registry_;
    const SessionLockOwnerTag tag_;
  };

  struct OwnerInfo {
    OwnerInfo(TransactionId txn_id_, const TabletId& status_tablet_)
        : txn_id(txn_id_), status_tablet(status_tablet_) {}

    TransactionId txn_id;
    TabletId status_tablet;
  };

  ObjectLockOwnerRegistry();
  ~ObjectLockOwnerRegistry();

  RegistrationGuard Register(const TransactionId& id, const TabletId& tablet_id);

  [[nodiscard]] std::shared_ptr<OwnerInfo> GetOwnerInfo(SessionLockOwnerTag tag) const;

 private:
  std::unique_ptr<Impl> impl_;
};

class ObjectLockSharedStateManager {
 public:
  explicit ObjectLockSharedStateManager(std::shared_ptr<ObjectLockTracker> object_lock_tracker)
      : object_lock_tracker_(std::move(object_lock_tracker)) {}

  void SetupShared(ObjectLockSharedState& shared);

  void PauseAndResetSharedLockState();
  void ResumeSharedLockState();

  [[nodiscard]] ObjectLockOwnerRegistry& registry() { return registry_; }

  size_t ConsumePendingSharedLockRequests(const LockRequestConsumer& consume);

  size_t ConsumeAndAcquireExclusiveLockIntents(
      const LockRequestConsumer& consume,
      std::span<const LockBatchEntry<ObjectLockManager>*> lock_entries);

  void ReleaseExclusiveLockIntent(const ObjectLockPrefix& object_id, LockState lock_state);

  [[nodiscard]] TransactionId TEST_last_owner() const;

  [[nodiscard]] bool TEST_has_exclusive_intents() const;

 private:
  template<typename ConsumeMethod>
  size_t CallWithRequestConsumer(
      ObjectLockSharedState* shared, ConsumeMethod&& m, const LockRequestConsumer& consume);

  std::atomic<ObjectLockSharedState*> shared_{nullptr};
  ObjectLockOwnerRegistry registry_;

  const std::shared_ptr<ObjectLockTracker> object_lock_tracker_;

  std::mutex setup_mutex_;
  ObjectLockSharedState::ActivationGuard shared_activate_ GUARDED_BY(setup_mutex_);
  // We can accumulate exclusive lock intents before shared memory is set up via lock manager
  // bootstrap. If these are not released before shared memory is set up, they must be transferred
  // to shared memory before PG has a chance to use the fastpath. We track them here until setup
  // time.
  std::unordered_map<ObjectLockPrefix, LockState> pre_setup_locks_ GUARDED_BY(setup_mutex_);
};

} // namespace yb::docdb
