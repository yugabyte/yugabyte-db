//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_RPC_THREAD_POOL_H
#define YB_RPC_THREAD_POOL_H

#include <memory>
#include <string>

namespace yb {

class Status;

namespace rpc {

class ThreadPoolTask {
 public:
  // Invoked in thread pool
  virtual void Run() = 0;

  // When thread pool done with task, i.e. it completed or failed, it invokes Done
  virtual void Done(const Status& status) = 0;
 protected:
  ~ThreadPoolTask() {}
};

struct ThreadPoolOptions {
  std::string name;
  size_t queue_limit;
  size_t max_workers;
};

class ThreadPool {
 public:
  explicit ThreadPool(ThreadPoolOptions options);

  template <class... Args>
  explicit ThreadPool(Args&&... args)
      : ThreadPool(ThreadPoolOptions{std::forward<Args>(args)...}) {

  }
  ~ThreadPool();

  const ThreadPoolOptions& options() const;

  bool Enqueue(ThreadPoolTask* task);
  void Shutdown();

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace rpc
} // namespace yb

#endif // YB_RPC_THREAD_POOL_H
