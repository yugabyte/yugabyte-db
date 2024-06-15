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

#include <functional>
#include <memory>
#include <string>

#include <gflags/gflags.h>

namespace yb {
using FlagCallback = std::function<void(void)>;

class FlagCallbackInfo;
template<class T>
class Result;

class FlagCallbackRegistration {
 public:
  FlagCallbackRegistration();
  explicit FlagCallbackRegistration(const std::shared_ptr<FlagCallbackInfo>& callback_info);
  ~FlagCallbackRegistration();
  FlagCallbackRegistration(FlagCallbackRegistration&& other);
  FlagCallbackRegistration& operator=(FlagCallbackRegistration&& other);

  void Deregister();

 private:
  friend class FlagsCallbackRegistry;
  std::shared_ptr<FlagCallbackInfo> callback_info_;
  bool callback_registered_;
};

// Register a callback which will be invoked when the value of the gFlag changes.
// Each flag can have multiple callbacks.
// The descriptive name of the callback must be unique per flag.
// Callbacks may be invoked even when the flag is set to the same value.
// All callbacks are invoked once during startup at time of gFlag initialization.
// If successfully registered then Deregister must be called on FlagCallbackRegistration before any
// object that the callback depends on gets destroyed.
Result<FlagCallbackRegistration> RegisterFlagUpdateCallback(
    const void* flag_ptr, const std::string& descriptive_name, FlagCallback callback);

// Same as above. Macro to register a callback at global construction time.
// Since this is used at static initialization time, the descriptive_name and callback construction
// (ex: using std::bind) must not depend on any other static objects. The callback itself is invoked
// only at runtime so is safe to access other static objects.
// This is never Deregistered so all dependencies must outlive the program.
// Note: This macro should be used in the same file that DEFINEs the flag. Using it any other file
// can result in segfault due to indeterminate order of static initialization.
#define REGISTER_CALLBACK(flag_name, descriptive_name, callback) \
  static_assert( \
      sizeof(_DEFINE_FLAG_IN_FILE(flag_name)), "callback must be DEFINED in the same file as the flag"); \
  namespace { \
  static const std::shared_ptr<yb::FlagCallbackInfo> BOOST_PP_CAT( \
      flag_name, _global_callback_registered) \
      __attribute__((unused)) = yb::flags_callback_internal::RegisterGlobalFlagUpdateCallback( \
          &BOOST_PP_CAT(FLAGS_, flag_name), descriptive_name, callback); \
  }

namespace flags_callback_internal {
// Don't use this, use REGISTER_CALLBACK instead.
// A variation of RegisterFlagUpdateCallback which does not allow any failures. If a failure does
// occur the process will crash.
std::shared_ptr<FlagCallbackInfo> RegisterGlobalFlagUpdateCallback(
    const void* flag_ptr, const std::string& descriptive_name, FlagCallback callback);

void InvokeCallbacks(const void* flag_ptr, const std::string& flag_name);
}  // namespace flags_callback_internal
}  // namespace yb
