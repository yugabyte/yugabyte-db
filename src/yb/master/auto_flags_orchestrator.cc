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

#include <fstream>
#include <string>

#include "yb/client/auto_flags_manager.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/master/auto_flags_orchestrator.h"
#include "yb/master/catalog_manager.h"
#include "yb/tablet/operations/change_auto_flags_config_operation.h"
#include "yb/tablet/operations/operation.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/auto_flags_util.h"

using std::string;

DEFINE_int32(
    limit_auto_flag_promote_for_new_universe, yb::to_underlying(yb::AutoFlagClass::kExternal),
    "The maximum class value up to which AutoFlags are promoted during new cluster creation. "
    "Value should be in the range [0-3]. Will not promote any AutoFlags if set to 0.");
TAG_FLAG(limit_auto_flag_promote_for_new_universe, stable);

namespace {
bool ValidateAutoFlagClass(const char* flag_name, int32_t value) {
  if (value == 0) {
    return true;
  }

  auto result = yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(value);
  if (!result.ok()) {
    LOG(ERROR) << "Invalid value for '" << flag_name << "'. Value should be in the range [0-3]. "
               << result.status();
    return false;
  }

  return true;
}

__attribute__((unused))
DEFINE_validator(limit_auto_flag_promote_for_new_universe, &ValidateAutoFlagClass);

}  // namespace

DECLARE_bool(disable_auto_flags_management);

namespace yb {
using OK = Status::OK;

namespace master {

namespace {
// Add the flags to the config if it is not already present. Bumps up the config version and returns
// true if any new flags were added. Config with version 0 is always bumped to 1.
bool InsertFlagsToConfig(
    const std::map<std::string, vector<AutoFlagInfo>>& eligible_flags, AutoFlagsConfigPB* config,
    bool* non_runtime_flags_added) {
  *non_runtime_flags_added = false;
  bool config_changed = false;
  // Initial config
  if (config->config_version() == 0) {
    config_changed = true;
  }

  for (const auto& per_process_flags : eligible_flags) {
    if (!per_process_flags.second.empty()) {
      const auto& process_name = per_process_flags.first;
      google::protobuf::RepeatedPtrField<string>* process_flags_pb = nullptr;
      for (auto& promoted_flags : *config->mutable_promoted_flags()) {
        if (promoted_flags.process_name() == process_name) {
          process_flags_pb = promoted_flags.mutable_flags();
          break;
        }
      }

      if (!process_flags_pb) {
        auto new_per_process_flags = config->add_promoted_flags();
        new_per_process_flags->set_process_name(process_name);
        process_flags_pb = new_per_process_flags->mutable_flags();
        config_changed = true;
      }

      for (const auto& flags_to_promote : per_process_flags.second) {
        if (std::find(process_flags_pb->begin(), process_flags_pb->end(), flags_to_promote.name) ==
            process_flags_pb->end()) {
          *process_flags_pb->Add() = flags_to_promote.name;
          *non_runtime_flags_added |= !flags_to_promote.is_runtime;
          config_changed = true;
        }
      }
    }
  }

  if (config_changed) {
    config->set_config_version(config->config_version() + 1);
  }

  return config_changed;
}

Status PromoteAutoFlags(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime,
    const ApplyNonRuntimeAutoFlags apply_non_runtime, AutoFlagsManager* auto_flag_manager) {
  if (FLAGS_disable_auto_flags_management) {
    LOG(WARNING) << "AutoFlags management is disabled.";
    return OK();
  }

  LOG(INFO) << "Promoting AutoFlags. max_flag_class: " << ToString(max_flag_class)
            << ", promote_non_runtime: " << promote_non_runtime.get()
            << ", apply_non_runtime: " << apply_non_runtime.get();

  const auto eligible_flags = VERIFY_RESULT(
      AutoFlagsUtil::GetFlagsEligibleForPromotion(max_flag_class, promote_non_runtime));

  auto new_config = auto_flag_manager->GetConfig();
  bool dummy_non_runtime_flags_added;
  if (InsertFlagsToConfig(eligible_flags, &new_config, &dummy_non_runtime_flags_added)) {
    RETURN_NOT_OK(auto_flag_manager->LoadFromConfig(std::move(new_config), apply_non_runtime));
  }

  return OK();
}

}  // namespace

Status CreateAutoFlagsConfigForNewCluster(AutoFlagsManager* auto_flag_manager) {
  LOG(INFO) << "Creating AutoFlags configuration for new cluster.";

  if (FLAGS_limit_auto_flag_promote_for_new_universe != 0) {
    const auto max_class = VERIFY_RESULT(yb::UnderlyingToEnumSlow<yb::AutoFlagClass>(
        FLAGS_limit_auto_flag_promote_for_new_universe));

    return PromoteAutoFlags(
        max_class, PromoteNonRuntimeAutoFlags::kTrue, ApplyNonRuntimeAutoFlags::kTrue,
        auto_flag_manager);
  }

  return OK();
}

Status CreateEmptyAutoFlagsConfig(AutoFlagsManager* auto_flag_manager) {
  LOG(INFO) << "Creating empty AutoFlags configuration.";

  AutoFlagsConfigPB new_config;
  new_config.set_config_version(1);
  RETURN_NOT_OK(
      auto_flag_manager->LoadFromConfig(std::move(new_config), ApplyNonRuntimeAutoFlags::kTrue));
  return OK();
}

Status PromoteAutoFlags(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime_flags,
    const bool force, const AutoFlagsManager& auto_flag_manager, CatalogManager* catalog_manager,
    uint32_t* new_config_version, bool* non_runtime_flags_promoted) {
  SCHECK(!FLAGS_disable_auto_flags_management, NotSupported, "AutoFlags management is disabled.");

  *non_runtime_flags_promoted = false;

  const auto eligible_flags = VERIFY_RESULT(
      AutoFlagsUtil::GetFlagsEligibleForPromotion(max_flag_class, promote_non_runtime_flags));

  auto new_config = auto_flag_manager.GetConfig();
  if (!InsertFlagsToConfig(eligible_flags, &new_config, non_runtime_flags_promoted)) {
    if (force) {
      new_config.set_config_version(new_config.config_version() + 1);
    } else {
      return STATUS(AlreadyPresent, "Nothing to promote");
    }
  }

  consensus::ChangeAutoFlagsConfigOpResponsePB operation_res;
  // SubmitToSysCatalog will set the correct tablet
  auto operation =
      std::make_unique<tablet::ChangeAutoFlagsConfigOperation>(nullptr /*tablet*/, &new_config);
  CountDownLatch latch(1);
  operation->set_completion_callback(
      tablet::MakeLatchOperationCompletionCallback(&latch, &operation_res));

  LOG(INFO) << "Promoting AutoFlags. max_flag_class: " << ToString(max_flag_class)
            << ", promote_non_runtime: " << promote_non_runtime_flags << ", force: " << force;

  catalog_manager->SubmitToSysCatalog(std::move(operation));

  latch.Wait();

  if (operation_res.has_error()) {
    auto status = StatusFromPB(operation_res.error().status());
    LOG(WARNING) << "Failed to apply new AutoFlags config: " << status.ToString();
    return status;
  }

  *new_config_version = new_config.config_version();

  return OK();
}
}  // namespace master
}  // namespace yb
