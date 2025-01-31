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

#include "yb/tserver/xcluster_output_client.h"

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_rpc.h"

#include "yb/common/wire_protocol.h"
#include "yb/common/xcluster_util.h"

#include "yb/client/client_fwd.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/client_utils.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/master/master_replication.pb.h"

#include "yb/rocksdb/rate_limiter.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/xcluster_poller.h"
#include "yb/tserver/xcluster_write_interface.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"

DECLARE_int32(cdc_write_rpc_timeout_ms);

DEFINE_test_flag(bool, xcluster_force_remote_tserver, false,
    "Avoid local tserver apply optimization for xCluster and force remote RPCs.");

DEFINE_RUNTIME_bool(xcluster_enable_packed_rows_support, true,
    "Enables rewriting of packed rows with xcluster consumer schema version");
TAG_FLAG(xcluster_enable_packed_rows_support, advanced);

DECLARE_int32(cdc_read_rpc_timeout_ms);

DEFINE_test_flag(bool, xcluster_consumer_fail_after_process_split_op, false,
    "Whether or not to fail after processing a replicated split_op on the consumer.");

DECLARE_bool(TEST_running_test);

#define HANDLE_ERROR_AND_RETURN_IF_NOT_OK(status) \
  do { \
    auto&& _s = (status); \
    if (!_s.ok()) { \
      HandleError(std::move(_s)); \
      return; \
    } \
  } while (0)

#define ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN \
  std::lock_guard l(lock_); \
  if (IsOffline()) { \
    VLOG_WITH_PREFIX(1) << "xCluster output client is shutting down."; \
    return; \
  } \
  do { \
  } while (false)

#define ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN_STATUS \
  std::lock_guard l(lock_); \
  SCHECK_FORMAT(!IsOffline(), Aborted, "$0$1", LogPrefix(), "xCluster output client went offline")

#define STORE_REPLICATION_ERROR_AND_HANDLE_ERROR(replication_error, status) \
  do { \
    { \
      ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN; \
      xcluster_poller_->StoreReplicationError(replication_error); \
    } \
    HandleError(status); \
  } while (0)

using namespace std::placeholders;

namespace yb {
namespace tserver {

XClusterOutputClient::XClusterOutputClient(
    XClusterPoller* xcluster_poller, const xcluster::ConsumerTabletInfo& consumer_tablet_info,
    const xcluster::ProducerTabletInfo& producer_tablet_info, client::YBClient& local_client,
    ThreadPool* thread_pool, rpc::Rpcs* rpcs, bool use_local_tserver, bool is_automatic_mode,
    rocksdb::RateLimiter* rate_limiter)
    : XClusterAsyncExecutor(thread_pool, local_client.messenger(), rpcs),
      xcluster_poller_(xcluster_poller),
      consumer_tablet_info_(consumer_tablet_info),
      producer_tablet_info_(producer_tablet_info),
      local_client_(local_client),
      use_local_tserver_(use_local_tserver),
      is_automatic_mode_(is_automatic_mode),
      all_tablets_result_(STATUS(Uninitialized, "Result has not been initialized.")),
      rate_limiter_(rate_limiter) {
  const auto& consumer_table_id = consumer_tablet_info.table_id;
  if (xcluster::IsSequencesDataAlias(consumer_table_id)) {
    auto namespace_id = xcluster::GetReplicationNamespaceBelongsTo(consumer_table_id);
    if (!namespace_id) {
      MarkFailed(Format("Malformed namespace ID in sequences_data alias: $0", consumer_table_id));
    } else {
      auto oid = GetPgsqlDatabaseOid(*namespace_id);
      if (!oid) {
        MarkFailed(Format("Malformed namespace ID in sequences_data alias: $0", consumer_table_id));
      } else {
        db_oid_write_sequences_to_ = *oid;
      }
    }
  }
}

XClusterOutputClient::~XClusterOutputClient() {
  VLOG_WITH_PREFIX(1) << "Destroying XClusterOutputClient";
  DCHECK(IsOffline());
}

void XClusterOutputClient::StartShutdown() {
  DCHECK(!IsOffline());
  shutdown_ = true;

  XClusterAsyncExecutor::StartShutdown();
}

void XClusterOutputClient::CompleteShutdown() {
  // Wait for tasks that started before shutdown to complete. We release mutex as new tasks acquire
  // it before checking for shutdown.
  { std::lock_guard l(lock_); }

  XClusterAsyncExecutor::CompleteShutdown();
}

void XClusterOutputClient::MarkFailed(const std::string& reason, const Status& status) {
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
  MarkFailedUnlocked(reason, status);
}

void XClusterOutputClient::MarkFailedUnlocked(const std::string& reason, const Status& status) {
  LOG_WITH_PREFIX(WARNING) << "xCluster Output client failed as " << reason;
  xcluster_poller_->MarkFailed(reason, status);
}

void XClusterOutputClient::UpdateSchemaVersionMappings(
    const cdc::XClusterSchemaVersionMap& schema_version_map,
    const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map) {
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  // Incoming schema version map is typically a superset of what we have so merge should be ok.
  for(const auto& [producer_version, consumer_version] : schema_version_map) {
    schema_versions_[producer_version] = consumer_version;
  }

  for(const auto& [colocationid, schema_versions] : colocated_schema_version_map) {
    for(const auto& [producer_version, consumer_version] : schema_versions) {
      colocated_schema_version_map_[colocationid][producer_version] = consumer_version;
    }
  }
}

void XClusterOutputClient::ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> poller_resp) {
  // ApplyChanges is called in a single threaded manner.
  // For all the changes in GetChangesResponsePB, we first fan out and find the tablet for
  // every record key.
  // Then we apply the records in the same order in which we received them.
  // Once all changes have been applied (successfully or not), we invoke the callback which will
  // then either poll for next set of changes (in case of successful application) or will try to
  // re-apply.
  DCHECK(poller_resp->has_checkpoint());

  // Init class variables that threads will use.
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
    DCHECK(consensus::OpIdEquals(op_id_, consensus::MinimumOpId()));
    op_id_ = poller_resp->checkpoint().op_id();
    error_status_ = Status::OK();
    done_processing_ = false;
    processed_record_count_ = 0;
    record_count_ = poller_resp->records_size();
    ResetWriteInterface(&write_strategy_);
  }

  get_changes_resp_ = std::move(poller_resp);

  // Ensure we have records.
  if (get_changes_resp_->records_size() == 0) {
    HandleResponse();
    return;
  }

  // Ensure we have a connection to the consumer table cached.
  if (!table_) {
    auto stripped_table_id =
        xcluster::StripSequencesDataAliasIfPresent(consumer_tablet_info_.table_id);
    HANDLE_ERROR_AND_RETURN_IF_NOT_OK(
        local_client_.OpenTable(stripped_table_id, &table_));
  }

  timeout_ms_ = MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms);
  // Using this future as a barrier to get all the tablets before processing.  Ordered iteration
  // matters: we need to ensure that each record is handled sequentially.
  all_tablets_result_ =
      local_client_.LookupAllTabletsFuture(table_, CoarseMonoClock::now() + timeout_ms_).get();

  HANDLE_ERROR_AND_RETURN_IF_NOT_OK(ProcessChangesStartingFromIndex(0));
}

Status XClusterOutputClient::ProcessChangesStartingFromIndex(int start) {
  bool processed_write_record = false;
  auto records_size = get_changes_resp_->records_size();
  for (int i = start; i < records_size; i++) {
    // All KV-pairs within a single CDC record will be for the same row.
    // key(0).key() will contain the hash code for that row. We use this to lookup the tablet.
    const auto& record = get_changes_resp_->records(i);

    if (IsValidMetaOp(record)) {
      RSTATUS_DCHECK(
          !IsSequencesDataTablet(), IllegalState,
          "WAL of a sequences_data tablet unexpectedly contains a meta op");
      if (processed_write_record) {
        // We have existing write operations, so flush them first (see WriteCDCRecordDone and
        // SendTransactionUpdates).
        break;
      }
      // No other records to process, so we can process the meta ops.
      bool done = VERIFY_RESULT(ProcessMetaOp(record));
      if (done) {
        // Currently, we expect Producers to send any terminating ops last.
        DCHECK(i == records_size - 1);
        HandleResponse();
        return Status::OK();
      }

      // If a meta_op has not completed processing and sent any RPCs,
      // then we need to wait until those RPCs are sent out and responses
      // are received, so we break out and wait for callbacks to continue processing.
      break;
    } else if (UseLocalTserver()) {
      RSTATUS_DCHECK(
          !IsSequencesDataTablet(), IllegalState,
          "Incorrectly attempting to use local TServer optimization for a sequences_data "
          "tablet");
      RETURN_NOT_OK(ProcessRecordForLocalTablet(record));
    } else {
      switch (record.operation()) {
        case cdc::CDCRecordPB::APPLY:
          RSTATUS_DCHECK(
              !IsSequencesDataTablet(), IllegalState,
              "WAL of a sequences_data tablet unexpectedly contains an apply op");
          RETURN_NOT_OK(ProcessRecordForTabletRange(record, all_tablets_result_));
          break;
        default: {
          const cdc::CDCRecordPB* record_to_process = &record;
          std::unique_ptr<cdc::CDCRecordPB> transformed_record;
          if (IsSequencesDataTablet()) {
            transformed_record = std::make_unique<cdc::CDCRecordPB>(
                VERIFY_RESULT(TransformSequencesDataRecord(record)));
            record_to_process = transformed_record.get();
          }
          const std::string& partition_key = record_to_process->key(0).key();
          auto tablet_result = local_client_
                                   .LookupTabletByKeyFuture(
                                       table_, partition_key, CoarseMonoClock::now() + timeout_ms_)
                                   .get();
          RETURN_NOT_OK(ProcessRecordForTablet(*record_to_process, tablet_result));
          break;
        }
      }
    }
    processed_write_record = true;
  }

  if (processed_write_record) {
    return SendUserTableWrites();
  }
  return Status::OK();
}

Result<cdc::CDCRecordPB> XClusterOutputClient::TransformSequencesDataRecord(
    const cdc::CDCRecordPB& record) {
  RSTATUS_DCHECK(
      IsSequencesDataTablet(), IllegalState,
      "Preconditioned violated: TransformSequencesDataRecord called on a non-sequences_data "
      "tablet");
  auto new_record = record;
  uint16_t hash = 0;
  for (auto& change : *new_record.mutable_changes()) {
    // Decode hash columns based on known sequences_data schema.
    Slice sub_doc_key = change.key();
    dockv::SubDocKey decoded_key;
    RETURN_NOT_OK(decoded_key.DecodeFrom(&sub_doc_key, dockv::HybridTimeRequired::kFalse));
    auto& hashed_group = decoded_key.doc_key().hashed_group();
    SCHECK_EQ(
        hashed_group.size(), 2, IllegalState,
        Format("$0sequences_data table expected to have two primary hash columns", LogPrefix()));
    SCHECK_FORMAT(
        hashed_group[0].IsInt64(), IllegalState,
        "$0sequences_data table expected to have first primary column of type Int64; actual type: "
        "$1",
        LogPrefix(), hashed_group[0].type());
    SCHECK_FORMAT(
        hashed_group[1].IsInt64(), IllegalState,
        "$0sequences_data table expected to have secondary primary column of type Int64; actual "
        "type: $1",
        LogPrefix(), hashed_group[1].type());

    // Update DB OID field.
    hashed_group[0] = dockv::KeyEntryValue::Int64(*db_oid_write_sequences_to_);

    // Compute new hash using hashed_group values.
    std::string data_for_hashing;
    QLValue value;
    value.set_int64_value(hashed_group[0].GetInt64());
    AppendToKey(value, &data_for_hashing);
    value.set_int64_value(hashed_group[1].GetInt64());
    AppendToKey(value, &data_for_hashing);
    uint16_t new_hash = YBPartition::HashColumnCompoundValue(data_for_hashing);
    DCHECK(hash == 0 || hash == new_hash);
    hash = new_hash;
    decoded_key.doc_key().set_hash(hash);

    // Update change key with the changes.  Do not change the value part.
    change.set_key(decoded_key.Encode().ToStringBuffer());
  }

  // Finally, we need to set the record's overall key to the new hash.
  new_record.clear_key();
  auto kv_pair = new_record.add_key();
  kv_pair->set_key(dockv::PartitionSchema::EncodeMultiColumnHashValue(hash));

  return new_record;
}

Status XClusterOutputClient::SendUserTableWrites() {
  // Send out the buffered writes.
  std::unique_ptr<WriteRequestPB> write_request;
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN_STATUS;
    write_request = write_strategy_->FetchNextRequest();
  }
  if (!write_request) {
    LOG_WITH_PREFIX(WARNING) << "Expected to find a write_request but were unable to";
    return STATUS(IllegalState, "Could not find a write request to send");
  }
  SendNextCDCWriteToTablet(std::move(write_request));
  return Status::OK();
}

bool XClusterOutputClient::UseLocalTserver() {
  return use_local_tserver_ && !FLAGS_TEST_xcluster_force_remote_tserver;
}

bool XClusterOutputClient::IsSequencesDataTablet() {
  return db_oid_write_sequences_to_.has_value();
}

Result<cdc::XClusterSchemaVersionMap> XClusterOutputClient::GetSchemaVersionMap(
    const cdc::CDCRecordPB& record) {
  cdc::XClusterSchemaVersionMap* cached_schema_versions = nullptr;
  auto& kv_pair = record.changes(0);
  auto decoder = dockv::DocKeyDecoder(kv_pair.key());
  ColocationId colocationId = kColocationIdNotSet;
  if (VERIFY_RESULT(decoder.DecodeColocationId(&colocationId))) {
    // Can't find the table, so we most likely need an update
    // to the consumer registry, so return an error and fail the apply.
    cached_schema_versions = FindOrNull(colocated_schema_version_map_, colocationId);
    SCHECK(cached_schema_versions, NotFound, Format("ColocationId $0 not found.", colocationId));
  } else {
    cached_schema_versions = &schema_versions_;
  }

  if (PREDICT_FALSE(VLOG_IS_ON(3))) {
    for (const auto& [producer_schema_version, consumer_schema_version] : *cached_schema_versions) {
      VLOG_WITH_PREFIX(3) << Format(
          "ColocationId:$0 Producer Schema Version:$1, Consumer Schema Version:$2", colocationId,
          producer_schema_version, consumer_schema_version);
    }
  }

  return *cached_schema_versions;
}

Status XClusterOutputClient::ProcessRecord(
    const std::vector<std::string>& tablet_ids, const cdc::CDCRecordPB& record) {
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN_STATUS;
  for (const auto& tablet_id : tablet_ids) {
    SCHECK(!IsOffline(), Aborted, "$0$1", LogPrefix(), "xCluster output client went offline");

    cdc::XClusterSchemaVersionMap schema_versions_map;
    if (PREDICT_TRUE(FLAGS_xcluster_enable_packed_rows_support) &&
        !record.changes().empty() &&
        (record.operation() == cdc::CDCRecordPB::WRITE ||
         record.operation() == cdc::CDCRecordPB::DELETE)) {
        schema_versions_map = VERIFY_RESULT(GetSchemaVersionMap(record));
    }

    auto status = write_strategy_->ProcessRecord(
        ProcessRecordInfo{.tablet_id = tablet_id, .schema_versions_map = schema_versions_map},
        record);
    if (!status.ok()) {
      error_status_ = status;
      return status;
    }
  }
  IncProcessedRecordCount();
  return Status::OK();
}

Status XClusterOutputClient::ProcessRecordForTablet(
    const cdc::CDCRecordPB& record, const Result<client::internal::RemoteTabletPtr>& tablet) {
  RETURN_NOT_OK(tablet);
  return ProcessRecord({tablet->get()->tablet_id()}, record);
}

Status XClusterOutputClient::ProcessRecordForTabletRange(
    const cdc::CDCRecordPB& record,
    const Result<std::vector<client::internal::RemoteTabletPtr>>& tablets) {
  RETURN_NOT_OK(tablets);

  auto filtered_tablets = client::FilterTabletsByKeyRange(
      *tablets, record.partition().partition_key_start(), record.partition().partition_key_end());
  if (filtered_tablets.empty()) {
    table_->MarkPartitionsAsStale();
    return STATUS(TryAgain, "No tablets found for key range, refreshing partitions to try again.");
  }
  auto tablet_ids = std::vector<std::string>(filtered_tablets.size());
  std::transform(filtered_tablets.begin(), filtered_tablets.end(), tablet_ids.begin(),
                 [&](const auto& tablet_ptr) { return tablet_ptr->tablet_id(); });
  return ProcessRecord(tablet_ids, record);
}

Status XClusterOutputClient::ProcessRecordForLocalTablet(const cdc::CDCRecordPB& record) {
  return ProcessRecord({consumer_tablet_info_.tablet_id}, record);
}

bool XClusterOutputClient::IsValidMetaOp(const cdc::CDCRecordPB& record) {
  auto type = record.operation();
  return type == cdc::CDCRecordPB::SPLIT_OP || type == cdc::CDCRecordPB::CHANGE_METADATA;
}

Result<bool> XClusterOutputClient::ProcessChangeMetadataOp(const cdc::CDCRecordPB& record) {
  YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 300)
      << " Processing Change Metadata Op :" << record.DebugString();
  if (record.change_metadata_request().has_remove_table_id() ||
      !record.change_metadata_request().add_multiple_tables().empty()) {
    // TODO (#16557): Support remove_table_id() for colocated tables / tablegroups.
    LOG_WITH_PREFIX(INFO)
        << "Ignoring change metadata request to add multiple/remove tables to tablet : "
        << producer_tablet_info_.tablet_id;
    return true;
  }

  if (!record.change_metadata_request().has_schema() &&
      !record.change_metadata_request().has_add_table()) {
    LOG_WITH_PREFIX(INFO) << "Ignoring change metadata request for tablet : "
                          << producer_tablet_info_.tablet_id
                          << " as it does not contain any schema. ";
    return true;
  }

  auto schema = record.change_metadata_request().has_add_table() ?
                record.change_metadata_request().add_table().schema() :
                record.change_metadata_request().schema();
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN_STATUS;

    // If this is a request to add a table and we have already have a mapping for the producer
    // schema version, we can safely ignore the add table as the table and mapping was present at
    // setup_replication and we are just receiving the change metadata op for the new table
    if (record.change_metadata_request().has_add_table()) {
      cdc::XClusterSchemaVersionMap* cached_schema_versions = &schema_versions_;
      if (schema.has_colocated_table_id()) {
        cached_schema_versions =
            FindOrNull(colocated_schema_version_map_, schema.colocated_table_id().colocation_id());
      }

      if (cached_schema_versions &&
          cached_schema_versions->contains(record.change_metadata_request().schema_version())) {
        LOG_WITH_PREFIX(INFO) << Format(
            "Ignoring change metadata request with schema $0 for tablet $1 as mapping from"
            "producer-consumer schema version already exists",
            schema.DebugString(), producer_tablet_info_.tablet_id);
        return true;
      }
    }

    // Cache the producer schema version and colocation id if present
    producer_schema_version_ = record.change_metadata_request().schema_version();
    if (schema.has_colocated_table_id()) {
      colocation_id_ = schema.colocated_table_id().colocation_id();
    }
  }

  // Find the compatible consumer schema version for the incoming change metadata record.
  tserver::GetCompatibleSchemaVersionRequestPB req;
  req.set_tablet_id(consumer_tablet_info_.tablet_id);
  req.mutable_schema()->CopyFrom(schema);
  UpdateSchemaVersionMapping(&req);

  // Since we started an Rpc, do not complete the processing of the record
  // as we need to wait for the response.
  return false;
}

Result<bool> XClusterOutputClient::ProcessMetaOp(const cdc::CDCRecordPB& record) {
  if (record.operation() == cdc::CDCRecordPB::SPLIT_OP) {
    // Construct and send the update request.
    master::ProducerSplitTabletInfoPB split_info;
    split_info.set_tablet_id(record.split_tablet_request().tablet_id());
    split_info.set_new_tablet1_id(record.split_tablet_request().new_tablet1_id());
    split_info.set_new_tablet2_id(record.split_tablet_request().new_tablet2_id());
    split_info.set_split_encoded_key(record.split_tablet_request().split_encoded_key());
    split_info.set_split_partition_key(record.split_tablet_request().split_partition_key());

    if (PREDICT_FALSE(FLAGS_TEST_xcluster_consumer_fail_after_process_split_op)) {
      return STATUS(
          InternalError, "Fail due to FLAGS_TEST_xcluster_consumer_fail_after_process_split_op");
    }

    RETURN_NOT_OK(local_client_.UpdateConsumerOnProducerSplit(
        producer_tablet_info_.replication_group_id, producer_tablet_info_.stream_id, split_info));
  } else if (record.operation() == cdc::CDCRecordPB::CHANGE_METADATA) {
    if (!VERIFY_RESULT(ProcessChangeMetadataOp(record))) {
      return false;
    }
  }

  // Increment processed records, and check for completion.
  bool done = false;
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN_STATUS;
    done = IncProcessedRecordCount();
  }
  return done;
}

void XClusterOutputClient::SendNextCDCWriteToTablet(std::unique_ptr<WriteRequestPB> write_request) {
  LOG_SLOW_EXECUTION_EVERY_N_SECS(INFO, 1 /* n_secs */, 100 /* max_expected_millis */,
      Format("Rate limiting write request for tablet $0", write_request->tablet_id())) {
    rate_limiter_->Request(write_request->ByteSizeLong(), IOPriority::kHigh);
  };
  // TODO: This should be parallelized for better performance with M:N setups.
  auto deadline =
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms);

  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  auto handle = rpcs_->Prepare();
  if (handle == rpcs_->InvalidHandle()) {
    DCHECK(IsOffline());
    MarkFailedUnlocked("we could not prepare rpc as it is shutting down");
    return;
  }

  // Send in nullptr for RemoteTablet since cdc rpc now gets the tablet_id from the write request.
  *handle = rpc::xcluster::CreateXClusterWriteRpc(
      deadline, nullptr /* RemoteTablet */, table_, &local_client_, write_request.get(),
      [weak_ptr = weak_from_this(), this, handle, rpcs = rpcs_](
          const Status& status, WriteResponsePB&& resp) {
        RpcCallback(
            weak_ptr, handle, rpcs,
            BIND_FUNCTION_AND_ARGS(
                XClusterOutputClient::DoWriteCDCRecordDone, status, std::move(resp)));
      },
      UseLocalTserver());
  SetHandleAndSendRpc(handle);
}

void XClusterOutputClient::UpdateSchemaVersionMapping(
    tserver::GetCompatibleSchemaVersionRequestPB* req) {
  auto deadline =
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms);

  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  auto handle = rpcs_->Prepare();
  if (handle == rpcs_->InvalidHandle()) {
    DCHECK(IsOffline());
    MarkFailedUnlocked("we could not prepare rpc as it is shutting down");
    return;
  }

  // Send in nullptr for RemoteTablet since cdc rpc now gets the tablet_id from the write
  // request.
  *handle = rpc::xcluster::CreateGetCompatibleSchemaVersionRpc(
      deadline, nullptr, &local_client_, req,
      [weak_ptr = weak_from_this(), this, handle, rpcs = rpcs_](
          const Status& status, const GetCompatibleSchemaVersionRequestPB& req,
          GetCompatibleSchemaVersionResponsePB&& resp) {
        RpcCallback(
            weak_ptr, handle, rpcs,
            BIND_FUNCTION_AND_ARGS(
                XClusterOutputClient::DoSchemaVersionCheckDone, status, std::move(req),
                std::move(resp)));
      },
      UseLocalTserver());
  SetHandleAndSendRpc(handle);
}

void XClusterOutputClient::DoSchemaVersionCheckDone(
    const Status& status, const GetCompatibleSchemaVersionRequestPB& req,
    const GetCompatibleSchemaVersionResponsePB& resp) {
  SchemaVersion producer_schema_version = cdc::kInvalidSchemaVersion;
  ColocationId colocation_id = 0;
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

    producer_schema_version = producer_schema_version_;
    colocation_id = colocation_id_;
  }

  if (!status.ok()) {
    ReplicationErrorPb replication_error = ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH;
    if (resp.error().code() == TabletServerErrorPB::TABLET_NOT_FOUND) {
      replication_error = ReplicationErrorPb::REPLICATION_MISSING_TABLE;
    }

    auto msg = Format(
        "XCluster schema mismatch. No matching schema for producer schema $0 with version $1",
        req.schema().DebugString(), producer_schema_version);
    LOG_WITH_PREFIX(WARNING) << msg << ": " << status;
    if (resp.error().code() == TabletServerErrorPB::MISMATCHED_SCHEMA) {
      // For automatic DDL replication, we need to insert this packing schema into the historical
      // set of schemas - this will allow us to correctly map packing schema versions until the
      // replicated DDL is run via ddl_queue.
      if (is_automatic_mode_) {
        // Also pass the latest schema version so that we don't repeatedly insert the same schema.
        if (!resp.has_latest_schema_version()) {
          STORE_REPLICATION_ERROR_AND_HANDLE_ERROR(
              ReplicationErrorPb::REPLICATION_SYSTEM_ERROR,
              STATUS(IllegalState, "Missing latest schema version in response"));
          return;
        }
        std::optional<ColocationId> colocation_id_opt = std::nullopt;
        if (req.schema().has_colocated_table_id() &&
            req.schema().colocated_table_id().colocation_id() != kColocationIdNotSet) {
          colocation_id_opt = std::make_optional(req.schema().colocated_table_id().colocation_id());
        }
        auto s = client::XClusterClient(local_client_)
                     .InsertPackedSchemaForXClusterTarget(
                         consumer_tablet_info_.table_id, req.schema(), resp.latest_schema_version(),
                         colocation_id_opt);

        if (s.ok()) {
          VLOG_WITH_PREFIX(2) << "Inserted schema for automatic DDL replication: "
                              << req.schema().ShortDebugString();
          // Can now retry creating the schema version mapping.
          tserver::GetCompatibleSchemaVersionRequestPB new_req(req);
          UpdateSchemaVersionMapping(&new_req);
          return;
        }

        LOG_WITH_PREFIX(WARNING) << "Failed to insert schema for automatic DDL replication: " << s;
        STORE_REPLICATION_ERROR_AND_HANDLE_ERROR(ReplicationErrorPb::REPLICATION_SYSTEM_ERROR, s);
        return;
      }
    }
    STORE_REPLICATION_ERROR_AND_HANDLE_ERROR(replication_error, status);
    return;
  }

  if (resp.has_error()) {
    HandleError(StatusFromPB(resp.error().status()));
    return;
  }

  // Compatible schema version found, update master with the mapping and update local cache also
  // as there could be some delay in propagation from master to all the
  // XClusterConsumer/XClusterPollers.
  tablet::ChangeMetadataRequestPB meta;
  meta.set_tablet_id(producer_tablet_info_.tablet_id);
  master::UpdateConsumerOnProducerMetadataResponsePB response;
  Status s = local_client_.UpdateConsumerOnProducerMetadata(
      producer_tablet_info_.replication_group_id, producer_tablet_info_.stream_id, meta,
      colocation_id, producer_schema_version, resp.compatible_schema_version(), &response);
  if (!s.ok()) {
    HandleError(s);
    return;
  }

  {
    // Since we are done processing the meta op, we can increment the processed count.
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
    // Update the local cache with the response. This should happen automatically on the heartbeat
    // as well when the poller gets updated, but we opportunistically update here.
    cdc::XClusterSchemaVersionMap* schema_version_map = nullptr;
    if (colocation_id != kColocationIdNotSet) {
      schema_version_map = &colocated_schema_version_map_[colocation_id];
    } else {
      schema_version_map = &schema_versions_;
    }
    // Log the response from master.
    LOG_WITH_FUNC(INFO) << Format(
        "Received response: $0 for schema: $1 on producer tablet $2",
        response.DebugString(), req.schema().DebugString(), producer_tablet_info_.tablet_id);
    DCHECK(schema_version_map);
    auto resp_schema_versions = response.schema_versions();
    (*schema_version_map)[resp_schema_versions.current_producer_schema_version()] =
        resp_schema_versions.current_consumer_schema_version();

    // Update the old producer schema version, only if it is not the same as
    // current producer schema version.
    if (resp_schema_versions.old_producer_schema_version() !=
        resp_schema_versions.current_producer_schema_version()) {
      (*schema_version_map)[resp_schema_versions.old_producer_schema_version()] =
          resp_schema_versions.old_consumer_schema_version();
    }

    IncProcessedRecordCount();
    xcluster_poller_->ClearReplicationError();
  }

  // Clear out any replication errors.

  // MetaOps should be the last ones in a batch, so we should be done at this point.
  HandleResponse();
}

void XClusterOutputClient::DoWriteCDCRecordDone(
    const Status& status, const WriteResponsePB& response) {
  if (!status.ok()) {
    HandleError(status);
    return;
  } else if (response.has_error()) {
    HandleError(StatusFromPB(response.error().status()));
    return;
  }

  // See if we need to handle any more writes.
  std::unique_ptr<WriteRequestPB> write_request;
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
    if (FLAGS_TEST_running_test) {
      xcluster_poller_->TEST_IncrementNumSuccessfulWriteRpcs();
    }
    write_request = write_strategy_->FetchNextRequest();
  }

  if (write_request) {
    SendNextCDCWriteToTablet(std::move(write_request));
  } else {
    // We may still have more records to process (in case of ddls/master requests).
    int next_record = 0;
    {
      SharedLock<decltype(lock_)> l(lock_);
      if (processed_record_count_ < record_count_) {
        // processed_record_count_ is 1-based, so no need to add 1 to get next record.
        next_record = processed_record_count_;
      }
    }
    if (next_record > 0) {
      // Process rest of the records.
      Status s = ProcessChangesStartingFromIndex(next_record);
      if (!s.ok()) {
        HandleError(s);
      }
    } else {
      // Last record, return response to caller.
      HandleResponse();
    }
  }
}

void XClusterOutputClient::HandleError(const Status& s) {
  if (s.IsTryAgain()) {
    LOG_WITH_PREFIX(WARNING) << "Retrying applying replicated record for consumer tablet: "
                             << consumer_tablet_info_.tablet_id << ", reason: " << s;
  } else {
    LOG_WITH_PREFIX(ERROR) << "Error while applying replicated record: " << s
                           << ", consumer tablet: " << consumer_tablet_info_.tablet_id;
  }
  {
    ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;
    error_status_ = s;
    // In case of a consumer side tablet split, need to refresh the partitions.
    if (client::ClientError(error_status_) == client::ClientErrorCode::kTablePartitionListIsStale) {
      table_->MarkPartitionsAsStale();
    }
  }

  HandleResponse();
}

void XClusterOutputClient::HandleResponse() {
  // If we're shutting down, then don't try to call the callback as that object might be deleted.
  ACQUIRE_MUTEX_IF_ONLINE_ELSE_RETURN;

  XClusterOutputClientResponse response;
  response.status = error_status_;
  response.get_changes_response = std::move(get_changes_resp_);
  if (response.status.ok()) {
    response.last_applied_op_id = op_id_;
    response.processed_record_count = processed_record_count_;
  }
  op_id_ = consensus::MinimumOpId();
  processed_record_count_ = 0;

  xcluster_poller_->ApplyChangesCallback(std::move(response));
}

bool XClusterOutputClient::IncProcessedRecordCount() {
  processed_record_count_++;
  if (processed_record_count_ == record_count_) {
    done_processing_ = true;
  }
  CHECK(processed_record_count_ <= record_count_);
  return done_processing_;
}

std::shared_ptr<XClusterOutputClient> CreateXClusterOutputClient(
    XClusterPoller* xcluster_poller, const xcluster::ConsumerTabletInfo& consumer_tablet_info,
    const xcluster::ProducerTabletInfo& producer_tablet_info, client::YBClient& local_client,
    ThreadPool* thread_pool, rpc::Rpcs* rpcs, bool use_local_tserver, bool is_automatic_mode,
    rocksdb::RateLimiter* rate_limiter) {
  return std::make_unique<XClusterOutputClient>(
      xcluster_poller, consumer_tablet_info, producer_tablet_info, local_client, thread_pool, rpcs,
      use_local_tserver, is_automatic_mode, rate_limiter);
}

}  // namespace tserver
}  // namespace yb
