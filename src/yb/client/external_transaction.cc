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

ExternalTransaction::ExternalTransaction(TransactionManager* transaction_manager)
    : transaction_manager_(transaction_manager),
      handle_(transaction_manager_->rpcs().InvalidHandle()) {
}

ExternalTransaction::~ExternalTransaction() {
  transaction_manager_->rpcs().Abort({&handle_});
}

void ExternalTransaction::Create(const ExternalTransactionMetadata& metadata,
                                 CreateCallback create_callback) {
  LOG(INFO) << "Create for " << metadata.transaction_id;
  tserver::UpdateTransactionRequestPB req;
  req.set_tablet_id(metadata.status_tablet);
  req.set_propagated_hybrid_time(transaction_manager_->Now().ToUint64());
  req.set_is_external(true);
  auto& state = *req.mutable_state();
  state.set_transaction_id(metadata.transaction_id.data(), metadata.transaction_id.size());
  state.set_status(TransactionStatus::CREATED);
  state.set_external_hybrid_time(metadata.hybrid_time);
  transaction_manager_->rpcs().RegisterAndStart(
      UpdateTransaction(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          transaction_manager_->client(),
          &req,
          [this, create_callback](const auto& status, const auto& req, const auto& resp) {
            this->CreateDone(status, resp, create_callback);
          }),
      &handle_);
}

std::future<Status> ExternalTransaction::CreateFuture(const ExternalTransactionMetadata& metadata) {
  return MakeFuture<Status>([&](auto callback) {
    Create(metadata, std::move(callback));
  });
}

void ExternalTransaction::CreateDone(const Status& status,
                                     const tserver::UpdateTransactionResponsePB& response,
                                     CreateCallback create_callback) {
  transaction_manager_->rpcs().Unregister(&handle_);
  create_callback(status);
}

void ExternalTransaction::Commit(const ExternalTransactionMetadata& metadata,
                                 CommitCallback commit_callback) {
  LOG(INFO) << "Commit for " << metadata.transaction_id;
  tserver::UpdateTransactionRequestPB req;
  req.set_tablet_id(metadata.status_tablet);
  req.set_propagated_hybrid_time(transaction_manager_->Now().ToUint64());
  req.set_is_external(true);
  auto& state = *req.mutable_state();
  state.set_transaction_id(metadata.transaction_id.data(), metadata.transaction_id.size());
  state.set_status(TransactionStatus::COMMITTED);
  state.set_external_hybrid_time(metadata.hybrid_time);
  for (const auto& involved_tablet_id : metadata.involved_tablet_ids) {
    *state.add_tablets() = involved_tablet_id;
  }
  transaction_manager_->rpcs().RegisterAndStart(
      UpdateTransaction(
          TransactionRpcDeadline(),
          nullptr /* tablet */,
          transaction_manager_->client(),
          &req,
          [this, commit_callback](const auto& status, const auto& req, const auto& resp) {
            this->CommitDone(status, resp, commit_callback);
          }),
      &handle_);
}

std::future<Status> ExternalTransaction::CommitFuture(const ExternalTransactionMetadata& metadata) {
  return MakeFuture<Status>([&](auto callback) {
    Commit(metadata, std::move(callback));
  });
}

void ExternalTransaction::CommitDone(const Status& status,
                                     const tserver::UpdateTransactionResponsePB& response,
                                     CommitCallback commit_callback) {
  transaction_manager_->rpcs().Unregister(&handle_);
  commit_callback(status);
}

CoarseTimePoint ExternalTransaction::TransactionRpcDeadline() {
  return CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_transaction_rpc_timeout_ms);
}

} // namespace client

} // namespace yb
