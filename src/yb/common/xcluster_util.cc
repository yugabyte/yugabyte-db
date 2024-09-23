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

#include "yb/common/xcluster_util.h"

#include "yb/common/entity_ids.h"
#include "yb/common/ysql_utils.h"
#include "yb/gutil/strings/util.h"
#include "yb/util/result.h"

namespace yb::xcluster {

namespace {
constexpr char kAlterReplicationGroupSuffix[] = ".ALTER";

constexpr char kSequencesDataAliasTableIdMid[] = ".sequences_data_for.";

// How many characters a normal TableId (e.g., no suffixes) takes up.
constexpr int kTableIdSize = 32;
}  // namespace

ReplicationGroupId GetAlterReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  return ReplicationGroupId(replication_group_id.ToString() + kAlterReplicationGroupSuffix);
}

bool IsAlterReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  return GStringPiece(replication_group_id.ToString()).ends_with(kAlterReplicationGroupSuffix);
}

ReplicationGroupId GetOriginalReplicationGroupId(const ReplicationGroupId& replication_group_id) {
  // Remove the .ALTER suffix from universe_uuid if applicable.
  GStringPiece clean_id(replication_group_id.ToString());
  if (clean_id.ends_with(kAlterReplicationGroupSuffix)) {
    clean_id.remove_suffix(sizeof(kAlterReplicationGroupSuffix) - 1 /* exclude \0 ending */);
  }
  return ReplicationGroupId(clean_id.ToString());
}

std::string ShortReplicationType(XClusterReplicationType type) {
  return StringReplace(
      XClusterReplicationType_Name(type), "XCLUSTER_", "",
      /*replace_all=*/false);
}

TableId GetSequencesDataAliasForNamespace(const NamespaceId& namespace_id) {
  DCHECK(kPgSequencesDataTableId.size() == kTableIdSize);
  return kPgSequencesDataTableId + kSequencesDataAliasTableIdMid + namespace_id;
}

bool IsSequencesDataAlias(const TableId& table_id) {
  return table_id.find(kSequencesDataAliasTableIdMid) == kTableIdSize;
}

TableId StripSequencesDataAliasIfPresent(const TableId& table_id) {
  if (!IsSequencesDataAlias(table_id)) {
    return table_id;
  }
  return kPgSequencesDataTableId;
}

Result<NamespaceId> GetReplicationNamespaceBelongsTo(const TableId& table_id) {
  if (!IsSequencesDataAlias(table_id)) {
    return GetNamespaceIdFromYsqlTableId(table_id);
  }
  return table_id.substr(kTableIdSize + strlen(kSequencesDataAliasTableIdMid));
}

}  // namespace yb::xcluster
