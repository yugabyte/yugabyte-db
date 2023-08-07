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

#include <map>
#include "yb/util/flags/auto_flags.h"
#include "yb/util/status.h"

namespace yb {
namespace server {
class ServerBaseOptions;
}

YB_STRONGLY_TYPED_BOOL(RuntimeAutoFlag)

struct AutoFlagInfo {
  const std::string name;
  const AutoFlagClass flag_class;
  const RuntimeAutoFlag is_runtime;
  AutoFlagInfo(
      const std::string& name, const AutoFlagClass flag_class, const RuntimeAutoFlag is_runtime)
      : name(name), flag_class(flag_class), is_runtime(is_runtime) {}
};

YB_STRONGLY_TYPED_BOOL(PromoteNonRuntimeAutoFlags)

typedef std::map<std::string, std::vector<AutoFlagInfo>> AutoFlagsInfoMap;

namespace AutoFlagsUtil {
std::string DumpAutoFlagsToJSON(const std::string& program_name);

Result<AutoFlagsInfoMap> GetAvailableAutoFlags();

Result<AutoFlagsInfoMap> GetFlagsEligibleForPromotion(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime);

AutoFlagsInfoMap GetFlagsEligibleForPromotion(
    const AutoFlagsInfoMap& available_flags, const AutoFlagClass max_flag_class,
    const PromoteNonRuntimeAutoFlags promote_non_runtime);
};  // namespace AutoFlagsUtil

}  // namespace yb
