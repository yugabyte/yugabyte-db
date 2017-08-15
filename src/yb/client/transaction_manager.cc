//
// Copyright (c) YugaByte, Inc.
//

#include "yb/client/transaction_manager.h"

#include "yb/rpc/thread_pool.h"
#include "yb/rpc/tasks_pool.h"
#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"

#include "yb/util/random_util.h"

#include "yb/client/client.h"

DEFINE_uint64(transaction_table_default_num_tablets, 24,
              "Automatically create transaction table with specified number of tablets if missing. "
              "0 to disable.");
DEFINE_uint64(io_thread_pool_size, 4, "Size of allocated IO Thread Pool.");

namespace yb {
namespace client {

namespace {

const YBTableName kTransactionTableName("system", "transactions");

// Picks status tablet for transaction.
class PickStatusTabletTask {
 public:
  PickStatusTabletTask(const YBClientPtr& client,
                       std::atomic<bool>* status_table_exists,
                       PickStatusTabletCallback callback)
      : client_(client), status_table_exists_(status_table_exists), callback_(std::move(callback)) {
  }

  void Run() {
    auto status = EnsureStatusTableExists();
    if (!status.ok()) {
      callback_(status);
      return;
    }

    // TODO(dtxn) async
    // TODO(dtxn) prefer tablets with leader on the local node
    // TODO(dtxn) prevent deletion of picked tablet
    std::vector<std::string> tablets;
    status = client_->GetTablets(kTransactionTableName, 0, &tablets, /* ranges */ nullptr);
    if (!status.ok()) {
      callback_(status);
      return;
    }
    if (tablets.empty()) {
      callback_(STATUS_FORMAT(IllegalState, "No tablets in table $0", kTransactionTableName));
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
  CHECKED_STATUS EnsureStatusTableExists() {
    if (status_table_exists_->load(std::memory_order_acquire)) {
      return Status::OK();
    }
    std::shared_ptr<YBTable> table;
    auto status = client_->OpenTable(kTransactionTableName, &table);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to open transaction table: " << status.ToString();
      auto tablets = FLAGS_transaction_table_default_num_tablets;
      if (tablets > 0 && status.IsNotFound()) {
        status = client_->CreateNamespaceIfNotExists(kTransactionTableName.namespace_name());
        if (status.ok()) {
          std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
          table_creator->num_tablets(tablets);
          table_creator->table_type(client::YBTableType::REDIS_TABLE_TYPE);
          table_creator->table_name(kTransactionTableName);
          status = table_creator->Create();
          LOG_IF(DFATAL, !status.ok() && !status.IsAlreadyPresent())
              << "Failed to create transaction table: " << status;
        } else {
          LOG(DFATAL) << "Failed to create namespace: " << status;
        }
        status = client_->OpenTable(kTransactionTableName, &table);
      }
    }
    if (status.ok()) {
      status_table_exists_->store(true, std::memory_order_release);
    }
    return status;
  }

  YBClientPtr client_;
  std::atomic<bool>* status_table_exists_;
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
        tasks_pool_(kQueueLimit),
        io_thread_pool_(FLAGS_io_thread_pool_size),
        scheduler_(&io_thread_pool_.io_service()) {}

  ~Impl() {
    Shutdown();
  }

  void Shutdown() {
    bool expected = false;
    if (closed_.compare_exchange_strong(expected, true)) {
      scheduler_.Shutdown();
      io_thread_pool_.Shutdown();
      io_thread_pool_.Join();
    }
  }

  void PickStatusTablet(PickStatusTabletCallback callback) {
    if (!tasks_pool_.Enqueue(&thread_pool_, client_, &status_table_exists_, std::move(callback))) {
      callback(STATUS_FORMAT(ServiceUnavailable, "Tasks overflow, exists: $0", tasks_pool_.size()));
    }
  }

  const YBClientPtr& client() const {
    return client_;
  }

  rpc::Scheduler& scheduler() {
    return scheduler_;
  }

 private:
  YBClientPtr client_;
  std::atomic<bool> status_table_exists_{false};
  std::atomic<bool> closed_{false};
  yb::rpc::ThreadPool thread_pool_; // TODO async operations instead of pool
  yb::rpc::TasksPool<PickStatusTabletTask> tasks_pool_;
  yb::rpc::IoThreadPool io_thread_pool_;
  yb::rpc::Scheduler scheduler_;
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

rpc::Scheduler& TransactionManager::scheduler() {
  return impl_->scheduler();
}

} // namespace client
} // namespace yb
