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

#include "yb/cdc/xrepl_stream_metadata.h"

#include "yb/cdc/cdc_service.h"
#include "yb/client/session.h"
#include "yb/common/common.pb.h"
#include "yb/common/xcluster_util.h"
#include "yb/gutil/map-util.h"
#include "yb/util/shared_lock.h"

DECLARE_bool(ysql_yb_enable_replica_identity);

// If this is the initial load, assign the variable to class local variable of the same name and _
// suffix. If this is a refresh, validate that the value has not changed.
#define ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(var) \
  do { \
    if (is_refresh) { \
      SCHECK_EQ(var##_, var, IllegalState, "Unexpected change to stream metadata " #var); \
    } else { \
      var##_ = std::move(var); \
    } \
  } while (false)

namespace yb {
namespace cdc {

namespace {

// This function is to handle the upgrade scenario where the DB is upgraded from a version
// without CDCSDK changes to the one with it. So in case some required options are missing,
// the default values will be added for the same.
// (DEPRECATE_EOL 2024.1) This can be removed since XClusterSourceManager populates these defaults
// on new streams and CDCStreamLoader backfills them for older streams.
void AddDefaultOptionsIfMissing(std::unordered_map<std::string, std::string>* options) {
  InsertIfNotPresent(options, kSourceType, CDCRequestSource_Name(CDCRequestSource::XCLUSTER));
  InsertIfNotPresent(options, kCheckpointType, CDCCheckpointType_Name(CDCCheckpointType::IMPLICIT));
}

}  // anonymous namespace

std::shared_ptr<StreamMetadata::StreamTabletMetadata> StreamMetadata::GetTabletMetadata(
    const TabletId& tablet_id) {
  DCHECK(loaded_);
  {
    SharedLock l(tablet_metadata_map_mutex_);
    auto metadata = FindPtrOrNull(tablet_metadata_map_, tablet_id);
    if (metadata) {
      return metadata;
    }
  }

  std::lock_guard l(tablet_metadata_map_mutex_);
  auto metadata = FindPtrOrNull(tablet_metadata_map_, tablet_id);
  if (!metadata) {
    metadata = std::make_shared<StreamTabletMetadata>();
    EmplaceOrDie(&tablet_metadata_map_, tablet_id, metadata);
  }

  return metadata;
}

std::vector<xrepl::StreamTabletStats> StreamMetadata::GetAllStreamTabletStats(
    const xrepl::StreamId& stream_id) const {
  std::vector<xrepl::StreamTabletStats> result;
  const auto table_ids = GetTableIds();
  SharedLock l(tablet_metadata_map_mutex_);
  for (const auto& [tablet_id, metadata] : tablet_metadata_map_) {
    xrepl::StreamTabletStats stat;
    stat.stream_id_str = stream_id.ToString();
    stat.producer_tablet_id = tablet_id;
    stat.producer_table_id = table_ids.size() == 1 ? table_ids[0] : yb::AsString(table_ids);
    stat.state = SysCDCStreamEntryPB_State_Name(state_);
    metadata->PopulateStats(&stat);

    result.emplace_back(std::move(stat));
  }

  return result;
}

Status StreamMetadata::InitOrReloadIfNeeded(
    const xrepl::StreamId& stream_id, RefreshStreamMapOption opts, client::YBClient* client) {
  std::lock_guard l(load_mutex_);

  if (!loaded_ || opts == RefreshStreamMapOption::kAlways ||
      (opts == RefreshStreamMapOption::kIfInitiatedState &&
       state_.load(std::memory_order_acquire) == master::SysCDCStreamEntryPB_State_INITIATED)) {
    return GetStreamInfoFromMaster(stream_id, client);
  }

  return Status::OK();
}

Status StreamMetadata::GetStreamInfoFromMaster(
    const xrepl::StreamId& stream_id, client::YBClient* client) {
  std::lock_guard l_table(mutex_);

  bool is_refresh = loaded_.load(std::memory_order_acquire);
  // If this is the first time we are loading the metadata then we populate all the fields.
  // If this is a refresh, then only table_ids_, state_, tablet_metadata_map_ and transactional_ are
  // repopulated. The remaining fields are validated for consistency.
  ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(stream_id);

  {
    std::lock_guard l_tablet_meta(tablet_metadata_map_mutex_);
    tablet_metadata_map_.clear();
  }

  std::vector<ObjectId> object_ids;
  NamespaceId namespace_id;
  std::unordered_map<std::string, std::string> options;
  StreamModeTransactional transactional(false);
  std::optional<uint64> consistent_snapshot_time;
  std::optional<CDCSDKSnapshotOption> consistent_snapshot_option;
  std::optional<ReplicationSlotLsnType> replication_slot_lsn_type;
  std::optional<uint64> stream_creation_time;
  std::unordered_map<std::string, PgReplicaIdentity> replica_identity_map;
  std::optional<std::string> replication_slot_name;
  std::vector<TableId> unqualified_table_ids;
  std::optional<uint32_t> db_oid_to_get_sequences_for;

  RETURN_NOT_OK(client->GetCDCStream(
      stream_id, &namespace_id, &object_ids, &options, &transactional, &consistent_snapshot_time,
      &consistent_snapshot_option, &stream_creation_time, &replica_identity_map,
      &replication_slot_name, &unqualified_table_ids, &replication_slot_lsn_type));

  AddDefaultOptionsIfMissing(&options);

  for (const auto& [key, value] : options) {
    if (key == kRecordType) {
      // For replication slot consumption, the CDC RecordType Option is ignored. The replica
      // identity present in the replica identity map determines the record type for each table.
      if (FLAGS_ysql_yb_enable_replica_identity && replication_slot_name.has_value() &&
          !replication_slot_name->empty()) {
        continue;
      }
      CDCRecordType record_type;
      SCHECK(
          CDCRecordType_Parse(value, &record_type), IllegalState, "CDC record type parsing error");
      ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(record_type);
    } else if (key == kRecordFormat) {
      CDCRecordFormat record_format;
      SCHECK(
          CDCRecordFormat_Parse(value, &record_format), IllegalState,
          "CDC record format parsing error");
      ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(record_format);
    } else if (key == kSourceType) {
      CDCRequestSource source_type;
      SCHECK(
          CDCRequestSource_Parse(value, &source_type), IllegalState,
          "CDC record format parsing error");
      ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(source_type);
    } else if (key == kCheckpointType) {
      CDCCheckpointType checkpoint_type;
      SCHECK(
          CDCCheckpointType_Parse(value, &checkpoint_type), IllegalState,
          "CDC record format parsing error");
      ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(checkpoint_type);
    } else if (key == kIdType) {
      if (value == kNamespaceId) {
        // Ignore use-after-move-warning. There is at most one key of this type and value.
        ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(namespace_id); // NOLINT(bugprone-use-after-move)
      } else if (value != kTableId) {
        LOG(WARNING) << "Unsupported CDC Stream Id type: " << value;
      }

      std::lock_guard l_table(table_ids_mutex_);
      table_ids_.swap(object_ids);
      unqualified_table_ids_.swap(unqualified_table_ids);
    } else if (key == kStreamState) {
      master::SysCDCStreamEntryPB_State state;
      SCHECK(
          master::SysCDCStreamEntryPB_State_Parse(value, &state), IllegalState,
          "CDC state parsing error");
      state_.store(state, std::memory_order_release);
    } else {
      LOG(WARNING) << "Unsupported CDC Stream option: " << key;
    }
  }
  db_oid_to_get_sequences_for = VERIFY_RESULT(GetDbOidToGetSequencesForUnlocked());
  ASSIGN_ON_LOAD_SCHECK_EQ_ON_REFRESH(db_oid_to_get_sequences_for);

  {
    std::lock_guard l_table(table_ids_mutex_);
    replica_identitity_map_.swap(replica_identity_map);
  }
  transactional_.store(transactional, std::memory_order_release);
  consistent_snapshot_time_.store(consistent_snapshot_time, std::memory_order_release);
  stream_creation_time_.store(stream_creation_time, std::memory_order_release);
  consistent_snapshot_option_ = consistent_snapshot_option;
  replication_slot_name_ = replication_slot_name;
  replication_slot_lsn_type_ = replication_slot_lsn_type;

  if (!is_refresh) {
    loaded_.store(true, std::memory_order_release);
  }

  return Status::OK();
}

Result<std::optional<uint32_t>> StreamMetadata::GetDbOidToGetSequencesForUnlocked() const {
  if (source_type_ != XCLUSTER) {
    return std::nullopt;
  }
  SharedLock l(table_ids_mutex_);
  SCHECK_FORMAT(
      table_ids_.size() == 1, InvalidArgument,
      "xCluster StreamMetadata for stream $0 does not have exactly one table ID", stream_id_);
  const auto& table_id = table_ids_.back();
  if (!xcluster::IsSequencesDataAlias(table_id)) {
    return std::nullopt;
  }
  auto namespace_id = VERIFY_RESULT(xcluster::GetReplicationNamespaceBelongsTo(table_id));
  return GetPgsqlDatabaseOid(namespace_id);
}

void StreamMetadata::StreamTabletMetadata::UpdateStats(
    const MonoTime& start_time, const Status& status, int num_records, size_t bytes_sent,
    int64_t sent_index, int64_t latest_wal_index) {
  stats_history_.UpdateStats(
      start_time, status, num_records, bytes_sent, sent_index, latest_wal_index);
}

void StreamMetadata::StreamTabletMetadata::PopulateStats(xrepl::StreamTabletStats* stats) const {
  stats_history_.PopulateStats(stats);
}

std::string StreamMetadata::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_CLASS_TO_STRING(
      stream_id, namespace_id, (record_type, CDCRecordType_Name(record_type_)),
      (source_type, CDCRequestSource_Name(source_type_)));
}

}  // namespace cdc
}  // namespace yb
