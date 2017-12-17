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

#include "yb/client/transaction_manager.h"

#include "yb/rpc/rpc.h"
#include "yb/rpc/thread_pool.h"
#include "yb/rpc/tasks_pool.h"

#include "yb/util/random_util.h"

#include "yb/client/client.h"

DEFINE_uint64(transaction_table_default_num_tablets, 24,
              "Automatically create transaction table with specified number of tablets if missing. "
              "0 to disable.");

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
      callback_(status.CloneAndPrepend("Failed to create status table"));
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
    constexpr int kNumRetries = 5;
    Status status;
    for (int i = 0; i != kNumRetries; ++i) {
      status = client_->OpenTable(kTransactionTableName, &table);
      if (status.ok()) {
        break;
      }
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
  explicit Impl(const YBClientPtr& client, const scoped_refptr<ClockBase>& clock)
      : client_(client),
        clock_(clock),
        thread_pool_("TransactionManager", kQueueLimit, kMaxWorkers),
        tasks_pool_(kQueueLimit) {}

  void PickStatusTablet(PickStatusTabletCallback callback) {
    if (!tasks_pool_.Enqueue(&thread_pool_, client_, &status_table_exists_, std::move(callback))) {
      callback(STATUS_FORMAT(ServiceUnavailable, "Tasks overflow, exists: $0", tasks_pool_.size()));
    }
  }

  const YBClientPtr& client() const {
    return client_;
  }

  rpc::Rpcs& rpcs() {
    return rpcs_;
  }

  HybridTime Now() const {
    return clock_->Now();
  }

  void UpdateClock(HybridTime time) {
    clock_->Update(time);
  }

 private:
  YBClientPtr client_;
  scoped_refptr<ClockBase> clock_;
  std::atomic<bool> status_table_exists_{false};
  std::atomic<bool> closed_{false};
  yb::rpc::ThreadPool thread_pool_; // TODO async operations instead of pool
  yb::rpc::TasksPool<PickStatusTabletTask> tasks_pool_;
  yb::rpc::Rpcs rpcs_;
};

TransactionManager::TransactionManager(
    const YBClientPtr& client, const scoped_refptr<ClockBase>& clock)
    : impl_(new Impl(client, clock)) {}

TransactionManager::~TransactionManager() {
}

void TransactionManager::PickStatusTablet(PickStatusTabletCallback callback) {
  impl_->PickStatusTablet(std::move(callback));
}

const YBClientPtr& TransactionManager::client() const {
  return impl_->client();
}

rpc::Rpcs& TransactionManager::rpcs() {
  return impl_->rpcs();
}

HybridTime TransactionManager::Now() const {
  return impl_->Now();
}

void TransactionManager::UpdateClock(HybridTime time) {
  impl_->UpdateClock(time);
}

} // namespace client
} // namespace yb
