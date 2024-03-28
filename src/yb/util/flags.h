// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>

#include "yb/util/flags/flag_tags.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/flags/auto_flags.h"

// Redefine the macro from gflags.h with an unused attribute.
// Note: This macro should be used in the same file that DEFINEs the flag. Using it any other file
// can result in segfault due to indeterminate order of static initialization.
#ifdef DEFINE_validator
#undef DEFINE_validator
#endif
#define DEFINE_validator(name, validator) \
  static_assert( \
      sizeof(_DEFINE_FLAG_IN_FILE(name)), "validator must be DEFINED in the same file as the flag"); \
  static const bool BOOST_PP_CAT(name, _validator_registered) __attribute__((unused)) = \
      google::RegisterFlagValidator(&BOOST_PP_CAT(FLAGS_, name), (validator))

#define SET_FLAG(name, value) \
  (::yb::flags_internal::SetFlag(&BOOST_PP_CAT(FLAGS_, name), BOOST_PP_STRINGIZE(name), value))

#define SET_FLAG_DEFAULT_AND_CURRENT(name, value) \
  (::yb::flags_internal::SetFlagDefaultAndCurrent( \
      &BOOST_PP_CAT(FLAGS_, name), BOOST_PP_STRINGIZE(name), value))

namespace yb {

// Looks for flags in argv and parses them.  Rearranges argv to put
// flags first, or removes them entirely if remove_flags is true.
// If a flag is defined more than once in the command line or flag
// file, the last definition is used.  Returns the index (into argv)
// of the first non-flag argument.
//
// This is a wrapper around google::ParseCommandLineFlags, but integrates
// with YB flag tags and AutoFlags. For example, --helpxml will include the list of
// tags for each flag and AutoFlag info. This should be be used instead of
// google::ParseCommandLineFlags in any user-facing binary.
//
// See gflags.h for more information.
void ParseCommandLineFlags(int* argc, char*** argv, bool remove_flags);

// Reads the given file and updates the value of all flags specified in the file. Returns true on
// success, false otherwise.
bool RefreshFlagsFile(const std::string& filename);

namespace flags_internal {
// Set a particular flag and invoke update callbacks. Returns a string
// describing the new value that the option has been set to. The return value API is not
// well-specified, so just depend on it to be empty if the setting failed for some reason
// -- the name is not a valid flag name, or the value is not a valid value -- and non-empty else.
std::string SetFlagInternal(
    const void* flag_ptr, const char* flag_name, const std::string& new_value,
    gflags::FlagSettingMode set_mode);

// Set a particular flag and invoke update callbacks.
Status SetFlagInternal(const void* flag_ptr, const char* flag_name, const std::string& new_value);

Status SetFlag(const std::string* flag_ptr, const char* flag_name, const std::string& new_value);

template <class T>
Status SetFlag(const T* flag_ptr, const char* flag_name, const T& new_value) {
  return SetFlagInternal(flag_ptr, flag_name, std::to_string(new_value));
}

// Set a particular flags current and default value and invoke update callbacks.
Status SetFlagDefaultAndCurrentInternal(
    const void* flag_ptr, const char* flag_name, const std::string& value);

Status SetFlagDefaultAndCurrent(
    const std::string* flag_ptr, const char* flag_name, const std::string& new_value);

template <class T>
Status SetFlagDefaultAndCurrent(const T* flag_ptr, const char* flag_name, const T& new_value) {
  return SetFlagDefaultAndCurrentInternal(flag_ptr, flag_name, std::to_string(new_value));
}

// Warn if flag associated with flagname has explicit setting in current configuration. Do not use
// this method directly. Instead, use the DEPRECATE_FLAG macro below.
void WarnFlagDeprecated(const std::string& flagname, const std::string& date_mm_yyyy);

YB_STRONGLY_TYPED_BOOL(SetFlagForce);
YB_DEFINE_ENUM(SetFlagResult, (SUCCESS)(NO_SUCH_FLAG)(NOT_SAFE)(BAD_VALUE));

// Set the current value of the flag if it is runtime safe or if SetFlagForce is set. old_value is
// only set on success.
SetFlagResult SetFlag(
    const std::string& flag_name, const std::string& new_value, const SetFlagForce force,
    std::string* old_value, std::string* output_msg);

}  // namespace flags_internal

// In order to mark a flag as deprecated, use this macro:
//   DEPRECATE_FLAG(int32, foo_flag, "10_2022")
// This will print a warning at startup if a process has been configured with the specified flag or
// any time the flags are updated at runtime with this flag.
//
// The third argument is a date in the format of "MM_YYYY" to make it easy to track when a flag was
// deprecated, so we may fully remove declarations in future releases.
// Flags are set to a dummy value
#define DEPRECATE_FLAG(type, name, date_mm_yyyy) \
  namespace deprecated_flag_do_not_use { \
  using std::string; \
  type default_##name; \
  DEFINE_RUNTIME_##type(name, default_##name, "Deprecated"); \
  _TAG_FLAG(name, ::yb::FlagTag::kDeprecated, deprecated); \
  REGISTER_CALLBACK(name, "Warn deprecated flag", []() { \
    yb::flags_internal::WarnFlagDeprecated(#name, date_mm_yyyy); \
  }); \
  } \
  static_assert(true, "semi-colon required after this macro")

// Validate that the given flag is a valid percentage value (0-100).
bool ValidatePercentageFlag(const char* flag_name, int value);

// Check if SetUsageMessage() was called. Useful for tools.
bool IsUsageMessageSet();

} // namespace yb
