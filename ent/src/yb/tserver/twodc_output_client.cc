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

#include "yb/cdc/cdc_consumer_util.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/gutil/strings/join.h"
#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/util/flag_tags.h"
#include "yb/util/net/net_util.h"

DEFINE_test_flag(bool, twodc_write_hybrid_time_override, false,
  "Override external_hybrid_time with initialHybridTimeValue for testing.");

DECLARE_int32(cdc_rpc_timeout_ms);

namespace yb {
namespace tserver {
namespace enterprise {

using rpc::Rpc;

class WriteAsyncRpc;

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

  void HandleWriteRpcResponse(const Status& s,
      const cdc::CDCRecordPB& record,
      std::unique_ptr<WriteResponsePB> resp);
  void RegisterRpc(rpc::RpcCommandPtr call, rpc::Rpcs::Handle* handle);
  rpc::RpcCommandPtr UnregisterRpc(rpc::Rpcs::Handle* handle);

  rpc::Rpcs::Handle InvalidRpcHandle() {
    return rpcs_.InvalidHandle();
  }

 private:
  void TabletLookupCallback(
      const cdc::GetChangesResponsePB* resp,
      const cdc::CDCRecordPB& record,
      const Result<client::internal::RemoteTabletPtr>& tablet);

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
};

Status TwoDCOutputClient::ApplyChanges(const cdc::GetChangesResponsePB* resp) {
  // ApplyChanges is called in a single threaded manner.
  // For all the changes in GetChangesResponsePB, it fans out applying the changes.
  // Once all changes have been applied (successfully or not), we invoke the callback which will
  // then either poll for next set of changes (in case of successful application) or will try to
  // re-apply.
  processed_record_count_.store(0, std::memory_order_release);
  record_count_.store(resp->records_size(), std::memory_order_release);
  DCHECK(resp->has_checkpoint());

  // Init class variables that threads will use.
  {
    std::lock_guard<rw_spinlock> l(lock_);
    DCHECK(consensus::OpIdEquals(op_id_, consensus::MinimumOpId()));
    op_id_ = resp->checkpoint().op_id();
    error_status_ = Status::OK();
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
  bool records_to_process = false;
  for (int i = 0; i < resp->records_size(); i++) {
    if (resp->records(i).key_size() == 0) {
      // Transaction status record, ignore.
      IncProcessedRecordCount();
    } else {
      // All KV-pairs within a single CDC record will be for the same row.
      // key(0).key() will contain the hash code for that row. We use this to lookup the tablet.
      client_->LookupTabletByKey(
          table_.get(),
          PartitionSchema::EncodeMultiColumnHashValue(
              boost::lexical_cast<uint16_t>(resp->records(i).key(0).key())),
          CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms),
          std::bind(&TwoDCOutputClient::TabletLookupCallback, this, resp, resp->records(i),
                    std::placeholders::_1));
      records_to_process = true;
    }
  }

  if (!records_to_process) {
    // Nothing to process, return success.
    HandleResponse();
  }
  return Status::OK();
}

void TwoDCOutputClient::HandleWriteRpcResponse(
    const Status& status, const cdc::CDCRecordPB& record, std::unique_ptr<WriteResponsePB> resp ) {
  if (!status.ok()) {
    HandleError(status, record);
  } else if (resp->has_error()) {
    HandleError(StatusFromPB(resp->error().status()), record);
  } else {
    IncProcessedRecordCount();
    HandleResponse();
  }
}

void TwoDCOutputClient::RegisterRpc(rpc::RpcCommandPtr call, rpc::Rpcs::Handle* handle) {
  rpcs_.Register(call, handle);
}

rpc::RpcCommandPtr TwoDCOutputClient::UnregisterRpc(rpc::Rpcs::Handle* handle) {
  return rpcs_.Unregister(handle);
}

void TwoDCOutputClient::TabletLookupCallback(
    const cdc::GetChangesResponsePB* resp,
    const cdc::CDCRecordPB& record,
    const Result<client::internal::RemoteTabletPtr>& tablet) {
  if (!tablet.ok()) {
    HandleError(tablet.status(), record);
    return;
  }

  auto ts = tablet->get()->LeaderTServer();
  if (ts == nullptr) {
    HandleError(STATUS_FORMAT(IllegalState,
        "Cannot find leader tserver for tablet $0", tablet->get()->tablet_id()), record);
    return;
  }

  Status s = ts->InitProxy(client_.get());
  if (!s.ok()) {
    HandleError(s, record);
    return;
  }

  auto deadline = CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms);
  auto write_rpc = rpc::StartRpc<WriteAsyncRpc>(
      deadline, client_->messenger(), &client_->proxy_cache(), ts->proxy(),
      tablet->get()->tablet_id(), record, this);
}

void TwoDCOutputClient::HandleError(const Status& s, const cdc::CDCRecordPB& bad_record) {
  IncProcessedRecordCount();
  LOG(ERROR) << "Error while applying replicated record for " << bad_record.DebugString()
             << ": " << s.ToString();
  {
    std::lock_guard<rw_spinlock> l(lock_);
    error_status_ = s;
  }
  HandleResponse();
}

void TwoDCOutputClient::HandleResponse() {
  if (processed_record_count_.load(std::memory_order_acquire) ==
      record_count_.load(std::memory_order_acquire)) {
    cdc::OutputClientResponse response;
    {
      std::lock_guard<rw_spinlock> l(lock_);
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

class WriteAsyncRpc : public Rpc {
 public:
  WriteAsyncRpc(
      CoarseTimePoint deadline,
      rpc::Messenger* messenger,
      rpc::ProxyCache* proxy_cache,
      const std::shared_ptr<TabletServerServiceProxy>& proxy,
      const TabletId& tablet_id,
      const cdc::CDCRecordPB& record,
      TwoDCOutputClient* twodc_client) :
      Rpc(deadline, messenger, proxy_cache),
      proxy_(proxy),
      tablet_id_(tablet_id),
      record_(record),
      resp_(std::make_unique<WriteResponsePB>()),
      twodc_client_(twodc_client),
      retained_self_(twodc_client->InvalidRpcHandle()) {}

  void SendRpc() override;

  string ToString() const override;

  virtual ~WriteAsyncRpc() = default;

 private:
  void Finished(const Status& status) override;

  std::shared_ptr<TabletServerServiceProxy> proxy_;
  TabletId tablet_id_;
  cdc::CDCRecordPB record_;
  std::unique_ptr<WriteResponsePB> resp_;
  TwoDCOutputClient* twodc_client_;
  rpc::Rpcs::Handle retained_self_;
};

void WriteAsyncRpc::SendRpc() {
  twodc_client_->RegisterRpc(shared_from_this(), &retained_self_);

  auto now = CoarseMonoClock::Now();
  if (retrier().deadline() < now) {
    Finished(STATUS(TimedOut, "WriteAsyncRpc timed out after deadline expired"));
    return;
  }

  auto rpc_deadline = now + MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms);
  mutable_retrier()->mutable_controller()->set_deadline(
      std::min(rpc_deadline, retrier().deadline()));

  WriteRequestPB req;
  req.set_tablet_id(tablet_id_);
  if (FLAGS_twodc_write_hybrid_time_override) {
    // Used only for testing external hybrid time.
    req.set_external_hybrid_time(yb::kInitialHybridTimeValue);
  } else {
    req.set_external_hybrid_time(record_.time());
  }
  for (const auto& kv_pair : record_.changes()) {
    auto* write_pair = req.mutable_write_batch()->add_write_pairs();
    write_pair->set_key(kv_pair.key());
    write_pair->set_value(kv_pair.value().binary_value());
  }

  proxy_->WriteAsync(
      req, resp_.get(), mutable_retrier()->mutable_controller(),
      std::bind(&WriteAsyncRpc::Finished, this, Status::OK()));
}

string WriteAsyncRpc::ToString() const {
  return strings::Substitute("WriteAsyncRpc(tablet_id: $0, num_attempts: $1)",
                             tablet_id_, num_attempts());
}

void WriteAsyncRpc::Finished(const Status& status) {
  Status new_status = status;
  if (new_status.ok() &&
      mutable_retrier()->HandleResponse(this, &new_status, rpc::RetryWhenBusy::kFalse)) {
    return;
  }
  if (new_status.ok() && resp_->has_error()) {
    new_status = StatusFromPB(resp_->error().status());
  }
  auto retained_self = twodc_client_->UnregisterRpc(&retained_self_);
  twodc_client_->HandleWriteRpcResponse(new_status, record_, std::move(resp_));
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
