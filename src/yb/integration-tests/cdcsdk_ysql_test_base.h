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
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include <boost/assign.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"

#include "yb/cdc/cdc_types.h"
#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/gutil/walltime.h"
#include "yb/rocksdb/db.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/stol_utils.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/thread.h"
#include "yb/tablet/tablet_types.pb.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::map;
using std::min;
using std::pair;
using std::string;
using std::vector;

DECLARE_int64(cdc_intent_retention_ms);
DECLARE_bool(enable_update_local_peer_min_index);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_bool(stream_truncate_record);
DECLARE_int32(cdc_state_checkpoint_update_interval_ms);
DECLARE_int32(update_metrics_interval_ms);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_uint64(aborted_intent_cleanup_ms);
DECLARE_int32(cdc_min_replicated_index_considered_stale_secs);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_bool(tablet_enable_ttl_file_filter);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_int32(cdc_max_stream_intent_records);
DECLARE_bool(enable_single_record_update);
DECLARE_bool(enable_truncate_cdcsdk_table);
DECLARE_bool(enable_load_balancing);
DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_int32(cdcsdk_table_processing_limit_per_run);
DECLARE_int32(cdc_snapshot_batch_size);
DECLARE_bool(TEST_cdc_snapshot_failure);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(ysql_packed_row_size_limit);
DECLARE_bool(cdc_populate_safepoint_record);
DECLARE_string(vmodule);
DECLARE_int32(ysql_num_shards_per_tserver);
DECLARE_int32(TEST_txn_participant_inject_latency_on_apply_update_txn_ms);
DECLARE_bool(cdc_enable_consistent_records);
DECLARE_bool(cdc_populate_end_markers_transactions);
DECLARE_uint64(cdc_stream_records_threshold_size_bytes);
DECLARE_int64(cdc_resolve_intent_lag_threshold_ms);
DECLARE_bool(enable_tablet_split_of_cdcsdk_streamed_tables);
DECLARE_bool(cdc_enable_postgres_replica_identity);
DECLARE_uint64(ysql_cdc_active_replication_slot_window_ms);
DECLARE_bool(enable_log_retention_by_op_idx);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_uint32(cdcsdk_tablet_not_of_interest_timeout_secs);
DECLARE_uint32(cdcsdk_retention_barrier_no_revision_interval_secs);
DECLARE_bool(TEST_cdcsdk_skip_processing_dynamic_table_addition);

namespace yb {

using client::YBClient;
using client::YBTableName;

using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;

using rpc::RpcController;

namespace cdc {

YB_DEFINE_ENUM(IntentCountCompareOption, (GreaterThanOrEqualTo)(GreaterThan)(EqualTo));
YB_DEFINE_ENUM(OpIdExpectedValue, (MaxOpId)(InvalidOpId)(ValidNonMaxOpId));

class CDCSDKYsqlTest : public CDCSDKTestBase {
 public:
  struct ExpectedRecord {
    int32_t key;
    int32_t value;
  };

  struct ExpectedRecordWithThreeColumns {
    int32_t key;
    int32_t value;
    int32_t value2;
  };

  struct VaryingExpectedRecord {
    uint32_t key;
    vector<std::pair<std::string, uint32_t>> val_vec;
  };

  struct CdcStateTableRow {
    OpId op_id = OpId::Max();
    int64_t cdc_sdk_latest_active_time = 0;
    HybridTime cdc_sdk_safe_time = HybridTime::kInvalid;
  };

  struct GetAllPendingChangesResponse {
    vector<CDCSDKProtoRecordPB> records;
    CDCSDKCheckpointPB checkpoint;
    int64 safe_hybrid_time = -1;
  };

  Result<string> GetUniverseId(PostgresMiniCluster* cluster);

  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;

  void StartYbAdminClient() {
    const auto addrs = AsString(test_cluster()->mini_master(0)->bound_rpc_addr());
    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        addrs, MonoDelta::FromSeconds(30) /* timeout */);
    ASSERT_OK(yb_admin_client_->Init());
  }

  void VerifyCdcStateMatches(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const uint64_t term, const uint64_t index);

  Status WriteRowsToTwoTables(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag,
      const char* const first_table_name, const char* const second_table_name,
      uint32_t num_cols = 2);

  void VerifyStreamDeletedFromCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      int timeout_secs = 120);

  Result<OpId> GetStreamCheckpointInCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id);

  void VerifyStreamCheckpointInCdcState(
      client::YBClient* client, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      OpIdExpectedValue op_id_expected_value = OpIdExpectedValue::ValidNonMaxOpId,
      int timeout_secs = 120);

  void VerifyTransactionParticipant(const TabletId& tablet_id, const OpId& opid);

  Status DropDB(PostgresMiniCluster* cluster);

  Status TruncateTable(PostgresMiniCluster* cluster, const std::vector<string>& table_ids);

  // The range is exclusive of end i.e. [start, end)
  Status WriteRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster,
      const vector<string>& optional_cols_name = {});

  // The range is exclusive of end i.e. [start, end)
  Status WriteRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster, uint32_t num_cols);

  void DropTable(PostgresMiniCluster* cluster, const char* table_name = kTableName);

  Status WriteRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t num_cols = 2,
      const char* const table_name = kTableName, const vector<string>& optional_cols_name = {},
      const bool trasaction_enabled = true);

  Status CreateTableWithoutPK(PostgresMiniCluster* cluster);

  Status WriteAndUpdateRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag,
      const std::multimap<uint32_t, uint32_t>& col_val_map, const std::string& table_id);

  Status CreateColocatedObjects(PostgresMiniCluster* cluster);

  Status AddColocatedTable(
      PostgresMiniCluster* cluster, const TableName& table_name,
      const std::string& table_group_name = "tg1");

  Status PopulateColocatedData(
      PostgresMiniCluster* cluster, int insert_count, bool transaction = false);

  Status WriteEnumsRows(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, const string& enum_suffix = "",
      string database_name = kNamespaceName, string table_name = kTableName,
      string schema_name = "public");

  Result<YBTableName> CreateCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets,
      const std::string& type_suffix = "");

  Status WriteCompositeRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster);

  Result<YBTableName> CreateNestedCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets,
      const std::string& type_suffix = "");

  Status WriteNestedCompositeRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster);

  Result<YBTableName> CreateArrayCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets,
      const std::string& type_suffix = "");

  Status WriteArrayCompositeRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster);

  Result<YBTableName> CreateRangeCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets,
      const std::string& type_suffix = "");

  Status WriteRangeCompositeRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster);

  Result<YBTableName> CreateRangeArrayCompositeTable(
      PostgresMiniCluster* cluster, const uint32_t num_tablets,
      const std::string& type_suffix = "");
  Status WriteRangeArrayCompositeRows(uint32_t start, uint32_t end, PostgresMiniCluster* cluster);

  Status UpdateRows(uint32_t key, uint32_t value, PostgresMiniCluster* cluster);

  Status UpdatePrimaryKey(uint32_t key, uint32_t value, PostgresMiniCluster* cluster);

  Status UpdateRows(
      uint32_t key, const std::map<std::string, uint32_t>& col_val_map,
      PostgresMiniCluster* cluster);

  Status UpdateRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t key,
      const std::map<std::string, uint32_t>& col_val_map1,
      const std::map<std::string, uint32_t>& col_val_map2, uint32_t num_cols);

  Status UpdateDeleteRowsHelper(
      uint32_t start, uint32_t end, PostgresMiniCluster* cluster, bool flag, uint32_t key,
      const std::map<std::string, uint32_t>& col_val_map, uint32_t num_cols);

  Status DeleteRows(uint32_t key, PostgresMiniCluster* cluster);

  Status SplitTablet(const TabletId& tablet_id, PostgresMiniCluster* cluster);

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> SetUpCluster();

  Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>>
  SetUpClusterMultiColumnUsecase(uint32_t num_cols);

  Result<GetChangesResponsePB> UpdateSnapshotDone(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId table_id = "");

  Result<GetChangesResponsePB> UpdateCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const GetChangesResponsePB* change_resp,
      const TableId table_id = "");

  Result<GetChangesResponsePB> UpdateCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& resp_checkpoint,
      const TableId table_id = "");

  std::unique_ptr<tserver::TabletServerAdminServiceProxy> GetTServerAdminProxy(
      const uint32_t tserver_index);

  Status GetIntentCounts(const uint32_t tserver_index, int64* num_intents);

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int tablet_idx = 0, int64 index = 0, int64 term = 0, std::string key = "",
      int32_t write_id = 0, int64 snapshot_time = 0, const TableId table_id = "",
      int64 safe_hybrid_time = -1, int32_t wal_segment_index = 0,
      const bool populate_checkpoint = true);

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const int tablet_idx = 0, int64 index = 0, int64 term = 0, std::string key = "",
      int32_t write_id = 0, int64 snapshot_time = 0);

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp, const int tablet_idx = 0, const TableId table_id = "",
      int64 safe_hybrid_time = -1, int32_t wal_segment_index = 0);

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id, const TabletId& tablet_id,
      const CDCSDKCheckpointPB& cp, const int tablet_idx = 0);

  void PrepareChangeRequestWithExplicitCheckpoint(
      GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* from_op_id, const CDCSDKCheckpointPB* explicit_checkpoint,
      const TableId table_id = "", const int tablet_idx = 0);

  void PrepareSetCheckpointRequest(
      SetCDCCheckpointRequestPB* set_checkpoint_req,
      const xrepl::StreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      const int tablet_idx,
      const OpId& op_id,
      bool initial_checkpoint,
      const uint64_t cdc_sdk_safe_time,
      bool bootstrap);

  bool IsDMLRecord(const CDCSDKProtoRecordPB& record) {
    return record.row_message().op() == RowMessage::INSERT
        || record.row_message().op() == RowMessage::UPDATE
        || record.row_message().op() == RowMessage::DELETE
        || record.row_message().op() == RowMessage::READ;
  }

  Result<int64> GetChangeRecordCount(
      const xrepl::StreamId& stream_id, const YBTableName& table,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      std::map<TabletId, CDCSDKCheckpointPB> tablet_to_checkpoint,
      const int64 expected_total_records, bool explicit_checkpointing_enabled = false,
      std::map<TabletId, std::vector<CDCSDKProtoRecordPB>> records = {});

  Result<SetCDCCheckpointResponsePB> SetCDCCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const OpId& op_id = OpId::Min(), const uint64_t cdc_sdk_safe_time = 0,
      bool initial_checkpoint = true, const int tablet_idx = 0, bool bootstrap = false);

  Result<std::vector<OpId>> GetCDCCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets);

  Result<GetCheckpointResponsePB> GetCDCSnapshotCheckpoint(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id = "");

  Result<CDCSDKCheckpointPB> GetCDCSDKSnapshotCheckpoint(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id = "");

  Result<GetTabletListToPollForCDCResponsePB> GetTabletListToPollForCDC(
      const xrepl::StreamId& stream_id, const TableId& table_id, const TabletId& tablet_id = "");

  void AssertKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column = false, const int32_t& value2 = 0);

  void AssertBeforeImageKeyValue(
      const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value,
      const bool& validate_third_column = false, const int32_t& value2 = 0);

  void AssertKeyValues(
      const CDCSDKProtoRecordPB& record, const int32_t& key,
      const vector<std::pair<std::string, uint32_t>>& col_val_vec);

  void EnableCDCServiceInAllTserver(uint32_t num_tservers);

  int FindTserversWithCacheHit(
      const xrepl::StreamId stream_id, const TabletId tablet_id, uint32_t num_tservers);

  void CheckRecord(
      const CDCSDKProtoRecordPB& record, CDCSDKYsqlTest::ExpectedRecord expected_records,
      uint32_t* count, const bool& validate_old_tuple = false,
      CDCSDKYsqlTest::ExpectedRecord expected_before_image_records = {});

  void CheckRecordWithThreeColumns(
      const CDCSDKProtoRecordPB& record,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_records, uint32_t* count,
      const bool& validate_old_tuple = false,
      CDCSDKYsqlTest::ExpectedRecordWithThreeColumns expected_before_image_records = {},
      const bool& validate_third_column = false, const bool is_nothing_record = false);

  void CheckCount(const uint32_t* expected_count, uint32_t* count);

  void CheckRecord(
      const CDCSDKProtoRecordPB& record, CDCSDKYsqlTest::VaryingExpectedRecord expected_records,
      uint32_t* count, uint32_t num_cols);

  Result<GetChangesResponsePB> GetChangesFromCDC(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0,
      const bool populate_checkpoint = true,
      const bool should_retry = true);

  Result<GetChangesResponsePB> GetChangesFromCDC(
      const xrepl::StreamId& stream_id,
      const TabletId& tablet_id,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0);

  Result<GetChangesResponsePB> GetChangesFromCDCWithoutRetry(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp);

  GetAllPendingChangesResponse GetAllPendingChangesWithRandomReqSafeTimeChanges(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0);

  GetAllPendingChangesResponse GetAllPendingChangesFromCdc(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr,
      int tablet_idx = 0,
      int64 safe_hybrid_time = -1,
      int wal_segment_index = 0);

  Result<GetChangesResponsePB> GetChangesFromCDCWithExplictCheckpoint(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* from_op_id = nullptr,
      const CDCSDKCheckpointPB* explicit_checkpoint = nullptr,
      const TableId& colocated_table_id = "",
      int tablet_idx = 0);

  bool DeleteCDCStream(const xrepl::StreamId& db_stream_id);

  Result<GetChangesResponsePB> GetChangesFromCDCSnapshot(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const TableId& colocated_table_id = "");

  void TestGetChanges(
      const uint32_t replication_factor, bool add_tables_without_primary_key = false);

  void TestIntentGarbageCollectionFlag(
      const uint32_t num_tservers,
      const bool set_flag_to_a_smaller_value,
      const uint32_t cdc_intent_retention_ms,
      CDCCheckpointType checkpoint_type,
      const bool extend_expiration = false);

  void TestSetCDCCheckpoint(CDCCheckpointType checkpoint_type);

  Result<GetChangesResponsePB> VerifyIfDDLRecordPresent(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      bool expect_ddl_record, bool is_first_call, const CDCSDKCheckpointPB* cp = nullptr);

  void PollForIntentCount(
      const int64& min_expected_num_intents, const uint32_t& tserver_index,
      const IntentCountCompareOption intentCountCompareOption, int64* num_intents);

  Result<GetCDCDBStreamInfoResponsePB> GetDBStreamInfo(const xrepl::StreamId db_stream_id);

  void VerifyTablesInStreamMetadata(
      const xrepl::StreamId& stream_id, const std::unordered_set<std::string>& expected_table_ids,
      const std::string& timeout_msg);

  Status ChangeLeaderOfTablet(size_t new_leader_index, const TabletId tablet_id);

  Status StepDownLeader(size_t new_leader_index, const TabletId tablet_id);

  Status CreateSnapshot(const NamespaceName& ns);

  int CountEntriesInDocDB(std::vector<tablet::TabletPeerPtr> peers, const std::string& table_id);

  Status TriggerCompaction(const TabletId tablet_id);

  Status CompactSystemTable();

  void GetTabletLeaderAndAnyFollowerIndex(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      size_t* leader_index, size_t* follower_index);

  void CompareExpirationTime(
      const TabletId& tablet_id, const CoarseTimePoint& prev_leader_expiry_time,
      size_t current_leader_idx, bool strictly_greater_than = false);

  Result<int64_t> GetLastActiveTimeFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client);

  Result<std::tuple<uint64, std::string>> GetSnapshotDetailsFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client);

  Result<int64_t> GetSafeHybridTimeFromCdcStateTable(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, client::YBClient* client);

  void ValidateColumnCounts(const GetChangesResponsePB& resp, uint32_t excepted_column_counts);

  void ValidateInsertCounts(const GetChangesResponsePB& resp, uint32_t excepted_insert_counts);

  void WaitUntilSplitIsSuccesful(
      const TabletId& tablet_id, const yb::client::YBTableName& table,
      const int expected_num_tablets = 2);

  void CheckTabletsInCDCStateTable(
      const std::unordered_set<TabletId> expected_tablet_ids,
      client::YBClient* client,
      const xrepl::StreamId& stream_id = xrepl::StreamId::Nil());

  Result<std::vector<TableId>> GetCDCStreamTableIds(const xrepl::StreamId& stream_id);

  Result<master::GetCDCStreamResponsePB> GetCDCStream(const xrepl::StreamId& stream_id);

  uint32_t GetTotalNumRecordsInTablet(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB* cp = nullptr);

  void CDCSDKAddColumnsWithImplictTransaction(bool packed_row);

  void CDCSDKAddColumnsWithExplictTransaction(bool packed_row);
  void CDCSDKDropColumnsWithRestartTServer(bool packed_row);
  void CDCSDKDropColumnsWithImplictTransaction(bool packed_row);

  void CDCSDKDropColumnsWithExplictTransaction(bool packed_row);
  void CDCSDKRenameColumnsWithImplictTransaction(bool packed_row);

  void CDCSDKRenameColumnsWithExplictTransaction(bool packed_row);

  void CDCSDKMultipleAlterWithRestartTServer(bool packed_row);
  void CDCSDKMultipleAlterWithTabletLeaderSwitch(bool packed_row);
  void CDCSDKAlterWithSysCatalogCompaction(bool packed_row);
  void CDCSDKIntentsBatchReadWithAlterAndTabletLeaderSwitch(bool packed_row);

  void WaitForCompaction(YBTableName table);
  void VerifySnapshotOnColocatedTables(
      xrepl::StreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      const CDCSDKCheckpointPB& snapshot_bootstrap_checkpoint, const TableId& req_table_id,
      const TableName& table_name, int64_t snapshot_records_per_table);

  Result<std::string> GetValueFromMap(const QLMapValuePB& map_value, const std::string& key);

  template <class T>
  Result<T> GetIntValueFromMap(const QLMapValuePB& map_value, const std::string& key);
  // Read the cdc_state table
  Result<CdcStateTableRow> ReadFromCdcStateTable(
      const xrepl::StreamId stream_id, const std::string& tablet_id);

  void UpdateRecordCount(const CDCSDKProtoRecordPB& record, int* record_count);

  void CheckRecordsConsistency(const std::vector<CDCSDKProtoRecordPB>& records);

  void GetRecordsAndSplitCount(
      const xrepl::StreamId& stream_id, const TabletId& tablet_id, const TableId& table_id,
      CDCCheckpointType checkpoint_type,
      int* record_count, int* total_records, int* total_splits);

  void PerformSingleAndMultiShardInserts(
      const int& num_batches, const int& inserts_per_batch, int apply_update_latency = 0,
      const int& start_index = 0);

  void PerformSingleAndMultiShardQueries(
      const int& num_batches, const int& queries_per_batch, const string& query,
      int apply_update_latency = 0, const int& start_index = 0);

  OpId GetHistoricalMaxOpId(
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& tablet_idx = 0);

  TableId GetColocatedTableId(const std::string& req_table_name);

  void AssertSafeTimeAsExpectedInTabletPeers(
      const TabletId& tablet_id, const HybridTime expected_safe_time);

  void AssertSafeTimeAsExpectedInTabletPeersForConsistentSnapshot(
      const TabletId& tablet_id, const HybridTime expected_safe_time);

  Status WaitForGetChangesToFetchRecords(
      GetChangesResponsePB* get_changes_resp, const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& expected_count, bool is_explicit_checkpoint = false,
      const CDCSDKCheckpointPB* cp = nullptr, const int& tablet_idx = 0,
      const int64& safe_hybrid_time = -1, const int& wal_segment_index = 0,
      const double& timeout_secs = 5);

  Status WaitForGetChangesToFetchRecordsAcrossTablets(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const int& expected_count, bool is_explicit_checkpoint = false,
      const CDCSDKCheckpointPB* cp = nullptr, const int64& safe_hybrid_time = -1,
      const int& wal_segment_index = 0, const double& timeout_secs = 5);

  Status XreplValidateSplitCandidateTable(const TableId& table);

  void LogRetentionBarrierAndRelatedDetails(const GetCheckpointResponsePB& checkpoint_result,
                                            const tablet::TabletPeerPtr& tablet_peer);

  void LogRetentionBarrierDetails(const tablet::TabletPeerPtr& tablet_peer);

  void ConsumeSnapshotAndVerifyRecords(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp_resp,
      const CDCSDKYsqlTest::ExpectedRecord* expected_records,
      const uint32_t* expected_count,
      uint32_t* count);

  Result<uint32_t> ConsumeSnapshotAndVerifyCounts(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp_resp,
      GetChangesResponsePB* change_resp_updated);

  Result<uint32_t> ConsumeInsertsAndVerifyCounts(
      const xrepl::StreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const GetChangesResponsePB& change_resp_after_snapshot);

  void TestCDCLagMetric(CDCCheckpointType checkpoint_type);

  void TestMultipleStreamOnSameTablet(CDCCheckpointType checkpoint_type);

  void TestMultipleActiveStreamOnSameTablet(CDCCheckpointType checkpoint_type);

  void TestActiveAndInactiveStreamOnSameTablet(CDCCheckpointType checkpoint_type);

  void TestCheckpointPersistencyAllNodesRestart(CDCCheckpointType checkpoint_type);

  void TestIntentCountPersistencyAllNodesRestart(CDCCheckpointType checkpoint_type);

  void TestHighIntentCountPersistencyAllNodesRestart(CDCCheckpointType checkpoint_type);

  void TestIntentCountPersistencyBootstrap(CDCCheckpointType checkpoint_type);

  void ConsumeSnapshotAndPerformDML(
      xrepl::StreamId stream_id, YBTableName table,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets,
      CDCSDKCheckpointPB checkpoint, GetChangesResponsePB* change_resp);
};

}  // namespace cdc
}  // namespace yb
