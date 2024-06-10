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

#include <unordered_map>

#include <boost/optional/optional.hpp>

#include "yb/gutil/ref_counted.h"

#include "yb/master/master_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

namespace master {

// Maps tablespace id -> placement policies.
typedef std::unordered_map<TablespaceId, boost::optional<ReplicationInfoPB>>
    TablespaceIdToReplicationInfoMap;

// Maps table id -> tablespace id.
typedef std::unordered_map<TableId, boost::optional<TablespaceId>> TableToTablespaceIdMap;

// Number of default tablespaces created by PG upon startup. Postgres creates 'pg_default'
// which is the default tablespace associated with tables/indexes unless the user explicitly
// specifies a custom tablespace. 'pg_global' is used for shared objects like certain system
// catalog tables. Any database objects associated with these two tablespaces will be stored
// based on the replication info specified in the cluster_config.
static const int kYsqlNumDefaultTablespaces = 2;

// This class is a container for the result of the CatalogManager Tablespace background task.
// Every time the task runs, its results are stored in a new instance of YsqlTablespaceManager.
// These results basically comprise two maps -> 1. table_id->tablespace_id
// 2. tablespace_id->replication_info. These maps are then used to find the replication info
// for a table, given its table_id.
class YsqlTablespaceManager {
 public:
  YsqlTablespaceManager(std::shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_map,
                        std::shared_ptr<TableToTablespaceIdMap> table_to_tablespace_map);

  std::shared_ptr<YsqlTablespaceManager>
  CreateCloneWithTablespaceMap(std::shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_map);

  Result<boost::optional<ReplicationInfoPB>> GetTablespaceReplicationInfo(
    const TablespaceId& tablespace_id);

  Result<boost::optional<TablespaceId>> GetTablespaceForTable(
      const scoped_refptr<const TableInfo>& table) const;

  Result<boost::optional<ReplicationInfoPB>> GetTableReplicationInfo(
    const scoped_refptr<const TableInfo>& table) const;

  // Indicates whether we need to wait for the next run of the tablespace background task to know
  // the tablespace information for a table.
  bool NeedsRefreshToFindTablePlacement(const scoped_refptr<TableInfo>& table);

 private:
  // By default we have 2 tablespaces in the system, pg_default and pg_global. Indicates whether
  // there are any other user created custom tablespaces in the database.
  bool ContainsCustomTablespaces() const;

 private:
  // Map to provide the replication info associated with a tablespace.
  std::shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_id_to_replication_info_map_;

  // Map to provide the tablespace associated with a given table.
  std::shared_ptr<TableToTablespaceIdMap> table_to_tablespace_map_;
};

}  // namespace master
}  // namespace yb
