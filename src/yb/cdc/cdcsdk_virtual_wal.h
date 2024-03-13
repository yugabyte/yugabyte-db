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

#pragma once

#include <queue>
#include <unordered_set>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdcsdk_unique_record_id.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/opid.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"

namespace yb {

namespace cdc {

class CDCSDKVirtualWAL {
 public:
  explicit CDCSDKVirtualWAL(CDCServiceImpl* cdc_service) : cdc_service_(cdc_service) {}

  using RecordInfo =
      std::pair<std::shared_ptr<CDCSDKUniqueRecordID>, std::shared_ptr<CDCSDKProtoRecordPB>>;

  using TabletRecordInfoPair = std::pair<TabletId, RecordInfo>;

  Status InitVirtualWALInternal(
      const xrepl::StreamId& stream_id, const std::unordered_set<TableId>& table_list,
      const HostPort hostport, const CoarseTimePoint deadline);

  Status GetConsistentChangesInternal(
      const xrepl::StreamId& stream_id, GetConsistentChangesResponsePB* resp,
      const HostPort hostport, const CoarseTimePoint deadline);

  // Returns the actually persisted restart_lsn.
  Result<uint64_t> UpdateAndPersistLSNInternal(
      const xrepl::StreamId& stream_id, const uint64_t confirmed_flush_lsn,
      const uint64_t restart_lsn_hint);

 private:
  struct GetChangesRequestInfo {
    int64_t safe_hybrid_time;
    int32_t wal_segment_index;

    // The following fields will be used to populate from_cdc_sdk_checkpoint object of the next
    // GetChanges RPC call.
    OpId from_op_id;
    std::string key;
    int32_t write_id;
  };

  // For explict checkpointing, we only require a subset of fields defined in GetChangesRequestInfo
  // object. Hence, instead of reusing GetChangesRequestInfo object, we have defined a separate
  // object that will only hold enough information required for explicit checkpoint.
  struct LastSentGetChangesRequestInfo {
    OpId from_op_id;
    uint64_t safe_hybrid_time;
  };

  struct CommitRecordMetadata {
    uint64_t commit_lsn;
    uint64_t commit_txn_id;
    std::shared_ptr<CDCSDKUniqueRecordID> commit_record_unique_id;
  };

  // Custom comparator to sort records in the TabletRecordPriorityQueue by comparing their unique
  // record IDs.
  struct CompareCDCSDKProtoRecords {
    bool operator()(const TabletRecordInfoPair& lhs, const TabletRecordInfoPair& rhs) const;
  };

  using TabletRecordPriorityQueue = std::priority_queue<
      TabletRecordInfoPair, std::vector<TabletRecordInfoPair>, CompareCDCSDKProtoRecords>;

  Status GetTabletListAndCheckpoint(
      const xrepl::StreamId& stream_id, const TableId table_id, const HostPort hostport,
      const CoarseTimePoint deadline, const TabletId& parent_tablet_id = "");

  Status RemoveParentTabletEntryOnSplit(const TabletId& parent_tablet_id);

  Status GetChangesInternal(
      const xrepl::StreamId& stream_id, const std::unordered_set<TabletId> tablet_to_poll_list,
      const HostPort hostport, const CoarseTimePoint deadline);

  Status PopulateGetChangesRequest(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, GetChangesRequestPB* req);

  Status AddRecordsToTabletQueue(const TabletId& tablet_id, const GetChangesResponsePB* resp);

  Status UpdateTabletCheckpointForNextRequest(
      const TabletId& tablet_id, const GetChangesResponsePB* resp);

  Status AddRecordToVirtualWalPriorityQueue(
      const TabletId& tablet_id, TabletRecordPriorityQueue* sorted_records);

  Result<RecordInfo> FindConsistentRecord(
      const xrepl::StreamId& stream_id, TabletRecordPriorityQueue* sorted_records,
      std::vector<TabletId>* empty_tablet_queues, const HostPort hostport,
      const CoarseTimePoint deadline);

  Status InitLSNAndTxnIDGenerators(const xrepl::StreamId& stream_id);

  Result<uint64_t> GetRecordLSN(const std::shared_ptr<CDCSDKUniqueRecordID>& record_id);

  Result<uint32_t> GetRecordTxnID(const std::shared_ptr<CDCSDKUniqueRecordID>& record_id);

  Status AddEntryForBeginRecord(const RecordInfo& record_id_to_record);

  Status TruncateMetaMap(const uint64_t restart_lsn);

  Status UpdateSlotEntryInCDCState(
      const xrepl::StreamId& stream_id, const uint64_t confirmed_flush_lsn,
      const CommitRecordMetadata& record_metadata);

  CDCServiceImpl* cdc_service_;

  std::unordered_set<TableId> publication_table_list_;
  std::unordered_map<TabletId, TableId> tablet_id_to_table_id_map_;

  // Tablet queues hold the records received from GetChanges RPC call on their respective tablets.
  std::unordered_map<TabletId, std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>> tablet_queues_;

  // The next set of fields (last_seen_lsn_, last_seen_txn_id_, last_seen_unique_record_id_) hold
  // metadata (LSN/txnID/UniqueRecordId) about the record that has been last added to a
  // GetConsistentChanges response.
  uint64_t last_seen_lsn_;
  uint32_t last_seen_txn_id_;
  std::shared_ptr<CDCSDKUniqueRecordID> last_seen_unique_record_id_;

  // This will hold the restart_lsn value received in the UpdateAndPersistLSN RPC call. It will
  // initialised by the restart_lsn stores in the cdc_state's entry for slot.
  uint64_t last_received_restart_lsn;

  // This map stores all information for the next GetChanges call on a per tablet basis except for
  // the explicit checkpoint. The key is the tablet id. The value is a struct used to populate the
  // from_cdc_sdk_checkpoint field, safe_hybrid_time and wal_segment_index fields of the
  // GetChangesRequestPB of the next GetChanges RPC call.
  std::unordered_map<TabletId, GetChangesRequestInfo> tablet_next_req_map_;

  // This map stores all relevant PB objects sent in the last GetChanges call on a per tablet basis
  // except for the explicit checkpoint. The primary usage of this map is to populate the
  // explicit_cdc_sdk_checkpoint field of the GetChangesRequestPB of the next GetChanges RPC
  // call. Also refer description on the last_sent_req_for_begin_map (part of
  // CommitMetadataAndLastSentRequest struct).
  std::unordered_map<TabletId, LastSentGetChangesRequestInfo> tablet_last_sent_req_map_;

  // Stores the commit metadata of last shipped commit
  // record in GetConsistentChanges response.
  CommitRecordMetadata last_shipped_commit;

  struct CommitMetadataAndLastSentRequest {
    // The metadata values for COMMIT record will be persisted in the cdc_state entry representing
    // the replication slot on receiving feedback from Walsender via the UpdateAndPersistLSN RPC
    // call. These persisted values will then be used to initialise LSN & txnID generators on
    // restarts.
    CommitRecordMetadata record_metadata;

    // For each BEGIN record that is shipped, we want to store the last sent GetChanges request on
    // each tablet so that we can re-assemble the transaction if there was a restart since the
    // client hasnt completely acknowledged the transaction we may or may not have shipped.
    // Therefore, we will simply store a copy of tablet_last_sent_req_map_ while shipping a BEGIN
    // record. If the client acknowledges the entire transaction, we'll use the corresponding BEGIN
    // record's request information to send an explicit checkpoint in the next GetChanges call.
    std::unordered_map<TabletId, LastSentGetChangesRequestInfo> last_sent_req_for_begin_map;
  };

  // ordered map in increasing order of LSN of commit record.
  std::map<uint64_t, CommitMetadataAndLastSentRequest> commit_meta_and_last_req_map_;
};

}  // namespace cdc
}  // namespace yb
