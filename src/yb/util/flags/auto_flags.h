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

// AutoFlags
//
// AutoFlags are gFlags with two hard-coded values: Initial and Target (instead of the regular gFlag
// Default). AutoFlags have two states: promoted and not-promoted. An AutoFlag is set to its Initial
// value in the not-promoted state and to its Target value in the promoted state.
//
// When to use AutoFlags?
//
// A Workflow is the series of activities that are necessary to complete a task (ex: user issuing a
// DML, tablet split, load balancing). They can be simple and confined to a function block or
// involve coordination between multiple universes. Workflows that add or modify the format of data
// that is sent over the wire to another process or is persisted on disk require special care. The
// consumer of the data and its persistence determines when the new workflow can be safely enabled
// after an upgrade, and whether it is safe to perform rollbacks or downgrades after they are
// enabled. AutoFlags are required to safely and automatically enable such workflows.
//
// - New universes will start with all AutoFlags in the promoted state (Target value).
// - When a process undergoes an upgrade, all new AutoFlags will be in the not-promoted state
// (Initial value). They will get promoted only after all the yb-master and yb-tserver processes in
// the universe (and other external processes like CDCServers and xClusters for class kExternal)
// have been upgraded.
// - Custom override via command line or the flags file has higher precedence and takes effect
// immediately.
// - AutoFlags cannot be renamed or removed. If the flag definition is moved to a different file,
// then make sure the new file is included in the same processes as before.
// TODO: Validate this at build time [#13474]
// - At build time, an auto_flags.json file is created in the bin directory. This file has the list
// of all AutoFlags in the yb-master and yb-tserver processes.
//
// Note: Non-Runtime flags require an additional restart of the process after the upgrade, so they
// should be avoided when possible.
// String flags are not Runtime safe.

#include <string_view>

#include <gflags/gflags.h>

#include "yb/util/enums.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/tostring.h"

namespace yb {

YB_DEFINE_ENUM(
    AutoFlagClass,
    // Adds/modifies format of data sent over the wire to another process within the universe. No
    // modification to persisted data.
    ((kLocalVolatile, 1))
    // Adds/modifies format of data sent over the wire or persisted and used within the universe.
    ((kLocalPersisted, 2))
    // Adds/modifies format of data which might be used outside the universe.
    // Example of external processes: XCluster and CDCServer.
    ((kExternal, 3))
    // Promotes a flag only for new installs, no promotions for upgrade workflow.
    // Example: features that are not yet safe for upgrades.
    ((kNewInstallsOnly, 4)));

// Disable Auto Flag Promotion for a test file
#define DISABLE_PROMOTE_ALL_AUTO_FLAGS_FOR_TEST \
  static yb::auto_flags_internal::DisablePromoteAllAutoFlags disable_promote_all_auto_flags_

// Runtime AutoFlags
#define DEFINE_RUNTIME_AUTO_bool(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO(bool, name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

#define DEFINE_RUNTIME_AUTO_int32(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO(int32, name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

#define DEFINE_RUNTIME_AUTO_int64(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO(int64, name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

#define DEFINE_RUNTIME_AUTO_uint64(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO(uint64, name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

#define DEFINE_RUNTIME_AUTO_double(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO(double, name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

#define DEFINE_RUNTIME_AUTO_string(name, flag_class, initial_val, target_val, txt) \
  _DEFINE_AUTO_string(name, flag_class, initial_val, target_val, RUNTIME, true, txt); \

struct AutoFlagDescription {
  std::string name;
  const void* flag_ptr; /* Pointer to the gFlag */
  yb::AutoFlagClass flag_class;
  std::string initial_val;
  std::string target_val;
  bool is_runtime;
};

const AutoFlagDescription* GetAutoFlagDescription(const std::string& flag_name);
std::vector<const AutoFlagDescription*> GetAllAutoFlagsDescription();

void PromoteAutoFlag(const AutoFlagDescription& flag_desc);
void PromoteAllAutoFlags();

void DemoteAutoFlag(const AutoFlagDescription& flag_desc);

bool IsFlagPromoted(
    const gflags::CommandLineFlagInfo& flag, const AutoFlagDescription& auto_flag_desc);

// Should test promote all Auto Flags at startup?
bool ShouldTestPromoteAllAutoFlags();

const char* AutoFlagValueAsString(bool value);

template <class T>
auto AutoFlagValueAsString(const T& value) {
  return AsString(value);
}

// Create the gFlag with appropriate tags and register it as an AutoFlag.
// COMPILE_ASSERT is used to make sure initial_val and target_val are of the specified flag type.
// If a value of an invalid type is provided, it will cause compilation to fail with an error like
// FLAG_<name>_initial_val_is_not_valid.
#define _DEFINE_AUTO( \
  type, name, flag_class, initial_val, target_val, runtime_prefix, is_runtime, txt) \
  static_assert( \
      yb::auto_flags_internal::BOOST_PP_CAT(IsValid_, type)(initial_val), \
      "Initial value of AutoFlag " BOOST_PP_STRINGIZE(name) " '" \
      BOOST_PP_STRINGIZE(initial_val) "' is not assignable to " BOOST_PP_STRINGIZE(type)); \
  static_assert( \
      yb::auto_flags_internal::BOOST_PP_CAT(IsValid_, type)(target_val), \
      "Target value of AutoFlag " BOOST_PP_STRINGIZE(name) " '" BOOST_PP_STRINGIZE(target_val) \
      "' is not assignable to " BOOST_PP_STRINGIZE(type)); \
  static_assert((initial_val) != (target_val), "Initial and target value of AutoFlag " \
  BOOST_PP_STRINGIZE(name) " are the same"); \
  BOOST_PP_CAT(DEFINE_, BOOST_PP_CAT(runtime_prefix, BOOST_PP_CAT(_, type)))( \
    name, initial_val, txt); \
  namespace { \
  yb::auto_flags_internal::AutoFlagDescRegisterer \
      BOOST_PP_CAT(afr_, name)(BOOST_PP_STRINGIZE(name), /* name */ \
        &BOOST_PP_CAT(FLAGS_, name),              /* flag_ptr */ \
        ::yb::AutoFlagClass::flag_class,          /* flag_class */ \
        ::yb::AutoFlagValueAsString(initial_val), /* initial_val */ \
        ::yb::AutoFlagValueAsString(target_val),  /* target_val */ \
        is_runtime);                              /* is_runtime */ \
  } \
  _TAG_FLAG(name, ::yb::FlagTag::kAuto, auto); \
  TAG_FLAG(name, stable)

#define _DEFINE_AUTO_string( \
  name, flag_class, initial_val, target_val, runtime_prefix, is_runtime, txt) \
  static_assert( \
      yb::auto_flags_internal::IsValid_string(initial_val), \
      "Initial value of AutoFlag " BOOST_PP_STRINGIZE(name) " '" initial_val \
                                                            "' is not assignable to string"); \
  static_assert( \
      yb::auto_flags_internal::IsValid_string(target_val), \
      "Target value of AutoFlag " BOOST_PP_STRINGIZE(name) " '" target_val \
                                                           "' is not assignable to string"); \
  static_assert(yb::auto_flags_internal::StringsNotEqual(initial_val, target_val), "Initial and " \
  "target value of AutoFlag " BOOST_PP_STRINGIZE(name) " are the same"); \
  BOOST_PP_CAT(DEFINE_, BOOST_PP_CAT(runtime_prefix, _string))(name, initial_val, txt); \
  namespace { \
  yb::auto_flags_internal::AutoFlagDescRegisterer \
      BOOST_PP_CAT(afr_, name)(BOOST_PP_STRINGIZE(name), /* name */ \
        &BOOST_PP_CAT(FLAGS_, name),   /* flag_ptr */ \
        yb::AutoFlagClass::flag_class, /* flag_class */ \
        initial_val,                   /* initial_val */ \
        target_val,                    /* target_val */ \
        is_runtime);                   /* is_runtime */ \
  } \
  _TAG_FLAG(name, ::yb::FlagTag::kAuto, auto); \
  TAG_FLAG(name, stable)

namespace auto_flags_internal {

constexpr bool StringsNotEqual(char const* a, char const* b) {
    return std::string_view(a) != b;
}

template <typename T>
constexpr bool IsValid_bool(T a) {
  return std::is_same<bool, decltype(a)>::value;
}

template <typename T>
constexpr bool IsValid_int32(T a) {
  return std::is_same<int32_t, decltype(a)>::value;
}

template <typename T>
constexpr bool IsValid_int64(T a) {
  return std::is_same<int64_t, decltype(a)>::value || std::is_same<uint32_t, decltype(a)>::value ||
         std::is_same<int32_t, decltype(a)>::value;
}

template <typename T>
constexpr bool IsValid_uint64(T a) {
  return std::is_same<uint64_t, decltype(a)>::value || std::is_same<uint32_t, decltype(a)>::value ||
         std::is_same<int32_t, decltype(a)>::value;
}

template <typename T>
constexpr bool IsValid_double(T a) {
  return std::is_same<double, decltype(a)>::value || std::is_same<int64_t, decltype(a)>::value ||
         std::is_same<uint32_t, decltype(a)>::value || std::is_same<int32_t, decltype(a)>::value;
}

template <typename T>
constexpr bool IsValid_string(T a) {
  return std::is_same<std::string, decltype(a)>::value ||
         std::is_same<const char*, decltype(a)>::value || std::is_same<char*, decltype(a)>::value;
}

void SetAutoFlagDescription(const AutoFlagDescription* desc);

class AutoFlagDescRegisterer {
 public:
  AutoFlagDescRegisterer(
      std::string name, const void* flag_ptr, yb::AutoFlagClass flag_class,
      const std::string& initial_val, const std::string& target_val, bool is_runtime)
      : description_{
            .name = name,
            .flag_ptr = flag_ptr,
            .flag_class = flag_class,
            .initial_val = initial_val,
            .target_val = target_val,
            .is_runtime = is_runtime} {
    SetAutoFlagDescription(&description_);
  };

 private:
  AutoFlagDescription description_;
  DISALLOW_COPY_AND_ASSIGN(AutoFlagDescRegisterer);
};

class DisablePromoteAllAutoFlags {
 public:
  DisablePromoteAllAutoFlags();
};
}  // namespace auto_flags_internal

}  // namespace yb
