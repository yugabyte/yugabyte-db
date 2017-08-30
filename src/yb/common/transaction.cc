//
// Copyright (c) YugaByte, Inc.
//

#include "yb/common/transaction.h"

namespace yb {

Result<TransactionId> MakeTransactionIdFromBinaryRepresentation(
  const std::string& binary_representation_of_transaction_id) {
  if (binary_representation_of_transaction_id.size() != TransactionId::static_size()) {
    return STATUS_FORMAT(Corruption,
                         "Invalid length of transaction id: $0",
                         Slice(binary_representation_of_transaction_id).ToDebugHexString());
  }
  TransactionId id;
  memcpy(id.data, binary_representation_of_transaction_id.data(), TransactionId::static_size());
  return id;
}

} // namespace yb
