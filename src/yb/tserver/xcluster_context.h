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

#include <atomic>
#include <functional>

#include "yb/tserver/xcluster_safe_time_map.h"

namespace yb {

class XClusterContext {
 public:
  XClusterContext(
      std::reference_wrapper<const XClusterSafeTimeMap> safe_time_map,
      std::reference_wrapper<const std::atomic<bool>> is_xcluster_read_only_mode)
      : safe_time_map_(safe_time_map), is_xcluster_read_only_mode_(is_xcluster_read_only_mode) {}

  [[nodiscard]] const XClusterSafeTimeMap& safe_time_map() const { return safe_time_map_; }

  [[nodiscard]] bool is_xcluster_read_only_mode() const {
    return is_xcluster_read_only_mode_.load(std::memory_order_acquire);
  }

 private:
  const XClusterSafeTimeMap& safe_time_map_;
  const std::atomic<bool>& is_xcluster_read_only_mode_;
};

}  // namespace yb
