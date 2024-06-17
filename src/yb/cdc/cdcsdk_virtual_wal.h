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
  explicit CDCSDKVirtualWAL(
      CDCServiceImpl* cdc_service, const xrepl::StreamId& stream_id, const uint64_t session_id);

  using RecordInfo =
      std::pair<std::shared_ptr<CDCSDKUniqueRecordID>, std::shared_ptr<CDCSDKProtoRecordPB>>;

  using TabletRecordInfoPair = std::pair<TabletId, RecordInfo>;

  Status InitVirtualWALInternal(
      const std::unordered_set<TableId>& table_list, const HostPort hostport,
      const CoarseTimePoint deadline);

  Status GetConsistentChangesInternal(
      GetConsistentChangesResponsePB* resp, const HostPort hostport,
      const CoarseTimePoint deadline);

  // Returns the actually persisted restart_lsn.
  Result<uint64_t> UpdateAndPersistLSNInternal(
      const uint64_t confirmed_flush_lsn, const uint64_t restart_lsn_hint);

  Status UpdatePublicationTableListInternal(
      const std::unordered_set<TableId>& new_tables, const HostPort hostport,
      const CoarseTimePoint deadline);

 private:
  struct GetChangesRequestInfo {
    int64_t safe_hybrid_time;
    int32_t wal_segment_index;

    // The following fields will be used to populate from_cdc_sdk_checkpoint object of the next
    // GetChanges RPC call.
    OpId from_op_id;
    std::string key;
    int32_t write_id;

    std::string ToString() const;
  };

  // For explict checkpointing, we only require a subset of fields defined in GetChangesRequestInfo
  // object. Hence, instead of reusing GetChangesRequestInfo object, we have defined a separate
  // object that will only hold enough information required for explicit checkpoint.
  struct LastSentGetChangesRequestInfo {
    OpId from_op_id;
    uint64_t safe_hybrid_time;
    std::string ToString() const;
  };

  struct CommitRecordMetadata {
    uint64_t commit_lsn;
    uint64_t commit_txn_id;
    std::shared_ptr<CDCSDKUniqueRecordID> commit_record_unique_id;
    uint64_t last_pub_refresh_time;
  };

  // Custom comparator to sort records in the TabletRecordPriorityQueue by comparing their unique
  // record IDs.
  struct CompareCDCSDKProtoRecords {
    bool operator()(
        const TabletRecordInfoPair& new_record, const TabletRecordInfoPair& old_record) const;
  };

  struct GetConsistentChangesRespMetadata {
    int begin_records = 0;
    int commit_records = 0;
    int insert_records = 0;
    int update_records = 0;
    int delete_records = 0;
    int ddl_records = 0;
    std::unordered_set<uint32_t> txn_ids;
    uint32_t min_txn_id = std::numeric_limits<uint32_t>::max();
    uint32_t max_txn_id = 0;
    uint64_t min_lsn = std::numeric_limits<uint64_t>::max();
    uint64_t max_lsn = 0;
    bool is_last_txn_fully_sent = false;
    bool contains_publication_refresh_record = false;
  };

  using TabletRecordPriorityQueue = std::priority_queue<
      TabletRecordInfoPair, std::vector<TabletRecordInfoPair>, CompareCDCSDKProtoRecords>;

  Status GetTabletListAndCheckpoint(
      const TableId table_id, const HostPort hostport, const CoarseTimePoint deadline,
      const TabletId& parent_tablet_id = "");

  Status UpdateTabletMapsOnSplit(
      const TabletId& parent_tablet_id, const std::vector<TabletId> children_tablets);

  Status GetChangesInternal(
      const std::unordered_set<TabletId> tablet_to_poll_list, const HostPort hostport,
      const CoarseTimePoint deadline);

  Status PopulateGetChangesRequest(const TabletId& tablet_id, GetChangesRequestPB* req);

  Status AddRecordsToTabletQueue(const TabletId& tablet_id, const GetChangesResponsePB* resp);

  Status UpdateTabletCheckpointForNextRequest(
      const TabletId& tablet_id, const GetChangesResponsePB* resp);

  Status AddRecordToVirtualWalPriorityQueue(
      const TabletId& tablet_id, TabletRecordPriorityQueue* sorted_records);

  Result<TabletRecordInfoPair> GetNextRecordToBeShipped(
      TabletRecordPriorityQueue* sorted_records, std::vector<TabletId>* empty_tablet_queues,
      const HostPort hostport, const CoarseTimePoint deadline);

  Result<TabletRecordInfoPair> FindConsistentRecord(
      TabletRecordPriorityQueue* sorted_records, std::vector<TabletId>* empty_tablet_queues,
      const HostPort hostport, const CoarseTimePoint deadline);

  Status InitLSNAndTxnIDGenerators();

  Result<uint64_t> GetRecordLSN(const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id);

  Result<uint32_t> GetRecordTxnID(
      const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id);

  Status AddEntryForBeginRecord(const RecordInfo& record_id_to_record);

  Status TruncateMetaMap(const uint64_t restart_lsn);

  Status UpdateSlotEntryInCDCState(
      const uint64_t confirmed_flush_lsn, const CommitRecordMetadata& record_metadata);

  void ResetCommitDecisionVariables();

  Status CreatePublicationRefreshTabletQueue();

  Status UpdatePubRefreshInfoInCDCState(bool update_pub_refresh_times);

  Status PushPublicationRefreshRecord(uint64_t pub_refresh_time, bool should_apply);

  Status PushNextPublicationRefreshRecord();

  std::set<uint64_t> ParsePubRefreshTimes(const std::string& pub_refresh_times_str);

  std::pair<uint64_t, bool> ParseLastDecidedPubRefreshTime(
      const std::string& last_decided_pub_refresh_time_str);

  std::string GetPubRefreshTimesString();

  std::string GetLastDecidedPubRefreshTimeString();

  std::vector<TabletId> GetTabletsForTable(const TableId& table_id);

  Status ValidateTablesToBeAddedPresentInStream(
      const std::unordered_set<TableId>& tables_to_be_added, const CoarseTimePoint deadline);

  std::string LogPrefix() const;

  CDCServiceImpl* cdc_service_;

  xrepl::StreamId stream_id_;

  uint64_t vwal_session_id_;

  std::string log_prefix_;

  std::unordered_set<TableId> publication_table_list_;

  // The primary requirement of this map is to efficiently retrieve the table_id of a tablet
  // whenever we plan to call GetTabletListToPoll on a tablet to get children tablets when it is
  // split. It is safe to get the first element of the corresponsding set. Note that, tablet entry
  // for colocated tables can have a set with more than 1 element. But this map will never be used
  // in the case of a tablet hosting colocated tables since such a tablet is never expected to
  // split.
  std::unordered_map<TabletId, std::unordered_set<TableId>> tablet_id_to_table_id_map_;

  const std::string kPublicationRefreshTabletID = "publication_refresh_tablet_id";

  // This dummy txn ID will be set in a pub refresh record, if the value of the flag
  // cdcsdk_enable_dynamic_table_support is true corresponding to the record.
  const std::string kDummyTransactionID = "dummy_transaction_id";

  // Tablet queues hold the records received from GetChanges RPC call on their respective tablets.
  std::unordered_map<TabletId, std::queue<std::shared_ptr<CDCSDKProtoRecordPB>>> tablet_queues_;

  // The next set of fields (last_seen_lsn_, last_seen_txn_id_, last_seen_unique_record_id_) hold
  // metadata (LSN/txnID/UniqueRecordId) about the record that has been last added to a
  // GetConsistentChanges response.
  uint64_t last_seen_lsn_;
  uint32_t last_seen_txn_id_;
  std::shared_ptr<CDCSDKUniqueRecordID> last_seen_unique_record_id_;
  TabletId last_shipped_record_tablet_id = "";

  // This will hold the restart_lsn value received in the UpdateAndPersistLSN RPC call. It will
  // initialised by the restart_lsn stores in the cdc_state's entry for slot.
  uint64_t last_received_restart_lsn;

  // Set to true when a BEGIN record is shipped. Reset to false after we ship the commit record for
  // the pg_txn.
  bool is_txn_in_progress = false;

  // Set to true when we are able to generate an LSN for the commit record held by
  // curr_active_txn_commit_record. Reset to false after we ship the commit record.
  bool should_ship_commit = false;

  // Holds the 1st commit record of a pg_txn. Reset to false after shipping the held commit record.
  std::shared_ptr<TabletRecordInfoPair> curr_active_txn_commit_record = nullptr;

  // This will hold the latest publication refresh time that was acknowledged, i.e
  // last_pub_refresh_time is the maximum value of pub refresh time that is less than restart commit
  // time.
  uint64_t last_pub_refresh_time;

  // This will hold the list of publication refresh times at which publication's tables list was
  // refreshed, but have not yet been acknowledged, i.e the list of applied pub refresh times with
  // value greater than or equal to the restart commit time.
  std::set<uint64_t> pub_refresh_times;

  // This will hold the commit time of the last pub refresh record pushed into the pub refresh
  // queue, along with the value of the flag cdcsdk_enable_dynamic_table_support corresponding to
  // the record. In other words, this holds the last decided pub refresh time along with the
  // decision whether to perform pub refresh at that time or not.
  std::pair<uint64_t, bool> last_decided_pub_refresh_time;

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
