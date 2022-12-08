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

#include <limits>

namespace yb {
// Bounds for transaction priority. We distinguish two classes of transactions:
//  1. High-priority, currently used for transactions/statements with explicit locking clauses
//    (e.g. SELECT .. FOR UPDATE).
//  2. Regular used for all other transactions.
// We reserve the top uint32 range of txn priorities (uint64) for high priority transactions.

constexpr uint64_t kHighPriTxnUpperBound = std::numeric_limits<uint64_t>::max();
constexpr uint64_t kHighPriTxnLowerBound = kHighPriTxnUpperBound -
                                           std::numeric_limits<uint32_t>::max();

constexpr uint64_t kRegularTxnLowerBound = 0;
constexpr uint64_t kRegularTxnUpperBound = kHighPriTxnLowerBound - 1;

} // namespace
