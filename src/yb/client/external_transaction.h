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

#ifndef YB_CLIENT_EXTERNAL_TRANSACTION_H
#define YB_CLIENT_EXTERNAL_TRANSACTION_H

#include <future>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "yb/client/client_fwd.h"
#include "yb/client/in_flight_op.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/rpc/rpc.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_uuid.h"

namespace yb {

namespace client {

struct ExternalTransactionMetadata {
  TransactionId transaction_id = TransactionId::Nil();
  TabletId status_tablet;
  uint64_t commit_ht;
  std::vector<TabletId> involved_tablet_ids;
};

class ExternalTransaction {
 public:
  ExternalTransaction() {}
  explicit ExternalTransaction(TransactionManager* transaction_manager_,
                               const ExternalTransactionMetadata& metadata);
  ~ExternalTransaction();
  void Commit(CommitCallback commit_callback);
    // Utility function for Commit.
  std::future<Status> CommitFuture();

 private:
  CoarseTimePoint TransactionRpcDeadline();
  void CommitDone(const Status& status,
                  const tserver::UpdateTransactionResponsePB& response,
                  CommitCallback commit_callback);

  TransactionManager* transaction_manager_;
  ExternalTransactionMetadata metadata_;
  rpc::Rpcs::Handle commit_handle_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_EXTERNAL_TRANSACTION_H
