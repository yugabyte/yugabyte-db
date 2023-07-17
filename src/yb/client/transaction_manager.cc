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

#include "yb/server/server_base_options.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/thread_restrictions.h"

DEFINE_UNKNOWN_uint64(transaction_manager_workers_limit, 50,
              "Max number of workers used by transaction manager");

DEFINE_UNKNOWN_uint64(transaction_manager_queue_limit, 500,
              "Max number of tasks used by transaction manager");

DEFINE_test_flag(string, transaction_manager_preferred_tablet, "",
                 "For testing only. If non-empty, transaction manager will try to use the status "
                 "tablet with id matching this flag, if present in the list of status tablets.");

namespace yb {
namespace client {

namespace {

// Cache of tablet ids of the global transaction table and any transaction tables with
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

  bool IsInitialized() {
    return initialized_.load();
  }

  void UpdateStatusTablets(uint64_t new_version,
                           TransactionStatusTablets&& tablets) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    if (!initialized_.load() || status_tablets_version_ < new_version) {
      tablets_ = std::move(tablets);
      has_placement_local_tablets_.store(!tablets_.placement_local_tablets.empty());
      status_tablets_version_ = new_version;
      initialized_.store(true);
    }
  }

  bool HasAnyPlacementLocalStatusTablets() {
    return has_placement_local_tablets_.load();
  }

  uint64_t GetStatusTabletsVersion() EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
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
    if (PREDICT_FALSE(!FLAGS_TEST_transaction_manager_preferred_tablet.empty()) &&
        std::find(tablets.begin(), tablets.end(),
                  FLAGS_TEST_transaction_manager_preferred_tablet) != tablets.end()) {
      callback(FLAGS_TEST_transaction_manager_preferred_tablet);
      return true;
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
    if (tablets_.placement_local_tablets.empty()) {
      return tablets_.global_tablets;
    }
    switch (locality) {
      case TransactionLocality::GLOBAL:
        return tablets_.global_tablets;
      case TransactionLocality::LOCAL:
        return tablets_.placement_local_tablets;
    }
    FATAL_INVALID_ENUM_VALUE(TransactionLocality, locality);
  }

  LocalTabletFilter local_tablet_filter_;

  // Set to true once transaction tablets have been loaded at least once. global_tablets
  // is assumed to have at least one entry in it if this is true.
  std::atomic<bool> initialized_{false};

  // Set to true if there are any placement local transaction tablets.
  std::atomic<bool> has_placement_local_tablets_{false};

  // Locks the version/tablet lists. A read lock is acquired when picking
  // tablets, and a write lock is acquired when updating tablet lists.
  RWMutex mutex_;

  uint64_t status_tablets_version_ GUARDED_BY(mutex_) = 0;

  TransactionStatusTablets tablets_ GUARDED_BY(mutex_);
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
    auto tablets = GetTransactionStatusTablets();
    if (!tablets.ok()) {
      YB_LOG_EVERY_N_SECS(ERROR, 1) << "Failed to get tablets of txn status tables: "
                                    << tablets.status();
      if (callback_) {
        callback_(tablets.status());
      }
      return;
    }

    table_state_->UpdateStatusTablets(version_, std::move(*tablets));

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
  Result<TransactionStatusTablets> GetTransactionStatusTablets() {
    CloudInfoPB this_pb = yb::server::GetPlacementFromGFlags();
    return client_->GetTransactionStatusTablets(this_pb);
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
        thread_pool_(rpc::ThreadPoolOptions {
          .name = "TransactionManager",
          .max_workers = FLAGS_transaction_manager_workers_limit,
        }),
        tasks_pool_(FLAGS_transaction_manager_queue_limit),
        invoke_callback_tasks_(FLAGS_transaction_manager_queue_limit) {
    CHECK(clock);
  }

  ~Impl() {
    Shutdown();
  }

  void UpdateTransactionTablesVersion(
      uint64_t version, UpdateTransactionTablesVersionCallback callback) {
    if (table_state_.GetStatusTabletsVersion() >= version) {
      if (callback) {
        callback(Status::OK());
      }
      return;
    }

    PickStatusTabletCallback cb;
    if (callback) {
      cb = [callback](const Result<std::string>& result) {
        return callback(ResultToStatus(result));
      };
    }

    if (!tasks_pool_.Enqueue(&thread_pool_, client_, &table_state_, version, std::move(cb))) {
      YB_LOG_EVERY_N_SECS(ERROR, 1) << "Update tasks overflow, number of tasks: "
                                    << tasks_pool_.size();
      if (callback) {
        callback(STATUS_FORMAT(ServiceUnavailable,
                               "Update tasks queue overflow, number of tasks: $0",
                               tasks_pool_.size()));
      }
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

    if (!tasks_pool_.Enqueue(
        &thread_pool_, client_, &table_state_, 0 /* version */, callback,
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

  bool PlacementLocalTransactionsPossible() {
    return table_state_.HasAnyPlacementLocalStatusTablets();
  }

  uint64_t GetLoadedStatusTabletsVersion() {
    return table_state_.GetStatusTabletsVersion();
  }

 private:
  YBClient* const client_;
  scoped_refptr<ClockBase> clock_;
  TransactionTableState table_state_;
  std::atomic<bool> closed_{false};

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

void TransactionManager::UpdateTransactionTablesVersion(
    uint64_t version, UpdateTransactionTablesVersionCallback callback) {
  impl_->UpdateTransactionTablesVersion(version, std::move(callback));
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

bool TransactionManager::PlacementLocalTransactionsPossible() {
  return impl_->PlacementLocalTransactionsPossible();
}

uint64_t TransactionManager::GetLoadedStatusTabletsVersion() {
  return impl_->GetLoadedStatusTabletsVersion();
}

void TransactionManager::Shutdown() {
  impl_->Shutdown();
}

TransactionManager::TransactionManager(TransactionManager&& rhs) = default;
TransactionManager& TransactionManager::operator=(TransactionManager&& rhs) = default;

} // namespace client
} // namespace yb
