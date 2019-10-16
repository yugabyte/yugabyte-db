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
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/gutil/strings/join.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/cdc_rpc.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"

DEFINE_test_flag(bool, twodc_write_hybrid_time_override, false,
  "Override external_hybrid_time with initialHybridTimeValue for testing.");

DECLARE_int32(cdc_rpc_timeout_ms);

namespace yb {
namespace tserver {
namespace enterprise {

using rpc::Rpc;

class TwoDCOutputClient : public cdc::CDCOutputClient {
 public:
  TwoDCOutputClient(
      const cdc::ConsumerTabletInfo& consumer_tablet_info,
      const std::shared_ptr<client::YBClient>& client,
      std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk) :
      consumer_tablet_info_(consumer_tablet_info),
      client_(client),
      apply_changes_clbk_(std::move(apply_changes_clbk)) {}

  ~TwoDCOutputClient() {
    Shutdown();
  }

  void Shutdown() override {
    rpcs_.Shutdown();
  }

  CHECKED_STATUS ApplyChanges(const cdc::GetChangesResponsePB* resp) override;

  rpc::Rpcs::Handle RegisterRpc(rpc::RpcCommandPtr call);
  rpc::RpcCommandPtr UnregisterRpc(rpc::Rpcs::Handle* handle);

  void WriteCDCRecordDone(
      const Status& status, const WriteResponsePB& response, const size_t record_idx);

 private:
  void TabletLookupCallback(
      const size_t record_idx, const Result<client::internal::RemoteTabletPtr>& tablet);
  void SendCDCWriteToTablet(const size_t record_idx);

  void IncProcessedRecordCount();

  void HandleResponse();
  void HandleError(const Status& s, const cdc::CDCRecordPB& bad_record);

  cdc::ConsumerTabletInfo consumer_tablet_info_;
  std::shared_ptr<client::YBClient> client_;
  std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk_;

  rpc::Rpcs rpcs_;
  std::shared_ptr<client::YBTable> table_;

  // Used to protect error_status_ and op_id_.
  mutable rw_spinlock lock_;
  Status error_status_;
  OpIdPB op_id_ = consensus::MinimumOpId();

  std::atomic<uint32_t> processed_record_count_{0};
  std::atomic<uint32_t> record_count_{0};

  // Map of CDCRecord index -> RemoteTablet.
  std::unordered_map<size_t, client::internal::RemoteTablet*> records_;

  // This will cache the response to an ApplyChanges() request.
  cdc::GetChangesResponsePB resp_;
};

Status TwoDCOutputClient::ApplyChanges(const cdc::GetChangesResponsePB* resp) {
  // ApplyChanges is called in a single threaded manner.
  // For all the changes in GetChangesResponsePB, we first fan out and find the tablet for
  // every record key.
  // Then we apply the records in the same order in which we received them.
  // Once all changes have been applied (successfully or not), we invoke the callback which will
  // then either poll for next set of changes (in case of successful application) or will try to
  // re-apply.
  processed_record_count_.store(0, std::memory_order_release);
  record_count_.store(resp->records_size(), std::memory_order_release);
  DCHECK(resp->has_checkpoint());
  resp_.Clear();

  // Init class variables that threads will use.
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    DCHECK(consensus::OpIdEquals(op_id_, consensus::MinimumOpId()));
    op_id_ = resp->checkpoint().op_id();
    error_status_ = Status::OK();
    records_.clear();
  }

  // Ensure we have records.
  if (record_count_.load(std::memory_order_acquire) == 0) {
    HandleResponse();
    return Status::OK();
  }

  // Ensure we have a connection to the consumer table cached.
  if (!table_) {
    Status s = client_->OpenTable(consumer_tablet_info_.table_id, &table_);
    if (!s.ok()) {
      cdc::OutputClientResponse response;
      response.status = s;
      apply_changes_clbk_(response);
      return s;
    }
  }

  // Inspect all records in the response.
  for (int i = 0; i < resp->records_size(); i++) {
    if (resp->records(i).key_size() == 0) {
      // Transaction status record, ignore for now.
      // Support for handling transactions will be added in future.
      IncProcessedRecordCount();
    } else {
      auto record_pb = resp_.add_records();
      *record_pb = resp->records(i);
    }
  }

  for (int i = 0; i < resp_.records_size(); i++) {
    // All KV-pairs within a single CDC record will be for the same row.
    // key(0).key() will contain the hash code for that row. We use this to lookup the tablet.
    client_->LookupTabletByKey(
        table_.get(),
        PartitionSchema::EncodeMultiColumnHashValue(
            boost::lexical_cast<uint16_t>(resp_.records(i).key(0).key())),
        CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms),
        std::bind(&TwoDCOutputClient::TabletLookupCallback, this, i, std::placeholders::_1));
  }

  if (resp_.records_size() == 0) {
    // Nothing to process, return success.
    HandleResponse();
  }
  return Status::OK();
}

rpc::Rpcs::Handle TwoDCOutputClient::RegisterRpc(rpc::RpcCommandPtr call) {
  return rpcs_.Register(call);
}

rpc::RpcCommandPtr TwoDCOutputClient::UnregisterRpc(rpc::Rpcs::Handle* handle) {
  return rpcs_.Unregister(handle);
}

void TwoDCOutputClient::TabletLookupCallback(
    const size_t record_idx,
    const Result<client::internal::RemoteTabletPtr>& tablet) {
  if (!tablet.ok()) {
    IncProcessedRecordCount();
    HandleError(tablet.status(), resp_.records(record_idx));
    return;
  }

  {
    std::lock_guard<decltype(lock_)> l(lock_);
    records_.emplace(record_idx, tablet->get());
  }

  IncProcessedRecordCount();
  if (processed_record_count_.load(std::memory_order_acquire) ==
      record_count_.load(std::memory_order_acquire)) {

    // Found tablets for all records, now we should write the records.
    // But first, check if there were any errors during tablet lookup for any record.
    bool has_error = false;
    {
      std::shared_lock<decltype(lock_)> l(lock_);
      if (!error_status_.ok()) {
        has_error = true;
      }
    }

    if (has_error) {
      // Return error, if any, without applying records.
      HandleResponse();
    } else {
      // Apply the writes on consumer.
      SendCDCWriteToTablet(0);
    }
  }
}

void TwoDCOutputClient::SendCDCWriteToTablet(const size_t record_idx) {
  client::internal::RemoteTablet* tablet;
  {
    std::shared_lock<decltype(lock_)> l(lock_);
    tablet = records_[record_idx];
  }

  WriteRequestPB req;
  req.set_tablet_id(tablet->tablet_id());
  if (PREDICT_FALSE(FLAGS_twodc_write_hybrid_time_override)) {
    // Used only for testing external hybrid time.
    req.set_external_hybrid_time(yb::kInitialHybridTimeValue);
  } else {
    req.set_external_hybrid_time(resp_.records(record_idx).time());
  }

  for (const auto& kv_pair : resp_.records(record_idx).changes()) {
    auto* write_pair = req.mutable_write_batch()->add_write_pairs();
    write_pair->set_key(kv_pair.key());
    write_pair->set_value(kv_pair.value().binary_value());
  }

  auto deadline = CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms);
  auto write_rpc = WriteCDCRecord(
      deadline,
      tablet,
      client_.get(),
      std::bind(&TwoDCOutputClient::RegisterRpc, this, std::placeholders::_1),
      std::bind(&TwoDCOutputClient::UnregisterRpc, this, std::placeholders::_1),
      &req,
      std::bind(&TwoDCOutputClient::WriteCDCRecordDone, this,
                std::placeholders::_1, std::placeholders::_2, record_idx));
}

void TwoDCOutputClient::WriteCDCRecordDone(
    const Status& status, const WriteResponsePB& response, size_t record_idx) {
  VLOG(1) << "Wrote CDC record: " << status << ": " << response.DebugString();
  if (!status.ok()) {
    HandleError(status, resp_.records(record_idx));
    return;
  } else if (response.has_error()) {
    HandleError(StatusFromPB(response.error().status()), resp_.records(record_idx));
    return;
  }

  if (record_idx == resp_.records_size() - 1) {
    // Last record, return response to caller.
    HandleResponse();
  } else {
    SendCDCWriteToTablet(record_idx + 1);
  }
}

void TwoDCOutputClient::HandleError(const Status& s, const cdc::CDCRecordPB& bad_record) {
  LOG(ERROR) << "Error while applying replicated record for " << bad_record.DebugString()
             << ": " << s.ToString();
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    error_status_ = s;
  }
  HandleResponse();
}

void TwoDCOutputClient::HandleResponse() {
  if (processed_record_count_.load(std::memory_order_acquire) ==
      record_count_.load(std::memory_order_acquire)) {
    cdc::OutputClientResponse response;
    {
      std::lock_guard<decltype(lock_)> l(lock_);
      response.status = error_status_;
      if (response.status.ok()) {
        response.last_applied_op_id = op_id_;
      }
      op_id_ = consensus::MinimumOpId();
    }
    apply_changes_clbk_(response);
  }
}

void TwoDCOutputClient::IncProcessedRecordCount() {
  processed_record_count_.fetch_add(1, std::memory_order_acq_rel);
}

std::unique_ptr<cdc::CDCOutputClient> CreateTwoDCOutputClient(
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    const std::shared_ptr<client::YBClient>& client,
    std::function<void(const cdc::OutputClientResponse& response)> apply_changes_clbk) {
  return std::make_unique<TwoDCOutputClient>(
      consumer_tablet_info, client, std::move(apply_changes_clbk));
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
