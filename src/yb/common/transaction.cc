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
#include "yb/common/transaction.h"

#include "yb/common/common.messages.h"

#include "yb/util/compare_util.h"
#include "yb/util/result.h"
#include "yb/util/tsan_util.h"
#include "yb/util/flags.h"

using namespace std::literals;

DEFINE_RUNTIME_int64(transaction_rpc_timeout_ms, 5000 * yb::kTimeMultiplier,
                     "Timeout used by transaction related RPCs in milliseconds.");

namespace yb {

YB_STRONGLY_TYPED_UUID_IMPL(TransactionId);

const char* kGlobalTransactionsTableName = "transactions";
const std::string kMetricsSnapshotsTableName = "metrics";
const std::string kTransactionTablePrefix = "transactions_";

TransactionStatusResult::TransactionStatusResult(
    TransactionStatus status_, HybridTime status_time_, Status expected_deadlock_status_)
    : TransactionStatusResult(status_, status_time_, SubtxnSet(), expected_deadlock_status_) {}

TransactionStatusResult::TransactionStatusResult(
    TransactionStatus status_, HybridTime status_time_, SubtxnSet aborted_subtxn_set_,
    Status expected_deadlock_status_) : status(status_), status_time(status_time_),
        aborted_subtxn_set(aborted_subtxn_set_),
        expected_deadlock_status(expected_deadlock_status_) {
  DCHECK(status == TransactionStatus::ABORTED || status_time.is_valid())
      << "Status: " << status << ", status_time: " << status_time;
}

TransactionStatusResult::TransactionStatusResult(
    TransactionStatus status_, HybridTime status_time_, SubtxnSet aborted_subtxn_set_,
    TabletId status_tablet_) : status(status_), status_time(status_time_),
      aborted_subtxn_set(aborted_subtxn_set_), status_tablet(status_tablet_) {}

namespace {

void DupStatusTablet(const TabletId& tablet_id, TransactionMetadataPB* out) {
  out->set_status_tablet(tablet_id);
}

void DupStatusTablet(const TabletId& tablet_id, LWTransactionMetadataPB* out) {
  out->dup_status_tablet(tablet_id);
}

template <class PB>
void DoToPB(const TransactionMetadata& source, PB* dest) {
  source.TransactionIdToPB(dest);
  dest->set_isolation(source.isolation);
  DupStatusTablet(source.status_tablet, dest);
  dest->set_priority(source.priority);
  dest->set_start_hybrid_time(source.start_time.ToUint64());
  dest->set_locality(source.locality);
}

} // namespace

template <class PB>
Result<TransactionMetadata> TransactionMetadata::DoFromPB(const PB& source) {
  TransactionMetadata result;
  auto id = FullyDecodeTransactionId(source.transaction_id());
  RETURN_NOT_OK(id);
  result.transaction_id = *id;
  if (source.has_isolation()) {
    result.isolation = source.isolation();
    std::string_view string_view(source.status_tablet());
    result.status_tablet.assign(string_view.data(), string_view.size());
    result.priority = source.priority();
    result.start_time = HybridTime(source.start_hybrid_time());
  }

  if (source.has_locality()) {
    result.locality = source.locality();
  } else {
    result.locality = TransactionLocality::GLOBAL;
  }

  return result;
}

Result<TransactionMetadata> TransactionMetadata::FromPB(const LWTransactionMetadataPB& source) {
  return DoFromPB(source);
}

Result<TransactionMetadata> TransactionMetadata::FromPB(const TransactionMetadataPB& source) {
  return DoFromPB(source);
}

void TransactionMetadata::ToPB(TransactionMetadataPB* dest) const {
  if (isolation == IsolationLevel::NON_TRANSACTIONAL) {
    TransactionIdToPB(dest);
  } else {
    DoToPB(*this, dest);
  }
}

void TransactionMetadata::ToPB(LWTransactionMetadataPB* dest) const {
  if (isolation == IsolationLevel::NON_TRANSACTIONAL) {
    TransactionIdToPB(dest);
  } else {
    DoToPB(*this, dest);
  }
}

void TransactionMetadata::TransactionIdToPB(TransactionMetadataPB* dest) const {
  dest->set_transaction_id(transaction_id.data(), transaction_id.size());
}

void TransactionMetadata::TransactionIdToPB(LWTransactionMetadataPB* dest) const {
  dest->dup_transaction_id(transaction_id.AsSlice());
}

bool operator==(const TransactionMetadata& lhs, const TransactionMetadata& rhs) {
  return lhs.transaction_id == rhs.transaction_id &&
         lhs.isolation == rhs.isolation &&
         lhs.status_tablet == rhs.status_tablet &&
         lhs.priority == rhs.priority &&
         lhs.start_time == rhs.start_time &&
         lhs.locality == rhs.locality;
}

std::ostream& operator<<(std::ostream& out, const TransactionMetadata& metadata) {
  return out << metadata.ToString();
}

namespace {

template <class PB>
void DoToPB(const PostApplyTransactionMetadata& source, PB* dest) {
  source.TransactionIdToPB(dest);
  source.apply_op_id.ToPB(dest->mutable_apply_op_id());
  dest->set_commit_ht(source.commit_ht.ToUint64());
  dest->set_log_ht(source.log_ht.ToUint64());
}

} // namespace

template <class PB>
Result<PostApplyTransactionMetadata> PostApplyTransactionMetadata::DoFromPB(const PB& source) {
  PostApplyTransactionMetadata result;

  result.transaction_id = VERIFY_RESULT(FullyDecodeTransactionId(source.transaction_id()));
  result.apply_op_id = OpId::FromPB(source.apply_op_id());
  result.commit_ht = HybridTime(source.commit_ht());
  result.log_ht = HybridTime(source.log_ht());

  return result;
}

Result<PostApplyTransactionMetadata> PostApplyTransactionMetadata::FromPB(
    const LWPostApplyTransactionMetadataPB& source) {
  return DoFromPB(source);
}

Result<PostApplyTransactionMetadata> PostApplyTransactionMetadata::FromPB(
    const PostApplyTransactionMetadataPB& source) {
  return DoFromPB(source);
}

void PostApplyTransactionMetadata::ToPB(PostApplyTransactionMetadataPB* dest) const {
  DoToPB(*this, dest);
}

void PostApplyTransactionMetadata::ToPB(LWPostApplyTransactionMetadataPB* dest) const {
  DoToPB(*this, dest);
}

void PostApplyTransactionMetadata::TransactionIdToPB(PostApplyTransactionMetadataPB* dest) const {
  dest->set_transaction_id(transaction_id.data(), transaction_id.size());
}

void PostApplyTransactionMetadata::TransactionIdToPB(LWPostApplyTransactionMetadataPB* dest) const {
  dest->dup_transaction_id(transaction_id.AsSlice());
}

bool operator==(const PostApplyTransactionMetadata& lhs, const PostApplyTransactionMetadata& rhs) {
  return YB_STRUCT_EQUALS(transaction_id, apply_op_id, commit_ht, log_ht);
}

std::ostream& operator<<(std::ostream& out, const PostApplyTransactionMetadata& metadata) {
  return out << metadata.ToString();
}

void SubTransactionMetadata::ToPB(SubTransactionMetadataPB* dest) const {
  dest->set_subtransaction_id(subtransaction_id);
  aborted.ToPB(dest->mutable_aborted()->mutable_set());
}

Result<SubTransactionMetadata> SubTransactionMetadata::FromPB(
    const SubTransactionMetadataPB& source) {
  return SubTransactionMetadata {
    .subtransaction_id = source.has_subtransaction_id()
        ? source.subtransaction_id()
        : kMinSubTransactionId,
    .aborted = VERIFY_RESULT(SubtxnSet::FromPB(source.aborted().set())),
  };
}

bool SubTransactionMetadata::IsDefaultState() const {
  DCHECK(subtransaction_id >= kMinSubTransactionId);
  return subtransaction_id == kMinSubTransactionId && aborted.IsEmpty();
}

std::ostream& operator<<(std::ostream& out, const SubTransactionMetadata& metadata) {
  return out << metadata.ToString();
}

MonoDelta TransactionRpcTimeout() {
  return FLAGS_transaction_rpc_timeout_ms * 1ms * kTimeMultiplier;
}

// TODO(dtxn) correct deadline should be calculated and propagated.
CoarseTimePoint TransactionRpcDeadline() {
  return CoarseMonoClock::Now() + TransactionRpcTimeout();
}

TransactionOperationContext::TransactionOperationContext()
    : transaction_id(TransactionId::Nil()), txn_status_manager(nullptr) {}

TransactionOperationContext::TransactionOperationContext(
    const TransactionId& transaction_id_, TransactionStatusManager* txn_status_manager_)
    : transaction_id(transaction_id_),
      txn_status_manager(DCHECK_NOTNULL(txn_status_manager_)) {}

TransactionOperationContext::TransactionOperationContext(
    const TransactionId& transaction_id_,
    SubTransactionMetadata&& subtransaction_,
    TransactionStatusManager* txn_status_manager_)
    : transaction_id(transaction_id_),
      subtransaction(std::move(subtransaction_)),
      txn_status_manager(DCHECK_NOTNULL(txn_status_manager_)) {}

bool TransactionOperationContext::transactional() const {
  return !transaction_id.IsNil();
}

TransactionLockInfoManager::TransactionLockInfoManager(
    TabletLockInfoPB* tablet_lock_info): tablet_lock_info_(tablet_lock_info) {}

TabletLockInfoPB::TransactionLockInfoPB* TransactionLockInfoManager::GetOrAddTransactionLockInfo(
    const TransactionId& id) {
  auto it = transaction_lock_infos_.find(id);
  if (it != transaction_lock_infos_.end()) {
    return it->second;
  }
  auto* transaction_lock_info = tablet_lock_info_->add_transaction_locks();
  transaction_lock_info->set_id(id.data(), id.size());
  transaction_lock_infos_.emplace(id, transaction_lock_info);
  return transaction_lock_info;
}

TabletLockInfoPB::WaiterInfoPB* TransactionLockInfoManager::GetSingleShardLockInfo() {
  return tablet_lock_info_->add_single_shard_waiters();
}

} // namespace yb
