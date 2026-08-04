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

#include "yb/cdc/xcluster_types.h"

namespace yb::xcluster {

ReplicationGroupId GetAlterReplicationGroupId(const ReplicationGroupId& replication_group_id);

bool IsAlterReplicationGroupId(const ReplicationGroupId& replication_group_id);

ReplicationGroupId GetOriginalReplicationGroupId(const ReplicationGroupId& replication_group_id);

std::string ShortReplicationType(XClusterReplicationType type);


TableId GetSequencesDataAliasForNamespace(const NamespaceId& namespace_id);

bool IsSequencesDataAlias(const TableId& table_id);

TableId StripSequencesDataAliasIfPresent(const TableId& table_id);

// Returns the namespace a sequences_data alias is for; if table_id is not a sequences_data alias,
// instead returns the ID's namespace as usual.
Result<NamespaceId> GetReplicationNamespaceBelongsTo(const TableId& table_id);

// The primary key used to access safe time information in the safe time table.
class SafeTimeTablePK {
 public:
  static Result<SafeTimeTablePK> FromProducerTabletInfo(const ProducerTabletInfo& info);

  static Result<SafeTimeTablePK> FromProducerTabletInfo(
      const xcluster::ReplicationGroupId& replication_group_id,
      const std::string& producer_tablet_id, const std::string& producer_table_id);

  static Result<SafeTimeTablePK> FromSafeTimeTableRow(
      const xcluster::ReplicationGroupId& replication_group_id_column_value,
      const std::string& tablet_id_column_value);

  // These are what we store in the safe time table's primary key columns.
  xcluster::ReplicationGroupId replication_group_id_column_value() const;
  std::string tablet_id_column_value() const;

  // These are the actual underlying values.
  xcluster::ReplicationGroupId replication_group_id() const;
  TabletId tablet_id() const;
  // Only tests should access this information directly.
  NamespaceId TEST_sequences_data_namespace_id() const;

  bool operator==(const SafeTimeTablePK& rhs) const;

  bool operator<(const SafeTimeTablePK& rhs) const;

  std::string ToString() const;

 private:
  xcluster::ReplicationGroupId replication_group_id_;
  TabletId tablet_id_;
  // If this is a sequences_data tablet, then this holds its producer tablet
  // ID sequence alias's replication namespace.  Otherwise holds the empty string.
  //
  // We add this to the primary key to distinguish sequence_data tablets belonging to different
  // streams in a backward-compatible way: we want to still be able to decode old primary keys
  // (which will not involve sequences_data tablets).
  NamespaceId sequences_data_namespace_id_{""};
};

}  // namespace yb::xcluster
