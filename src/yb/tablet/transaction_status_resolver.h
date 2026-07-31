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
//

#pragma once

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <memory>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"
#include "yb/util/tostring.h"

namespace yb {
enum TransactionStatus : int;
namespace rpc {
class Rpcs;
}  // namespace rpc

namespace tablet {
class TransactionParticipantContext;

struct TransactionStatusInfo {
  TabletId status_tablet;
  TransactionId transaction_id = TransactionId::Nil();
  TransactionStatus status;
  SubtxnSet aborted_subtxn_set;
  HybridTime status_ht;
  HybridTime coordinator_safe_time;
  // Status containing the deadlock info if the transaction was aborted due to a deadlock.
  // Defaults to Status::OK() in all other cases.
  Status expected_deadlock_status = Status::OK();
  // Only relevant for docdb transactions of type PgClientSessionKind::kPgSession. The field is
  // used by the the wait-queue to resume deadlocked session advisory lock requests.
  PgSessionRequestVersion pg_session_req_version = 0;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(
        status_tablet, transaction_id, status, status_ht, coordinator_safe_time,
        expected_deadlock_status, pg_session_req_version);
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
