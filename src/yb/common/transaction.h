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

#pragma once

#include <stdint.h>

#include <functional>
#include <iterator>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <boost/container/small_vector.hpp>
#include <boost/functional/hash/hash.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/hybrid_time.h"

#include "yb/gutil/template_util.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/metrics.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_uuid.h"
#include "yb/util/uint_set.h"

namespace yb {

YB_STRONGLY_TYPED_UUID_DECL(TransactionId);
using TransactionIdSet = std::unordered_set<TransactionId, TransactionIdHash>;
using SubTransactionId = uint32_t;

// By default, postgres SubTransactionId's propagated to DocDB start at 1, so we use this as a
// minimum value on the DocDB side as well. All intents written without an explicit SubTransactionId
// are assumed to belong to the subtransaction with this kMinSubTransactionId.
constexpr SubTransactionId kMinSubTransactionId = 1;

// Decodes transaction id from its binary representation.
// Checks that slice contains only TransactionId.
Result<TransactionId> FullyDecodeTransactionId(const Slice& slice);

// Decodes transaction id from slice which contains binary encoding. Consumes corresponding bytes
// from slice.
Result<TransactionId> DecodeTransactionId(Slice* slice);

using SubtxnSet = UnsignedIntSet<SubTransactionId>;

// SubtxnSetAndPB avoids repeated serialization of SubtxnSet, required for rpc calls, by storing
// the serialized proto form (SubtxnSetPB). A shared_ptr to a SubtxnSetAndPB object can be obtained
// by calling SubtxnSetAndPB::Create(const T& set_pb), where T should be some type where
// SubtxnSet::FromPB(set_pb.set()) is well defined. for instance, SubtxnSetPB/ ::yb::LWSubtxnSetPB.
class SubtxnSetAndPB {
 public:
  SubtxnSetAndPB() {}

  SubtxnSetAndPB(SubtxnSet&& set, SubtxnSetPB&& pb) : set_(std::move(set)), pb_(std::move(pb)) {}

  template <class T>
  static Result<std::shared_ptr<SubtxnSetAndPB>> Create(const T& set_pb) {
    auto res = SubtxnSet::FromPB(set_pb.set());
    RETURN_NOT_OK(res);
    SubtxnSetPB pb;
    res->ToPB(pb.mutable_set());
    std::shared_ptr<SubtxnSetAndPB>
        subtxn_info = std::make_shared<SubtxnSetAndPB>(std::move(*res), std::move(pb));
    return subtxn_info;
  }

  const SubtxnSet& set() const {
    return set_;
  }

  const SubtxnSetPB& pb() const {
    return pb_;
  }

  std::string ToString() const {
    // Skip including the redundant string representation of the proto form.
    return set_.ToString();
  }

 private:
  const SubtxnSet set_;
  const SubtxnSetPB pb_;
};

struct TransactionStatusResult {
  TransactionStatus status;

  // Meaning of status_time is related to status value.
  // PENDING - status_time reflects maximal guaranteed PENDING time, i.e. transaction cannot be
  // committed before this time.
  // COMMITTED - status_time is a commit time.
  // ABORTED - not used.
  HybridTime status_time;

  // Set of thus-far aborted subtransactions in this transaction.
  SubtxnSet aborted_subtxn_set;

  // Populating status_tablet field is optional, except when we report transaction promotion.
  TabletId status_tablet;

  // Status containing the deadlock info if the transaction was aborted due to a deadlock.
  // Defaults to Status::OK() in all other cases.
  Status expected_deadlock_status = Status::OK();

  TransactionStatusResult() {}

  TransactionStatusResult(TransactionStatus status_, HybridTime status_time_);

  TransactionStatusResult(
      TransactionStatus status_, HybridTime status_time_,
      SubtxnSet aborted_subtxn_set_, Status expected_deadlock_status_ = Status::OK());

  TransactionStatusResult(
      TransactionStatus status_, HybridTime status_time_, SubtxnSet aborted_subtxn_set_,
      TabletId status_tablet);

  static TransactionStatusResult Aborted() {
    return TransactionStatusResult(TransactionStatus::ABORTED, HybridTime());
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(status, status_time, aborted_subtxn_set, status_tablet);
  }
};

using SubtxnHasNonLockConflict = std::unordered_map<SubTransactionId, bool>;

struct BlockingTransactionData {
  TransactionId id;
  TabletId status_tablet;
  std::shared_ptr<SubtxnHasNonLockConflict> subtransactions = nullptr;

  std::string ToString() const {
    return Format("{id: $0, status_tablet: $1, subtransactions_size: $2}",
                  id, status_tablet,
                  subtransactions ? Format("$0", subtransactions->size()) : "null");
  }
};

inline std::ostream& operator<<(std::ostream& out, const TransactionStatusResult& result) {
  return out << "{ status: " << TransactionStatus_Name(result.status)
             << " status_time: " << result.status_time << " }";
}

typedef std::function<void(Result<TransactionStatusResult>)> TransactionStatusCallback;
struct TransactionMetadata;

YB_DEFINE_ENUM(TransactionLoadFlag, (kMustExist)(kCleanup));
typedef EnumBitSet<TransactionLoadFlag> TransactionLoadFlags;

// Used by RequestStatusAt.
struct StatusRequest {
  const TransactionId* id;
  HybridTime read_ht;
  HybridTime global_limit_ht;
  int64_t serial_no;
  const std::string* reason;
  TransactionLoadFlags flags;
  TransactionStatusCallback callback;
  // If non-null, populate status_tablet_id for known transactions in the same thread the request is
  // initiated.
  std::string* status_tablet_id = nullptr;

  std::string ToString() const {
    return Format("{ id: $0 read_ht: $1 global_limit_ht: $2 serial_no: $3 reason: $4 flags: $5}",
                  *id, read_ht, global_limit_ht, serial_no, *reason, flags);
  }
};

class RequestScope;

struct TransactionLocalState {
  HybridTime commit_ht;
  SubtxnSet aborted_subtxn_set;
};

class TransactionStatusManager {
 public:
  virtual ~TransactionStatusManager() {}

  // If this tablet is aware that this transaction has committed, returns the commit ht for the
  // transaction. Otherwise, returns HybridTime::kInvalid.
  virtual HybridTime LocalCommitTime(const TransactionId& id) = 0;

  // If this tablet is aware that this transaction has committed, returns the TransactionLocalState
  // for the transaction. Otherwise, returns boost::none.
  virtual boost::optional<TransactionLocalState> LocalTxnData(const TransactionId& id) = 0;

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
  virtual void RequestStatusAt(const StatusRequest& request) = 0;

  // Prepares metadata for provided protobuf. Either trying to extract it from pb, or fetch
  // from existing metadatas.
  virtual Result<TransactionMetadata> PrepareMetadata(const LWTransactionMetadataPB& pb) = 0;

  virtual void Abort(const TransactionId& id, TransactionStatusCallback callback) = 0;

  virtual Status Cleanup(TransactionIdSet&& set) = 0;

  // For each pair fills second with priority of transaction with id equals to first.
  virtual Status FillPriorities(
      boost::container::small_vector_base<std::pair<TransactionId, uint64_t>>* inout) = 0;

  virtual Result<boost::optional<TabletId>> FindStatusTablet(const TransactionId& id) = 0;

  // Returns minimal running hybrid time of all running transactions.
  virtual HybridTime MinRunningHybridTime() const = 0;

  virtual Result<HybridTime> WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) = 0;

  virtual const TabletId& tablet_id() const = 0;

  virtual void RecordConflictResolutionKeysScanned(int64_t num_keys) = 0;

  virtual void RecordConflictResolutionScanLatency(MonoDelta latency)  = 0;

 private:
  friend class RequestScope;

  // Registers new request assigning next serial no to it. So this serial no could be used
  // to check whether one request happened before another one.
  virtual Result<int64_t> RegisterRequest() = 0;

  // request_id - is request id returned by RegisterRequest, that should be unregistered.
  virtual void UnregisterRequest(int64_t request_id) = 0;
};

// Utility class that invokes UnregisterRequest on deletion.
class NODISCARD_CLASS RequestScope {
 public:
  RequestScope() noexcept : status_manager_(nullptr), request_id_(0) {}

  RequestScope(RequestScope&& rhs) noexcept
      : status_manager_(rhs.status_manager_), request_id_(rhs.request_id_) {
    rhs.status_manager_ = nullptr;
  }

  void operator=(RequestScope&& rhs) {
    Reset();
    status_manager_ = rhs.status_manager_;
    request_id_ = rhs.request_id_;
    rhs.status_manager_ = nullptr;
  }

  ~RequestScope() {
    Reset();
  }

  int64_t request_id() const { return request_id_; }

  RequestScope(const RequestScope&) = delete;
  void operator=(const RequestScope&) = delete;

  static Result<RequestScope> Create(TransactionStatusManager* status_manager) {
    return RequestScope(status_manager, VERIFY_RESULT(status_manager->RegisterRequest()));
  }

 private:
  RequestScope(TransactionStatusManager* status_manager, uint64_t request_id)
      : status_manager_(status_manager), request_id_(request_id) {
  }

  void Reset() {
    if (status_manager_) {
      status_manager_->UnregisterRequest(request_id_);
      status_manager_ = nullptr;
    }
  }

  TransactionStatusManager* status_manager_;
  int64_t request_id_;
};

// Represents all metadata tracked about subtransaction state by the client in support of postgres
// savepoints. Can be serialized and deserialized to/from SubTransactionMetadataPB. This should be
// sent by the client on any transactional read/write requests where a savepoint has been created,
// and finally on transaction commit.
struct SubTransactionMetadata {
  SubTransactionId subtransaction_id = kMinSubTransactionId;
  SubtxnSet aborted;

  void ToPB(SubTransactionMetadataPB* dest) const;

  static Result<SubTransactionMetadata> FromPB(
      const SubTransactionMetadataPB& source);

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(subtransaction_id, aborted);
  }

  bool operator==(const SubTransactionMetadata& other) const {
    return subtransaction_id == other.subtransaction_id &&
      aborted == other.aborted;
  }

  // Returns true if this is the default state, i.e. default subtransaction_id. This indicates
  // whether the client has interacted with savepoints at all in the context of a session. If true,
  // the client could, for example, skip sending subtransaction-related metadata in RPCs.
  // TODO(savepoints) -- update behavior and comment to track default aborted subtransaction state
  // as well.
  bool IsDefaultState() const;
};

std::ostream& operator<<(std::ostream& out, const SubTransactionMetadata& metadata);

struct TransactionOperationContext {
  TransactionOperationContext();

  TransactionOperationContext(
      const TransactionId& transaction_id_, TransactionStatusManager* txn_status_manager_);

  TransactionOperationContext(
      const TransactionId& transaction_id_,
      SubTransactionMetadata&& subtransaction_,
      TransactionStatusManager* txn_status_manager_);

  bool transactional() const;

  explicit operator bool() const {
    return txn_status_manager != nullptr;
  }

  TransactionId transaction_id;
  SubTransactionMetadata subtransaction;
  TransactionStatusManager* txn_status_manager;
};

inline std::ostream& operator<<(std::ostream& out, const TransactionOperationContext& context) {
  if (context.transactional()) {
    out << context.transaction_id;
  } else {
    out << "<non transactional>";
  }
  return out;
}

struct TransactionMetadata {
  TransactionId transaction_id = TransactionId::Nil();
  IsolationLevel isolation = IsolationLevel::NON_TRANSACTIONAL;
  TabletId status_tablet;

  // By default, a random value is picked for a newly created transaction.
  uint64_t priority = 0;

  // Used for snapshot isolation (as read time and for conflict resolution).
  // start_time is used only for backward compability during rolling update.
  HybridTime start_time;

  // Indicates whether this transaction is a local transaction or global transaction.
  TransactionLocality locality = TransactionLocality::GLOBAL;

  // Former transaction status tablet that the transaction was using prior to a move.
  TabletId old_status_tablet;

  static Result<TransactionMetadata> FromPB(const LWTransactionMetadataPB& source);
  static Result<TransactionMetadata> FromPB(const TransactionMetadataPB& source);

  void ToPB(LWTransactionMetadataPB* dest) const;
  void ToPB(TransactionMetadataPB* dest) const;

  void TransactionIdToPB(LWTransactionMetadataPB* dest) const;
  void TransactionIdToPB(TransactionMetadataPB* dest) const;

  std::string ToString() const {
    return Format(
        "{ transaction_id: $0 isolation: $1 status_tablet: $2 priority: $3 start_time: $4"
        " locality: $5 old_status_tablet: $6}",
        transaction_id, IsolationLevel_Name(isolation), status_tablet, priority, start_time,
        TransactionLocality_Name(locality), old_status_tablet);
  }

 private:
  template <class PB>
  static Result<TransactionMetadata> DoFromPB(const PB& source);
};

bool operator==(const TransactionMetadata& lhs, const TransactionMetadata& rhs);

inline bool operator!=(const TransactionMetadata& lhs, const TransactionMetadata& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& out, const TransactionMetadata& metadata);

MonoDelta TransactionRpcTimeout();
CoarseTimePoint TransactionRpcDeadline();

extern const char* kGlobalTransactionsTableName;
extern const std::string kMetricsSnapshotsTableName;
extern const std::string kTransactionTablePrefix;

YB_DEFINE_ENUM(CleanupType, (kGraceful)(kImmediate))

} // namespace yb
