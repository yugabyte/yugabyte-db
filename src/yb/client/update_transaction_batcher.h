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

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/client/client_fwd.h"
#include "yb/client/transaction_rpc.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"

namespace yb::client {

namespace internal {
class BatchedUpdateTransaction;
} // namespace internal

// Sends UpdateTransaction requests to the leaders of their tablets, packing the requests that go
// to the same tablet server into one UpdateTransactions RPC. Uses natural batching: a request is
// sent at once when fewer than --update_transactions_batch_max_in_flight batches are in flight to
// its tablet server, otherwise it joins the next batch. A request that the batch does not complete
// successfully, for example because of a leader change or a tablet split, is resent alone through
// the regular UpdateTransaction RPC, which owns all the retry logic.
//
// One instance per YBClient, see YBClient::update_transaction_batcher().
class UpdateTransactionBatcher : public std::enable_shared_from_this<UpdateTransactionBatcher> {
 public:
  explicit UpdateTransactionBatcher(YBClient* client);
  ~UpdateTransactionBatcher();

  // Same contract as client::UpdateTransaction(deadline, nullptr, client, req, callback).
  rpc::RpcCommandPtr UpdateTransaction(
      CoarseTimePoint deadline, tserver::UpdateTransactionRequestPB* req,
      UpdateTransactionCallback callback);

  YBClient* client() const { return client_; }

 private:
  friend class internal::BatchedUpdateTransaction;
  using EntryPtr = std::shared_ptr<internal::BatchedUpdateTransaction>;

  struct Destination {
    internal::RemoteTabletServer* tserver = nullptr;
    std::mutex mutex;
    size_t in_flight = 0;
    std::vector<EntryPtr> queue;
  };

  struct BatchData;

  void Enqueue(internal::RemoteTabletServer* tserver, EntryPtr entry);
  void SendBatch(Destination* destination, std::vector<EntryPtr> entries);
  void BatchDone(Destination* destination, BatchData& data);
  // Takes the next batch from destination's queue, or decrements in_flight if it is empty.
  std::vector<EntryPtr> NextBatch(Destination* destination);
  std::vector<EntryPtr> NextBatchUnlocked(Destination* destination);

  YBClient* const client_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<Destination>> destinations_;
};

// Sends an UpdateTransaction request through the client's UpdateTransactionBatcher.
MUST_USE_RESULT rpc::RpcCommandPtr BatchedUpdateTransaction(
    CoarseTimePoint deadline, YBClient* client, tserver::UpdateTransactionRequestPB* req,
    UpdateTransactionCallback callback);

} // namespace yb::client
