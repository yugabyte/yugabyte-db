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

#include "yb/master/master_auto_flags_manager.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/xcluster/xcluster_manager_if.h"
#include "yb/tablet/operations/change_auto_flags_config_operation.h"
#include "yb/util/scope_exit.h"

DEFINE_NON_RUNTIME_int32(limit_auto_flag_promote_for_new_universe,
    yb::to_underlying(yb::AutoFlagClass::kNewInstallsOnly),
    "The maximum class value up to which AutoFlags are promoted during new cluster creation. "
    "Value should be in the range [0-4]. Will not promote any AutoFlags if set to 0.");
TAG_FLAG(limit_auto_flag_promote_for_new_universe, stable);

DEFINE_test_flag(bool, disable_versioned_auto_flags, false,
    "When set new flags will be added to the older 'flags' field instead of the newer versioned "
    "'auto_flags' field of PromotedFlagsPerProcessPB");

namespace {
bool ValidateAutoFlagClass(const char* flag_name, int32_t value) {
  if (value == 0) {
    return true;
  }

  auto result = yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(value);
  if (!result.ok()) {
    LOG_FLAG_VALIDATION_ERROR(flag_name, value)
        << "Value should be in the range [0-3]. " << result.status();
    return false;
  }

  return true;
}
}  // namespace

DEFINE_validator(limit_auto_flag_promote_for_new_universe, &ValidateAutoFlagClass);

DECLARE_bool(disable_auto_flags_management);

namespace yb::master {

namespace {

int FindPosition(
    const google::protobuf::RepeatedPtrField<std::string>& list, const std::string& name) {
  for (int i = 0; i < list.size(); i++) {
    if (list.Get(i) == name) {
      return i;
    }
  }
  return -1;
}

template <typename Type>
void Erase(google::protobuf::RepeatedPtrField<Type>* list, int position) {
  CHECK(position >= 0 && position < list->size());
  list->erase(std::next(list->begin(), position));
}

const AutoFlagInfo* FindOrNullptr(
    const std::vector<AutoFlagInfo>& auto_flags, const std::string& flag_name) {
  auto it = std::find_if(auto_flags.begin(), auto_flags.end(), [&flag_name](const auto& flag_info) {
    return flag_info.name == flag_name;
  });

  if (it == auto_flags.end()) {
    return nullptr;
  }
  return &(*it);
}

// Add the flags to the config if it is not already present. Bumps up the config version and returns
// true if any new flags were added. Config with version 0 is always bumped to 1. Second output bool
// indicates if any non-runtime flags were added.
PromoteAutoFlagsOutcome InsertFlagsToConfig(
    const AutoFlagsInfoMap& flags_to_insert, AutoFlagsConfigPB& config, bool force_version_change) {
  auto non_runtime_flags_added = false;
  bool config_changed = false;
  // Initial config or forced version bump.
  if (config.config_version() == kInvalidAutoFlagsConfigVersion || force_version_change) {
    config_changed = true;
  }
  auto new_config_version = config.config_version() + 1;

  for (const auto& [process_name, process_flags] : flags_to_insert) {
    if (!process_flags.empty()) {
      google::protobuf::RepeatedPtrField<std::string>* process_flags_pb = nullptr;
      google::protobuf::RepeatedPtrField<PromotedFlagInfoPB>* process_flag_info_pb = nullptr;
      for (auto& promoted_flags : *config.mutable_promoted_flags()) {
        if (promoted_flags.process_name() == process_name) {
          process_flags_pb = promoted_flags.mutable_flags();
          process_flag_info_pb = promoted_flags.mutable_flag_infos();

          // Backfill the promoted_version field to 0 for older AutoFlags promoted before yb
          // version 2.20.
          if (process_flag_info_pb->size() != process_flags_pb->size()) {
            CHECK(process_flag_info_pb->empty());
            for (int i = 0; i < process_flags_pb->size(); i++) {
              process_flag_info_pb->Add()->set_promoted_version(0);
            }
            config_changed = true;
          }
          break;
        }
      }

      if (!process_flags_pb) {
        // Process not found in the config. Add a new entry.
        auto new_per_process_flags = config.add_promoted_flags();
        new_per_process_flags->set_process_name(process_name);
        process_flags_pb = new_per_process_flags->mutable_flags();
        process_flag_info_pb = new_per_process_flags->mutable_flag_infos();
        config_changed = true;
      }

      for (const auto& flags_to_promote : process_flags) {
        // Add the flag if it does not exist.
        if (FindPosition(*process_flags_pb, flags_to_promote.name) < 0) {
          *process_flags_pb->Add() = flags_to_promote.name;
          if (!FLAGS_TEST_disable_versioned_auto_flags) {
            process_flag_info_pb->Add()->set_promoted_version(new_config_version);
          }
          non_runtime_flags_added |= !flags_to_promote.is_runtime;
          config_changed = true;
        }
      }
    }
  }

  if (!config_changed) {
    return PromoteAutoFlagsOutcome::kNoFlagsPromoted;
  }

  config.set_config_version(new_config_version);
  return non_runtime_flags_added ? PromoteAutoFlagsOutcome::kNonRuntimeFlagsPromoted
                                 : PromoteAutoFlagsOutcome::kNewFlagsPromoted;
}

// Remove flags from the config if they were promoted on a version higher than rollback_version.
// Bumps up the config version if any flags were removed. Returns the list of removed flags per
// process.
AutoFlagsNameMap RemoveFlagsFromConfig(uint32_t rollback_version, AutoFlagsConfigPB& config) {
  bool config_changed = false;
  AutoFlagsNameMap flags_removed;

  for (auto& promoted_flags : *config.mutable_promoted_flags()) {
    for (int i = 0; i < promoted_flags.flag_infos().size();) {
      if (promoted_flags.flag_infos(i).promoted_version() > rollback_version) {
        flags_removed[promoted_flags.process_name()].insert(promoted_flags.flags(i));
        Erase(promoted_flags.mutable_flags(), i);
        Erase(promoted_flags.mutable_flag_infos(), i);
        config_changed = true;
      } else {
        i++;
      }
    }
  }

  if (config_changed) {
    config.set_config_version(config.config_version() + 1);
  }

  return flags_removed;
}

// Remove a single flag from the config if it exists. Bumps up the config version and returns true
// if the flag was removed.
Result<bool> RemoveFlagFromConfig(
    const ProcessName& process_name, const std::string& flag_name, AutoFlagsConfigPB* config) {
  for (auto& promoted_flags : *config->mutable_promoted_flags()) {
    if (promoted_flags.process_name() == process_name) {
      auto pos = FindPosition(promoted_flags.flags(), flag_name);
      if (pos >= 0) {
        SCHECK(!promoted_flags.flag_infos().empty(), IllegalState, "Flag info list is empty");
        Erase(promoted_flags.mutable_flags(), pos);
        // Support Rollback before the backfill of promoted_version field.
        Erase(promoted_flags.mutable_flag_infos(), pos);
        config->set_config_version(config->config_version() + 1);
        return true;
      }

      break;
    }
  }

  return false;
}

AutoFlagsNameMap GetFlagsFromConfig(const AutoFlagsConfigPB& config) {
  AutoFlagsNameMap result;
  for (auto& per_process_flags : config.promoted_flags()) {
    auto& process_flags = result[per_process_flags.process_name()];
    for (auto& flag_name : per_process_flags.flags()) {
      process_flags.insert(flag_name);
    }
  }
  return result;
}

Result<AutoFlagInfo> GetFlagInfo(const ProcessName& process_name, const std::string& flag_name) {
  auto all_flags = VERIFY_RESULT(AutoFlagsUtil::GetAvailableAutoFlags());

  SCHECK(all_flags.contains(process_name), NotFound, "Process $0 not found", process_name);
  auto flag_info = FindOrNullptr(all_flags[process_name], flag_name);
  SCHECK(flag_info, NotFound, "AutoFlag $0 not found in process $1", flag_name, process_name);

  return *flag_info;
}

// Wrapper over the AutoFlagsUtil::AreAutoFlagsCompatible.
Result<bool> AreAutoFlagsCompatible(
    const AutoFlagsConfigPB& base_config, const AutoFlagsConfigPB& config_to_check,
    AutoFlagClass min_class) {
  const auto base_flags = GetFlagsFromConfig(base_config);
  const auto to_check_flags = GetFlagsFromConfig(config_to_check);
  const auto auto_flag_infos = VERIFY_RESULT(AutoFlagsUtil::GetAvailableAutoFlags());

  return AutoFlagsUtil::AreAutoFlagsCompatible(
      base_flags, to_check_flags, auto_flag_infos, min_class);
}

}  // namespace

constexpr auto kYbMasterProcessName = "yb-master";

MasterAutoFlagsManager::MasterAutoFlagsManager(
    const scoped_refptr<ClockBase>& clock, FsManager* fs_manager, CatalogManager* catalog_manager)
    : AutoFlagsManagerBase(kYbMasterProcessName, clock, fs_manager),
      catalog_manager_(catalog_manager),
      update_lock_(mutex_, std::defer_lock) {}

Status MasterAutoFlagsManager::Init(
    rpc::Messenger* messenger, std::function<bool()> has_sys_catalog_func, bool is_shell_mode) {
  RETURN_NOT_OK(AutoFlagsManagerBase::Init(messenger));

  if (VERIFY_RESULT(LoadFromFile())) {
    return Status::OK();
  }

  if (has_sys_catalog_func()) {
    // Pre-existing cluster that has just upgraded to a version with AutoFlags.
    RETURN_NOT_OK(CreateEmptyConfig());
    return Status::OK();
  }

  if (is_shell_mode) {
    // New master in Shell mode.
    LOG(INFO) << "AutoFlags initialization delayed as master is in Shell mode.";
    return Status::OK();
  }

  // Newly created cluster.
  return CreateConfigForNewCluster();
}

Status MasterAutoFlagsManager::LoadFromMasterLeader(
    const server::MasterAddresses& master_addresses) {
  return AutoFlagsManagerBase::LoadFromMasterLeader(master_addresses);
}

Status MasterAutoFlagsManager::LoadNewConfig(const AutoFlagsConfigPB new_config) {
  std::lock_guard l(mutex_);
  return LoadFromConfigUnlocked(std::move(new_config), ApplyNonRuntimeAutoFlags::kTrue);
}

Status MasterAutoFlagsManager::CreateEmptyConfig() {
  LOG(INFO) << "Creating empty AutoFlags configuration.";

  AutoFlagsConfigPB new_config;
  new_config.set_config_version(kInvalidAutoFlagsConfigVersion);
  return LoadNewConfig(std::move(new_config));
}

Status MasterAutoFlagsManager::CreateConfigForNewCluster() {
  LOG(INFO) << "Creating AutoFlags configuration for new cluster.";

  if (FLAGS_limit_auto_flag_promote_for_new_universe != 0) {
    const auto max_flag_class = VERIFY_RESULT(yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(
        FLAGS_limit_auto_flag_promote_for_new_universe));
    const auto promote_non_runtime = PromoteNonRuntimeAutoFlags::kTrue;

    if (FLAGS_disable_auto_flags_management) {
      LOG(WARNING) << "AutoFlags management is disabled.";
      return Status::OK();
    }

    LOG(INFO) << "Promoting AutoFlags. max_flag_class: " << ToString(max_flag_class)
              << ", promote_non_runtime: " << promote_non_runtime;

    const auto eligible_flags = VERIFY_RESULT(
        AutoFlagsUtil::GetFlagsEligibleForPromotion(max_flag_class, promote_non_runtime));

    auto new_config = GetConfig();
    InsertFlagsToConfig(eligible_flags, new_config, true /* force */);
    DCHECK_GE(new_config.config_version(), kMinAutoFlagsConfigVersion);

    RETURN_NOT_OK(LoadNewConfig(std::move(new_config)));
  }

  return Status::OK();
}

Status MasterAutoFlagsManager::PersistConfigInSysCatalog(AutoFlagsConfigPB& new_config) {
  consensus::ChangeAutoFlagsConfigOpResponsePB operation_res;
  // SubmitToSysCatalog will set the correct tablet
  auto operation = std::make_unique<tablet::ChangeAutoFlagsConfigOperation>(nullptr /* tablet */);
  *operation->AllocateRequest() = new_config;
  CountDownLatch latch(1);
  operation->set_completion_callback(
      tablet::MakeLatchOperationCompletionCallback(&latch, &operation_res));

  RETURN_NOT_OK_PREPEND(
      catalog_manager_->SubmitToSysCatalog(std::move(operation)),
      "Failed to store AutoFlags config");

  latch.Wait();

  if (operation_res.has_error()) {
    auto status = StatusFromPB(operation_res.error().status());
    LOG(WARNING) << "Failed to apply new AutoFlags config: " << status.ToString();
    return status;
  }
  catalog_manager_->GetXClusterManager()->NotifyAutoFlagsConfigChanged();

  return Status::OK();
}

// No thread safety analysis, as it cannot detect that the mutex is locked by UniqueLock.
Status MasterAutoFlagsManager::StoreConfig(AutoFlagsConfigPB& new_config)
    NO_THREAD_SAFETY_ANALYSIS {
  // The config has to get quorum committed in the sys_catalog before it can be stored in
  // current_config_ even on the master leader. So, there will be delay between when the
  // config_apply_time is computed and it being stored.
  // During this window we should not respond to heartbeats since it will renew the leases in the
  // tserver for auto_flags_apply_delay_ms, which can cause it to be higher than the
  // config_apply_time we picked. We hold onto the mutex_ so that we do not respond to heartbeats.
  //
  // Raft and master leader election will guarantee that this is safe from crashes:
  // If we crash between writing the WAL op and the it getting applied, then the new leader will
  // apply it before responding to heartbeats. This is because new leader will have to commit the
  // NO_OP record and apply all pending operations before it is marked ready. If the op was never
  // replicated to the new leader then the operation will be lost, and the user will have to try
  // again.
  RSTATUS_DCHECK(
      !update_lock_.owns_lock(), IllegalState, "AutoFlags config update already in progress");
  update_lock_.lock();
  auto se = ScopeExit([this]() NO_THREAD_SAFETY_ANALYSIS { update_lock_.unlock(); });

  // This is an update of an existing config. The initial config must be applied immediately.
  DCHECK_GE(new_config.config_version(), kMinAutoFlagsConfigVersion);
  // Every config change must update the version by 1.
  RSTATUS_DCHECK_EQ(
      new_config.config_version(), current_config_.config_version() + 1, IllegalState,
      "Attempting to store a stale config");

  const auto now = clock_->Now();
  const auto config_apply_ht = now.AddDelta(GetApplyDelay());
  new_config.set_config_apply_time(config_apply_ht.ToUint64());

  return PersistConfigInSysCatalog(new_config);
}

// No thread safety analysis, as it cannot detect that the mutex is locked by UniqueLock.
Status MasterAutoFlagsManager::ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB new_config)
    NO_THREAD_SAFETY_ANALYSIS {
  bool unlock_needed = false;
  auto se = ScopeExit([&update_lock = update_lock_, &unlock_needed]() NO_THREAD_SAFETY_ANALYSIS {
    if (unlock_needed) {
      update_lock.unlock();
    }
  });

  // This function will be invoked when the ChangeAutoFlagsConfigOperation is applied. The
  // StoreUpdatedConfig may be holding the lock already and waiting for us to complete in which case
  // we do not have to reacquire the lock. If we crashed during StoreUpdatedConfig, then the
  // operation can get applied at tablet bootstrap or a later time, and in both cases we need to get
  // the lock.
  if (!update_lock_.owns_lock()) {
    update_lock_.lock();
    unlock_needed = true;
  }

  return LoadFromConfigUnlocked(std::move(new_config), ApplyNonRuntimeAutoFlags::kFalse);
}

Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> MasterAutoFlagsManager::PromoteAutoFlags(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime_flags,
    const bool force_version_change) {
  SCHECK(!FLAGS_disable_auto_flags_management, NotSupported, "AutoFlags management is disabled.");

  LOG(INFO) << "Promoting AutoFlags. max_flag_class: " << ToString(max_flag_class)
            << ", promote_non_runtime: " << promote_non_runtime_flags
            << ", force: " << force_version_change;

  const auto eligible_flags = VERIFY_RESULT(
      AutoFlagsUtil::GetFlagsEligibleForPromotion(max_flag_class, promote_non_runtime_flags));

  auto new_config = GetConfig();
  auto outcome = InsertFlagsToConfig(eligible_flags, new_config, force_version_change);

  if (outcome != PromoteAutoFlagsOutcome::kNoFlagsPromoted) {
    RETURN_NOT_OK(StoreConfig(new_config));
  }

  return std::make_pair(new_config.config_version(), outcome);
}

Result<std::pair<uint32_t, bool>> MasterAutoFlagsManager::RollbackAutoFlags(
    uint32_t rollback_version) {
  SCHECK(!FLAGS_disable_auto_flags_management, NotSupported, "AutoFlags management is disabled.");

  auto new_config = GetConfig();
  const auto removed_flags = RemoveFlagsFromConfig(rollback_version, new_config);
  if (removed_flags.empty()) {
    // Nothing to rollback.
    return std::make_pair(GetConfigVersion(), false);
  }

  // Make sure only Volatile flags are being rolled back.
  auto available_flags = VERIFY_RESULT(AutoFlagsUtil::GetAvailableAutoFlags());
  for (auto& [process_name, flags_removed] : removed_flags) {
    for (auto& flag_name : flags_removed) {
      auto flag_info = FindOrNullptr(available_flags[process_name], flag_name);
      SCHECK(
          flag_info, RuntimeError,
          Format("Flag $0 not found in list of available flags", flag_name));

      SCHECK_EQ(
          flag_info->flag_class, AutoFlagClass::kLocalVolatile, InvalidArgument,
          Format(
              "Flag $0 belongs to class $1 which is not eligible for rollback", flag_name,
              flag_info->flag_class));
    }
  }

  LOG(INFO) << "Rollback AutoFlags. rollback_version: " << rollback_version
            << ", flags_removed: " << yb::ToString(removed_flags)
            << ", new_config_version: " << new_config.config_version();

  RETURN_NOT_OK(StoreConfig(new_config));

  return std::make_pair(new_config.config_version(), true);
}

Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> MasterAutoFlagsManager::PromoteSingleAutoFlag(
    const ProcessName& process_name, const std::string& flag_name) {
  SCHECK(!FLAGS_disable_auto_flags_management, NotSupported, "AutoFlags management is disabled.");

  AutoFlagsInfoMap flag_to_insert;
  flag_to_insert[process_name].emplace_back(VERIFY_RESULT(GetFlagInfo(process_name, flag_name)));

  auto new_config = GetConfig();
  auto outcome = InsertFlagsToConfig(flag_to_insert, new_config, /* force_version_change */ false);

  if (outcome != PromoteAutoFlagsOutcome::kNoFlagsPromoted) {
    RETURN_NOT_OK(StoreConfig(new_config));
  }

  LOG(INFO) << "Promote AutoFlag. process_name: " << process_name << ", flag_name: " << flag_name
            << ", new_config_version: " << new_config.config_version();

  return std::make_pair(new_config.config_version(), outcome);
}

Result<std::pair<uint32_t, bool>> MasterAutoFlagsManager::DemoteSingleAutoFlag(
    const ProcessName& process_name, const std::string& flag_name) {
  SCHECK(!FLAGS_disable_auto_flags_management, NotSupported, "AutoFlags management is disabled.");

  // Make sure process and AutoFlag exists.
  RETURN_NOT_OK(GetFlagInfo(process_name, flag_name));

  auto new_config = GetConfig();
  if (!VERIFY_RESULT(RemoveFlagFromConfig(process_name, flag_name, &new_config))) {
    // Nothing to rollback.
    return std::make_pair(GetConfigVersion(), false);
  }

  LOG(INFO) << "Demote AutoFlag. process_name: " << process_name << ", flag_name: " << flag_name
            << ", new_config_version: " << new_config.config_version();

  RETURN_NOT_OK(StoreConfig(new_config));

  return std::make_pair(new_config.config_version(), true);
}

Status MasterAutoFlagsManager::GetAutoFlagsConfig(
    const GetAutoFlagsConfigRequestPB* req, GetAutoFlagsConfigResponsePB* resp) {
  *resp->mutable_config() = GetConfig();

  return Status::OK();
}

Status MasterAutoFlagsManager::PromoteAutoFlags(
    const PromoteAutoFlagsRequestPB* req, PromoteAutoFlagsResponsePB* resp) {
  const auto max_class = VERIFY_RESULT_PREPEND(
      ParseEnumInsensitive<AutoFlagClass>(req->max_flag_class()),
      "Invalid value provided for flag class");

  // It is expected PromoteAutoFlags RPC is triggered only for upgrades, hence it is required
  // to avoid promotion of flags with AutoFlagClass::kNewInstallsOnly class.
  SCHECK_LT(
      max_class, AutoFlagClass::kNewInstallsOnly, InvalidArgument,
      Format("max_class cannot be set to $0.", ToString(AutoFlagClass::kNewInstallsOnly)));

  auto [new_config_version, outcome] = VERIFY_RESULT(PromoteAutoFlags(
      max_class, PromoteNonRuntimeAutoFlags(req->promote_non_runtime_flags()), req->force()));

  resp->set_new_config_version(new_config_version);
  resp->set_flags_promoted(outcome != PromoteAutoFlagsOutcome::kNoFlagsPromoted);
  resp->set_non_runtime_flags_promoted(
      outcome == PromoteAutoFlagsOutcome::kNonRuntimeFlagsPromoted);
  return Status::OK();
}

Status MasterAutoFlagsManager::RollbackAutoFlags(
    const RollbackAutoFlagsRequestPB* req, RollbackAutoFlagsResponsePB* resp) {
  auto [new_config_version, outcome] = VERIFY_RESULT(RollbackAutoFlags(req->rollback_version()));

  resp->set_new_config_version(new_config_version);
  resp->set_flags_rolledback(outcome);
  return Status::OK();
}

Status MasterAutoFlagsManager::PromoteSingleAutoFlag(
    const PromoteSingleAutoFlagRequestPB* req, PromoteSingleAutoFlagResponsePB* resp) {
  auto [new_config_version, outcome] =
      VERIFY_RESULT(PromoteSingleAutoFlag(req->process_name(), req->auto_flag_name()));

  resp->set_new_config_version(new_config_version);
  resp->set_flag_promoted(outcome != PromoteAutoFlagsOutcome::kNoFlagsPromoted);
  resp->set_non_runtime_flag_promoted(outcome == PromoteAutoFlagsOutcome::kNonRuntimeFlagsPromoted);
  return Status::OK();
}

Status MasterAutoFlagsManager::DemoteSingleAutoFlag(
    const DemoteSingleAutoFlagRequestPB* req, DemoteSingleAutoFlagResponsePB* resp) {
  auto [new_config_version, outcome] =
      VERIFY_RESULT(DemoteSingleAutoFlag(req->process_name(), req->auto_flag_name()));

  resp->set_new_config_version(new_config_version);
  resp->set_flag_demoted(outcome);
  return Status::OK();
}

Status MasterAutoFlagsManager::ValidateAutoFlagsConfig(
    const ValidateAutoFlagsConfigRequestPB* req, ValidateAutoFlagsConfigResponsePB* resp) {
  VLOG_WITH_FUNC(1) << req->ShortDebugString();

  auto min_class = AutoFlagClass::kLocalVolatile;
  if (req->has_min_flag_class()) {
    min_class = VERIFY_RESULT_PREPEND(
        yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(req->min_flag_class()),
        "Invalid value provided for flag class");
  }

  auto local_auto_flag_config = GetConfig();
  auto valid =
      VERIFY_RESULT(AreAutoFlagsCompatible(local_auto_flag_config, req->config(), min_class));
  VLOG_WITH_FUNC(1) << valid;

  resp->set_valid(valid);
  resp->set_config_version(local_auto_flag_config.config_version());

  return Status::OK();
}

}  // namespace yb::master
