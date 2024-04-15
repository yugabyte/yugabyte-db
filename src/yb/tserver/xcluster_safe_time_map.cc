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

#include "yb/gutil/map-util.h"
#include "yb/tserver/xcluster_safe_time_map.h"
#include "yb/util/shared_lock.h"
#include "yb/util/result.h"

namespace yb {
XClusterSafeTimeMap::XClusterSafeTimeMap() : map_initialized_(false) {}

bool XClusterSafeTimeMap::HasNamespace(const NamespaceId& namespace_id) const {
  if (empty()) {
    return false;
  }

  SharedLock l(xcluster_safe_time_map_mutex_);
  return ContainsKey(xcluster_safe_time_map_, namespace_id);
}

Result<std::optional<HybridTime>> XClusterSafeTimeMap::GetSafeTime(
    const NamespaceId& namespace_id) const {
  SharedLock l(xcluster_safe_time_map_mutex_);
  SCHECK(map_initialized_, TryAgain, "XCluster safe time not yet initialized");
  auto* safe_time = FindOrNull(xcluster_safe_time_map_, namespace_id);
  if (!safe_time) {
    return std::nullopt;
  }

  HybridTime safe_ht = HybridTime(*safe_time);

  if (safe_ht.is_special()) {
    return STATUS(
        TryAgain, Format("XCluster safe time not yet initialized for namespace $0", namespace_id));
  }

  return safe_ht;
}

void XClusterSafeTimeMap::Update(XClusterNamespaceToSafeTimePBMap safe_time_map) {
  std::lock_guard l(xcluster_safe_time_map_mutex_);
  map_initialized_ = true;
  xcluster_safe_time_map_ = std::move(safe_time_map);
  empty_.store(xcluster_safe_time_map_.empty(), std::memory_order_release);
}
}  // namespace yb
