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

#include <mutex>
#include <condition_variable>
#include "yb/gutil/ref_counted.h"
#include "yb/util/status.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {

class Thread;

class LlvmProfileDumper {
 public:
  LlvmProfileDumper() = default;
  ~LlvmProfileDumper();
  Status Start() EXCLUDES(mutex_);

 private:
  void ProfileDumpLoop() EXCLUDES(mutex_);

 private:
  std::mutex mutex_;
  bool stop_ GUARDED_BY(mutex_) = false;
  std::condition_variable cond_variable_;
  scoped_refptr<Thread> thread_;
};
}  // namespace yb
