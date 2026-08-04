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
#include <shared_mutex>
#include <unordered_map>

#include "yb/common/common_types.pb.h"  // gcc needs for std::unordered_map XClusterNamespaceInfoPB
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

  Result<std::optional<HybridTime>> GetSafeTime(NamespaceIdView namespace_id) const override;

  XClusterNamespaceInfoPB_XClusterRole GetXClusterRole(
      NamespaceIdView namespace_id) const override EXCLUDES(mutex_);

  bool IsReadOnlyMode(NamespaceIdView namespace_id) const override;
  bool IsTargetAndInAutomaticMode(const NamespaceId& namespace_id) const override EXCLUDES(mutex_);

  bool SafeTimeComputationRequired() const override;
  bool SafeTimeComputationRequired(const NamespaceId& namespace_id) const override;

  void UpdateSafeTimeMap(const XClusterNamespaceToSafeTimePBMap& safe_time_map);

  void UpdateXClusterInfoPerNamespace(
      const ::google::protobuf::Map<std::string, XClusterNamespaceInfoPB>&
          automatic_mode_replication_state_per_namespace) EXCLUDES(mutex_);

  void UpdateTargetNamespacesInAutomaticModeSet(
      const std::unordered_set<NamespaceId>& target_namespaces_in_automatic_mode) override
      EXCLUDES(mutex_);

  Status SetSourceTableInfoMappingForCreateTable(
      const YsqlFullTableName& table_name, const PgObjectId& source_table_id,
      ColocationId colocation_id, const HybridTime& backfill_time) override
      EXCLUDES(table_map_mutex_);
  void ClearSourceTableInfoMappingForCreateTable(const YsqlFullTableName& table_name) override
      EXCLUDES(table_map_mutex_);

  void PrepareCreateTableHelper(
      const PgCreateTableRequestPB& req, PgCreateTable& helper) const override;

 private:
  XClusterSafeTimeMap safe_time_map_;

  mutable std::shared_mutex mutex_;
  bool have_received_a_heartbeat_ GUARDED_BY(mutex_) = false;
  // The set of namespaces that for this universe are targets of xCluster automatic mode
  // replication.
  std::unordered_set<NamespaceId> target_namespaces_in_automatic_mode_ GUARDED_BY(mutex_);

  UnorderedStringMap<NamespaceId, XClusterNamespaceInfoPB> xcluster_info_per_namespace_
      GUARDED_BY(mutex_);

  struct CreateTableInfo {
    PgObjectId source_table_id;
    ColocationId colocation_id;
    HybridTime backfill_time_opt;  // Only set for colocated index creations.

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(source_table_id, colocation_id, backfill_time_opt);
    }
  };

  mutable rw_spinlock table_map_mutex_;
  std::unordered_map<YsqlFullTableName, CreateTableInfo, YsqlFullTableNameHash>
      create_table_info_map_ GUARDED_BY(table_map_mutex_);
};

}  // namespace tserver
}  // namespace yb
