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

#include <optional>
#include <string>

#include <google/protobuf/map.h>
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/util/locks.h"

namespace yb {

// Map[NamespaceId]:xClusterSafeTime
typedef google::protobuf::Map<std::string, google::protobuf::uint64>
    XClusterNamespaceToSafeTimePBMap;

class XClusterSafeTimeMap {
 public:
  XClusterSafeTimeMap();

  // Gets the xcluster safe time for the given namespace. If the namespace does not have a xcluster
  // safe time, either because it is not part of replication or because the cluster is in ACTIVE
  // role, then this will return nullopt. System namespace will always return nullopt.
  Result<std::optional<HybridTime>> GetSafeTime(const NamespaceId& namespace_id) const
      EXCLUDES(xcluster_safe_time_map_mutex_);

  bool HasNamespace(const NamespaceId& namespace_id) const;

  void Update(XClusterNamespaceToSafeTimePBMap safe_time_map)
      EXCLUDES(xcluster_safe_time_map_mutex_);

 private:
  mutable rw_spinlock xcluster_safe_time_map_mutex_;
  bool map_initialized_ GUARDED_BY(xcluster_safe_time_map_mutex_);
  XClusterNamespaceToSafeTimePBMap xcluster_safe_time_map_
      GUARDED_BY(xcluster_safe_time_map_mutex_);
};

}  // namespace yb
