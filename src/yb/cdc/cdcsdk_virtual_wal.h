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
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/opid.h"

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

 private:
  struct NextGetChangesRequestInfo {
    int64_t safe_hybrid_time;
    int32_t wal_segment_index;

    // The following fields will be used to populate from_cdc_sdk_checkpoint object of the next
    // GetChanges RPC call.
    OpId from_op_id;
    std::string key;
    int32_t write_id;
    uint64_t snapshot_time;
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

  CDCServiceImpl* cdc_service_;

  std::unordered_set<TableId> publication_table_list_;
  std::unordered_map<TabletId, TableId> tablet_id_to_table_id_map_;

  // Tablet queues hold the records received from GetChanges RPC call on their respective tablets.
  std::unordered_map<TabletId, std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>> tablet_queues_;

  // The next set of fields (last_seen_lsn_, last_seen_txn_id_, last_seen_unique_record_id_) hold
  // metadata (LSN/txnID/UniqueRecordId) about the record that has been last added to a
  // GetConsistentChanges response. (TODO): On the initialisation of VirtualWAL, all these  will be
  // initialised to the value set in the restart_lsn field of the cdc_state entry for the slot.
  uint64_t last_seen_lsn_;
  uint32_t last_seen_txn_id_;
  std::shared_ptr<CDCSDKUniqueRecordID> last_seen_unique_record_id_;

  // This map stores all info for the next GetChanges call except for the explicit checkpoint on a
  // per tablet basis. The key is the tablet id. The value is a struct used to populate the
  // from_cdc_sdk_checkpoint field, safe_hybrid_time and wal_segment_index fields of the
  // GetChangesRequestPB of the next GetChanges RPC call.
  std::unordered_map<TabletId, NextGetChangesRequestInfo> tablet_next_req_map_;
};

}  // namespace cdc
}  // namespace yb
