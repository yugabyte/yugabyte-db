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

#include "yb/tserver/pg_xcluster_context.h"

#include "yb/util/result.h"
#include "yb/tserver/xcluster_safe_time_map.h"

namespace yb {

Result<std::optional<HybridTime>> PgXClusterContext::GetSafeTime(
    const NamespaceId& namespace_id) const {
  return safe_time_map_.GetSafeTime(namespace_id);
}

bool PgXClusterContext::IsXClusterReadOnlyMode(const NamespaceId namespace_id) const {
  if (!is_xcluster_read_only_mode_.load(std::memory_order_acquire)) {
    return false;
  }

  // Only namespaces that are part of the safe time computation belong to xCluster replication.
  return safe_time_map_.HasNamespace(namespace_id);
}

}  // namespace yb
