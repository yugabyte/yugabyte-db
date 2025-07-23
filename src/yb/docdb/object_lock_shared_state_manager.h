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

#include <memory>

#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_data.h"
#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/gutil/macros.h"

#include "yb/util/lw_function.h"
#include "yb/util/tostring.h"

namespace yb::docdb {

struct ObjectSharedLockRequest {
  ObjectLockOwner owner;
  LockBatchEntry<ObjectLockManager> entry;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(owner, entry);
  }
};

using LockRequestConsumer = LWFunction<void(ObjectSharedLockRequest)>;

class ObjectLockOwnerRegistry {
  class Impl;

 public:
  class [[nodiscard]] RegistrationGuard { // NOLINT(whitespace/braces)
   public:
    RegistrationGuard(Impl& registry, SessionLockOwnerTag tag) : registry_(registry), tag_(tag) {}
    ~RegistrationGuard();

    [[nodiscard]] SessionLockOwnerTag tag() const { return tag_; }

   private:
    DISALLOW_COPY_AND_ASSIGN(RegistrationGuard);

    Impl& registry_;
    const SessionLockOwnerTag tag_;
  };

  ObjectLockOwnerRegistry();
  ~ObjectLockOwnerRegistry();

  RegistrationGuard Register(const TransactionId& id);

  [[nodiscard]] TransactionId GetTransactionId(SessionLockOwnerTag tag) const;

 private:
  std::unique_ptr<Impl> impl_;
};

class ObjectLockSharedStateManager {
 public:
  void SetupShared(ObjectLockSharedState& shared);

  [[nodiscard]] ObjectLockOwnerRegistry& registry() { return registry_; }

  void ConsumePendingSharedLockRequests(const LockRequestConsumer& consume);

  [[nodiscard]] TransactionId TEST_last_owner() const;

 private:
  ObjectLockSharedState* shared_ = nullptr;
  ObjectLockOwnerRegistry registry_;
};

} // namespace yb::docdb
