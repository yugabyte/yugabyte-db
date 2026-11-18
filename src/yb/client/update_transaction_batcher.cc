// Copyright (c) YugabyteDB, Inc.
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

#include "yb/client/update_transaction_batcher.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_uint32(update_transactions_batch_max_size, 256,
    "Maximum number of UpdateTransaction requests in one UpdateTransactions RPC.");

DEFINE_RUNTIME_uint32(update_transactions_batch_max_in_flight, 2,
    "Maximum number of UpdateTransactions RPCs in flight to one tablet server. Requests issued "
    "while this many are in flight wait for the next batch.");

DECLARE_bool(rpc_cheap_callbacks_on_reactor);

namespace yb::client {

namespace internal {

class BatchedUpdateTransaction : public rpc::RpcCommand {
 public:
  BatchedUpdateTransaction(
      std::shared_ptr<UpdateTransactionBatcher> batcher, CoarseTimePoint deadline,
      tserver::UpdateTransactionRequestPB* req, UpdateTransactionCallback callback)
      : batcher_(std::move(batcher)), deadline_(deadline), callback_(std::move(callback)) {
    req_.Swap(req);
  }

  void SendRpc() override {
    auto self = shared_self();
    batcher_->client()->LookupTabletById(
        req_.tablet_id(), /* table= */ nullptr, master::IncludeHidden::kFalse,
        master::IncludeDeleted::kFalse, deadline_,
        [self](const Result<RemoteTabletPtr>& result) {
          self->TabletLookedUp(result);
        },
        UseCache::kTrue);
  }

  void Finished(const Status& status) override {}

  void Abort() override {
    aborted_.store(true, std::memory_order_release);
    rpc::RpcCommandPtr alone;
    {
      std::lock_guard lock(mutex_);
      alone = alone_;
    }
    if (alone) {
      alone->Abort();
    }
  }

  CoarseTimePoint deadline() const override { return deadline_; }

  std::string ToString() const override {
    return Format("BatchedUpdateTransaction: $0", req_.ShortDebugString());
  }

  const tserver::UpdateTransactionRequestPB& request() const { return req_; }

  bool aborted() const { return aborted_.load(std::memory_order_acquire); }

  // Resends the request alone through the regular UpdateTransaction RPC.
  void SendAlone() {
    if (aborted()) {
      CompleteAborted();
      return;
    }
    auto req = req_;
    auto self = shared_self();
    auto alone = client::UpdateTransaction(
        deadline_, /* tablet= */ nullptr, batcher_->client(), &req,
        [self](const Status& status, const tserver::UpdateTransactionRequestPB&,
               const tserver::UpdateTransactionResponsePB& resp) {
      self->Complete(status, resp);
    });
    {
      std::lock_guard lock(mutex_);
      alone_ = alone;
    }
    alone->SendRpc();
  }

  void Complete(const Status& status, const tserver::UpdateTransactionResponsePB& resp) {
    if (completed_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    callback_(status, req_, resp);
  }

  void CompleteAborted() {
    static const tserver::UpdateTransactionResponsePB kEmptyResponse;
    Complete(STATUS(Aborted, "UpdateTransaction aborted"), kEmptyResponse);
  }

 private:
  std::shared_ptr<BatchedUpdateTransaction> shared_self() {
    return std::static_pointer_cast<BatchedUpdateTransaction>(shared_from_this());
  }

  void TabletLookedUp(const Result<RemoteTabletPtr>& result) {
    if (aborted()) {
      CompleteAborted();
      return;
    }
    RemoteTabletServer* leader = result.ok() ? (*result)->LeaderTServer() : nullptr;
    if (!leader) {
      // Let the regular RPC deal with lookup failures and unknown leaders.
      SendAlone();
      return;
    }
    batcher_->Enqueue(leader, shared_self());
  }

  const std::shared_ptr<UpdateTransactionBatcher> batcher_;
  const CoarseTimePoint deadline_;
  const UpdateTransactionCallback callback_;
  tserver::UpdateTransactionRequestPB req_;
  std::atomic<bool> aborted_{false};
  std::atomic<bool> completed_{false};
  std::mutex mutex_;
  rpc::RpcCommandPtr alone_ GUARDED_BY(mutex_);
};

} // namespace internal

struct UpdateTransactionBatcher::BatchData {
  std::vector<EntryPtr> entries;
  tserver::UpdateTransactionsRequestPB req;
  tserver::UpdateTransactionsResponsePB resp;
  rpc::RpcController controller;
};

UpdateTransactionBatcher::UpdateTransactionBatcher(YBClient* client) : client_(client) {}

UpdateTransactionBatcher::~UpdateTransactionBatcher() = default;

rpc::RpcCommandPtr UpdateTransactionBatcher::UpdateTransaction(
    CoarseTimePoint deadline, tserver::UpdateTransactionRequestPB* req,
    UpdateTransactionCallback callback) {
  return std::make_shared<internal::BatchedUpdateTransaction>(
      shared_from_this(), deadline, req, std::move(callback));
}

void UpdateTransactionBatcher::Enqueue(internal::RemoteTabletServer* tserver, EntryPtr entry) {
  Destination* destination;
  {
    std::lock_guard lock(mutex_);
    auto& ptr = destinations_[tserver->permanent_uuid()];
    if (!ptr) {
      ptr = std::make_unique<Destination>();
      ptr->tserver = tserver;
    }
    destination = ptr.get();
  }
  std::vector<EntryPtr> batch;
  {
    std::lock_guard lock(destination->mutex);
    destination->queue.push_back(std::move(entry));
    if (destination->in_flight >= std::max<uint32_t>(
            FLAGS_update_transactions_batch_max_in_flight, 1)) {
      return;
    }
    ++destination->in_flight;
    batch = NextBatchUnlocked(destination);
  }
  SendBatch(destination, std::move(batch));
}

std::vector<UpdateTransactionBatcher::EntryPtr> UpdateTransactionBatcher::NextBatchUnlocked(
    Destination* destination) {
  std::vector<EntryPtr> result;
  auto max_size = std::max<size_t>(FLAGS_update_transactions_batch_max_size, 1);
  if (destination->queue.size() <= max_size) {
    result.swap(destination->queue);
  } else {
    auto split = destination->queue.begin() + max_size;
    result.assign(std::make_move_iterator(destination->queue.begin()),
                  std::make_move_iterator(split));
    destination->queue.erase(destination->queue.begin(), split);
  }
  return result;
}

std::vector<UpdateTransactionBatcher::EntryPtr> UpdateTransactionBatcher::NextBatch(
    Destination* destination) {
  std::lock_guard lock(destination->mutex);
  if (destination->queue.empty()) {
    --destination->in_flight;
    return {};
  }
  return NextBatchUnlocked(destination);
}

void UpdateTransactionBatcher::SendBatch(Destination* destination, std::vector<EntryPtr> entries) {
  for (;;) {
    auto data = std::make_shared<BatchData>();
    auto now = CoarseMonoClock::now();
    auto deadline = CoarseTimePoint::max();
    uint64_t propagated_hybrid_time = 0;
    for (auto& entry : entries) {
      if (entry->aborted()) {
        entry->CompleteAborted();
        continue;
      }
      if (entry->deadline() <= now) {
        // The single RPC reports the timeout in the regular way.
        entry->SendAlone();
        continue;
      }
      deadline = std::min(deadline, entry->deadline());
      propagated_hybrid_time = std::max(
          propagated_hybrid_time, entry->request().propagated_hybrid_time());
      *data->req.add_requests() = entry->request();
      data->entries.push_back(std::move(entry));
    }
    if (!data->entries.empty()) {
      auto status = destination->tserver->InitProxy(client_);
      if (status.ok()) {
        data->req.set_propagated_hybrid_time(propagated_hybrid_time);
        data->controller.set_deadline(deadline);
        if (FLAGS_rpc_cheap_callbacks_on_reactor) {
          data->controller.set_invoke_callback_mode(rpc::InvokeCallbackMode::kReactorThread);
        }
        auto proxy = destination->tserver->proxy();
        proxy->UpdateTransactionsAsync(
            data->req, &data->resp, &data->controller,
            [self = shared_from_this(), destination, data] {
          self->BatchDone(destination, *data);
        });
        return;
      }
      for (auto& entry : data->entries) {
        entry->SendAlone();
      }
    }
    entries = NextBatch(destination);
    if (entries.empty()) {
      return;
    }
  }
}

void UpdateTransactionBatcher::BatchDone(Destination* destination, BatchData& data) {
  auto status = data.controller.status();
  bool batch_ok = status.ok() && !data.resp.has_error() &&
                  data.resp.responses_size() == std::ssize(data.entries);
  if (!batch_ok) {
    YB_LOG_EVERY_N_SECS(WARNING, 10)
        << "UpdateTransactions to " << destination->tserver->permanent_uuid() << " failed: "
        << (status.ok() ? data.resp.ShortDebugString() : status.ToString())
        << ", resending " << data.entries.size() << " requests one by one";
  }
  for (size_t i = 0; i != data.entries.size(); ++i) {
    auto& entry = data.entries[i];
    if (batch_ok) {
      auto& resp = *data.resp.mutable_responses(narrow_cast<int>(i));
      if (!resp.has_error()) {
        if (data.resp.has_propagated_hybrid_time()) {
          resp.set_propagated_hybrid_time(data.resp.propagated_hybrid_time());
        }
        entry->Complete(Status::OK(), resp);
        continue;
      }
    }
    entry->SendAlone();
  }
  auto next = NextBatch(destination);
  if (!next.empty()) {
    SendBatch(destination, std::move(next));
  }
}

rpc::RpcCommandPtr BatchedUpdateTransaction(
    CoarseTimePoint deadline, YBClient* client, tserver::UpdateTransactionRequestPB* req,
    UpdateTransactionCallback callback) {
  return client->update_transaction_batcher().UpdateTransaction(
      deadline, req, std::move(callback));
}

} // namespace yb::client
