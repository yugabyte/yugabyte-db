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

#pragma once

#include <future>
#include <memory>
#include <vector>

#include "yb/tablet/tablet_retention_policy.h"
#include "yb/util/env.h"
#include "yb/util/threadpool.h"
#include "yb/rocksdb/env.h"

#include "yb/client/client_fwd.h"

#include "yb/consensus/log_fwd.h"

#include "yb/docdb/local_waiting_txn_registry.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/tablet_fwd.h"

namespace rocksdb {
class Cache;
class EventListener;
class MemoryMonitor;
class Env;

struct RocksDBPriorityThreadPoolMetrics;
}

namespace yb {

class AutoFlagsManagerBase;
class Env;
class MemTracker;
class MetricRegistry;

namespace tablet {

struct TabletFullCompactionListener;

// Common for all tablets within TabletManager.
struct TabletOptions {
  std::shared_ptr<rocksdb::Cache> block_cache;
  std::shared_ptr<rocksdb::MemoryMonitor> memory_monitor;
  std::vector<std::shared_ptr<rocksdb::EventListener>> listeners;
  yb::Env* env = Env::Default();
  rocksdb::Env* rocksdb_env = rocksdb::Env::Default();
  std::shared_ptr<rocksdb::RateLimiter> rate_limiter;
  std::shared_ptr<rocksdb::RocksDBPriorityThreadPoolMetrics> priority_thread_pool_metrics;
};

using TransactionManagerProvider = std::function<client::TransactionManager&()>;

struct TabletInitData {
  RaftGroupMetadataPtr metadata;
  std::shared_future<client::YBClient*> client_future;
  scoped_refptr<server::Clock> clock;
  std::shared_ptr<MemTracker> parent_mem_tracker;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker;
  MetricRegistry* metric_registry = nullptr;
  log::LogAnchorRegistryPtr log_anchor_registry;
  const TabletOptions tablet_options;
  std::string log_prefix_suffix;
  TransactionParticipantContext* transaction_participant_context = nullptr;
  client::LocalTabletFilter local_tablet_filter;
  TransactionCoordinatorContext* transaction_coordinator_context = nullptr;
  TransactionsEnabled txns_enabled = TransactionsEnabled::kTrue;
  IsSysCatalogTablet is_sys_catalog = IsSysCatalogTablet::kFalse;
  SnapshotCoordinator* snapshot_coordinator = nullptr;
  TabletSplitter* tablet_splitter = nullptr;
  AllowedHistoryCutoffProvider allowed_history_cutoff_provider;
  TransactionManagerProvider transaction_manager_provider;
  docdb::LocalWaitingTxnRegistry* waiting_txn_registry = nullptr;
  ThreadPool* wait_queue_pool = nullptr;
  AutoFlagsManagerBase* auto_flags_manager = nullptr;
  ThreadPool* full_compaction_pool;
  ThreadPool* admin_triggered_compaction_pool;
  scoped_refptr<yb::AtomicGauge<uint64_t>> post_split_compaction_added;
  client::YBMetaDataCache* metadata_cache;
  std::function<SchemaVersion(const TableId&, const ColocationId&)>
      get_min_xcluster_schema_version = nullptr;
  rpc::Messenger* messenger = nullptr;
};

} // namespace tablet
} // namespace yb
