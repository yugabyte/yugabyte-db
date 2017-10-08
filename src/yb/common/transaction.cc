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

namespace yb {

Result<TransactionId> MakeTransactionIdFromBinaryRepresentation(
  Slice binary_representation_of_transaction_id) {
  if (binary_representation_of_transaction_id.size() != TransactionId::static_size()) {
    return STATUS_FORMAT(Corruption,
                         "Invalid length of transaction id: $0",
                         binary_representation_of_transaction_id.ToDebugHexString());
  }
  TransactionId id;
  memcpy(id.data, binary_representation_of_transaction_id.data(), TransactionId::static_size());
  return id;
}

} // namespace yb
