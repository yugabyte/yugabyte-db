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

#include "yb/common/entity_ids_types.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/sys_catalog_types.h"
#include "yb/util/status_fwd.h"

namespace yb {

class VersionInfoPB;

namespace master {

class YsqlCatalogConfig;
struct PersistentTableInfo;

class YsqlManagerIf {
 public:
  virtual ~YsqlManagerIf() = default;

  virtual YsqlCatalogConfig& GetYsqlCatalogConfig() = 0;
  virtual const YsqlCatalogConfig& GetYsqlCatalogConfig() const = 0;

  virtual Result<uint64_t> IncrementYsqlCatalogVersion(const LeaderEpoch& epoch) = 0;

  virtual bool IsTransactionalSysCatalogEnabled() const = 0;
  virtual Status SetTransactionalSysCatalogEnabled(const LeaderEpoch& epoch) = 0;

  virtual Result<TableId> GetVersionSpecificCatalogTableId(
      const TableId& current_table_id) const = 0;

  virtual bool IsMajorUpgradeInProgress() const = 0;

  virtual Status ValidateTServerVersion(const VersionInfoPB& version) const = 0;

  virtual Result<std::string> GetCachedPgSchemaName(
      const PgTableAllOids& oids, PgDbRelNamespaceMap& cache) const = 0;
  virtual Result<std::string> GetPgSchemaName(
      const PgTableAllOids& oids, const ReadHybridTime& read_time = ReadHybridTime()) const = 0;

  // Returns a boolean pg_index status column (e.g. indislive/indisready/indisvalid) for the
  // given PG OIDs.
  virtual Result<bool> GetPgIndexStatus(
      PgOid database_oid, PgOid index_oid, const std::string& status_col_name,
      const ReadHybridTime& read_time = ReadHybridTime()) const = 0;
};

}  // namespace master
}  // namespace yb
