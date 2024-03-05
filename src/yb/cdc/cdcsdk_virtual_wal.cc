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
  }

  for (const auto& tablet_checkpoint_pair : resp.tablet_checkpoint_pairs()) {
    auto tablet_id = tablet_checkpoint_pair.tablet_locations().tablet_id();
    if (!tablet_id_to_table_id_map_.contains(tablet_id)) {
      tablet_id_to_table_id_map_[tablet_id] = table_id;
    }
    auto checkpoint = tablet_checkpoint_pair.cdc_sdk_checkpoint();
    if (!tablet_next_req_map_.contains(tablet_id)) {
      NextGetChangesRequestInfo info;
      info.from_op_id = OpId::FromPB(checkpoint);
      // TODO(20951): if key is non-empty, mark snapshot done.
      info.write_id = checkpoint.write_id();
      info.snapshot_time = checkpoint.snapshot_time();
      info.safe_hybrid_time = -1;
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

Status CDCSDKVirtualWAL::InitLSNAndTxnIDGenerators(const xrepl::StreamId& stream_id) {
  // TODO: last_seen_lsn_, last_seen_txn_id_ and commit_time for last_seen_unique_record_id_ will be
  // initialized by values stored in the cdc_state table entry for the replication slot.
  TabletId consistent_point_tablet_id = "";  // empty tablet id for the consistent point
  last_seen_unique_record_id_ = std::make_shared<CDCSDKUniqueRecordID>(CDCSDKUniqueRecordID(
      RowMessage::UNKNOWN, 0, std::numeric_limits<uint64_t>::max(), consistent_point_tablet_id,
      std::numeric_limits<uint32_t>::max()));
  last_seen_lsn_ = 1;
  last_seen_txn_id_ = 1;

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
  const NextGetChangesRequestInfo& info = tablet_next_req_map_[tablet_id];
  req->set_stream_id(stream_id.ToString());
  // Make the CDC service return the values as QLValuePB.
  req->set_cdcsdk_request_source(CDCSDKRequestSource::WALSENDER);
  req->set_tablet_id(tablet_id);
  req->set_safe_hybrid_time(info.safe_hybrid_time);
  req->set_wal_segment_index(info.wal_segment_index);

  auto req_checkpoint = req->mutable_from_cdc_sdk_checkpoint();
  req_checkpoint->set_term(info.from_op_id.term);
  req_checkpoint->set_index(info.from_op_id.index);
  req_checkpoint->set_key(info.key);
  req_checkpoint->set_write_id(info.write_id);
  req_checkpoint->set_snapshot_time(info.snapshot_time);

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
  NextGetChangesRequestInfo& tablet_checkpoint_info = tablet_next_req_map_[tablet_id];
  tablet_checkpoint_info.from_op_id = OpId::FromPB(resp->cdc_sdk_checkpoint());
  tablet_checkpoint_info.key = resp->cdc_sdk_checkpoint().key();
  tablet_checkpoint_info.write_id = resp->cdc_sdk_checkpoint().write_id();
  tablet_checkpoint_info.snapshot_time = resp->cdc_sdk_checkpoint().snapshot_time();
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
  auto record_info = sorted_records->top();
  auto tablet_id = record_info.first;
  auto record_id_to_record = record_info.second;
  sorted_records->pop();
  tablet_queues_[tablet_id].pop();

  // Add next record from the tablet queue if available.
  auto s = AddRecordToVirtualWalPriorityQueue(tablet_id, sorted_records);
  if (!s.ok()) {
    empty_tablet_queues->push_back(tablet_id);
  }

  return record_id_to_record;
}

Result<uint64_t> CDCSDKVirtualWAL::GetRecordLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& record_id) {
  if (last_seen_unique_record_id_->lessThan(record_id)) {
    last_seen_lsn_ += 1;
    return last_seen_lsn_;
  }

  return STATUS_FORMAT(InternalError, "RecordID is less than or equal to last seen RecordID");
}

Result<uint32_t> CDCSDKVirtualWAL::GetRecordTxnID(
    const std::shared_ptr<CDCSDKUniqueRecordID>& record_id) {
  if (last_seen_unique_record_id_->GetCommitTime() < record_id->GetCommitTime()) {
    last_seen_txn_id_ += 1;
    return last_seen_txn_id_;
  } else if (last_seen_unique_record_id_->GetCommitTime() == record_id->GetCommitTime()) {
    return last_seen_txn_id_;
  }

  std::string error_msg =
      "Unexpected record! Record's commit_time is lesser than last seen record.";
  LOG(WARNING) << error_msg;
  return STATUS_FORMAT(InternalError, error_msg);
}

bool CDCSDKVirtualWAL::CompareCDCSDKProtoRecords::operator()(
    const TabletRecordInfoPair& lhs, const TabletRecordInfoPair& rhs) const {
  auto lhs_record_id = lhs.second.first;
  auto rhs_record_id = rhs.second.first;

  return !lhs_record_id->lessThan(rhs_record_id);
}

}  // namespace cdc
}  // namespace yb
