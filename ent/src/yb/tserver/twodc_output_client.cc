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

#include "yb/tserver/twodc_output_client.h"

#include <shared_mutex>

#include "yb/cdc/cdc_util.h"
#include "yb/cdc/cdc_rpc.h"
#include "yb/client/client.h"
#include "yb/client/client_error.h"
#include "yb/client/client_utils.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/gutil/strings/join.h"
#include "yb/master/master_replication.pb.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/twodc_write_interface.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/stol_utils.h"

DECLARE_int32(cdc_write_rpc_timeout_ms);

DEFINE_bool(cdc_force_remote_tserver, false,
            "Avoid local tserver apply optimization for CDC and force remote RPCs.");
TAG_FLAG(cdc_force_remote_tserver, runtime);

DECLARE_int32(cdc_read_rpc_timeout_ms);

DEFINE_test_flag(bool, xcluster_consumer_fail_after_process_split_op, false,
                 "Whether or not to fail after processing a replicated split_op on the consumer.");

using namespace std::placeholders;

namespace yb {
namespace tserver {
namespace enterprise {

using rpc::Rpc;

class TwoDCOutputClient : public cdc::CDCOutputClient {
 public:
  TwoDCOutputClient(
      CDCConsumer* cdc_consumer,
      const cdc::ConsumerTabletInfo& consumer_tablet_info,
      const cdc::ProducerTabletInfo& producer_tablet_info,
      const std::shared_ptr<CDCClient>& local_client,
      rpc::Rpcs* rpcs,
      std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk,
      bool use_local_tserver) :
      cdc_consumer_(cdc_consumer),
      consumer_tablet_info_(consumer_tablet_info),
      producer_tablet_info_(producer_tablet_info),
      local_client_(local_client),
      rpcs_(rpcs),
      write_handle_(rpcs->InvalidHandle()),
      apply_changes_clbk_(std::move(apply_changes_clbk)),
      use_local_tserver_(use_local_tserver),
      all_tablets_result_(STATUS(Uninitialized, "Result has not been initialized.")) {}

  ~TwoDCOutputClient() {
    std::lock_guard<decltype(lock_)> l(lock_);
    shutdown_ = true;
    rpcs_->Abort({&write_handle_});
  }

  Status ApplyChanges(const cdc::GetChangesResponsePB* resp) override;

  void WriteCDCRecordDone(const Status& status, const WriteResponsePB& response);

 private:

  // Process all records in twodc_resp_copy_ starting from the start index. If we find a ddl
  // record, then we process the current changes first, wait for those to complete, then process
  // the ddl + other changes after.
  Status ProcessChangesStartingFromIndex(int start);

  Status ProcessRecordForTablet(
      const cdc::CDCRecordPB& record, const Result<client::internal::RemoteTabletPtr>& tablet);

  Status ProcessRecordForLocalTablet(const cdc::CDCRecordPB& record);

  Status ProcessRecordForTabletRange(
      const cdc::CDCRecordPB& record,
      const Result<std::vector<client::internal::RemoteTabletPtr>>& tablets);

  bool IsValidMetaOp(const cdc::CDCRecordPB& record);
  Result<bool> ProcessMetaOp(const cdc::CDCRecordPB& record);

  // Processes the Record and sends the CDCWrite for it.
  Status ProcessRecord(
      const std::vector<std::string>& tablet_ids, const cdc::CDCRecordPB& record);

  void SendNextCDCWriteToTablet(std::unique_ptr<WriteRequestPB> write_request);

  // Increment processed record count.
  // Returns true if all records are processed, false if there are still some pending records.
  bool IncProcessedRecordCount() REQUIRES(lock_);

  cdc::OutputClientResponse PrepareResponse() REQUIRES(lock_);
  void SendResponse(const cdc::OutputClientResponse& resp) EXCLUDES(lock_);

  void HandleResponse() EXCLUDES(lock_);
  void HandleError(const Status& s, bool done) EXCLUDES(lock_);

  bool UseLocalTserver();

  CDCConsumer* cdc_consumer_;
  cdc::ConsumerTabletInfo consumer_tablet_info_;
  cdc::ProducerTabletInfo producer_tablet_info_;
  std::shared_ptr<CDCClient> local_client_;
  rpc::Rpcs* rpcs_;
  rpc::Rpcs::Handle write_handle_ GUARDED_BY(lock_);
  std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk_;

  bool use_local_tserver_;

  std::shared_ptr<client::YBTable> table_;

  // Used to protect error_status_, op_id_, done_processing_, write_handle_ and record counts.
  mutable rw_spinlock lock_;
  Status error_status_ GUARDED_BY(lock_);
  OpIdPB op_id_ GUARDED_BY(lock_) = consensus::MinimumOpId();
  bool done_processing_ GUARDED_BY(lock_) = false;
  bool shutdown_ GUARDED_BY(lock_) = false;
  uint32_t wait_for_version_ GUARDED_BY(lock_) = 0;

  uint32_t processed_record_count_ GUARDED_BY(lock_) = 0;
  uint32_t record_count_ GUARDED_BY(lock_) = 0;

  // This will cache the response to an ApplyChanges() request.
  cdc::GetChangesResponsePB twodc_resp_copy_;

  // Store the result of the lookup for all the tablets.
  yb::Result<std::vector<scoped_refptr<yb::client::internal::RemoteTablet>>> all_tablets_result_;

  yb::MonoDelta timeout_ms_;

  std::unique_ptr<TwoDCWriteInterface> write_strategy_ GUARDED_BY(lock_);
};

#define HANDLE_ERROR_AND_RETURN_IF_NOT_OK(status) do { \
  auto&& _s = (status); \
  if (!_s.ok()) { \
    HandleError(_s, true); \
    return std::move(_s); \
  } \
} while (0);

Status TwoDCOutputClient::ApplyChanges(const cdc::GetChangesResponsePB* poller_resp) {
  // ApplyChanges is called in a single threaded manner.
  // For all the changes in GetChangesResponsePB, we first fan out and find the tablet for
  // every record key.
  // Then we apply the records in the same order in which we received them.
  // Once all changes have been applied (successfully or not), we invoke the callback which will
  // then either poll for next set of changes (in case of successful application) or will try to
  // re-apply.
  DCHECK(poller_resp->has_checkpoint());
  twodc_resp_copy_.Clear();

  // Init class variables that threads will use.
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    DCHECK(consensus::OpIdEquals(op_id_, consensus::MinimumOpId()));
    op_id_ = poller_resp->checkpoint().op_id();
    error_status_ = Status::OK();
    done_processing_ = false;
    wait_for_version_ = 0;
    processed_record_count_ = 0;
    record_count_ = poller_resp->records_size();
    ResetWriteInterface(&write_strategy_);
  }

  // Ensure we have records.
  if (poller_resp->records_size() == 0) {
    HandleResponse();
    return Status::OK();
  }

  // Ensure we have a connection to the consumer table cached.
  if (!table_) {
    HANDLE_ERROR_AND_RETURN_IF_NOT_OK(
        local_client_->client->OpenTable(consumer_tablet_info_.table_id, &table_));
  }

  twodc_resp_copy_ = *poller_resp;
  timeout_ms_ = MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms);
  // Using this future as a barrier to get all the tablets before processing.  Ordered iteration
  // matters: we need to ensure that each record is handled sequentially.
  all_tablets_result_ = local_client_->client->LookupAllTabletsFuture(
      table_, CoarseMonoClock::now() + timeout_ms_).get();

  HANDLE_ERROR_AND_RETURN_IF_NOT_OK(ProcessChangesStartingFromIndex(0));

  return Status::OK();
}

Status TwoDCOutputClient::ProcessChangesStartingFromIndex(int start) {
  bool processed_write_record = false;
  auto records_size = twodc_resp_copy_.records_size();
  for (int i = start; i < records_size; i++) {
    // All KV-pairs within a single CDC record will be for the same row.
    // key(0).key() will contain the hash code for that row. We use this to lookup the tablet.
    const auto& record = twodc_resp_copy_.records(i);

    if (IsValidMetaOp(record)) {
      if (processed_write_record) {
        // We have existing write operations, so flush them first (see WriteCDCRecordDone).
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
      continue;
    } else if (UseLocalTserver()) {
      RETURN_NOT_OK(ProcessRecordForLocalTablet(record));
    } else {
      if (record.operation() == cdc::CDCRecordPB::APPLY) {
        RETURN_NOT_OK(ProcessRecordForTabletRange(record, all_tablets_result_));
      } else {
        auto partition_hash_key = PartitionSchema::EncodeMultiColumnHashValue(
            VERIFY_RESULT(CheckedStoInt<uint16_t>(record.key(0).key())));
        auto tablet_result = local_client_->client->LookupTabletByKeyFuture(
            table_, partition_hash_key, CoarseMonoClock::now() + timeout_ms_).get();
        RETURN_NOT_OK(ProcessRecordForTablet(record, tablet_result));
      }
    }
    processed_write_record = true;
  }

  if (processed_write_record) {
    // Send out the buffered writes.
    std::unique_ptr<WriteRequestPB> write_request;
    {
      std::lock_guard<decltype(lock_)> l(lock_);
      write_request = write_strategy_->GetNextWriteRequest();
    }
    if (!write_request) {
      LOG(WARNING) << "Expected to find a write_request but were unable to";
      return STATUS(IllegalState, "Could not find a write request to send");
    }
    SendNextCDCWriteToTablet(std::move(write_request));
  }

  return Status::OK();
}

bool TwoDCOutputClient::UseLocalTserver() {
  return use_local_tserver_ && !FLAGS_cdc_force_remote_tserver;
}

Status TwoDCOutputClient::ProcessRecord(const std::vector<std::string>& tablet_ids,
                                      const cdc::CDCRecordPB& record) {
  std::lock_guard<decltype(lock_)> l(lock_);
  for (const auto& tablet_id : tablet_ids) {
    auto status = write_strategy_->ProcessRecord(tablet_id, record);
    if (!status.ok()) {
      error_status_ = status;
      return status;
    }
  }
  IncProcessedRecordCount();
  return Status::OK();
}

Status TwoDCOutputClient::ProcessRecordForTablet(
    const cdc::CDCRecordPB& record, const Result<client::internal::RemoteTabletPtr>& tablet) {
  RETURN_NOT_OK(tablet);
  return ProcessRecord({tablet->get()->tablet_id()}, record);
}

Status TwoDCOutputClient::ProcessRecordForTabletRange(
    const cdc::CDCRecordPB& record,
    const Result<std::vector<client::internal::RemoteTabletPtr>>& tablets) {
  RETURN_NOT_OK(tablets);

  auto filtered_tablets_result = client::FilterTabletsByHashPartitionKeyRange(
      *tablets, record.partition().partition_key_start(), record.partition().partition_key_end());
  RETURN_NOT_OK(filtered_tablets_result);

  auto filtered_tablets = *filtered_tablets_result;
  if (filtered_tablets.empty()) {
    table_->MarkPartitionsAsStale();
    return STATUS(TryAgain, "No tablets found for key range, refreshing partitions to try again.");
  }
  auto tablet_ids = std::vector<std::string>(filtered_tablets.size());
  std::transform(filtered_tablets.begin(), filtered_tablets.end(), tablet_ids.begin(),
                 [&](const auto& tablet_ptr) {
    return tablet_ptr->tablet_id();
  });
  return ProcessRecord(tablet_ids, record);
}

Status TwoDCOutputClient::ProcessRecordForLocalTablet(const cdc::CDCRecordPB& record) {
  return ProcessRecord({consumer_tablet_info_.tablet_id}, record);
}

bool TwoDCOutputClient::IsValidMetaOp(const cdc::CDCRecordPB& record) {
  auto type = record.operation();
  return type == cdc::CDCRecordPB::SPLIT_OP || type == cdc::CDCRecordPB::CHANGE_METADATA;
}

Result<bool> TwoDCOutputClient::ProcessMetaOp(const cdc::CDCRecordPB& record) {
  uint32_t wait_for_version = 0;
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
        producer_tablet_info_.universe_uuid, producer_tablet_info_.stream_id, split_info));
  } else if (record.operation() == cdc::CDCRecordPB::CHANGE_METADATA) {
    bool should_halt = VERIFY_RESULT(local_client_->client->UpdateConsumerOnProducerMetadata(
        producer_tablet_info_.universe_uuid, producer_tablet_info_.stream_id,
        record.change_metadata_request()));
    if (should_halt) {
      LOG(INFO) << "Halting Polling for " << producer_tablet_info_.tablet_id;
      wait_for_version = record.change_metadata_request().schema_version();
    }
  }

  // Increment processed records, and check for completion.
  bool done = false;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    done = IncProcessedRecordCount();
    wait_for_version_ = wait_for_version;
    DCHECK(wait_for_version == 0 || done); // If (should_wait) then done.
  }
  return done;
}

void TwoDCOutputClient::SendNextCDCWriteToTablet(std::unique_ptr<WriteRequestPB> write_request) {
  // TODO: This should be parallelized for better performance with M:N setups.
  auto deadline = CoarseMonoClock::Now() +
                  MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms);
  std::lock_guard<decltype(lock_)> l(lock_);
  write_handle_ = rpcs_->Prepare();
  if (write_handle_ != rpcs_->InvalidHandle()) {
    // Send in nullptr for RemoteTablet since cdc rpc now gets the tablet_id from the write request.
    *write_handle_ = CreateCDCWriteRpc(
        deadline,
        nullptr /* RemoteTablet */,
        table_,
        local_client_->client.get(),
        write_request.get(),
        std::bind(&TwoDCOutputClient::WriteCDCRecordDone, this, _1, _2),
        UseLocalTserver());
    (**write_handle_).SendRpc();
  } else {
    LOG(WARNING) << "Invalid handle for CDC write, tablet ID: " << write_request->tablet_id();
  }
}

void TwoDCOutputClient::WriteCDCRecordDone(const Status& status, const WriteResponsePB& response) {
  // Handle response.
  rpc::RpcCommandPtr retained = nullptr;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    retained = rpcs_->Unregister(&write_handle_);
    if (shutdown_) {
      LOG(INFO) << "Aborting ApplyChanges since the client is shutting down.";
      return;
    }
  }
  if (!status.ok()) {
    HandleError(status, true /* done */);
    return;
  } else if (response.has_error()) {
    HandleError(StatusFromPB(response.error().status()), true /* done */);
    return;
  }
  cdc_consumer_->IncrementNumSuccessfulWriteRpcs();

  // See if we need to handle any more writes.
  std::unique_ptr <WriteRequestPB> write_request;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    write_request = write_strategy_->GetNextWriteRequest();
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
        HandleError(s, true);
      }
    } else {
      // Last record, return response to caller.
      HandleResponse();
    }
  }
}

void TwoDCOutputClient::HandleError(const Status& s, bool done) {
  LOG(ERROR) << "Error while applying replicated record: " << s
             << ", consumer tablet: " << consumer_tablet_info_.tablet_id;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    error_status_ = s;
    // In case of a consumer side tablet split, need to refresh the partitions.
    if (client::ClientError(error_status_) == client::ClientErrorCode::kTablePartitionListIsStale) {
      table_->MarkPartitionsAsStale();
    }
  }
  if (done) {
    HandleResponse();
  }
}

cdc::OutputClientResponse TwoDCOutputClient::PrepareResponse() {
  cdc::OutputClientResponse response;
  response.status = error_status_;
  if (response.status.ok()) {
    response.last_applied_op_id = op_id_;
    response.processed_record_count = processed_record_count_;
    response.wait_for_version = wait_for_version_;
  }
  op_id_ = consensus::MinimumOpId();
  processed_record_count_ = 0;
  return response;
}

void TwoDCOutputClient::SendResponse(const cdc::OutputClientResponse& resp) {
  apply_changes_clbk_(resp);
}

void TwoDCOutputClient::HandleResponse() {
  cdc::OutputClientResponse response;
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    response = PrepareResponse();
  }
  SendResponse(response);
}

bool TwoDCOutputClient::IncProcessedRecordCount() {
  processed_record_count_++;
  if (processed_record_count_ == record_count_) {
    done_processing_ = true;
  }
  CHECK(processed_record_count_ <= record_count_);
  return done_processing_;
}

std::unique_ptr<cdc::CDCOutputClient> CreateTwoDCOutputClient(
    CDCConsumer* cdc_consumer,
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const std::shared_ptr<CDCClient>& local_client,
    rpc::Rpcs* rpcs,
    std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk,
    bool use_local_tserver) {
  return std::make_unique<TwoDCOutputClient>(
      cdc_consumer, consumer_tablet_info, producer_tablet_info, local_client, rpcs,
      std::move(apply_changes_clbk), use_local_tserver);
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
