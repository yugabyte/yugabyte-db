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

#pragma once

#include <unordered_map>
#include "yb/util/flags/auto_flags.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_string.h"

namespace yb {

constexpr uint32 kInvalidAutoFlagsConfigVersion = 0;
constexpr uint32 kMinAutoFlagsConfigVersion = 1;

namespace server {
class ServerBaseOptions;
}

YB_STRONGLY_TYPED_BOOL(RuntimeAutoFlag);
using ProcessName = std::string;

struct AutoFlagInfo {
  const std::string name;
  const AutoFlagClass flag_class;
  const RuntimeAutoFlag is_runtime;
  AutoFlagInfo(
      const std::string& name, const AutoFlagClass flag_class, const RuntimeAutoFlag is_runtime)
      : name(name), flag_class(flag_class), is_runtime(is_runtime) {}
};

YB_STRONGLY_TYPED_BOOL(PromoteNonRuntimeAutoFlags)

// Map from ProcessName to AutoFlag infos.
using AutoFlagsInfoMap = std::unordered_map<ProcessName, std::vector<AutoFlagInfo>>;
// Map from ProcessName to AutoFlag names.
using AutoFlagsNameMap = std::unordered_map<ProcessName, std::unordered_set<std::string>>;

namespace AutoFlagsUtil {
std::string DumpAutoFlagsToJSON(const ProcessName& program_name);

Result<AutoFlagsInfoMap> GetAvailableAutoFlags();

Result<AutoFlagsInfoMap> GetFlagsEligibleForPromotion(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime);

AutoFlagsInfoMap GetFlagsEligibleForPromotion(
    const AutoFlagsInfoMap& available_flags, const AutoFlagClass max_flag_class,
    const PromoteNonRuntimeAutoFlags promote_non_runtime);

// Returns true if all flags in base_config with class greater to or equal to min_class are found in
// the base_config.
// That is, base_config is a superset of flags with class greater to or equal to min_class.
Result<bool> AreAutoFlagsCompatible(
    const AutoFlagsNameMap& base_flags, const AutoFlagsNameMap& to_check_flags,
    const AutoFlagsInfoMap& auto_flag_infos, AutoFlagClass min_class);

};  // namespace AutoFlagsUtil

}  // namespace yb
