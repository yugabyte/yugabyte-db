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

#include <optional>
#include <unordered_map>

#include "yb/common/entity_ids_types.h"
#include "yb/common/pg_types.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_safe_time_map.h"
#include "yb/util/status_fwd.h"

namespace yb {
class HybridTime;
class XClusterSafeTimeMap;

namespace tserver {
class TserverXClusterContext : public TserverXClusterContextIf {
 public:
  TserverXClusterContext() {}

  Result<std::optional<HybridTime>> GetSafeTime(const NamespaceId& namespace_id) const override;

  bool IsReadOnlyMode(const NamespaceId namespace_id) const override;

  bool SafeTimeComputationRequired() const override;
  bool SafeTimeComputationRequired(const NamespaceId namespace_id) const override;

  void UpdateSafeTime(const XClusterNamespaceToSafeTimePBMap& safe_time_map);

  Status SetSourceTableMappingForCreateTable(
      const YsqlFullTableName& table_name, const PgObjectId& source_table_id) override
      EXCLUDES(source_table_id_for_create_table_map_mutex_);
  void ClearSourceTableMappingForCreateTable(const YsqlFullTableName& table_name) override
      EXCLUDES(source_table_id_for_create_table_map_mutex_);
  // Returns an invalid PgObjectId if the table is not found.
  PgObjectId GetXClusterSourceTableId(const YsqlFullTableName& table_name) const override
      EXCLUDES(source_table_id_for_create_table_map_mutex_);

 private:
  XClusterSafeTimeMap safe_time_map_;

  mutable rw_spinlock source_table_id_for_create_table_map_mutex_;
  std::unordered_map<YsqlFullTableName, PgObjectId, YsqlFullTableNameHash>
      source_table_id_for_create_table_map_ GUARDED_BY(source_table_id_for_create_table_map_mutex_);
};

}  // namespace tserver
}  // namespace yb
