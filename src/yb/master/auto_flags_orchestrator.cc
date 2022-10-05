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
#include "yb/common/wire_protocol.pb.h"
#include "yb/master/auto_flags_orchestrator.h"
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
namespace master {

namespace {
// Add the flags to the config if it is not already present. Bumps up the config version and returns
// true if any new flags were added. Config with version 0 is always bumped to 1.
bool InsertFlagsToConfig(
    const std::map<std::string, vector<AutoFlagInfo>>& eligible_flags, AutoFlagsConfigPB* config) {
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
    return Status::OK();
  }

  LOG(INFO) << "Promoting AutoFlags. max_flag_class: " << ToString(max_flag_class)
            << ", promote_non_runtime: " << promote_non_runtime.get()
            << ", apply_non_runtime: " << apply_non_runtime.get();

  const auto eligible_flags = VERIFY_RESULT(
      AutoFlagsUtil::GetFlagsEligibleForPromotion(max_flag_class, promote_non_runtime));

  auto new_config = auto_flag_manager->GetConfig();
  if (InsertFlagsToConfig(eligible_flags, &new_config)) {
    RETURN_NOT_OK(auto_flag_manager->LoadFromConfig(std::move(new_config), apply_non_runtime));
  }

  return Status::OK();
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

  return Status::OK();
}

Status CreateEmptyAutoFlagsConfig(AutoFlagsManager* auto_flag_manager) {
  LOG(INFO) << "Creating empty AutoFlags configuration.";

  AutoFlagsConfigPB new_config;
  new_config.set_config_version(1);
  RETURN_NOT_OK(
      auto_flag_manager->LoadFromConfig(std::move(new_config), ApplyNonRuntimeAutoFlags::kTrue));
  return Status::OK();
}

}  // namespace master
}  // namespace yb
