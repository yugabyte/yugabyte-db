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

#include "yb/docdb/docdb-internal.h"

#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/shared_lock_manager_fwd.h"
#include "yb/dockv/value_type.h"

namespace yb {
namespace docdb {

KeyType GetKeyType(const Slice& slice, StorageDbType db_type) {
  if (slice.empty()) {
    return KeyType::kEmpty;
  }

  if (db_type == StorageDbType::kRegular) {
    return KeyType::kPlainSubDocKey;
  }

  if (slice[0] == dockv::KeyEntryTypeAsChar::kTransactionId) {
    if (slice.size() == TransactionId::StaticSize() + 1) {
      return KeyType::kTransactionMetadata;
    } else if (slice.size() == TransactionId::StaticSize() + 2) {
      // Key for post-apply transaction metadata is [prefix] [transaction id] 00.
      return KeyType::kPostApplyTransactionMetadata;
    } else {
      return KeyType::kReverseTxnKey;
    }
  }
  if (slice[0] == dockv::KeyEntryTypeAsChar::kExternalTransactionId) {
    return KeyType::kExternalIntents;
  }

  return KeyType::kIntentKey;
}

} // namespace docdb
} // namespace yb
