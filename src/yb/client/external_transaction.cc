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

#include <chrono>

#include "yb/client/client_fwd.h"
#include "yb/client/external_transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_rpc.h"

#include "yb/common/common.pb.h"

#include "yb/util/async_util.h"

using namespace std::literals;

DECLARE_int64(transaction_rpc_timeout_ms);

namespace yb {

namespace client {

ExternalTransaction::ExternalTransaction(TransactionManager* transaction_manager,
                                         const ExternalTransactionMetadata& metadata)
    : transaction_manager_(transaction_manager),
      metadata_(metadata),
      commit_handle_(transaction_manager_->rpcs().InvalidHandle()) {
}

ExternalTransaction::~ExternalTransaction() {
  transaction_manager_->rpcs().Abort({&commit_handle_});
}

void ExternalTransaction::Commit(CommitCallback commit_callback) {
  tserver::UpdateTransactionRequestPB req;
  req.set_tablet_id(metadata_.status_tablet);
  req.set_propagated_hybrid_time(transaction_manager_->Now().ToUint64());
  req.set_is_external(true);
  auto& state = *req.mutable_state();
  state.set_transaction_id(metadata_.transaction_id.data(), metadata_.transaction_id.size());
  state.set_status(TransactionStatus::COMMITTED);
  state.set_external_commit_ht(metadata_.commit_ht);
  for (const auto& involved_tablet_id : metadata_.involved_tablet_ids) {
    *state.add_tablets() = involved_tablet_id;
  }
  transaction_manager_->rpcs().RegisterAndStart(
      UpdateTransaction(
          TransactionRpcDeadline(),
          nullptr,
          transaction_manager_->client(),
          &req,
          [this, commit_callback](const auto& status, const auto& req, const auto& resp) {
            this->CommitDone(status, resp, commit_callback);
          }),
      &commit_handle_);
}

std::future<Status> ExternalTransaction::CommitFuture() {
  return MakeFuture<Status>([this](auto callback) {
    Commit(std::move(callback));
  });
}

void ExternalTransaction::CommitDone(const Status& status,
                                     const tserver::UpdateTransactionResponsePB& response,
                                     CommitCallback commit_callback) {
  transaction_manager_->rpcs().Unregister(&commit_handle_);
  commit_callback(status);
}

CoarseTimePoint ExternalTransaction::TransactionRpcDeadline() {
  return CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_transaction_rpc_timeout_ms);
}

} // namespace client

} // namespace yb
