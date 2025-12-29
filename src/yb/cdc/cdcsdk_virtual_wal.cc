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

#include "yb/cdc/cdc_state_table.h"
#include "yb/cdc/cdcsdk_virtual_wal.h"
#include "yb/cdc/xrepl_stream_metadata.h"

#include "yb/common/entity_ids.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/util/backoff_waiter.h"

// TODO(22655): Remove the below macro once YB_LOG_EVERY_N_SECS_OR_VLOG() is fixed.
#define YB_CDC_LOG_WITH_PREFIX_EVERY_N_SECS_OR_VLOG(oss, n_secs, verbose_level) \
  do { \
    if (VLOG_IS_ON(verbose_level)) { \
      switch (verbose_level) { \
        case 1: \
          VLOG_WITH_PREFIX(1) << (oss).str(); \
          break; \
        case 2: \
          VLOG_WITH_PREFIX(2) << (oss).str(); \
          break; \
        case 3: \
          VLOG_WITH_PREFIX(3) << (oss).str(); \
          break; \
        case 4: \
          VLOG_WITH_PREFIX(4) << (oss).str(); \
          break; \
        default: \
          LOG(INFO) << (oss).str(); \
          break; \
      } \
    } else { \
      YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, n_secs) << (oss).str(); \
    } \
  } while (0)

#define GET_RELKIND_FROM_PG_CLASS_RECORD(record) \
  record->row_message().new_tuple().Get(16).pg_catalog_value().int8_value()

#define GET_PUBOID_FROM_PG_PUBLICATION_REL_RECORD(record) \
  record->row_message().new_tuple().Get(1).pg_catalog_value().uint32_value()

DEFINE_RUNTIME_uint32(cdcsdk_max_consistent_records, 500,
    "Controls the maximum number of records sent in GetConsistentChanges response. Only used when "
    "cdc_vwal_use_byte_threshold_for_consistent_changes flag is set to false.");

DEFINE_RUNTIME_uint64(
    cdcsdk_publication_list_refresh_interval_secs, 900 /* 15 mins */,
    "Interval in seconds at which the table list in the publication will be refreshed");

DEFINE_RUNTIME_uint64(
    cdcsdk_vwal_getchanges_resp_max_size_bytes, 4_MB,
    "Max size (in bytes) of GetChanges response for all GetChanges requests sent "
    "from Virtual WAL.");

DEFINE_test_flag(
    bool, cdcsdk_use_microseconds_refresh_interval, false,
    "Used in tests to simulate commit time ties of publication refresh record with transactions. "
    "When this flag is set to true the value of FLAGS_cdcsdk_publication_list_refresh_interval_secs"
    " will be ignored and publication refresh interval will be set to "
    "cdcsdk_publication_list_refresh_interval_micros.");

DEFINE_test_flag(
    uint64, cdcsdk_publication_list_refresh_interval_micros, 300000000 /* 5 minutes */,
    "Interval in micro seconds at which the table list in the publication will be refreshed. This "
    "will be used only when cdcsdk_use_microseconds_refresh_interval is set to true");

DEFINE_RUNTIME_bool(
    cdcsdk_enable_dynamic_table_support, true,
    "This flag can be used to switch the dynamic addition of tables ON or OFF.");

DEFINE_RUNTIME_bool(cdc_use_byte_threshold_for_vwal_changes, true,
    "This controls the size of GetConsistentChanges RPC response. If true, records will be limited "
    "by cdc_stream_records_threshold_size_bytes flag. If false, records will be limited by "
    "cdcsdk_max_consistent_records flag.");
TAG_FLAG(cdc_use_byte_threshold_for_vwal_changes, advanced);

DEFINE_RUNTIME_uint64(cdcsdk_update_restart_time_interval_secs, 60 /* 1 min */,
    "We will check if the restart lsn is equal to the last shipped lsn periodically and move the "
    "restart time forward. This flag determines the periodicity of this operation.");
TAG_FLAG(cdcsdk_update_restart_time_interval_secs, advanced);

DEFINE_RUNTIME_bool(cdcsdk_update_restart_time_when_nothing_to_stream, true,
    "When this flag is enabled, the restart time would be moved forward to the commit time of the "
    "most recently popped COMMIT / SAFEPOINT record from the priority queue iff restart lsn is "
    "equal to the last shipped lsn");
TAG_FLAG(cdcsdk_update_restart_time_when_nothing_to_stream, advanced);

DECLARE_uint64(cdc_stream_records_threshold_size_bytes);
DECLARE_bool(ysql_yb_enable_consistent_replication_from_hash_range);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);

namespace yb::cdc {

using RecordInfo = CDCSDKVirtualWAL::RecordInfo;
using TabletRecordInfoPair = CDCSDKVirtualWAL::TabletRecordInfoPair;

CDCSDKVirtualWAL::CDCSDKVirtualWAL(
    CDCServiceImpl* cdc_service, const xrepl::StreamId& stream_id, const uint64_t session_id,
    ReplicationSlotLsnType lsn_type, const uint64_t consistent_snapshot_time)
    : cdc_service_(cdc_service),
      stream_id_(stream_id),
      vwal_session_id_(session_id),
      log_prefix_(Format("VWAL [$0:$1]: ", stream_id_, vwal_session_id_)),
      slot_lsn_type_(lsn_type),
      consistent_snapshot_time_(consistent_snapshot_time) {}

std::string CDCSDKVirtualWAL::LogPrefix() const {
  return log_prefix_;
}

std::string CDCSDKVirtualWAL::GetChangesRequestInfo::ToString() const {
  std::string result = Format("from_op_id: $0", from_op_id);
  result += Format(", key: $0", key);
  result += Format(", write_id: $0", write_id);
  result += Format(", safe_hybrid_time: $0", safe_hybrid_time);
  result += Format(", wal_segment_index: $0", wal_segment_index);

  return result;
}

std::string CDCSDKVirtualWAL::LastSentGetChangesRequestInfo::ToString() const {
  std::string result = Format("from_op_id: $0", from_op_id);
  result += Format(", safe_hybrid_time: $0", safe_hybrid_time);

  return result;
}

bool CDCSDKVirtualWAL::IsTabletEligibleForVWAL(
    const std::string& tablet_id, const PartitionPB& tablet_partition_pb) {
  dockv::Partition tablet_partition;
  dockv::Partition::FromPB(tablet_partition_pb, &tablet_partition);
  const auto& [tablet_start_hash_range, _] =
      dockv::PartitionSchema::GetHashPartitionBounds(tablet_partition);
  VLOG_WITH_PREFIX(1) << "tablet " << tablet_id << " has start range: " << tablet_start_hash_range;
  return (tablet_start_hash_range >= slot_hash_range_->start_range) &&
         (tablet_start_hash_range < slot_hash_range_->end_range);
}

Status CDCSDKVirtualWAL::CheckHashRangeConstraints(const CDCStateTableEntry& slot_entry) {
  // If the slot was started with hash range constraints for the 1st time, persist the hash range in
  // slot's cdc_state entry so that on a restart, we can compare the original hash range with
  // provided hash range.
  RSTATUS_DCHECK_GT(
      *slot_entry.record_id_commit_time, 0, NotFound,
      Format(
          "Couldnt find unique record Id's commit_time on the slot's cdc_state entry for "
          "stream_id: $0",
          stream_id_));
  auto slot_restart_time = *slot_entry.record_id_commit_time;
  if (slot_entry.start_hash_range.has_value() && slot_entry.end_hash_range.has_value()) {
    auto original_start_hash_range = *slot_entry.start_hash_range;
    auto original_end_hash_range = *slot_entry.end_hash_range;
    RSTATUS_DCHECK_EQ(
        slot_hash_range_ != nullptr, true, IllegalState,
        Format(
            "Slot only meant to be used for the hash range - [$0, $1]. Please provide hash range "
            "with START_REPLICATION command.",
            original_start_hash_range, original_end_hash_range));
    if (slot_hash_range_->start_range != original_start_hash_range ||
        slot_hash_range_->end_range != original_end_hash_range) {
      return STATUS_FORMAT(
          IllegalState, Format(
                            "Slot hash range should remain unchanged. Original hash range - [$0, "
                            "$1], Provided hash range - [$2,$3]",
                            original_start_hash_range, original_end_hash_range,
                            slot_hash_range_->start_range, slot_hash_range_->end_range));
    }
  } else if (slot_hash_range_ && slot_restart_time == consistent_snapshot_time_) {
    // Note: Slot can be restarted with hash range constraints if it had no constraints initially,
    // but only if it remain upolled or received no acknowledgments in its first run.
    CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id_);
    entry.start_hash_range = slot_hash_range_->start_range;
    entry.end_hash_range = slot_hash_range_->end_range;
    LOG_WITH_PREFIX(INFO) << "Updating slot entry in cdc_state with start_hash_range: "
                          << slot_hash_range_->start_range
                          << ", end_hash_range: " << slot_hash_range_->end_range;
    RETURN_NOT_OK(cdc_service_->cdc_state_table_->UpdateEntries({entry}));
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::InitVirtualWALInternal(
    std::unordered_set<TableId> table_list, const HostPort hostport, const CoarseTimePoint deadline,
    std::unique_ptr<ReplicationSlotHashRange> slot_hash_range,
    const std::unordered_set<uint32_t>& publications_list, bool pub_all_tables) {
  DCHECK_EQ(publication_table_list_.size(), 0);
  LOG_WITH_PREFIX(INFO) << "Publication table list: " << AsString(table_list);
  auto slot_entry_opt = VERIFY_RESULT(cdc_service_->cdc_state_table_->TryFetchEntry(
      {kCDCSDKSlotEntryTabletId, stream_id_}, CDCStateTableEntrySelector().IncludeData()));
  SCHECK_FORMAT(
      slot_entry_opt, NotFound,
      "CDC State Table entry for the replication slot with stream_id $0 not found", stream_id_);

  if (FLAGS_ysql_yb_enable_consistent_replication_from_hash_range && slot_hash_range) {
    slot_hash_range_ = std::move(slot_hash_range);
    LOG_WITH_PREFIX(INFO) << "Slot provided with start_hash_range: "
                          << slot_hash_range_->start_range
                          << ", end_hash_range: " << slot_hash_range_->end_range;
    RETURN_NOT_OK(CheckHashRangeConstraints(*slot_entry_opt));
  }

  if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
    pub_all_tables_ = pub_all_tables;
    publications_list_ = std::move(publications_list);

    // Add the PG catalog tables to the table_list.
    auto stream = VERIFY_RESULT(cdc_service_->GetStream(stream_id_));
    auto namespace_id = stream->GetNamespaceId();
    auto pg_database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id));
    pg_class_table_id_ = GetPgsqlTableId(pg_database_oid, kPgClassTableOid);
    pg_publication_rel_table_id_ = GetPgsqlTableId(pg_database_oid, kPgPublicationRelOid);
    table_list.emplace(pg_class_table_id_);
    table_list.emplace(pg_publication_rel_table_id_);
    VLOG_WITH_PREFIX(1) << "Successfully added the catalog tables pg_class and pg_publication_rel "
                           "to the polling list.";
  }

  for (const auto& table_id : table_list) {
    // TODO: Make parallel calls or introduce a batch GetTabletListToPoll API in CDC which takes a
    // list of tables and provide the information in one shot.
    auto s = GetTabletListAndCheckpoint(table_id, hostport, deadline);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Format(
          "Error fetching tablet list & checkpoints for table_id $0: $1", table_id));
      LOG_WITH_PREFIX(DFATAL) << s;
      return s;
    }
    publication_table_list_.insert(table_id);
  }

  auto s = InitLSNAndTxnIDGenerators(*slot_entry_opt);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Format(
          "Init LSN & TxnID generators failed for stream_id: $0", stream_id_));
    LOG_WITH_PREFIX(DFATAL) << s;
    return s;
  }

  s = CreatePublicationRefreshTabletQueue();
  if (!s.ok()) {
    s = s.CloneAndPrepend("Could not create and initialize Publication Refresh Tablet Queue");
    LOG_WITH_PREFIX(DFATAL) << s;
    return s;
  }

  std::ostringstream oss;
  oss << "Initialised Virtual WAL with tablet queues : " << tablet_queues_.size()
      << ", LSN & txnID generator initialised with LSN : " << last_seen_lsn_
      << ", txnID: " << last_seen_txn_id_
      << ", commit_time: " << last_seen_unique_record_id_->GetCommitTime()
      << ", last_pub_refresh_time: " << last_pub_refresh_time
      << ", last_decided_pub_refresh_time: " << last_decided_pub_refresh_time.first << " - "
      << (last_decided_pub_refresh_time.second ? "true" : "false")
      << ", pub_refresh_times: " << AsString(pub_refresh_times);

  if (slot_hash_range_) {
    oss << ", start_hash_range: " << slot_hash_range_->start_range
        << ", end_hash_range: " << slot_hash_range_->end_range;
  }

  LOG_WITH_PREFIX(INFO) << oss.str();

  return Status::OK();
}

Status CDCSDKVirtualWAL::GetTabletListAndCheckpoint(
    const TableId table_id, const HostPort hostport, const CoarseTimePoint deadline,
    const TabletId& parent_tablet_id) {
  GetTabletListToPollForCDCRequestPB req;
  GetTabletListToPollForCDCResponsePB resp;
  auto table_info = req.mutable_table_info();
  table_info->set_stream_id(stream_id_.ToString());
  table_info->set_table_id(table_id);
  // parent_tablet_id will be non-empty in case of tablet split.
  if (!parent_tablet_id.empty()) {
    req.set_tablet_id(parent_tablet_id);
  }
  // TODO(20946): Change this RPC call to a local call.
  auto cdc_proxy = cdc_service_->GetCDCServiceProxy(hostport);
  rpc::RpcController rpc;
  rpc.set_deadline(deadline);
  auto s = cdc_proxy->GetTabletListToPollForCDC(req, &resp, &rpc);
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << s.ToString();
    RETURN_NOT_OK(s);
  } else {
    if (resp.has_error()) {
      LOG_WITH_PREFIX(WARNING) << StatusFromPB(resp.error().status()).ToString();
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    } else {
      if (!parent_tablet_id.empty() && resp.tablet_checkpoint_pairs_size() != 2) {
        // parent_tablet_id will be non-empty in case of tablet-splits. In this case, we expect to
        // receive two children tablets.
        std::string error_msg = Format(
            "GetTabletListToPollForCDC didnt return two children tablets for tablet_id: $0",
            parent_tablet_id);
        LOG_WITH_PREFIX(WARNING) << error_msg;
        return STATUS_FORMAT(NotFound, error_msg);
      }
    }
  }

  // parent_tablet_id will be non-empty in case of tablet split. Hence, add children tablet entries
  // in all relevant maps & then remove parent tablet's entry from all relevant maps.
  if (!parent_tablet_id.empty()) {
    std::vector<std::pair<TabletId, GetChangesRequestInfo>> children_tablet_to_next_req_info;
    for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
      auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
      auto checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();
      GetChangesRequestInfo info;
      info.from_op_id = OpId::FromPB(checkpoint);
      info.write_id = checkpoint.write_id();
      info.safe_hybrid_time = checkpoint.snapshot_time();
      info.wal_segment_index = 0;
      children_tablet_to_next_req_info.emplace_back(tablet_id, info);
    }

    RSTATUS_DCHECK(
        !tablet_id_to_table_id_map_[parent_tablet_id].empty(), NotFound,
        Format("Table_id not found for parent tablet_id $0"), parent_tablet_id);
    RSTATUS_DCHECK(
        tablet_next_req_map_.contains(parent_tablet_id), NotFound,
        Format("Next getchanges_request_info not found for parent tablet_id $0", parent_tablet_id));
    RSTATUS_DCHECK(
        tablet_queues_.contains(parent_tablet_id), NotFound,
        Format("Tablet queue not found for parent tablet_id $0", parent_tablet_id));

    // We cannot assert entry of parent tablet_id in last_sent_req_map because it is only
    // initialised after the 1st Getchanges call on a tablet. Therefore, in a scenario, it can
    // happen that on the 1st GetChanges call on the parent tablet, we might get the split error and
    // so, we might not even add an entry for the parent tablet in the last_sent_req_map.

    RETURN_NOT_OK(UpdateTabletMapsOnSplit(parent_tablet_id, children_tablet_to_next_req_info));
    return Status::OK();
  }

  for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
    auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    if (FLAGS_ysql_yb_enable_consistent_replication_from_hash_range && slot_hash_range_) {
      DCHECK(tablet_checkpoint_pair.has_tablet_locations());
      DCHECK(tablet_checkpoint_pair.tablet_locations().has_partition());
      if (!IsTabletEligibleForVWAL(
              tablet_id, tablet_checkpoint_pair.tablet_locations().partition())) {
        continue;
      }
    }

    if (!tablet_id_to_table_id_map_.contains(tablet_id)) {
      tablet_id_to_table_id_map_[tablet_id].insert(table_id);
    }
    auto checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();
    if (!tablet_next_req_map_.contains(tablet_id)) {
      GetChangesRequestInfo info;
      info.from_op_id = OpId::FromPB(checkpoint);
      info.write_id = checkpoint.write_id();
      info.safe_hybrid_time = checkpoint.snapshot_time();
      info.wal_segment_index = 0;
      tablet_next_req_map_[tablet_id] = info;
      VLOG_WITH_PREFIX(1) << "Adding entry in tablet_next_req map for tablet_id: " << tablet_id
                          << " table_id: " << table_id
                          << " with next getchanges_request_info: " << info.ToString();
    }

    if (!tablet_queues_.contains(tablet_id)) {
      tablet_queues_[tablet_id] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();
      VLOG_WITH_PREFIX(4) << "Adding empty tablet queue for tablet_id: " << tablet_id;
    }
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::UpdateTabletMapsOnSplit(
    const TabletId& parent_tablet_id,
    const std::vector<std::pair<TabletId, GetChangesRequestInfo>>
        children_tablet_to_next_req_info) {
  // First add children tablet entries in all relevant maps and initialise them with the values of
  // the parent tablet, then erase parent tablet's entry from these maps.
  const auto parent_tablet_table_id = tablet_id_to_table_id_map_.at(parent_tablet_id);
  const auto parent_next_req_info = tablet_next_req_map_.at(parent_tablet_id);

  LastSentGetChangesRequestInfo parent_last_sent_req_info;
  if (tablet_last_sent_req_map_.contains(parent_tablet_id)) {
    parent_last_sent_req_info = tablet_last_sent_req_map_.at(parent_tablet_id);
  } else {
    // If parent tablet's entry is not found in tablet_last_sent_req_map_, add children tablet
    // entries in this map with the same values that we are planning to send in the 1st GetChanges
    // call on the children tablets. It is safe to do this because without making the GetChanges
    // call on children tablets, we are not going to ship anything from the VWAL. Since, after the
    // 1st GetChanges call, this map will anyway be updated with the values present in the
    // tablet_next_req_map_, it is safe to perform the same step here itself.
    parent_last_sent_req_info.from_op_id = parent_next_req_info.from_op_id;
    parent_last_sent_req_info.safe_hybrid_time = parent_next_req_info.safe_hybrid_time;
  }

  for (const auto& [child_tablet_id, next_req_info] : children_tablet_to_next_req_info) {
    DCHECK(!tablet_id_to_table_id_map_.contains(child_tablet_id));
    tablet_id_to_table_id_map_[child_tablet_id] = parent_tablet_table_id;
    tablet_id_to_table_id_map_.erase(parent_tablet_id);

    DCHECK(!tablet_queues_.contains(child_tablet_id));
    tablet_queues_[child_tablet_id] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();
    VLOG_WITH_PREFIX(4) << "Adding empty tablet queue for child tablet_id: " << child_tablet_id;
    if (tablet_queues_.contains(parent_tablet_id)) {
      tablet_queues_.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(4) << "Removed tablet queue for parent tablet_id: " << parent_tablet_id;
    }

    if (tablet_last_sent_req_map_.contains(parent_tablet_id)) {
      tablet_last_sent_req_map_.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(4) << "Removed entry in tablet_last_sent_req_map_ for parent tablet_id: "
                          << parent_tablet_id;
    }

    DCHECK(!tablet_next_req_map_.contains(child_tablet_id));
    tablet_next_req_map_[child_tablet_id] = parent_next_req_info;
    if (next_req_info.from_op_id > parent_next_req_info.from_op_id) {
      tablet_next_req_map_[child_tablet_id].from_op_id = next_req_info.from_op_id;
    }
    if (next_req_info.safe_hybrid_time > parent_next_req_info.safe_hybrid_time) {
      tablet_next_req_map_[child_tablet_id].safe_hybrid_time = next_req_info.safe_hybrid_time;
    }
    VLOG_WITH_PREFIX(4) << "Added entry in tablet_next_req_map_ for child tablet_id: "
                        << child_tablet_id << " with next getchanges_request_info: "
                        << tablet_next_req_map_[child_tablet_id].ToString();
    if (tablet_next_req_map_.contains(parent_tablet_id)) {
      tablet_next_req_map_.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(4) << "Removed entry in tablet_next_req_map_ for parent tablet_id: "
                          << parent_tablet_id;
    }
  }

  for (auto& entry : commit_meta_and_last_req_map_) {
    auto& last_req_map = entry.second.last_sent_req_for_begin_map;
    if (last_req_map.contains(parent_tablet_id)) {
      // Delete parent's tablet entry
      last_req_map.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(4) << "Succesfully removed entry for parent tablet_id: " << parent_tablet_id
                          << "from last_sent_req_for_begin_map corresponding to commit_lsn: "
                          << entry.first;
    }
  }

  LOG_WITH_PREFIX(INFO)
      << "Succesfully replaced parent tablet " << parent_tablet_id << " with children tablets "
      << children_tablet_to_next_req_info[0].first << " [next Getchanges req: "
      << tablet_next_req_map_[children_tablet_to_next_req_info[0].first].ToString() << "] & "
      << children_tablet_to_next_req_info[1].first << " [next Getchanges req: "
      << tablet_next_req_map_[children_tablet_to_next_req_info[1].first].ToString() << "]";

  return Status::OK();
}

Status CDCSDKVirtualWAL::InitLSNAndTxnIDGenerators(
    const CDCStateTableEntry& entry_opt) {
  RSTATUS_DCHECK_GT(
      *entry_opt.restart_lsn, 0, NotFound,
      Format(
          "Couldnt find restart_lsn on the slot's cdc_state entry for stream_id: $0", stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt.confirmed_flush_lsn, 0, NotFound,
      Format(
          "Couldnt find confirmed_flush_lsn on the slot's cdc_state entry for stream_id: $0",
          stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt.xmin, 0, NotFound,
      Format("Couldnt find xmin on the slot's cdc_state entry for stream_id: $0", stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt.record_id_commit_time, 0, NotFound,
      Format(
          "Couldnt find unique record Id's commit_time on the slot's cdc_state entry for "
          "stream_id: $0",
          stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt.last_pub_refresh_time, 0, NotFound,
      Format(
          "Couldnt find last_pub_refresh_time on the slot's cdc_state entry for stream_id: $0",
          stream_id_));

  last_seen_lsn_ = *entry_opt.restart_lsn;
  last_received_restart_lsn = *entry_opt.restart_lsn;
  last_received_confirmed_flush_lsn_ = *entry_opt.confirmed_flush_lsn;

  last_seen_txn_id_ = *entry_opt.xmin;

  last_pub_refresh_time = *entry_opt.last_pub_refresh_time;

  pub_refresh_times = ParsePubRefreshTimes(*entry_opt.pub_refresh_times);

  last_decided_pub_refresh_time =
      ParseLastDecidedPubRefreshTime(*entry_opt.last_decided_pub_refresh_time);

  last_persisted_record_id_commit_time_ = HybridTime(*entry_opt.record_id_commit_time);
  // Values from the slot's entry will be used to form a unique record ID corresponding to a COMMIT
  // record with commit_time set to the record_id_commit_time field of the state table.
  std::string commit_record_docdb_txn_id = "";
  TabletId commit_record_table_id = "";
  std::string commit_record_primary_key = "";
  last_seen_unique_record_id_ = std::make_shared<CDCSDKUniqueRecordID>(CDCSDKUniqueRecordID(
      false /* publication_refresh_record*/, RowMessage::COMMIT,
      last_persisted_record_id_commit_time_.ToUint64(), commit_record_docdb_txn_id,
      std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint32_t>::max(),
      commit_record_table_id, commit_record_primary_key));

  last_shipped_commit.commit_lsn = last_seen_lsn_;
  last_shipped_commit.commit_txn_id = last_seen_txn_id_;
  last_shipped_commit.commit_record_unique_id = last_seen_unique_record_id_;
  last_shipped_commit.last_pub_refresh_time = last_pub_refresh_time;

  virtual_wal_safe_time_ = last_persisted_record_id_commit_time_;

  return Status::OK();
}

bool CanAddMoreRecords(uint64_t records_byte_size, int record_count) {
  if (FLAGS_cdc_use_byte_threshold_for_vwal_changes) {
    return records_byte_size < FLAGS_cdc_stream_records_threshold_size_bytes;
  }

  return record_count < static_cast<int>(FLAGS_cdcsdk_max_consistent_records);
}

Status CDCSDKVirtualWAL::GetConsistentChangesInternal(
    GetConsistentChangesResponsePB* resp, HostPort hostport, CoarseTimePoint deadline) {
  auto start_time = GetCurrentTimeMicros();
  MicrosecondsInt64 time_in_get_changes_micros = 0;

  std::unordered_set<TabletId> tablet_to_poll_list;
  for (const auto& tablet_queue : tablet_queues_) {
    auto tablet_id = tablet_queue.first;
    auto records_queue = tablet_queue.second;

    if (records_queue.empty()) {
      if (tablet_id == kPublicationRefreshTabletID) {
        if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
          // Delete the empty pub refresh tablet queue, since it will be no longer used.
          tablet_queues_.erase(kPublicationRefreshTabletID);
        } else {
          // Before shipping any LSN with commit time greater than the last_pub_refresh_time, we
          // need to persist the next pub_refresh_time.
          RETURN_NOT_OK(PushNextPublicationRefreshRecord());
        }
      } else {
        tablet_to_poll_list.insert(tablet_id);
      }
    }
  }
  if (!tablet_to_poll_list.empty()) {
    auto curr_time_micros = GetCurrentTimeMicros();
    RETURN_NOT_OK(GetChangesInternal(tablet_to_poll_list, hostport, deadline));
    time_in_get_changes_micros = GetCurrentTimeMicros() - curr_time_micros;
  }

  TabletRecordPriorityQueue sorted_records;
  std::vector<TabletId> empty_tablet_queues;
  for (const auto& entry : tablet_queues_) {
    auto s = AddRecordToVirtualWalPriorityQueue(entry.first, &sorted_records);
    if (!s.ok()) {
      VLOG_WITH_PREFIX(1)
          << "Couldnt add entries to the VirtualWAL Queue for stream_id: " << stream_id_
          << " and tablet_id: " << entry.first;
      RETURN_NOT_OK(s.CloneAndReplaceCode(Status::Code::kTryAgain));
    }
  }

  GetConsistentChangesRespMetadata metadata;
  uint64_t resp_records_size = 0;
  while (CanAddMoreRecords(resp_records_size, resp->cdc_sdk_proto_records_size()) &&
         !sorted_records.empty() && empty_tablet_queues.size() == 0) {
    auto tablet_record_info_pair = VERIFY_RESULT(
        GetNextRecordToBeShipped(&sorted_records, &empty_tablet_queues, hostport, deadline));
    const auto tablet_id = tablet_record_info_pair.first;
    const auto unique_id = tablet_record_info_pair.second.first;
    auto record = tablet_record_info_pair.second.second;

    // TODO(#27686): We should only send a publication refresh signal to the walsender based on
    // catalog tablet records when the pub refresh tablet queue is deleted. In case of streams that
    // are upgraded from a version which did not have this mechanism we need to keep the existing
    // pub refresh mechanism until we have reached the point in time at which retention barriers
    // were set on the sys catalog tablet.
    if (tablet_id == master::kSysCatalogTabletId) {
      auto pub_refresh_required =
          DeterminePubRefreshFromMasterRecord(tablet_record_info_pair.second);
      if (pub_refresh_required) {
        last_pub_refresh_time = unique_id->GetCommitTime();
        resp->set_needs_publication_table_list_refresh(true);
        resp->set_publication_refresh_time(last_pub_refresh_time);
        metadata.contains_publication_refresh_record = true;
        VLOG_WITH_PREFIX(1)
            << "Notifying walsender to refresh publication list in the GetConsistentChanges "
               "response at commit_time: "
            << last_pub_refresh_time;
        break;
      }
      continue;
    }

    // We never ship safepoint record to the walsender.
    if (record->row_message().op() == RowMessage_Op_SAFEPOINT) {
      RETURN_NOT_OK(ValidateAndUpdateVWALSafeTime(*unique_id));
      continue;
    }

    // Skip generating LSN & txnID for a DDL record and directly add it to the response.
    if (record->row_message().op() == RowMessage_Op_DDL) {
      auto records = resp->add_cdc_sdk_proto_records();
      VLOG_WITH_PREFIX(1) << "Shipping DDL record: " << record->ShortDebugString();
      last_seen_ddl_commit_time_ = HybridTime(record->row_message().commit_time());
      records->CopyFrom(*record);
      metadata.ddl_records++;
      continue;
    }

    // We want to ship all the txns having same commit_time as a single txn. Therefore, when we
    // encounter the first commit_record for a txn in progress, dont ship it right away since
    // there can be more txns at the same commit_time that are not yet popped from PQ. We'll ship
    // the commit record for this pg_txn_id once we are sure that we have fully shipped all DMLs
    // with the same commit_time.
    if (record->row_message().op() == RowMessage_Op_COMMIT && !should_ship_commit) {
      if (is_txn_in_progress && !curr_active_txn_commit_record) {
        VLOG_WITH_PREFIX(2) << "Encountered 1st commit record for txn_id: " << last_seen_txn_id_
                            << "with commit_time " << record->row_message().commit_time()
                            << ". Will store it for shipping later";
        curr_active_txn_commit_record =
            std::make_shared<TabletRecordInfoPair>(tablet_record_info_pair);
        continue;
      }

      // Discard any intermediate commit records having the same commit_time as the 1st commit
      // record we encountered.
      if (is_txn_in_progress && curr_active_txn_commit_record &&
          curr_active_txn_commit_record->second.second->row_message().commit_time() ==
              record->row_message().commit_time()) {
        VLOG_WITH_PREFIX(3)
            << "Encountered intermediate commit record with same commit_time for txn_id: "
            << last_seen_txn_id_ << ". Will skip it";
        continue;
      }
    }

    // When a publication refresh record is popped from the priority queue, stop populating further
    // records in the response. Set the fields 'needs_publication_table_list_refresh' and
    // 'publication_refresh_time' and return the response.
    if (unique_id->IsPublicationRefreshRecord() &&
        !FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
      // The dummy transaction id is set in the publication refresh message only when the value of
      // the flag 'cdcsdk_enable_dynamic_table_support' is true. In other words, the VWAL notifies
      // the walsender to refresh publication when the pub refresh message has a dummy transaction
      // id.
      if (record->row_message().has_transaction_id() &&
          record->row_message().transaction_id() == kDummyTransactionID) {
        last_pub_refresh_time = unique_id->GetCommitTime();
        resp->set_needs_publication_table_list_refresh(true);
        resp->set_publication_refresh_time(last_pub_refresh_time);
        metadata.contains_publication_refresh_record = true;
        VLOG_WITH_PREFIX(1)
            << "Notifying walsender to refresh publication list in the GetConsistentChanges "
               "response at commit_time: "
            << last_pub_refresh_time;
        break;
      }
      continue;
    }

    auto row_message = record->mutable_row_message();
    auto lsn_result = GetRecordLSN(unique_id);
    auto txn_id_result = GetRecordTxnID(unique_id);

    if (!lsn_result.ok() || !txn_id_result.ok()) {
      VLOG_WITH_PREFIX(3) << "Couldnt generate LSN/txnID for record: "
                          << record->ShortDebugString();
      VLOG_WITH_PREFIX(3) << "Rejected record's unique_record_id: " << unique_id->ToString()
                          << ", Last_seen_unique_record_id: "
                          << last_seen_unique_record_id_->ToString()
                          << ", Rejected record received from tablet_id: " << tablet_id
                          << ", Last_shipped_record's tablet_id: " << last_shipped_record_tablet_id;
    }

    if (lsn_result.ok() && txn_id_result.ok()) {
      row_message->set_pg_lsn(*lsn_result);
      row_message->set_pg_transaction_id(*txn_id_result);
      last_seen_unique_record_id_ = unique_id;
      last_shipped_record_tablet_id = tablet_id;

      metadata.txn_ids.insert(row_message->pg_transaction_id());
      metadata.min_txn_id = std::min(metadata.min_txn_id, row_message->pg_transaction_id());
      metadata.max_txn_id = std::max(metadata.max_txn_id, row_message->pg_transaction_id());
      metadata.min_lsn = std::min(metadata.min_lsn, row_message->pg_lsn());
      metadata.max_lsn = std::max(metadata.max_lsn, row_message->pg_lsn());

      auto& record_entry = metadata.txn_id_to_ct_records_map_[*txn_id_result];
      switch (record->row_message().op()) {
        case RowMessage_Op_INSERT: {
          metadata.insert_records++;
          record_entry.second += 1;
          record_entry.first = *lsn_result;
          break;
        }
        case RowMessage_Op_UPDATE: {
          metadata.update_records++;
          record_entry.second += 1;
          record_entry.first = *lsn_result;
          break;
        }
        case RowMessage_Op_DELETE: {
          metadata.delete_records++;
          record_entry.second += 1;
          record_entry.first = *lsn_result;
          break;
        }
        case RowMessage_Op_BEGIN: {
          RETURN_NOT_OK(AddEntryForBeginRecord({unique_id, record}));
          is_txn_in_progress = true;
          metadata.begin_records++;
          metadata.is_last_txn_fully_sent = false;
          break;
        }
        case RowMessage_Op_COMMIT: {
          last_shipped_commit.commit_lsn = *lsn_result;
          last_shipped_commit.commit_txn_id = *txn_id_result;
          last_shipped_commit.commit_record_unique_id = unique_id;
          last_shipped_commit.last_pub_refresh_time = last_pub_refresh_time;

          ResetCommitDecisionVariables();

          metadata.commit_records++;
          if (row_message->pg_transaction_id() == metadata.max_txn_id &&
              !metadata.is_last_txn_fully_sent) {
            metadata.is_last_txn_fully_sent = true;
          }
          RETURN_NOT_OK(ValidateAndUpdateVWALSafeTime(*unique_id));
          break;
        }

        case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
        case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
        case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
        case RowMessage_Op_SAFEPOINT: FALLTHROUGH_INTENDED;
        case RowMessage_Op_UNKNOWN:
          break;
      }

      VLOG_WITH_PREFIX(4) << "shipping record: " << record->ShortDebugString();

      auto records = resp->add_cdc_sdk_proto_records();
      resp_records_size += (*record).ByteSizeLong();
      records->CopyFrom(*record);
    }
  }

  auto s = UpdateRestartTimeIfRequired();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Could not update restart time for stream id: " << stream_id_
                 << ", because: " << s.ToString();
  }
  std::ostringstream oss;
  if (resp->cdc_sdk_proto_records_size() == 0) {
    oss.clear();
    oss << "Sending empty GetConsistentChanges response from total tablet queues: "
        << tablet_queues_.size();
    YB_CDC_LOG_WITH_PREFIX_EVERY_N_SECS_OR_VLOG(oss, 300, 1);
  } else {
    MonoDelta vwal_lag;
    // VWAL lag is only calculated when the response contains a commit record. If there are
    // multiple commit records in the same resposne, lag will be calculated for the last shipped
    // commit.
    if (metadata.commit_records > 0) {
      auto current_clock_time_ht = HybridTime::FromMicros(GetCurrentTimeMicros());
      vwal_lag = current_clock_time_ht.PhysicalDiff(
          HybridTime::FromPB(last_shipped_commit.commit_record_unique_id->GetCommitTime()));
    }

    int64_t unacked_txn = 0;
    if (!commit_meta_and_last_req_map_.empty()) {
      unacked_txn = (metadata.max_txn_id -
            commit_meta_and_last_req_map_.begin()->second.record_metadata.commit_txn_id);
    }

    oss.clear();
    oss << "Sending non-empty GetConsistentChanges response from total tablet queues: "
        << tablet_queues_.size() << " with total_records: " << resp->cdc_sdk_proto_records_size()
        << ", resp size: " << resp_records_size << ", total_txns: " << metadata.txn_ids.size()
        << ", min_txn_id: "
        << ((metadata.min_txn_id == std::numeric_limits<uint32_t>::max()) ? 0 : metadata.min_txn_id)
        << ", max_txn_id: " << metadata.max_txn_id << ", min_lsn: "
        << (metadata.min_lsn == std::numeric_limits<uint64_t>::max() ? 0 : metadata.min_lsn)
        << ", max_lsn: " << metadata.max_lsn
        << ", is_last_txn_fully_sent: " << (metadata.is_last_txn_fully_sent ? "true" : "false")
        << ", begin_records: " << metadata.begin_records
        << ", commit_records: " << metadata.commit_records
        << ", insert_records: " << metadata.insert_records
        << ", update_records: " << metadata.update_records
        << ", delete_records: " << metadata.delete_records
        << ", ddl_records: " << metadata.ddl_records
        << ", contains_publication_refresh_record: " << metadata.contains_publication_refresh_record
        << ", VWAL lag: " << vwal_lag.ToPrettyString()
        << ", Number of unacked txns in VWAL: " << unacked_txn;

    YB_CDC_LOG_WITH_PREFIX_EVERY_N_SECS_OR_VLOG(oss, 300, 1);

    if (VLOG_IS_ON(3) && metadata.txn_ids.size() > 0) {
      std::ostringstream txn_oss;

      txn_oss << "Records per txn details:";

      for (const auto& entry : metadata.txn_id_to_ct_records_map_) {
        txn_oss << "{txn_id, ct, dml}: {" << entry.first << ", " << entry.second.first << ", "
                << entry.second.second << "} ";
      }

      VLOG_WITH_PREFIX(3) << (txn_oss).str();
    }
  }

  VLOG_WITH_PREFIX(1)
      << "Total time spent in processing GetConsistentChanges (GetConsistentChangesInternal) is: "
      << (GetCurrentTimeMicros() - start_time)
      << " microseconds, out of which the time spent in GetChangesInternal is: "
      << time_in_get_changes_micros << " microseconds.";

  return Status::OK();
}

bool IsRetryableError(const Status& status) {
  DCHECK(!status.ok()) << "Status is not expected to be OK when calling IsRetryableError, "
                       << "status: " << status.ToString();

  for (const auto& pattern : CDCSDKVirtualWAL::kRetryableErrorPatterns) {
    if (status.code() == pattern.first &&
        status.message().ToBuffer().find(pattern.second) != std::string::npos) {
      return true;
    }
  }

  return false;
}

Status CDCSDKVirtualWAL::GetChangesInternal(
    const std::unordered_set<TabletId> tablet_to_poll_list, HostPort hostport,
    CoarseTimePoint deadline) {
  VLOG_WITH_PREFIX(2) << "Tablet poll list has " << tablet_to_poll_list.size()
                      << " tablets : " << AsString(tablet_to_poll_list);
  for (const auto& tablet_id : tablet_to_poll_list) {
    GetChangesRequestPB req;
    GetChangesResponsePB resp;

    RETURN_NOT_OK(PopulateGetChangesRequest(tablet_id, &req));

    // TODO(20946): Change this RPC call to a local call.
    auto cdc_proxy = cdc_service_->GetCDCServiceProxy(hostport);
    rpc::RpcController rpc;
    rpc.set_deadline(deadline);
    auto s = cdc_proxy->GetChanges(req, &resp, &rpc);
    std::string error_msg = Format("Error calling GetChanges on tablet_id: $0", tablet_id);
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << "GetChanges failed for tablet_id: " << tablet_id
                               << " with error: " << s.CloneAndPrepend(error_msg).ToString();
      RETURN_NOT_OK(s);
    } else {
      if (resp.has_error()) {
        s = StatusFromPB(resp.error().status());
        if (s.IsTabletSplit()) {
          RSTATUS_DCHECK(
              tablet_id_to_table_id_map_.contains(tablet_id), InternalError,
              Format("Couldnt find the correspondig table_id for tablet_id: $0", tablet_id));
          LOG_WITH_PREFIX(INFO) << "Tablet split encountered on tablet_id : " << tablet_id
                                << " on table_id: "
                                << *tablet_id_to_table_id_map_[tablet_id].begin()
                                << ". Fetching children tablets";

          // It is safe to get the table_id at the begin position since there will be only one
          // single entry in the set unless it's a colocated table case, in which case, the tablet
          // is not expected to split.
          s = GetTabletListAndCheckpoint(
              *tablet_id_to_table_id_map_[tablet_id].begin(), hostport, deadline, tablet_id);
          if (!s.ok()) {
            error_msg = Format("Error fetching children tablets for tablet_id: $0", tablet_id);
            LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(error_msg).ToString();
            RETURN_NOT_OK(s);
          }
          continue;
        } else {
          // Replace the status code with 'kTryAgain' for the errors which are retryable so they
          // don't get propagated to walsender.
          if (IsRetryableError(s)) {
            s = s.CloneAndReplaceCode(Status::Code::kTryAgain);
          }
          LOG_WITH_PREFIX(WARNING) << "GetChanges failed for tablet_id: " << tablet_id
                                   << " with error: " << s.CloneAndPrepend(error_msg).ToString();
          RETURN_NOT_OK(s);
        }
      }
    }

    RETURN_NOT_OK(AddRecordsToTabletQueue(tablet_id, &resp));
    RETURN_NOT_OK(UpdateTabletCheckpointForNextRequest(tablet_id, &resp));
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::PopulateGetChangesRequest(
    const TabletId& tablet_id, GetChangesRequestPB* req) {
  RSTATUS_DCHECK(
      tablet_next_req_map_.contains(tablet_id), InternalError,
      Format("Couldn't find entry in the tablet checkpoint map for tablet_id: $0", tablet_id));
  const GetChangesRequestInfo& next_req_info = tablet_next_req_map_[tablet_id];
  req->set_stream_id(stream_id_.ToString());
  // Make the CDC service return the values as QLValuePB.
  req->set_cdcsdk_request_source(CDCSDKRequestSource::WALSENDER);
  req->set_tablet_id(tablet_id);
  req->set_getchanges_resp_max_size_bytes(FLAGS_cdcsdk_vwal_getchanges_resp_max_size_bytes);
  // It is safe to set the safe_hybrid_time as the max of the next_req_info's safe_hybrid_time
  // and last_persisted_record_id_commit_time_ because upon restart VWAL will always send records
  // with commit time greater than restart time. When we have successive GetChanges calls (i.e VWAL
  // running in steady state), next_req_info.safe_hybrid_time will always be >= restart_time.
  req->set_safe_hybrid_time(
      std::max(next_req_info.safe_hybrid_time, last_persisted_record_id_commit_time_.ToUint64()));
  req->set_wal_segment_index(next_req_info.wal_segment_index);

  // We dont set the snapshot_time in from_cdc_sdk_checkpoint object of GetChanges request since it
  // is not used by the GetChanges RPC.
  auto req_checkpoint = req->mutable_from_cdc_sdk_checkpoint();
  req_checkpoint->set_term(next_req_info.from_op_id.term);
  req_checkpoint->set_index(next_req_info.from_op_id.index);
  req_checkpoint->set_key(next_req_info.key);
  req_checkpoint->set_write_id(next_req_info.write_id);

  if (!ShouldPopulateExplicitCheckpoint()) {
    VLOG_WITH_PREFIX(1) << "Will not be populating the explicit checkpoint with GetChanges "
                        << "request since we have unacknowledged DDL(s). Sending request as "
                        << req->ShortDebugString();
    return Status::OK();
  }

  if (!commit_meta_and_last_req_map_.empty()) {
    const auto& last_sent_req_for_begin_map =
        commit_meta_and_last_req_map_.begin()->second.last_sent_req_for_begin_map;
    if (last_sent_req_for_begin_map.contains(tablet_id)) {
      const LastSentGetChangesRequestInfo& last_sent_req_info =
          last_sent_req_for_begin_map.at(tablet_id);
      auto explicit_checkpoint = req->mutable_explicit_cdc_sdk_checkpoint();
      explicit_checkpoint->set_term(last_sent_req_info.from_op_id.term);
      explicit_checkpoint->set_index(last_sent_req_info.from_op_id.index);
      explicit_checkpoint->set_snapshot_time(last_sent_req_info.safe_hybrid_time);
    } else {
      // One possible scenario where we may not find last_sent_from_op_id for a tablet is in case
      // of children tablets after a tablet split. Consider this scenario: We shipped a BEGIN
      // record from Parent tablet P1. At this moment, we'll add an entry for the begin record
      // into the begin_record_consumption_info map and it will contain last_sent_from_op_id of
      // P1. Now, suppose P1 was polled and we received a split error. We'll get the children
      // tablet C1 & C2 and we'll poll on C1 & C2 in the same GetConsistentChanges call. Suppose,
      // after this, we received an UpdateRestartLSN RPC call for a record that was part of the
      // txn shipped from P1. Now, in the next GetChanges call on C1 & C2, we wont find
      // last_sent_from_op_id on C1 & C2. Note that we havent shipped any new txns from C1/C2, so
      // the begin_record_consumption_info map still holds the begin record shipped from P1.
      // TODO (20968): We should send parent's last sent explicit checkpoint on the 1st call on
      // children tablets to release resources.
      LOG_WITH_PREFIX(INFO) << "Couldnt find last_sent_from_op_id for tablet_id: " << tablet_id;
    }
  } else {
    // Send the from_checkpoint as the explicit checkpoint.
    auto explicit_checkpoint = req->mutable_explicit_cdc_sdk_checkpoint();
    explicit_checkpoint->set_term(next_req_info.from_op_id.term);
    explicit_checkpoint->set_index(next_req_info.from_op_id.index);
    explicit_checkpoint->set_snapshot_time(next_req_info.safe_hybrid_time);
  }

  VLOG_WITH_PREFIX(2) << "Populated GetChanges Request: " << req->ShortDebugString();

  return Status::OK();
}

Status CDCSDKVirtualWAL::AddRecordsToTabletQueue(
    const TabletId& tablet_id, const GetChangesResponsePB* resp) {
  RSTATUS_DCHECK(
      tablet_queues_.contains(tablet_id), InternalError,
      Format("Couldn't find tablet queue for tablet_id: $0", tablet_id));
  std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>& tablet_queue = tablet_queues_[tablet_id];
  if (resp->cdc_sdk_proto_records_size() > 0) {
    for (const auto& record : resp->cdc_sdk_proto_records()) {
      // cdc_service sends artificially generated DDL records whenever it has a cache miss while
      // checking for table schema. These DDL records do not have a commit_time value as they does
      // not correspond to an actual WAL entry. Hence, it is safe to skip them from adding into the
      // tablet queue.
      if (record.row_message().has_commit_time()) {
        tablet_queue.push(std::make_shared<CDCSDKProtoRecordPB>(record));
      } else {
        DCHECK_EQ(record.row_message().op(), RowMessage_Op_DDL);
        VLOG_WITH_PREFIX(3) << "Filtered record due to lack of commit_time: "
                            << record.ShortDebugString();
      }
    }
  }

  return Status::OK();
}

// We do not assign LSNs to DDL records. As a result we can only infer the acknowledgement of the
// RELATION messages based on the acknowledgement of subsequent DMLs. Upon encountering DDLs, we
// stop sending explicit checkpoint in the GetChanges requests until the restart time crosses the
// last seen DDL's commit time, hence signifying its acknowledgement.
bool CDCSDKVirtualWAL::ShouldPopulateExplicitCheckpoint() {
  return !last_seen_ddl_commit_time_.is_valid() ||
         (last_persisted_record_id_commit_time_ > last_seen_ddl_commit_time_);
}

Status CDCSDKVirtualWAL::UpdateTabletCheckpointForNextRequest(
    const TabletId& tablet_id, const GetChangesResponsePB* resp) {
  RSTATUS_DCHECK(
      tablet_next_req_map_.contains(tablet_id), InternalError,
      Format("Couldn't find entry in the tablet checkpoint map for tablet_id: $0", tablet_id));

  // Update the tablet_last_sent_req_map_ with the current GetChanges request info so that it can be
  // used as the explicit checkpoint in the next GetChanges call.
  const GetChangesRequestInfo& request_info = tablet_next_req_map_[tablet_id];
  LastSentGetChangesRequestInfo info;
  info.from_op_id = request_info.from_op_id;
  info.safe_hybrid_time = request_info.safe_hybrid_time;
  tablet_last_sent_req_map_[tablet_id] = info;

  // Update the tablet_next_req_map_ with the response checkpoint of the current GetChanges call.
  GetChangesRequestInfo& tablet_checkpoint_info = tablet_next_req_map_[tablet_id];
  tablet_checkpoint_info.from_op_id = OpId::FromPB(resp->cdc_sdk_checkpoint());
  tablet_checkpoint_info.key = resp->cdc_sdk_checkpoint().key();
  tablet_checkpoint_info.write_id = resp->cdc_sdk_checkpoint().write_id();
  tablet_checkpoint_info.safe_hybrid_time = resp->safe_hybrid_time();
  tablet_checkpoint_info.wal_segment_index = resp->wal_segment_index();

  return Status::OK();
}

Status CDCSDKVirtualWAL::AddRecordToVirtualWalPriorityQueue(
    const TabletId& tablet_id, TabletRecordPriorityQueue* sorted_records) {
  auto tablet_queue = &tablet_queues_[tablet_id];
  while (true) {
    if (tablet_queue->empty()) {
      return STATUS_FORMAT(
          InternalError, Format("Tablet queue is empty for tablet_id: $0", tablet_id));
    }
    auto record = tablet_queue->front();
    bool is_publication_refresh_record =
        (tablet_id == kPublicationRefreshTabletID || tablet_id == master::kSysCatalogTabletId);
    bool result =
        CDCSDKUniqueRecordID::CanFormUniqueRecordId(is_publication_refresh_record, record);
    if (result) {
      auto unique_id = std::make_shared<CDCSDKUniqueRecordID>(
          CDCSDKUniqueRecordID(is_publication_refresh_record, record));

      if (GetAtomicFlag(&FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream) &&
          virtual_wal_safe_time_.is_valid() &&
          unique_id->GetCommitTime() < virtual_wal_safe_time_.ToUint64()) {
        VLOG_WITH_PREFIX(3) << "Received a record with commit time lesser than virtual wal "
                               "safe time. The unique id for filtered record: "
                            << unique_id->ToString()
                            << " . virtual_wal_safe_time_: " << virtual_wal_safe_time_.ToUint64()
                            << ". The filtered record is: " << record->ShortDebugString();
        tablet_queue->pop();
        continue;
      }

      sorted_records->push({tablet_id, {unique_id, record}});
      break;
    } else {
      DLOG(FATAL) << "Received unexpected record from Tablet Queue: " << record->DebugString();
      tablet_queue->pop();
    }
  }
  return Status::OK();
}

Result<TabletRecordInfoPair> CDCSDKVirtualWAL::GetNextRecordToBeShipped(
    TabletRecordPriorityQueue* sorted_records, std::vector<TabletId>* empty_tablet_queues,
    const HostPort hostport, const CoarseTimePoint deadline) {
  TabletRecordInfoPair tablet_record_info_pair;
  if (is_txn_in_progress && curr_active_txn_commit_record) {
    // If we have already encounterd a commit record for the current txn_in_progress,
    // curr_active_txn_commit_record will point to a valid commit record. At this point, peek the
    // next entry of PQ and check if the curr_active_txn_commit_record's unique ID is less than the
    // peeked entry's unique record ID by calling CanGenerateLSN().
    //
    // If peeked entry's unique record ID > curr_active_txn_commit_record's unique ID,
    // this implies that we have shipped all the DMLs with the same commit_time, therefore, we can
    // now ship the commit record for the current pg_txn_id. So, skip popping a record from the PQ
    // in this case and pass the commit record to the LSN generator.
    auto next_pq_entry = sorted_records->top();
    auto next_record_unique_id = next_pq_entry.second.first;
    auto commit_record_unique_id = curr_active_txn_commit_record->second.first;
    if (next_record_unique_id->GreaterThanDistributedLSN(commit_record_unique_id)) {
      VLOG_WITH_PREFIX(2)
          << "Can generate LSN for commit record. Will ship the commit record for txn_id: "
          << last_seen_txn_id_;
      should_ship_commit = true;
      tablet_record_info_pair = *curr_active_txn_commit_record;
    } else {
      VLOG_WITH_PREFIX(3) << "Cannot generate LSN for commit record of txn_id: "
                          << last_seen_txn_id_ << ". Will pop from PQ";
      tablet_record_info_pair = VERIFY_RESULT(
          FindConsistentRecord(sorted_records, empty_tablet_queues, hostport, deadline));
    }
  } else {
    tablet_record_info_pair = VERIFY_RESULT(
        FindConsistentRecord(sorted_records, empty_tablet_queues, hostport, deadline));
  }

  return tablet_record_info_pair;
}

Result<TabletRecordInfoPair> CDCSDKVirtualWAL::FindConsistentRecord(
    TabletRecordPriorityQueue* sorted_records, std::vector<TabletId>* empty_tablet_queues,
    const HostPort hostport, const CoarseTimePoint deadline) {
  auto tablet_record_info_pair = sorted_records->top();
  auto tablet_id = tablet_record_info_pair.first;
  sorted_records->pop();
  tablet_queues_[tablet_id].pop();

  // Add next record from the tablet queue if available.
  auto s = AddRecordToVirtualWalPriorityQueue(tablet_id, sorted_records);
  if (!s.ok()) {
    empty_tablet_queues->push_back(tablet_id);
  }

  return tablet_record_info_pair;
}

Status CDCSDKVirtualWAL::ValidateAndUpdateVWALSafeTime(const CDCSDKUniqueRecordID& popped_record) {
  if (!GetAtomicFlag(&FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream)) {
    return Status::OK();
  }

  if (popped_record.GetOp() != RowMessage_Op_COMMIT &&
      popped_record.GetOp() != RowMessage_Op_SAFEPOINT) {
    return Status::OK();
  }

  DCHECK(virtual_wal_safe_time_.is_valid());
  // The virtual wal safe time should be non decreasing. We allow the popped record's commit time to
  // be equal to virtual_wal_safe_time_ here because there can be multiple commit / safepoint
  // records with same commit time in the priority queue. Ideally we should never get a record which
  // fails this check since we filter while inserting to the priority queue.
  RSTATUS_DCHECK(
      popped_record.GetCommitTime() >= virtual_wal_safe_time_.ToUint64(), IllegalState,
      "Received a record with commit time: {} lesser than the Virtual WAL safe "
      "time: {}. This record will not be shipped, filtered record: {}",
      popped_record.GetCommitTime(), virtual_wal_safe_time_.ToUint64(), popped_record.ToString());

  virtual_wal_safe_time_ = HybridTime(popped_record.GetCommitTime());
  return Status::OK();
}

Status CDCSDKVirtualWAL::UpdateRestartTimeIfRequired() {
  if (!GetAtomicFlag(&FLAGS_cdcsdk_update_restart_time_when_nothing_to_stream)) {
    return Status::OK();
  }

  auto current_time = HybridTime::FromMicros(GetCurrentTimeMicros());
  if (last_restart_lsn_read_time_.is_valid() &&
      current_time.PhysicalDiff(last_restart_lsn_read_time_) <
          MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_cdcsdk_update_restart_time_interval_secs))) {
    return Status::OK();
  }

  last_restart_lsn_read_time_ = current_time;

  if (last_received_restart_lsn == last_seen_lsn_) {
    RETURN_NOT_OK(UpdateAndPersistLSNInternal(
        last_received_confirmed_flush_lsn_, last_received_restart_lsn,
        true /* use_vwal_safe_time */));
  }

  return Status::OK();
}

Result<uint64_t> CDCSDKVirtualWAL::GetRecordLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id) {
  // We want to stream all records with the same commit_time as a single transaction even if the
  // changes were done as part of separate transactions. This check helps to filter
  // duplicate records like BEGIN/COMMIT that can be received in case of multi-shard transaction or
  // multiple transactions with same commit_time.
  if (curr_unique_record_id->GreaterThanDistributedLSN(last_seen_unique_record_id_)) {
    switch (slot_lsn_type_) {
      case ReplicationSlotLsnType_SEQUENCE:
        last_seen_lsn_ += 1;
        break;
      case ReplicationSlotLsnType_HYBRID_TIME:
        last_seen_lsn_ = curr_unique_record_id->GetCommitTime();
        break;
      default:
        return STATUS_FORMAT(
            IllegalState,
            Format("Invalid LSN type specified $0 for stream $1", slot_lsn_type_, stream_id_));
    }

    return last_seen_lsn_;
  }

  return STATUS_FORMAT(InternalError, "RecordID is less than or equal to last seen RecordID");
}

Result<uint32_t> CDCSDKVirtualWAL::GetRecordTxnID(
    const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id) {
  auto last_seen_commit_time = last_seen_unique_record_id_->GetCommitTime();
  auto curr_record_commit_time = curr_unique_record_id->GetCommitTime();

  if (last_seen_commit_time < curr_record_commit_time) {
    last_seen_txn_id_ += 1;
    return last_seen_txn_id_;
  } else if (last_seen_commit_time == curr_record_commit_time) {
    return last_seen_txn_id_;
  }

  return STATUS_FORMAT(InternalError, "Record's commit_time is lesser than last seen record.");
}

Status CDCSDKVirtualWAL::AddEntryForBeginRecord(const RecordInfo& record_info) {
  auto commit_lsn = last_shipped_commit.commit_lsn;
  CommitMetadataAndLastSentRequest obj;
  obj.record_metadata = last_shipped_commit;
  obj.last_sent_req_for_begin_map =
      std::unordered_map<TabletId, LastSentGetChangesRequestInfo>(tablet_last_sent_req_map_);
  DCHECK(commit_meta_and_last_req_map_.find(commit_lsn) == commit_meta_and_last_req_map_.end());
  commit_meta_and_last_req_map_[commit_lsn] = obj;
  VLOG_WITH_PREFIX(2) << "Popped BEGIN record, adding an entry in commit_meta_map with commit_lsn: "
                      << commit_lsn << ", txn_id: " << last_shipped_commit.commit_txn_id
                      << ", commit_time: "
                      << last_shipped_commit.commit_record_unique_id->GetCommitTime();

  return Status::OK();
}

Result<uint64_t> CDCSDKVirtualWAL::UpdateAndPersistLSNInternal(
    const uint64_t confirmed_flush_lsn, const uint64_t restart_lsn_hint,
    const bool use_vwal_safe_time) {
  if (restart_lsn_hint < last_received_restart_lsn) {
    return STATUS_FORMAT(
        IllegalState, Format(
                          "Received restart LSN $0 is less than the last received restart LSN $1",
                          restart_lsn_hint, last_received_restart_lsn));
  }

  CommitRecordMetadata record_metadata = last_shipped_commit;
  if (restart_lsn_hint < last_shipped_commit.commit_lsn) {
    RSTATUS_DCHECK(
        !use_vwal_safe_time, IllegalState,
        "When trying to move the restart time to VWAL safe time we should always have restart_lsn "
        "equal to the last_seen_lsn.");
    RETURN_NOT_OK(TruncateMetaMap(restart_lsn_hint));
    record_metadata = commit_meta_and_last_req_map_.begin()->second.record_metadata;
    VLOG_WITH_PREFIX(2) << "Restart_lsn " << restart_lsn_hint
                        << " is less than last_shipped_commit lsn "
                        << last_shipped_commit.commit_lsn;
  } else {
    // Special case: restart_lsn >= last_shipped_commit.commit_lsn
    // Remove all entries < last_shipped_commit.commit_lsn. Incase the map becomes empty, we send
    // the from_cdc_sdk_checkpoint as the explicit checkpoint on the next GetChanges on all of the
    // empty tablet queues.
    VLOG_WITH_PREFIX(2) << "Restart_lsn " << restart_lsn_hint
                        << " is greater than equal to last_shipped_commit lsn "
                        << last_shipped_commit.commit_lsn;
    auto pos = commit_meta_and_last_req_map_.lower_bound(last_shipped_commit.commit_lsn);
    commit_meta_and_last_req_map_.erase(commit_meta_and_last_req_map_.begin(), pos);
  }

  auto pub_refresh_trim_time = use_vwal_safe_time ? virtual_wal_safe_time_.ToUint64()
                                                  : record_metadata.last_pub_refresh_time;

  // Find the last pub_refresh_time that will be trimmed, i.e. the entry in pub_refresh_times with
  // largest value that is <= pub_refresh_trim_time.
  auto itr = pub_refresh_times.upper_bound(pub_refresh_trim_time);
  uint64_t last_trimmed_pub_refresh_time = 0;
  if (itr != pub_refresh_times.begin()) {
    last_trimmed_pub_refresh_time = *(--itr);
  }

  // Remove the entries from pub_refresh_times which are <= pub_refresh_trim_time.
  pub_refresh_times.erase(
      pub_refresh_times.begin(), pub_refresh_times.upper_bound(pub_refresh_trim_time));

  RETURN_NOT_OK(UpdateSlotEntryInCDCState(
      confirmed_flush_lsn, record_metadata, use_vwal_safe_time, last_trimmed_pub_refresh_time));
  last_received_restart_lsn = restart_lsn_hint;
  last_received_confirmed_flush_lsn_ = confirmed_flush_lsn;

  return record_metadata.commit_lsn;
}

xrepl::StreamId CDCSDKVirtualWAL::GetStreamId() {
  return stream_id_;
}

Status CDCSDKVirtualWAL::TruncateMetaMap(const uint64_t restart_lsn) {
  RSTATUS_DCHECK_LT(
      restart_lsn, last_shipped_commit.commit_lsn, InvalidArgument,
      "Restart lsn should be less than last shipped commit lsn");
  // Get the first entry whose commit_lsn > restart_lsn.
  auto itr = commit_meta_and_last_req_map_.upper_bound(restart_lsn);

  // There should definitely exist atleast one entry in the map whose commit_lsn < restart_lsn.
  // Therefore itr should never point the begin entry of the map. Consider the following for
  // correctness: Let T be the earliest unacked txn. Say T= txnid 100. This means that T=99 has been
  // acked in the past. Otherwise, T=99 would have been the earliest unacked txn. The fact that T=99
  // has been acked implies that last_received_restart_lsn >= c99 Therefore, now, we have c99 as the
  // 1st entry (begin) of the map. Therefor, restart_lsn will always be >= c99 and
  // Upper_bound(c99)will always return itr > map.begin().
  RSTATUS_DCHECK(
      itr != commit_meta_and_last_req_map_.begin(), NotFound,
      "Couldn't find any entry whose commit_lsn < restart_lsn");

  // Decrement by 1 to get the greatest entry whose commit_lsn is < restart_lsn. Invariant is <
  // and not <= because the restart_lsn passed to this method will always be equal to some
  // commit_lsn + 1.
  --itr;

  // Erase all entries in the map starting from the [start, itr).
  commit_meta_and_last_req_map_.erase(commit_meta_and_last_req_map_.begin(), itr);

  return Status::OK();
}

Status CDCSDKVirtualWAL::UpdateSlotEntryInCDCState(
    const uint64_t confirmed_flush_lsn, const CommitRecordMetadata& record_metadata,
    const bool use_vwal_safe_time, const uint64_t last_trimmed_pub_refresh_time) {
  CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id_);
  entry.confirmed_flush_lsn = confirmed_flush_lsn;
  // Also update the return value sent from UpdateAndPersistLSNInternal if the restart_lsn value is
  // changed here.
  entry.restart_lsn = record_metadata.commit_lsn;
  entry.xmin = record_metadata.commit_txn_id;
  entry.record_id_commit_time = use_vwal_safe_time
                                    ? virtual_wal_safe_time_.ToUint64()
                                    : record_metadata.commit_record_unique_id->GetCommitTime();
  entry.cdc_sdk_safe_time = entry.record_id_commit_time;
  entry.last_pub_refresh_time = (use_vwal_safe_time && last_trimmed_pub_refresh_time > 0)
                                    ? last_trimmed_pub_refresh_time
                                    : record_metadata.last_pub_refresh_time;
  entry.pub_refresh_times = GetPubRefreshTimesString();
  // Doing an update instead of upsert since we expect an entry for the slot to already exist in
  // cdc_state.
  std::ostringstream oss;
  oss << "Updating slot entry in cdc_state with confirmed_flush_lsn: " << confirmed_flush_lsn
      << ", restart_lsn: " << record_metadata.commit_lsn
      << ", xmin: " << record_metadata.commit_txn_id
      << ", commit_time: " << record_metadata.commit_record_unique_id->GetCommitTime()
      << ", last_pub_refresh_time: " << record_metadata.last_pub_refresh_time
      << ", pub_refresh_times: " << GetPubRefreshTimesString();
  YB_CDC_LOG_WITH_PREFIX_EVERY_N_SECS_OR_VLOG(oss, 300, 1);

  RETURN_NOT_OK(cdc_service_->cdc_state_table_->UpdateEntries({entry}));

  // Update the local copy of slot restart time after successfully updating the cdc_state entry.
  last_persisted_record_id_commit_time_ = HybridTime(*entry.record_id_commit_time);

  return Status::OK();
}

Status CDCSDKVirtualWAL::UpdatePubRefreshInfoInCDCState(bool update_pub_refresh_times) {
  CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id_);
  if (update_pub_refresh_times) {
    entry.pub_refresh_times = GetPubRefreshTimesString();
    VLOG_WITH_PREFIX(1) << "Updating the slot entry for stream id: " << stream_id_
                        << " with pub_refresh_times: " << *entry.pub_refresh_times;
  }
  entry.last_decided_pub_refresh_time = GetLastDecidedPubRefreshTimeString();
  VLOG_WITH_PREFIX(1) << "Updating the slot entry for stream id: " << stream_id_
                      << " with last_decided_pub_refresh_time: "
                      << *entry.last_decided_pub_refresh_time;
  RETURN_NOT_OK(cdc_service_->cdc_state_table_->UpdateEntries({entry}));
  return Status::OK();
}

void CDCSDKVirtualWAL::ResetCommitDecisionVariables() {
  is_txn_in_progress = false;
  should_ship_commit = false;
  curr_active_txn_commit_record = nullptr;
}

bool CDCSDKVirtualWAL::CompareCDCSDKProtoRecords::operator()(
    const TabletRecordInfoPair& new_record, const TabletRecordInfoPair& old_record) const {
  auto old_record_id = old_record.second.first;
  auto new_record_id = new_record.second.first;

  return old_record_id->HasHigherPriorityThan(new_record_id);
}

Status CDCSDKVirtualWAL::CreatePublicationRefreshTabletQueue() {
  if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
    LOG(INFO) << "Will not create a pub refresh tablet queue as "
                 "FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication is enabled.";
    return Status::OK();
  }

  RSTATUS_DCHECK(
      !tablet_queues_.contains(kPublicationRefreshTabletID), InternalError,
      "Publication Refresh Tablet Queue already exists");
  tablet_queues_[kPublicationRefreshTabletID] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();

  // This will be true in the very first instance of InitVirtualWAL, before any GetConsistentChanges
  // call is made. In the subsequent InitVirtualWAL calls due to restarts, the last txn shipped
  // before restart will have the previous pub refresh time stored in the
  // last_pub_refresh_time, while the last_decided_pub_refresh_time will contain the next pub
  // refresh time.
  if (pub_refresh_times.empty() && last_decided_pub_refresh_time.first == last_pub_refresh_time) {
    return PushNextPublicationRefreshRecord();
  }

  // The commit times present in the pub_refresh_times represent the times at which publication
  // refresh message was sent to the walsender. Inorder to maintain LSN determinism, the pub refresh
  // will be performed at these points in time across restarts.
  for (auto pub_refresh_time : pub_refresh_times) {
    RETURN_NOT_OK(PushPublicationRefreshRecord(pub_refresh_time, true));
  }

  // The last_decided_pub_refresh_time gives us the value of the last record that was pushed
  // into the pub refresh queue. The last_decided_pub_refresh_time.second tells us about the value
  // of the flag cdcsdk_enable_dynamic_table_support corresponding to that record, i.e whether the
  // publication refresh was performed corresponding to the record or not. If true, then the entry
  // would be present in the pub_refresh_times and hence we need not add it to the pub_refresh_queue
  // again.
  if (!last_decided_pub_refresh_time.second) {
    RETURN_NOT_OK(PushPublicationRefreshRecord(last_decided_pub_refresh_time.first, false));
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::PushNextPublicationRefreshRecord() {
  if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
    LOG(INFO) << "Will not push any records to the pub refresh tablet queue since "
                 "FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication is enabled.";
    return Status::OK();
  }

  auto last_decided_pub_refresh_time_hybrid = HybridTime(last_decided_pub_refresh_time.first);
  HybridTime hybrid_sum;
  if (FLAGS_TEST_cdcsdk_use_microseconds_refresh_interval) {
    hybrid_sum = last_decided_pub_refresh_time_hybrid.AddMicroseconds(
        GetAtomicFlag(&FLAGS_TEST_cdcsdk_publication_list_refresh_interval_micros));
  } else {
    hybrid_sum = last_decided_pub_refresh_time_hybrid.AddSeconds(
        GetAtomicFlag(&FLAGS_cdcsdk_publication_list_refresh_interval_secs));
  }
  DCHECK(hybrid_sum.ToUint64() > last_decided_pub_refresh_time.first);

  bool should_apply = GetAtomicFlag(&FLAGS_cdcsdk_enable_dynamic_table_support);
  if (should_apply) {
    pub_refresh_times.insert(hybrid_sum.ToUint64());
  }
  last_decided_pub_refresh_time = {hybrid_sum.ToUint64(), should_apply};

  // Before shipping any LSN with commit time greater than the last_pub_refresh_time, we need
  // to persist the next pub_refresh_time in last_decided_pub_refresh_time field of slot entry. If a
  // publication refresh is to be performed at the next pub_refresh_time, then we also persist it in
  // pub_refresh_times list.
  RETURN_NOT_OK(UpdatePubRefreshInfoInCDCState(should_apply));

  RSTATUS_DCHECK(
      tablet_queues_[kPublicationRefreshTabletID].empty(), InternalError,
      Format("Expected the Publication Refresh Tablet queue to be empty before inserting a record "
             "to it, but it had $0 records"),
      tablet_queues_[kPublicationRefreshTabletID].size());

  return PushPublicationRefreshRecord(hybrid_sum.ToUint64(), should_apply);
}

Status CDCSDKVirtualWAL::PushPublicationRefreshRecord(
    uint64_t pub_refresh_time, bool should_apply) {
  if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication) {
    LOG(INFO) << "Will not push any records to the pub refresh tablet queue as "
                 "FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication is enabled.";
    return Status::OK();
  }
  auto publication_refresh_record = std::make_shared<CDCSDKProtoRecordPB>();
  auto row_message = publication_refresh_record->mutable_row_message();
  row_message->set_commit_time(pub_refresh_time);
  // Set the transaction id in the pub refresh record if it should be sent to the walsender.
  if (should_apply) {
    row_message->set_transaction_id(kDummyTransactionID);
  }
  std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>& tablet_queue =
      tablet_queues_[kPublicationRefreshTabletID];
  tablet_queue.push(publication_refresh_record);
  return Status::OK();
}

std::vector<TabletId> CDCSDKVirtualWAL::GetTabletsForTable(const TableId& table_id) {
  std::vector<TabletId> tablet_ids;
  for (const auto& entry : tablet_id_to_table_id_map_) {
    if (entry.second.contains(table_id)) {
      tablet_ids.push_back(entry.first);
    }
  }
  return tablet_ids;
}

Status CDCSDKVirtualWAL::UpdatePublicationTableListInternal(
    const std::unordered_set<TableId>& new_tables, const HostPort hostport,
    const CoarseTimePoint deadline) {
  std::unordered_set<TableId> tables_to_be_added;
  std::unordered_set<TableId> tables_to_be_removed;

  for (auto table_id : new_tables) {
    if (!publication_table_list_.contains(table_id)) {
      tables_to_be_added.insert(table_id);
      VLOG_WITH_PREFIX(1) << "Table: " << table_id << " to be added to polling list";
    }
  }

  for (auto table_id : publication_table_list_) {
    if (!new_tables.contains(table_id) && table_id != pg_class_table_id_ &&
        table_id != pg_publication_rel_table_id_) {
      tables_to_be_removed.insert(table_id);
      VLOG_WITH_PREFIX(1) << "Table: " << table_id << " to be removed from polling list";
    }
  }

  if (!tables_to_be_added.empty()) {
    // Wait and validate that all the tables to be added to the streaming list have been added to
    // the stream. This is required for the dynamically created tables, as their addition to the
    // stream is done by the master background thread. Calling GetTabletListAndCheckpoint on a table
    // that is yet to be added to the stream will cause it to fail.
    RETURN_NOT_OK(ValidateTablesToBeAddedPresentInStream(tables_to_be_added, deadline));
    for (auto table_id : tables_to_be_added) {
      // Initialize the tablet_queues_, tablet_id_to_table_id_map_, and tablet_next_req_map_
      auto s = GetTabletListAndCheckpoint(table_id, hostport, deadline);
      if (!s.ok()) {
        LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(
            Format("Error fetching tablet list & checkpoints for table_id: $0", table_id));
        RETURN_NOT_OK(s);
      }
      publication_table_list_.insert(table_id);
      VLOG_WITH_PREFIX(1) << "Table: " << table_id << " added to polling list";
    }
  }

  if (!tables_to_be_removed.empty()) {
    for (auto table_id : tables_to_be_removed) {
      publication_table_list_.erase(table_id);
      auto tablet_list = GetTabletsForTable(table_id);
      for (auto tablet : tablet_list) {
        if (tablet_id_to_table_id_map_.contains(tablet)) {
          tablet_id_to_table_id_map_.erase(tablet);
        }

        if (tablet_next_req_map_.contains(tablet)) {
          tablet_next_req_map_.erase(tablet);
        }

        if (tablet_queues_.contains(tablet)) {
          tablet_queues_.erase(tablet);
        }

        if (tablet_last_sent_req_map_.contains(tablet)) {
          tablet_last_sent_req_map_.erase(tablet);
        }

        for (auto& entry : commit_meta_and_last_req_map_) {
          if (entry.second.last_sent_req_for_begin_map.contains(tablet)) {
            entry.second.last_sent_req_for_begin_map.erase(tablet);
          }
        }
      }
      VLOG_WITH_PREFIX(1) << "Table: " << table_id << " removed from the polling list";
    }
  }

  return Status::OK();
}

std::set<uint64_t> CDCSDKVirtualWAL::ParsePubRefreshTimes(
    const std::string& pub_refresh_times_str) {
  std::set<uint64_t> pub_refresh_times_result;

  if (pub_refresh_times_str.empty()) {
    return pub_refresh_times_result;
  }

  std::istringstream iss(pub_refresh_times_str);
  std::string token;
  while (std::getline(iss, token, ',')) {
    pub_refresh_times_result.insert(std::stoull(token));
  }

  return pub_refresh_times_result;
}

std::pair<uint64_t, bool> CDCSDKVirtualWAL::ParseLastDecidedPubRefreshTime(
    const std::string& last_decided_pub_refresh_time_str) {
  DCHECK(!last_decided_pub_refresh_time_str.empty());

  char lastChar = last_decided_pub_refresh_time_str.back();
  DCHECK(lastChar == 'T' || lastChar == 'F');

  std::string commit_time_str =
      last_decided_pub_refresh_time_str.substr(0, last_decided_pub_refresh_time_str.size() - 1);
  uint64_t commit_time;

  commit_time = std::stoull(commit_time_str);

  return std::make_pair(commit_time, lastChar == 'T');
}

std::string CDCSDKVirtualWAL::GetPubRefreshTimesString() {
  if (pub_refresh_times.empty()) {
    return "";
  }

  std::ostringstream oss;
  auto iter = pub_refresh_times.begin();
  oss << *iter;
  ++iter;
  for (; iter != pub_refresh_times.end(); ++iter) {
    oss << "," << *iter;
  }
  return oss.str();
}

std::string CDCSDKVirtualWAL::GetLastDecidedPubRefreshTimeString() {
  if (last_decided_pub_refresh_time.first == 0) {
    return "";
  }
  std::ostringstream oss;

  oss << last_decided_pub_refresh_time.first;
  if (last_decided_pub_refresh_time.second) {
    oss << 'T';
  } else {
    oss << 'F';
  }
  return oss.str();
}

Status CDCSDKVirtualWAL::ValidateTablesToBeAddedPresentInStream(
    const std::unordered_set<TableId>& tables_to_be_added, const CoarseTimePoint deadline) {
  CoarseTimePoint now = CoarseMonoClock::now();
  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
      deadline - now - std::chrono::milliseconds(1));
  MonoDelta timeout = MonoDelta::FromNanoseconds(duration.count());

  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto stream_metadata_result =
            cdc_service_->GetStream(stream_id_, RefreshStreamMapOption::kAlways);
        if (!stream_metadata_result.ok()) {
          LOG_WITH_PREFIX(WARNING) << "Unable to get stream metadata for stream id: " << stream_id_
                                   << " " << ResultToStatus(stream_metadata_result);
          return false;
        }
        const auto& stream_metadata = **stream_metadata_result;

        std::unordered_set<TableId> tables_in_stream;
        for (const auto& table_id : stream_metadata.GetTableIds()) {
          tables_in_stream.insert(table_id);
        }

        bool all_tables_present_in_stream = true;

        for (const auto& table_id : tables_to_be_added) {
          all_tables_present_in_stream &= tables_in_stream.contains(table_id);
        }

        return all_tables_present_in_stream;
      },
      timeout, "Timed out waiting for table to get added to the stream"));

  return Status::OK();
}

std::vector<TabletId> CDCSDKVirtualWAL::GetTabletIdsFromVirtualWAL() {
  std::vector<TabletId> tablet_ids;
  for (auto& entry : tablet_queues_) {
    tablet_ids.push_back(entry.first);
  }
  return tablet_ids;
}

bool CDCSDKVirtualWAL::DeterminePubRefreshFromMasterRecord(const RecordInfo& record_info) {
  auto const& record = record_info.second;
  auto table_id = record->row_message().table_id();

  // The record is a BEGIN / COMMIT.
  if (table_id.empty()) {
    return false;
  } else if (table_id == pg_class_table_id_) {
    // We are only interested in INSERTS to pg_class when pub_all_tables is true. Also we are only
    // interested in tables (relations) but pg_class can get entries for indexes, views etc. We only
    // signal for a pub refresh when an entry is INSERTED into pg_class table for a relation.
    if (!pub_all_tables_ || record->row_message().op() != RowMessage_Op_INSERT ||
        GET_RELKIND_FROM_PG_CLASS_RECORD(record) != 'r') {
      return false;
    }

    return true;
  } else if (table_id == pg_publication_rel_table_id_) {
    if (record->row_message().op() != RowMessage_Op_INSERT &&
        record->row_message().op() != RowMessage_Op_DELETE) {
      return false;
    }

    if (record->row_message().op() == RowMessage_Op_DELETE) {
      return true;
    }

    // If we reach here it means that this is an INSERT record in pg_publication_rel.
    auto pub_oid = GET_PUBOID_FROM_PG_PUBLICATION_REL_RECORD(record);
    if (!publications_list_.contains(pub_oid)) {
      return false;
    }

    return true;
  }

  // We should only receive records corresponding to pg_class and pg_publication_rel tables.
  DLOG(FATAL) << "Records from an unexpected table: " << table_id
             << " received from sys catalog tablet in virtual WAL."
             << " pg_class_table_id_ = " << pg_class_table_id_
             << " pg_publication_rel_table_id_ = " << pg_publication_rel_table_id_;
  return false;
}

} // namespace yb::cdc
