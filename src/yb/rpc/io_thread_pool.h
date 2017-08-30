//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_IO_THREAD_POOL_H
#define YB_RPC_IO_THREAD_POOL_H

#include <memory>

#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace rpc {

// Runs io service in specified number of threads.
class IoThreadPool {
 public:
  explicit IoThreadPool(size_t num_threads);
  ~IoThreadPool();

  void Shutdown();
  void Join();

  IoService& io_service();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_IO_THREAD_POOL_H
