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

#include <map>

#include "yb/gutil/singleton.h"
#include "yb/util/flags/auto_flags.h"
#include "yb/util/flags.h"

using std::string;

DEFINE_test_flag(bool, promote_all_auto_flags, false, "Promote all AutoFlags for tests.");

namespace yb {

namespace {
// Singleton registry storing the set of tags for each flag.
class AutoFlagsRegistry {
 public:
  static AutoFlagsRegistry* GetInstance() { return Singleton<AutoFlagsRegistry>::get(); }

  const AutoFlagDescription* Get(const string& name) const {
    if (!description_map_.contains(name)) {
      return nullptr;
    }
    return description_map_.at(name);
  }

  std::vector<const AutoFlagDescription*> GetAll() const {
    std::vector<const AutoFlagDescription*> output;
    for (auto const& desc : description_map_) {
      output.push_back(desc.second);
    }
    return output;
  }

  void Set(const AutoFlagDescription* auto_flag) { description_map_[auto_flag->name] = auto_flag; }

 private:
  friend class Singleton<AutoFlagsRegistry>;

  AutoFlagsRegistry() {}

  std::map<string, const AutoFlagDescription*> description_map_;

  DISALLOW_COPY_AND_ASSIGN(AutoFlagsRegistry);
};

static bool TEST_promote_all_auto_flags = true;

}  // namespace

namespace auto_flags_internal {
void SetAutoFlagDescription(const AutoFlagDescription* desc) {
  AutoFlagsRegistry::GetInstance()->Set(desc);
}

DisablePromoteAllAutoFlags::DisablePromoteAllAutoFlags() { TEST_promote_all_auto_flags = false; }

}  // namespace auto_flags_internal

bool ShouldTestPromoteAllAutoFlags() { return TEST_promote_all_auto_flags; }

const AutoFlagDescription* GetAutoFlagDescription(const string& flag_name) {
  return AutoFlagsRegistry::GetInstance()->Get(flag_name);
}

std::vector<const AutoFlagDescription*> GetAllAutoFlagsDescription() {
  return AutoFlagsRegistry::GetInstance()->GetAll();
}

void PromoteAutoFlag(const AutoFlagDescription& flag_desc) {
  auto res = flags_internal::SetFlagInternal(
      flag_desc.flag_ptr, flag_desc.name.c_str(), flag_desc.target_val.c_str(),
      gflags::FlagSettingMode::SET_FLAGS_DEFAULT);

  // This can fail only on flag validation errors. DumpAutoFlagsJSONAndExit promotes all AutoFlags
  // to run the validations, and it is always called at build time, so this will never fail during
  // actual process runtime.
  LOG_IF(FATAL, res.empty()) << "Failed to set AutoFlag " << flag_desc.name << " to target value"
                             << flag_desc.target_val;
}

void PromoteAllAutoFlags() {
  auto auto_flags = GetAllAutoFlagsDescription();
  for (const auto& flag_desc : auto_flags) {
    PromoteAutoFlag(*flag_desc);
  }
}

void DemoteAutoFlag(const AutoFlagDescription& flag_desc) {
  auto res = flags_internal::SetFlagInternal(
      flag_desc.flag_ptr, flag_desc.name.c_str(), flag_desc.initial_val.c_str(),
      gflags::FlagSettingMode::SET_FLAGS_DEFAULT);

  // This can fail only on flag validation errors. DumpAutoFlagsJSONAndExit promotes all AutoFlags
  // to run the validations, and it is always called at build time, so this will never fail during
  // actual process runtime.
  LOG_IF(FATAL, res.empty()) << "Failed to set AutoFlag " << flag_desc.name << " to initial value"
                             << flag_desc.initial_val;
}

bool IsFlagPromoted(
    const gflags::CommandLineFlagInfo& flag, const AutoFlagDescription& auto_flag_desc) {
  return flag.default_value == auto_flag_desc.target_val;
}

const char* AutoFlagValueAsString(bool value) {
  return value ? "true" : "false";
}

}  // namespace yb
