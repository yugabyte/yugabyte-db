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

#include <boost/functional/hash.hpp>

#include "yb/common/transaction.h"
#include "yb/dockv/dockv_fwd.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/compare_util.h"

namespace yb::docdb {

struct VersionedTransaction {
  TransactionId txn_id;
  TxnReuseVersion txn_version;

  VersionedTransaction(
      const TransactionId& txn_id_, TxnReuseVersion txn_version_) : txn_id(txn_id_),
      txn_version(txn_version_) {}

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(txn_id, txn_version);
  }
};

inline bool operator==(const VersionedTransaction& lhs, const VersionedTransaction& rhs) {
  return YB_STRUCT_EQUALS(txn_id, txn_version);
}

inline bool operator<(const VersionedTransaction& lhs, const VersionedTransaction& rhs) {
  if (lhs.txn_id != rhs.txn_id) {
    return lhs.txn_id < rhs.txn_id;
  }
  return lhs.txn_version < rhs.txn_version;
}

inline size_t hash_value(const VersionedTransaction object) noexcept {
  size_t seed = 0;
  boost::hash_combine(seed, object.txn_id);
  boost::hash_combine(seed, object.txn_version);
  return seed;
}

struct ObjectLockOwner {
  VersionedTransaction versioned_txn;
  SubTransactionId subtxn_id;

  ObjectLockOwner(
      const TransactionId& txn_id_, TxnReuseVersion txn_version_, SubTransactionId subtxn_id_)
      : versioned_txn(txn_id_, txn_version_), subtxn_id(subtxn_id_) {}
  ObjectLockOwner(VersionedTransaction&& versioned_txn_, SubTransactionId subtxn_id_)
      : versioned_txn(std::move(versioned_txn_)), subtxn_id(subtxn_id_) {}

  template<class T>
  void PopulateLockRequest(T* req) const {
    req->set_txn_id(versioned_txn.txn_id.data(), versioned_txn.txn_id.size());
    req->set_txn_reuse_version(versioned_txn.txn_version);
    req->set_subtxn_id(subtxn_id);
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(versioned_txn, subtxn_id);
  }
};

inline bool operator==(const ObjectLockOwner& lhs, const ObjectLockOwner& rhs) {
  return YB_STRUCT_EQUALS(versioned_txn, subtxn_id);
}

inline bool operator<(const ObjectLockOwner& lhs, const ObjectLockOwner& rhs) {
  if (lhs.versioned_txn != rhs.versioned_txn) {
    return lhs.versioned_txn < rhs.versioned_txn;
  }
  return lhs.subtxn_id < rhs.subtxn_id;
}

inline size_t hash_value(const ObjectLockOwner object) noexcept {
  size_t seed = 0;
  boost::hash_combine(seed, object.versioned_txn);
  boost::hash_combine(seed, object.subtxn_id);
  return seed;
}

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

  uint64_t database_oid;
  uint64_t object_oid;
  dockv::KeyEntryType lock_type;
};

inline bool operator==(const ObjectLockPrefix& lhs, const ObjectLockPrefix& rhs) {
  return YB_STRUCT_EQUALS(database_oid, object_oid, lock_type);
}

inline bool operator<(const ObjectLockPrefix& lhs, const ObjectLockPrefix& rhs) {
  if (lhs.database_oid != rhs.database_oid) {
    return lhs.database_oid < rhs.database_oid;
  }
  if (lhs.object_oid != rhs.object_oid) {
    return lhs.object_oid < rhs.object_oid;
  }
  return lhs.lock_type < rhs.lock_type;
}

inline size_t hash_value(const ObjectLockPrefix object) noexcept {
  size_t seed = 0;
  boost::hash_combine(seed, object.database_oid);
  boost::hash_combine(seed, object.object_oid);
  boost::hash_combine(seed, object.lock_type);
  return seed;
}

} // namespace yb::docdb
