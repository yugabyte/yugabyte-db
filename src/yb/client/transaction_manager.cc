//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction_manager.h"

#include "yb/rpc/thread_pool.h"
#include "yb/rpc/tasks_pool.h"

#include "yb/util/random_util.h"

#include "yb/client/client.h"

namespace yb {
namespace client {

namespace {

// Picks status tablet for transaction.
class PickStatusTabletTask {
 public:
  PickStatusTabletTask(const YBClientPtr& client, PickStatusTabletCallback callback)
      : client_(client), callback_(std::move(callback)) {
  }

  void Run() {
    // TODO(dtxn) async
    // TODO(dtxn) prefer tablets with leader on the local node
    // TODO(dtxn) prevent deletion of picked tablet
    std::vector<YBTableName> tables;
    auto status = client_->ListTables(&tables);
    if (!status.ok()) {
      callback_(status);
      return;
    }
    if (tables.empty()) {
      callback_(STATUS(IllegalState, "No tables exists"));
      return;
    }
    auto& table = RandomElement(tables);
    std::vector<std::string> tablets;
    status = client_->GetTablets(table, 0, &tablets, /* ranges */ nullptr);
    if (!status.ok()) {
      callback_(status);
      return;
    }
    if (tablets.empty()) {
      callback_(STATUS_FORMAT(IllegalState, "No tablets in table $0", table));
      return;
    }
    callback_(RandomElement(tablets));
  }

  void Done(const Status& status) {
    if (!status.ok()) {
      callback_(status);
    }
    callback_ = PickStatusTabletCallback();
    client_.reset();
  }

 private:
  YBClientPtr client_;
  PickStatusTabletCallback callback_;
};

constexpr size_t kQueueLimit = 150;
constexpr size_t kMaxWorkers = 50;

} // namespace

class TransactionManager::Impl {
 public:
  explicit Impl(const YBClientPtr& client)
      : client_(client),
        thread_pool_("TransactionManager", kQueueLimit, kMaxWorkers),
        tasks_pool_(kQueueLimit) {}

  void Shutdown() {
    thread_pool_.Shutdown();
  }

  void PickStatusTablet(PickStatusTabletCallback callback) {
    if (!tasks_pool_.Enqueue(&thread_pool_, client_, std::move(callback))) {
      callback(STATUS_FORMAT(ServiceUnavailable, "Tasks overflow, exists: $0", tasks_pool_.size()));
    }
  }

  const YBClientPtr& client() const {
    return client_;
  }

 private:
  YBClientPtr client_;
  yb::rpc::ThreadPool thread_pool_; // TODO async opeations instead of pool
  yb::rpc::TasksPool<PickStatusTabletTask> tasks_pool_;
};

TransactionManager::TransactionManager(const YBClientPtr& client)
    : impl_(new Impl(client)) {}

TransactionManager::~TransactionManager() {
}

void TransactionManager::Shutdown() {
  impl_->Shutdown();
}

void TransactionManager::PickStatusTablet(PickStatusTabletCallback callback) {
  impl_->PickStatusTablet(std::move(callback));
}

const YBClientPtr& TransactionManager::client() const {
  return impl_->client();
}

} // namespace client
} // namespace yb
