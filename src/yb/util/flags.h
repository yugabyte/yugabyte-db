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

#include <map>
#include <unordered_map>

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>

#include "yb/util/flags/flag_tags.h"
#include "yb/util/flags/flags_callback.h"
#include "yb/util/flags/auto_flags.h"

// Macro for the registration of a flag validator.
//
// ==== DEFINE_validator ====
// Flag validators enforce that the value assigned to a gFlag satisfy certain conditions. If a new
// value does not pass all the validation functions then it is not assigned.
// Check flag_validators.h for commonly used validation functions.
//
// The validation function should return true if the flag value is valid, and false otherwise. If
// the function returns false for the new value of the flag, the flag will retain its current value.
// If it returns false for the default value, the process will die. Use LOG_FLAG_VALIDATION_ERROR to
// log error messages when returning false.
//
// Validator fuction should be of the form:
// bool ValidatorFunc(const char* flag_name, <flag_type> value);
// for strings the second argument should be `const std::string&`.
//
// Note:
// If the validation depends on the value of other flags then make sure to call
// DELAY_FLAG_VALIDATION_ON_STARTUP macro, so that we do not enforce a restriction on the order of
// flags in command line and flags file.
//
// This macro should be used in the same file that DEFINEs the flag. Using it any other file can
// result in segfault due to indeterminate order of static initialization.
#ifdef DEFINE_validator
#undef DEFINE_validator
#endif
#define VALIDATOR_AND_CALL_HELPER(r, unused, validator) && (validator)(_flag_name, _new_value)
#define DEFINE_validator(name, ...) \
  static_assert( \
    sizeof(_DEFINE_FLAG_IN_FILE(name)), "validator must be DEFINED in the same file as the flag"); \
  static const bool BOOST_PP_CAT(name, _validator_registered) __attribute__((unused)) = \
      google::RegisterFlagValidator(&BOOST_PP_CAT(FLAGS_, name), \
      [](const char* _flag_name, auto _new_value) -> bool { \
        return true BOOST_PP_SEQ_FOR_EACH(VALIDATOR_AND_CALL_HELPER, _, \
                                          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)); \
      })

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

// If the flag is tagged as sensitive_info then returns a masked value('***').
// Only string values are sensitive.
std::string GetMaskedValueIfSensitive(const std::string& flag_name, const std::string& value);
template <typename T>
T GetMaskedValueIfSensitive(const std::string& flag_name, T value) {
  return value;
}

// Check if the flag can be set to the new value. Does not actually set the flag.
Status ValidateFlagValue(const std::string& flag_name, const std::string& value);

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

// GLog sink that keeps an internal buffer of messages that have been logged from DEFINE_validator
// flag macro.
class FlagValidatorSink : public google::LogSink {
 public:
  void send(
      google::LogSeverity severity, const char* full_filename, const char* base_filename, int line,
      const struct ::tm* tm_time, const char* message, size_t message_len) override;

  const std::vector<std::string> GetMessagesAndClear();

 private:
  std::mutex mutex_;
  std::vector<std::string> logged_msgs_ GUARDED_BY(mutex_);
};

FlagValidatorSink& GetFlagValidatorSink();

Result<std::unordered_set<std::string>> GetFlagNamesFromXmlFile(const std::string& flag_file_name);

// Log error message to the error log and the flag validator sink, which will ensure it is sent
// back to the user. Also masks any sensitive values.
#define LOG_FLAG_VALIDATION_ERROR(flag_name, value) \
  LOG_TO_SINK(&yb::GetFlagValidatorSink(), ERROR) \
      << "Invalid value '" << yb::flags_internal::GetMaskedValueIfSensitive(flag_name, value) \
      << "' for flag '" << flag_name << "': "

// Returns true if the flag was recorded for delayed validation, and the validation can be skipped.
bool RecordFlagForDelayedValidation(const std::string& flag_name);

// Some flag validation may depend on the value of another flag. Flags are parsed, validated and set
// in order they are passed in via the command line, or flags file. In order to not impose a
// restriction on the user to pass the flags in a particular obscure order, this macro delays
// the validation until all flags have been set.
#define DELAY_FLAG_VALIDATION_ON_STARTUP(flag_name) \
  do { \
    if (yb::RecordFlagForDelayedValidation(flag_name)) { \
      return true; \
    } \
  } while (false)

YB_DEFINE_ENUM(FlagType, (kInvalid)(kNodeInfo)(kCustom)(kAuto)(kDefault));

struct FlagInfo {
  std::string name;
  std::string value;
  bool is_auto_flag_promoted = false;  // Only set for AutoFlags
};

// Get a user friendly info about all the flags in the system grouped by FlagType.
// auto_flags_filter_func is used to filter only AutoFlags that are relevant to this this process.
// AutoFlags for which this function returns false are treated as kDefault type.
// Flags of type kDefault are not part of the result if default_flags_filter returns false.
// default_flags_filter and custom_varz are optional.
std::unordered_map<FlagType, std::vector<FlagInfo>> GetFlagInfos(
    std::function<bool(const std::string&)> auto_flags_filter,
    std::function<bool(const std::string&)> default_flags_filter,
    const std::map<std::string, std::string>& custom_varz, bool mask_value_if_private = false);

} // namespace yb
