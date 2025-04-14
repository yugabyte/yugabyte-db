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

#include "yb/master/ysql/ysql_catalog_config.h"

#include "yb/master/master_defaults.h"
#include "yb/util/shared_lock.h"
#include "yb/master/sys_catalog.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/common/version_info.h"

DECLARE_bool(log_ysql_catalog_versions);

namespace yb::master {

YsqlCatalogConfig::YsqlCatalogConfig(SysCatalogTable& sys_catalog) : sys_catalog_(sys_catalog) {}

Status YsqlCatalogConfig::PrepareDefaultIfNeeded(const LeaderEpoch& epoch) {
  std::lock_guard m_lock(mutex_);
  if (config_) {
    return Status::OK();
  }

  SysYSQLCatalogConfigEntryPB ysql_catalog_config;
  ysql_catalog_config.set_version(0);
  ysql_catalog_config.mutable_ysql_major_catalog_upgrade_info()->set_catalog_version(
      VersionInfo::YsqlMajorVersion());

  config_ = new SysConfigInfo(kYsqlCatalogConfigType);
  auto l = config_->LockForWrite();
  *l.mutable_data()->pb.mutable_ysql_catalog_config() = std::move(ysql_catalog_config);

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();

  return Status::OK();
}

void YsqlCatalogConfig::SetConfig(scoped_refptr<SysConfigInfo> config) {
  std::lock_guard m_lock(mutex_);
  LOG_IF(WARNING, config_ != nullptr) << "Multiple Ysql Catalog configs found";
  config_ = std::move(config);
}

void YsqlCatalogConfig::Reset() {
  std::lock_guard m_lock(mutex_);
  config_.reset();
}

std::pair<CowReadLock<PersistentSysConfigInfo>, const SysYSQLCatalogConfigEntryPB&>
YsqlCatalogConfig::LockForRead() const {
  SharedLock lock(mutex_);
  CHECK_NOTNULL(config_.get());
  auto l = config_->LockForRead();
  const auto& pb = l->pb.ysql_catalog_config();
  return {std::move(l), pb};
}

std::pair<YsqlCatalogConfig::Updater, SysYSQLCatalogConfigEntryPB&> YsqlCatalogConfig::LockForWrite(
    const LeaderEpoch& epoch) {
  auto l = YsqlCatalogConfig::Updater(*this, epoch);
  auto& pb = l.pb();
  return {std::move(l), pb};
}

uint64 YsqlCatalogConfig::GetVersion() const {
  auto [l, pb] = LockForRead();
  return pb.version();
}

Result<uint64> YsqlCatalogConfig::IncrementVersion(const LeaderEpoch& epoch) {
  auto [l, pb] = LockForWrite(epoch);

  uint64_t new_version = pb.version() + 1;
  pb.set_version(new_version);

  RETURN_NOT_OK(l.UpsertAndCommit());

  if (FLAGS_log_ysql_catalog_versions) {
    LOG_WITH_FUNC(WARNING) << "set catalog version: " << new_version
                           << " (using old protobuf method)";
  }

  return new_version;
}

IsOperationDoneResult YsqlCatalogConfig::IsInitDbDone() const {
  auto [l, pb] = LockForRead();
  if (!pb.initdb_done()) {
    return IsOperationDoneResult::NotDone();
  }
  if (pb.has_initdb_error() && !pb.initdb_error().empty()) {
    return IsOperationDoneResult::Done(STATUS(InternalError, pb.initdb_error()));
  }
  return IsOperationDoneResult::Done();
}

Status YsqlCatalogConfig::SetInitDbDone(const Status& initdb_status, const LeaderEpoch& epoch) {
  if (initdb_status.ok()) {
    LOG(INFO) << "Global initdb completed successfully";
  } else {
    LOG(ERROR) << "Global initdb failed: " << initdb_status;
  }

  auto [l, pb] = LockForWrite(epoch);

  pb.set_initdb_done(true);
  if (initdb_status.ok()) {
    pb.clear_initdb_error();
  } else {
    pb.set_initdb_error(initdb_status.ToString());
  }

  RETURN_NOT_OK(l.UpsertAndCommit());
  return Status::OK();
}

bool YsqlCatalogConfig::IsTransactionalSysCatalogEnabled() const {
  auto [l, pb] = LockForRead();
  return pb.transactional_sys_catalog_enabled();
}

Status YsqlCatalogConfig::SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) {
  LOG(INFO) << "Marking YSQL system catalog as transactional in YSQL catalog config";

  auto [l, pb] = LockForWrite(epoch);
  pb.set_transactional_sys_catalog_enabled(true);
  return l.UpsertAndCommit();
}

YsqlMajorCatalogUpgradeInfoPB::State YsqlCatalogConfig::GetMajorCatalogUpgradeState() const {
  auto [l, pb] = LockForRead();
  if (!pb.has_ysql_major_catalog_upgrade_info()) {
    return YsqlMajorCatalogUpgradeInfoPB::DONE;
  }

  return pb.ysql_major_catalog_upgrade_info().state();
}

Status YsqlCatalogConfig::GetMajorCatalogUpgradePreviousError() const {
  auto [l, pb] = LockForRead();
  Status status;
  if (pb.has_ysql_major_catalog_upgrade_info() &&
      pb.ysql_major_catalog_upgrade_info().has_previous_error()) {
    status = StatusFromPB(pb.ysql_major_catalog_upgrade_info().previous_error());
  }
  return status;
}

bool YsqlCatalogConfig::IsPreviousVersionCatalogCleanupRequired() const {
  auto [l, pb] = LockForRead();
  return pb.has_ysql_major_catalog_upgrade_info() &&
         pb.ysql_major_catalog_upgrade_info().previous_version_catalog_cleanup_required();
}

YsqlCatalogConfig::Updater::Updater(
    YsqlCatalogConfig& ysql_catalog_config, const LeaderEpoch& epoch)
    : ysql_catalog_config_(ysql_catalog_config), epoch_(epoch) {
  SharedLock m_lock(ysql_catalog_config_.mutex_);
  CHECK_NOTNULL(ysql_catalog_config_.config_.get());
  cow_lock_ = ysql_catalog_config_.config_->LockForWrite();
}

YsqlCatalogConfig::Updater::Updater(YsqlCatalogConfig::Updater&& rhs) noexcept
    : ysql_catalog_config_(rhs.ysql_catalog_config_),
      cow_lock_(std::move(rhs.cow_lock_)),
      epoch_(rhs.epoch_) {}

SysYSQLCatalogConfigEntryPB& YsqlCatalogConfig::Updater::pb() {
  CHECK(cow_lock_.locked());
  return *cow_lock_.mutable_data()->pb.mutable_ysql_catalog_config();
}

Status YsqlCatalogConfig::Updater::UpsertAndCommit() {
  RSTATUS_DCHECK(
      cow_lock_.locked(), InternalError, "Invalid attempt to YsqlCatalogConfig without a lock");

  SharedLock m_lock(ysql_catalog_config_.mutex_);
  // Although we have released and reacquired the mutex_, the epoch ensures that config_ pointer
  // remains unchanged.
  RETURN_NOT_OK(ysql_catalog_config_.sys_catalog_.Upsert(epoch_, ysql_catalog_config_.config_));
  cow_lock_.Commit();

  return Status::OK();
}

}  // namespace yb::master
