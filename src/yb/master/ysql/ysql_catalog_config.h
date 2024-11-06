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

#include <shared_mutex>

#include "yb/master/master_fwd.h"
#include "yb/util/cow_object.h"

namespace yb {

namespace master {

class IsInitDbDoneResponsePB;
class IsYsqlMajorVersionUpgradeInitdbDoneResponsePB;
struct LeaderEpoch;
struct PersistentSysConfigInfo;

class YsqlCatalogConfig {
 public:
  explicit YsqlCatalogConfig(SysCatalogTable& sys_catalog);
  ~YsqlCatalogConfig() = default;

  Status PrepareDefaultIfNeeded(int64_t term) EXCLUDES(mutex_);
  void SetConfig(scoped_refptr<SysConfigInfo> config) EXCLUDES(mutex_);
  void Reset() EXCLUDES(mutex_);

  uint64 GetVersion() const EXCLUDES(mutex_);

  // Increments and return the new version.
  Result<uint64> IncrementVersion(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  bool IsInitDbDone() const EXCLUDES(mutex_);
  void IsInitDbDone(IsInitDbDoneResponsePB& resp) const EXCLUDES(mutex_);

  Status SetInitDbDone(const Status& initdb_status, const LeaderEpoch& epoch) EXCLUDES(mutex_);

  bool IsTransactionalSysCatalogEnabled() const EXCLUDES(mutex_);
  Status SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) EXCLUDES(mutex_);

  void IsYsqlMajorVersionUpgradeInitdbDone(
      IsYsqlMajorVersionUpgradeInitdbDoneResponsePB& resp) const EXCLUDES(mutex_);
  Status ResetNextVerInitdbStatus(const LeaderEpoch& epoch) EXCLUDES(mutex_);
  Status SetNextVerInitdbDone(const Status& upgrade_status, const LeaderEpoch& epoch)
      EXCLUDES(mutex_);

 private:
  std::pair<CowReadLock<PersistentSysConfigInfo>, const SysYSQLCatalogConfigEntryPB&> LockForRead()
      const EXCLUDES(mutex_);

  std::pair<CowWriteLock<PersistentSysConfigInfo>, SysYSQLCatalogConfigEntryPB&> LockForWrite()
      REQUIRES_SHARED(mutex_);

  SysCatalogTable& sys_catalog_;
  mutable std::shared_mutex mutex_;
  scoped_refptr<SysConfigInfo> config_ GUARDED_BY(mutex_);
};

}  // namespace master

}  // namespace yb
