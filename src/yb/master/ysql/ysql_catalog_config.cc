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
#include "yb/util/version_info.pb.h"
#include "yb/util/version_info.h"
#include "yb/util/is_operation_done_result.h"

DECLARE_bool(log_ysql_catalog_versions);

DEFINE_test_flag(
    string, fail_ysql_catalog_upgrade_state_transition_from, "",
    "When set fail the transition to the provided state");

namespace yb::master {

namespace {

uint32 GetMajorVersionOfCurrentBuild() {
  VersionInfoPB version_info;
  VersionInfo::GetVersionInfoPB(&version_info);
  return version_info.ysql_major_version();
}

bool IsYsqlMajorCatalogOperationRunning(YsqlMajorCatalogUpgradeInfoPB::State state) {
  return state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE ||
         state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB ||
         state == YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK;
}

}  // namespace

YsqlCatalogConfig::YsqlCatalogConfig(SysCatalogTable& sys_catalog) : sys_catalog_(sys_catalog) {}

Status YsqlCatalogConfig::PrepareDefaultIfNeeded(const LeaderEpoch& epoch) {
  std::lock_guard m_lock(mutex_);
  if (config_) {
    return Status::OK();
  }

  SysYSQLCatalogConfigEntryPB ysql_catalog_config;
  ysql_catalog_config.set_version(0);
  ysql_catalog_config.mutable_ysql_major_catalog_upgrade_info()->set_catalog_version(
      GetMajorVersionOfCurrentBuild());

  config_ = new SysConfigInfo(kYsqlCatalogConfigType);
  auto l = config_->LockForWrite();
  *l.mutable_data()->pb.mutable_ysql_catalog_config() = std::move(ysql_catalog_config);

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();

  return Status::OK();
}

void YsqlCatalogConfig::SetConfig(scoped_refptr<SysConfigInfo> config) {
  auto& ysql_catalog_config = config->mutable_metadata()->mutable_dirty()->pb.ysql_catalog_config();
  if (ysql_catalog_config.has_ysql_major_catalog_upgrade_info()) {
    const auto persisted_version =
        ysql_catalog_config.ysql_major_catalog_upgrade_info().catalog_version();
    LOG_IF(FATAL, persisted_version > GetMajorVersionOfCurrentBuild())
        << "Persisted major version in YSQL catalog config is not supported. Restart "
           "the process in the correct version. Min required major version: "
        << persisted_version << ", Current major version: " << VersionInfo::GetShortVersionString();

    // A new yb-master leader has started. If we were in the middle of the ysql major catalog
    // upgrade (initdb, pg_upgrade, or rollback) then mark the major upgrade as failed. No action
    // is taken if we are in the monitoring phase.
    // We cannot update the config right now, so do so after the sys_catalog is loaded.
    restarted_during_major_upgrade_ = IsYsqlMajorCatalogOperationRunning(
        ysql_catalog_config.ysql_major_catalog_upgrade_info().state());
  }

  std::lock_guard m_lock(mutex_);
  LOG_IF(WARNING, config_ != nullptr) << "Multiple Ysql Catalog configs found";
  config_ = std::move(config);
}

void YsqlCatalogConfig::SysCatalogLoaded(const LeaderEpoch& epoch) {
  if (restarted_during_major_upgrade_) {
    ERROR_NOT_OK(
        TransitionMajorCatalogUpgradeState(
            YsqlMajorCatalogUpgradeInfoPB::FAILED, epoch,
            STATUS(InternalError, "yb-master restarted during ysql major catalog upgrade")),
        "Failed to set major version upgrade state to FAILED");
    restarted_during_major_upgrade_ = false;
  }
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

IsOperationDoneResult YsqlCatalogConfig::IsYsqlMajorCatalogUpgradeDone() const {
  auto [l, pb] = LockForRead();
  if (!pb.has_ysql_major_catalog_upgrade_info()) {
    return IsOperationDoneResult::Done();
  }

  const auto state = pb.ysql_major_catalog_upgrade_info().state();
  if (IsYsqlMajorCatalogOperationRunning(state)) {
    return IsOperationDoneResult::NotDone();
  }

  Status status;
  if (pb.ysql_major_catalog_upgrade_info().has_previous_error()) {
    status = StatusFromPB(pb.ysql_major_catalog_upgrade_info().previous_error());
  }

  return IsOperationDoneResult::Done(status);
}

YsqlMajorCatalogUpgradeInfoPB::State YsqlCatalogConfig::GetMajorCatalogUpgradeState() const {
  auto [l, pb] = LockForRead();
  return pb.ysql_major_catalog_upgrade_info().state();
}

bool YsqlCatalogConfig::IsCurrentVersionCatalogEstablished() const {
  auto [l, pb] = LockForRead();
  auto major_upgrade_info = pb.ysql_major_catalog_upgrade_info();

  // In the DONE state we either have not started the catalog upgrade or have completed it.
  if (major_upgrade_info.state() == YsqlMajorCatalogUpgradeInfoPB::DONE) {
    return major_upgrade_info.catalog_version() == GetMajorVersionOfCurrentBuild();
  }

  return major_upgrade_info.state() == YsqlMajorCatalogUpgradeInfoPB::MONITORING;
}

const std::unordered_map<
    YsqlMajorCatalogUpgradeInfoPB::State, std::unordered_set<YsqlMajorCatalogUpgradeInfoPB::State>>
    kAllowedTransitions = {
        {YsqlMajorCatalogUpgradeInfoPB::INVALID, {}},

        {YsqlMajorCatalogUpgradeInfoPB::DONE, {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB}},

        {YsqlMajorCatalogUpgradeInfoPB::FAILED,
         {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB,
         {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE,
          YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE,
         {YsqlMajorCatalogUpgradeInfoPB::MONITORING,
          YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::MONITORING,
         {YsqlMajorCatalogUpgradeInfoPB::DONE, YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
          YsqlMajorCatalogUpgradeInfoPB::FAILED}},

        {YsqlMajorCatalogUpgradeInfoPB::PERFORMING_ROLLBACK,
         {YsqlMajorCatalogUpgradeInfoPB::DONE, YsqlMajorCatalogUpgradeInfoPB::FAILED}},
};

Status YsqlCatalogConfig::TransitionMajorCatalogUpgradeState(
    const YsqlMajorCatalogUpgradeInfoPB::State new_state, const LeaderEpoch& epoch,
    const Status& failed_status) {
  DCHECK_EQ(kAllowedTransitions.size(), YsqlMajorCatalogUpgradeInfoPB::State_ARRAYSIZE);

  const auto new_state_str = YsqlMajorCatalogUpgradeInfoPB::State_Name(new_state);

  RSTATUS_DCHECK_EQ(
      failed_status.ok(), new_state != YsqlMajorCatalogUpgradeInfoPB::FAILED, IllegalState,
      Format("Bad status must be set if and only if transitioning to FAILED state", failed_status));

  SharedLock m_lock(mutex_);
  auto [l, pb] = LockForWrite();

  auto* ysql_major_catalog_upgrade_info = pb.mutable_ysql_major_catalog_upgrade_info();
  const auto current_state = ysql_major_catalog_upgrade_info->state();
  SCHECK_NE(
      current_state, new_state, IllegalState,
      Format("Major upgrade state already set to $0", new_state_str));

  const auto current_state_str = YsqlMajorCatalogUpgradeInfoPB::State_Name(current_state);

  SCHECK_NE(
      current_state_str, FLAGS_TEST_fail_ysql_catalog_upgrade_state_transition_from, IllegalState,
      "Failed due to FLAGS_TEST_fail_ysql_catalog_upgrade_state_transition_from");

  auto allowed_states_it = FindOrNull(kAllowedTransitions, current_state);
  RSTATUS_DCHECK(allowed_states_it, IllegalState, Format("Invalid state $0", current_state_str));

  SCHECK(
      allowed_states_it->contains(new_state), IllegalState,
      Format("Invalid state transition from $0 to $1", current_state_str, new_state_str));

  if (current_state == YsqlMajorCatalogUpgradeInfoPB::MONITORING &&
      new_state == YsqlMajorCatalogUpgradeInfoPB::DONE) {
    ysql_major_catalog_upgrade_info->set_catalog_version(GetMajorVersionOfCurrentBuild());
  } else if (current_state == YsqlMajorCatalogUpgradeInfoPB::DONE) {
    const auto major_version = GetMajorVersionOfCurrentBuild();
    SCHECK_GT(
        major_version, ysql_major_catalog_upgrade_info->catalog_version(), IllegalState,
        "Ysql Catalog is already on the current major version");
  }

  ysql_major_catalog_upgrade_info->set_state(new_state);

  if (!failed_status.ok()) {
    StatusToPB(failed_status, ysql_major_catalog_upgrade_info->mutable_previous_error());
  } else {
    ysql_major_catalog_upgrade_info->clear_previous_error();
  }

  LOG(INFO) << "Transitioned major upgrade state from " << current_state_str << " to "
            << new_state_str;

  RETURN_NOT_OK(sys_catalog_.Upsert(epoch, config_));
  l.Commit();
  return Status::OK();
}

}  // namespace yb::master
