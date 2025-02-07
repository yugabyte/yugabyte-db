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

#pragma once

#include "yb/common/transaction.h"
#include "yb/dockv/dockv_fwd.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/compare_util.h"
#include "yb/util/hash_util.h"

namespace yb::docdb {

struct ObjectLockOwner {
  TransactionId txn_id;
  SubTransactionId subtxn_id;

  ObjectLockOwner(
      const TransactionId& txn_id_, SubTransactionId subtxn_id_)
      : txn_id(txn_id_), subtxn_id(subtxn_id_) {}

  template<class T>
  void PopulateLockRequest(T* req) const {
    req->set_txn_id(txn_id.data(), txn_id.size());
    req->set_subtxn_id(subtxn_id);
  }

  YB_STRUCT_DEFINE_HASH(ObjectLockOwner, txn_id, subtxn_id);

  auto operator<=>(const ObjectLockOwner&) const = default;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(txn_id, subtxn_id);
  }
};

// ObjectLockPrefix is the entity for which the ts_local_lock_manager acquires locks. In context of
// object/table locks, when a session requests lock(s) on an object oid corresponding to a database
// oid, an 'ObjectLockPrefix' in formed which is then passed to the ObjectLockManager.
struct ObjectLockPrefix {
  ObjectLockPrefix(
      uint64_t database_oid_, uint64_t object_oid_, dockv::KeyEntryType lock_type_)
      : database_oid(database_oid_), object_oid(object_oid_), lock_type(lock_type_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(database_oid, object_oid, lock_type);
  }

  YB_STRUCT_DEFINE_HASH(ObjectLockPrefix, database_oid, object_oid, lock_type);

  auto operator<=>(const ObjectLockPrefix&) const = default;

  uint64_t database_oid;
  uint64_t object_oid;
  dockv::KeyEntryType lock_type;
};

} // namespace yb::docdb
