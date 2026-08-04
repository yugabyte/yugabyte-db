// Copyright (c) YugabyteDB, Inc.
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

#include <boost/container/small_vector.hpp>

#include "yb/gutil/map-util.h"
#include "yb/gutil/singleton.h"

#include "yb/util/flags/flags_callback.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
#include "yb/util/result.h"
#include "yb/util/shared_lock.h"
#include "yb/util/thread.h"

DECLARE_bool(TEST_running_test);

using std::string;

namespace yb {

struct FlagCallbackInfo {
  const void* flag_ptr;
  std::string descriptive_name;
  FlagCallback callback;
};

// Singleton registry storing the list of callbacks for each gFlag.
class FlagsCallbackRegistry {
 public:
  static FlagsCallbackRegistry* GetInstance() { return Singleton<FlagsCallbackRegistry>::get(); }

  Result<FlagCallbackInfoPtr> RegisterCallback(
      const void* flag_ptr, const std::string& descriptive_name, const FlagCallback& callback);

  Status DeregisterCallback(const FlagCallbackInfoPtr& callback_info);

  void InvokeCallbacks(const void* flag_ptr);

 private:
  struct FlagCallbacks {
    std::mutex mutex;
    boost::container::small_vector<FlagCallbackInfoPtr, 2> callbacks GUARDED_BY(mutex);
  };

  std::shared_ptr<FlagCallbacks> GetFlagCallbacks(const void* flag_ptr);

 private:
  friend class Singleton<FlagsCallbackRegistry>;

  FlagsCallbackRegistry() {}

  mutable std::mutex mutex_;
  std::map<const void*, std::shared_ptr<FlagCallbacks>> callback_map_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(FlagsCallbackRegistry);
};

Result<FlagCallbackInfoPtr> FlagsCallbackRegistry::RegisterCallback(
    const void* flag_ptr, const std::string& descriptive_name, const FlagCallback& callback) {
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
    flag_callbacks = callback_map_.try_emplace(flag_ptr, LazySharedPtrFactory()).first->second;
  }

  auto info = std::make_shared<FlagCallbackInfo>(FlagCallbackInfo {
    .flag_ptr = flag_ptr,
    .descriptive_name = *unique_descriptive_name,
    .callback = callback,
  });

  std::lock_guard lock(flag_callbacks->mutex);
  auto it = std::find_if(
      flag_callbacks->callbacks.begin(), flag_callbacks->callbacks.end(),
      [&unique_descriptive_name](const FlagCallbackInfoPtr& flag_info) {
        return flag_info->descriptive_name == *unique_descriptive_name;
      });

  SCHECK(
      it == flag_callbacks->callbacks.end(), AlreadyPresent,
      Format("Callback '$0' already registered", *unique_descriptive_name));

  flag_callbacks->callbacks.push_back(info);
  return info;
}

Status FlagsCallbackRegistry::DeregisterCallback(
    const FlagCallbackInfoPtr& callback_info) {
  auto flag_callbacks = GetFlagCallbacks(callback_info->flag_ptr);
  SCHECK(
      flag_callbacks != nullptr, NotFound,
      Format("Flag for callback '$0' not found", callback_info->descriptive_name));

  std::lock_guard lock(flag_callbacks->mutex);
  auto it = std::find_if(
      flag_callbacks->callbacks.begin(), flag_callbacks->callbacks.end(),
      [&callback_info](const FlagCallbackInfoPtr& cb_info) {
        return cb_info.get() == callback_info.get();
  });

  SCHECK(
      it != flag_callbacks->callbacks.end(), NotFound,
      Format("Callback '$0' not found", callback_info->descriptive_name));

  flag_callbacks->callbacks.erase(it);

  return Status::OK();
}

void FlagsCallbackRegistry::InvokeCallbacks(const void* flag_ptr) {
  auto flag_callbacks = GetFlagCallbacks(flag_ptr);
  if (flag_callbacks == nullptr) {
    return;
  }
  decltype(flag_callbacks->callbacks) callbacks;
  {
    std::lock_guard lock(flag_callbacks->mutex);
    callbacks = flag_callbacks->callbacks;
  }
  for (const auto& cb_info : callbacks) {
    VLOG_WITH_FUNC(1) << ": " << cb_info->descriptive_name;
    cb_info->callback();
  }
}

std::shared_ptr<FlagsCallbackRegistry::FlagCallbacks> FlagsCallbackRegistry::GetFlagCallbacks(
    const void* flag_ptr) {
  std::lock_guard lock(mutex_);
  return FindPtrOrNull(callback_map_, flag_ptr);
}

FlagCallbackRegistration::FlagCallbackRegistration() = default;

FlagCallbackRegistration::FlagCallbackRegistration(const FlagCallbackInfoPtr& callback_info)
    : callback_info_(callback_info) {
  DCHECK_NOTNULL(callback_info.get());
}

FlagCallbackRegistration::~FlagCallbackRegistration() {
  CHECK(!callback_info_) << callback_info_->descriptive_name
                         << " callback was not Deregistered";
}

FlagCallbackRegistration::FlagCallbackRegistration(FlagCallbackRegistration&& other) {
  *this = std::move(other);
}

FlagCallbackRegistration& FlagCallbackRegistration::operator=(FlagCallbackRegistration&& other) {
  if (this != &other) {
    CHECK(!callback_info_) << callback_info_->descriptive_name
                           << " Callback was not Deregistered";

    callback_info_ = other.callback_info_;
    other.callback_info_ = nullptr;
  }
  return *this;
}

void FlagCallbackRegistration::Deregister() {
  if (callback_info_) {
    CHECK_OK(FlagsCallbackRegistry::GetInstance()->DeregisterCallback(callback_info_));
    callback_info_ = nullptr;
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

FlagCallbackInfoPtr RegisterGlobalFlagUpdateCallback(
    const void* flag_ptr, const std::string& descriptive_name, const FlagCallback& callback) {
  return CHECK_RESULT(
      FlagsCallbackRegistry::GetInstance()->RegisterCallback(flag_ptr, descriptive_name, callback));
}
}  // namespace flags_callback_internal
}  // namespace yb
