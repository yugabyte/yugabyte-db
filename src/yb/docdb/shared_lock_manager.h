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

#include "yb/util/monotime.h"

namespace yb::docdb {

// This class manages locks on keys of type RefCntPrefix. On each key, the possibilities arise
// from a combination of kWeak/kStrong Read/Write intent types.
//
// Every tablet maintains its own SharedLockManager and uses it to acquire required in-memory locks
// for the scope of the read/write request being served.
class SharedLockManager {
 public:
  SharedLockManager();
  ~SharedLockManager();

  // Attempt to lock a batch of keys. The call may be blocked waiting for other locks to be
  // released. If the entries don't exist, they are created. The lock batch gets associated with
  // this lock manager, which makes it auto-unlock on destruction.
  //
  // Returns false if was not able to acquire lock until deadline.
  MUST_USE_RESULT bool Lock(
      LockBatchEntries<SharedLockManager>& key_to_intent_type, CoarseTimePoint deadline);

  // Release the batch of locks. Requires that the locks are held.
  void Unlock(const LockBatchEntries<SharedLockManager>& key_to_intent_type);

  void DumpStatusHtml(std::ostream& out);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::docdb
