//
// Copyright (c) YugaByte, Inc.
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
