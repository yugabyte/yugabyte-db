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

  bool IsReadOnlyMode(const NamespaceId& namespace_id) const override;

  bool SafeTimeComputationRequired() const override;
  bool SafeTimeComputationRequired(const NamespaceId& namespace_id) const override;

  void UpdateSafeTimeMap(const XClusterNamespaceToSafeTimePBMap& safe_time_map);

  Status SetSourceTableInfoMappingForCreateTable(
      const YsqlFullTableName& table_name, const PgObjectId& source_table_id,
      ColocationId colocation_id) override EXCLUDES(table_map_mutex_);
  void ClearSourceTableInfoMappingForCreateTable(const YsqlFullTableName& table_name) override
      EXCLUDES(table_map_mutex_);

  void PrepareCreateTableHelper(
      const PgCreateTableRequestPB& req, PgCreateTable& helper) const override;

 private:
  XClusterSafeTimeMap safe_time_map_;

  struct CreateTableInfo {
    PgObjectId source_table_id;
    ColocationId colocation_id;

    std::string ToString() const { return YB_STRUCT_TO_STRING(source_table_id, colocation_id); }
  };

  mutable rw_spinlock table_map_mutex_;
  std::unordered_map<YsqlFullTableName, CreateTableInfo, YsqlFullTableNameHash>
      create_table_info_map_ GUARDED_BY(table_map_mutex_);
};

}  // namespace tserver
}  // namespace yb
