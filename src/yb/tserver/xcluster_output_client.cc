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

#include <shared_mutex>

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/cdc_rpc.h"

#include "yb/common/wire_protocol.h"

#include "yb/client/client_fwd.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/client_utils.h"
#include "yb/client/external_transaction.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/dockv/doc_key.h"
#include "yb/docdb/docdb.h"

#include "yb/gutil/strings/join.h"

#include "yb/master/master_replication.pb.h"

#include "yb/rocksdb/rate_limiter.h"
#include "yb/rocksdb/util/rate_limiter.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/xcluster_consumer.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/xcluster_write_interface.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/stol_utils.h"
#include "yb/util/stopwatch.h"

DECLARE_int32(cdc_write_rpc_timeout_ms);

DEFINE_RUNTIME_bool(cdc_force_remote_tserver, false,
    "Avoid local tserver apply optimization for xCluster and force remote RPCs.");

DEFINE_RUNTIME_bool(xcluster_enable_packed_rows_support, true,
    "Enables rewriting of packed rows with xcluster consumer schema version");
TAG_FLAG(xcluster_enable_packed_rows_support, advanced);

DECLARE_int32(cdc_read_rpc_timeout_ms);

DEFINE_test_flag(bool, xcluster_consumer_fail_after_process_split_op, false,
    "Whether or not to fail after processing a replicated split_op on the consumer.");

DEFINE_test_flag(bool, xcluster_disable_replication_transaction_status_table, false,
                 "Whether or not to disable replication of txn status table.");

using namespace std::placeholders;

namespace yb {
namespace tserver {

class XClusterOutputClient : public XClusterOutputClientIf {
 public:
  XClusterOutputClient(
      XClusterConsumer* xcluster_consumer,
      const cdc::ConsumerTabletInfo& consumer_tablet_info,
      const cdc::ProducerTabletInfo& producer_tablet_info,
      const std::shared_ptr<XClusterClient>& local_client,
      ThreadPool* thread_pool,
      rpc::Rpcs* rpcs,
      std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk,
      bool use_local_tserver,
      const std::vector<TabletId>& global_transaction_status_tablets,
      bool enable_replicate_transaction_status_table,
      rocksdb::RateLimiter* rate_limiter)
      : xcluster_consumer_(xcluster_consumer),
        consumer_tablet_info_(consumer_tablet_info),
        producer_tablet_info_(producer_tablet_info),
        local_client_(local_client),
        thread_pool_(thread_pool),
        rpcs_(rpcs),
        write_handle_(rpcs->InvalidHandle()),
        apply_changes_clbk_(std::move(apply_changes_clbk)),
        use_local_tserver_(use_local_tserver),
        all_tablets_result_(STATUS(Uninitialized, "Result has not been initialized.")),
        global_transaction_status_tablets_(global_transaction_status_tablets),
        enable_replicate_transaction_status_table_(enable_replicate_transaction_status_table),
        rate_limiter_(rate_limiter) {}

  ~XClusterOutputClient() {
    VLOG_WITH_PREFIX_UNLOCKED(1) << "Destroying XClusterOutputClient";
    DCHECK(shutdown_);
  }

  // Sets the last compatible consumer schema version
  void SetLastCompatibleConsumerSchemaVersion(uint32_t schema_version) override;

  void UpdateSchemaVersionMappings(
      const cdc::XClusterSchemaVersionMap& schema_version_map,
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map) override;

  // Async call for applying changes. Will invoke the apply_changes_clbk when the changes are
  // applied, or when any error occurs.
  void ApplyChanges(std::shared_ptr<cdc::GetChangesResponsePB> resp) override;

  void Shutdown() override {
    DCHECK(!shutdown_);
    shutdown_ = true;

    rpc::RpcCommandPtr rpc_to_abort;
    {
      std::lock_guard l(lock_);
      if (write_handle_ != rpcs_->InvalidHandle()) {
        rpc_to_abort = *write_handle_;
      }
    }
    if (rpc_to_abort) {
      rpc_to_abort->Abort();
    }
  }

 private:
  // Utility function since we are inheriting shared_from_this().
  std::shared_ptr<XClusterOutputClient> SharedFromThis() {
    return std::dynamic_pointer_cast<XClusterOutputClient>(shared_from_this());
  }

  std::string LogPrefixUnlocked() const {
    return Format(
        "P [$0:$1] C [$2:$3]: ",
        producer_tablet_info_.stream_id,
        producer_tablet_info_.tablet_id,
        consumer_tablet_info_.table_id,
        consumer_tablet_info_.tablet_id);
  }

  void SetLastCompatibleConsumerSchemaVersionUnlocked(uint32_t schema_version) REQUIRES(lock_);

  // Process all records in get_changes_resp_ starting from the start index. If we find a ddl
  // record, then we process the current changes first, wait for those to complete, then process
  // the ddl + other changes after.
  Status ProcessChangesStartingFromIndex(int start);

  Status ProcessRecordForTablet(
      const cdc::CDCRecordPB& record, const Result<client::internal::RemoteTabletPtr>& tablet);

  Status ProcessRecordForLocalTablet(const cdc::CDCRecordPB& record);

  Status ProcessRecordForTransactionStatusTablet(const cdc::CDCRecordPB& record);

  Status ProcessRecordForTabletRange(
      const cdc::CDCRecordPB& record,
      const Result<std::vector<client::internal::RemoteTabletPtr>>& tablets);

  bool IsValidMetaOp(const cdc::CDCRecordPB& record);
  Result<bool> ProcessMetaOp(const cdc::CDCRecordPB& record);
  Result<bool> ProcessChangeMetadataOp(const cdc::CDCRecordPB& record);

  // Gets the producer/consumer schema mapping for the record.
  Result<cdc::XClusterSchemaVersionMap> GetSchemaVersionMap(
      const cdc::CDCRecordPB& record) REQUIRES(lock_);

  // Processes the Record and sends the CDCWrite for it.
  Status ProcessRecord(const std::vector<std::string>& tablet_ids, const cdc::CDCRecordPB& record)
      EXCLUDES(lock_);

  Status ProcessCreateRecord(const std::string& status_tablet, const cdc::CDCRecordPB& record)
      EXCLUDES(lock_);

  Status ProcessCommitRecord(
      const std::string& status_tablet,
      const std::vector<std::string>& involved_target_tablet_ids,
      const cdc::CDCRecordPB& record) EXCLUDES(lock_);

  Result<std::vector<TabletId>> GetInvolvedTargetTabletsFromCommitRecord(
      const cdc::CDCRecordPB& record);

  Result<TabletId> SelectStatusTabletIdForTransaction(const TransactionId& txn_id);

  Status SendTransactionUpdates();

  Status SendUserTableWrites();

  void SendNextCDCWriteToTablet(std::unique_ptr<WriteRequestPB> write_request);
  void UpdateSchemaVersionMapping(tserver::GetCompatibleSchemaVersionRequestPB* req);

  void WriteCDCRecordDone(const Status& status, const WriteResponsePB& response);
  void DoWriteCDCRecordDone(const Status& status, const WriteResponsePB& response);

  void SchemaVersionCheckDone(
      const Status& status,
      const GetCompatibleSchemaVersionRequestPB& req,
      const GetCompatibleSchemaVersionResponsePB& response);

  void DoSchemaVersionCheckDone(
      const Status& status,
      const GetCompatibleSchemaVersionRequestPB& req,
      const GetCompatibleSchemaVersionResponsePB& response);

  // Increment processed record count.
  // Returns true if all records are processed, false if there are still some pending records.
  bool IncProcessedRecordCount() REQUIRES(lock_);

  XClusterOutputClientResponse PrepareResponse() REQUIRES(lock_);
  void SendResponse(const XClusterOutputClientResponse& resp) EXCLUDES(lock_);

  void HandleResponse() EXCLUDES(lock_);
  void HandleError(const Status& s) EXCLUDES(lock_);

  bool UseLocalTserver();

  XClusterConsumer* xcluster_consumer_;
  const cdc::ConsumerTabletInfo consumer_tablet_info_;
  const cdc::ProducerTabletInfo producer_tablet_info_;
  cdc::XClusterSchemaVersionMap schema_versions_ GUARDED_BY(lock_);
  cdc::ColocatedSchemaVersionMap colocated_schema_version_map_ GUARDED_BY(lock_);
  std::shared_ptr<XClusterClient> local_client_;
  ThreadPool* thread_pool_;  // Use threadpool so that callbacks aren't run on reactor threads.
  rpc::Rpcs* rpcs_;
  rpc::Rpcs::Handle write_handle_ GUARDED_BY(lock_);
  // Retain COMMIT rpcs in-flight as these need to be cleaned up on shutdown
  std::vector<std::shared_ptr<client::ExternalTransaction>> external_transactions_;
  std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk_;

  bool use_local_tserver_;

  std::shared_ptr<client::YBTable> table_;

  // Used to protect error_status_, op_id_, done_processing_, write_handle_ and record counts.
  mutable rw_spinlock lock_;
  Status error_status_ GUARDED_BY(lock_);
  OpIdPB op_id_ GUARDED_BY(lock_) = consensus::MinimumOpId();
  bool done_processing_ GUARDED_BY(lock_) = false;
  uint32_t wait_for_version_ GUARDED_BY(lock_) = 0;
  std::atomic<bool> shutdown_ = false;

  uint32_t processed_record_count_ GUARDED_BY(lock_) = 0;
  uint32_t record_count_ GUARDED_BY(lock_) = 0;

  SchemaVersion last_compatible_consumer_schema_version_ GUARDED_BY(lock_) = 0;
  SchemaVersion producer_schema_version_ GUARDED_BY(lock_) = 0;
  ColocationId colocation_id_  GUARDED_BY(lock_) = 0;

  // This will cache the response to an ApplyChanges() request.
  std::shared_ptr<cdc::GetChangesResponsePB> get_changes_resp_;

  // Store the result of the lookup for all the tablets.
  yb::Result<std::vector<scoped_refptr<yb::client::internal::RemoteTablet>>> all_tablets_result_;

  std::vector<TabletId> global_transaction_status_tablets_;

  yb::MonoDelta timeout_ms_;

  std::unique_ptr<XClusterWriteInterface> write_strategy_ GUARDED_BY(lock_);

  bool enable_replicate_transaction_status_table_;

  rocksdb::RateLimiter* rate_limiter_;
};

#define HANDLE_ERROR_AND_RETURN_IF_NOT_OK(status) \
  do { \
    auto&& _s = (status); \
    if (!_s.ok()) { \
      HandleError(std::move(_s)); \
      return; \
    } \
  } while (0)

#define RETURN_WHEN_OFFLINE() \
  if (shutdown_.load()) { \
    VLOG_WITH_PREFIX_UNLOCKED(1) \
        << "Aborting ApplyChanges since the output client is shutting down."; \
    return; \
  }

void XClusterOutputClient::SetLastCompatibleConsumerSchemaVersionUnlocked(
    SchemaVersion schema_version) {
  if (schema_version != cdc::kInvalidSchemaVersion &&
      schema_version > last_compatible_consumer_schema_version_) {
    LOG(INFO) << "Last compatible consumer schema version updated to  "
              << schema_version;
    last_compatible_consumer_schema_version_ = schema_version;
  }
}

void XClusterOutputClient::SetLastCompatibleConsumerSchemaVersion(SchemaVersion schema_version) {
  std::lock_guard lock(lock_);
  SetLastCompatibleConsumerSchemaVersionUnlocked(schema_version);
}

void XClusterOutputClient::UpdateSchemaVersionMappings(
      const cdc::XClusterSchemaVersionMap& schema_version_map,
      const cdc::ColocatedSchemaVersionMap& colocated_schema_version_map) {
  std::lock_guard l(lock_);

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
    std::lock_guard l(lock_);
    DCHECK(consensus::OpIdEquals(op_id_, consensus::MinimumOpId()));
    op_id_ = poller_resp->checkpoint().op_id();
    error_status_ = Status::OK();
    done_processing_ = false;
    wait_for_version_ = 0;
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
    HANDLE_ERROR_AND_RETURN_IF_NOT_OK(
        local_client_->client->OpenTable(consumer_tablet_info_.table_id, &table_));
  }

  timeout_ms_ = MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms);
  // Using this future as a barrier to get all the tablets before processing.  Ordered iteration
  // matters: we need to ensure that each record is handled sequentially.
  all_tablets_result_ = local_client_->client->LookupAllTabletsFuture(
      table_, CoarseMonoClock::now() + timeout_ms_).get();

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
    } else if (table_->table_type() == client::YBTableType::TRANSACTION_STATUS_TABLE_TYPE) {
      // Handle txn status tablets specially, since we always map them to a specific status tablet.
      RETURN_NOT_OK(ProcessRecordForTransactionStatusTablet(record));
    } else if (UseLocalTserver()) {
      RETURN_NOT_OK(ProcessRecordForLocalTablet(record));
    } else {
      switch (record.operation()) {
        case cdc::CDCRecordPB::APPLY:
          RETURN_NOT_OK(ProcessRecordForTabletRange(record, all_tablets_result_));
          break;
        default: {
          std::string partition_key = record.key(0).key();
          auto tablet_result = local_client_->client->LookupTabletByKeyFuture(
              table_, partition_key, CoarseMonoClock::now() + timeout_ms_).get();
          RETURN_NOT_OK(ProcessRecordForTablet(record, tablet_result));
          break;
        }
      }
    }
    processed_write_record = true;
  }

  if (processed_write_record) {
    if (table_->table_type() == client::YBTableType::TRANSACTION_STATUS_TABLE_TYPE) {
      return SendTransactionUpdates();
    } else {
      return SendUserTableWrites();
    }
  }
  return Status::OK();
}

Result<std::vector<TabletId>> XClusterOutputClient::GetInvolvedTargetTabletsFromCommitRecord(
    const cdc::CDCRecordPB& record) {
  std::unordered_set<TabletId> involved_target_tablets;
  auto involved_producer_tablets = std::vector<TabletId>(
      record.transaction_state().tablets().begin(),
      record.transaction_state().tablets().end());
  for (const auto& producer_tablet : involved_producer_tablets) {
    auto consumer_tablet_info = xcluster_consumer_->GetConsumerTableInfo(producer_tablet);
    // Ignore records for which we dont have any consumer tablets.
    if (!consumer_tablet_info.ok()) {
      if (consumer_tablet_info.status().IsNotFound()) {
        VLOG_WITH_FUNC(3) << "Ignoring producer tablet '" << producer_tablet
                          << "' from commit record of transaction '"
                          << record.transaction_state().transaction_id()
                          << "' as it is not replicated to our universe";
        continue;
      }
      return consumer_tablet_info.status();
    }
    involved_target_tablets.insert(consumer_tablet_info->tablet_id);
    // TODO(Rahul): Add the n : m case for involved tablets.
  }

  return std::vector<TabletId>(involved_target_tablets.begin(), involved_target_tablets.end());
}

Status XClusterOutputClient::SendTransactionUpdates() {
  std::vector<client::ExternalTransactionMetadata> transaction_metadatas;
  {
    std::lock_guard l(lock_);
    transaction_metadatas = std::move(write_strategy_->GetTransactionMetadatas());
  }
  std::vector<std::future<Status>> transaction_update_futures;
  // Send all CREATEs to the coordinatory, wait till we've replicated all of them, then send all
  // the COMMITs. This avoids a situation where the COMMMIT is processed by the coordinator before
  // the CREATE.
  for (const auto& operation_type : {client::ExternalTransactionMetadata::OperationType::CREATE,
                                     client::ExternalTransactionMetadata::OperationType::COMMIT}) {
    for (const auto& transaction_metadata : transaction_metadatas) {
      if (transaction_metadata.operation_type != operation_type) {
        continue;
      }
      // Create the ExternalTransactions and COMMIT them locally.
      auto* transaction_manager = xcluster_consumer_->TransactionManager();
      if (!transaction_manager) {
        return STATUS(InvalidArgument, "Could not commit transactions");
      }
      auto external_transaction =
          std::make_shared<client::ExternalTransaction>(transaction_manager);
      external_transactions_.push_back(external_transaction);

      switch (operation_type) {
        case client::ExternalTransactionMetadata::OperationType::CREATE: {
          transaction_update_futures.push_back(external_transaction->CreateFuture(
              transaction_metadata));
          break;
        }
        case client::ExternalTransactionMetadata::OperationType::COMMIT: {
          transaction_update_futures.push_back(external_transaction->CommitFuture(
              transaction_metadata));
          break;
        }
        default: {
          return STATUS(IllegalState, Format("Unsupported OperationType $0",
                                             transaction_metadata.OperationTypeToString()));
        }
      }
    }
    for (auto& future : transaction_update_futures) {
      DCHECK(future.valid());
      RETURN_NOT_OK(future.get());
    }
    transaction_update_futures.clear();
    external_transactions_.clear();
  }

  int next_record = 0;
  {
    std::lock_guard l(lock_);
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

  return Status::OK();
}

Status XClusterOutputClient::SendUserTableWrites() {
  // Send out the buffered writes.
  std::unique_ptr<WriteRequestPB> write_request;
  {
    std::lock_guard l(lock_);
    write_request = write_strategy_->FetchNextRequest();
  }
  if (!write_request) {
    LOG(WARNING) << "Expected to find a write_request but were unable to";
    return STATUS(IllegalState, "Could not find a write request to send");
  }
  SendNextCDCWriteToTablet(std::move(write_request));
  return Status::OK();
}

bool XClusterOutputClient::UseLocalTserver() {
  return use_local_tserver_ && !FLAGS_cdc_force_remote_tserver;
}

Status XClusterOutputClient::ProcessCreateRecord(
    const std::string& status_tablet, const cdc::CDCRecordPB& record) {
  std::lock_guard l(lock_);
  return write_strategy_->ProcessCreateRecord(status_tablet, record);
}

Status XClusterOutputClient::ProcessCommitRecord(
    const std::string& status_tablet,
    const std::vector<std::string>& involved_target_tablet_ids,
    const cdc::CDCRecordPB& record) {
  std::lock_guard l(lock_);
  return write_strategy_->ProcessCommitRecord(status_tablet, involved_target_tablet_ids, record);
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
        VLOG_WITH_PREFIX_UNLOCKED(3) << Format(
            "ColocationId:$0 Producer Schema Version:$1, Consumer Schema Version:$2",
            colocationId, producer_schema_version, consumer_schema_version);
    }
  }

  return *cached_schema_versions;
}

Status XClusterOutputClient::ProcessRecord(
    const std::vector<std::string>& tablet_ids, const cdc::CDCRecordPB& record) {
  std::lock_guard l(lock_);
  for (const auto& tablet_id : tablet_ids) {
    std::string status_tablet_id;
    if (enable_replicate_transaction_status_table_ && record.has_transaction_state()) {
      // This is an intent record and we want to use the txn status table, so get the status tablet
      // for this txn id.
      // Always fill this field, since in the uneven tablets case, we might not have previously sent
      // a batch with the status tablet id to a tablet yet, in which case that tablet would be
      // unable to find the matching status tablet.
      auto txn_id =
          VERIFY_RESULT(FullyDecodeTransactionId(record.transaction_state().transaction_id()));
      status_tablet_id = VERIFY_RESULT(SelectStatusTabletIdForTransaction(txn_id));
    }

    // Find the last_compatible_consumer_schema_version for each record as it may be different
    // for different records depending on the colocation id.
    cdc::XClusterSchemaVersionMap schema_versions_map;
    if (PREDICT_TRUE(FLAGS_xcluster_enable_packed_rows_support) &&
        !record.changes().empty() &&
        (record.operation() == cdc::CDCRecordPB::WRITE ||
         record.operation() == cdc::CDCRecordPB::DELETE)) {
        schema_versions_map = VERIFY_RESULT(GetSchemaVersionMap(record));
    }

    auto status = write_strategy_->ProcessRecord(ProcessRecordInfo {
      .tablet_id = tablet_id,
      .enable_replicate_transaction_status_table = enable_replicate_transaction_status_table_,
      .status_tablet_id = status_tablet_id,
      .schema_versions_map = schema_versions_map
    }, record);
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

Result<TabletId> XClusterOutputClient::SelectStatusTabletIdForTransaction(
    const TransactionId& txn_id) {
  // Return a deterministic status tablet for a given txn id. This is done since we only include
  // the external_status_tablet for the first batch, and are not guaranteed to compute this value
  // for later batches. Thus, for uneven tablet counts, we require a way to select a status tablet
  // that is consistent across all consumers.
  SCHECK(!global_transaction_status_tablets_.empty(), IllegalState, "Found no status tablets");
  return global_transaction_status_tablets_
      [hash_value(txn_id) % global_transaction_status_tablets_.size()];
}

Status XClusterOutputClient::ProcessRecordForTransactionStatusTablet(
    const cdc::CDCRecordPB& record) {
  const auto txn_id =
      VERIFY_RESULT(FullyDecodeTransactionId(record.transaction_state().transaction_id()));
  const auto status_tablet_id = VERIFY_RESULT(SelectStatusTabletIdForTransaction(txn_id));

  const auto& operation = record.operation();
  if (operation == cdc::CDCRecordPB::TRANSACTION_COMMITTED) {
    // TODO handle involved_tablets for uneven tablet counts.
    auto target_tablet_ids = VERIFY_RESULT(GetInvolvedTargetTabletsFromCommitRecord(record));
    return ProcessCommitRecord(status_tablet_id, target_tablet_ids, record);
  }

  if (operation == cdc::CDCRecordPB::TRANSACTION_CREATED) {
    return ProcessCreateRecord(status_tablet_id, record);
  }

  return ProcessRecord({status_tablet_id}, record);
}

bool XClusterOutputClient::IsValidMetaOp(const cdc::CDCRecordPB& record) {
  auto type = record.operation();
  return type == cdc::CDCRecordPB::SPLIT_OP || type == cdc::CDCRecordPB::CHANGE_METADATA;
}

Result<bool> XClusterOutputClient::ProcessChangeMetadataOp(const cdc::CDCRecordPB& record) {
  YB_LOG_WITH_PREFIX_UNLOCKED_EVERY_N_SECS(INFO, 300) << " Processing Change Metadata Op :"
                                                      << record.DebugString();
  if (record.change_metadata_request().has_remove_table_id() ||
      !record.change_metadata_request().add_multiple_tables().empty()) {
    // TODO (#16557): Support remove_table_id() for colocated tables / tablegroups.
    LOG(INFO) << "Ignoring change metadata request to add multiple/remove tables to tablet : "
              << producer_tablet_info_.tablet_id;
    return true;
  }

  if (!record.change_metadata_request().has_schema() &&
      !record.change_metadata_request().has_add_table()) {
    LOG(INFO) << "Ignoring change metadata request for tablet : " << producer_tablet_info_.tablet_id
              << " as it does not contain any schema. ";
    return true;
  }

  auto schema = record.change_metadata_request().has_add_table() ?
                record.change_metadata_request().add_table().schema() :
                record.change_metadata_request().schema();
  {
    std::lock_guard l(lock_);

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
        LOG(INFO) << Format(
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

    RETURN_NOT_OK(local_client_->client->UpdateConsumerOnProducerSplit(
        producer_tablet_info_.replication_group_id, producer_tablet_info_.stream_id, split_info));
  } else if (record.operation() == cdc::CDCRecordPB::CHANGE_METADATA) {
    if (!VERIFY_RESULT(ProcessChangeMetadataOp(record))) {
      return false;
    }
  }

  // Increment processed records, and check for completion.
  bool done = false;
  {
    std::lock_guard l(lock_);
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

  std::lock_guard l(lock_);
  write_handle_ = rpcs_->Prepare();
  if (write_handle_ != rpcs_->InvalidHandle()) {
    // Send in nullptr for RemoteTablet since cdc rpc now gets the tablet_id from the write request.
    *write_handle_ = cdc::CreateCDCWriteRpc(
        deadline,
        nullptr /* RemoteTablet */,
        table_,
        local_client_->client.get(),
        write_request.get(),
        std::bind(&XClusterOutputClient::WriteCDCRecordDone, SharedFromThis(), _1, _2),
        UseLocalTserver());
    (**write_handle_).SendRpc();
  } else {
    LOG(WARNING) << "Invalid handle for CDC write, tablet ID: " << write_request->tablet_id();
  }
}

void XClusterOutputClient::UpdateSchemaVersionMapping(
    tserver::GetCompatibleSchemaVersionRequestPB* req) {
  auto deadline =
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms);

  std::lock_guard l(lock_);
  write_handle_ = rpcs_->Prepare();
  if (write_handle_ != rpcs_->InvalidHandle()) {
    // Send in nullptr for RemoteTablet since cdc rpc now gets the tablet_id from the write request.
    *write_handle_ = cdc::CreateGetCompatibleSchemaVersionRpc(
        deadline,
        nullptr,
        local_client_->client.get(),
        req,
        std::bind(&XClusterOutputClient::SchemaVersionCheckDone, SharedFromThis(), _1, _2, _3),
        UseLocalTserver());
    (**write_handle_).SendRpc();
  } else {
    LOG(WARNING) << "Invalid handle for GetCompatibleSchemaVersion,tablet ID: " << req->tablet_id();
  }
}

void XClusterOutputClient::SchemaVersionCheckDone(
    const Status& status,
    const GetCompatibleSchemaVersionRequestPB& req,
    const GetCompatibleSchemaVersionResponsePB& response) {
  rpc::RpcCommandPtr retained;
  {
    std::lock_guard l(lock_);
    retained = rpcs_->Unregister(&write_handle_);
  }
  RETURN_WHEN_OFFLINE();

  WARN_NOT_OK(thread_pool_->SubmitFunc(std::bind(
      &XClusterOutputClient::DoSchemaVersionCheckDone,
      SharedFromThis(),
      status,
      std::move(req),
      std::move(response))),
      "Could not submit DoSchemaVersionCheckDone to thread pool");
}

void XClusterOutputClient::DoSchemaVersionCheckDone(
    const Status& status,
    const GetCompatibleSchemaVersionRequestPB& req,
    const GetCompatibleSchemaVersionResponsePB& resp) {
  RETURN_WHEN_OFFLINE();

  SchemaVersion producer_schema_version = cdc::kInvalidSchemaVersion;
  ColocationId colocation_id = 0;
  {
    std::lock_guard l(lock_);
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
    LOG(WARNING) << msg;
    if (resp.error().code() == TabletServerErrorPB::MISMATCHED_SCHEMA) {
      xcluster_consumer_->StoreReplicationError(
          consumer_tablet_info_.tablet_id,
          producer_tablet_info_.stream_id,
          replication_error,
          msg);
    }
    HandleError(status);
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
  Status s = local_client_->client->UpdateConsumerOnProducerMetadata(
      producer_tablet_info_.replication_group_id, producer_tablet_info_.stream_id, meta,
      colocation_id, producer_schema_version, resp.compatible_schema_version(), &response);
  if (!s.ok()) {
    HandleError(s);
    return;
  }

  {
    // Since we are done processing the meta op, we can increment the processed count.
    std::lock_guard l(lock_);
    // Update the local cache with the response. This should happen automatically on the heartbeat
    // as well when the poller gets updated, but we opportunistically update here.
    cdc::XClusterSchemaVersionMap *schema_version_map = nullptr;
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
  }

  // Clear out any replication errors.
  xcluster_consumer_->ClearReplicationError(
      consumer_tablet_info_.tablet_id,
      producer_tablet_info_.stream_id);

  // MetaOps should be the last ones in a batch, so we should be done at this point.
  HandleResponse();
}

void XClusterOutputClient::WriteCDCRecordDone(
    const Status& status, const WriteResponsePB& response) {
  rpc::RpcCommandPtr retained;
  {
    std::lock_guard l(lock_);
    retained = rpcs_->Unregister(&write_handle_);
  }
  RETURN_WHEN_OFFLINE();

  WARN_NOT_OK(
      thread_pool_->SubmitFunc(std::bind(
          &XClusterOutputClient::DoWriteCDCRecordDone, SharedFromThis(), status,
          std::move(response))),
      "Could not submit DoWriteCDCRecordDone to thread pool");
}

void XClusterOutputClient::DoWriteCDCRecordDone(
    const Status& status, const WriteResponsePB& response) {
  RETURN_WHEN_OFFLINE();

  if (!status.ok()) {
    HandleError(status);
    return;
  } else if (response.has_error()) {
    HandleError(StatusFromPB(response.error().status()));
    return;
  }
  xcluster_consumer_->IncrementNumSuccessfulWriteRpcs();

  // See if we need to handle any more writes.
  std::unique_ptr<WriteRequestPB> write_request;
  {
    std::lock_guard l(lock_);
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
    LOG(WARNING) << "Retrying applying replicated record for consumer tablet: "
                 << consumer_tablet_info_.tablet_id << ", reason: " << s;
  } else {
    LOG(ERROR) << "Error while applying replicated record: " << s
               << ", consumer tablet: " << consumer_tablet_info_.tablet_id;
  }
  {
    std::lock_guard l(lock_);
    error_status_ = s;
    // In case of a consumer side tablet split, need to refresh the partitions.
    if (client::ClientError(error_status_) == client::ClientErrorCode::kTablePartitionListIsStale) {
      table_->MarkPartitionsAsStale();
    }
  }

  HandleResponse();
}

XClusterOutputClientResponse XClusterOutputClient::PrepareResponse() {
  XClusterOutputClientResponse response;
  response.status = error_status_;
  response.get_changes_response = std::move(get_changes_resp_);
  if (response.status.ok()) {
    response.last_applied_op_id = op_id_;
    response.processed_record_count = processed_record_count_;
    response.wait_for_version = wait_for_version_;
  }
  op_id_ = consensus::MinimumOpId();
  processed_record_count_ = 0;
  return response;
}

void XClusterOutputClient::SendResponse(const XClusterOutputClientResponse& resp) {
  // If we're shutting down, then don't try to call the callback as that object might be deleted.
  RETURN_WHEN_OFFLINE();
  apply_changes_clbk_(resp);
}

void XClusterOutputClient::HandleResponse() {
  XClusterOutputClientResponse response;
  {
    std::lock_guard l(lock_);
    response = PrepareResponse();
  }
  SendResponse(response);
}

bool XClusterOutputClient::IncProcessedRecordCount() {
  processed_record_count_++;
  if (processed_record_count_ == record_count_) {
    done_processing_ = true;
  }
  CHECK(processed_record_count_ <= record_count_);
  return done_processing_;
}

std::shared_ptr<XClusterOutputClientIf> CreateXClusterOutputClient(
    XClusterConsumer* xcluster_consumer,
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const std::shared_ptr<XClusterClient>& local_client,
    ThreadPool* thread_pool,
    rpc::Rpcs* rpcs,
    std::function<void(const XClusterOutputClientResponse& response)> apply_changes_clbk,
    bool use_local_tserver,
    const std::vector<TabletId>& global_transaction_status_tablets,
    bool enable_replicate_transaction_status_table,
    rocksdb::RateLimiter* rate_limiter) {
  return std::make_unique<XClusterOutputClient>(
      xcluster_consumer, consumer_tablet_info, producer_tablet_info, local_client, thread_pool,
      rpcs, std::move(apply_changes_clbk), use_local_tserver, global_transaction_status_tablets,
      enable_replicate_transaction_status_table, rate_limiter);
}

}  // namespace tserver
}  // namespace yb
