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

#include "yb/cdc/cdc_state_table.h"

#include "yb/cdc/cdcsdk_virtual_wal.h"

DEFINE_RUNTIME_uint32(
    cdcsdk_max_consistent_records, 500,
    "Controls the maximum number of records sent in GetConsistentChanges "
    "response");

DEFINE_RUNTIME_uint64(
    cdcsdk_publication_list_refresh_interval_micros, 300000000 /* 5 minutes */,
    "Interval in micro seconds at which the table list in the publication will be refreshed");

namespace yb {
namespace cdc {

using RecordInfo = CDCSDKVirtualWAL::RecordInfo;
using TabletRecordInfoPair = CDCSDKVirtualWAL::TabletRecordInfoPair;

CDCSDKVirtualWAL::CDCSDKVirtualWAL(
    CDCServiceImpl* cdc_service, const xrepl::StreamId& stream_id, const uint64_t session_id)
    : cdc_service_(cdc_service),
      stream_id_(stream_id),
      vwal_session_id_(session_id),
      log_prefix_(Format("VWAL [$0:$1]: ", stream_id_, vwal_session_id_)) {}

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

Status CDCSDKVirtualWAL::InitVirtualWALInternal(
    const std::unordered_set<TableId>& table_list, const HostPort hostport,
    const CoarseTimePoint deadline) {
  DCHECK_EQ(publication_table_list_.size(), 0);
  for (const auto& table_id : table_list) {
    // TODO: Make parallel calls or introduce a batch GetTabletListToPoll API in CDC which takes a
    // list of tables and provide the information in one shot.
    auto s = GetTabletListAndCheckpoint(table_id, hostport, deadline);
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(
          Format("Error fetching tablet list & checkpoints for table_id: $0", table_id));
      RETURN_NOT_OK(s);
    }
    publication_table_list_.insert(table_id);
  }

  auto s = InitLSNAndTxnIDGenerators();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << Format(
        "Init LSN & TxnID generators failed for stream_id: $0", stream_id_);
    RETURN_NOT_OK(s.CloneAndPrepend(Format("Init LSN & TxnID generators failed")));
  }

  pub_refresh_interval = GetAtomicFlag(&FLAGS_cdcsdk_publication_list_refresh_interval_micros);

  s = CreatePublicationRefreshTabletQueue();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Could not create Publication Refresh Tablet Queue";
    RETURN_NOT_OK(s);
  }

  s = PushRecordToPublicationRefreshTabletQueue();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Could not push an entry to the Publication Refresh Tablet queue";
    RETURN_NOT_OK(s);
  }

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
    std::vector<TabletId> children_tablets;
    for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
      auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
      children_tablets.push_back(tablet_id);
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

    RETURN_NOT_OK(UpdateTabletMapsOnSplit(parent_tablet_id, children_tablets));
    return Status::OK();
  }

  for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
    auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
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
      VLOG_WITH_PREFIX(2) << "Adding entry in tablet_next_req map for tablet_id: " << tablet_id
                          << " with next getchanges_request_info: " << info.ToString();
    }

    if (!tablet_queues_.contains(tablet_id)) {
      tablet_queues_[tablet_id] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();
      VLOG_WITH_PREFIX(2) << "Adding empty tablet queue for tablet_id: " << tablet_id;
    }
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::UpdateTabletMapsOnSplit(
    const TabletId& parent_tablet_id, const std::vector<TabletId> children_tablets) {
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

  for (const auto& child_tablet_id : children_tablets) {
    DCHECK(!tablet_id_to_table_id_map_.contains(child_tablet_id));
    tablet_id_to_table_id_map_[child_tablet_id] = parent_tablet_table_id;
    tablet_id_to_table_id_map_.erase(parent_tablet_id);

    DCHECK(!tablet_queues_.contains(child_tablet_id));
    tablet_queues_[child_tablet_id] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();
    VLOG_WITH_PREFIX(3) << "Adding empty tablet queue for child tablet_id: " << child_tablet_id;
    tablet_queues_.erase(parent_tablet_id);
    VLOG_WITH_PREFIX(3) << "Removed tablet queue for parent tablet_id: " << parent_tablet_id;

    DCHECK(!tablet_last_sent_req_map_.contains(child_tablet_id));
    tablet_last_sent_req_map_[child_tablet_id] = parent_last_sent_req_info;
    VLOG_WITH_PREFIX(3) << "Added entry in tablet_last_sent_req_map_ for child tablet_id: "
                        << child_tablet_id
                        << " with last_sent_request_info: " << parent_last_sent_req_info.ToString();
    if (tablet_last_sent_req_map_.contains(parent_tablet_id)) {
      tablet_last_sent_req_map_.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(3) << "Removed entry in tablet_last_sent_req_map_ for parent tablet_id: "
                          << parent_tablet_id;
    }

    DCHECK(!tablet_next_req_map_.contains(child_tablet_id));
    tablet_next_req_map_[child_tablet_id] = parent_next_req_info;
    VLOG_WITH_PREFIX(3) << "Added entry in tablet_next_req_map_ for child tablet_id: "
                        << child_tablet_id << " with next getchanges_request_info: "
                        << parent_next_req_info.ToString();
    tablet_next_req_map_.erase(parent_tablet_id);
    VLOG_WITH_PREFIX(3) << "Removed entry in tablet_next_req_map_ for parent tablet_id: "
                        << parent_tablet_id;
  }

  for (auto& entry : commit_meta_and_last_req_map_) {
    auto& last_req_map = entry.second.last_sent_req_for_begin_map;
    if (last_req_map.contains(parent_tablet_id)) {
      auto parent_tablet_req_info = last_req_map.at(parent_tablet_id);
      for (const auto& child_tablet_id : children_tablets) {
        DCHECK(!last_req_map.contains(child_tablet_id));
        last_req_map[child_tablet_id] = parent_tablet_req_info;
      }
      // Delete parent's tablet entry
      last_req_map.erase(parent_tablet_id);
      VLOG_WITH_PREFIX(3)
          << "Succesfully added entries in last_sent_req_for_begin_map corresponding to "
             "commit_lsn: "
          << entry.first << " for child tablets: " << children_tablets[0] << " &  "
          << children_tablets[1] << " and removed entry for parent tablet_id: " << parent_tablet_id;
    }
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::InitLSNAndTxnIDGenerators() {
  auto entry_opt = VERIFY_RESULT(cdc_service_->cdc_state_table_->TryFetchEntry(
      {kCDCSDKSlotEntryTabletId, stream_id_}, CDCStateTableEntrySelector().IncludeData()));
  if (!entry_opt) {
    return STATUS_FORMAT(
        NotFound, "CDC State Table entry for the replication slot with stream_id $0 not found",
        stream_id_);
  }

  RSTATUS_DCHECK_GT(
      *entry_opt->restart_lsn, 0, NotFound,
      Format(
          "Couldnt find restart_lsn on the slot's cdc_state entry for stream_id: $0", stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt->xmin, 0, NotFound,
      Format("Couldnt find xmin on the slot's cdc_state entry for stream_id: $0", stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt->record_id_commit_time, 0, NotFound,
      Format(
          "Couldnt find unique record Id's commit_time on the slot's cdc_state entry for "
          "stream_id: $0",
          stream_id_));

  RSTATUS_DCHECK_GT(
      *entry_opt->last_pub_refresh_time, 0, NotFound,
      Format(
          "Couldnt find last_pub_refresh_time on the slot's cdc_state entry for stream_id: $0",
          stream_id_));

  last_seen_lsn_ = *entry_opt->restart_lsn;
  last_received_restart_lsn = *entry_opt->restart_lsn;

  last_seen_txn_id_ = *entry_opt->xmin;

  last_pub_refresh_time = *entry_opt->last_pub_refresh_time;

  auto commit_time = *entry_opt->record_id_commit_time;
  // Values from the slot's entry will be used to form a unique record ID corresponding to a COMMIT
  // record with commit_time set to the record_id_commit_time field of the state table.
  std::string commit_record_docdb_txn_id = "";
  TabletId commit_record_table_id = "";
  std::string commit_record_primary_key = "";
  last_seen_unique_record_id_ = std::make_shared<CDCSDKUniqueRecordID>(CDCSDKUniqueRecordID(
      false /* publication_refresh_record*/, RowMessage::COMMIT, commit_time,
      commit_record_docdb_txn_id, std::numeric_limits<uint64_t>::max(),
      std::numeric_limits<uint32_t>::max(), commit_record_table_id, commit_record_primary_key));

  last_shipped_commit.commit_lsn = last_seen_lsn_;
  last_shipped_commit.commit_txn_id = last_seen_txn_id_;
  last_shipped_commit.commit_record_unique_id = last_seen_unique_record_id_;
  last_shipped_commit.last_pub_refresh_time = last_pub_refresh_time;

  VLOG_WITH_PREFIX(2) << "LSN & txnID generator initialised with LSN: " << last_seen_lsn_
                      << ", txnID: " << last_seen_txn_id_ << ", commit_time: " << commit_time;

  return Status::OK();
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
        RETURN_NOT_OK(PushRecordToPublicationRefreshTabletQueue());
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
      LOG_WITH_PREFIX(INFO) << "Couldnt add entries to the VirtualWAL Queue for stream_id: "
                            << stream_id_ << " and tablet_id: " << entry.first;
      RETURN_NOT_OK(s.CloneAndReplaceCode(Status::Code::kTryAgain));
    }
  }

  GetConsistentChangesRespMetadata metadata;
  auto max_records = static_cast<int>(FLAGS_cdcsdk_max_consistent_records);
  while (resp->cdc_sdk_proto_records_size() < max_records && !sorted_records.empty() &&
         empty_tablet_queues.size() == 0) {
    auto tablet_record_info_pair = VERIFY_RESULT(
        GetNextRecordToBeShipped(&sorted_records, &empty_tablet_queues, hostport, deadline));
    const auto tablet_id = tablet_record_info_pair.first;
    const auto unique_id = tablet_record_info_pair.second.first;
    auto record = tablet_record_info_pair.second.second;

    // We never ship safepoint record to the walsender.
    if (record->row_message().op() == RowMessage_Op_SAFEPOINT) {
      continue;
    }

    // Skip generating LSN & txnID for a DDL record and directly add it to the response.
    if (record->row_message().op() == RowMessage_Op_DDL) {
      auto records = resp->add_cdc_sdk_proto_records();
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
        VLOG_WITH_PREFIX(2)
            << "Encountered intermediate commit record with same commit_time for txn_id: "
            << last_seen_txn_id_ << ". Will skip it";
        continue;
      }
    }

    // When a publication refresh record is popped from the priority queue, stop populating further
    // records in the response. Set the fields 'needs_publication_table_list_refresh' and
    // 'publication_refresh_time' and return the response.
    if (unique_id->IsPublicationRefreshRecord()) {
      last_pub_refresh_time = unique_id->GetCommitTime();
      resp->set_needs_publication_table_list_refresh(true);
      resp->set_publication_refresh_time(last_pub_refresh_time);
      metadata.contains_publication_refresh_record = true;
      VLOG_WITH_PREFIX(2)
          << "Notifying walsender to refresh publication list in the GetConsistentChanges "
             "response at commit_time: "
          << last_pub_refresh_time;
      break;
    }

    auto row_message = record->mutable_row_message();
    auto lsn_result = GetRecordLSN(unique_id);
    auto txn_id_result = GetRecordTxnID(unique_id);

    if (!lsn_result.ok()) {
      VLOG_WITH_PREFIX(2) << "Couldnt generate LSN for record: " << record->DebugString();
      VLOG_WITH_PREFIX(2) << "Rejected record's unique_record_id: " << unique_id->ToString()
                          << ", Last_seen_unique_record_id: "
                          << last_seen_unique_record_id_->ToString()
                          << ", Rejected record received from tablet_id: " << tablet_id
                          << ", Last_shipped_record's tablet_id: " << last_shipped_record_tablet_id;
    }

    if (!txn_id_result.ok()) {
      VLOG_WITH_PREFIX(2) << "Couldnt generate txnID for record: " << record->DebugString();
      VLOG_WITH_PREFIX(2) << "Rejected record's unique_record_id: " << unique_id->ToString()
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

      switch (record->row_message().op()) {
        case RowMessage_Op_INSERT: {
          metadata.insert_records++;
          break;
        }
        case RowMessage_Op_UPDATE: {
          metadata.update_records++;
          break;
        }
        case RowMessage_Op_DELETE: {
          metadata.delete_records++;
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
          break;
        }

        case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
        case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
        case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
        case RowMessage_Op_SAFEPOINT: FALLTHROUGH_INTENDED;
        case RowMessage_Op_UNKNOWN:
          break;
      }

      auto records = resp->add_cdc_sdk_proto_records();
      records->CopyFrom(*record);
    }
  }

  if (resp->cdc_sdk_proto_records_size() == 0) {
    VLOG_WITH_PREFIX(1) << "Sending empty GetConsistentChanges response";
  } else {
    int64_t vwal_lag_in_ms = 0;
    // VWAL lag is only calculated when the response contains a commit record. If there are
    // multiple commit records in the same resposne, lag will be calculated for the last shipped
    // commit.
    if (metadata.commit_records > 0) {
      auto current_clock_time_ht = HybridTime::FromMicros(GetCurrentTimeMicros());
      vwal_lag_in_ms = (current_clock_time_ht.PhysicalDiff(HybridTime::FromPB(
                           last_shipped_commit.commit_record_unique_id->GetCommitTime()))) /
                       1000;
    }

    int64_t unacked_txn = 0;
    if(!commit_meta_and_last_req_map_.empty()) {
      unacked_txn = (metadata.max_txn_id -
            commit_meta_and_last_req_map_.begin()->second.record_metadata.commit_txn_id);
    }

    VLOG_WITH_PREFIX(1)
        << "Sending non-empty GetConsistentChanges response with total_records: "
        << resp->cdc_sdk_proto_records_size() << ", total_txns: " << metadata.txn_ids.size()
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
        << ", VWAL lag: " << (metadata.commit_records > 0 ? Format("$0 ms", vwal_lag_in_ms) : "-1")
        << ", Number of unacked txns in VWAL: " << unacked_txn;
  }

  VLOG_WITH_PREFIX(1)
      << "Total time spent in processing GetConsistentChanges (GetConsistentChangesInternal) is: "
      << (GetCurrentTimeMicros() - start_time)
      << " microseconds, out of which the time spent in GetChangesInternal is: "
      << time_in_get_changes_micros << " microseconds.";

  return Status::OK();
}

Status CDCSDKVirtualWAL::GetChangesInternal(
    const std::unordered_set<TabletId> tablet_to_poll_list, HostPort hostport,
    CoarseTimePoint deadline) {
  VLOG_WITH_PREFIX(2) << "Tablet poll list: " << AsString(tablet_to_poll_list);
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
      LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(error_msg).ToString();
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
          LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(error_msg).ToString();
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
  req->set_safe_hybrid_time(next_req_info.safe_hybrid_time);
  req->set_wal_segment_index(next_req_info.wal_segment_index);

  // We dont set the snapshot_time in from_cdc_sdk_checkpoint object of GetChanges request since it
  // is not used by the GetChanges RPC.
  auto req_checkpoint = req->mutable_from_cdc_sdk_checkpoint();
  req_checkpoint->set_term(next_req_info.from_op_id.term);
  req_checkpoint->set_index(next_req_info.from_op_id.index);
  req_checkpoint->set_key(next_req_info.key);
  req_checkpoint->set_write_id(next_req_info.write_id);

  auto explicit_checkpoint = req->mutable_explicit_cdc_sdk_checkpoint();
  if (!commit_meta_and_last_req_map_.empty()) {
    const auto& last_sent_req_for_begin_map =
        commit_meta_and_last_req_map_.begin()->second.last_sent_req_for_begin_map;
    if (last_sent_req_for_begin_map.contains(tablet_id)) {
      const LastSentGetChangesRequestInfo& last_sent_req_info =
          last_sent_req_for_begin_map.at(tablet_id);
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
    explicit_checkpoint->set_term(next_req_info.from_op_id.term);
    explicit_checkpoint->set_index(next_req_info.from_op_id.index);
    explicit_checkpoint->set_snapshot_time(next_req_info.safe_hybrid_time);
  }

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
      }
    }
  }

  return Status::OK();
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
    bool is_publication_refresh_record = (tablet_id == kPublicationRefreshTabletID);
    bool result =
        CDCSDKUniqueRecordID::CanFormUniqueRecordId(is_publication_refresh_record, record);
    if (result) {
      auto unique_id = std::make_shared<CDCSDKUniqueRecordID>(
          CDCSDKUniqueRecordID(is_publication_refresh_record, record));
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
      VLOG_WITH_PREFIX(2) << "Cannot generate LSN for commit record of txn_id: "
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

Result<uint64_t> CDCSDKVirtualWAL::GetRecordLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id) {
  // We want to stream all records with the same commit_time as a single transaction even if the
  // changes were done as part of separate transactions. This check helps to filter
  // duplicate records like BEGIN/COMMIT that can be received in case of multi-shard transaction or
  // multiple transactions with same commit_time.
  if (curr_unique_record_id->GreaterThanDistributedLSN(last_seen_unique_record_id_)) {
    last_seen_lsn_ += 1;
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
    const uint64_t confirmed_flush_lsn, const uint64_t restart_lsn_hint) {
  if (restart_lsn_hint < last_received_restart_lsn) {
    return STATUS_FORMAT(
        IllegalState, Format(
                          "Received restart LSN $0 is less than the last received restart LSN $1",
                          restart_lsn_hint, last_received_restart_lsn));
  }

  CommitRecordMetadata record_metadata = last_shipped_commit;
  if (restart_lsn_hint < last_shipped_commit.commit_lsn) {
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

  auto current_clock_time_ht = HybridTime::FromMicros(GetCurrentTimeMicros());
  auto lag_in_ms = (current_clock_time_ht.PhysicalDiff(HybridTime::FromPB(
                       record_metadata.commit_record_unique_id->GetCommitTime()))) /
                   1000;
  VLOG_WITH_PREFIX(2) << "Replication Lag: " << Format("$0 ms", lag_in_ms)
                      << ", commit_lsn: " << record_metadata.commit_lsn
                      << ", commit_txn_id: " << record_metadata.commit_txn_id;

  RETURN_NOT_OK(UpdateSlotEntryInCDCState(confirmed_flush_lsn, record_metadata));
  last_received_restart_lsn = restart_lsn_hint;

  return record_metadata.commit_lsn;
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
    const uint64_t confirmed_flush_lsn, const CommitRecordMetadata& record_metadata) {
  CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id_);
  entry.confirmed_flush_lsn = confirmed_flush_lsn;
  // Also update the return value sent from UpdateAndPersistLSNInternal if the restart_lsn value is
  // changed here.
  entry.restart_lsn = record_metadata.commit_lsn;
  entry.xmin = record_metadata.commit_txn_id;
  entry.record_id_commit_time = record_metadata.commit_record_unique_id->GetCommitTime();
  entry.cdc_sdk_safe_time = entry.record_id_commit_time;
  entry.last_pub_refresh_time = record_metadata.last_pub_refresh_time;
  // Doing an update instead of upsert since we expect an entry for the slot to already exist in
  // cdc_state.
  VLOG_WITH_PREFIX(2) << "Updating slot entry in cdc_state with confirmed_flush_lsn: "
                      << confirmed_flush_lsn << ", restart_lsn: " << record_metadata.commit_lsn
                      << ", xmin: " << record_metadata.commit_txn_id << ", commit_time: "
                      << record_metadata.commit_record_unique_id->GetCommitTime();
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
  RSTATUS_DCHECK(
      !tablet_queues_.contains(kPublicationRefreshTabletID), InternalError,
      "Publication Refresh Tablet Queue already exists");
  tablet_queues_[kPublicationRefreshTabletID] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();

  return Status::OK();
}

Status CDCSDKVirtualWAL::PushRecordToPublicationRefreshTabletQueue() {
  auto publication_refresh_record = std::make_shared<CDCSDKProtoRecordPB>();
  auto row_message = publication_refresh_record->mutable_row_message();
  auto last_pub_refresh_time_hybrid = HybridTime(last_pub_refresh_time);
  auto hybrid_sum = last_pub_refresh_time_hybrid.AddMicroseconds(pub_refresh_interval);
  row_message->set_commit_time(hybrid_sum.ToUint64());
  RSTATUS_DCHECK(
      tablet_queues_[kPublicationRefreshTabletID].empty(), InternalError,
      Format("Expected the Publication Refresh Tablet queue to be empty before inserting a record "
             "to it, but it had $0 records"),
      tablet_queues_[kPublicationRefreshTabletID].size());

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
      VLOG_WITH_PREFIX(2) << "Table: " << table_id << "to be added to polling list";
    }
  }

  for (auto table_id : publication_table_list_) {
    if (!new_tables.contains(table_id)) {
      tables_to_be_removed.insert(table_id);
      VLOG_WITH_PREFIX(2) << "Table: " << table_id << "to be removed from polling list";
    }
  }

  if (!tables_to_be_added.empty()) {
    for (auto table_id : tables_to_be_added) {
      // Initialize the tablet_queues_, tablet_id_to_table_id_map_, and tablet_next_req_map_
      auto s = GetTabletListAndCheckpoint(table_id, hostport, deadline);
      if (!s.ok()) {
        LOG_WITH_PREFIX(WARNING) << s.CloneAndPrepend(
            Format("Error fetching tablet list & checkpoints for table_id: $0", table_id));
        RETURN_NOT_OK(s);
      }
      publication_table_list_.insert(table_id);
      VLOG_WITH_PREFIX(2) << "Table: " << table_id << " added to polling list";
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
      VLOG_WITH_PREFIX(2) << "Table: " << table_id << " removed from the polling list";
    }
  }

  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
