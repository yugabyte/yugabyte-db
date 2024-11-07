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

#include "yb/master/master_admin.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/util/shared_lock.h"
#include "yb/master/sys_catalog.h"

#include "yb/master/catalog_entity_info.h"

DECLARE_bool(log_ysql_catalog_versions);

namespace yb::master {

YsqlCatalogConfig::YsqlCatalogConfig(SysCatalogTable& sys_catalog) : sys_catalog_(sys_catalog) {}

Status YsqlCatalogConfig::PrepareDefaultIfNeeded(int64_t term) {
  std::lock_guard m_lock(mutex_);
  if (config_) {
    return Status::OK();
  }

  SysYSQLCatalogConfigEntryPB ysql_catalog_config;
  ysql_catalog_config.set_version(0);

  // Create in memory objects.
  config_ = new SysConfigInfo(kYsqlCatalogConfigType);

  // Prepare write.
  auto l = config_->LockForWrite();
  *l.mutable_data()->pb.mutable_ysql_catalog_config() = std::move(ysql_catalog_config);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_.Upsert(term, config_));
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

std::pair<CowWriteLock<PersistentSysConfigInfo>, SysYSQLCatalogConfigEntryPB&>
YsqlCatalogConfig::LockForWrite() {
  CHECK_NOTNULL(config_.get());
  auto l = config_->LockForWrite();
  auto& pb = *l.mutable_data()->pb.mutable_ysql_catalog_config();
  return {std::move(l), pb};
}

uint64 YsqlCatalogConfig::GetVersion() const {
  auto [l, pb] = LockForRead();
  return pb.version();
}

Result<uint64> YsqlCatalogConfig::IncrementVersion(const LeaderEpoch& epoch) {
  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  uint64_t new_version = pb.version() + 1;
  pb.set_version(new_version);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();

  if (FLAGS_log_ysql_catalog_versions) {
    LOG_WITH_FUNC(WARNING) << "set catalog version: " << new_version
                           << " (using old protobuf method)";
  }

  return new_version;
}

bool YsqlCatalogConfig::IsInitDbDone() const {
  auto [l, pb] = LockForRead();
  return pb.initdb_done();
}

void YsqlCatalogConfig::IsInitDbDone(IsInitDbDoneResponsePB& resp) const {
  auto [l, pb] = LockForRead();
  resp.set_done(pb.initdb_done());
  if (pb.has_initdb_error() && !pb.initdb_error().empty()) {
    resp.set_initdb_error(pb.initdb_error());
  }
}

Status YsqlCatalogConfig::SetInitDbDone(const Status& initdb_status, const LeaderEpoch& epoch) {
  if (initdb_status.ok()) {
    LOG(INFO) << "Global initdb completed successfully";
  } else {
    LOG(ERROR) << "Global initdb failed: " << initdb_status;
  }

  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  pb.set_initdb_done(true);
  if (initdb_status.ok()) {
    pb.clear_initdb_error();
  } else {
    pb.set_initdb_error(initdb_status.ToString());
  }

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();
  return Status::OK();
}

bool YsqlCatalogConfig::IsTransactionalSysCatalogEnabled() const {
  auto [l, pb] = LockForRead();
  return pb.transactional_sys_catalog_enabled();
}

Status YsqlCatalogConfig::SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) {
  LOG(INFO) << "Marking YSQL system catalog as transactional in YSQL catalog config";

  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  pb.set_transactional_sys_catalog_enabled(true);
  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();

  return Status::OK();
}

void YsqlCatalogConfig::IsYsqlMajorVersionUpgradeInitdbDone(
    IsYsqlMajorVersionUpgradeInitdbDoneResponsePB& resp) const {
  auto [l, pb] = LockForRead();
  resp.set_done(pb.ysql_major_upgrade_info().next_ver_initdb_done());
  if (pb.ysql_major_upgrade_info().has_next_ver_initdb_error()) {
    resp.mutable_initdb_error()->CopyFrom(pb.ysql_major_upgrade_info().next_ver_initdb_error());
  }
}

Status YsqlCatalogConfig::ResetNextVerInitdbStatus(const LeaderEpoch& epoch) {
  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  auto* ysql_major_upgrade_info = pb.mutable_ysql_major_upgrade_info();
  ysql_major_upgrade_info->set_next_ver_initdb_done(false);
  ysql_major_upgrade_info->clear_next_ver_initdb_error();
  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();
  return Status::OK();
}

Status YsqlCatalogConfig::SetNextVerInitdbDone(
    const Status& upgrade_status, const LeaderEpoch& epoch) {
  if (upgrade_status.ok()) {
    LOG(INFO) << "Ysql major catalog upgrade completed successfully";
  } else {
    LOG(ERROR) << "Ysql major catalog upgrade failed: " << upgrade_status;
  }

  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  auto* ysql_major_upgrade_info = pb.mutable_ysql_major_upgrade_info();
  ysql_major_upgrade_info->set_next_ver_initdb_done(true);

  if (upgrade_status.ok()) {
    ysql_major_upgrade_info->clear_next_ver_initdb_error();
  } else {
    ysql_major_upgrade_info->mutable_next_ver_initdb_error()->set_code(
        MasterErrorPB::INTERNAL_ERROR);
    StatusToPB(
        upgrade_status, ysql_major_upgrade_info->mutable_next_ver_initdb_error()->mutable_status());
  }

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();
  return Status::OK();
}

}  // namespace yb::master
