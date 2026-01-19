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

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/lock_util.h"
#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/util/metrics.h"
#include "yb/util/status_callback.h"

namespace yb {

class ThreadPool;

namespace docdb {

struct LockData {
  DetermineKeysToLockResult<ObjectLockManager> key_to_lock;
  CoarseTimePoint deadline;
  ObjectLockOwner object_lock_owner;
  TabletId status_tablet;
  MonoTime start_time;
  StdStatusCallback callback;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(key_to_lock, deadline, object_lock_owner, status_tablet, start_time);
  }
};

// Helper struct used for keying table/object locks of a transaction.
struct TrackedLockEntryKey {
  TrackedLockEntryKey(const ObjectLockOwner& object_lock_owner_, ObjectLockPrefix object_id_)
      : object_lock_owner(object_lock_owner_), object_id(object_id_) {}

  const ObjectLockOwner object_lock_owner;
  const ObjectLockPrefix object_id;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(object_lock_owner, object_id);
  }

  bool operator==(const TrackedLockEntryKey& other) const = default;
};

class ObjectLockManagerImpl;

// This class manages locks on keys of type ObjectLockPrefix. On each key, the possibilities arise
// from a combination of kWeak/kStrong Read/Write intent types.
//
// Currently, this class is only being used for object/table level locks codepath. Each tablet
// server maintains an instance of the ObjectLockManager.
class ObjectLockManager {
 public:
  ObjectLockManager(
      ThreadPool* thread_pool, server::RpcServerBase& server, const MetricEntityPtr& metric_entity,
      ObjectLockSharedStateManager* shared_manager = nullptr);
  ~ObjectLockManager();

  // Attempt to lock a batch of keys and track the lock against data.object_lock_owner key. The
  // callback is executed with failure if the locks aren't able to be acquired within the deadline.
  void Lock(LockData&& data);

  // Release all locks held against the given object_lock_owner.
  TxnBlockedTableLockRequests Unlock(const ObjectLockOwner& object_lock_owner);

  void Poll();

  void Start(docdb::LocalWaitingTxnRegistry* waiting_txn_registry);

  void Shutdown();

  void DumpStatusHtml(std::ostream& out);

  void ConsumePendingSharedLockRequests();

  size_t TEST_GrantedLocksSize();
  size_t TEST_WaitingLocksSize();
  std::unordered_map<ObjectLockPrefix, LockState>
      TEST_GetLockStateMapForTxn(const TransactionId& txn) const;

 private:
  std::unique_ptr<ObjectLockManagerImpl> impl_;
};

}  // namespace docdb
}  // namespace yb
