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
#include "yb/util/flags.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_AUTO_bool(xcluster_safe_time_table_include_stream_id, kLocalPersisted, false, true,
    "Include the stream ID in the xcluster safe time table key, so that a replaced stream's "
    "poller cannot advance the safe time of the stream that replaced it.");

namespace yb::xcluster {

namespace {
constexpr char kAlterReplicationGroupSuffix[] = ".ALTER";

constexpr char kSequencesDataAliasTableIdMid[] = ".sequences_data_for.";

// How many characters a normal TableId (e.g., no suffixes) takes up.
constexpr int kTableIdSize = 32;

constexpr std::string_view kTabletIdColumnSequencePrefix  = "sequence.";
constexpr std::string_view kTabletIdColumnStreamPrefix = "stream.";
constexpr std::string_view kTabletIdColumnSeparator = ".";

// Consumes "<prefix><id>." from the start of rest and sets id. Returns:
// - true if rest does not start with prefix. Nothing is consumed.
// - true if "<prefix><id>." was consumed.
// - false if prefix is not followed by "<id>.", so rest is malformed.
bool ConsumePrefixedId(std::string_view prefix, std::string_view* rest, std::string_view* id) {
  if (!rest->starts_with(prefix)) {
    return true;
  }
  rest->remove_prefix(prefix.size());
  const auto separator_position = rest->find(kTabletIdColumnSeparator);
  if (separator_position == 0 || separator_position == std::string_view::npos) {
    return false;
  }
  *id = rest->substr(0, separator_position);
  rest->remove_prefix(separator_position + kTabletIdColumnSeparator.size());
  return true;
}

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
      info.replication_group_id, info.tablet_id, info.table_id, info.stream_id);
}

Result<SafeTimeTablePK> SafeTimeTablePK::FromProducerTabletInfo(
    const xcluster::ReplicationGroupId& replication_group_id, const std::string& producer_tablet_id,
    const std::string& producer_table_id, const xrepl::StreamId& stream_id) {
  SafeTimeTablePK result;
  result.replication_group_id_ = replication_group_id;
  result.tablet_id_ = producer_tablet_id;
  if (FLAGS_xcluster_safe_time_table_include_stream_id) {
    RSTATUS_DCHECK(
        !stream_id.IsNil(), IllegalState, "Nil stream ID for producer tablet $0",
        producer_tablet_id);
    result.stream_id_ = stream_id;
  }
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

  std::string_view rest = tablet_id_column_value;
  std::string_view stream_id;
  std::string_view namespace_id;
  const bool well_formed = ConsumePrefixedId(kTabletIdColumnStreamPrefix, &rest, &stream_id) &&
                           ConsumePrefixedId(kTabletIdColumnSequencePrefix, &rest, &namespace_id) &&
                           !rest.empty();
  RSTATUS_DCHECK(
      well_formed, IllegalState,
      "Safe time table tablet ID column is not in the form "
      "[stream.<stream_id>.][sequence.<namespace_id>.]<tablet_id>: $0",
      tablet_id_column_value);

  if (!stream_id.empty()) {
    result.stream_id_ = VERIFY_RESULT(xrepl::StreamId::FromString(std::string(stream_id)));
    RSTATUS_DCHECK(
        !result.stream_id_.IsNil(), IllegalState,
        "Safe time table tablet ID column has a nil stream ID: $0", tablet_id_column_value);
  }
  result.sequences_data_namespace_id_ = namespace_id;
  result.tablet_id_ = rest;
  return result;
}

xcluster::ReplicationGroupId SafeTimeTablePK::replication_group_id_column_value() const {
  return replication_group_id_;
}

TabletId SafeTimeTablePK::tablet_id_column_value() const {
  TabletId value = tablet_id_;
  if (!sequences_data_namespace_id_.empty()) {
    value = Format(
        "$0$1$2$3", kTabletIdColumnSequencePrefix, sequences_data_namespace_id_,
        kTabletIdColumnSeparator, value);
  }
  if (!stream_id_.IsNil()) {
    value = Format(
        "$0$1$2$3", kTabletIdColumnStreamPrefix, stream_id_, kTabletIdColumnSeparator, value);
  }
  return value;
}

xcluster::ReplicationGroupId SafeTimeTablePK::replication_group_id() const {
  return replication_group_id_;
}

TabletId SafeTimeTablePK::tablet_id() const { return tablet_id_; }

const xrepl::StreamId& SafeTimeTablePK::stream_id() const { return stream_id_; }

NamespaceId SafeTimeTablePK::TEST_sequences_data_namespace_id() const {
  return sequences_data_namespace_id_;
}

bool SafeTimeTablePK::operator==(const SafeTimeTablePK& rhs) const {
  return replication_group_id_ == rhs.replication_group_id_ && tablet_id_ == rhs.tablet_id_ &&
         stream_id_ == rhs.stream_id_ &&
         sequences_data_namespace_id_ == rhs.sequences_data_namespace_id_;
}

bool SafeTimeTablePK::operator<(const SafeTimeTablePK& rhs) const {
  return std::tie(replication_group_id_, tablet_id_, stream_id_, sequences_data_namespace_id_) <
         std::tie(
             rhs.replication_group_id_, rhs.tablet_id_, rhs.stream_id_,
             rhs.sequences_data_namespace_id_);
}

std::string SafeTimeTablePK::ToString() const {
  return YB_CLASS_TO_STRING(
      replication_group_id, tablet_id, stream_id, sequences_data_namespace_id);
}

}  // namespace yb::xcluster
