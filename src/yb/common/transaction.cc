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

#include "yb/util/tsan_util.h"

namespace yb {

namespace {

// Makes transaction id from its binary representation.
// If check_exact_size is true, checks that slice contains only TransactionId.
Result<TransactionId> DoDecodeTransactionId(const Slice &slice, const bool check_exact_size) {
  if (check_exact_size ? slice.size() != TransactionId::static_size()
                       : slice.size() < TransactionId::static_size()) {
    return STATUS_FORMAT(
        Corruption, "Invalid length of binary data with transaction id '$0': $1 (expected $2$3)",
        slice.ToDebugHexString(), slice.size(), check_exact_size ? "" : "at least ",
        TransactionId::static_size());
  }
  TransactionId id;
  memcpy(id.data, slice.data(), TransactionId::static_size());
  return id;
}

} // namespace

Result<TransactionId> FullyDecodeTransactionId(const Slice& slice) {
  return DoDecodeTransactionId(slice, true);
}

Result<TransactionId> DecodeTransactionId(Slice* slice) {
  Result<TransactionId> id = DoDecodeTransactionId(*slice, false);
  RETURN_NOT_OK(id);
  slice->remove_prefix(TransactionId::static_size());
  return id;
}

Result<TransactionMetadata> TransactionMetadata::FromPB(const TransactionMetadataPB& source) {
  TransactionMetadata result;
  auto id = FullyDecodeTransactionId(source.transaction_id());
  RETURN_NOT_OK(id);
  result.transaction_id = *id;
  if (source.has_isolation()) {
    result.isolation = source.isolation();
    result.status_tablet = source.status_tablet();
    result.priority = source.priority();
    result.start_time = HybridTime(source.start_hybrid_time());
  }
  return result;
}

void TransactionMetadata::ToPB(TransactionMetadataPB* dest) const {
  dest->set_transaction_id(transaction_id.data, transaction_id.size());
  if (isolation != IsolationLevel::NON_TRANSACTIONAL) {
    dest->set_isolation(isolation);
    dest->set_status_tablet(status_tablet);
    dest->set_priority(priority);
    dest->set_start_hybrid_time(start_time.ToUint64());
  }
}

bool operator==(const TransactionMetadata& lhs, const TransactionMetadata& rhs) {
  return lhs.transaction_id == rhs.transaction_id &&
         lhs.isolation == rhs.isolation &&
         lhs.status_tablet == rhs.status_tablet &&
         lhs.priority == rhs.priority &&
         lhs.start_time == rhs.start_time;
}

std::ostream& operator<<(std::ostream& out, const TransactionMetadata& metadata) {
  return out << Format("{ transaction_id: $0 isolation: $1 status_tablet: $2 priority: $3 "
                           "start_time: $4",
                       metadata.transaction_id, IsolationLevel_Name(metadata.isolation),
                       metadata.status_tablet, metadata.priority, metadata.start_time);
}

// TODO(dtxn) correct deadline should be calculated and propagated.
MonoTime TransactionRpcDeadline() {
  return MonoTime::Now() + MonoDelta::FromSeconds(NonTsanVsTsan(15, 5));
}

bool TransactionOperationContext::transactional() const {
  return !transaction_id.is_nil();
}

} // namespace yb
