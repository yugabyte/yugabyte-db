//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_COMMON_TRANSACTION_H
#define YB_COMMON_TRANSACTION_H

#include <boost/uuid/uuid.hpp>

namespace yb {

using TransactionId = boost::uuids::uuid;
typedef boost::hash<TransactionId> TransactionIdHash;

}

#endif // YB_COMMON_TRANSACTION_H
