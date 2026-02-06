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

#include "yb/ash/wait_state.h"

#include "yb/common/common_net.h"
#include "yb/common/transaction.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/rpc/tasks_pool.h"

#include "yb/server/server_base_options.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/metrics.h"
#include "yb/util/rw_mutex.h"
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

METRIC_DEFINE_counter(server, transaction_promotions,
                      "Number of transactions being promoted to global transactions",
                      yb::MetricUnit::kTransactions,
                      "Number of transactions being promoted to global transactions");

METRIC_DEFINE_counter(server, initially_global_transactions,
                      "Number of transactions that were started as global transactions",
                      yb::MetricUnit::kTransactions,
                      "Number of transactions that were started as global transactions");

METRIC_DEFINE_counter(server, initially_region_local_transactions,
                      "Number of transactions that were started as region-local transactions",
                      yb::MetricUnit::kTransactions,
                      "Number of transactions that were started as reigon-local transactions");

METRIC_DEFINE_counter(server, initially_tablespace_local_transactions,
                      "Number of transactions that were started as tablespace-local transactions",
                      yb::MetricUnit::kTransactions,
                      "Number of transactions that were started as tablespace-local transactions");

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

namespace yb {
namespace client {

namespace {

const CloudInfoPB& GetPlacementFromGFlags() {
  static GoogleOnceType once = GOOGLE_ONCE_INIT;
  static CloudInfoPB cloud_info;
  auto set_placement_from_gflags = [](CloudInfoPB* cloud_info) {
    cloud_info->set_placement_cloud(FLAGS_placement_cloud);
    cloud_info->set_placement_region(FLAGS_placement_region);
    cloud_info->set_placement_zone(FLAGS_placement_zone);
  };
  GoogleOnceInitArg(
      &once, static_cast<void (*)(CloudInfoPB*)>(set_placement_from_gflags), &cloud_info);

  return cloud_info;
}

// Cache of tablet ids of the global transaction table and any transaction tables with
// the same placement.
class TransactionTableState {
 public:
  explicit TransactionTableState(LocalTabletFilter local_tablet_filter)
      : local_tablet_filter_(local_tablet_filter) {
  }

  void InvokeCallback(const PickStatusTabletCallback& callback,
                      TransactionFullLocality locality) EXCLUDES(mutex_) {
    SharedLock<yb::RWMutex> lock(mutex_);
    const auto& tablets = PickTabletList(locality);
    if (tablets.empty()) {
      callback(STATUS_FORMAT(
          IllegalState, "No $0 transaction tablets found", locality));
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
      tablespace_region_local_.clear();
      tablespace_contains_tablespace_.clear();
      has_region_local_tablets_.store(!tablets_.region_local_tablets.empty());
      status_tablets_version_ = new_version;
      initialized_.store(true);
    }
  }

  bool HasAnyRegionLocalStatusTablets() {
    return has_region_local_tablets_.load();
  }

  bool HasAnyTransactionLocalStatusTablets() {
    SharedLock lock(mutex_);
    return !tablets_.tablespaces.empty();
  }

  bool HasAnyTransactionLocalStatusTablets(PgTablespaceOid tablespace_oid) {
    SharedLock lock(mutex_);
    return tablets_.tablespaces.contains(tablespace_oid);
  }

  bool TablespaceIsRegionLocal(PgTablespaceOid tablespace_oid) {
    if (tablespace_oid == kPgInvalidOid) {
      return false;
    }

    {
      SharedLock lock(mutex_);
      auto itr = tablespace_region_local_.find(tablespace_oid);
      if (itr != tablespace_region_local_.end()) {
        return itr->second;
      }
    }

    std::lock_guard lock(mutex_);
    auto itr = tablets_.tablespaces.find(tablespace_oid);
    if (itr == tablets_.tablespaces.end()) {
      return false;
    }

    auto& placement = itr->second.placement_info;
    return tablespace_region_local_.try_emplace(
        tablespace_oid,
        PlacementInfoContainsCloudInfo(placement, GetPlacementFromGFlags()) &&
            !PlacementInfoSpansMultipleRegions(placement)).first->second;
  }

  bool TablespaceContainsTablespace(PgTablespaceOid lhs, PgTablespaceOid rhs) {
    if (lhs == rhs) {
      return true;
    }

    {
      SharedLock lock(mutex_);
      auto itr = tablespace_contains_tablespace_.find({lhs, rhs});
      if (PREDICT_TRUE(itr != tablespace_contains_tablespace_.end())) {
        return itr->second;
      }
    }

    std::lock_guard lock(mutex_);
    return tablespace_contains_tablespace_.try_emplace(
        {lhs, rhs}, CalculateTablespaceContainsTablespace(lhs, rhs)).first->second;
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

  const std::vector<TabletId>& PickTabletList(TransactionFullLocality locality)
      REQUIRES_SHARED(mutex_) {
    switch (locality.locality) {
      case TransactionLocality::GLOBAL:
        return tablets_.global_tablets;
      case TransactionLocality::REGION_LOCAL:
        if (tablets_.region_local_tablets.empty()) {
          YB_LOG_EVERY_N_SECS(WARNING, 1) << "No region-local status tablets found";
          return tablets_.global_tablets;
        }
        return tablets_.region_local_tablets;
      case TransactionLocality::TABLESPACE_LOCAL: {
        auto itr = tablets_.tablespaces.find(locality.tablespace_oid);
        if (itr == tablets_.tablespaces.end() || itr->second.tablets.empty()) {
          YB_LOG_EVERY_N_SECS(WARNING, 1)
              << "No status tablets found for tablespace " << locality.tablespace_oid;
          return tablets_.global_tablets;
        }
        return itr->second.tablets;
      }
    }
    FATAL_INVALID_ENUM_VALUE(TransactionLocality, locality.locality);
  }

  bool CalculateTablespaceContainsTablespace(PgTablespaceOid lhs, PgTablespaceOid rhs)
      REQUIRES(mutex_) {
    // If a tablespace oid is not in tablets_.tablespaces, that means that we have no status
    // tablet information for the tablespace, and thus are not able to meaningfully process a
    // tablespace-local(tablespace) locality. So we treat such a case as global, and thus:
    // - LHS not found: LHS is global => contains everything
    // - LHS found, RHS not found: RHS is global => does not contain LHS (which is not global)
    auto lhs_itr = tablets_.tablespaces.find(lhs);
    if (lhs_itr == tablets_.tablespaces.end()) {
      return true;
    }
    auto rhs_itr = tablets_.tablespaces.find(rhs);
    if (rhs_itr == tablets_.tablespaces.end()) {
      return false;
    }

    return PlacementInfoContainsPlacementInfo(
        lhs_itr->second.placement_info, rhs_itr->second.placement_info);
  }

  LocalTabletFilter local_tablet_filter_;

  // Set to true once transaction tablets have been loaded at least once. global_tablets
  // is assumed to have at least one entry in it if this is true.
  std::atomic<bool> initialized_{false};

  // Set to true if there are any region local transaction tablets.
  std::atomic<bool> has_region_local_tablets_{false};

  // Locks the version/tablet lists. A read lock is acquired when picking
  // tablets, and a write lock is acquired when updating tablet lists.
  RWMutex mutex_;

  uint64_t status_tablets_version_ GUARDED_BY(mutex_) = 0;

  TransactionStatusTablets tablets_ GUARDED_BY(mutex_);

  std::unordered_map<PgTablespaceOid, bool> tablespace_region_local_ GUARDED_BY(mutex_);

  struct TablespaceOidPair {
    PgTablespaceOid first;
    PgTablespaceOid second;

    bool operator==(const TablespaceOidPair&) const = default;
    YB_STRUCT_DEFINE_HASH(TablespaceOidPair, first, second);
  };
  std::unordered_map<TablespaceOidPair, bool> tablespace_contains_tablespace_ GUARDED_BY(mutex_);
};

// Loads transaction tablets list to cache.
class LoadStatusTabletsTask {
 public:
  LoadStatusTabletsTask(YBClient* client,
                        TransactionTableState* table_state,
                        uint64_t version,
                        PickStatusTabletCallback callback = PickStatusTabletCallback(),
                        TransactionFullLocality locality = TransactionFullLocality::Global())
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
    return client_->GetTransactionStatusTablets(GetPlacementFromGFlags());
  }

  YBClient* client_;
  TransactionTableState* table_state_;
  uint64_t version_;
  PickStatusTabletCallback callback_;
  TransactionFullLocality locality_;
};

class InvokeCallbackTask {
 public:
  InvokeCallbackTask(TransactionTableState* table_state,
                     PickStatusTabletCallback callback,
                     TransactionFullLocality locality)
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
  TransactionFullLocality locality_;
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
    if (auto metric_entity = client_->metric_entity()) {
      transaction_promotions_ = METRIC_transaction_promotions.Instantiate(metric_entity);
      initially_global_transactions_ =
          METRIC_initially_global_transactions.Instantiate(metric_entity);
      initially_region_local_transactions_ =
          METRIC_initially_region_local_transactions.Instantiate(metric_entity);
      initially_tablespace_local_transactions_ =
          METRIC_initially_tablespace_local_transactions.Instantiate(metric_entity);
    }
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

  void PickStatusTablet(
      PickStatusTabletCallback callback, TransactionFullLocality locality) {
    if (table_state_.IsInitialized()) {
      if (ThreadRestrictions::IsWaitAllowed()) {
        table_state_.InvokeCallback(callback, locality);
      } else {
        ASH_ENABLE_CONCURRENT_UPDATES();
        SET_WAIT_STATUS(OnCpu_Passive);
        if (!invoke_callback_tasks_.Enqueue(
          &thread_pool_, &table_state_, callback, locality)) {
          SCOPED_WAIT_STATUS(OnCpu_Active);
          callback(STATUS_FORMAT(ServiceUnavailable,
                                 "Invoke callback queue overflow, number of tasks: $0",
                                 invoke_callback_tasks_.size()));
        }
      }
      return;
    }

    ASH_ENABLE_CONCURRENT_UPDATES();
    SET_WAIT_STATUS(OnCpu_Passive);
    if (!tasks_pool_.Enqueue(
      &thread_pool_, client_, &table_state_, 0 /* version */, callback,
      locality)) {
      SCOPED_WAIT_STATUS(OnCpu_Active);
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

  bool RegionLocalTransactionsPossible() {
    return table_state_.HasAnyRegionLocalStatusTablets();
  }

  bool TablespaceIsRegionLocal(PgTablespaceOid tablespace_oid) {
    return table_state_.TablespaceIsRegionLocal(tablespace_oid);
  }

  bool TablespaceLocalTransactionsPossible() {
    return table_state_.HasAnyTransactionLocalStatusTablets();
  }

  bool TablespaceLocalTransactionsPossible(PgTablespaceOid tablespace_oid) {
    return table_state_.HasAnyTransactionLocalStatusTablets(tablespace_oid);
  }

  bool TablespaceContainsTablespace(PgTablespaceOid lhs, PgTablespaceOid rhs) {
    return table_state_.TablespaceContainsTablespace(lhs, rhs);
  }

  uint64_t GetLoadedStatusTabletsVersion() {
    return table_state_.GetStatusTabletsVersion();
  }

  scoped_refptr<Counter> transaction_promotions_metric() const {
    return transaction_promotions_;
  }

  scoped_refptr<Counter> initially_global_transactions_metric() const {
    return initially_global_transactions_;
  }

  scoped_refptr<Counter> initially_region_local_transactions_metric() const {
    return initially_region_local_transactions_;
  }

  scoped_refptr<Counter> initially_tablespace_local_transactions_metric() const {
    return initially_tablespace_local_transactions_;
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

  // These are incremented by YBTransaction, but we keep them here so that the counters are not
  // deleted/recreated after a period of idleness.
  scoped_refptr<Counter> transaction_promotions_;
  scoped_refptr<Counter> initially_global_transactions_;
  scoped_refptr<Counter> initially_region_local_transactions_;
  scoped_refptr<Counter> initially_tablespace_local_transactions_;
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
    PickStatusTabletCallback callback, TransactionFullLocality locality) {
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

bool TransactionManager::RegionLocalTransactionsPossible() {
  return impl_->RegionLocalTransactionsPossible();
}

bool TransactionManager::TablespaceIsRegionLocal(PgTablespaceOid tablespace_oid) {
  return impl_->TablespaceIsRegionLocal(tablespace_oid);
}

bool TransactionManager::TablespaceLocalTransactionsPossible() {
  return impl_->TablespaceLocalTransactionsPossible();
}

bool TransactionManager::TablespaceLocalTransactionsPossible(PgTablespaceOid tablespace_oid) {
  return impl_->TablespaceLocalTransactionsPossible(tablespace_oid);
}

bool TransactionManager::TablespaceContainsTablespace(PgTablespaceOid lhs, PgTablespaceOid rhs) {
  return impl_->TablespaceContainsTablespace(lhs, rhs);
}

uint64_t TransactionManager::GetLoadedStatusTabletsVersion() {
  return impl_->GetLoadedStatusTabletsVersion();
}

void TransactionManager::Shutdown() {
  impl_->Shutdown();
}

scoped_refptr<Counter> TransactionManager::transaction_promotions_metric() const {
  return impl_->transaction_promotions_metric();
}

scoped_refptr<Counter> TransactionManager::initially_global_transactions_metric() const {
  return impl_->initially_global_transactions_metric();
}

scoped_refptr<Counter> TransactionManager::initially_region_local_transactions_metric() const {
  return impl_->initially_region_local_transactions_metric();
}

scoped_refptr<Counter> TransactionManager::initially_tablespace_local_transactions_metric() const {
  return impl_->initially_tablespace_local_transactions_metric();
}

TransactionManager::TransactionManager(TransactionManager&& rhs) = default;
TransactionManager& TransactionManager::operator=(TransactionManager&& rhs) = default;

} // namespace client
} // namespace yb
