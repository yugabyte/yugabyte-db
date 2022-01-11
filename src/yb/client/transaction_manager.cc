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

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/master/catalog_manager.h"

#include "yb/rpc/tasks_pool.h"

#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/thread_restrictions.h"

DEFINE_uint64(transaction_manager_workers_limit, 50,
              "Max number of workers used by transaction manager");

DEFINE_uint64(transaction_manager_queue_limit, 500,
              "Max number of tasks used by transaction manager");

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

namespace yb {
namespace client {

namespace {

const YBTableName kGlobalTransactionTableName(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);

// Cache of tablet ids of the global transaction table and any local transaction tables with
// the same placement.
class TransactionTableState {
 public:
  explicit TransactionTableState(LocalTabletFilter local_tablet_filter)
      : local_tablet_filter_(local_tablet_filter) {
  }

  void InvokeCallback(const PickStatusTabletCallback& callback,
                      TransactionLocality locality) EXCLUDES(mutex_) {
    SharedLock<yb::RWMutex> lock(mutex_);
    const auto& tablets = PickTabletList(locality);
    if (tablets.empty()) {
      callback(STATUS_FORMAT(
          IllegalState, "No $0 transaction tablets found", TransactionLocality_Name(locality)));
      return;
    }
    if (PickStatusTabletId(tablets, callback)) {
      return;
    }
    YB_LOG_EVERY_N_SECS(WARNING, 1) << "No local transaction status tablet found";
    callback(RandomElement(tablets));
  }

  // Update transaction table versions hash and return true if changed.
  bool UpdateTxnTableVersionsHash(uint64_t hash) EXCLUDES(mutex_) {
    std::lock_guard<yb::RWMutex> lock(mutex_);
    if (txn_table_versions_hash_ == hash) {
      return false;
    }
    txn_table_versions_hash_ = hash;
    return true;
  }

  bool IsInitialized() {
    return initialized_.load();
  }

  void UpdateStatusTablets(uint64_t new_version,
                           std::vector<TabletId>&& global_tablets,
                           std::vector<TabletId>&& local_tablets) EXCLUDES(mutex_) {
    std::lock_guard<yb::RWMutex> lock(mutex_);
    if (status_tablets_version_ < new_version) {
      global_tablets_ = global_tablets;
      local_tablets_ = local_tablets;
      has_local_tablets_.store(!local_tablets.empty());
      status_tablets_version_ = new_version;
      initialized_.store(true);
    }
  }

  bool HasAnyLocalStatusTablets() {
    return has_local_tablets_.load();
  }

  uint64_t GetStatusTabletsVersion() EXCLUDES(mutex_) {
    std::lock_guard<yb::RWMutex> lock(mutex_);
    return status_tablets_version_;
  }

 private:
  // Picks a status tablet id from 'tablets' filtered by 'filter'. Returns true if a
  // tablet id was picked successfully, and false if there were no applicable tablet ids.
  bool PickStatusTabletId(const std::vector<TabletId>& tablets,
                          const PickStatusTabletCallback& callback) REQUIRES_SHARED(mutex_) {
    if (tablets.empty()) {
      return false;
    }
    if (local_tablet_filter_) {
      std::vector<const TabletId*> ids;
      ids.reserve(tablets.size());
      for (const auto& id : tablets) {
        ids.push_back(&id);
      }
      local_tablet_filter_(&ids);
      if (!ids.empty()) {
        callback(*RandomElement(ids));
        return true;
      }
      return false;
    }
    callback(RandomElement(tablets));
    return true;
  }

  const std::vector<TabletId>& PickTabletList(TransactionLocality locality)
      REQUIRES_SHARED(mutex_) {
    if (local_tablets_.empty()) {
      return global_tablets_;
    }
    switch (locality) {
      case TransactionLocality::GLOBAL:
        return global_tablets_;
      case TransactionLocality::LOCAL:
        return local_tablets_;
    }
    FATAL_INVALID_ENUM_VALUE(TransactionLocality, locality);
  }

  LocalTabletFilter local_tablet_filter_;

  // Set to true once transaction tablets have been loaded at least once. global_tablets
  // is assumed to have at least one entry in it if this is true.
  std::atomic<bool> initialized_{false};

  // Set to true if there are any local transaction tablets.
  std::atomic<bool> has_local_tablets_{false};

  // Locks the hash/version/tablet lists. A read lock is acquired when picking
  // tablets, and a write lock is acquired when updating tablet lists.
  RWMutex mutex_;

  uint64_t txn_table_versions_hash_ GUARDED_BY(mutex_) = 0;
  uint64_t status_tablets_version_ GUARDED_BY(mutex_) = 0;

  std::vector<TabletId> global_tablets_ GUARDED_BY(mutex_);
  std::vector<TabletId> local_tablets_ GUARDED_BY(mutex_);
};

// Loads transaction tablets list to cache.
class LoadStatusTabletsTask {
 public:
  LoadStatusTabletsTask(YBClient* client,
                        TransactionTableState* table_state,
                        uint64_t version,
                        PickStatusTabletCallback callback = PickStatusTabletCallback(),
                        TransactionLocality locality = TransactionLocality::GLOBAL)
      : client_(client), table_state_(table_state), version_(version), callback_(callback),
        locality_(locality) {
  }

  void Run() {
    // TODO(dtxn) async
    // TODO These should be consolidated into one RPC call (#10536).
    auto global_tablets_result = GetGlobalTransactionTableTablets();
    if (!global_tablets_result) {
      YB_LOG_EVERY_N_SECS(ERROR, 1) << "Failed to get tablets of global txn status table: "
                                    << global_tablets_result.status();
      callback_(global_tablets_result.status());
      return;
    }
    auto local_tablets_result = GetLocalTransactionTableTablets();
    if (!local_tablets_result) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Failed to get tablets of local txn status tables: "
                                      << local_tablets_result.status();
      callback_(local_tablets_result.status());
      return;
    }

    table_state_->UpdateStatusTablets(version_,
                                      std::move(*global_tablets_result),
                                      std::move(*local_tablets_result));

    if (callback_) {
      table_state_->InvokeCallback(callback_, locality_);
    }
  }

  void Done(const Status& status) {
    if (!status.ok()) {
      callback_(status);
    }
    callback_ = PickStatusTabletCallback();
    client_ = nullptr;
  }

 private:
  Result<std::vector<TabletId>> GetGlobalTransactionTableTablets() {
    std::vector<TabletId> tablets;
    if (!FetchGlobalTransactionTableTablets(&tablets).ok()) {
      // Tablets for txn status table are not ready yet.
      // Wait for table creation completion and try again.
      RETURN_NOT_OK(client_->WaitForCreateTableToFinish(kGlobalTransactionTableName));
      RETURN_NOT_OK(FetchGlobalTransactionTableTablets(&tablets));
    }
    SCHECK(!tablets.empty(), IllegalState, Format(
        "No tablets in table $0", kGlobalTransactionTableName));
    return std::move(tablets);
  }

  CHECKED_STATUS FetchGlobalTransactionTableTablets(std::vector<TabletId>* tablets) {
    return client_->GetTablets(kGlobalTransactionTableName,
                               0 /* max_tablets */,
                               tablets,
                               nullptr /* ranges */,
                               nullptr /* locations */,
                               RequireTabletsRunning::kTrue);
  }

  Result<std::vector<TabletId>> GetLocalTransactionTableTablets() {
    std::vector<TabletId> tablets;
    RETURN_NOT_OK(FetchLocalTransactionTableTablets(&tablets));
    return std::move(tablets);
  }

  CHECKED_STATUS FetchLocalTransactionTableTablets(std::vector<TabletId>* tablets) {
    const auto& table_names = CHECK_RESULT(client_->ListTables(
        kTransactionTablePrefix, true /* exclude_ysql */));
    const auto local_ts = client_->GetLocalTabletServer();
    if (!local_ts) {
      return Status::OK();
    }
    const auto& this_pb = local_ts->cloud_info();
    std::shared_ptr<client::YBTable> table;
    for (const auto& table_name : table_names) {
      if (!StringStartsWithOrEquals(table_name.table_name(), kTransactionTablePrefix)) {
        continue;
      }
      RETURN_NOT_OK(client_->OpenTable(table_name, &table));
      if (!table->replication_info()) {
        continue;
      }
      const auto& replicas = table->replication_info().get().live_replicas();

      bool contains_this = false;
      int num_placement_blocks = replicas.placement_blocks_size();
      for (int i = 0; i < num_placement_blocks; ++i) {
        auto pb = replicas.placement_blocks(i).cloud_info();
        if (pb.placement_cloud() == this_pb.placement_cloud() &&
            pb.placement_region() == this_pb.placement_region() &&
            pb.placement_zone() == this_pb.placement_zone()) {
          contains_this = true;
          break;
        }
      }
      if (!contains_this) {
        continue;
      }
      std::vector<TabletId> table_tablets;
      RETURN_NOT_OK(client_->GetTablets(table_name,
                                        0 /* max_tablets */,
                                        &table_tablets,
                                        nullptr /* ranges */,
                                        nullptr /* locations */,
                                        RequireTabletsRunning::kTrue));
      std::move(table_tablets.begin(), table_tablets.end(), std::back_inserter(*tablets));
    }
    return Status::OK();
  }

  YBClient* client_;
  TransactionTableState* table_state_;
  uint64_t version_;
  PickStatusTabletCallback callback_;
  TransactionLocality locality_;
};

class InvokeCallbackTask {
 public:
  InvokeCallbackTask(TransactionTableState* table_state,
                     PickStatusTabletCallback callback,
                     TransactionLocality locality)
      : table_state_(table_state), callback_(std::move(callback)), locality_(locality) {
  }

  void Run() {
    table_state_->InvokeCallback(callback_, locality_);
  }

  void Done(const Status& status) {
    if (!status.ok()) {
      callback_(status);
    }
    callback_ = PickStatusTabletCallback();
  }

 private:
  TransactionTableState* table_state_;
  PickStatusTabletCallback callback_;
  TransactionLocality locality_;
};
} // namespace

class TransactionManager::Impl {
 public:
  explicit Impl(YBClient* client, const scoped_refptr<ClockBase>& clock,
                LocalTabletFilter local_tablet_filter)
      : client_(client),
        clock_(clock),
        table_state_{std::move(local_tablet_filter)},
        thread_pool_(
            "TransactionManager", FLAGS_transaction_manager_queue_limit,
            FLAGS_transaction_manager_workers_limit),
        tasks_pool_(FLAGS_transaction_manager_queue_limit),
        invoke_callback_tasks_(FLAGS_transaction_manager_queue_limit) {
    CHECK(clock);
  }

  ~Impl() {
    Shutdown();
  }

  void UpdateTxnTableVersionsHash(uint64_t hash) {
    if (!table_state_.UpdateTxnTableVersionsHash(hash)) {
      return;
    }

    uint64_t version = ++status_tablets_version_;
    if (!tasks_pool_.Enqueue(&thread_pool_, client_, &table_state_, version)) {
      YB_LOG_EVERY_N_SECS(ERROR, 1) << "Update tasks overflow, number of tasks: "
                                    << tasks_pool_.size();
    }
  }

  void PickStatusTablet(PickStatusTabletCallback callback, TransactionLocality locality) {
    if (table_state_.IsInitialized()) {
      if (ThreadRestrictions::IsWaitAllowed()) {
        table_state_.InvokeCallback(callback, locality);
      } else if (!invoke_callback_tasks_.Enqueue(
            &thread_pool_, &table_state_, callback, locality)) {
        callback(STATUS_FORMAT(ServiceUnavailable,
                               "Invoke callback queue overflow, number of tasks: $0",
                               invoke_callback_tasks_.size()));
      }
      return;
    }

    // Bump version up to be at least 1.
    // The possible cases are:
    // - status_tablets_version_ = 0, new transaction id request before initialization, bump to 1.
    // - status_tablets_version_ = 1, new transaction id request before initialization but not the
    //   first request, no need to bump version (no changes in transaction tables) but queue
    //   anyways.
    // - status_tablets_version_ > 1, was not initialized during above check, but became
    //   initialized by heartbeat since then, do not set to 1 since status_tablets_version_ should
    //   never decrease.
    uint64_t expected_version = 0;
    uint64_t new_version = 1;
    status_tablets_version_.compare_exchange_strong(
        expected_version, new_version, std::memory_order_acq_rel);

    if (!tasks_pool_.Enqueue(
        &thread_pool_, client_, &table_state_, status_tablets_version_.load(), callback,
        locality)) {
      callback(STATUS_FORMAT(ServiceUnavailable, "Tasks overflow, exists: $0", tasks_pool_.size()));
    }
  }

  const scoped_refptr<ClockBase>& clock() const {
    return clock_;
  }

  YBClient* client() const {
    return client_;
  }

  rpc::Rpcs& rpcs() {
    return rpcs_;
  }

  HybridTime Now() const {
    return clock_->Now();
  }

  HybridTimeRange NowRange() const {
    return clock_->NowRange();
  }

  void UpdateClock(HybridTime time) {
    clock_->Update(time);
  }

  void Shutdown() {
    rpcs_.Shutdown();
    thread_pool_.Shutdown();
  }

  bool LocalTransactionsPossible() {
    return table_state_.HasAnyLocalStatusTablets();
  }

  uint64_t GetLoadedStatusTabletsVersion() {
    return table_state_.GetStatusTabletsVersion();
  }

 private:
  YBClient* const client_;
  scoped_refptr<ClockBase> clock_;
  TransactionTableState table_state_;
  std::atomic<bool> closed_{false};

  // Version of set of transaction status tablets. Each time the transaction table versions
  // hash changes, this is incremented. This version is internal and specific to the
  // transaction manager, and may not be equal across multiple instances of transaction managers.
  std::atomic<uint64_t> status_tablets_version_{0};

  yb::rpc::ThreadPool thread_pool_; // TODO async operations instead of pool
  yb::rpc::TasksPool<LoadStatusTabletsTask> tasks_pool_;
  yb::rpc::TasksPool<InvokeCallbackTask> invoke_callback_tasks_;
  yb::rpc::Rpcs rpcs_;
};

TransactionManager::TransactionManager(
    YBClient* client, const scoped_refptr<ClockBase>& clock,
    LocalTabletFilter local_tablet_filter)
    : impl_(new Impl(client, clock, std::move(local_tablet_filter))) {}

TransactionManager::~TransactionManager() = default;

void TransactionManager::UpdateTxnTableVersionsHash(uint64_t hash) {
  impl_->UpdateTxnTableVersionsHash(hash);
}

void TransactionManager::PickStatusTablet(
    PickStatusTabletCallback callback, TransactionLocality locality) {
  impl_->PickStatusTablet(std::move(callback), locality);
}

YBClient* TransactionManager::client() const {
  return impl_->client();
}

rpc::Rpcs& TransactionManager::rpcs() {
  return impl_->rpcs();
}

const scoped_refptr<ClockBase>& TransactionManager::clock() const {
  return impl_->clock();
}

HybridTime TransactionManager::Now() const {
  return impl_->Now();
}

HybridTimeRange TransactionManager::NowRange() const {
  return impl_->NowRange();
}

void TransactionManager::UpdateClock(HybridTime time) {
  impl_->UpdateClock(time);
}

bool TransactionManager::LocalTransactionsPossible() {
  return impl_->LocalTransactionsPossible();
}

uint64_t TransactionManager::GetLoadedStatusTabletsVersion() {
  return impl_->GetLoadedStatusTabletsVersion();
}

TransactionManager::TransactionManager(TransactionManager&& rhs) = default;
TransactionManager& TransactionManager::operator=(TransactionManager&& rhs) = default;

} // namespace client
} // namespace yb
