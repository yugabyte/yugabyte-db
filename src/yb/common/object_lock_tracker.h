//
// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include <memory>
#include <shared_mutex>

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"

#include "yb/util/hash_util.h"

namespace yb {

struct ObjectLockContext {
  ObjectLockContext(
      TransactionId txn_id, SubTransactionId subtxn_id, uint64_t database_oid,
      uint64_t relation_oid, uint64_t object_oid, uint64_t object_sub_oid, TableLockType lock_type)
      : txn_id(txn_id),
        subtxn_id(subtxn_id),
        database_oid(database_oid),
        relation_oid(relation_oid),
        object_oid(object_oid),
        object_sub_oid(object_sub_oid),
        lock_type(lock_type) {}

  YB_STRUCT_DEFINE_HASH(
      ObjectLockContext, txn_id, subtxn_id, database_oid, relation_oid, object_oid, object_sub_oid,
      lock_type);

  auto operator<=>(const ObjectLockContext&) const = default;

  TransactionId txn_id;
  SubTransactionId subtxn_id;
  uint64_t database_oid;
  uint64_t relation_oid;
  uint64_t object_oid;
  uint64_t object_sub_oid;
  TableLockType lock_type;
};

// This class tracks object locks specifically for the pg_locks view.
// An alternative would be to extract and decrypt locks directly from ObjectLockManager,
// but for simplicity, we decided to maintain a separate tracker here.
class ObjectLockTracker {
 public:

  void TrackLocks(
    const std::vector<ObjectLockContext>& lock_contexts, ObjectLockState lock_state,
    HybridTime wait_start = HybridTime::kInvalid);

  void TrackLock(
      const ObjectLockContext& lock_context, ObjectLockState lock_state,
      HybridTime wait_start = HybridTime::kInvalid);

  void UntrackLocks(const std::vector<ObjectLockContext>& lock_contexts);

  void UntrackLock(const ObjectLockContext& lock_context);

  void UntrackAllLocks(TransactionId txn_id, SubTransactionId subtxn_id);

  void PopulateObjectLocks(
      google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) const;

 private:
  mutable std::shared_mutex mutex_;

  struct LockId {
    YB_STRUCT_DEFINE_HASH(
        LockId, database_oid, relation_oid, object_oid, object_sub_oid, lock_type);
    auto operator<=>(const LockId&) const = default;
    uint64_t database_oid;
    uint64_t relation_oid;
    uint64_t object_oid;
    uint64_t object_sub_oid;
    TableLockType lock_type;
  };

  struct LockInfo {
    ObjectLockState lock_state;
    HybridTime wait_start = HybridTime::kInvalid;
    size_t counter = 0;
  };

  std::unordered_map<TransactionId,
      std::unordered_map<SubTransactionId,
          std::unordered_map<LockId, LockInfo>>> lock_map_;
};

}  // namespace yb
