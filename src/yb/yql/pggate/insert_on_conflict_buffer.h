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

#include "yb/util/status.h"

#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

class InsertOnConflictBuffer {
 public:
  InsertOnConflictBuffer();

  // Functions to manipulate the key cache
  Status AddIndexKey(
      const LightweightTableYbctid& key, const YbcPgInsertOnConflictKeyInfo& key_info);
  Result<YbcPgInsertOnConflictKeyInfo> DeleteIndexKey(const LightweightTableYbctid& key);
  Result<YbcPgInsertOnConflictKeyInfo> DeleteNextIndexKey();
  YbcPgInsertOnConflictKeyState IndexKeyExists(const LightweightTableYbctid& key) const;
  int64_t GetNumIndexKeys() const;

  // Functions to manipulate the intents cache
  void AddIndexKeyIntent(const LightweightTableYbctid& key);
  void ClearIntents();

  // Functions to manipulate both the key cache and the intents cache
  void Clear(bool clear_intents);
  bool IsEmpty() const;

 private:
  using InsertOnConflictMap = TableYbctidMap<YbcPgInsertOnConflictKeyInfo>;

  YbcPgInsertOnConflictKeyState IndexKeyExists(const TableYbctid& ybctid) const;
  YbcPgInsertOnConflictKeyInfo DoDeleteIndexKey(InsertOnConflictMap::iterator& iter);

  InsertOnConflictMap keys_; // a map holding tuple IDs and their corresponding index scan slots

  // An iterator to the map holding the index scan slots. Used to drop the slots one by one.
  InsertOnConflictMap::iterator keys_iter_;

  // Returns a set of tuple IDs representing modified tuples
  static TableYbctidSet& IntentKeys();
};

}  // namespace yb::pggate
