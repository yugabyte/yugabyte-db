//
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
//

#pragma once

#include <future>
#include <mutex>
#include <string_view>

#include "yb/client/client_fwd.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/result.h"

namespace yb::tserver {

YB_DEFINE_ENUM(AdvisoryLockTableSchemaVersion, (kDbIdHash)(kAllColsHash));

constexpr std::string_view kPgAdvisoryLocksTableName = "pg_advisory_locks";

struct YsqlAdvisoryLocksTableLockId {
  uint32_t db_oid;
  uint32_t class_oid;
  uint32_t objid;
  uint32_t objsubid;
};

// Helper class for the advisory locks table.
class YsqlAdvisoryLocksTable {
 public:
  using LockId = YsqlAdvisoryLocksTableLockId;

  explicit YsqlAdvisoryLocksTable(std::shared_future<client::YBClient*> client_future);

  Result<client::YBPgsqlLockOpPtr> MakeLockOp(
      const LockId& lock_id, AdvisoryLockMode mode, bool wait) EXCLUDES(mutex_);

  Result<client::YBPgsqlLockOpPtr> MakeUnlockOp(
      const LockId& lock_id, AdvisoryLockMode mode) EXCLUDES(mutex_);

  Result<std::vector<client::YBOperationPtr>> MakeUnlockAllOps(uint32_t db_oid) EXCLUDES(mutex_);

  auto TEST_GetTable() { return GetTable(); }

  Result<std::vector<client::internal::RemoteTabletPtr>> LookupAllTablets(
      CoarseTimePoint deadline) EXCLUDES(mutex_);

 private:
  Result<client::YBTablePtr> GetTable() EXCLUDES(mutex_);

  std::mutex mutex_;
  client::YBTablePtr table_ GUARDED_BY(mutex_);
  std::shared_future<client::YBClient*> client_future_;
  std::vector<client::internal::RemoteTabletPtr> tablet_ptrs_;
  // The client request for lock/unlock has to be set based on the advisory lock table schema
  // which changed at some point. The below field helps achieve it.
  AdvisoryLockTableSchemaVersion schema_version_;
};

} // namespace yb::tserver
