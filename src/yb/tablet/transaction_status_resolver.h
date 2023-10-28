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
//

#pragma once

#include <stdint.h>

#include <memory>
#include <type_traits>

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/transaction_participant.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace tablet {

struct TransactionStatusInfo {
  TabletId status_tablet;
  TransactionId transaction_id = TransactionId::Nil();
  TransactionStatus status;
  SubtxnSet aborted_subtxn_set;
  HybridTime status_ht;
  HybridTime coordinator_safe_time;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        status_tablet, transaction_id, status, status_ht, coordinator_safe_time);
  }
};

using TransactionStatusResolverCallback =
    std::function<void(const std::vector<TransactionStatusInfo>&)>;

// Utility class to resolve status of multiple transactions.
// It sends one request at a time to avoid generating too much load for transaction status
// resolution.
class TransactionStatusResolver {
 public:
  // If max_transactions_per_request is zero then resolution is skipped.
  TransactionStatusResolver(
      TransactionParticipantContext* participant_context, rpc::Rpcs* rpcs,
      int max_transactions_per_request,
      TransactionStatusResolverCallback callback);
  ~TransactionStatusResolver();

  // Shutdown this resolver.
  void Shutdown();

  // Add transaction id with its status tablet to the set of transactions to resolve.
  // Cannot be called after Start.
  void Add(const TabletId& status_tablet, const TransactionId& transaction_id);

  // Starts transaction resolution, no more adds are allowed after this point.
  void Start(CoarseTimePoint deadline);

  // Returns future for resolution status.
  std::future<Status> ResultFuture();

  bool Running() const;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace tablet
} // namespace yb
