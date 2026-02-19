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

#include "yb/tserver/tserver_xcluster_context.h"

#include "yb/common/pg_types.h"
#include "yb/gutil/map-util.h"
#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_create_table.h"
#include "yb/tserver/xcluster_safe_time_map.h"
#include "yb/util/result.h"
#include "yb/util/shared_lock.h"

namespace yb::tserver {

Result<std::optional<HybridTime>> TserverXClusterContext::GetSafeTime(
    NamespaceIdView namespace_id) const {
  return safe_time_map_.GetSafeTime(namespace_id);
}

XClusterNamespaceInfoPB_XClusterRole TserverXClusterContext::GetXClusterRole(
    NamespaceIdView namespace_id) const {
  SharedLock lock(mutex_);
  if (!have_received_a_heartbeat_) {
    return XClusterNamespaceInfoPB_XClusterRole_UNAVAILABLE;
  }
  if (auto* xcluster_info_per_namespace = FindOrNull(xcluster_info_per_namespace_, namespace_id)) {
    return xcluster_info_per_namespace->role();
  } else {
    return XClusterNamespaceInfoPB_XClusterRole_NOT_AUTOMATIC_MODE;
  }
}

bool TserverXClusterContext::IsReadOnlyMode(NamespaceIdView namespace_id) const {
  // Namespaces that are part of the safe time belong to an inbound transactional xCluster
  // replication.
  return safe_time_map_.HasNamespace(namespace_id);
}

bool TserverXClusterContext::IsTargetAndInAutomaticMode(const NamespaceId& namespace_id) const {
  SharedLock lock(mutex_);
  return target_namespaces_in_automatic_mode_.contains(namespace_id);
}

void TserverXClusterContext::UpdateSafeTimeMap(
    const XClusterNamespaceToSafeTimePBMap& safe_time_map) {
  safe_time_map_.Update(safe_time_map);
}

void TserverXClusterContext::UpdateXClusterInfoPerNamespace(
    const ::google::protobuf::Map<std::string, XClusterNamespaceInfoPB>&
        xcluster_info_per_namespace) {
  std::lock_guard lock(mutex_);
  have_received_a_heartbeat_ = true;
  xcluster_info_per_namespace_.clear();
  for (const auto& [namespace_id, info] : xcluster_info_per_namespace) {
    xcluster_info_per_namespace_[namespace_id] = info;
  }
}

void TserverXClusterContext::UpdateTargetNamespacesInAutomaticModeSet(
    const std::unordered_set<NamespaceId>& target_namespaces_in_automatic_mode) {
  std::lock_guard lock(mutex_);
  target_namespaces_in_automatic_mode_ = target_namespaces_in_automatic_mode;
}

bool TserverXClusterContext::SafeTimeComputationRequired() const {
  // If we have any namespaces with safe times, then we need to compute safe time.
  return !safe_time_map_.empty();
}

bool TserverXClusterContext::SafeTimeComputationRequired(const NamespaceId& namespace_id) const {
  return safe_time_map_.HasNamespace(namespace_id);
}

Status TserverXClusterContext::SetSourceTableInfoMappingForCreateTable(
    const YsqlFullTableName& table_name, const PgObjectId& source_table_id,
    ColocationId colocation_id, const HybridTime& backfill_time_opt) {
  CreateTableInfo new_create_table_info{
      .source_table_id = source_table_id,
      .colocation_id = colocation_id,
      .backfill_time_opt = backfill_time_opt};

  std::lock_guard l(table_map_mutex_);
  SCHECK(
      !create_table_info_map_.contains(table_name), IllegalState,
      "Table $0 already has entry in mapping, existing entry: $1, new entry: $2", table_name,
      create_table_info_map_[table_name].ToString(), new_create_table_info.ToString());

  create_table_info_map_[table_name] = std::move(new_create_table_info);

  return Status::OK();
}

void TserverXClusterContext::ClearSourceTableInfoMappingForCreateTable(
    const YsqlFullTableName& table_name) {
  std::lock_guard l(table_map_mutex_);
  create_table_info_map_.erase(table_name);
}

void TserverXClusterContext::PrepareCreateTableHelper(
    const PgCreateTableRequestPB& req, PgCreateTable& helper) const {
  SharedLock l(table_map_mutex_);
  auto create_table_info = FindOrNull(
      create_table_info_map_,
      YsqlFullTableName{req.database_name(), req.schema_name(), req.table_name()});
  if (!create_table_info) {
    return;
  }

  // Force the same colocation id as on the source.
  if (create_table_info->colocation_id != kColocationIdNotSet) {
    helper.OverwriteColocationId(create_table_info->colocation_id);
  }

  // Set the matching source table id. Also marks the table as an automatic mode xCluster table.
  if (create_table_info->source_table_id.IsValid()) {
    helper.SetXClusterSourceTableId(create_table_info->source_table_id);
  }

  if (create_table_info->backfill_time_opt.is_valid()) {
    auto indexed_table = PgObjectId::FromPB(req.base_table_id());
    if (indexed_table.IsValid()) {
      helper.SetXClusterBackfillHybridTime(create_table_info->backfill_time_opt.ToUint64());
    } else {
      LOG(DFATAL) << "XCluster backfill time is set for non-index create DDL "
                  << req.ShortDebugString();
    }
  }
}

}  // namespace yb::tserver
