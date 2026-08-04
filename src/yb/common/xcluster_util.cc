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

constexpr std::string_view kTabletIdColumnSequencePrefix  = "sequence.";
constexpr std::string_view kTabletIdColumnSeparator = ".";

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

Result<SafeTimeTablePK> SafeTimeTablePK::FromProducerTabletInfo(const ProducerTabletInfo& info) {
  return SafeTimeTablePK::FromProducerTabletInfo(
      info.replication_group_id, info.tablet_id, info.table_id);
}

Result<SafeTimeTablePK> SafeTimeTablePK::FromProducerTabletInfo(
    const xcluster::ReplicationGroupId& replication_group_id, const std::string& producer_tablet_id,
    const std::string& producer_table_id) {
  SafeTimeTablePK result;
  result.replication_group_id_ = replication_group_id;
  result.tablet_id_ = producer_tablet_id;
  if (IsSequencesDataAlias(producer_table_id)) {
    result.sequences_data_namespace_id_ =
        VERIFY_RESULT(GetReplicationNamespaceBelongsTo(producer_table_id));
  }
  return result;
}

Result<SafeTimeTablePK> SafeTimeTablePK::FromSafeTimeTableRow(
    const xcluster::ReplicationGroupId& replication_group_id_column_value,
    const std::string& tablet_id_column_value) {
  SafeTimeTablePK result;
  result.replication_group_id_ = replication_group_id_column_value;
  RSTATUS_DCHECK(
      !result.replication_group_id_.empty(), IllegalState,
      "Safe time table replication group ID column is empty");
  if (!tablet_id_column_value.starts_with(kTabletIdColumnSequencePrefix)) {
    result.tablet_id_ = tablet_id_column_value;
    RSTATUS_DCHECK(
        !result.tablet_id_.empty(), IllegalState, "Safe time table tablet ID column is empty");
  } else {
    size_t namespace_id_start = kTabletIdColumnSequencePrefix.size();
    size_t separator_position =
        tablet_id_column_value.find(kTabletIdColumnSeparator, namespace_id_start);
    if (separator_position != std::string::npos) {
      result.sequences_data_namespace_id_ = tablet_id_column_value.substr(
          namespace_id_start, separator_position - namespace_id_start);
      result.tablet_id_ =
          tablet_id_column_value.substr(separator_position + kTabletIdColumnSeparator.size());
    }
    RSTATUS_DCHECK(
        !result.tablet_id_.empty() && !result.sequences_data_namespace_id_.empty(), IllegalState,
        Format(
            "Safe time table tablet ID column starts with '$0' but is not in the form $0$1$2$3: $4",
            kTabletIdColumnSequencePrefix, "<namespace_id>", kTabletIdColumnSeparator,
            "<tablet_id>", tablet_id_column_value));
  }
  return result;
}

xcluster::ReplicationGroupId SafeTimeTablePK::replication_group_id_column_value() const {
  return replication_group_id_;
}

TabletId SafeTimeTablePK::tablet_id_column_value() const {
  if (sequences_data_namespace_id_.empty()) {
    return tablet_id_;
  }
  return Format(
      "$0$1$2$3", kTabletIdColumnSequencePrefix, sequences_data_namespace_id_,
      kTabletIdColumnSeparator, tablet_id_);
}

xcluster::ReplicationGroupId SafeTimeTablePK::replication_group_id() const {
  return replication_group_id_;
}

TabletId SafeTimeTablePK::tablet_id() const { return tablet_id_; }

NamespaceId SafeTimeTablePK::TEST_sequences_data_namespace_id() const {
  return sequences_data_namespace_id_;
}

bool SafeTimeTablePK::operator==(const SafeTimeTablePK& rhs) const {
  return replication_group_id_ == rhs.replication_group_id_ && tablet_id_ == rhs.tablet_id_ &&
         sequences_data_namespace_id_ == rhs.sequences_data_namespace_id_;
}

bool SafeTimeTablePK::operator<(const SafeTimeTablePK& rhs) const {
  return std::tie(replication_group_id_, tablet_id_, sequences_data_namespace_id_) <
         std::tie(rhs.replication_group_id_, rhs.tablet_id_, rhs.sequences_data_namespace_id_);
}

std::string SafeTimeTablePK::ToString() const {
  return YB_CLASS_TO_STRING(replication_group_id, tablet_id, sequences_data_namespace_id);
}

}  // namespace yb::xcluster
