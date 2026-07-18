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

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/util/status_fwd.h"

namespace yb {
class HybridTime;
struct PgObjectId;
struct YsqlFullTableName;

namespace tserver {
class PgCreateTable;
class PgCreateTableRequestPB;

class TserverXClusterContextIf {
 public:
  TserverXClusterContextIf() = default;
  virtual ~TserverXClusterContextIf() = default;

  virtual Result<std::optional<HybridTime>> GetSafeTime(NamespaceIdView namespace_id) const = 0;

  virtual XClusterNamespaceInfoPB_XClusterRole GetXClusterRole(
      NamespaceIdView namespace_id) const = 0;

  virtual bool IsReadOnlyMode(NamespaceIdView namespace_id) const = 0;
  virtual bool IsTargetAndInAutomaticMode(const NamespaceId& namespace_id) const = 0;

  virtual bool SafeTimeComputationRequired() const = 0;
  virtual bool SafeTimeComputationRequired(const NamespaceId& namespace_id) const = 0;

  virtual void UpdateTargetNamespacesInAutomaticModeSet(
      const std::unordered_set<NamespaceId>& target_namespaces_in_automatic_mode) = 0;

  virtual Status SetSourceTableInfoMappingForCreateTable(
      const YsqlFullTableName& table_name, const PgObjectId& source_table_id,
      ColocationId colocation_id, const HybridTime& backfill_time_opt) = 0;
  virtual void ClearSourceTableInfoMappingForCreateTable(const YsqlFullTableName& table_name) = 0;

  virtual void PrepareCreateTableHelper(
      const PgCreateTableRequestPB& req, PgCreateTable& helper) const = 0;
};

}  // namespace tserver
}  // namespace yb
