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
#include "yb/util/auto_flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags_callback.h"

// Redefine the macro from gflags.h with an unused attribute.
#ifdef DEFINE_validator
#undef DEFINE_validator
#endif
#define DEFINE_validator(name, validator) \
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

YB_STRONGLY_TYPED_BOOL(SetFlagForce);
YB_DEFINE_ENUM(SetFlagResult, (SUCCESS)(NO_SUCH_FLAG)(NOT_SAFE)(BAD_VALUE));

// Set the current value of the flag if it is runtime safe or if SetFlagForce is set. old_value is
// only set on success.
SetFlagResult SetFlag(
    const std::string& flag_name, const std::string& new_value, const SetFlagForce force,
    std::string* old_value, std::string* output_msg);

}  // namespace flags_internal

} // namespace yb
