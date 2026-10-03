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

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "yb/util/result.h"
#include "yb/util/subprocess.h"

namespace yb {

struct PerfProfilerStopResult {
  std::string collapsed_stacks_name;
  std::string flamegraph;
};

class PerfProfiler {
 public:
  PerfProfiler() = default;
  ~PerfProfiler();

  Status Start(int freq, const std::string& storage_dir);
  Result<PerfProfilerStopResult> Stop();

 private:
  // Held from Start() until Stop() finishes. Every profiler writes the same files under
  // storage_dir, so only one may be active per process.
  std::unique_lock<std::mutex> active_lock_;
  std::unique_ptr<Subprocess> perf_proc_;
  std::string storage_dir_;
};

} // namespace yb
