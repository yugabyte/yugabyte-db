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

#include <boost/multi_index/hashed_index.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.pb.h"
#include "yb/docdb/shared_lock_manager.h"
#include "yb/dockv/value_type.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/status.h"

namespace yb::tablet {

// LockManager for acquiring table/object locks of type TableLockType on a given object id.
// TSLocalLockManager uses LockManagerImpl<ObjectLockPrefix> to acheive the locking/unlocking
// behavior, yet the scope of the object lock is not just limited to the scope of the lock rpc
// request. If a lock request is responded with success, the object lock(s) are stored in memory
// at the LockManagerImpl until an explicit Unlock request is issued. In case of failure to lock,
// only the locks acquired so far as part of the same rpc request are released.
//
// TSLocalLockManager is currently used for table locking feature. In brief, all DMLs acquire
// required table/object locks on the local tserver's object lock manager, and all DDLs acquire
// table/object locks on all live tservers.
//
// Note that upon a server crash/restart, all acquired object locks are lost, which is the
// desired behavior for table locks. This is because, all DMLs hosted by the query layer client
// of the corresponding tserver would be aborted, hence we want to release all object locks
// held by the DMLs. The master leader is responsible for re-acquiring locks corresponding to
// all active DDLs. The same applies on addition of a new tserver node, the master bootstraps
// it with all exisitng DDL (global) locks.
class TSLocalLockManager {
 public:
  TSLocalLockManager();
  ~TSLocalLockManager();

  // Tries acquiring object locks with the specified modes and registers them against the given
  // session id-host pair. When locking a batch of keys, if the lock mananger is unable to acquire
  // the lock on the k'th key/record, all acquired locks i.e (1 to k-1) are released and the error
  // is returned back to the client. Note that previous successful locks corresponding to the same
  // session id-host pair remain unchanged until an explicit unlock request comes in.
  //
  // Note that the lock manager ignores the session's conflict with itself. So a session can acquire
  // conflicting lock types on a key given that there aren't other sessions with active conflciting
  // locks on the key.
  //
  // Continuous influx of readers can starve writers. For instance, if there are multiple sessions
  // requesting ACCESS_SHARE on a key, a writer requesting ACCESS_EXCLUSIVE may face starvation.
  // Since we intend to use this for table locks, DDLs may face starvation if there is influx of
  // conflicting DMLs.
  // TODO: DDLs don't face starvation in PG. Address the above starvation problem.
  //
  // TODO: Augment the 'pg_locks' path to show the acquired/waiting object/table level locks.
  Status AcquireObjectLocks(
      const tserver::AcquireObjectLockRequestPB& req, CoarseTimePoint deadline);

  // The call releases all locks on the object(s) corresponding to the session id-host pair. There
  // is no 1:1 mapping that exists among lock and unlock requests. A session can acquire different
  // lock modes on a key multiple times, and will unlock them all with a single unlock rpc.
  //
  // If the release fails with error indicating the existence of a concurrent request of the same
  // session, then no locks corresponding to the session are released, and the rpc must be retried.
  Status ReleaseObjectLocks(const tserver::ReleaseObjectLockRequestPB& req);
  void DumpLocksToHtml(std::ostream& out);

  size_t TEST_GrantedLocksSize() const;
  size_t TEST_WaitingLocksSize() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace yb::tablet
