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

#include "yb/common/object_lock_tracker.h"

#include "yb/util/shared_lock.h"

namespace yb {

void ObjectLockTracker::TrackLocks(
    const std::vector<ObjectLockContext>& lock_contexts, ObjectLockState lock_state,
    HybridTime wait_start) {
  for (const auto& lock_context : lock_contexts) {
    TrackLock(lock_context, lock_state, wait_start);
  }
}

void ObjectLockTracker::TrackLock(
    const ObjectLockContext& lock_context, ObjectLockState lock_state, HybridTime wait_start) {
  const auto txn_id = lock_context.txn_id;
  const auto subtxn_id = lock_context.subtxn_id;
  const auto lock_id = LockId{
    .database_oid = lock_context.database_oid,
    .relation_oid = lock_context.relation_oid,
    .object_oid = lock_context.object_oid,
    .object_sub_oid = lock_context.object_sub_oid,
    .lock_type = lock_context.lock_type
  };

  std::lock_guard l(mutex_);
  auto& lock_id_map = lock_map_[txn_id][subtxn_id];

  auto lock_it = lock_id_map.find(lock_id);
  if (lock_it != lock_id_map.end()) {
    auto& existing = lock_it->second;
    existing.counter++;
    if (existing.lock_state == ObjectLockState::WAITING &&
        lock_state == ObjectLockState::GRANTED) {
      // Upgrade from WAITING to GRANTED state and
      // keep the wait_start time as it is for pg_locks view.
      existing.lock_state = ObjectLockState::GRANTED;
    }
    return;
  }

  LockInfo new_lock{
    .lock_state = lock_state,
    .wait_start = (lock_state == ObjectLockState::WAITING) ? wait_start : HybridTime::kInvalid,
    .counter = 1
  };
  lock_id_map.emplace(lock_id, new_lock);
}

void ObjectLockTracker::UntrackLocks(const std::vector<ObjectLockContext>& lock_contexts) {
  for (const auto& lock_context : lock_contexts) {
    UntrackLock(lock_context);
  }
}

void ObjectLockTracker::UntrackLock(const ObjectLockContext& lock_context) {
  if (lock_context.subtxn_id == 0) {
    UntrackAllLocks(lock_context.txn_id, lock_context.subtxn_id);
    return;
  }

  std::lock_guard l(mutex_);
  auto txn_it = lock_map_.find(lock_context.txn_id);
  if (txn_it == lock_map_.end()) {
    return;
  }
  auto subtxn_it = txn_it->second.find(lock_context.subtxn_id);
  if (subtxn_it == txn_it->second.end()) {
    return;
  }

  auto& lock_id_map = subtxn_it->second;
  const auto lock_id = LockId{
    .database_oid = lock_context.database_oid,
    .relation_oid = lock_context.relation_oid,
    .object_oid = lock_context.object_oid,
    .object_sub_oid = lock_context.object_sub_oid,
    .lock_type = lock_context.lock_type
  };
  auto lock_it = lock_id_map.find(lock_id);
  DCHECK(lock_it != lock_id_map.end());
  if (lock_it->second.counter > 1) {
    lock_it->second.counter--;
    return;
  }

  lock_id_map.erase(lock_it);
  // // clean up empty txn map
  if (lock_id_map.empty()) {
    txn_it->second.erase(subtxn_it);
    if (txn_it->second.empty()) {
      lock_map_.erase(txn_it);
    }
  }
}

void ObjectLockTracker::UntrackAllLocks(TransactionId txn_id, SubTransactionId subtxn_id) {
  std::lock_guard l(mutex_);
  if (subtxn_id == 0) {
    // Remove all subtransactions for this transaction.
    lock_map_.erase(txn_id);
  } else {
    auto txn_iter = lock_map_.find(txn_id);
    if (txn_iter != lock_map_.end()) {
      txn_iter->second.erase(subtxn_id);
      if (txn_iter->second.empty()) {
        lock_map_.erase(txn_iter);
      }
    }
  }
}

void ObjectLockTracker::PopulateObjectLocks(
    google::protobuf::RepeatedPtrField<ObjectLockInfoPB>* object_lock_infos) const {
  object_lock_infos->Clear();
  SharedLock l(mutex_);
  for (const auto& [txn_id, subtxn_map] : lock_map_) {
    for (const auto& [subtxn_id, lock_id_map] : subtxn_map) {
      for (const auto& [lock_id, lock_info] : lock_id_map) {
        auto* object_lock_info_pb = object_lock_infos->Add();
        object_lock_info_pb->set_transaction_id(txn_id.data(), txn_id.size());
        object_lock_info_pb->set_subtransaction_id(subtxn_id);
        object_lock_info_pb->set_database_oid(lock_id.database_oid);
        object_lock_info_pb->set_relation_oid(lock_id.relation_oid);
        object_lock_info_pb->set_object_oid(lock_id.object_oid);
        object_lock_info_pb->set_object_sub_oid(lock_id.object_sub_oid);
        object_lock_info_pb->set_mode(lock_id.lock_type);
        object_lock_info_pb->set_lock_state(lock_info.lock_state);
        object_lock_info_pb->set_wait_start_ht(lock_info.wait_start.ToUint64());
      }
    }
  }
}

} // namespace yb
