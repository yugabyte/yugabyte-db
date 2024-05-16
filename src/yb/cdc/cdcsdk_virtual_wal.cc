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

namespace yb {
namespace cdc {

using RecordInfo = CDCSDKVirtualWAL::RecordInfo;
using TabletRecordInfoPair = CDCSDKVirtualWAL::TabletRecordInfoPair;

Status CDCSDKVirtualWAL::InitVirtualWALInternal(
    const xrepl::StreamId& stream_id, const std::unordered_set<TableId>& table_list,
    const HostPort hostport, const CoarseTimePoint deadline) {
  DCHECK_EQ(publication_table_list_.size(), 0);
  for (const auto& table_id : table_list) {
    publication_table_list_.insert(table_id);
    // TODO: Make parallel calls or introduce a batch GetTabletListToPoll API in CDC which takes a
    // list of tables and provide the information in one shot.
    auto s = GetTabletListAndCheckpoint(stream_id, table_id, hostport, deadline);
    if (!s.ok()) {
      LOG(WARNING) << s.CloneAndPrepend(
          Format("Error fetching tablet list & checkpoints for table_id: $0", table_id));
      RETURN_NOT_OK(s);
    }
  }

  auto s = InitLSNAndTxnIDGenerators(stream_id);
  if (!s.ok()) {
    LOG(WARNING) << Format("Init LSN & TxnID generators failed for stream_id: $0", stream_id);
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status CDCSDKVirtualWAL::GetTabletListAndCheckpoint(
    const xrepl::StreamId& stream_id, const TableId table_id, const HostPort hostport,
    const CoarseTimePoint deadline, const TabletId& parent_tablet_id) {
  GetTabletListToPollForCDCRequestPB req;
  GetTabletListToPollForCDCResponsePB resp;
  auto table_info = req.mutable_table_info();
  table_info->set_stream_id(stream_id.ToString());
  table_info->set_table_id(table_id);
  if (!parent_tablet_id.empty()) {
    req.set_tablet_id(parent_tablet_id);
  }
  // TODO(20946): Change this RPC call to a local call.
  auto cdc_proxy = cdc_service_->GetCDCServiceProxy(hostport);
  rpc::RpcController rpc;
  rpc.set_deadline(deadline);
  auto s = cdc_proxy->GetTabletListToPollForCDC(req, &resp, &rpc);
  if (!s.ok()) {
    LOG(WARNING) << s.ToString();
    RETURN_NOT_OK(s);
  } else {
    if (resp.has_error()) {
      LOG(WARNING) << StatusFromPB(resp.error().status()).ToString();
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    } else {
      if (!parent_tablet_id.empty() && resp.tablet_checkpoint_pairs_size() != 2) {
        // parent_tablet_id will be non-empty in case of tablet-splits. In this case, we expect to
        // receive two children tablets.
        std::string error_msg = Format(
            "GetTabletListToPollForCDC didnt return two children tablets for tablet_id: $0",
            parent_tablet_id);
        LOG(WARNING) << error_msg;
        return STATUS_FORMAT(NotFound, error_msg);
      }
    }
  }

  // parent_tablet_id will be non-empty in case of tablet split. Hence, remove parent tablet's entry
  // from all relevant maps.
  if (!parent_tablet_id.empty()) {
    RETURN_NOT_OK(RemoveParentTabletEntryOnSplit(parent_tablet_id));
  }

  for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
    auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    if (!tablet_id_to_table_id_map_.contains(tablet_id)) {
      tablet_id_to_table_id_map_[tablet_id] = table_id;
    }
    auto checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();
    if (!tablet_next_req_map_.contains(tablet_id)) {
      GetChangesRequestInfo info;
      info.from_op_id = OpId::FromPB(checkpoint);
      info.write_id = checkpoint.write_id();
      info.safe_hybrid_time = checkpoint.snapshot_time();
      info.wal_segment_index = 0;
      tablet_next_req_map_[tablet_id] = info;
      VLOG(2) << "Adding entry in checkpoint map for tablet_id: " << tablet_id
              << " with cdc_sdk_checkpointt: " << checkpoint.DebugString();
    }

    if (!tablet_queues_.contains(tablet_id)) {
      VLOG(2) << "Adding tablet queue for tablet_id: " << tablet_id;
      tablet_queues_[tablet_id] = std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>();
    }
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::RemoveParentTabletEntryOnSplit(const TabletId& parent_tablet_id) {
  // TODO (20968): get the parent from_op_id and assign it to children's entry on split.
  if (tablet_next_req_map_.contains(parent_tablet_id)) {
    tablet_next_req_map_.erase(parent_tablet_id);
    VLOG(1) << "Removed entry in tablet checkpoint map for tablet_id: " << parent_tablet_id;
  }

  if (tablet_queues_.contains(parent_tablet_id)) {
    tablet_queues_.erase(parent_tablet_id);
    VLOG(1) << "Removed tablet queue for tablet_id: " << parent_tablet_id;
  }

  if (tablet_id_to_table_id_map_.contains(parent_tablet_id)) {
    tablet_id_to_table_id_map_.erase(parent_tablet_id);
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::InitLSNAndTxnIDGenerators(const xrepl::StreamId& stream_id) {
  auto entry_opt = VERIFY_RESULT(cdc_service_->cdc_state_table_->TryFetchEntry(
      {kCDCSDKSlotEntryTabletId, stream_id}, CDCStateTableEntrySelector().IncludeData()));
  if (!entry_opt) {
    return STATUS_FORMAT(
        NotFound, "CDC State Table entry for the replication slot with stream_id $0 not found",
        stream_id);
  }

  RSTATUS_DCHECK_GT(
      *entry_opt->restart_lsn, 0, NotFound,
      Format(
          "Couldnt find restart_lsn on the slot's cdc_state entry for stream_id: $0", stream_id));

  RSTATUS_DCHECK_GT(
      *entry_opt->xmin, 0, NotFound,
      Format("Couldnt find xmin on the slot's cdc_state entry for stream_id: $0", stream_id));

  RSTATUS_DCHECK_GT(
      *entry_opt->record_id_commit_time, 0, NotFound,
      Format(
          "Couldnt find unique record Id's commit_time on the slot's cdc_state entry for "
          "stream_id: $0",
          stream_id));

  last_seen_lsn_ = *entry_opt->restart_lsn;
  last_received_restart_lsn = *entry_opt->restart_lsn;

  last_seen_txn_id_ = *entry_opt->xmin;

  auto commit_time = *entry_opt->record_id_commit_time;
  TabletId commit_record_tablet_id = "";
  last_seen_unique_record_id_ = std::make_shared<CDCSDKUniqueRecordID>(CDCSDKUniqueRecordID(
      RowMessage::COMMIT, commit_time, std::numeric_limits<uint64_t>::max(),
      commit_record_tablet_id, std::numeric_limits<uint32_t>::max()));

  last_shipped_commit.commit_lsn = last_seen_lsn_;
  last_shipped_commit.commit_txn_id = last_seen_txn_id_;
  last_shipped_commit.commit_record_unique_id = last_seen_unique_record_id_;

  return Status::OK();
}

Status CDCSDKVirtualWAL::GetConsistentChangesInternal(
    const xrepl::StreamId& stream_id, GetConsistentChangesResponsePB* resp, HostPort hostport,
    CoarseTimePoint deadline) {
  std::unordered_set<TabletId> tablet_to_poll_list;
  for (const auto& tablet_queue : tablet_queues_) {
    auto tablet_id = tablet_queue.first;
    auto records_queue = tablet_queue.second;

    if (records_queue.empty()) {
      tablet_to_poll_list.insert(tablet_id);
    }
  }
  if (!tablet_to_poll_list.empty()) {
    RETURN_NOT_OK(GetChangesInternal(stream_id, tablet_to_poll_list, hostport, deadline));
  }

  TabletRecordPriorityQueue sorted_records;
  std::vector<TabletId> empty_tablet_queues;
  for (const auto& entry : tablet_queues_) {
    auto s = AddRecordToVirtualWalPriorityQueue(entry.first, &sorted_records);
    if (!s.ok()) {
      LOG(WARNING) << "Couldnt add entries to the VirtualWAL Queue for stream_id: " << stream_id
                   << " and tablet_id: " << entry.first;
      RETURN_NOT_OK(s);
    }
  }

  auto max_records = static_cast<int>(FLAGS_cdcsdk_max_consistent_records);
  while (resp->cdc_sdk_proto_records_size() < max_records && !sorted_records.empty() &&
         empty_tablet_queues.size() == 0) {
    auto next_record = VERIFY_RESULT(
        FindConsistentRecord(stream_id, &sorted_records, &empty_tablet_queues, hostport, deadline));
    auto unique_id = next_record.first;
    auto record = next_record.second;
    if (record->row_message().op() == RowMessage_Op_SAFEPOINT) {
      continue;
    }
    auto row_message = record->mutable_row_message();
    auto lsn_result = GetRecordLSN(unique_id);
    auto txn_id_result = GetRecordTxnID(unique_id);
    if (lsn_result.ok() && txn_id_result.ok()) {
      row_message->set_pg_lsn(*lsn_result);
      row_message->set_pg_transaction_id(*txn_id_result);
      last_seen_unique_record_id_ = unique_id;

      if (record->row_message().op() == RowMessage_Op_BEGIN) {
        RETURN_NOT_OK(AddEntryForBeginRecord({unique_id, record}));
      } else if (record->row_message().op() == RowMessage_Op_COMMIT) {
        last_shipped_commit.commit_lsn = *lsn_result;
        last_shipped_commit.commit_txn_id = *txn_id_result;
        last_shipped_commit.commit_record_unique_id = unique_id;
      }
      auto records = resp->add_cdc_sdk_proto_records();
      records->CopyFrom(*record);
    }
  }

  return Status::OK();
}

Status CDCSDKVirtualWAL::GetChangesInternal(
    const xrepl::StreamId& stream_id, const std::unordered_set<TabletId> tablet_to_poll_list,
    HostPort hostport, CoarseTimePoint deadline) {
  VLOG(2) << "Tablet poll list: " << AsString(tablet_to_poll_list);
  for (const auto& tablet_id : tablet_to_poll_list) {
    GetChangesRequestPB req;
    GetChangesResponsePB resp;

    RETURN_NOT_OK(PopulateGetChangesRequest(stream_id, tablet_id, &req));

    // TODO(20946): Change this RPC call to a local call.
    auto cdc_proxy = cdc_service_->GetCDCServiceProxy(hostport);
    rpc::RpcController rpc;
    rpc.set_deadline(deadline);
    auto s = cdc_proxy->GetChanges(req, &resp, &rpc);
    std::string error_msg = Format("Error calling GetChanges on tablet_id: $0", tablet_id);
    if (!s.ok()) {
      LOG(WARNING) << s.CloneAndPrepend(error_msg).ToString();
      RETURN_NOT_OK(s);
    } else {
      if (resp.has_error()) {
        s = StatusFromPB(resp.error().status());
        if (s.IsTabletSplit()) {
          RSTATUS_DCHECK(
              tablet_id_to_table_id_map_.contains(tablet_id), InternalError,
              Format("Couldnt find the correspondig table_id for tablet_id: $0", tablet_id));
          LOG(INFO) << "Tablet split encountered on tablet_id : " << tablet_id
                    << " on table_id: " << tablet_id_to_table_id_map_[tablet_id]
                    << ". Fetching children tablets";

          s = GetTabletListAndCheckpoint(
                  stream_id, tablet_id_to_table_id_map_[tablet_id], hostport, deadline, tablet_id);
          if (!s.ok()) {
            error_msg = Format("Error fetching children tablets for tablet_id: $0", tablet_id);
            LOG(WARNING) << s.CloneAndPrepend(error_msg).ToString();
            RETURN_NOT_OK(s);
          }
          continue;
        } else {
          LOG(WARNING) << s.CloneAndPrepend(error_msg).ToString();
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
    const xrepl::StreamId& stream_id, const TabletId& tablet_id, GetChangesRequestPB* req) {
  RSTATUS_DCHECK(
      tablet_next_req_map_.contains(tablet_id), InternalError,
      Format("Couldn't find entry in the tablet checkpoint map for tablet_id: $0", tablet_id));
  const GetChangesRequestInfo& next_req_info = tablet_next_req_map_[tablet_id];
  req->set_stream_id(stream_id.ToString());
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
      LOG(INFO) << "Couldnt find last_sent_from_op_id for tablet_id: " << tablet_id;
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
      tablet_queue.push(std::make_shared<CDCSDKProtoRecordPB>(record));
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
    // TODO: Remove this check once we add support for streaming DDL records.
    if (record->row_message().op() == RowMessage_Op_DDL) {
      tablet_queue->pop();
      continue;
    }
    bool result = CDCSDKUniqueRecordID::CanFormUniqueRecordId(record);
    if (result) {
      auto unique_id =
          std::make_shared<CDCSDKUniqueRecordID>(CDCSDKUniqueRecordID(tablet_id, record));
      sorted_records->push({tablet_id, {unique_id, record}});
      break;
    } else {
      DLOG(FATAL) << "Received unexpected record from Tablet Queue: " << record->DebugString();
      tablet_queue->pop();
    }
  }
  return Status::OK();
}

Result<RecordInfo> CDCSDKVirtualWAL::FindConsistentRecord(
    const xrepl::StreamId& stream_id, TabletRecordPriorityQueue* sorted_records,
    std::vector<TabletId>* empty_tablet_queues, const HostPort hostport,
    const CoarseTimePoint deadline) {
  auto tablet_record_info_pair = sorted_records->top();
  auto tablet_id = tablet_record_info_pair.first;
  auto record_info = tablet_record_info_pair.second;
  sorted_records->pop();
  tablet_queues_[tablet_id].pop();

  // Add next record from the tablet queue if available.
  auto s = AddRecordToVirtualWalPriorityQueue(tablet_id, sorted_records);
  if (!s.ok()) {
    empty_tablet_queues->push_back(tablet_id);
  }

  return record_info;
}

Result<uint64_t> CDCSDKVirtualWAL::GetRecordLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& record_id) {
  // We want to stream all records with the same commit_time as a single transaction even if the
  // changes were done as part of separate transactions. This check helps to filter
  // duplicate records like BEGIN/COMMIT that can be received in case of multi-shard transaction or
  // multiple transactions with same commit_time.
  if (last_seen_unique_record_id_->lessThan(record_id)) {
    last_seen_lsn_ += 1;
    return last_seen_lsn_;
  }

  return STATUS_FORMAT(InternalError, "RecordID is less than or equal to last seen RecordID");
}

Result<uint32_t> CDCSDKVirtualWAL::GetRecordTxnID(
    const std::shared_ptr<CDCSDKUniqueRecordID>& record_id) {
  auto last_seen_commit_time = last_seen_unique_record_id_->GetCommitTime();
  auto curr_record_commit_time = record_id->GetCommitTime();

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

  return Status::OK();
}

Result<uint64_t> CDCSDKVirtualWAL::UpdateAndPersistLSNInternal(
    const xrepl::StreamId& stream_id, const uint64_t confirmed_flush_lsn,
    const uint64_t restart_lsn_hint) {
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
  } else {
    // Special case: restart_lsn >= last_shipped_commit.commit_lsn
    // Remove all entries < last_shipped_commit.commit_lsn. Incase the map becomes empty, we send
    // the from_cdc_sdk_checkpoint as the explicit checkpoint on the next GetChanges on all of the
    // empty tablet queues.
    auto pos = commit_meta_and_last_req_map_.lower_bound(last_shipped_commit.commit_lsn);
    commit_meta_and_last_req_map_.erase(commit_meta_and_last_req_map_.begin(), pos);
  }

  RETURN_NOT_OK(UpdateSlotEntryInCDCState(stream_id, confirmed_flush_lsn, record_metadata));
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
    const xrepl::StreamId& stream_id, const uint64_t confirmed_flush_lsn,
    const CommitRecordMetadata& record_metadata) {
  CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id);
  entry.confirmed_flush_lsn = confirmed_flush_lsn;
  // Also update the return value sent from UpdateAndPersistLSNInternal if the restart_lsn value is
  // changed here.
  entry.restart_lsn = record_metadata.commit_lsn;
  entry.xmin = record_metadata.commit_txn_id;
  entry.record_id_commit_time = record_metadata.commit_record_unique_id->GetCommitTime();
  entry.cdc_sdk_safe_time = entry.record_id_commit_time;
  // Doing an update instead of upsert since we expect an entry for the slot to already exist in
  // cdc_state.
  RETURN_NOT_OK(cdc_service_->cdc_state_table_->UpdateEntries({entry}));

  return Status::OK();
}

bool CDCSDKVirtualWAL::CompareCDCSDKProtoRecords::operator()(
    const TabletRecordInfoPair& lhs, const TabletRecordInfoPair& rhs) const {
  auto lhs_record_id = lhs.second.first;
  auto rhs_record_id = rhs.second.first;

  return !lhs_record_id->lessThan(rhs_record_id);
}

}  // namespace cdc
}  // namespace yb
