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
#include "yb/util/flags/flags_callback.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"
#include "yb/util/result.h"
#include "yb/util/thread.h"

DECLARE_bool(TEST_running_test);

using std::string;

namespace yb {
class FlagCallbackInfo {
 public:
  FlagCallbackInfo(const void* flag_ptr, std::string descriptive_name, FlagCallback callback)
      : flag_ptr_(flag_ptr), descriptive_name_(descriptive_name), callback_(callback) {}

  const void* flag_ptr_;
  std::string descriptive_name_;
  FlagCallback callback_;
};

// Singleton registry storing the list of callbacks for each gFlag.
class FlagsCallbackRegistry {
 public:
  static FlagsCallbackRegistry* GetInstance() { return Singleton<FlagsCallbackRegistry>::get(); }

  Result<std::shared_ptr<FlagCallbackInfo>> RegisterCallback(
      const void* flag_ptr, const string& descriptive_name, const FlagCallback callback);

  Status DeregisterCallback(const std::shared_ptr<FlagCallbackInfo>& callback_info);

  void InvokeCallbacks(const void* flag_ptr);

 private:
  struct FlagCallbacks {
    std::mutex mutex;
    std::vector<std::shared_ptr<FlagCallbackInfo>> callbacks GUARDED_BY(mutex);
  };

  std::shared_ptr<FlagCallbacks> GetFlagCallbacks(const void* flag_ptr);

 private:
  friend class Singleton<FlagsCallbackRegistry>;

  FlagsCallbackRegistry() {}

  mutable std::mutex mutex_;
  std::map<const void*, std::shared_ptr<FlagCallbacks>> callback_map_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(FlagsCallbackRegistry);
};

Result<std::shared_ptr<FlagCallbackInfo>> FlagsCallbackRegistry::RegisterCallback(
    const void* flag_ptr, const string& descriptive_name, FlagCallback callback) {
  auto* unique_descriptive_name = &descriptive_name;
  string test_descriptive_name;
  if (FLAGS_TEST_running_test) {
    // Certain dynamically registered callbacks like ReloadPgConfig in pg_supervisor use constant
    // string name as they are expected to be singleton per process. But in MiniClusterTests
    // multiple YB masters and tservers will register for callbacks with same name in one test
    // process. Prefix the names of these callbacks with the yb process names to make them unique.
    test_descriptive_name = Format("$0$1", TEST_GetThreadLogPrefix(), descriptive_name);
    unique_descriptive_name = &test_descriptive_name;
  }

  std::shared_ptr<FlagCallbacks> flag_callbacks;
  {
    std::lock_guard lock(mutex_);

    if (!callback_map_.contains(flag_ptr)) {
      callback_map_[flag_ptr] = std::make_shared<FlagCallbacks>();
    }
    flag_callbacks = callback_map_[flag_ptr];
  }

  auto info = std::make_shared<FlagCallbackInfo>(flag_ptr, *unique_descriptive_name, callback);

  std::lock_guard lock(flag_callbacks->mutex);
  auto it = std::find_if(
      flag_callbacks->callbacks.begin(), flag_callbacks->callbacks.end(),
      [&unique_descriptive_name](const std::shared_ptr<FlagCallbackInfo>& flag_info) {
        return flag_info->descriptive_name_ == *unique_descriptive_name;
      });

  SCHECK(
      it == flag_callbacks->callbacks.end(), AlreadyPresent,
      Format("Callback '$0' already registered", *unique_descriptive_name));

  flag_callbacks->callbacks.push_back(info);
  return info;
}

Status FlagsCallbackRegistry::DeregisterCallback(
    const std::shared_ptr<FlagCallbackInfo>& callback_info) {
  auto flag_callbacks = GetFlagCallbacks(callback_info->flag_ptr_);
  SCHECK(
      flag_callbacks != nullptr, NotFound,
      Format("Flag for callback '$0' not found", callback_info->descriptive_name_));

  std::lock_guard lock(flag_callbacks->mutex);
  auto count_erased = std::erase_if(
      flag_callbacks->callbacks, [callback_info](const std::shared_ptr<FlagCallbackInfo>& cb_info) {
        return cb_info.get() == callback_info.get();
      });

  SCHECK_GT(
      count_erased, 0, NotFound,
      Format("Callback '$0' not found", callback_info->descriptive_name_));

  return Status::OK();
}

void FlagsCallbackRegistry::InvokeCallbacks(const void* flag_ptr) {
  auto flag_callbacks = GetFlagCallbacks(flag_ptr);

  if (flag_callbacks != nullptr) {
    std::lock_guard lock(flag_callbacks->mutex);
    for (auto& cb_info : flag_callbacks->callbacks) {
      VLOG_WITH_FUNC(1) << ": " << cb_info->descriptive_name_;
      cb_info->callback_();
    }
  }
}

std::shared_ptr<FlagsCallbackRegistry::FlagCallbacks> FlagsCallbackRegistry::GetFlagCallbacks(
    const void* flag_ptr) {
  std::lock_guard lock(mutex_);
  if (callback_map_.contains(flag_ptr)) {
    return callback_map_[flag_ptr];
  }

  return nullptr;
}

FlagCallbackRegistration::FlagCallbackRegistration() : callback_registered_(false) {}

FlagCallbackRegistration::FlagCallbackRegistration(
    const std::shared_ptr<FlagCallbackInfo>& callback_info)
    : callback_info_(callback_info), callback_registered_(true) {
  DCHECK_NOTNULL(callback_info.get());
}

FlagCallbackRegistration::~FlagCallbackRegistration() {
  CHECK(!callback_registered_) << callback_info_->descriptive_name_
                               << " Callback was not Deregistered";
}

FlagCallbackRegistration::FlagCallbackRegistration(FlagCallbackRegistration&& other)
    : callback_registered_(false) {
  *this = std::move(other);
}

FlagCallbackRegistration& FlagCallbackRegistration::operator=(FlagCallbackRegistration&& other) {
  if (this != &other) {
    CHECK(!callback_registered_) << callback_info_->descriptive_name_
                                 << " Callback was not Deregistered";

    callback_info_ = other.callback_info_;
    callback_registered_ = other.callback_registered_;

    other.callback_info_ = nullptr;
    other.callback_registered_ = false;
  }
  return *this;
}

void FlagCallbackRegistration::Deregister() {
  if (callback_registered_) {
    CHECK_OK(FlagsCallbackRegistry::GetInstance()->DeregisterCallback(callback_info_));
    callback_registered_ = false;
  }
}

Result<FlagCallbackRegistration> RegisterFlagUpdateCallback(
    const void* flag_ptr, const string& descriptive_name, FlagCallback callback) {
  auto callback_info = VERIFY_RESULT(
      FlagsCallbackRegistry::GetInstance()->RegisterCallback(flag_ptr, descriptive_name, callback));
  return FlagCallbackRegistration(callback_info);
}

namespace flags_callback_internal {
void InvokeCallbacks(const void* flag_ptr, const std::string& flag_name) {
  VLOG_WITH_FUNC(1) << flag_name << ": Start";
  FlagsCallbackRegistry::GetInstance()->InvokeCallbacks(flag_ptr);
  VLOG_WITH_FUNC(1) << flag_name << ": End";
}

std::shared_ptr<FlagCallbackInfo> RegisterGlobalFlagUpdateCallback(
    const void* flag_ptr, const string& descriptive_name, FlagCallback callback) {
  return CHECK_RESULT(
      FlagsCallbackRegistry::GetInstance()->RegisterCallback(flag_ptr, descriptive_name, callback));
}
}  // namespace flags_callback_internal
}  // namespace yb
