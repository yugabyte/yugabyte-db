//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_CLIENT_TRANSACTION_H
#define YB_CLIENT_TRANSACTION_H

#include <memory>
#include <unordered_set>

#include "yb/common/common.pb.h"

#include "yb/client/client_fwd.h"

#include "yb/util/status.h"

namespace yb {
namespace client {

typedef std::function<void(const Status&)> Waiter;
typedef std::function<void(const Status&)> CommitCallback;

// YBTransaction is a representation of single transaction.
// After YBTransaction is created, it could be used during construction of YBSession,
// to mark that this session will send commands related to this transaction.
class YBTransaction : public std::enable_shared_from_this<YBTransaction> {
 public:
  YBTransaction(TransactionManager* manager, IsolationLevel isolation);
  ~YBTransaction();

  // This function is used to init metadata of Write/Read request.
  // If we don't have enough information, then the function returns false and stores
  // waiter, that will be invoked when we obtain such information.
  bool Prepare(const std::unordered_set<internal::InFlightOpPtr>& ops,
               Waiter waiter,
               TransactionMetadataPB* metadata);
  void Flushed(const internal::InFlightOps& ops, const Status& status);

  void Commit(CommitCallback callback);
 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_H
