//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_CLIENT_CLIENT_FWD_H
#define YB_CLIENT_CLIENT_FWD_H

#include <memory>
#include <vector>

template <class T>
class scoped_refptr;

namespace yb {
namespace client {

class YBClient;
typedef std::shared_ptr<YBClient> YBClientPtr;

class YBTransaction;
typedef std::shared_ptr<YBTransaction> YBTransactionPtr;

class TransactionManager;
class YBOperation;
class YBSchema;
class YBTableName;

namespace internal {

struct InFlightOp;
typedef std::shared_ptr<InFlightOp> InFlightOpPtr;
typedef std::vector<InFlightOpPtr> InFlightOps;

class RemoteTablet;
typedef scoped_refptr<RemoteTablet> RemoteTabletPtr;

class RemoteTabletServer;

} // namespace internal

} // namespace client
} // namespace yb

#endif // YB_CLIENT_CLIENT_FWD_H
