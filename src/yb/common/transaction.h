//
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
//

#ifndef YB_COMMON_TRANSACTION_H
#define YB_COMMON_TRANSACTION_H

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/nil_generator.hpp>

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"

#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/uuid.h"

namespace rocksdb {

class DB;

}

namespace yb {

using TransactionId = boost::uuids::uuid;
typedef boost::hash<TransactionId> TransactionIdHash;

inline TransactionId GenerateTransactionId() { return Uuid::Generate(); }

// Processing mode:
//   LEADER - processing in leader.
//   NON_LEADER - processing in non leader.
YB_DEFINE_ENUM(ProcessingMode, (NON_LEADER)(LEADER));

// Decodes transaction id from its binary representation.
// Checks that slice contains only TransactionId.
Result<TransactionId> FullyDecodeTransactionId(const Slice& slice);

// Decodes transaction id from slice which contains binary encoding. Consumes corresponding bytes
// from slice.
Result<TransactionId> DecodeTransactionId(Slice* slice);

struct TransactionStatusResult {
  TransactionStatus status;

  // Meaning of status_time is related to status value.
  // PENDING - status_time reflects maximal guaranteed PENDING time, i.e. transaction cannot be
  // committed before this time.
  // COMMITTED - status_time is a commit time.
  // ABORTED - not used.
  HybridTime status_time;
};

typedef std::function<void(Result<TransactionStatusResult>)> TransactionStatusCallback;
struct TransactionMetadata;

class TransactionStatusManager {
 public:
  virtual ~TransactionStatusManager() {}

  // Checks whether this tablet knows that transaction is committed.
  // In case of success returns commit time of transaction, otherwise returns invalid time.
  virtual HybridTime LocalCommitTime(const TransactionId& id) = 0;

  // Fetches status of specified transaction at specified time from transaction coordinator.
  // Callback would be invoked in any case.
  // There are the following potential cases:
  // 1. Status tablet knows transaction id and could determine it's status at this time. In this
  // case status structure is filled with transaction status with corresponding status time.
  // 2. Status tablet don't know this transaction id, in this case status structure contains
  // ABORTED status.
  // 3. Status tablet could not determine transaction status at this time. In this case callback
  // will be invoked with TryAgain result.
  // 4. Any kind of network/timeout errors would be reflected in error passed to callback.
  virtual void RequestStatusAt(const TransactionId& id,
                               HybridTime time,
                               TransactionStatusCallback callback) = 0;

  virtual boost::optional<TransactionMetadata> Metadata(rocksdb::DB* db,
                                                        const TransactionId& id) = 0;

  virtual void Abort(const TransactionId& id, TransactionStatusCallback callback) = 0;
};

struct TransactionOperationContext {
  TransactionOperationContext(
      const TransactionId& transaction_id_, TransactionStatusManager* txn_status_manager_)
      : transaction_id(transaction_id_),
        txn_status_manager(*(DCHECK_NOTNULL(txn_status_manager_))) {}

  TransactionId transaction_id;
  TransactionStatusManager& txn_status_manager;
};

typedef boost::optional<TransactionOperationContext> TransactionOperationContextOpt;

struct TransactionMetadata {
  TransactionId transaction_id = boost::uuids::nil_uuid();
  IsolationLevel isolation = IsolationLevel::NON_TRANSACTIONAL;
  TabletId status_tablet;
  uint64_t priority;

  // Used for snapshot isolation (as read time and for conflict resolution).
  HybridTime start_time;

  static Result<TransactionMetadata> FromPB(const TransactionMetadataPB& source);

  void ToPB(TransactionMetadataPB* source) const;
};

bool operator==(const TransactionMetadata& lhs, const TransactionMetadata& rhs);

inline bool operator!=(const TransactionMetadata& lhs, const TransactionMetadata& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out, const TransactionMetadata& metadata);

} // namespace yb

#endif // YB_COMMON_TRANSACTION_H
