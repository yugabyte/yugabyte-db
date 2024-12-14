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

#include "yb/yql/pggate/insert_on_conflict_buffer.h"

#include <utility>

namespace yb::pggate {

InsertOnConflictBuffer::InsertOnConflictBuffer()
    : keys_iter_(keys_.end()) {}

Status InsertOnConflictBuffer::AddIndexKey(const LightweightTableYbctid& key,
                                           const YBCPgInsertOnConflictKeyInfo& key_info) {
    auto index_key = TableYbctid(key);

    SCHECK_FORMAT(
        keys_.find(index_key) == keys_.end(), AlreadyPresent, "Key $0 already exists", index_key);
    keys_.insert(std::make_pair(index_key, key_info));

    return Status::OK();
}

Result<YBCPgInsertOnConflictKeyInfo> InsertOnConflictBuffer::DeleteIndexKey(
    const LightweightTableYbctid& key) {
  auto index_key = TableYbctid(key);
  if (IndexKeyExists(index_key) != KEY_READ)
    return STATUS_FORMAT(NotFound, "Key $0 not found", index_key.ybctid);

  auto it = keys_.find(index_key);
  SCHECK(keys_iter_ == keys_.end(), IllegalState, "Map deletion already in progress");
  return DoDeleteIndexKey(it);
}

Result<YBCPgInsertOnConflictKeyInfo> InsertOnConflictBuffer::DeleteNextIndexKey() {
  SCHECK(
      !keys_.empty(), IllegalState,
      "Expected to have non-zero keys in the insert on conflict buffer, found none");

  if (keys_iter_ == keys_.end()) {
    keys_iter_ = keys_.begin();
  }

  return DoDeleteIndexKey(keys_iter_);
}

YBCPgInsertOnConflictKeyInfo InsertOnConflictBuffer::DoDeleteIndexKey(
    InsertOnConflictMap::iterator& iter) {
  const auto& res = iter->second;
  YBCPgInsertOnConflictKeyInfo key_info = res;
  keys_.erase(iter++);
  return key_info;
}

int64_t InsertOnConflictBuffer::GetNumIndexKeys() const {
  return keys_.size();
}

YBCPgInsertOnConflictKeyState InsertOnConflictBuffer::IndexKeyExists(
    const TableYbctid& index_key) const {
  if (IntentKeys().find(index_key) != IntentKeys().end()) {
    return YBCPgInsertOnConflictKeyState::KEY_JUST_INSERTED;
  }

  if (keys_.find(index_key) != keys_.end()) {
    return YBCPgInsertOnConflictKeyState::KEY_READ;
  }

  return YBCPgInsertOnConflictKeyState::KEY_NOT_FOUND;
}

YBCPgInsertOnConflictKeyState InsertOnConflictBuffer::IndexKeyExists(
    const LightweightTableYbctid& key) const {
  return IndexKeyExists(TableYbctid(key));
}

void InsertOnConflictBuffer::AddIndexKeyIntent(const LightweightTableYbctid& key) {
  IntentKeys().emplace(TableYbctid(key));
}

void InsertOnConflictBuffer::ClearIntents() {
  IntentKeys().clear();
}

void InsertOnConflictBuffer::Clear(bool clear_intents) {
  keys_.clear();
  keys_iter_ = keys_.end();
  if (clear_intents) {
    IntentKeys().clear();
  }
}

bool InsertOnConflictBuffer::IsEmpty() const {
  return keys_.empty() && IntentKeys().empty();
}

TableYbctidSet& InsertOnConflictBuffer::IntentKeys() {
  static TableYbctidSet intent_keys_;
  return intent_keys_;
}

}  // namespace yb::pggate
