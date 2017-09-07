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
#include <boost/uuid/uuid.hpp>

#include "yb/util/enums.h"
#include "yb/util/result.h"

namespace yb {

using TransactionId = boost::uuids::uuid;
typedef boost::hash<TransactionId> TransactionIdHash;

// Processing mode:
//   LEADER - processing in leader.
//   NON_LEADER - processing in non leader.
YB_DEFINE_ENUM(ProcessingMode, (NON_LEADER)(LEADER));

// Makes transaction id from its binary representation string.
Result<TransactionId> MakeTransactionIdFromBinaryRepresentation(
  const std::string& binary_representation_of_transaction_id);

} // namespace yb

#endif // YB_COMMON_TRANSACTION_H
