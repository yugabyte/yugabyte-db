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

#include "yb/common/llvm_profile_dumper.h"

#include <chrono>
#include <functional>
#include "yb/util/thread.h"

#if defined(YB_PROFGEN) && defined(__clang__)
#define LLVM_PROFILE_DUMPER_ENABLED
#endif

#if defined(LLVM_PROFILE_DUMPER_ENABLED)
extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char *);
extern "C" void __llvm_profile_reset_counters();
#endif

using namespace std::chrono_literals;

namespace yb {
LlvmProfileDumper::~LlvmProfileDumper() {
  if (thread_) {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    cond_variable_.notify_all();
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
  }
}

Status LlvmProfileDumper::Start() {
#if defined(LLVM_PROFILE_DUMPER_ENABLED)
  std::lock_guard lock(mutex_);
  if (thread_) {
    return Status::OK();
  }
  return Thread::Create(
      "llvm_profile_dumper", "loop", &LlvmProfileDumper::ProfileDumpLoop, this, &thread_);
#else
  return Status::OK();
#endif
}

void LlvmProfileDumper::ProfileDumpLoop() {
#if defined(LLVM_PROFILE_DUMPER_ENABLED)
  __llvm_profile_set_filename("tserver-%p-%m.profraw");
  while (true) {
    __llvm_profile_write_file();
    __llvm_profile_reset_counters();

    std::unique_lock lock(mutex_);
    if (cond_variable_.wait_for(lock, 60s, [this]() REQUIRES(mutex_) { return stop_; })) {
      return;
    }
  }
#endif
}
}  // namespace yb
