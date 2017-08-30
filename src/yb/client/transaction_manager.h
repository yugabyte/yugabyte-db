//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_CLIENT_TRANSACTION_MANAGER_H
#define YB_CLIENT_TRANSACTION_MANAGER_H

#include <functional>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/hybrid_time.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/result.h"

namespace yb {
namespace client {

typedef std::function<void(const Result<std::string>&)> PickStatusTabletCallback;

// TransactionManager manages multiple transactions.
class TransactionManager {
 public:
  explicit TransactionManager(const YBClientPtr& client);
  ~TransactionManager();

  void PickStatusTablet(PickStatusTabletCallback callback);

  const YBClientPtr& client() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb

#endif // YB_CLIENT_TRANSACTION_MANAGER_H
