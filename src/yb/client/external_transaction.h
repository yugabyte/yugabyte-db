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

#pragma once

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
  enum class OperationType {
    CREATE = 0,
    COMMIT = 1
  };
  TransactionId transaction_id;
  TabletId status_tablet;
  OperationType operation_type;
  uint64_t hybrid_time;
  std::vector<TabletId> involved_tablet_ids;

  std::string OperationTypeToString() const {
    switch (operation_type) {
      case OperationType::CREATE:
        return "CREATE";
      case OperationType::COMMIT:
        return "COMMIT";
      default:
        return "UNKNOWN";
    }
  }
};

class ExternalTransaction {
 public:
  explicit ExternalTransaction(TransactionManager* transaction_manager_);
  ~ExternalTransaction();
  void Create(const ExternalTransactionMetadata& metadata, CreateCallback create_callback);
  void Commit(const ExternalTransactionMetadata& metadata, CommitCallback commit_callback);
    // Utility function for Commit.
  std::future<Status> CommitFuture(const ExternalTransactionMetadata& metadata);
  std::future<Status> CreateFuture(const ExternalTransactionMetadata& metadata);

 private:
  CoarseTimePoint TransactionRpcDeadline();
  void CommitDone(const Status& status,
                  const tserver::UpdateTransactionResponsePB& response,
                  CommitCallback commit_callback);

  void CreateDone(const Status& status,
                  const tserver::UpdateTransactionResponsePB& response,
                  CreateCallback create_callback);

  TransactionManager* transaction_manager_;
  rpc::Rpcs::Handle handle_;
};

} // namespace client
} // namespace yb
