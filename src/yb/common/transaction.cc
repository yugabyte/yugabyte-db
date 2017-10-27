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

} // namespace yb
