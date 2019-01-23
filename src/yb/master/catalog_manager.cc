// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// The catalog manager handles the current list of tables
// and tablets in the cluster, as well as their current locations.
// Since most operations in the master go through these data
// structures, locking is carefully managed here to prevent unnecessary
// contention and deadlocks:
//
// - each structure has an internal spinlock used for operations that
//   are purely in-memory (eg the current status of replicas)
// - data that is persisted on disk is stored in separate PersistentTable(t)Info
//   structs. These are managed using copy-on-write so that writers may block
//   writing them back to disk while not impacting concurrent readers.
//
// Usage rules:
// - You may obtain READ locks in any order. READ locks should never block,
//   since they only conflict with COMMIT which is a purely in-memory operation.
//   Thus they are deadlock-free.
// - If you need a WRITE lock on both a table and one or more of its tablets,
//   acquire the lock on the table first. This strict ordering prevents deadlocks.

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"

#include <stdlib.h>

#include <algorithm>
#include <bitset>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <glog/logging.h>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "yb/common/common_flags.h"
#include "yb/common/partial_row.h"
#include "yb/common/partition.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.proxy.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/quorum_util.h"
#include "yb/gutil/atomicops.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/walltime.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/system_tablet.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/yql_auth_roles_vtable.h"
#include "yb/master/yql_auth_role_permissions_vtable.h"
#include "yb/master/yql_auth_resource_role_permissions_index.h"
#include "yb/master/yql_columns_vtable.h"
#include "yb/master/yql_empty_vtable.h"
#include "yb/master/yql_keyspaces_vtable.h"
#include "yb/master/yql_local_vtable.h"
#include "yb/master/yql_peers_vtable.h"
#include "yb/master/yql_tables_vtable.h"
#include "yb/master/yql_aggregates_vtable.h"
#include "yb/master/yql_functions_vtable.h"
#include "yb/master/yql_indexes_vtable.h"
#include "yb/master/yql_triggers_vtable.h"
#include "yb/master/yql_types_vtable.h"
#include "yb/master/yql_views_vtable.h"
#include "yb/master/yql_partitions_vtable.h"
#include "yb/master/yql_size_estimates_vtable.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/crypt.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/stopwatch.h"
#include "yb/util/thread.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"

#include "yb/tserver/remote_bootstrap_client.h"

using namespace std::literals;

DEFINE_int32(master_ts_rpc_timeout_ms, 30 * 1000,  // 30 sec
             "Timeout used for the Master->TS async rpc calls.");
TAG_FLAG(master_ts_rpc_timeout_ms, advanced);

DEFINE_int32(tablet_creation_timeout_ms, 30 * 1000,  // 30 sec
             "Timeout used by the master when attempting to create tablet "
             "replicas during table creation.");
TAG_FLAG(tablet_creation_timeout_ms, advanced);

DEFINE_bool(catalog_manager_wait_for_new_tablets_to_elect_leader, true,
            "Whether the catalog manager should wait for a newly created tablet to "
            "elect a leader before considering it successfully created. "
            "This is disabled in some tests where we explicitly manage leader "
            "election.");
TAG_FLAG(catalog_manager_wait_for_new_tablets_to_elect_leader, hidden);

DEFINE_int32(replication_factor, 3,
             "Default number of replicas for tables that do not have the num_replicas set.");
TAG_FLAG(replication_factor, advanced);

DEFINE_int32(catalog_manager_bg_task_wait_ms, 1000,
             "Amount of time the catalog manager background task thread waits "
             "between runs");
TAG_FLAG(catalog_manager_bg_task_wait_ms, hidden);

DEFINE_int32(max_create_tablets_per_ts, 50,
             "The number of tablets per TS that can be requested for a new table.");
TAG_FLAG(max_create_tablets_per_ts, advanced);

DEFINE_int32(master_failover_catchup_timeout_ms, 30 * 1000,  // 30 sec
             "Amount of time to give a newly-elected leader master to load"
             " the previous master's metadata and become active. If this time"
             " is exceeded, the node crashes.");
TAG_FLAG(master_failover_catchup_timeout_ms, advanced);
TAG_FLAG(master_failover_catchup_timeout_ms, experimental);

DEFINE_bool(master_tombstone_evicted_tablet_replicas, true,
            "Whether the Master should tombstone (delete) tablet replicas that "
            "are no longer part of the latest reported raft config.");
TAG_FLAG(master_tombstone_evicted_tablet_replicas, hidden);

DEFINE_bool(catalog_manager_check_ts_count_for_create_table, true,
            "Whether the master should ensure that there are enough live tablet "
            "servers to satisfy the provided replication count before allowing "
            "a table to be created.");
TAG_FLAG(catalog_manager_check_ts_count_for_create_table, hidden);

METRIC_DEFINE_gauge_uint32(cluster, num_tablet_servers_live,
                           "Number of live tservers in the cluster", yb::MetricUnit::kUnits,
                           "The number of tablet servers that have responded or done a heartbeat "
                           "in the time interval defined by the gflag "
                           "FLAGS_tserver_unresponsive_timeout_ms.");

DEFINE_test_flag(uint64, inject_latency_during_remote_bootstrap_secs, 0,
                 "Number of seconds to sleep during a remote bootstrap.");

DEFINE_test_flag(bool, catalog_manager_simulate_system_table_create_failure, false,
                 "This is only used in tests to simulate a failure where the table information is "
                 "persisted in syscatalog, but the tablet information is not yet persisted and "
                 "there is a failure.");

DEFINE_string(cluster_uuid, "", "Cluster UUID to be used by this cluster");
TAG_FLAG(cluster_uuid, hidden);

DECLARE_int32(yb_num_shards_per_tserver);
DECLARE_int32(master_discovery_timeout_ms);

DEFINE_uint64(transaction_table_num_tablets, 0,
    "Number of tablets to use when creating the transaction status table."
    "0 to use the same default num tablets as for regular tables.");

DEFINE_bool(
    hide_pg_catalog_table_creation_logs, false,
    "Whether to hide detailed log messages for PostgreSQL catalog table creation. "
    "This cuts down test logs significantly.");
TAG_FLAG(hide_pg_catalog_table_creation_logs, hidden);

namespace yb {
namespace master {

using std::atomic;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

using namespace std::placeholders;

using base::subtle::NoBarrier_Load;
using base::subtle::NoBarrier_CompareAndSwap;
using consensus::kMinimumTerm;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::CONSENSUS_CONFIG_ACTIVE;
using consensus::COMMITTED_OPID;
using consensus::Consensus;
using consensus::ConsensusMetadata;
using consensus::ConsensusServiceProxy;
using consensus::ConsensusStatePB;
using consensus::GetConsensusRole;
using consensus::OpId;
using consensus::RaftPeerPB;
using consensus::StartRemoteBootstrapRequestPB;
using rpc::RpcContext;
using strings::Substitute;
using tablet::TABLET_DATA_COPYING;
using tablet::TABLET_DATA_DELETED;
using tablet::TABLET_DATA_READY;
using tablet::TABLET_DATA_TOMBSTONED;
using tablet::TabletDataState;
using tablet::TabletMetadata;
using tablet::TabletPeer;
using tablet::TabletStatePB;
using tablet::TabletStatusListener;
using tablet::TabletStatusPB;
using tserver::HandleReplacingStaleTablet;
using tserver::YB_EDITION_NS_PREFIX RemoteBootstrapClient;
using tserver::TabletServerErrorPB;
using master::MasterServiceProxy;
using yb::util::kBcryptHashSize;
using yb::util::bcrypt_hashpw;

const auto kLockLongLimit = RegularBuildVsSanitizers(100ms, 750ms);

constexpr const char* const kDefaultCassandraUsername = "cassandra";
constexpr const char* const kDefaultCassandraPassword = "cassandra";

#define RETURN_NAMESPACE_NOT_FOUND(s, resp)                                       \
  do {                                                                            \
    if (PREDICT_FALSE(!s.ok())) {                                                 \
      if (s.IsNotFound()) {                                                       \
        return SetupError(                                                        \
            resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);        \
      }                                                                           \
      return s;                                                                   \
    }                                                                             \
  } while (false)

////////////////////////////////////////////////////////////
// Table Loader
////////////////////////////////////////////////////////////

class TableLoader : public Visitor<PersistentTableInfo> {
 public:
  explicit TableLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const TableId& table_id, const SysTablesEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->table_ids_map_, table_id))
          << "Table already exists: " << table_id;

    // Setup the table info.
    TableInfo *table = new TableInfo(table_id);
    auto l = table->LockForWrite();
    auto& pb = l->mutable_data()->pb;
    pb.CopyFrom(metadata);

    if (pb.table_type() == TableType::REDIS_TABLE_TYPE && pb.name() == kTransactionsTableName) {
      pb.set_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
    }

    // Add the table to the IDs map and to the name map (if the table is not deleted). Do not
    // add Postgres tables to the name map as the table name is not unique in a namespace.
    catalog_manager_->table_ids_map_[table->id()] = table;
    if (l->data().table_type() != PGSQL_TABLE_TYPE && !l->data().started_deleting()) {
      catalog_manager_->table_names_map_[{l->data().namespace_id(), l->data().name()}] = table;
    }

    l->Commit();

    LOG(INFO) << "Loaded metadata for table " << table->ToString();
    VLOG(1) << "Metadata for table " << table->ToString() << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TableLoader);
};

////////////////////////////////////////////////////////////
// Tablet Loader
////////////////////////////////////////////////////////////

class TabletLoader : public Visitor<PersistentTabletInfo> {
 public:
  explicit TabletLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const TabletId& tablet_id, const SysTabletsEntryPB& metadata) override {

    // Lookup the table.
    scoped_refptr<TableInfo> first_table(FindPtrOrNull(
                                     catalog_manager_->table_ids_map_, metadata.table_id()));

    // Setup the tablet info.
    TabletInfo* tablet = new TabletInfo(first_table, tablet_id);
    auto l = tablet->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // Add the tablet to the tablet manager.
    auto inserted = catalog_manager_->tablet_map_.emplace(tablet->tablet_id(), tablet).second;
    if (!inserted) {
      return STATUS_FORMAT(
          IllegalState, "Loaded tablet that already in map: $0", tablet->tablet_id());
    }

    std::vector<TableId> table_ids;
    for (int k = 0; k < metadata.table_ids_size(); ++k) {
      table_ids.push_back(metadata.table_ids(k));
    }

    // This is for backwards compatibility: we want to ensure that the table_ids
    // list contains the first table that created the tablet. If the table_ids field
    // was empty, we "upgrade" the master to support this new invariant.
    if (metadata.table_ids_size() == 0) {
      l->mutable_data()->pb.add_table_ids(metadata.table_id());
      Status s = catalog_manager_->sys_catalog_->UpdateItem(
          tablet, catalog_manager_->leader_ready_term_);
      if (PREDICT_FALSE(!s.ok())) {
        return STATUS_FORMAT(
            IllegalState, "An error occurred while inserting to sys-tablets: $0", s);
      }
      table_ids.push_back(metadata.table_id());
    }

    for (auto table_id : table_ids) {
      scoped_refptr<TableInfo> table(FindPtrOrNull(catalog_manager_->table_ids_map_, table_id));

      if (table == nullptr) {
        // If the table is missing and the tablet is in "preparing" state
        // may mean that the table was not created (maybe due to a failed write
        // for the sys-tablets). The cleaner will remove.
        if (l->data().pb.state() == SysTabletsEntryPB::PREPARING) {
          LOG(WARNING) << "Missing table " << table_id << " required by tablet " << tablet_id
                       << " (probably a failed table creation: the tablet was not assigned)";
          return Status::OK();
        }

        // if the tablet is not in a "preparing" state, something is wrong...
        LOG(ERROR) << "Missing table " << table_id << " required by tablet " << tablet_id
                   << "Metadata: " << metadata.DebugString();
        return STATUS(Corruption, "Missing table for tablet: ", tablet_id);
      }

      // Add the tablet to the Table.
      if (!l->mutable_data()->is_deleted()) {
        table->AddTablet(tablet);
      }
    }

    l->Commit();

    // TODO(KUDU-1070): if we see a running tablet under a deleted table,
    // we should "roll forward" the deletion of the tablet here.

    LOG(INFO) << "Loaded metadata for tablet " << tablet_id
              << " (first table " << first_table->ToString() << ")";
    VLOG(1) << "Metadata for tablet " << tablet_id << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TabletLoader);
};

////////////////////////////////////////////////////////////
// Namespace Loader
////////////////////////////////////////////////////////////

class NamespaceLoader : public Visitor<PersistentNamespaceInfo> {
 public:
  explicit NamespaceLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const NamespaceId& ns_id, const SysNamespaceEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->namespace_ids_map_, ns_id))
      << "Namespace already exists: " << ns_id;

    // Setup the namespace info.
    NamespaceInfo *const ns = new NamespaceInfo(ns_id);
    auto l = ns->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // Add the namespace to the IDs map and to the name map (if the namespace is not deleted).
    catalog_manager_->namespace_ids_map_[ns_id] = ns;
    if (!l->data().pb.name().empty()) {
      catalog_manager_->namespace_names_map_[l->data().pb.name()] = ns;
    }

    l->Commit();

    LOG(INFO) << "Loaded metadata for namespace " << ns->ToString();
    VLOG(1) << "Metadata for namespace " << ns->ToString() << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(NamespaceLoader);
};

////////////////////////////////////////////////////////////
// User-Defined Type Loader
////////////////////////////////////////////////////////////

class UDTypeLoader : public Visitor<PersistentUDTypeInfo> {
 public:
  explicit UDTypeLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const UDTypeId& udtype_id, const SysUDTypeEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->udtype_ids_map_, udtype_id))
        << "Type already exists: " << udtype_id;

    // Setup the table info.
    UDTypeInfo *const udtype = new UDTypeInfo(udtype_id);
    auto l = udtype->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // Add the used-defined type to the IDs map and to the name map (if the type is not deleted).
    catalog_manager_->udtype_ids_map_[udtype->id()] = udtype;
    if (!l->data().name().empty()) { // If name is set (non-empty) then type is not deleted.
      catalog_manager_->udtype_names_map_[{l->data().namespace_id(), l->data().name()}] = udtype;
    }

    l->Commit();

    LOG(INFO) << "Loaded metadata for type " << udtype->ToString();
    VLOG(1) << "Metadata for type " << udtype->ToString() << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(UDTypeLoader);
};

////////////////////////////////////////////////////////////
// Config Loader
////////////////////////////////////////////////////////////

class ClusterConfigLoader : public Visitor<PersistentClusterConfigInfo> {
 public:
  explicit ClusterConfigLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  virtual Status Visit(
      const std::string& unused_id, const SysClusterConfigEntryPB& metadata) override {
    // Debug confirm that there is no cluster_config_ set. This also ensures that this does not
    // visit multiple rows. Should update this, if we decide to have multiple IDs set as well.
    DCHECK(!catalog_manager_->cluster_config_) << "Already have config data!";

    // Prepare the config object.
    ClusterConfigInfo* config = new ClusterConfigInfo();
    auto l = config->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    if (metadata.has_server_blacklist()) {
      // Rebuild the blacklist state for load movement completion tracking.
      RETURN_NOT_OK(catalog_manager_->SetBlackList(metadata.server_blacklist()));
    }

    // Update in memory state.
    catalog_manager_->cluster_config_ = config;
    l->Commit();

    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(ClusterConfigLoader);
};

class RedisConfigLoader : public Visitor<PersistentRedisConfigInfo> {
 public:
  explicit RedisConfigLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  Status Visit(const std::string& key, const SysRedisConfigEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->redis_config_map_, key))
        << "Redis Config with key already exists: " << key;
    // Prepare the config object.
    RedisConfigInfo* config = new RedisConfigInfo(key);
    auto l = config->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);
    catalog_manager_->redis_config_map_[key] = config;
    l->Commit();
    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(RedisConfigLoader);
};

////////////////////////////////////////////////////////////
// Role Loader
////////////////////////////////////////////////////////////

class RoleLoader : public Visitor<PersistentRoleInfo> {
 public:
  explicit RoleLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const RoleName& role_name, const SysRoleEntryPB& metadata) override {
    CHECK(!ContainsKey(catalog_manager_->roles_map_, role_name))
      << "Role already exists: " << role_name;

    RoleInfo* const role = new RoleInfo(role_name);
    auto l = role->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);
    catalog_manager_->roles_map_[role_name] = role;

    l->Commit();

    LOG(INFO) << "Loaded metadata for role " << role->id();
    VLOG(1) << "Metadata for role " << role->id() << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(RoleLoader);
};

class SysConfigLoader : public Visitor<PersistentSysConfigInfo> {
 public:
  explicit SysConfigLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const string& config_type, const SysConfigEntryPB& metadata) override {
    SysConfigInfo* const config = new SysConfigInfo(config_type);
    auto l = config->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);

    // For now we are only using this to store security config.
    if (config_type == kSecurityConfigType) {
      LOG_IF(WARNING, catalog_manager_->security_config_ != nullptr)
          << "Multiple sys config type " << config_type << " found";
      catalog_manager_->security_config_ = config;
    }

    l->Commit();

    LOG(INFO) << "Loaded sys config type " << config_type;
    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(SysConfigLoader);
};

////////////////////////////////////////////////////////////
// Background Tasks
////////////////////////////////////////////////////////////

class CatalogManagerBgTasks {
 public:
  explicit CatalogManagerBgTasks(CatalogManager *catalog_manager)
    : closing_(false), pending_updates_(false), cond_(&lock_),
      thread_(nullptr), catalog_manager_(catalog_manager) {
  }

  ~CatalogManagerBgTasks() {}

  Status Init();
  void Shutdown();

  void Wake() {
    MutexLock lock(lock_);
    pending_updates_ = true;
    cond_.Broadcast();
  }

  void Wait(int msec) {
    MutexLock lock(lock_);
    if (closing_.load()) return;
    if (!pending_updates_) {
      cond_.TimedWait(MonoDelta::FromMilliseconds(msec));
    }
    pending_updates_ = false;
  }

  void WakeIfHasPendingUpdates() {
    MutexLock lock(lock_);
    if (pending_updates_) {
      cond_.Broadcast();
    }
  }

 private:
  void Run();

 private:
  atomic<bool> closing_;
  bool pending_updates_;
  mutable Mutex lock_;
  ConditionVariable cond_;
  scoped_refptr<yb::Thread> thread_;
  CatalogManager *catalog_manager_;
};

Status CatalogManagerBgTasks::Init() {
  RETURN_NOT_OK(yb::Thread::Create("catalog manager", "bgtasks",
      &CatalogManagerBgTasks::Run, this, &thread_));
  return Status::OK();
}

void CatalogManagerBgTasks::Shutdown() {
  {
    bool closing_expected = false;
    if (!closing_.compare_exchange_strong(closing_expected, true)) {
      VLOG(2) << "CatalogManagerBgTasks already shut down";
      return;
    }
  }

  Wake();
  if (thread_ != nullptr) {
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
  }
}

void CatalogManagerBgTasks::Run() {
  while (!closing_.load()) {
    // Perform assignment processing.
    CatalogManager::ScopedLeaderSharedLock l(catalog_manager_);
    if (!l.catalog_status().ok()) {
      LOG(WARNING) << "Catalog manager background task thread going to sleep: "
                   << l.catalog_status().ToString();
    } else if (l.leader_status().ok()) {
      // Clear metrics for dead tservers.
      vector<shared_ptr<TSDescriptor>> descs;
      const auto& ts_manager = catalog_manager_->master_->ts_manager();
      ts_manager->GetAllDescriptors(&descs);
      for (auto& ts_desc : descs) {
        if (!ts_manager->IsTSLive(ts_desc)) {
          ts_desc->ClearMetrics();
        }
      }

      // Report metrics.
      catalog_manager_->ReportMetrics();

      TabletInfos to_delete;
      TabletInfos to_process;

      // Get list of tablets not yet running or already replaced.
      catalog_manager_->ExtractTabletsToProcess(&to_delete, &to_process);

      if (!to_process.empty()) {
        // Transition tablet assignment state from preparing to creating, send
        // and schedule creation / deletion RPC messages, etc.
        Status s = catalog_manager_->ProcessPendingAssignments(to_process);
        if (!s.ok()) {
          // If there is an error (e.g., we are not the leader) abort this task
          // and wait until we're woken up again.
          //
          // TODO Add tests for this in the revision that makes
          // create/alter fault tolerant.
          LOG(ERROR) << "Error processing pending assignments, aborting the current task: "
                     << s.ToString();
        }
      } else {
        catalog_manager_->load_balance_policy_->RunLoadBalancer();
      }
    }

    // if (!to_delete.empty()) {
    //   // TODO: Run the cleaner
    // }

    // Wait for a notification or a timeout expiration.
    //  - CreateTable will call Wake() to notify about the tablets to add
    //  - HandleReportedTablet/ProcessPendingAssignments will call WakeIfHasPendingUpdates()
    //    to notify about tablets creation.
    l.Unlock();
    Wait(FLAGS_catalog_manager_bg_task_wait_ms);
  }
  VLOG(1) << "Catalog manager background task thread shutting down";
}

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

namespace {

std::string RequestorString(RpcContext* rpc) {
  if (rpc) {
    return rpc->requestor_string();
  } else {
    return "internal request";
  }
}

}  // anonymous namespace

CatalogManager::CatalogManager(Master* master)
    : master_(master),
      rng_(GetRandomSeed32()),
      tablet_exists_(false),
      state_(kConstructed),
      leader_ready_term_(-1),
      leader_lock_(RWMutex::Priority::PREFER_WRITING),
      load_balance_policy_(new YB_EDITION_NS_PREFIX ClusterLoadBalancer(this)) {
  yb::InitCommonFlags();
  CHECK_OK(ThreadPoolBuilder("leader-initialization")
           .set_max_threads(1)
           .Build(&worker_pool_));

  if (master_) {
    sys_catalog_.reset(new SysCatalogTable(
        master_, master_->metric_registry(),
        Bind(&CatalogManager::ElectedAsLeaderCb, Unretained(this))));
  }
}

CatalogManager::~CatalogManager() {
  Shutdown();
}

Status CatalogManager::Init(bool is_first_run) {
  {
    std::lock_guard<simple_spinlock> l(state_lock_);
    CHECK_EQ(kConstructed, state_);
    state_ = kStarting;
  }

  // Initialize the metrics emitted by the catalog manager.
  metric_num_tablet_servers_live_ =
    METRIC_num_tablet_servers_live.Instantiate(master_->metric_entity_cluster(), 0);

  RETURN_NOT_OK_PREPEND(InitSysCatalogAsync(is_first_run),
                        "Failed to initialize sys tables async");

  // WaitUntilRunning() must run outside of the lock as to prevent
  // deadlock. This is safe as WaitUntilRunning waits for another
  // thread to finish its work and doesn't itself depend on any state
  // within CatalogManager. Need not start sys catalog or background tasks
  // when we are started in shell mode.
  if (!master_->opts().IsShellMode()) {
    RETURN_NOT_OK_PREPEND(sys_catalog_->WaitUntilRunning(),
                          "Failed waiting for the catalog tablet to run");
    RETURN_NOT_OK(EnableBgTasks());
  }

  {
    std::lock_guard<simple_spinlock> l(state_lock_);
    CHECK_EQ(kStarting, state_);
    state_ = kRunning;
  }
  return Status::OK();
}

Status CatalogManager::ElectedAsLeaderCb() {
  return worker_pool_->SubmitClosure(
      Bind(&CatalogManager::LoadSysCatalogDataTask, Unretained(this)));
}

Status CatalogManager::WaitUntilCaughtUpAsLeader(const MonoDelta& timeout) {
  string uuid = master_->fs_manager()->uuid();
  Consensus* consensus = tablet_peer()->consensus();
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE);
  if (!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid) {
    return STATUS(IllegalState,
        Substitute("Node $0 not leader. Consensus state: $1",
                    uuid, cstate.ShortDebugString()));
  }

  // Wait for all transactions to be committed.
  const MonoTime deadline = MonoTime::Now() + timeout;
  RETURN_NOT_OK(tablet_peer()->operation_tracker()->WaitForAllToFinish(timeout));

  RETURN_NOT_OK(tablet_peer()->consensus()->WaitForLeaderLeaseImprecise(deadline));
  return Status::OK();
}

void CatalogManager::LoadSysCatalogDataTask() {
  auto consensus = tablet_peer()->shared_consensus();
  const int64_t term = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
  Status s = WaitUntilCaughtUpAsLeader(
      MonoDelta::FromMilliseconds(FLAGS_master_failover_catchup_timeout_ms));

  int64_t term_after_wait = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
  if (term_after_wait != term) {
    // If we got elected leader again while waiting to catch up then we will get another callback to
    // update state from sys_catalog, so bail now.
    //
    // If we failed when waiting, i.e. could not acquire a leader lease, this could be due to us
    // becoming a follower. If we're not partitioned away, we'll know about a new term soon.
    LOG(INFO) << "Term change from " << term << " to " << term_after_wait
              << " while waiting for master leader catchup. Not loading sys catalog metadata. "
              << "Status of waiting: " << s;
    return;
  }

  if (!s.ok()) {
    // This could happen e.g. if we are a partitioned-away leader that failed to acquire a leader
    // lease.
    //
    // TODO: handle this cleanly by transitioning to a follower without crashing.
    WARN_NOT_OK(s, "Failed waiting for node to catch up after master election");

    if (s.IsTimedOut()) {
      LOG(FATAL) << "Shutting down due to unavailability of other masters after"
                 << " election. TODO: Abdicate instead.";
    }
    return;
  }

  LOG(INFO) << "Loading table and tablet metadata into memory for term " << term;
  LOG_SLOW_EXECUTION(WARNING, 1000, LogPrefix() + "Loading metadata into memory") {
    Status status = VisitSysCatalog(term);
    if (!status.ok()) {
      auto new_term = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
      if (new_term != term) {
        LOG(INFO) << "Error loading sys catalog; but that's OK as term was changed from " << term
                  << " to " << new_term << ": " << status;
        return;
      }
      LOG(FATAL) << "Failed to load sys catalog: " << status;
    }
  }

  std::lock_guard<simple_spinlock> l(state_lock_);
  leader_ready_term_ = term;
  LOG(INFO) << "Completed load of sys catalog in term " << term;
}

CHECKED_STATUS CatalogManager::WaitForWorkerPoolTests(const MonoDelta& timeout) const {
  if (!worker_pool_->WaitFor(timeout)) {
    return STATUS(TimedOut, "Worker Pool hasn't finished processing tasks");
  }
  return Status::OK();
}

Status CatalogManager::VisitSysCatalog(int64_t term) {
  // Block new catalog operations, and wait for existing operations to finish.
  LOG(INFO) << __func__ << ": Wait on leader_lock_ for any existing operations to finish.";
  auto start = std::chrono::steady_clock::now();
  std::lock_guard<RWMutex> leader_lock_guard(leader_lock_);
  auto finish = std::chrono::steady_clock::now();
  if (finish > start + kLockLongLimit) {
    LOG(WARNING) << "Long wait on leader_lock_: " << yb::ToString(finish - start);
  }

  LOG(INFO) << __func__ << ": Acquire catalog manager lock_ before loading sys catalog..";
  boost::lock_guard<LockType> lock(lock_);

  // Abort any outstanding tasks. All TableInfos are orphaned below, so
  // it's important to end their tasks now; otherwise Shutdown() will
  // destroy master state used by these tasks.
  std::vector<scoped_refptr<TableInfo>> tables;
  AppendValuesFromMap(table_ids_map_, &tables);
  AbortAndWaitForAllTasks(tables);

  // Clear internal maps and run data loaders.
  RETURN_NOT_OK(RunLoaders());

  // Prepare various default system configurations.
  RETURN_NOT_OK(PrepareDefaultSysConfig(term));

  // Create the system namespaces (created only if they don't already exist).
  RETURN_NOT_OK(PrepareDefaultNamespaces(term));

  // Create the system tables (created only if they don't already exist).
  RETURN_NOT_OK(PrepareSystemTables(term));

  // Create the default cassandra (created only if they don't already exist).
  RETURN_NOT_OK(PrepareDefaultRoles(term));

  // If this is the first time we start up, we have no config information as default. We write an
  // empty version 0.
  RETURN_NOT_OK(PrepareDefaultClusterConfig(term));

  BuildRecursiveRolesUnlocked();

  return Status::OK();
}

Status CatalogManager::RunLoaders() {
  // Clear the table and tablet state.
  table_names_map_.clear();
  table_ids_map_.clear();
  tablet_map_.clear();

  // Clear the namespace mappings.
  namespace_ids_map_.clear();
  namespace_names_map_.clear();

  // Clear the type mappings.
  udtype_ids_map_.clear();
  udtype_names_map_.clear();

  // Clear the current cluster config.
  cluster_config_.reset();

  // Clear the roles mapping.
  roles_map_.clear();

  // Clear redis config mapping.
  redis_config_map_.clear();

  std::vector<std::shared_ptr<TSDescriptor>> descs;
  master_->ts_manager()->GetAllDescriptors(&descs);
  for (const auto& ts_desc : descs) {
    ts_desc->set_has_tablet_report(false);
  }

  // Visit tables and tablets, load them into memory.
  // TODO(hector): Refactor this code.
  LOG(INFO) << __func__ << ": Loading tables into memory.";
  unique_ptr<TableLoader> table_loader(new TableLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(table_loader.get()), "Failed while visiting tables in sys catalog");

  LOG(INFO) << __func__ << ": Loading tablets into memory.";
  unique_ptr<TabletLoader> tablet_loader(new TabletLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(tablet_loader.get()), "Failed while visiting tablets in sys catalog");

  LOG(INFO) << __func__ << ": Loading namespaces into memory.";
  unique_ptr<NamespaceLoader> namespace_loader(new NamespaceLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(namespace_loader.get()),
      "Failed while visiting namespaces in sys catalog");

  LOG(INFO) << __func__ << ": Loading user-defined types into memory.";
  unique_ptr<UDTypeLoader> udtype_loader(new UDTypeLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(udtype_loader.get()),
      "Failed while visiting namespaces in sys catalog");

  LOG(INFO) << __func__ << ": Loading cluster configuration into memory.";
  unique_ptr<ClusterConfigLoader> config_loader(new ClusterConfigLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(config_loader.get()), "Failed while visiting config in sys catalog");

  LOG(INFO) << __func__ << ": Loading roles into memory.";
  unique_ptr<RoleLoader> role_loader(new RoleLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(role_loader.get()),
      "Failed while visiting roles in sys catalog");

  LOG(INFO) << __func__ << ": Loading Redis config into memory.";
  unique_ptr<RedisConfigLoader> redis_config_loader(new RedisConfigLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(redis_config_loader.get()),
      "Failed while visiting redis config in sys catalog");

  LOG(INFO) << __func__ << ": Loading sys config into memory.";
  unique_ptr<SysConfigLoader> sys_config_loader(new SysConfigLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(sys_config_loader.get()),
      "Failed while visiting sys config in sys catalog");

  return Status::OK();
}

Status CatalogManager::PrepareDefaultClusterConfig(int64_t term) {
  // Verify we have the catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  if (cluster_config_) {
    LOG(INFO) << "Cluster configuration already setup, skipping re-initialization.";
    return Status::OK();
  }

  // Create default.
  SysClusterConfigEntryPB config;
  config.set_version(0);

  if (!FLAGS_cluster_uuid.empty()) {
    Uuid uuid;
    RETURN_NOT_OK(uuid.FromString(FLAGS_cluster_uuid));
    config.set_cluster_uuid(FLAGS_cluster_uuid);
  } else {
    auto uuid = Uuid::Generate();
    config.set_cluster_uuid(to_string(uuid));
  }

  // Create in memory object.
  cluster_config_ = new ClusterConfigInfo();

  // Prepare write.
  auto l = cluster_config_->LockForWrite();
  l->mutable_data()->pb = std::move(config);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->AddItem(cluster_config_.get(), term));
  l->Commit();

  return Status::OK();
}

Status CatalogManager::PrepareDefaultSysConfig(int64_t term) {
  // Verify we have the catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  // Set up default security config if not already present.
  if (!security_config_) {
    SysSecurityConfigEntryPB security_config;
    security_config.set_roles_version(0);

    // Create in memory object.
    security_config_ = new SysConfigInfo(kSecurityConfigType);

    // Prepare write.
    auto l = security_config_->LockForWrite();
    *l->mutable_data()->pb.mutable_security_config() = std::move(security_config);

    // Write to sys_catalog and in memory.
    RETURN_NOT_OK(sys_catalog_->AddItem(security_config_.get(), term));
    l->Commit();
  }

  return Status::OK();
}

Status CatalogManager::PrepareDefaultNamespaces(int64_t term) {
  RETURN_NOT_OK(PrepareNamespace(kSystemNamespaceName, kSystemNamespaceId, term));
  RETURN_NOT_OK(PrepareNamespace(kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term));
  RETURN_NOT_OK(PrepareNamespace(kSystemAuthNamespaceName, kSystemAuthNamespaceId, term));
  return Status::OK();
}

Status CatalogManager::PrepareSystemTables(int64_t term) {
  // Prepare sys catalog table.
  RETURN_NOT_OK(PrepareSysCatalogTable(term));

  // Create the required system tables here.
  RETURN_NOT_OK((PrepareSystemTableTemplate<PeersVTable>(
      kSystemPeersTableName, kSystemNamespaceName, kSystemNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<LocalVTable>(
      kSystemLocalTableName, kSystemNamespaceName, kSystemNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLKeyspacesVTable>(
      kSystemSchemaKeyspacesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLTablesVTable>(
      kSystemSchemaTablesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLColumnsVTable>(
      kSystemSchemaColumnsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLSizeEstimatesVTable>(
      kSystemSizeEstimatesTableName, kSystemNamespaceName, kSystemNamespaceId, term)));

  // Empty tables.
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAggregatesVTable>(
      kSystemSchemaAggregatesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLFunctionsVTable>(
      kSystemSchemaFunctionsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLIndexesVTable>(
      kSystemSchemaIndexesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLTriggersVTable>(
      kSystemSchemaTriggersTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLViewsVTable>(
      kSystemSchemaViewsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<QLTypesVTable>(
      kSystemSchemaTypesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLPartitionsVTable>(
      kSystemPartitionsTableName, kSystemNamespaceName, kSystemNamespaceId, term)));

  // System auth tables.
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthRolesVTable>(
      kSystemAuthRolesTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId, term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthRolePermissionsVTable>(
      kSystemAuthRolePermissionsTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId,
      term)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthResourceRolePermissionsIndexVTable>(
      kSystemAuthResourceRolePermissionsIndexTableName, kSystemAuthNamespaceName,
      kSystemAuthNamespaceId, term)));

  // Ensure kNumSystemTables is in-sync with the system tables created.
  DCHECK_EQ(system_tablets_.size(), kNumSystemTables);

  return Status::OK();
}

CHECKED_STATUS CatalogManager::PrepareDefaultRoles(int64_t term) {
  // Verify we have the catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  if (FindPtrOrNull(roles_map_, kDefaultCassandraUsername) != nullptr) {
    LOG(INFO) << strings::Substitute("Role $0 already created, skipping initialization",
                                     kDefaultCassandraUsername);
    return Status::OK();
  }

  char hash[kBcryptHashSize];
  // TODO: refactor interface to be more c++ like...
  int ret = bcrypt_hashpw(kDefaultCassandraPassword, hash);
  if (ret != 0) {
    return STATUS(IllegalState, Substitute("Could not hash password, reason: $0", ret));
  }

  // Create in memory object.
  Status s = CreateRoleUnlocked(kDefaultCassandraUsername, std::string(hash, kBcryptHashSize),
                                true, true, term, false /* Don't increment the roles version */);
  if (PREDICT_TRUE(s.ok())) {
    LOG(INFO) << "Created role: " << kDefaultCassandraUsername;
  }

  return s;
}

Status CatalogManager::PrepareSysCatalogTable(int64_t term) {
  // Prepare sys catalog table info.
  if (table_ids_map_.count(kSysCatalogTableId) == 0) {
    scoped_refptr<TableInfo> table(new TableInfo(kSysCatalogTableId));
    table->mutable_metadata()->StartMutation();
    SysTablesEntryPB& metadata = table->mutable_metadata()->mutable_dirty()->pb;
    metadata.set_state(SysTablesEntryPB::RUNNING);
    metadata.set_namespace_id(kSystemSchemaNamespaceId);
    metadata.set_name(kSysCatalogTableName);
    metadata.set_table_type(TableType::YQL_TABLE_TYPE);
    RETURN_NOT_OK(SchemaToPB(sys_catalog_->schema_with_ids_, metadata.mutable_schema()));
    metadata.set_version(0);

    table_ids_map_[table->id()] = table;
    table_names_map_[{kSystemSchemaNamespaceId, kSysCatalogTableName}] = table;

    RETURN_NOT_OK(sys_catalog_->AddItem(table.get(), term));
    table->mutable_metadata()->CommitMutation();
  }

  // Prepare sys catalog tablet info.
  if (tablet_map_.count(kSysCatalogTabletId) == 0) {
    scoped_refptr<TableInfo> table = table_ids_map_[kSysCatalogTableId];
    DCHECK_NOTNULL(table.get());
    scoped_refptr<TabletInfo> tablet(new TabletInfo(table, kSysCatalogTabletId));
    tablet->mutable_metadata()->StartMutation();
    SysTabletsEntryPB& metadata = tablet->mutable_metadata()->mutable_dirty()->pb;
    metadata.set_state(SysTabletsEntryPB::RUNNING);

    auto l = table->LockForRead();
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(l->data().pb.partition_schema(),
                                          sys_catalog_->schema_with_ids_,
                                          &partition_schema));
    vector<Partition> partitions;
    RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
    partitions[0].ToPB(metadata.mutable_partition());
    metadata.set_table_id(table->id());
    metadata.add_table_ids(table->id());

    table->AddTablet(tablet.get());

    tablet_map_[tablet->tablet_id()] = tablet;

    RETURN_NOT_OK(sys_catalog_->AddItem(tablet.get(), term));
    tablet->mutable_metadata()->CommitMutation();
  }

  system_tablets_[kSysCatalogTabletId] = sys_catalog_->tablet_peer_->shared_tablet();

  return Status::OK();
}

template <class T>
Status CatalogManager::PrepareSystemTableTemplate(const TableName& table_name,
                                                  const NamespaceName& namespace_name,
                                                  const NamespaceId& namespace_id,
                                                  int64_t term) {
  YQLVirtualTable* vtable = new T(master_);
  return PrepareSystemTable(
      table_name, namespace_name, namespace_id, vtable->schema(), term, vtable);
}

Status CatalogManager::PrepareSystemTable(const TableName& table_name,
                                          const NamespaceName& namespace_name,
                                          const NamespaceId& namespace_id,
                                          const Schema& schema,
                                          int64_t term,
                                          YQLVirtualTable* vtable) {
  std::unique_ptr<YQLVirtualTable> yql_storage(vtable);
  // Verify we have the catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  scoped_refptr<TableInfo> table = FindPtrOrNull(table_names_map_,
                                                 std::make_pair(namespace_id, table_name));
  bool create_table = true;
  if (table != nullptr) {
    LOG(INFO) << Substitute("Table $0.$1 already created", namespace_name, table_name);

    Schema persisted_schema;
    RETURN_NOT_OK(table->GetSchema(&persisted_schema));
    if (!persisted_schema.Equals(schema)) {
      LOG(INFO) << Substitute("Updating schema of $0.$1 ...", namespace_name, table_name);
      auto l = table->LockForWrite();
      RETURN_NOT_OK(SchemaToPB(schema, l->mutable_data()->pb.mutable_schema()));
      l->mutable_data()->pb.set_version(l->data().pb.version() + 1);

      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->UpdateItem(table.get(), term));
      l->Commit();
    }

    // There might have been a failure after writing the table but before writing the tablets. As
    // a result, if we don't find any tablets, we try to create the tablets only again.
    vector<scoped_refptr<TabletInfo>> tablets;
    table->GetAllTablets(&tablets);
    if (!tablets.empty()) {
      // Initialize the appropriate system tablet.
      DCHECK_EQ(1, tablets.size());
      system_tablets_[tablets[0]->tablet_id()] =
          std::make_shared<SystemTablet>(schema, std::move(yql_storage), tablets[0]->tablet_id());
      return Status::OK();
    } else {
      // Table is already created, only need to create tablets now.
      LOG(INFO) << Substitute("Creating tablets for $0.$1 ...", namespace_name, table_name);
      create_table = false;
    }
  }

  vector<TabletInfo*> tablets;

  // Create partitions.
  vector<Partition> partitions;
  PartitionSchemaPB partition_schema_pb;
  partition_schema_pb.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(partition_schema_pb, schema, &partition_schema));
  RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));

  if (create_table) {
    // Fill in details for the system table.
    CreateTableRequestPB req;
    req.set_name(table_name);
    req.set_table_type(TableType::YQL_TABLE_TYPE);

    RETURN_NOT_OK(CreateTableInMemory(req, schema, partition_schema, true /* create_tablets */,
                                      namespace_id, partitions, nullptr, &tablets, nullptr,
                                      &table));
    LOG(INFO) << Substitute("Inserted new $0.$1 table info into CatalogManager maps",
                            namespace_name, table_name);

    // Update the on-disk table state to "running".
    table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
    RETURN_NOT_OK(sys_catalog_->AddItem(table.get(), term));
    LOG(INFO) << "Wrote table to system catalog: " << ToString(table) << ", tablets: "
              << ToString(tablets);
  } else {
    // Still need to create the tablets.
    RETURN_NOT_OK(CreateTabletsFromTable(partitions, table, &tablets));
  }

  DCHECK_EQ(1, tablets.size());
  // We use LOG_ASSERT here since this is expected to crash in some unit tests.
  LOG_ASSERT(!FLAGS_catalog_manager_simulate_system_table_create_failure);

  // Write Tablets to sys-tablets (in "running" state since we don't want the loadbalancer to
  // assign these tablets since this table is virtual).
  for (TabletInfo *tablet : tablets) {
    tablet->mutable_metadata()->mutable_dirty()->pb.set_state(SysTabletsEntryPB::RUNNING);
  }
  RETURN_NOT_OK(sys_catalog_->AddItems(tablets, term));
  LOG(INFO) << "Wrote tablets to system catalog: " << ToString(tablets);

  // Commit the in-memory state.
  if (create_table) {
    table->mutable_metadata()->CommitMutation();
  }

  for (TabletInfo *tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
  }

  // Finally create the appropriate tablet object.
  system_tablets_[tablets[0]->tablet_id()] =
      std::make_shared<SystemTablet>(schema, std::move(yql_storage), tablets[0]->tablet_id());
  return Status::OK();
}

bool CatalogManager::IsYcqlTable(const TableInfo& table) {
  return table.GetTableType() == TableType::YQL_TABLE_TYPE && table.id() != kSysCatalogTableId;
}

Status CatalogManager::PrepareNamespace(
    const NamespaceName& name, const NamespaceId& id, int64_t term) {
  // Verify we have the catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  if (FindPtrOrNull(namespace_names_map_, name) != nullptr) {
    LOG(INFO) << strings::Substitute("Keyspace $0 already created, skipping initialization",
                                     name);
    return Status::OK();
  }

  // Create entry.
  SysNamespaceEntryPB ns_entry;
  ns_entry.set_name(name);

  // Create in memory object.
  scoped_refptr<NamespaceInfo> ns = new NamespaceInfo(id);

  // Prepare write.
  auto l = ns->LockForWrite();
  l->mutable_data()->pb = std::move(ns_entry);

  namespace_ids_map_[id] = ns;
  namespace_names_map_[l->mutable_data()->pb.name()] = ns;

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->AddItem(ns.get(), term));
  l->Commit();

  LOG(INFO) << "Created default namespace: " << ns->ToString();
  return Status::OK();
}

Status CatalogManager::CheckLocalHostInMasterAddresses() {
  auto local_hostport = master_->first_rpc_address();
  std::vector<IpAddress> local_addrs;

  if (local_hostport.address().is_unspecified()) {
    auto status = GetLocalAddresses(&local_addrs, AddressFilter::ANY);
    if (!status.ok() || local_addrs.empty()) {
      LOG(WARNING) << "Could not enumerate network interfaces due to " << status << ", found "
                   << local_addrs.size() << " local addresses.";
      return Status::OK();
    }
  } else {
    local_addrs.push_back(local_hostport.address());
  }

  auto master_addresses_shared_ptr = master_->opts().GetMasterAddresses();
  const auto kResolveSleepIntervalSec = 1;
  auto kResolveMaxIterations =
     (FLAGS_master_discovery_timeout_ms / 1000) / kResolveSleepIntervalSec;
  if (kResolveMaxIterations < 120) {
    kResolveMaxIterations = 120;
  }
  for (const auto& list : *master_addresses_shared_ptr) {
    for (const HostPort& peer_addr : list) {
      std::vector<Endpoint> addresses;
      // Ignore resolve errors for this check.
      int numIters = 0;
      Status s = peer_addr.ResolveAddresses(&addresses);
      while (!s.ok()) {
        numIters++;
        if (numIters > kResolveMaxIterations) {
          return STATUS_FORMAT(ConfigurationError, "Could not resolve address of $0",
              peer_addr.ToString());
        }
        std::this_thread::sleep_for(std::chrono::seconds(kResolveSleepIntervalSec));
        s = peer_addr.ResolveAddresses(&addresses);
      }
      for (auto const& addr : addresses) {
        if (addr.address().is_unspecified() ||
            std::find(local_addrs.begin(), local_addrs.end(), addr.address()) !=
                 local_addrs.end()) {
          return Status::OK();
        }
      }
    }
  }

  return STATUS(IllegalState,
                Substitute("None of the local addresses are present in master_addresses $0.",
                           master_->opts().master_addresses_flag));
}

Status CatalogManager::InitSysCatalogAsync(bool is_first_run) {
  std::lock_guard<LockType> l(lock_);
  if (is_first_run) {
    if (!master_->opts().AreMasterAddressesProvided()) {
      master_->SetShellMode(true);
      LOG(INFO) << "Starting master in shell mode.";
      return Status::OK();
    }

    RETURN_NOT_OK(CheckLocalHostInMasterAddresses());
    RETURN_NOT_OK(sys_catalog_->CreateNew(master_->fs_manager()));
  } else {
    RETURN_NOT_OK(sys_catalog_->Load(master_->fs_manager()));
  }
  return Status::OK();
}

bool CatalogManager::IsInitialized() const {
  std::lock_guard<simple_spinlock> l(state_lock_);
  return state_ == kRunning;
}

// TODO - delete this API after HandleReportedTablet() usage is removed.
Status CatalogManager::CheckIsLeaderAndReady() const {
  std::lock_guard<simple_spinlock> l(state_lock_);
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(ServiceUnavailable,
        Substitute("Catalog manager is shutting down. State: $0", state_));
  }
  string uuid = master_->fs_manager()->uuid();
  if (master_->opts().IsShellMode()) {
    // Consensus and other internal fields should not be checked when is shell mode.
    return STATUS(IllegalState, Substitute("Catalog manager of $0 is in shell mode, not the leader",
                                           uuid));
  }
  Consensus* consensus = tablet_peer()->consensus();
  if (consensus == nullptr) {
    return STATUS(IllegalState, "Consensus has not been initialized yet");
  }
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    return STATUS(IllegalState,
        Substitute("Not the leader. Local UUID: $0, Consensus state: $1",
                   uuid, cstate.ShortDebugString()));
  }
  if (PREDICT_FALSE(leader_ready_term_ != cstate.current_term())) {
    return STATUS(ServiceUnavailable,
        Substitute("Leader not yet ready to serve requests: ready term $0 vs cstate term $1",
                   leader_ready_term_, cstate.current_term()));
  }
  return Status::OK();
}

const std::shared_ptr<tablet::TabletPeer> CatalogManager::tablet_peer() const {
  return sys_catalog_->tablet_peer();
}

RaftPeerPB::Role CatalogManager::Role() const {
  CHECK(IsInitialized());
  if (master_->opts().IsShellMode()) {
    return RaftPeerPB::NON_PARTICIPANT;
  }

  return tablet_peer()->consensus()->role();
}

void CatalogManager::Shutdown() {
  {
    std::lock_guard<simple_spinlock> l(state_lock_);
    if (state_ == kClosing) {
      VLOG(2) << "CatalogManager already shut down";
      return;
    }
    state_ = kClosing;
  }

  // Shutdown the Catalog Manager background thread.
  if (background_tasks_) {
    background_tasks_->Shutdown();
  }

  // Mark all outstanding table tasks as aborted and wait for them to fail.
  //
  // There may be an outstanding table visitor thread modifying the table map,
  // so we must make a copy of it before we iterate. It's OK if the visitor
  // adds more entries to the map even after we finish; it won't start any new
  // tasks for those entries.
  vector<scoped_refptr<TableInfo>> copy;
  {
    shared_lock<LockType> l(lock_);
    AppendValuesFromMap(table_ids_map_, &copy);
  }
  AbortAndWaitForAllTasks(copy);

  // Shut down the underlying storage for tables and tablets.
  if (sys_catalog_) {
    sys_catalog_->Shutdown();
  }
}

Status CatalogManager::CheckOnline() const {
  if (PREDICT_FALSE(!IsInitialized())) {
    return STATUS(ServiceUnavailable, "CatalogManager is not running");
  }
  return Status::OK();
}

Status CatalogManager::AbortTableCreation(TableInfo* table,
                                          const vector<TabletInfo*>& tablets,
                                          const Status& s,
                                          CreateTableResponsePB* resp) {
  LOG(WARNING) << s;

  const TableId table_id = table->id();
  const TableName table_name = table->mutable_metadata()->mutable_dirty()->pb.name();
  const NamespaceId table_namespace_id =
      table->mutable_metadata()->mutable_dirty()->pb.namespace_id();
  vector<string> tablet_ids_to_erase;
  for (TabletInfo* tablet : tablets) {
    tablet_ids_to_erase.push_back(tablet->tablet_id());
  }

  LOG(INFO) << "Aborting creation of table '" << table_name << "', erasing table and tablets (" <<
      JoinStrings(tablet_ids_to_erase, ",") << ") from in-memory state.";

  // Since this is a failed creation attempt, it's safe to just abort
  // all tasks, as (by definition) no tasks may be pending against a
  // table that has failed to successfully create.
  table->AbortTasks();
  table->WaitTasksCompletion();

  std::lock_guard<LockType> l(lock_);

  // Call AbortMutation() manually, as otherwise the lock won't be released.
  for (TabletInfo* tablet : tablets) {
    tablet->mutable_metadata()->AbortMutation();
  }
  table->mutable_metadata()->AbortMutation();
  for (const TabletId& tablet_id_to_erase : tablet_ids_to_erase) {
    CHECK_EQ(tablet_map_.erase(tablet_id_to_erase), 1)
        << "Unable to erase tablet " << tablet_id_to_erase << " from tablet map.";
  }

  CHECK_EQ(table_names_map_.erase({table_namespace_id, table_name}), 1)
      << "Unable to erase table named " << table_name << " from table names map.";
  CHECK_EQ(table_ids_map_.erase(table_id), 1)
      << "Unable to erase tablet with id " << table_id << " from tablet ids map.";

  return CheckIfNoLongerLeaderAndSetupError(s, resp);
}

Status CatalogManager::ValidateTableReplicationInfo(const ReplicationInfoPB& replication_info) {
  // TODO(bogdan): add the actual subset rules, instead of just erroring out as not supported.
  const auto& live_placement_info = replication_info.live_replicas();
  if (!(live_placement_info.placement_blocks().empty() &&
        live_placement_info.num_replicas() <= 0 &&
        live_placement_info.placement_uuid().empty()) ||
      !replication_info.read_replicas().empty() ||
      !replication_info.affinitized_leaders().empty()) {
    return STATUS(
        InvalidArgument,
        "Unsupported: cannot set table level replication info yet.");
  }
  return Status::OK();
}

Status CatalogManager::CreateIndexInfo(const TableId& indexed_table_id,
                                       const Schema& indexed_schema,
                                       const Schema& index_schema,
                                       const bool is_local,
                                       const bool is_unique,
                                       IndexInfoPB* index_info) {
  // index_info's table_id is set in CreateTableInfo() after the table id is generated.
  index_info->set_indexed_table_id(indexed_table_id);
  index_info->set_version(0);
  index_info->set_is_local(is_local);
  index_info->set_is_unique(is_unique);
  for (size_t i = 0; i < index_schema.num_columns(); i++) {
    const auto& col_name = index_schema.column(i).name();
    const auto indexed_col_idx = indexed_schema.find_column(col_name);
    if (PREDICT_FALSE(indexed_col_idx == Schema::kColumnNotFound)) {
      return STATUS(NotFound, "The indexed table column does not exist", col_name);
    }
    auto* col = index_info->add_columns();
    col->set_column_id(index_schema.column_id(i));
    col->set_indexed_column_id(indexed_schema.column_id(indexed_col_idx));
  }
  index_info->set_hash_column_count(index_schema.num_hash_key_columns());
  index_info->set_range_column_count(index_schema.num_range_key_columns());

  for (size_t i = 0; i < indexed_schema.num_hash_key_columns(); i++) {
    index_info->add_indexed_hash_column_ids(indexed_schema.column_id(i));
  }
  for (size_t i = indexed_schema.num_hash_key_columns(); i < indexed_schema.num_key_columns();
       i++) {
    index_info->add_indexed_range_column_ids(indexed_schema.column_id(i));
  }

  return Status::OK();
}

Status CatalogManager::AddIndexInfoToTable(const scoped_refptr<TableInfo>& indexed_table,
                                           const IndexInfoPB& index_info) {
  TRACE("Locking indexed table");
  auto l = indexed_table->LockForWrite();

  // Add index info to indexed table and increment schema version.
  l->mutable_data()->pb.add_indexes()->CopyFrom(index_info);
  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  l->mutable_data()->set_state(SysTablesEntryPB::ALTERING,
                               Substitute("Alter table version=$0 ts=$1",
                                          l->mutable_data()->pb.version(),
                                          LocalTimeAsString()));

  // Update sys-catalog with the new indexed table info.
  TRACE("Updating indexed table metadata on disk");
  RETURN_NOT_OK(sys_catalog_->UpdateItem(indexed_table.get(), leader_ready_term_));

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();

  SendAlterTableRequest(indexed_table);

  return Status::OK();
}

Status CatalogManager::CreateCopartitionedTable(const CreateTableRequestPB req,
                                                CreateTableResponsePB* resp,
                                                rpc::RpcContext* rpc,
                                                Schema schema,
                                                NamespaceId namespace_id) {
  scoped_refptr<TableInfo> parent_table_info;
  Status s;
  const char* const object_type = "table";
  PartitionSchema partition_schema;
  std::vector<Partition> partitions;

  std::lock_guard<LockType> l(lock_);
  TRACE("Acquired catalog manager lock");
  parent_table_info = FindPtrOrNull(table_ids_map_,
                                    schema.table_properties().CopartitionTableId());
  if (parent_table_info == nullptr) {
    s = STATUS(NotFound, "The table does not exist",
               schema.table_properties().CopartitionTableId());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  scoped_refptr<TableInfo> this_table_info;
  std::vector<TabletInfo *> tablets;
  TabletInfos scoped_ref_tablets;
  // Verify that the table does not exist.
  this_table_info = FindPtrOrNull(table_names_map_, {namespace_id, req.name()});

  if (this_table_info != nullptr) {
    s = STATUS(AlreadyPresent, Substitute("Target $0 already exists", object_type),
               this_table_info->id());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT, s);
  }

  // TODO: pass index_info for copartitioned index.
  RETURN_NOT_OK(CreateTableInMemory(req, schema, partition_schema, false /* create_tablets */,
                                    namespace_id, partitions, nullptr, nullptr, resp,
                                    &this_table_info));

  TRACE("Inserted new table info into CatalogManager maps");

  // NOTE: the table is already locked for write at this point,
  // since the CreateTableInfo function leave it in that state.
  // It will get committed at the end of this function.
  // Sanity check: the table should be in "preparing" state.
  CHECK_EQ(SysTablesEntryPB::PREPARING, this_table_info->metadata().dirty().pb.state());
  parent_table_info->GetAllTablets(&scoped_ref_tablets);
  for (auto tablet : scoped_ref_tablets) {
    tablets.push_back(tablet.get());
    tablet->mutable_metadata()->StartMutation();
    tablet->mutable_metadata()->mutable_dirty()->pb.add_table_ids(this_table_info->id());
  }

  // Update Tablets about new table id to sys-tablets.
  s = sys_catalog_->UpdateItems(tablets, leader_ready_term_);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(this_table_info.get(), tablets, s.CloneAndPrepend(
        Substitute("An error occurred while inserting to sys-tablets: $0", s.ToString())), resp);
  }
  TRACE("Wrote tablets to system table");

  // Update the on-disk table state to "running".
  this_table_info->AddTablets(tablets);
  this_table_info->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
  s = sys_catalog_->AddItem(this_table_info.get(), leader_ready_term_);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(this_table_info.get(), tablets, s.CloneAndPrepend(
        Substitute("An error occurred while inserting to sys-tablets: $0",
                   s.ToString())), resp);
  }
  TRACE("Wrote table to system table");

  // Commit the in-memory state.
  this_table_info->mutable_metadata()->CommitMutation();

  for (TabletInfo *tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
  }

  for (const auto& tablet : scoped_ref_tablets) {
    SendCopartitionTabletRequest(tablet, this_table_info);
  }

  LOG(INFO) << "Successfully created " << object_type << " " << this_table_info->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

namespace {

CHECKED_STATUS ValidateCreateTableSchema(const Schema& schema, CreateTableResponsePB* resp) {
  if (schema.has_column_ids()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS(InvalidArgument, "User requests should not have Column IDs"));
  }
  if (schema.num_key_columns() <= 0) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS(InvalidArgument, "Must specify at least one key column"));
  }
  for (int i = 0; i < schema.num_key_columns(); i++) {
    if (!IsTypeAllowableInKey(schema.column(i).type_info())) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                        STATUS(InvalidArgument, "Invalid datatype for primary key column"));
    }
  }
  return Status::OK();
}

} // namespace

Status CatalogManager::CreatePgsqlSysTable(const CreateTableRequestPB* req,
                                           CreateTableResponsePB* resp,
                                           rpc::RpcContext* rpc) {
  // Lookup the namespace and verify if it exists.
  TRACE("Looking up namespace");
  scoped_refptr<NamespaceInfo> ns;
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);
  NamespaceId namespace_id = ns->id();

  Schema schema;
  Schema client_schema;
  RETURN_NOT_OK(SchemaFromPB(req->schema(), &client_schema));
  // If the schema contains column ids, we are copying a Postgres table from one namespace to
  // another. In that case, just use the schema as-is. Otherwise, validate the schema.
  if (client_schema.has_column_ids()) {
    schema = std::move(client_schema);
  } else {
    RETURN_NOT_OK(ValidateCreateTableSchema(client_schema, resp));
    schema = client_schema.CopyWithColumnIds();
  }

  // Verify no hash paritition schema is specified.
  if (req->partition_schema().has_hash_schema()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS(InvalidArgument,
                             "PostreSQL system catalog tables are non-partitioned"));
  }

  // Create partition schema and one partition.
  PartitionSchema partition_schema;
  vector<Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));

  // Create table info in memory.
  scoped_refptr<TableInfo> table;
  vector<TabletInfo*> tablets;
  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // Verify that the table does not exist.
    table = FindPtrOrNull(table_names_map_, {namespace_id, req->name()});
    if (table != nullptr) {
      return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT,
                        STATUS(AlreadyPresent, "Target table already exists"));
    }

    RETURN_NOT_OK(CreateTableInMemory(*req, schema, partition_schema, false /* create_tablets */,
                                      namespace_id, partitions, nullptr /* index_info */,
                                      nullptr /* tablets */, resp, &table));

    scoped_refptr<TabletInfo> tablet = tablet_map_[kSysCatalogTabletId];
    auto tablet_lock = tablet->LockForWrite();
    tablet_lock->mutable_data()->pb.add_table_ids(table->id());
    table->AddTablet(tablet.get());

    TabletMetadata* metadata = sys_catalog_->tablet_peer_->tablet()->metadata();
    metadata->AddTable(table->id(),
                       req->name(),
                       PGSQL_TABLE_TYPE,
                       schema,
                       IndexMap(),
                       partition_schema,
                       partitions[0],
                       boost::none,
                       0 /* schema_version */);
    RETURN_NOT_OK(metadata->Flush());

    RETURN_NOT_OK(sys_catalog_->UpdateItem(tablet.get(), leader_ready_term_));
    tablet_lock->Commit();
  }
  TRACE("Inserted new table info into CatalogManager maps");


  // Update the on-disk table state to "running".
  table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
  Status s = sys_catalog_->AddItem(table.get(), leader_ready_term_);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(table.get(), tablets,
                              s.CloneAndPrepend(
                                  Substitute("An error occurred while inserting to sys-tablets: $0",
                                             s.ToString())),
                              resp);
  }
  TRACE("Wrote table to system table");

  // Commit the in-memory state.
  table->mutable_metadata()->CommitMutation();

  LOG(INFO) << "Successfully created table " << table->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::ReservePgsqlOids(const ReservePgsqlOidsRequestPB* req,
                                        ReservePgsqlOidsResponsePB* resp,
                                        rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());

  VLOG(1) << "ReservePgsqlOids request: " << req->ShortDebugString();

  // Lookup namespace
  scoped_refptr<NamespaceInfo> ns;
  {
    boost::shared_lock<LockType> l(lock_);
    ns = FindPtrOrNull(namespace_ids_map_, req->namespace_id());
  }
  if (!ns) {
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                      STATUS(NotFound, "Namespace not found", req->namespace_id()));
  }

  // Reserve oids.
  auto l = ns->LockForWrite();

  uint32_t begin_oid = l->data().pb.next_pg_oid();
  if (begin_oid < req->next_oid()) {
    begin_oid = req->next_oid();
  }
  if (begin_oid == std::numeric_limits<uint32_t>::max()) {
    LOG(WARNING) << Format("No more object identifier is available for Postgres database $0 ($1)",
                           l->data().pb.name(), req->namespace_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR,
                      STATUS(InvalidArgument, "No more object identifier is available"));
  }

  uint32_t end_oid = begin_oid + req->count();
  if (end_oid < begin_oid) {
    end_oid = std::numeric_limits<uint32_t>::max(); // Handle wraparound.
  }

  resp->set_begin_oid(begin_oid);
  resp->set_end_oid(end_oid);
  l->mutable_data()->pb.set_next_pg_oid(end_oid);

  // Update the on-disk state.
  const Status s = sys_catalog_->UpdateItem(ns.get(), leader_ready_term_);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
  }

  // Commit the in-memory state.
  l->Commit();

  VLOG(1) << "ReservePgsqlOids response: " << resp->ShortDebugString();

  return Status::OK();
}

Status CatalogManager::CopyPgsqlSysTables(const NamespaceId& namespace_id,
                                          const std::vector<scoped_refptr<TableInfo>>& tables,
                                          CreateNamespaceResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  const uint32_t database_oid = CHECK_RESULT(GetPgsqlDatabaseOid(namespace_id));
  for (const auto& table : tables) {
    CreateTableRequestPB table_req;
    CreateTableResponsePB table_resp;

    const uint32_t table_oid = VERIFY_RESULT(GetPgsqlTableOid(table->id()));
    const TableId table_id = GetPgsqlTableId(database_oid, table_oid);

    // Hold read lock until rows from the table are copied also.
    auto l = table->LockForRead();

    // Skip shared table.
    if (l->data().pb.is_pg_shared_table()) {
      continue;
    }

    table_req.set_name(l->data().pb.name());
    table_req.mutable_namespace_()->set_id(namespace_id);
    table_req.set_table_type(PGSQL_TABLE_TYPE);
    table_req.mutable_schema()->CopyFrom(l->data().schema());
    table_req.set_is_pg_catalog_table(true);
    table_req.set_table_id(table_id);

    const Status s = CreatePgsqlSysTable(&table_req, &table_resp, rpc);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), table_resp.error().code(), s);
    }

    RETURN_NOT_OK(sys_catalog_->CopyPgsqlTable(table->id(), table_id, leader_ready_term_));
  }
  return Status::OK();
}

// Create a new table.
// See README file in this directory for a description of the design.
Status CatalogManager::CreateTable(const CreateTableRequestPB* orig_req,
                                   CreateTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());

  const bool is_pg_catalog_table =
      orig_req->table_type() == PGSQL_TABLE_TYPE && orig_req->is_pg_catalog_table();
  if (!is_pg_catalog_table || !FLAGS_hide_pg_catalog_table_creation_logs) {
    LOG(INFO) << "CreateTable from " << RequestorString(rpc)
                << ":\n" << orig_req->DebugString();
  } else {
    LOG(INFO) << "CreateTable from " << RequestorString(rpc) << ": " << orig_req->name();
  }

  if (is_pg_catalog_table) {
    return CreatePgsqlSysTable(orig_req, resp, rpc);
  }

  Status s;

  const char* const object_type = orig_req->indexed_table_id().empty() ? "table" : "index";

  // Copy the request, so we can fill in some defaults.
  CreateTableRequestPB req = *orig_req;

  // Lookup the namespace and verify if it exists.
  TRACE("Looking up namespace");
  scoped_refptr<NamespaceInfo> ns;
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req.namespace_(), &ns), resp);
  NamespaceId namespace_id = ns->id();

  // Validate schema.
  Schema client_schema;
  RETURN_NOT_OK(SchemaFromPB(req.schema(), &client_schema));
  RETURN_NOT_OK(ValidateCreateTableSchema(client_schema, resp));

  // checking that referenced user-defined types (if any) exist.
  {
    boost::shared_lock<LockType> l(lock_);
    for (int i = 0; i < client_schema.num_columns(); i++) {
      for (const auto &udt_id : client_schema.column(i).type()->GetUserDefinedTypeIds()) {
        if (FindPtrOrNull(udtype_ids_map_, udt_id) == nullptr) {
          Status s = STATUS(InvalidArgument, "Referenced user-defined type not found");
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
        }
      }
    }
  }
  // TODO (ENG-1860) The referenced namespace and types retrieved/checked above could be deleted
  // some time between this point and table creation below.
  Schema schema = client_schema.CopyWithColumnIds();

  if (schema.table_properties().HasCopartitionTableId()) {
    return CreateCopartitionedTable(req, resp, rpc, schema, namespace_id);
  }

  // If hashing scheme is not specified by protobuf request, table_type and hash_key are used to
  // determine which hashing scheme should be used.
  if (!req.partition_schema().has_hash_schema()) {
    if (req.table_type() == REDIS_TABLE_TYPE) {
      req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::REDIS_HASH_SCHEMA);
    } else if (schema.num_hash_key_columns() > 0) {
      req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    } else {
      Status s = STATUS(InvalidArgument, "Unknown table type or partitioning method");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    }
  }

  // Get cluster level placement info.
  ReplicationInfoPB replication_info;
  {
    auto l = cluster_config_->LockForRead();
    replication_info = l->data().pb.replication_info();
  }
  // Calculate number of tablets to be used.
  int num_tablets = req.num_tablets();
  if (num_tablets <= 0) {
    // Use default as client could have gotten the value before any tserver had heartbeated
    // to (a new) master leader.
    TSDescriptorVector ts_descs;
    master_->ts_manager()->GetAllLiveDescriptorsInCluster(
        &ts_descs, replication_info.live_replicas().placement_uuid(), blacklistState.tservers_);
    num_tablets = ts_descs.size() * FLAGS_yb_num_shards_per_tserver;
    LOG(INFO) << "Setting default tablets to " << num_tablets << " with "
              << ts_descs.size() << " primary servers";
  }

  // Create partitions.
  PartitionSchema partition_schema;
  vector<Partition> partitions;
  s = PartitionSchema::FromPB(req.partition_schema(), schema, &partition_schema);
  switch (partition_schema.hash_schema()) {
    case YBHashSchema::kPgsqlHash:
      // TODO(neil) After a discussion, PGSQL hash should be done appropriately.
      // For now, let's not doing anything. Just borrow the multi column hash.
      FALLTHROUGH_INTENDED;
    case YBHashSchema::kMultiColumnHash: {
      // Use the given number of tablets to create partitions and ignore the other schema options
      // in the request.
      RETURN_NOT_OK(partition_schema.CreatePartitions(num_tablets, &partitions));
      break;
    }
    case YBHashSchema::kRedisHash: {
      RETURN_NOT_OK(partition_schema.CreatePartitions(num_tablets, &partitions,
                                                      kRedisClusterSlots));
      break;
    }
  }

  // Validate the table placement rules are a subset of the cluster ones.
  s = ValidateTableReplicationInfo(req.replication_info());
  if (PREDICT_FALSE(!s.ok())) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  // For index table, populate the index info.
  scoped_refptr<TableInfo> indexed_table;
  IndexInfoPB index_info;
  const bool create_index_info = (orig_req->table_type() != PGSQL_TABLE_TYPE);
  if (req.has_indexed_table_id()) {
    TRACE("Looking up indexed table");
    indexed_table = GetTableInfo(req.indexed_table_id());
    if (indexed_table == nullptr) {
      return STATUS(NotFound, "The indexed table does not exist");
    }
    if (create_index_info) {
      Schema indexed_schema;
      RETURN_NOT_OK(indexed_table->GetSchema(&indexed_schema));

      RETURN_NOT_OK(CreateIndexInfo(req.indexed_table_id(),
                                    indexed_schema,
                                    schema,
                                    req.is_local_index(),
                                    req.is_unique_index(),
                                    &index_info));
    }
  }

  TSDescriptorVector all_ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&all_ts_descs);
  s = CheckValidReplicationInfo(replication_info, all_ts_descs, partitions, resp);
  if (!s.ok()) {
    return s;
  }

  scoped_refptr<TableInfo> table;
  vector<TabletInfo*> tablets;
  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // Verify that the table does not exist.
    table = FindPtrOrNull(table_names_map_, {namespace_id, req.name()});

    if (table != nullptr) {
      s = STATUS(AlreadyPresent, Substitute("Target $0 already exists", object_type), table->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT, s);
    }

    RETURN_NOT_OK(CreateTableInMemory(req, schema, partition_schema, true /* create_tablets */,
                                      namespace_id, partitions,
                                      create_index_info ? &index_info : nullptr,
                                      &tablets, resp, &table));
  }
  TRACE("Inserted new table and tablet info into CatalogManager maps");

  // NOTE: the table and tablets are already locked for write at this point,
  // since the CreateTableInfo/CreateTabletInfo functions leave them in that state.
  // They will get committed at the end of this function.
  // Sanity check: the tables and tablets should all be in "preparing" state.
  CHECK_EQ(SysTablesEntryPB::PREPARING, table->metadata().dirty().pb.state());
  for (const TabletInfo *tablet : tablets) {
    CHECK_EQ(SysTabletsEntryPB::PREPARING, tablet->metadata().dirty().pb.state());
  }

  // Write Tablets to sys-tablets (in "preparing" state).
  s = sys_catalog_->AddItems(tablets, leader_ready_term_);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(table.get(), tablets,
                              s.CloneAndPrepend(
                                  Substitute("An error occurred while inserting to sys-tablets: $0",
                                             s.ToString())),
                              resp);
  }
  TRACE("Wrote tablets to system table");

  // Update the on-disk table state to "running".
  table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
  s = sys_catalog_->AddItem(table.get(), leader_ready_term_);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(table.get(), tablets,
                              s.CloneAndPrepend(
                                  Substitute("An error occurred while inserting to sys-tablets: $0",
                                             s.ToString())),
                              resp);
  }
  TRACE("Wrote table to system table");

  // For index table, insert index info in the indexed table.
  if (req.has_indexed_table_id() && create_index_info) {
    s = AddIndexInfoToTable(indexed_table, index_info);
    if (PREDICT_FALSE(!s.ok())) {
      return AbortTableCreation(table.get(), tablets,
                                s.CloneAndPrepend(
                                    Substitute("An error occurred while inserting index info: $0",
                                               s.ToString())),
                                resp);
    }
  }

  // Commit the in-memory state.
  table->mutable_metadata()->CommitMutation();

  for (TabletInfo *tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
  }

  if (req.has_creator_role_name()) {
    const NamespaceName& keyspace_name = req.namespace_().name();
    const TableName& table_name = req.name();
    RETURN_NOT_OK(GrantPermissions(req.creator_role_name(),
                                   get_canonical_table(keyspace_name, table_name),
                                   table_name,
                                   keyspace_name,
                                   all_permissions_for_resource(ResourceType::TABLE),
                                   ResourceType::TABLE,
                                   resp));
  }

  LOG(INFO) << "Successfully created " << object_type << " " << table->ToString()
            << " per request from " << RequestorString(rpc);
  background_tasks_->Wake();

  // If this is a transactional table, we need to create the transaction status table (if it does
  // not exist already).
  if (req.schema().table_properties().is_transactional()) {
    Status s = CreateTransactionsStatusTableIfNeeded(rpc);
    if (!s.ok()) {
      return s.CloneAndPrepend("Error while creating transaction status table");
    }
  }
  return Status::OK();
}

Status CatalogManager::CreateTabletsFromTable(const vector<Partition>& partitions,
                                              const scoped_refptr<TableInfo>& table,
                                              std::vector<TabletInfo*>* tablets) {
  // Create the TabletInfo objects in state PREPARING.
  for (const Partition& partition : partitions) {
    PartitionPB partition_pb;
    partition.ToPB(&partition_pb);
    tablets->push_back(CreateTabletInfo(table.get(), partition_pb));
  }

  // Add the table/tablets to the in-memory map for the assignment.
  table->AddTablets(*tablets);
  for (TabletInfo* tablet : *tablets) {
    InsertOrDie(&tablet_map_, tablet->tablet_id(), tablet);
  }
  return Status::OK();
}

int CatalogManager::GetNumReplicasFromPlacementInfo(const PlacementInfoPB& placement_info) {
  return placement_info.num_replicas() > 0 ?
      placement_info.num_replicas() : FLAGS_replication_factor;
}

Status CatalogManager::CheckValidReplicationInfo(const ReplicationInfoPB& replication_info,
                                                 const TSDescriptorVector& all_ts_descs,
                                                 const vector<Partition>& partitions,
                                                 CreateTableResponsePB* resp) {
  return CheckValidPlacementInfo(replication_info.live_replicas(), all_ts_descs, partitions, resp);
}

Status CatalogManager::CheckValidPlacementInfo(const PlacementInfoPB& placement_info,
                                               const TSDescriptorVector& ts_descs,
                                               const vector<Partition>& partitions,
                                               CreateTableResponsePB* resp) {
  // Verify that the total number of tablets is reasonable, relative to the number
  // of live tablet servers.
  int num_live_tservers = ts_descs.size();
  int num_replicas = GetNumReplicasFromPlacementInfo(placement_info);
  int max_tablets = FLAGS_max_create_tablets_per_ts * num_live_tservers;
  Status s;
  string msg;
  if (num_replicas > 1 && max_tablets > 0 && partitions.size() > max_tablets) {
    msg = Substitute("The requested number of tablets ($0) is over the permitted maximum ($1)",
                     partitions.size(), max_tablets);
    s = STATUS(InvalidArgument, msg);
    LOG(WARNING) << msg;
    RETURN_NOT_OK(SetupError(resp->mutable_error(), MasterErrorPB::TOO_MANY_TABLETS, s));
    return s;
  }

  // Verify that the number of replicas isn't larger than the number of live tablet
  // servers.
  if (FLAGS_catalog_manager_check_ts_count_for_create_table &&
      num_replicas > num_live_tservers) {
    msg = Substitute("Not enough live tablet servers to create table with replication factor $0. "
                     "$1 tablet servers are alive.", num_replicas, num_live_tservers);
    LOG(WARNING) << msg;
    s = STATUS(InvalidArgument, msg);
    RETURN_NOT_OK(SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s));
    return s;
  }

  // Verify that placement requests are reasonable and we can satisfy the minimums.
  if (!placement_info.placement_blocks().empty()) {
    int minimum_sum = 0;
    for (const auto& pb : placement_info.placement_blocks()) {
      minimum_sum += pb.min_num_replicas();
      if (!pb.has_cloud_info()) {
        msg = Substitute("Got placement info without cloud info set: $0", pb.ShortDebugString());
        s = STATUS(InvalidArgument, msg);
        LOG(WARNING) << msg;
        RETURN_NOT_OK(SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s));
        return s;
      }
    }

    if (minimum_sum > num_replicas) {
      msg = Substitute("Sum of minimum replicas per placement ($0) is greater than num_replicas "
                       " ($1)", minimum_sum, num_replicas);
      s = STATUS(InvalidArgument, msg);
      LOG(WARNING) << msg;
      RETURN_NOT_OK(SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s));
      return s;
    }
  }
  return Status::OK();
}

Status CatalogManager::CreateTableInMemory(const CreateTableRequestPB& req,
                                           const Schema& schema,
                                           const PartitionSchema& partition_schema,
                                           const bool create_tablets,
                                           const NamespaceId& namespace_id,
                                           const std::vector<Partition>& partitions,
                                           IndexInfoPB* index_info,
                                           std::vector<TabletInfo*>* tablets,
                                           CreateTableResponsePB* resp,
                                           scoped_refptr<TableInfo>* table) {
  // Verify we have catalog manager lock.
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  // Add the new table in "preparing" state.
  table->reset(CreateTableInfo(req, schema, partition_schema, namespace_id, index_info));
  table_ids_map_[(*table)->id()] = *table;
  // Do not add Postgres tables to the name map as the table name is not unique in a namespace.
  if (req.table_type() != PGSQL_TABLE_TYPE) {
    table_names_map_[{namespace_id, req.name()}] = *table;
  }

  if (create_tablets) {
    RETURN_NOT_OK(CreateTabletsFromTable(partitions, *table, tablets));
  }

  if (resp != nullptr) {
    resp->set_table_id((*table)->id());
  }

  return Status::OK();
}

Status CatalogManager::CreateTransactionsStatusTableIfNeeded(rpc::RpcContext *rpc) {
  TableIdentifierPB table_indentifier;
  table_indentifier.set_table_name(kTransactionsTableName);
  table_indentifier.mutable_namespace_()->set_name(kSystemNamespaceName);

  // Check that the namespace exists.
  scoped_refptr<NamespaceInfo> ns_info;
  RETURN_NOT_OK(FindNamespace(table_indentifier.namespace_(), &ns_info));
  if (!ns_info) {
    return STATUS(NotFound, "Namespace does not exist", kSystemNamespaceName);
  }

  // If status table exists do nothing, otherwise create it.
  scoped_refptr<TableInfo> table_info;
  RETURN_NOT_OK(FindTable(table_indentifier, &table_info));

  if (!table_info) {
    // Set up a CreateTable request internally.
    CreateTableRequestPB req;
    CreateTableResponsePB resp;
    req.set_name(kTransactionsTableName);
    req.mutable_namespace_()->set_name(kSystemNamespaceName);
    req.set_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);

    // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
    // will use the same defaults as for regular tables.
    if (FLAGS_transaction_table_num_tablets > 0) {
      req.set_num_tablets(FLAGS_transaction_table_num_tablets);
    }

    ColumnSchema hash(kRedisKeyColumnName, BINARY, /* is_nullable */ false, /* is_hash_key */ true);
    ColumnSchemaToPB(hash, req.mutable_schema()->mutable_columns()->Add());

    Status s = CreateTable(&req, &resp, rpc);
    // We do not lock here so it is technically possible that the table was already created.
    // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
    if (!s.ok() && !s.IsAlreadyPresent()) {
      return s;
    }
  }
  return Status::OK();
}

Status CatalogManager::IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                         IsCreateTableDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists.
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The table was deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  // 2. Verify if the create is in-progress.
  TRACE("Verify if the table creation is in progress for $0", table->ToString());
  resp->set_done(!table->IsCreateInProgress());

  // 3. Set any current errors, if we are experiencing issues creating the table. This will be
  // bubbled up to the MasterService layer. If it is an error, it gets wrapped around in
  // MasterErrorPB::UNKNOWN_ERROR.
  RETURN_NOT_OK(table->GetCreateTableErrorStatus());

  // 4. For index table, check if alter schema is done on the indexed table also.
  if (resp->done() && !l->data().pb.indexed_table_id().empty()) {
    IsAlterTableDoneRequestPB alter_table_req;
    IsAlterTableDoneResponsePB alter_table_resp;
    alter_table_req.mutable_table()->set_table_id(l->data().pb.indexed_table_id());
    const Status s = IsAlterTableDone(&alter_table_req, &alter_table_resp);
    if (!s.ok()) {
      resp->mutable_error()->Swap(alter_table_resp.mutable_error());
      return s;
    }
    resp->set_done(alter_table_resp.done());
  }

  // If this is a transactional table we are not done until the transaction status table is created.
  if (resp->done() && l->data().pb.schema().table_properties().is_transactional()) {
    return IsTransactionStatusTableCreated(resp);
  }

  return Status::OK();
}

Status CatalogManager::IsTransactionStatusTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kTransactionsTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);

  return IsCreateTableDone(&req, resp);
}

std::string CatalogManager::GenerateId(boost::optional<const SysRowEntry::Type> entity_type) {
  while (true) {
    // Generate id and make sure it is unique within its category.
    std::string id = oid_generator_.Next();
    if (!entity_type) {
      return id;
    }
    switch (*entity_type) {
      case SysRowEntry::NAMESPACE:
        if (FindPtrOrNull(namespace_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::TABLE:
        if (FindPtrOrNull(table_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::TABLET:
        if (FindPtrOrNull(tablet_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::UDTYPE:
        if (FindPtrOrNull(udtype_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::SNAPSHOT:
        return id;
      case SysRowEntry::UNKNOWN: FALLTHROUGH_INTENDED;
      case SysRowEntry::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntry::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::SYS_CONFIG:
        LOG(DFATAL) << "Invalid id type: " << *entity_type;
        return id;
    }
  }
}

TableInfo *CatalogManager::CreateTableInfo(const CreateTableRequestPB& req,
                                           const Schema& schema,
                                           const PartitionSchema& partition_schema,
                                           const NamespaceId& namespace_id,
                                           IndexInfoPB* index_info) {
  DCHECK(schema.has_column_ids());
  TableId table_id = !req.table_id().empty() ? req.table_id() : GenerateId(SysRowEntry::TABLE);
  TableInfo* table = new TableInfo(table_id);
  table->mutable_metadata()->StartMutation();
  SysTablesEntryPB *metadata = &table->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTablesEntryPB::PREPARING);
  metadata->set_name(req.name());
  metadata->set_table_type(req.table_type());
  metadata->set_namespace_id(namespace_id);
  metadata->set_version(0);
  metadata->set_next_column_id(ColumnId(schema.max_col_id() + 1));
  // TODO(bogdan): add back in replication_info once we allow overrides!
  // Use the Schema object passed in, since it has the column IDs already assigned,
  // whereas the user request PB does not.
  CHECK_OK(SchemaToPB(schema, metadata->mutable_schema()));
  partition_schema.ToPB(metadata->mutable_partition_schema());
  // For index table, set index details (indexed table id and whether the index is local).
  if (req.has_indexed_table_id()) {
    metadata->set_indexed_table_id(req.indexed_table_id());
    metadata->set_is_local_index(req.is_local_index());
    metadata->set_is_unique_index(req.is_unique_index());
    if (index_info != nullptr) {
      index_info->set_table_id(table->id());
      metadata->mutable_index_info()->CopyFrom(*index_info);
    }
  }

  if (req.is_pg_shared_table()) {
    metadata->set_is_pg_shared_table(true);
  }

  return table;
}

TabletInfo* CatalogManager::CreateTabletInfo(TableInfo* table,
                                             const PartitionPB& partition) {
  TabletInfo* tablet = new TabletInfo(table, GenerateId(SysRowEntry::TABLET));
  tablet->mutable_metadata()->StartMutation();
  SysTabletsEntryPB *metadata = &tablet->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTabletsEntryPB::PREPARING);
  metadata->mutable_partition()->CopyFrom(partition);
  metadata->set_table_id(table->id());
  // This is important: we are setting the first table id in the table_ids list
  // to be the id of the original table that creates the tablet.
  metadata->add_table_ids(table->id());
  return tablet;
}

Status CatalogManager::FindTable(const TableIdentifierPB& table_identifier,
                                 scoped_refptr<TableInfo> *table_info) {
  boost::shared_lock<LockType> l(lock_);

  if (table_identifier.has_table_id()) {
    *table_info = FindPtrOrNull(table_ids_map_, table_identifier.table_id());
  } else if (table_identifier.has_table_name()) {
    NamespaceId namespace_id;

    if (table_identifier.has_namespace_()) {
      if (table_identifier.namespace_().has_id()) {
        namespace_id = table_identifier.namespace_().id();
      } else if (table_identifier.namespace_().has_name()) {
        // Find namespace by its name.
        scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_names_map_,
            table_identifier.namespace_().name());

        if (ns == nullptr) {
          // The namespace was not found. This is a correct case. Just return NULL.
          *table_info = nullptr;
          return Status::OK();
        }

        namespace_id = ns->id();
      } else {
        return STATUS(InvalidArgument, "Neither keyspace id or keyspace name are specified");
      }
    }

    if (namespace_id.empty()) {
      return STATUS(InvalidArgument, "No namespace used");
    }

    *table_info = FindPtrOrNull(table_names_map_, {namespace_id, table_identifier.table_name()});
  } else {
    return STATUS(InvalidArgument, "Neither table id or table name are specified");
  }
  return Status::OK();
}

Status CatalogManager::FindNamespace(const NamespaceIdentifierPB& ns_identifier,
                                     scoped_refptr<NamespaceInfo>* ns_info) const {
  boost::shared_lock<LockType> l(lock_);

  if (ns_identifier.has_id()) {
    *ns_info = FindPtrOrNull(namespace_ids_map_, ns_identifier.id());
    if (*ns_info == nullptr) {
      return STATUS(NotFound, "Keyspace identifier not found", ns_identifier.id());
    }
  } else if (ns_identifier.has_name()) {
    *ns_info = FindPtrOrNull(namespace_names_map_, ns_identifier.name());
    if (*ns_info == nullptr) {
      return STATUS(NotFound, "Keyspace name not found", ns_identifier.name());
    }
  } else {
    return STATUS(NotFound, "Neither keyspace id nor keyspace name is specified.");
  }
  return Status::OK();
}

Result<TabletInfos> CatalogManager::GetTabletsOrSetupError(
    const TableIdentifierPB& table_identifier,
    MasterErrorPB::Code* error,
    scoped_refptr<TableInfo>* table,
    scoped_refptr<NamespaceInfo>* ns) {
  DCHECK_ONLY_NOTNULL(error);
  // Lookup the table and verify it exists.
  TRACE("Looking up table");
  scoped_refptr<TableInfo> local_table_info;
  scoped_refptr<TableInfo>& table_obj = table ? *table : local_table_info;
  RETURN_NOT_OK(FindTable(table_identifier, &table_obj));
  if (table_obj == nullptr) {
    *error = MasterErrorPB::TABLE_NOT_FOUND;
    return STATUS(NotFound, "Table does not exist", table_identifier.DebugString());
  }

  TRACE("Locking table");
  auto l = table_obj->LockForRead();

  if (table_obj->IsCreateInProgress()) {
    *error = MasterErrorPB::TABLE_CREATION_IS_IN_PROGRESS;
    return STATUS(IllegalState, "Table creation is in progress", table_obj->ToString());
  }

  if (ns) {
    TRACE("Looking up namespace");
    boost::shared_lock<LockType> l(lock_);

    *ns = FindPtrOrNull(namespace_ids_map_, table_obj->namespace_id());
    if (*ns == nullptr) {
      *error = MasterErrorPB::NAMESPACE_NOT_FOUND;
      return STATUS(
          InvalidArgument, "Could not find namespace by namespace id", table_obj->namespace_id());
    }
  }

  TabletInfos tablets;
  table_obj->GetAllTablets(&tablets);
  return std::move(tablets);
}

// Truncate a Table.
Status CatalogManager::TruncateTable(const TruncateTableRequestPB* req,
                                     TruncateTableResponsePB* resp,
                                     rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing TruncateTable request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  for (int i = 0; i < req->table_ids_size(); i++) {
    RETURN_NOT_OK(TruncateTable(req->table_ids(i), false /* is_index */, resp, rpc));
  }

  return Status::OK();
}

Status CatalogManager::TruncateTable(const TableId& table_id,
                                     const bool is_index,
                                     TruncateTableResponsePB* resp,
                                     rpc::RpcContext* rpc) {
  const char* table_type = is_index ? "index" : "table";

  // Lookup the table and verify if it exists.
  TRACE(Substitute("Looking up $0", table_type));
  scoped_refptr<TableInfo> table = FindPtrOrNull(table_ids_map_, table_id);
  if (table == nullptr) {
    Status s = STATUS(NotFound, Substitute("The table for $0 id $1 does not exist",
                                           table_type, table_id));
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  TRACE(Substitute("Locking $0", table_type));
  auto l = table->LockForRead();
  DCHECK(is_index == !l->data().pb.indexed_table_id().empty());
  if (l->data().started_deleting() || l->data().is_deleted()) {
    Status s = STATUS(NotFound, Substitute("The $0 $1 does not exist",
                                           table_type, table->ToString()));
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  // Send a Truncate() request to each tablet in the table.
  SendTruncateTableRequest(table);

  LOG(INFO) << "Successfully initiated TRUNCATE for " << table->ToString() << " per request from "
            << RequestorString(rpc);
  background_tasks_->Wake();

  // Truncate indexes also.
  DCHECK(!is_index || l->data().pb.indexes().empty()) << "indexes should be empty for index table";
  for (const auto& index_info : l->data().pb.indexes()) {
    RETURN_NOT_OK(TruncateTable(index_info.table_id(), true /* is_index */, resp, rpc));
  }

  return Status::OK();
}

void CatalogManager::SendTruncateTableRequest(const scoped_refptr<TableInfo>& table) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    SendTruncateTabletRequest(tablet);
  }
}

void CatalogManager::SendTruncateTabletRequest(const scoped_refptr<TabletInfo>& tablet) {
  LOG_WITH_PREFIX(INFO) << Substitute("Truncating tablet $0", tablet->id());
  auto call = std::make_shared<AsyncTruncate>(master_, worker_pool_.get(), tablet);
  tablet->table()->AddTask(call);
  auto status = call->Run();
  WARN_NOT_OK(status, Substitute("Failed to send truncate request for tablet $0", tablet->id()));
}

Status CatalogManager::IsTruncateTableDone(const IsTruncateTableDoneRequestPB* req,
                                           IsTruncateTableDoneResponsePB* resp) {
  LOG(INFO) << "Servicing IsTruncateTableDone request for table id " << req->table_id();
  RETURN_NOT_OK(CheckOnline());

  // Lookup the truncated table.
  TRACE("Looking up table $0", req->table_id());
  std::lock_guard<LockType> l_map(lock_);
  scoped_refptr<TableInfo> table = FindPtrOrNull(table_ids_map_, req->table_id());

  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  if (l->data().started_deleting() || l->data().is_deleted()) {
    Status s = STATUS(NotFound, "The table does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  resp->set_done(!table->HasTasks(MonitoredTask::Type::ASYNC_TRUNCATE_TABLET));
  return Status::OK();
}

Status CatalogManager::DeleteIndexInfoFromTable(const TableId& indexed_table_id,
                                                const TableId& index_table_id,
                                                DeleteTableResponsePB* resp) {
  // Lookup the indexed table and verify if it exists.
  TRACE("Looking up indexed table");
  scoped_refptr<TableInfo> indexed_table = GetTableInfo(indexed_table_id);
  if (indexed_table == nullptr) {
    LOG(WARNING) << Substitute("Indexed table $0 for index $1 not found",
                               indexed_table_id, index_table_id);
    return Status::OK();
  }

  NamespaceIdentifierPB nsId;
  nsId.set_id(indexed_table->namespace_id());
  scoped_refptr<NamespaceInfo> nsInfo;
  RETURN_NOT_OK(FindNamespace(nsId, &nsInfo));
  auto *indexed_table_name = resp->mutable_indexed_table();
  indexed_table_name->mutable_namespace_()->set_name(nsInfo->name());
  indexed_table_name->set_table_name(indexed_table->name());

  TRACE("Locking indexed table");
  auto l = indexed_table->LockForWrite();

  auto *indexes = l->mutable_data()->pb.mutable_indexes();
  for (int i = 0; i < indexes->size(); i++) {
    if (indexes->Get(i).table_id() == index_table_id) {

      indexes->DeleteSubrange(i, 1);

      // Update sys-catalog with the deleted indexed table info.
      TRACE("Updating indexed table metadata on disk");
      RETURN_NOT_OK(sys_catalog_->UpdateItem(indexed_table.get(), leader_ready_term_));

      // Update the in-memory state.
      TRACE("Committing in-memory state");
      l->Commit();
      return Status::OK();
    }
  }

  LOG(WARNING) << Substitute("Index $0 not found in indexed table $1",
                             index_table_id, indexed_table_id);
  return Status::OK();
}

namespace {

// Helper class to abort mutations at the end of a scope.
template<class PersistentDataEntryPB>
class ScopedMutation {
 public:
  explicit ScopedMutation(PersistentDataEntryPB* cow_object)
      : cow_object_(DCHECK_NOTNULL(cow_object)) {
    cow_object->mutable_metadata()->StartMutation();
  }

  void Commit() {
    cow_object_->mutable_metadata()->CommitMutation();
    committed_ = true;
  }

  // Abort the mutation if it wasn't committed.
  ~ScopedMutation() {
    if (PREDICT_FALSE(!committed_)) {
      cow_object_->mutable_metadata()->AbortMutation();
    }
  }

 private:
  PersistentDataEntryPB* cow_object_;
  bool committed_ = false;
};
}  // anonymous namespace

// Delete a Table
//  - Update the table state to "removed"
//  - Write the updated table metadata to sys-table
//
// We are lazy about deletions... The cleaner will remove tables and tablets marked as "removed".
Status CatalogManager::DeleteTable(const DeleteTableRequestPB* req,
                                   DeleteTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteTable request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  vector<scoped_refptr<TableInfo>> tables;
  vector<scoped_refptr<DeletedTableInfo>> deleted_tables;
  vector<unique_ptr<TableInfo::lock_type>> table_locks;

  RETURN_NOT_OK(DeleteTableInMemory(req->table(), req->is_index_table(),
                                    true /* update_indexed_table */,
                                    &tables, &deleted_tables, &table_locks, resp, rpc));

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  for (int i = 0; i < table_locks.size(); i++) {
    table_locks[i]->Commit();
  }

  // The table lock (l) and the global lock (lock_) must be released for the next call.
  for (int i = 0; i < deleted_tables.size(); i++) {
    MarkTableDeletedIfNoTablets(deleted_tables[i], tables[i].get());

    // Send a DeleteTablet() request to each tablet replica in the table.
    DeleteTabletsAndSendRequests(tables[i]);
  }

  // If there are any permissions granted on this table find them and delete them. This is necessary
  // because we keep track of the permissions based on the canonical resource name which is a
  // combination of the keyspace and table names, so if another table with the same name is created
  // (in the same keyspace where the previous one existed), and the permissions were not deleted at
  // the time of the previous table deletion, then the permissions that existed for the previous
  // table will automatically be granted to the new table even though this wasn't the intention.
  string canonical_resource = get_canonical_table(req->table().namespace_().name(),
                                                  req->table().table_name());
  RETURN_NOT_OK(RemoveAllPermissionsForResource(canonical_resource, resp));

  LOG(INFO) << Substitute("Successfully initiated deletion of $0 ",
                          req->is_index_table() ? "index" : "table") << " with "
            << req->table().DebugString() << " per request from " << RequestorString(rpc);
  background_tasks_->Wake();
  return Status::OK();
}

Status CatalogManager::DeleteTableInMemory(const TableIdentifierPB& table_identifier,
                                           const bool is_index_table,
                                           const bool update_indexed_table,
                                           vector<scoped_refptr<TableInfo>>* tables,
                                           vector<scoped_refptr<DeletedTableInfo>>* deleted_tables,
                                           vector<unique_ptr<TableInfo::lock_type>>* table_lcks,
                                           DeleteTableResponsePB* resp,
                                           rpc::RpcContext* rpc) {
  const char* const object_type = is_index_table ? "index" : "table";
  const bool cascade_delete_index = is_index_table && !update_indexed_table;

  scoped_refptr<TableInfo> table;

  // Lookup the table and verify if it exists.
  TRACE(Substitute("Looking up $0", object_type));
  RETURN_NOT_OK(FindTable(table_identifier, &table));
  if (table == nullptr) {
    if (cascade_delete_index) {
      LOG(WARNING) << Substitute("Index $0 not found", table_identifier.DebugString());
      return Status::OK();
    } else {
      Status s = STATUS(NotFound, Substitute("The $0 does not exist", object_type),
                        table_identifier.DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    }
  }

  TRACE(Substitute("Locking $0", object_type));
  auto l = table->LockForWrite();
  resp->set_table_id(table->id());

  if (is_index_table == l->data().pb.indexed_table_id().empty()) {
    Status s = STATUS_SUBSTITUTE(NotFound, "The $0 does not exist", object_type);
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  if (l->data().started_deleting()) {
    if (cascade_delete_index) {
      LOG(WARNING) << Substitute("Index $0 was deleted", table_identifier.DebugString());
      return Status::OK();
    } else {
      Status s = STATUS(NotFound, Substitute("The $0 was deleted", object_type),
                        l->data().pb.state_msg());
      return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    }
  }

  TRACE("Updating metadata on disk");
  // Update the metadata for the on-disk state.
  l->mutable_data()->set_state(SysTablesEntryPB::DELETING,
                               Substitute("Started deleting at $0", LocalTimeAsString()));

  // Update sys-catalog with the removed table state.
  Status s = sys_catalog_->UpdateItem(table.get(), leader_ready_term_);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // For regular (indexed) table, delete all its index tables if any. Else for index table, delete
  // index info from the indexed table.
  if (!is_index_table) {
    TableIdentifierPB index_identifier;
    for (auto index : l->data().pb.indexes()) {
      index_identifier.set_table_id(index.table_id());
      RETURN_NOT_OK(DeleteTableInMemory(index_identifier, true /* is_index_table */,
                                        false /* update_index_table */, tables, deleted_tables,
                                        table_lcks, resp, rpc));
    }
  } else if (update_indexed_table) {
    s = DeleteIndexInfoFromTable(l->data().pb.indexed_table_id(), table->id(), resp);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute("An error occurred while deleting index info: $0",
                                       s.ToString()));
      LOG(WARNING) << s.ToString();
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }
  }

  table->AbortTasks();
  scoped_refptr<DeletedTableInfo> deleted_table(new DeletedTableInfo(table.get()));

  // Update the internal table maps.
  {
    // Exclude Postgres tables which are not in the name map.
    if (l->data().table_type() != PGSQL_TABLE_TYPE) {
      TRACE("Removing from by-name map");
      std::lock_guard<LockType> l_map(lock_);
      if (table_names_map_.erase({l->data().namespace_id(), l->data().name()}) != 1) {
        PANIC_RPC(rpc, "Could not remove table from map, name=" + table->ToString());
      }
    }

    TRACE("Add deleted table tablets into tablet wait list");
    deleted_table->AddTabletsToMap(&deleted_tablet_map_);
  }

  // For regular (indexed) table, insert table info and lock in the front of the list. Else for
  // index table, append them to the end. We do so so that we will commit and delete the indexed
  // table first before its indexes.
  if (!is_index_table) {
    tables->insert(tables->begin(), table);
    deleted_tables->insert(deleted_tables->begin(), deleted_table);
    table_lcks->insert(table_lcks->begin(), std::move(l));
  } else {
    tables->push_back(table);
    deleted_tables->push_back(deleted_table);
    table_lcks->push_back(std::move(l));
  }

  return Status::OK();
}

void CatalogManager::MarkTableDeletedIfNoTablets(scoped_refptr<DeletedTableInfo> deleted_table,
                                                 TableInfo* table_info) {
  DCHECK_NOTNULL(deleted_table.get());
  std::lock_guard<LockType> l_map(lock_);

  if (deleted_table->HasTablets()) {
    VLOG(1) << "The deleted table still has " << deleted_table->NumTablets()
            << " tablets, table id=" << deleted_table->id();
    return;
  }

  LOG(INFO) << "All tablets were deleted from deleted table " << deleted_table->id();
  // Try to use pointer from the arguments (may be NULL).
  scoped_refptr<TableInfo> table(table_info);

  if (table == nullptr) {
    table = FindPtrOrNull(table_ids_map_, deleted_table->id());
  }

  if (table != nullptr) {
    DCHECK_EQ(table->id(), deleted_table->id());
    auto l = table->LockForWrite();

    // If state == DELETING, then set state DELETED.
    if (l->data().pb.state() == SysTablesEntryPB::DELETING) {
      // Update the metadata for the on-disk state.
      l->mutable_data()->set_state(SysTablesEntryPB::DELETED,
          Substitute("Deleted with tablets at $0", LocalTimeAsString()));

      TRACE("Committing in-memory state");
      l->Commit();
    }
  }
}

void CatalogManager::CleanUpDeletedTables() {
  std::lock_guard<LockType> l_map(lock_);
  // Garbage collecting.
  // Going through all tables under the global lock.
  for (TableInfoMap::iterator it = table_ids_map_.begin(); it != table_ids_map_.end();) {
    scoped_refptr<TableInfo> table(it->second);

    if (!table->HasTasks()) {
      // Lock the candidate table and check the tablets under the lock.
      auto l = table->LockForRead();

      if (l->data().is_deleted()) {
        LOG(INFO) << "Removing from by-ids map table " << table->ToString();
        it = table_ids_map_.erase(it);
        // TODO: Check if we want to delete the totally deleted table from the sys_catalog here.
        continue;
      }
    }

    ++it;
  }
}

Status CatalogManager::IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                         IsDeleteTableDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  // Lookup the deleted table.
  TRACE("Looking up table $0", req->table_id());
  std::lock_guard<LockType> l_map(lock_);
  scoped_refptr<TableInfo> table = FindPtrOrNull(table_ids_map_, req->table_id());

  if (table == nullptr) {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": deleted (not found)";
    resp->set_done(true);
    return Status::OK();
  }

  TRACE("Locking table");
  auto l = table->LockForRead();

  if (!l->data().started_deleting()) {
    LOG(WARNING) << "Servicing IsDeleteTableDone request for table id "
                 << req->table_id() << ": NOT deleted";
    Status s = STATUS(IllegalState, "The table was NOT deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  if (l->data().is_deleted()) {
    if (table->HasTasks()) {
      LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
                << req->table_id() << ": waiting for " << table->NumTasks() << " pending tasks";
      resp->set_done(false);
    } else {
      LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
                << req->table_id() << ": totally deleted";
      resp->set_done(true);
    }
  } else {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": deleting tablets";
    resp->set_done(false);
  }

  return Status::OK();
}

namespace {

CHECKED_STATUS ApplyAlterSteps(const SysTablesEntryPB& current_pb,
                               const AlterTableRequestPB* req,
                               Schema* new_schema,
                               ColumnId* next_col_id) {
  const SchemaPB& current_schema_pb = current_pb.schema();
  Schema cur_schema;
  RETURN_NOT_OK(SchemaFromPB(current_schema_pb, &cur_schema));

  SchemaBuilder builder(cur_schema);
  if (current_pb.has_next_column_id()) {
    builder.set_next_column_id(ColumnId(current_pb.next_column_id()));
  }

  for (const AlterTableRequestPB::Step& step : req->alter_schema_steps()) {
    switch (step.type()) {
      case AlterTableRequestPB::ADD_COLUMN: {
        if (!step.has_add_column()) {
          return STATUS(InvalidArgument, "ADD_COLUMN missing column info");
        }

        // Verify that encoding is appropriate for the new column's type.
        ColumnSchemaPB new_col_pb = step.add_column().schema();
        if (new_col_pb.has_id()) {
          return STATUS(InvalidArgument, Substitute(
              "column $0: client should not specify column id", new_col_pb.ShortDebugString()));
        }
        ColumnSchema new_col = ColumnSchemaFromPB(new_col_pb);

        RETURN_NOT_OK(builder.AddColumn(new_col, false));
        break;
      }

      case AlterTableRequestPB::DROP_COLUMN: {
        if (!step.has_drop_column()) {
          return STATUS(InvalidArgument, "DROP_COLUMN missing column info");
        }

        if (cur_schema.is_key_column(step.drop_column().name())) {
          return STATUS(InvalidArgument, "cannot remove a key column");
        }

        RETURN_NOT_OK(builder.RemoveColumn(step.drop_column().name()));
        break;
      }

      case AlterTableRequestPB::RENAME_COLUMN: {
        if (!step.has_rename_column()) {
          return STATUS(InvalidArgument, "RENAME_COLUMN missing column info");
        }

        RETURN_NOT_OK(builder.RenameColumn(
        step.rename_column().old_name(),
        step.rename_column().new_name()));
        break;
      }

        // TODO: EDIT_COLUMN.

      default: {
        return STATUS(InvalidArgument,
            Substitute("Invalid alter step type: $0", step.type()));
      }
    }
  }

  if (req->has_alter_properties()) {
    RETURN_NOT_OK(builder.AlterProperties(req->alter_properties()));
  }

  *new_schema = builder.Build();
  *next_col_id = builder.next_column_id();
  return Status::OK();
}

} // namespace

Status CatalogManager::AlterTable(const AlterTableRequestPB* req,
                                  AlterTableResponsePB* resp,
                                  rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterTable request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // Lookup the table and verify if it exists.
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  NamespaceId new_namespace_id;

  if (req->has_new_namespace()) {
    // Lookup the new namespace and verify if it exists.
    TRACE("Looking up new namespace");
    scoped_refptr<NamespaceInfo> ns;
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->new_namespace(), &ns), resp);

    new_namespace_id = ns->id();
  }

  TRACE("Locking table");
  auto l = table->LockForWrite();
  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The table was deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  bool has_changes = false;
  const TableName table_name = l->data().name();
  const NamespaceId namespace_id = l->data().namespace_id();
  const TableName new_table_name = req->has_new_table_name() ? req->new_table_name() : table_name;

  // Calculate new schema for the on-disk state, not persisted yet.
  Schema new_schema;
  ColumnId next_col_id = ColumnId(l->data().pb.next_column_id());
  if (req->alter_schema_steps_size() || req->has_alter_properties()) {
    TRACE("Apply alter schema");
    Status s = ApplyAlterSteps(l->data().pb, req, &new_schema, &next_col_id);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    }
    DCHECK_NE(next_col_id, 0);
    DCHECK_EQ(new_schema.find_column_by_id(next_col_id),
              static_cast<int>(Schema::kColumnNotFound));
    has_changes = true;
  }

  // Try to acquire the new table name.
  if (req->has_new_namespace() || req->has_new_table_name()) {

    if (new_namespace_id.empty()) {
      const Status s = STATUS(InvalidArgument, "No namespace used");
      return SetupError(resp->mutable_error(), MasterErrorPB::NO_NAMESPACE_USED, s);
    }

    std::lock_guard<LockType> catalog_lock(lock_);

    TRACE("Acquired catalog manager lock");

    // Verify that the table does not exist.
    scoped_refptr<TableInfo> other_table = FindPtrOrNull(
        table_names_map_, {new_namespace_id, new_table_name});
    if (other_table != nullptr) {
      Status s = STATUS(AlreadyPresent, "Table already exists", other_table->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT, s);
    }

    // Acquire the new table name (now we have 2 name for the same table).
    table_names_map_[{new_namespace_id, new_table_name}] = table;
    l->mutable_data()->pb.set_namespace_id(new_namespace_id);
    l->mutable_data()->pb.set_name(new_table_name);

    has_changes = true;
  }

  // Skip empty requests...
  if (!has_changes) {
    return Status::OK();
  }

  // Serialize the schema Increment the version number.
  if (new_schema.initialized()) {
    if (!l->data().pb.has_fully_applied_schema()) {
      l->mutable_data()->pb.mutable_fully_applied_schema()->CopyFrom(l->data().pb.schema());
    }
    CHECK_OK(SchemaToPB(new_schema, l->mutable_data()->pb.mutable_schema()));
  }
  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  l->mutable_data()->pb.set_next_column_id(next_col_id);
  l->mutable_data()->set_state(SysTablesEntryPB::ALTERING,
                               Substitute("Alter table version=$0 ts=$1",
                                          l->mutable_data()->pb.version(),
                                          LocalTimeAsString()));

  // Update sys-catalog with the new table schema.
  TRACE("Updating metadata on disk");
  Status s = sys_catalog_->UpdateItem(table.get(), leader_ready_term_);
  if (!s.ok()) {
    s = s.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog tables entry: $0",
                   s.ToString()));
    LOG(WARNING) << s.ToString();
    if (req->has_new_namespace() || req->has_new_table_name()) {
      std::lock_guard<LockType> catalog_lock(lock_);
      CHECK_EQ(table_names_map_.erase({new_namespace_id, new_table_name}), 1);
    }
    // TableMetadaLock follows RAII paradigm: when it leaves scope,
    // 'l' will be unlocked, and the mutation will be aborted.
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // Remove the old name.
  if (req->has_new_namespace() || req->has_new_table_name()) {
    TRACE("Removing (namespace, table) combination ($0, $1) from by-name map",
        namespace_id, table_name);
    std::lock_guard<LockType> l_map(lock_);
    if (table_names_map_.erase({namespace_id, table_name}) != 1) {
      PANIC_RPC(rpc, "Could not remove table from map, name=" + l->data().name());
    }
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();

  SendAlterTableRequest(table);

  LOG(INFO) << "Successfully initiated ALTER TABLE (pending tablet schema updates) for "
            << table->ToString() << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                        IsAlterTableDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists.
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The table was deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  // 2. Verify if the alter is in-progress.
  TRACE("Verify if there is an alter operation in progress for $0", table->ToString());
  resp->set_schema_version(l->data().pb.version());
  resp->set_done(l->data().pb.state() != SysTablesEntryPB::ALTERING);

  return Status::OK();
}

Status CatalogManager::GetTableSchema(const GetTableSchemaRequestPB* req,
                                      GetTableSchemaResponsePB* resp) {
  VLOG(1) << "Servicing GetTableSchema request for " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // Lookup the table and verify if it exists.
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The table was deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  if (l->data().pb.has_fully_applied_schema()) {
    // An AlterTable is in progress; fully_applied_schema is the last
    // schema that has reached every TS.
    CHECK(l->data().pb.state() == SysTablesEntryPB::ALTERING);
    resp->mutable_schema()->CopyFrom(l->data().pb.fully_applied_schema());
  } else {
    // There's no AlterTable, the regular schema is "fully applied".
    resp->mutable_schema()->CopyFrom(l->data().pb.schema());
  }
  // TODO(bogdan): add back in replication_info once we allow overrides!
  resp->mutable_partition_schema()->CopyFrom(l->data().pb.partition_schema());
  resp->set_create_table_done(!table->IsCreateInProgress());
  resp->set_table_type(table->metadata().state().pb.table_type());
  resp->mutable_identifier()->set_table_name(l->data().pb.name());
  resp->mutable_identifier()->set_table_id(table->id());
  resp->mutable_identifier()->mutable_namespace_()->set_id(table->namespace_id());
  NamespaceIdentifierPB nsid;
  nsid.set_id(table->namespace_id());
  scoped_refptr<NamespaceInfo> nsinfo;
  if (FindNamespace(nsid, &nsinfo).ok()) {
    resp->mutable_identifier()->mutable_namespace_()->set_name(nsinfo->name());
  }
  resp->set_version(l->data().pb.version());
  resp->mutable_indexes()->CopyFrom(l->data().pb.indexes());
  if (l->data().pb.has_index_info()) {
    *resp->mutable_index_info() = l->data().pb.index_info();
    resp->set_deprecated_indexed_table_id(l->data().pb.index_info().indexed_table_id());
  }

  // Get namespace name by id.
  boost::shared_lock<LockType> l_map(lock_);
  TRACE("Looking up namespace");
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, table->namespace_id());

  if (ns == nullptr) {
    Status s = STATUS_SUBSTITUTE(NotFound,
        "Could not find namespace by namespace id $0 for request $1.",
        table->namespace_id(), req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  resp->mutable_identifier()->mutable_namespace_()->set_name(ns->name());
  return Status::OK();
}

Status CatalogManager::ListTables(const ListTablesRequestPB* req,
                                  ListTablesResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  NamespaceId namespace_id;

  // Validate namespace.
  if (req->has_namespace_()) {
    scoped_refptr<NamespaceInfo> ns;

    // Lookup the namespace and verify if it exists.
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);

    namespace_id = ns->id();
  }

  boost::shared_lock<LockType> l(lock_);

  for (const auto& entry : table_ids_map_) {
    auto& table_info = *entry.second;
    auto ltm = table_info.LockForRead();

    if (!ltm->data().is_running()) continue;

    if (!namespace_id.empty() && namespace_id != table_info.namespace_id()) {
      continue; // Skip tables from other namespaces.
    }

    if (req->has_name_filter()) {
      size_t found = ltm->data().name().find(req->name_filter());
      if (found == string::npos) {
        continue;
      }
    }

    if (req->exclude_system_tables() &&
        (IsSystemTable(table_info) || table_info.IsRedisTable())) {
      continue;
    }

    ListTablesResponsePB::TableInfo *table = resp->add_tables();
    table->set_id(entry.second->id());
    table->set_name(ltm->data().name());
    table->set_table_type(ltm->data().table_type());

    scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_,
        ltm->data().namespace_id());

    if (CHECK_NOTNULL(ns.get())) {
      auto l = ns->LockForRead();
      table->mutable_namespace_()->set_id(ns->id());
      table->mutable_namespace_()->set_name(ns->name());
    }
  }
  return Status::OK();
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfo(const TableId& table_id) {
  boost::shared_lock<LockType> l(lock_);
  return FindPtrOrNull(table_ids_map_, table_id);
}
scoped_refptr<TableInfo> CatalogManager::GetTableInfoFromNamespaceNameAndTableName(
    const NamespaceName& namespace_name, const TableName& table_name) {

  boost::shared_lock<LockType> l(lock_);
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_names_map_, namespace_name);
  if (ns == nullptr) {
    return nullptr;
  }
  return FindPtrOrNull(table_names_map_, {ns->id(), table_name});
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfoUnlocked(const TableId& table_id) {
  return FindPtrOrNull(table_ids_map_, table_id);
}

void CatalogManager::GetAllTables(std::vector<scoped_refptr<TableInfo>> *tables,
                                  bool includeOnlyRunningTables) {
  tables->clear();
  boost::shared_lock<LockType> l(lock_);
  for (const TableInfoMap::value_type& e : table_ids_map_) {
    if (includeOnlyRunningTables && !e.second->is_running()) {
      continue;
    }
    tables->push_back(e.second);
  }
}

void CatalogManager::GetAllNamespaces(std::vector<scoped_refptr<NamespaceInfo>>* namespaces) {
  namespaces->clear();
  boost::shared_lock<LockType> l(lock_);
  for (const NamespaceInfoMap::value_type& e : namespace_ids_map_) {
    namespaces->push_back(e.second);
  }
}

void CatalogManager::GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo>>* types) {
  types->clear();
  boost::shared_lock<LockType> l(lock_);
  for (const UDTypeInfoMap::value_type& e : udtype_ids_map_) {
    types->push_back(e.second);
  }
}

void CatalogManager::GetAllRoles(std::vector<scoped_refptr<RoleInfo>>* roles) {
  roles->clear();
  boost::shared_lock<LockType> l(lock_);
  for (const RoleInfoMap::value_type& e : roles_map_) {
    roles->push_back(e.second);
  }
}

vector<string> CatalogManager::DirectMemberOf(const RoleName& role) {
  vector<string> roles;
  for (const auto& e : roles_map_) {
    auto l = e.second->LockForRead();
    const auto& pb = l->data().pb;
    for (const auto& member_of : pb.member_of()) {
      if (member_of == role) {
        roles.push_back(pb.role());
        // No need to keep checking the rest of the members.
        break;
      }
    }
  }
  return roles;
}

void CatalogManager::BuildRecursiveRoles() {
  std::lock_guard<LockType> l_big(lock_);
  BuildRecursiveRolesUnlocked();
}

void CatalogManager::TraverseRole(const string& role_name, unordered_set<RoleName>* granted_roles) {
  auto iter = recursive_granted_roles_.find(role_name);
  // This node has already been visited. So just add all the granted (directly or through
  // inheritance) roles to granted_roles.
  if (iter != recursive_granted_roles_.end()) {
    if (granted_roles) {
      const auto& set = iter->second;
      granted_roles->insert(set.begin(), set.end());
    }
    return;
  }

  const auto& role_info = roles_map_[role_name];
  auto l = role_info->LockForRead();
  const auto& pb = l->data().pb;
  if (pb.member_of().size() == 0) {
    recursive_granted_roles_.insert({role_name, {}});
  } else {
    for (const auto& direct_granted_role : pb.member_of()) {
      recursive_granted_roles_[role_name].insert(direct_granted_role);
      TraverseRole(direct_granted_role, &recursive_granted_roles_[role_name]);
    }
    if (granted_roles) {
      const auto& set = recursive_granted_roles_[role_name];
      granted_roles->insert(set.begin(), set.end());
    }
  }
}

// Depth first search. We assume that there are no cycles in the graph of dependencies.
void CatalogManager::BuildRecursiveRolesUnlocked() {
  TRACE("Acquired catalog manager lock");
  recursive_granted_roles_.clear();

  // Build the first level member of map and find all the roles that have at least one member in its
  // member_of field.
  for (const auto& e : roles_map_) {
    const auto& role_name = e.first;
    if (recursive_granted_roles_.find(role_name) == recursive_granted_roles_.end()) {
      TraverseRole(role_name, nullptr);
    }
  }

  BuildResourcePermissionsUnlocked();
}

void CatalogManager::BuildResourcePermissionsUnlocked() {
  shared_ptr<GetPermissionsResponsePB> response = std::make_shared<GetPermissionsResponsePB>();

  for (const auto& e : recursive_granted_roles_) {
    const auto& role_name = e.first;
    // Get a copy of this set.
    auto granted_roles = e.second;
    granted_roles.insert(role_name);
    auto* role_permissions = response->add_role_permissions();
    for (const auto& granted_role : granted_roles) {
      const auto& role_info = roles_map_[granted_role];
      auto l = role_info->LockForRead();
      const auto& pb = l->data().pb;
      role_permissions->set_role(role_name);

      // No permissions on ALL ROLES and ALL KEYSPACES by default.
      role_permissions->set_all_keyspaces_permissions(0);
      role_permissions->set_all_roles_permissions(0);

      Permissions all_roles_permissions_bitset;
      Permissions all_keyspaces_permissions_bitset;
      for (const auto& resource : pb.resources()) {
        Permissions resource_permissions_bitset;

        for (const auto& permission : resource.permissions()) {
          if (resource.canonical_resource() == kRolesRoleResource) {
            all_roles_permissions_bitset.set(permission);
          } else if (resource.canonical_resource() == kRolesDataResource) {
            all_keyspaces_permissions_bitset.set(permission);
          } else {
            resource_permissions_bitset.set(permission);
          }
        }

        if (resource.canonical_resource() != kRolesDataResource &&
            resource.canonical_resource() != kRolesRoleResource) {
          auto* resource_permissions = role_permissions->add_resource_permissions();
          resource_permissions->set_canonical_resource(resource.canonical_resource());
          resource_permissions->set_permissions(resource_permissions_bitset.to_ullong());
          recursive_granted_permissions_[role_name][resource.canonical_resource()] =
              resource_permissions_bitset.to_ullong();
        }
      }

      role_permissions->set_all_keyspaces_permissions(all_keyspaces_permissions_bitset.to_ullong());
      role_permissions->set_all_roles_permissions(all_roles_permissions_bitset.to_ullong());

      // TODO: since this gets checked first when enforcing permissions, there is no point in
      // populating the rest of the permissions. Furthermore, we should remove any specific
      // permissions in the map.
      // In other words, if all-roles_all_keyspaces_permissions is equal to superuser_permissions,
      // it should be the only field sent to the clients for that specific role.
      if (pb.is_superuser()) {
        Permissions superuser_bitset;
        // Set all the bits to 1.
        superuser_bitset.set();

        role_permissions->set_all_keyspaces_permissions(superuser_bitset.to_ullong());
        role_permissions->set_all_roles_permissions(superuser_bitset.to_ullong());
      }
    }
  }

  auto config = CHECK_NOTNULL(security_config_.get())->LockForRead();
  response->set_version(config->data().pb.security_config().roles_version());
  permissions_cache_ = std::move(response);
}

// Get all the permissions granted to resources.
Status CatalogManager::GetPermissions(const GetPermissionsRequestPB* req,
                                      GetPermissionsResponsePB* resp,
                                      rpc::RpcContext* rpc) {
  std::shared_ptr<GetPermissionsResponsePB> permissions_cache;
  {
    std::lock_guard<LockType> l_big(lock_);
    if (!permissions_cache_) {
      BuildRecursiveRolesUnlocked();
      if (!permissions_cache_) {
        DFATAL_OR_RETURN_NOT_OK(STATUS(IllegalState, "Unable to build permissions cache"));
      }
    }
    // Create another reference so that the cache doesn't go away while we are using it.
    permissions_cache = permissions_cache_;
  }

  boost::optional<uint64_t> request_version;
  if (req->has_if_version_greater_than()) {
    request_version = req->if_version_greater_than();
  }

  if (request_version && permissions_cache->version() == *request_version) {
    resp->set_version(permissions_cache->version());
    return Status::OK();
  } else if (request_version && permissions_cache->version() < *request_version) {
    LOG(WARNING) << "GetPermissionsRequestPB version is greater than master's version";
    Status s = STATUS(IllegalState, Substitute(
        "GetPermissionsRequestPB version $0 is greater than master's version $1. "
        "Should call GetPermissions again",  *request_version,
        permissions_cache->version()));
    return SetupError(resp->mutable_error(), MasterErrorPB::CONFIG_VERSION_MISMATCH, s);
  }

  // permisions_cache_->version() > req->if_version_greater_than() or
  // req->if_version_greather_than() is not set.
  resp->CopyFrom(*permissions_cache);
  return Status::OK();
}

bool CatalogManager::IsMemberOf(const RoleName& granted_role, const RoleName& role) {
  const auto& iter = recursive_granted_roles_.find(role);
  if (iter == recursive_granted_roles_.end()) {
    // No roles have been granted to role.
    return false;
  }

  const auto& granted_roles = iter->second;
  if (granted_roles.find(granted_role) == granted_roles.end()) {
    // granted_role has not been granted directly or through inheritance to role.
    return false;
  }

  return true;
}

NamespaceName CatalogManager::GetNamespaceName(const NamespaceId& id) const {
  boost::shared_lock<LockType> l(lock_);
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, id);
  return ns == nullptr ? NamespaceName() : ns->name();
}

bool CatalogManager::IsSystemTable(const TableInfo& table) const {
  TabletInfos tablets;
  table.GetAllTablets(&tablets);
  for (const auto& tablet : tablets) {
    if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
      return true;
    }
  }
  return false;
}

void CatalogManager::NotifyTabletDeleteFinished(const TabletServerId& tserver_uuid,
                                                const TabletId& tablet_id) {
  scoped_refptr<DeletedTableInfo> deleted_table;
  {
    std::lock_guard<LockType> l_map(lock_);
    DeletedTabletMap::key_type tablet_key(tserver_uuid, tablet_id);
    deleted_table = FindPtrOrNull(deleted_tablet_map_, tablet_key);

    if (deleted_table != nullptr) {
      LOG(INFO) << "Deleted tablet " << tablet_key.second << " in ts " << tablet_key.first
                << " from deleted table " << deleted_table->id();
      deleted_tablet_map_.erase(tablet_key);
      deleted_table->DeleteTablet(tablet_key);
      // TODO: Check if we want to delete the tablet from the sys_catalog here.
    }
  }

  // Global lock (lock_) must be released for MarkTableDeletedIfNoTablets().
  if (deleted_table != nullptr) {
    MarkTableDeletedIfNoTablets(deleted_table);
  }

  shared_ptr<TSDescriptor> ts_desc;
  if (!master_->ts_manager()->LookupTSByUUID(tserver_uuid, &ts_desc)) {
    LOG(WARNING) << "Unable to find tablet server " << tserver_uuid;
  } else if (!ts_desc->IsTabletDeletePending(tablet_id)) {
    LOG(WARNING) << "Pending delete for tablet " << tablet_id << " in ts "
                 << tserver_uuid << " doesn't exist";
  } else {
    LOG(INFO) << "Clearing pending delete for tablet " << tablet_id << " in ts " << tserver_uuid;
    ts_desc->ClearPendingTabletDelete(tablet_id);
  }

  TRACE("Try to delete from internal by-id map");
  CleanUpDeletedTables();
}

Status CatalogManager::ProcessTabletReport(TSDescriptor* ts_desc,
                                           const TabletReportPB& report,
                                           TabletReportUpdatesPB *report_update,
                                           RpcContext* rpc) {
  TRACE_EVENT2("master", "ProcessTabletReport",
               "requestor", rpc->requestor_string(),
               "num_tablets", report.updated_tablets_size());

  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Received tablet report from " << RequestorString(rpc) << "("
            << ts_desc->permanent_uuid() << "): " << report.DebugString();
  }

  if (!ts_desc->has_tablet_report() && report.is_incremental()) {
    string msg = "Received an incremental tablet report when a full one was needed";
    LOG(WARNING) << "Invalid tablet report from " << ts_desc->permanent_uuid() << ": " << msg;
    // We should respond with success in order to send reply that we need full report.
    return Status::OK();
  }

  // TODO: on a full tablet report, we may want to iterate over the tablets we think
  // the server should have, compare vs the ones being reported, and somehow mark
  // any that have been "lost" (eg somehow the tablet metadata got corrupted or something).

  for (const ReportedTabletPB& reported : report.updated_tablets()) {
    ReportedTabletUpdatesPB *tablet_report = report_update->add_tablets();
    tablet_report->set_tablet_id(reported.tablet_id());
    RETURN_NOT_OK_PREPEND(HandleReportedTablet(ts_desc, reported, tablet_report),
                          Substitute("Error handling $0", reported.ShortDebugString()));
  }

  if (!ts_desc->has_tablet_report()) {
    LOG(INFO) << ts_desc->permanent_uuid() << " now has full report for "
              << report.updated_tablets_size() << " tablets.";
  }

  if (!report.is_incremental()) {
    if (report.updated_tablets_size() == 0) {
      LOG(INFO) << ts_desc->permanent_uuid() << " sent full tablet report with 0 tablets.";
    }
    // Do not unset full tablet report missing for ts desc for an incremental case.
    ts_desc->set_has_tablet_report(true);
  }

  if (report.updated_tablets_size() > 0) {
    background_tasks_->WakeIfHasPendingUpdates();
  }

  return Status::OK();
}

namespace {
// Return true if receiving 'report' for a tablet in CREATING state should
// transition it to the RUNNING state.
bool ShouldTransitionTabletToRunning(const ReportedTabletPB& report) {
  if (report.state() != tablet::RUNNING) return false;

  // In many tests, we disable leader election, so newly created tablets
  // will never elect a leader on their own. In this case, we transition
  // to RUNNING as soon as we get a single report.
  if (!FLAGS_catalog_manager_wait_for_new_tablets_to_elect_leader) {
    return true;
  }

  // Otherwise, we only transition to RUNNING once a leader is elected.
  return report.committed_consensus_state().has_leader_uuid();
}
}  // anonymous namespace

Status CatalogManager::HandleReportedTablet(TSDescriptor* ts_desc,
                                            const ReportedTabletPB& report,
                                            ReportedTabletUpdatesPB *report_updates) {
  TRACE_EVENT1("master", "HandleReportedTablet",
               "tablet_id", report.tablet_id());
  scoped_refptr<TabletInfo> tablet;
  {
    boost::shared_lock<LockType> l(lock_);
    tablet = FindPtrOrNull(tablet_map_, report.tablet_id());
  }
  RETURN_NOT_OK_PREPEND(CheckIsLeaderAndReady(),
      Substitute("This master is no longer the leader, unable to handle report for tablet $0",
                 report.tablet_id()));
  if (!tablet) {
    LOG(INFO) << "Got report from unknown tablet " << report.tablet_id()
              << ": Sending delete request for this orphan tablet";
    SendDeleteTabletRequest(report.tablet_id(), TABLET_DATA_DELETED, boost::none, nullptr, ts_desc,
                            "Report from unknown tablet");
    return Status::OK();
  }
  if (!tablet->table()) {
    LOG(INFO) << "Got report from an orphaned tablet " << report.tablet_id();
    SendDeleteTabletRequest(report.tablet_id(), TABLET_DATA_DELETED, boost::none, nullptr, ts_desc,
                            "Report from an orphaned tablet");
    return Status::OK();
  }
  VLOG(3) << "tablet report: " << report.ShortDebugString();

  // TODO: we don't actually need to do the COW here until we see we're going
  // to change the state. Can we change CowedObject to lazily do the copy?
  auto table_lock = tablet->table()->LockForRead();
  auto tablet_lock = tablet->LockForWrite();

  // If the TS is reporting a tablet which has been deleted, or a tablet from
  // a table which has been deleted, send it an RPC to delete it.
  // NOTE: when a table is deleted, we don't currently iterate over all of the
  // tablets and mark them as deleted. Hence, we have to check the table state,
  // not just the tablet state.
  if (tablet_lock->data().is_deleted() ||
      table_lock->data().started_deleting()) {
    report_updates->set_state_msg(tablet_lock->data().pb.state_msg());
    const string msg = tablet_lock->data().pb.state_msg();
    LOG(INFO) << "Got report from deleted tablet " << tablet->ToString()
              << " (" << msg << "): Sending delete request for this tablet";
    // TODO: Cancel tablet creation, instead of deleting, in cases where
    // that might be possible (tablet creation timeout & replacement).
    SendDeleteTabletRequest(tablet->tablet_id(), TABLET_DATA_DELETED, boost::none,
                            tablet->table(), ts_desc,
                            Substitute("Tablet deleted: $0", msg));
    return Status::OK();
  }

  if (!table_lock->data().is_running()) {
    LOG(INFO) << "Got report from tablet " << tablet->tablet_id()
              << " for non-running table " << tablet->table()->ToString() << ": "
              << tablet_lock->data().pb.state_msg();
    report_updates->set_state_msg(tablet_lock->data().pb.state_msg());
    return Status::OK();
  }

  // Check if the tablet requires an "alter table" call.
  bool tablet_needs_alter = false;
  if (report.has_schema_version() &&
      table_lock->data().pb.version() != report.schema_version()) {
    if (report.schema_version() > table_lock->data().pb.version()) {
      LOG(ERROR) << "TS " << ts_desc->permanent_uuid()
                 << " has reported a schema version greater than the current one "
                 << " for tablet " << tablet->ToString()
                 << ". Expected version " << table_lock->data().pb.version()
                 << " got " << report.schema_version()
                 << " (corruption)";
    } else {
      LOG(INFO) << "TS " << ts_desc->permanent_uuid()
            << " does not have the latest schema for tablet " << tablet->ToString()
            << ". Expected version " << table_lock->data().pb.version()
            << " got " << report.schema_version();
    }
    // It's possible that the tablet being reported is a laggy replica, and in fact
    // the leader has already received an AlterTable RPC. That's OK, though --
    // it'll safely ignore it if we send another.
    tablet_needs_alter = true;
  }

  if (report.has_error()) {
    Status s = StatusFromPB(report.error());
    DCHECK(!s.ok());
    DCHECK_EQ(report.state(), tablet::FAILED);
    LOG(WARNING) << "Tablet " << tablet->ToString() << " has failed on TS "
                 << ts_desc->permanent_uuid() << ": " << s.ToString();
    return Status::OK();
  }

  // The report will not have a committed_consensus_state if it is in the
  // middle of starting up, such as during tablet bootstrap.
  if (report.has_committed_consensus_state()) {
    const ConsensusStatePB& prev_cstate = tablet_lock->data().pb.committed_consensus_state();
    ConsensusStatePB cstate = report.committed_consensus_state();

    // Check if we got a report from a tablet that is no longer part of the raft
    // config. If so, tombstone it. We only tombstone replicas that include a
    // committed raft config in their report that has an opid_index strictly
    // less than the latest reported committed config, and (obviously) who are
    // not members of the latest config. This prevents us from spuriously
    // deleting replicas that have just been added to a pending config and are
    // in the process of catching up to the log entry where they were added to
    // the config.
    if (FLAGS_master_tombstone_evicted_tablet_replicas &&
        cstate.config().opid_index() < prev_cstate.config().opid_index() &&
        !IsRaftConfigMember(ts_desc->permanent_uuid(), prev_cstate.config())) {
      SendDeleteTabletRequest(report.tablet_id(), TABLET_DATA_TOMBSTONED,
                              prev_cstate.config().opid_index(), tablet->table(), ts_desc,
                              Substitute("Replica from old config with index $0 (latest is $1)",
                                         cstate.config().opid_index(),
                                         prev_cstate.config().opid_index()));
      return Status::OK();
    }

    // If the tablet was not RUNNING, and we have a leader elected, mark it as RUNNING.
    // We need to wait for a leader before marking a tablet as RUNNING, or else we
    // could incorrectly consider a tablet created when only a minority of its replicas
    // were successful. In that case, the tablet would be stuck in this bad state
    // forever.
    if (!tablet_lock->data().is_running() && ShouldTransitionTabletToRunning(report)) {
      DCHECK_EQ(SysTabletsEntryPB::CREATING, tablet_lock->data().pb.state())
          << "Tablet in unexpected state: " << tablet->ToString()
          << ": " << tablet_lock->data().pb.ShortDebugString();
      // Mark the tablet as running
      // TODO: we could batch the IO onto a background thread, or at least
      // across multiple tablets in the same report.
      VLOG(1) << "Tablet " << tablet->ToString() << " is now online";
      tablet_lock->mutable_data()->set_state(SysTabletsEntryPB::RUNNING,
                                             "Tablet reported with an active leader");
    }

    // The Master only accepts committed consensus configurations since it needs the committed index
    // to only cache the most up-to-date config.
    if (PREDICT_FALSE(!cstate.config().has_opid_index())) {
      LOG(DFATAL) << "Missing opid_index in reported config:\n" << report.DebugString();
      return STATUS(InvalidArgument, "Missing opid_index in reported config");
    }

    bool modified_cstate = false;
    if (cstate.config().opid_index() > prev_cstate.config().opid_index() ||
        (cstate.has_leader_uuid() &&
         (!prev_cstate.has_leader_uuid() || cstate.current_term() > prev_cstate.current_term()))) {

      // When a config change is reported to the master, it may not include the
      // leader because the follower doing the reporting may not know who the
      // leader is yet (it may have just started up). If the reported config
      // has the same term as the previous config, and the leader was
      // previously known for the current term, then retain knowledge of that
      // leader even if it wasn't reported in the latest config.
      if (cstate.current_term() == prev_cstate.current_term()) {
        if (!cstate.has_leader_uuid() && prev_cstate.has_leader_uuid()) {
          cstate.set_leader_uuid(prev_cstate.leader_uuid());
          modified_cstate = true;
        // Sanity check to detect consensus divergence bugs.
        } else if (cstate.has_leader_uuid() && prev_cstate.has_leader_uuid() &&
                   cstate.leader_uuid() != prev_cstate.leader_uuid()) {
          string msg = Substitute("Previously reported cstate for tablet $0 gave "
                                  "a different leader for term $1 than the current cstate. "
                                  "Previous cstate: $2. Current cstate: $3.",
                                  tablet->ToString(), cstate.current_term(),
                                  prev_cstate.ShortDebugString(), cstate.ShortDebugString());
          LOG(DFATAL) << msg;
          return STATUS(InvalidArgument, msg);
        }
      }

      // If a replica is reporting a new consensus configuration, reset the tablet's replicas.
      // Note that we leave out replicas who live in tablet servers who have not heartbeated to
      // master yet.
      LOG(INFO) << "Tablet: " << tablet->tablet_id() << " reported consensus state change."
                << " New consensus state: " << cstate.ShortDebugString()
                << "from " << ts_desc->permanent_uuid();

      // If we need to change the report, copy the whole thing on the stack
      // rather than const-casting.
      const ReportedTabletPB* final_report = &report;
      ReportedTabletPB updated_report;
      if (modified_cstate) {
        updated_report = report;
        *updated_report.mutable_committed_consensus_state() = cstate;
        final_report = &updated_report;
      }

      VLOG(2) << "Resetting replicas for tablet " << final_report->tablet_id()
              << " from config reported by " << ts_desc->permanent_uuid()
              << " to that committed in log index "
              << final_report->committed_consensus_state().config().opid_index()
              << " with leader state from term "
              << final_report->committed_consensus_state().current_term();

      RETURN_NOT_OK(ResetTabletReplicasFromReportedConfig(*final_report, tablet,
                                                          tablet_lock.get(), table_lock.get()));

      // Sanity check replicas for this tablet.
      TabletInfo::ReplicaMap replica_map;
      tablet->GetReplicaLocations(&replica_map);
      if (cstate.config().peers().size() != replica_map.size()) {
        LOG(WARNING) << "Received config count " << cstate.config().peers().size() << " is "
                     << "different than in-memory replica count " << replica_map.size();
      }
    } else {
      // Report opid_index is equal to the previous opid_index. If some
      // replica is reporting the same consensus configuration we already know about and hasn't
      // been added as replica, add it.
      LOG(INFO) << "Peer " << ts_desc->permanent_uuid() << " sent full tablet report for "
                << tablet->tablet_id() << ". Consensus state: " << cstate.ShortDebugString();
      AddReplicaToTabletIfNotFound(ts_desc, report, tablet);
    }
  }

  table_lock->Unlock();
  // We update the tablets each time that someone reports it.
  // This shouldn't be very frequent and should only happen when something in fact changed.
  Status s = sys_catalog_->UpdateItem(tablet.get(), leader_ready_term_);
  if (!s.ok()) {
    LOG(WARNING) << "Error updating tablets: " << s.ToString() << ". Tablet report was: "
                 << report.ShortDebugString();
    return s;
  }
  tablet_lock->Commit();

  // Need to defer the AlterTable command to after we've committed the new tablet data,
  // since the tablet report may also be updating the raft config, and the Alter Table
  // request needs to know who the most recent leader is.
  if (tablet_needs_alter) {
    SendAlterTabletRequest(tablet);
  } else if (report.has_schema_version()) {
    RETURN_NOT_OK(HandleTabletSchemaVersionReport(tablet.get(), report.schema_version()));
  }

  return Status::OK();
}

Status CatalogManager::GrantRevokePermission(const GrantRevokePermissionRequestPB* req,
                                             GrantRevokePermissionResponsePB* resp,
                                             rpc::RpcContext* rpc) {
  LOG(INFO) << (req->revoke() ? "Revoke" : "Grant") << " permission "
            << RequestorString(rpc) << ": " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());

  std::lock_guard<LockType> l_big(lock_);
  TRACE("Acquired catalog manager lock");
  Status s;
  scoped_refptr<NamespaceInfo> ns;
  scoped_refptr<TableInfo> table;

  // Checking if resources exist.
  if (req->resource_type() == ResourceType::TABLE ||
      req->resource_type() == ResourceType::KEYSPACE) {
    // We can't match Apache Cassandra's error because when a namespace is not provided, the error
    // is detected by the semantic analysis in PTQualifiedName::AnalyzeName.
    DCHECK(req->has_namespace_());
    ns = FindPtrOrNull(namespace_names_map_, req->namespace_().name());

    if (req->resource_type() == ResourceType::KEYSPACE) {
      if (ns == nullptr) {
        // Matches Apache Cassandra's error.
        s = STATUS_SUBSTITUTE(NotFound, "Resource <keyspace $0> doesn't exist",
                              req->namespace_().name());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
      }
    } else {
      if (ns) {
        table = FindPtrOrNull(table_names_map_, {ns->id(), req->resource_name()});
      }
      if (table == nullptr) {
        // Matches Apache Cassandra's error.
        s = STATUS_SUBSTITUTE(NotFound, "Resource <table $0.$1> doesn't exist",
            req->namespace_().name(), req->resource_name());
        return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
      }
    }
  }

  if (req->resource_type() == ResourceType::ROLE) {
    scoped_refptr<RoleInfo> role;
    role = FindPtrOrNull(roles_map_, req->resource_name());
    if (role == nullptr) {
      s = STATUS_SUBSTITUTE(NotFound, "Resource <role $0> does not exist", req->role_name());
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
    }
  }

  scoped_refptr<RoleInfo> rp;
  rp = FindPtrOrNull(roles_map_, req->role_name());
  if (rp == nullptr) {
    s = STATUS_SUBSTITUTE(InvalidArgument, "Role $0 doesn't exist", req->role_name());
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  // Increment roles version.
  RETURN_NOT_OK(IncrementRolesVersionUnlocked());

  SysRoleEntryPB* metadata;
  {
    ScopedMutation<RoleInfo> role_info_mutation(rp.get());
    metadata = &rp->mutable_metadata()->mutable_dirty()->pb;

    ResourcePermissionsPB* current_resource = nullptr;
    auto current_resource_iter = metadata->mutable_resources()->end();
    for (current_resource_iter = metadata->mutable_resources()->begin();
         current_resource_iter != metadata->mutable_resources()->end(); current_resource_iter++) {
      if (current_resource_iter->canonical_resource() == req->canonical_resource()) {
        break;
      }
    }

    if (current_resource_iter != metadata->mutable_resources()->end()) {
      current_resource = &(*current_resource_iter);
    }

    if (current_resource == nullptr) {
      if (req->revoke()) {
        return Status::OK();
      }

      current_resource = metadata->add_resources();
      current_resource_iter = std::prev(metadata->mutable_resources()->end());

      current_resource->set_canonical_resource(req->canonical_resource());
      current_resource->set_resource_type(req->resource_type());
      if (req->has_resource_name()) {
        current_resource->set_resource_name(req->resource_name());
      }
      if (req->has_namespace_()) {
        current_resource->set_namespace_name(req->namespace_().name());
      }
    }

    if (req->permission() != PermissionType::ALL_PERMISSION) {
      auto permission_iter = current_resource->permissions().end();
      for (permission_iter = current_resource->permissions().begin();
           permission_iter != current_resource->permissions().end(); permission_iter++) {
        if (*permission_iter == req->permission()) {
          break;
        }
      }

      // Resource doesn't have the permission, and we got a GRANT request.
      if (permission_iter == current_resource->permissions().end() && !req->revoke()) {
        // Verify that the permission is supported by the resource.
        if (!valid_permission_for_resource(req->permission(), req->resource_type())) {
          s = STATUS_SUBSTITUTE(InvalidArgument, "Invalid permission $0 for resource type $1",
              req->permission(), ResourceType_Name(req->resource_type()));
          // This should never happen because invalid permissions get rejected in the analysis part.
          // So crash the process if in debug mode.
          DFATAL_OR_RETURN_NOT_OK(s);
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
        }
        current_resource->add_permissions(req->permission());
      } else if (permission_iter != current_resource->permissions().end() && req->revoke()) {
        current_resource->mutable_permissions()->erase(permission_iter);
      }
    } else {
      // ALL permissions.
      // TODO (Bristy) : Add different permissions for different resources based on role names.
      // For REVOKE ALL we clear all the permissions and do nothing else.
      current_resource->clear_permissions();

      if (!req->revoke()) {
        for (const auto& permission : all_permissions_for_resource(req->resource_type())) {
          current_resource->add_permissions(permission);
        }
      }
    }

    // If this resource doesn't have any more permissions, remove it.
    if (current_resource->permissions().empty()) {
      metadata->mutable_resources()->erase(current_resource_iter);
    }

    s = sys_catalog_->UpdateItem(rp.get(), leader_ready_term_);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute(
          "An error occurred while updating permissions in sys-catalog: $0", s.ToString()));
      LOG(WARNING) << s.ToString();
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }
    TRACE("Wrote Permission to sys-catalog");

    role_info_mutation.Commit();
  }
  LOG(INFO) << "Modified Permission for role " << rp->id();
  BuildResourcePermissionsUnlocked();
  return Status::OK();
}

template<class RespClass>
Status CatalogManager::GrantPermissions(const RoleName& role_name,
                                        const std::string& canonical_resource,
                                        const std::string& resource_name,
                                        const NamespaceName& keyspace,
                                        const std::vector<PermissionType>& permissions,
                                        const ResourceType resource_type,
                                        RespClass* resp) {
  std::lock_guard<LockType> l(lock_);

  scoped_refptr<RoleInfo> rp;
  rp = FindPtrOrNull(roles_map_, role_name);
  if (rp == nullptr) {
    const Status s = STATUS_SUBSTITUTE(NotFound, "Role $0 was not found", role_name);
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  RETURN_NOT_OK(IncrementRolesVersionUnlocked());

  {
    SysRoleEntryPB* metadata;
    ScopedMutation <RoleInfo> role_info_mutation(rp.get());
    metadata = &rp->mutable_metadata()->mutable_dirty()->pb;

    ResourcePermissionsPB* current_resource = metadata->add_resources();

    current_resource->set_canonical_resource(canonical_resource);
    current_resource->set_resource_type(resource_type);
    current_resource->set_resource_name(resource_name);
    current_resource->set_namespace_name(keyspace);

    for (const auto& permission : permissions) {
      if (permission == PermissionType::DESCRIBE_PERMISSION &&
          resource_type != ResourceType::ROLE &&
          resource_type != ResourceType::ALL_ROLES) {
        // Describe permission should only be granted to the role resource.
        continue;
      }
      current_resource->add_permissions(permission);
    }
    Status s = sys_catalog_->UpdateItem(rp.get(), leader_ready_term_);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute(
          "An error occurred while updating permissions in sys-catalog: $0", s.ToString()));
      LOG(WARNING) << s;
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }
    TRACE("Wrote Permission to sys-catalog");
    role_info_mutation.Commit();

    BuildResourcePermissionsUnlocked();
  }
  return Status::OK();
}

Status CatalogManager::CreateNamespace(const CreateNamespaceRequestPB* req,
                                       CreateNamespaceResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());
  Status s;

  // Copy the request, so we can fill in some defaults.
  LOG(INFO) << "CreateNamespace from " << RequestorString(rpc)
            << ": " << req->DebugString();

  scoped_refptr<NamespaceInfo> ns;
  std::vector<scoped_refptr<TableInfo>> pgsql_tables;
  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // Validate the user request.

    // Verify that the namespace does not exist.
    ns = FindPtrOrNull(namespace_names_map_, req->name());
    if (ns != nullptr) {
      resp->set_id(ns->id());
      s = STATUS(AlreadyPresent, Substitute("Keyspace $0 already exists", req->name()), ns->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_ALREADY_PRESENT, s);
    }

    // Add the new namespace.

    // Create unique id for this new namespace.
    NamespaceId new_id = !req->namespace_id().empty() ? req->namespace_id()
                                                      : GenerateId(SysRowEntry::NAMESPACE);
    ns = new NamespaceInfo(new_id);
    ns->mutable_metadata()->StartMutation();
    SysNamespaceEntryPB *metadata = &ns->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_name(req->name());
    if (req->has_database_type()) {
      metadata->set_database_type(req->database_type());
    }

    if (req->database_type() == YQL_DATABASE_PGSQL) {
      if (req->source_namespace_id().empty()) {
        metadata->set_next_pg_oid(req->next_pg_oid());
      } else {
        const auto source_oid = GetPgsqlDatabaseOid(req->source_namespace_id());
        if (!source_oid.ok()) {
          return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                            source_oid.status());
        }
        for (const auto& iter : table_ids_map_) {
          if (IsPgsqlId(iter.first) &&
              CHECK_RESULT(GetPgsqlDatabaseOid(iter.first)) == *source_oid) {
            pgsql_tables.push_back(iter.second);
          }
        }

        scoped_refptr<NamespaceInfo> source_ns = FindPtrOrNull(namespace_ids_map_,
                                                               req->source_namespace_id());
        if (!source_ns) {
          return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                            STATUS(NotFound, "Source namespace not found",
                                   req->source_namespace_id()));
        }
        auto source_ns_lock = source_ns->LockForRead();
        metadata->set_next_pg_oid(source_ns_lock->data().pb.next_pg_oid());
      }
    }

    // Add the namespace to the in-memory map for the assignment.
    namespace_ids_map_[ns->id()] = ns;
    namespace_names_map_[req->name()] = ns;

    resp->set_id(ns->id());
  }
  TRACE("Inserted new namespace info into CatalogManager maps");

  // Update the on-disk system catalog.
  s = sys_catalog_->AddItem(ns.get(), leader_ready_term_);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute(
        "An error occurred while inserting namespace to sys-catalog: $0", s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  TRACE("Wrote namespace to sys-catalog");

  // Commit the namespace in-memory state.
  ns->mutable_metadata()->CommitMutation();

  LOG(INFO) << "Created namespace " << ns->ToString();

  if (req->has_creator_role_name()) {
    RETURN_NOT_OK(GrantPermissions(req->creator_role_name(),
                                   get_canonical_keyspace(req->name()),
                                   req->name() /* resource name */,
                                   req->name() /* keyspace name */,
                                   all_permissions_for_resource(ResourceType::KEYSPACE),
                                   ResourceType::KEYSPACE,
                                   resp));
  }

  if (req->database_type() == YQL_DATABASE_PGSQL && !pgsql_tables.empty()) {
    s = CopyPgsqlSysTables(ns->id(), pgsql_tables, resp, rpc);
    if (!s.ok()) {
      return s;
    }
  }

  return Status::OK();
}

Status CatalogManager::DeleteNamespace(const DeleteNamespaceRequestPB* req,
                                       DeleteNamespaceResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteNamespace request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<NamespaceInfo> ns;

  // Lookup the namespace and verify if it exists.
  TRACE("Looking up namespace");
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);

  // Check for PostgreSQL database.
  if (ns->database_type() == YQL_DATABASE_PGSQL) {
    if (!req->has_database_type() || req->database_type() != YQL_DATABASE_PGSQL) {
      Status s = STATUS(NotFound, "PostgreSQL database not found", ns->name());
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
  }

  TRACE("Locking namespace");
  auto l = ns->LockForWrite();

  // Only empty namespace can be deleted.
  TRACE("Looking for tables in the namespace");
  {
    boost::shared_lock<LockType> catalog_lock(lock_);

    for (const TableInfoMap::value_type& entry : table_ids_map_) {
      auto ltm = entry.second->LockForRead();

      if (!ltm->data().started_deleting() && ltm->data().namespace_id() == ns->id()) {
        Status s = STATUS(InvalidArgument,
                          Substitute("Cannot delete namespace which has $0: $1 [id=$2]",
                                     ltm->data().pb.indexed_table_id().empty() ? "table" : "index",
                                     ltm->data().name(), entry.second->id()), req->DebugString());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY, s);
      }
    }
  }

  // Only empty namespace can be deleted.
  TRACE("Looking for types in the namespace");
  {
    boost::shared_lock<LockType> catalog_lock(lock_);

    for (const UDTypeInfoMap::value_type& entry : udtype_ids_map_) {
      auto ltm = entry.second->LockForRead();

      if (ltm->data().namespace_id() == ns->id()) {
        Status s = STATUS(InvalidArgument,
            Substitute("Cannot delete namespace which has type: $0 [id=$1]",
                ltm->data().name(), entry.second->id()), req->DebugString());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY, s);
      }
    }
  }

  TRACE("Updating metadata on disk");
  // Update sys-catalog.
  Status s = sys_catalog_->DeleteItem(ns.get(), leader_ready_term_);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys-catalog: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // Remove it from the maps.
  {
    TRACE("Removing from maps");
    std::lock_guard<LockType> l_map(lock_);
    if (namespace_names_map_.erase(ns->name()) < 1) {
      PANIC_RPC(rpc, "Could not remove namespace from map, name=" + l->data().name());
    }
    if (namespace_ids_map_.erase(ns->id()) < 1) {
      PANIC_RPC(rpc, "Could not remove namespace from map, name=" + l->data().name());
    }
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();

  // Delete any permissions granted on this keyspace to any role. See comment in DeleteTable() for
  // more details.
  string canonical_resource = get_canonical_keyspace(req->namespace_().name());
  RETURN_NOT_OK(RemoveAllPermissionsForResource(canonical_resource, resp));

  LOG(INFO) << "Successfully deleted namespace " << ns->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::ListNamespaces(const ListNamespacesRequestPB* req,
                                      ListNamespacesResponsePB* resp) {

  RETURN_NOT_OK(CheckOnline());

  boost::shared_lock<LockType> l(lock_);

  for (const NamespaceInfoMap::value_type& entry : namespace_names_map_) {
    auto ltm = entry.second->LockForRead();

    if (entry.second->database_type() == YQL_DATABASE_PGSQL) {
      if (!req->has_database_type() || req->database_type() != YQL_DATABASE_PGSQL) {
        // Not a match as we do not allow UNDEFINED database type for PGSQL.
        continue;
      }
    }

    NamespaceIdentifierPB *ns = resp->add_namespaces();
    ns->set_id(entry.second->id());
    ns->set_name(entry.second->name());
  }
  return Status::OK();
}

// Create a SysVersionInfo object to track the roles versions.
Status CatalogManager::IncrementRolesVersionUnlocked() {
  DCHECK(lock_.is_locked()) << "We don't have the catalog manager lock!";

  // Prepare write.
  auto l = CHECK_NOTNULL(security_config_.get())->LockForWrite();
  const uint64_t roles_version = l->mutable_data()->pb.security_config().roles_version();
  if (roles_version == std::numeric_limits<uint64_t>::max()) {
    DFATAL_OR_RETURN_NOT_OK(
        STATUS_SUBSTITUTE(IllegalState,
                          "Roles version reached max allowable integer: $0", roles_version));
  }
  l->mutable_data()->pb.mutable_security_config()->set_roles_version(roles_version + 1);

  TRACE("Set CatalogManager's roles version");

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->UpdateItem(security_config_.get(), leader_ready_term_));

  l->Commit();
  return Status::OK();
}

template<class RespClass>
Status CatalogManager::RemoveAllPermissionsForResourceUnlocked(
    const std::string& canonical_resource,
    RespClass* resp) {

  DCHECK(lock_.is_locked()) << "We don't have the catalog manager lock!";

  bool permissions_modified = false;
  for (const auto& e : roles_map_) {
    scoped_refptr <RoleInfo> rp = e.second;
    ScopedMutation <RoleInfo> role_info_mutation(rp.get());
    auto* resources = rp->mutable_metadata()->mutable_dirty()->pb.mutable_resources();
    for (auto itr = resources->begin(); itr != resources->end(); itr++) {
      if (itr->canonical_resource() == canonical_resource) {
        resources->erase(itr);
        role_info_mutation.Commit();
        permissions_modified = true;
        break;
      }
    }
  }

  // Increment the roles version and update the cache only if there was a modification to the
  // permissions.
  if (permissions_modified) {
    const Status s = IncrementRolesVersionUnlocked();
    if (!s.ok()) {
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }

    BuildResourcePermissionsUnlocked();
  }

  return Status::OK();
}

template<class RespClass>
Status CatalogManager::RemoveAllPermissionsForResource(const std::string& canonical_resource,
                                                       RespClass* resp) {
  std::lock_guard<LockType> l_big(lock_);
  return RemoveAllPermissionsForResourceUnlocked(canonical_resource, resp);
}

Status CatalogManager::CreateRoleUnlocked(const std::string& role_name,
                                          const std::string& salted_hash,
                                          const bool login, const bool superuser,
                                          int64_t term,
                                          const bool increment_roles_version) {

  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }
  // Create Entry.
  SysRoleEntryPB role_entry;
  role_entry.set_role(role_name);
  role_entry.set_can_login(login);
  role_entry.set_is_superuser(superuser);
  if (salted_hash.size() != 0) {
    role_entry.set_salted_hash(salted_hash);
  }

  if (increment_roles_version) {
    // Increment roles version.
    RETURN_NOT_OK(IncrementRolesVersionUnlocked());
  }

  // Create in memory object.
  scoped_refptr<RoleInfo> role = new RoleInfo(role_name);

  // Prepare write.
  auto l = role->LockForWrite();
  l->mutable_data()->pb = std::move(role_entry);

  roles_map_[role_name] = role;
  TRACE("Inserted new role info into CatalogManager maps");

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->AddItem(role.get(), term));

  l->Commit();
  BuildRecursiveRolesUnlocked();
  return Status::OK();
}

Status CatalogManager::CreateRole(const CreateRoleRequestPB* req,
                                  CreateRoleResponsePB* resp,
                                  rpc::RpcContext* rpc) {

  LOG(INFO) << "CreateRole from " << RequestorString(rpc) << ": " << req->DebugString();

  RETURN_NOT_OK(CheckOnline());
  Status s;
  {
    TRACE("Acquired catalog manager lock");
    std::lock_guard<LockType> l_big(lock_);
    // Only a SUPERUSER role can create another SUPERUSER role. In Apache Cassandra this gets
    // checked before the existence of the new role.
    if (req->superuser()) {
      scoped_refptr<RoleInfo> creator_role = FindPtrOrNull(roles_map_, req->creator_role_name());
      if (creator_role == nullptr) {
        s = STATUS_SUBSTITUTE(NotFound, "role $0 does not exist", req->creator_role_name());
        return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
      }

      auto clr = creator_role->LockForRead();
      if (!clr->data().pb.is_superuser()) {
        s = STATUS(NotAuthorized, "Only superusers can create a role with superuser status");
        return SetupError(resp->mutable_error(), MasterErrorPB::NOT_AUTHORIZED, s);
      }
    }
    if (FindPtrOrNull(roles_map_, req->name()) != nullptr) {
      s = STATUS(AlreadyPresent,
                 Substitute("Role $0 already exists", req->name()));
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_ALREADY_PRESENT, s);
    }
    s = CreateRoleUnlocked(
        req->name(), req->salted_hash(), req->login(), req->superuser(), leader_ready_term_);
  }
  if (PREDICT_TRUE(s.ok())) {
    LOG(INFO) << "Created role: " << req->name();
    if (req->has_creator_role_name()) {
      RETURN_NOT_OK(GrantPermissions(req->creator_role_name(),
                                     get_canonical_role(req->name()),
                                     req->name() /* resource name */,
                                     "" /* keyspace name */,
                                     all_permissions_for_resource(ResourceType::ROLE),
                                     ResourceType::ROLE,
                                     resp));
    }
  }
  return s;
}

Status CatalogManager::AlterRole(const AlterRoleRequestPB* req,
                                 AlterRoleResponsePB* resp,
                                 rpc::RpcContext* rpc) {

  VLOG(1) << "AlterRole from " << RequestorString(rpc) << ": " << req->DebugString();

  RETURN_NOT_OK(CheckOnline());
  Status s;

  TRACE("Acquired catalog manager lock");
  std::lock_guard<LockType> l_big(lock_);

  auto role = FindPtrOrNull(roles_map_, req->name());
  if (role == nullptr) {
    s = STATUS(NotFound,
        Substitute("Role $0 does not exist", req->name()));
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  // Increment roles version.
  RETURN_NOT_OK(IncrementRolesVersionUnlocked());

  // Modify the role.
  auto l = role->LockForWrite();

  // If the role we are trying to alter is a SUPERUSER, and the request is trying to alter the
  // SUPERUSER field for that role, the role requesting the alter operation must be a SUPERUSER
  // too.
  // TODO(hector): Once "ENG-2663 Support for REVOKE PERMISSIONS and REVOKE ROLE commands" gets
  // committed, we need to enhance this codepath to also check that current_role is not trying to
  // alter the SUPERUSER field of any of the roles granted to it either directly or indirectly
  // through inheritance.
  if (l->mutable_data()->pb.is_superuser() && req->has_superuser()) {
    auto current_role = FindPtrOrNull(roles_map_, req->current_role());
    if (current_role == nullptr) {
      s = STATUS_SUBSTITUTE(NotFound, "Internal error: role $0 does not exist",
                            req->current_role());
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
    }

    auto clr = current_role->LockForRead();
    if (!clr->data().pb.is_superuser()) {
      s = STATUS(NotAuthorized, "Only superusers are allowed to alter superuser status");
      return SetupError(resp->mutable_error(), MasterErrorPB::NOT_AUTHORIZED, s);
    }
  }

  if (req->has_login()) {
    l->mutable_data()->pb.set_can_login(req->login());
  }
  if (req->has_superuser()) {
    l->mutable_data()->pb.set_is_superuser(req->superuser());
  }
  if (req->has_salted_hash()) {
    l->mutable_data()->pb.set_salted_hash(req->salted_hash());
  }

  l->Commit();
  VLOG(1) << "Altered role with request: " << req->ShortDebugString();
  if (req->has_superuser()) {
    BuildResourcePermissionsUnlocked();
  }
  return Status::OK();
}

Status CatalogManager::DeleteRole(const DeleteRoleRequestPB* req,
                                  DeleteRoleResponsePB* resp,
                                  rpc::RpcContext* rpc) {

  LOG(INFO) << "Servicing DeleteRole request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());
  Status s;

  if (!req->has_name()) {
    s = STATUS(InvalidArgument, "No role name given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  TRACE("Acquired catalog manager lock");
  std::lock_guard<LockType> l_big(lock_);

  auto role = FindPtrOrNull(roles_map_, req->name());
  if (role == nullptr) {
    s = STATUS(NotFound,
        Substitute("Role $0 does not exist", req->name()));
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  // Increment roles version.
  RETURN_NOT_OK(IncrementRolesVersionUnlocked());

  // Find all the roles where req->name() is part of the member_of list since we will need to remove
  // the role we are deleting from those lists.
  auto direct_member_of = DirectMemberOf(req->name());
  for (const auto& role_name : direct_member_of) {
    auto role = FindPtrOrNull(roles_map_, role_name);
    if (role == nullptr) {
      continue;
    }
    role->mutable_metadata()->StartMutation();
    auto metadata = &role->mutable_metadata()->mutable_dirty()->pb;

    // Create a new list that contains all the original roles in member_of with the exception of
    // the role we are deleting.
    vector<string> member_of_new_list;
    for (const auto& member_of : metadata->member_of()) {
      if (member_of != req->name()) {
        member_of_new_list.push_back(member_of);
      }
    }

    // Remove the role we are deleting from the list member_of.
    metadata->clear_member_of();
    for (auto member_of : member_of_new_list) {
      metadata->add_member_of(std::move(member_of));
    }

    // Update sys-catalog with the new member_of list for this role.
    s = sys_catalog_->UpdateItem(role.get(), leader_ready_term_);
    if (!s.ok()) {
      LOG(ERROR) << "Unable to remove role " << req->name()
                 << " from member_of list for role " << role_name;
      role->mutable_metadata()->AbortMutation();
    } else {
      role->mutable_metadata()->CommitMutation();
    }
  }

  auto l = role->LockForWrite();

  if (l->mutable_data()->pb.is_superuser()) {
    // If the role we are trying to remove is a SUPERUSER, the role trying to remove it has to be
    // a SUPERUSER too.
    auto current_role = FindPtrOrNull(roles_map_, req->current_role());
    if (current_role == nullptr) {
      s = STATUS_SUBSTITUTE(NotFound, "Internal error: role $0 does not exist",
                            req->current_role());
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
    }

    auto clr = current_role->LockForRead();
    if (!clr->data().pb.is_superuser()) {
      s = STATUS(NotAuthorized, "Only superusers can drop a role with superuser status");
      return SetupError(resp->mutable_error(), MasterErrorPB::NOT_AUTHORIZED, s);
    }
  }

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->DeleteItem(role.get(), leader_ready_term_));
  // Remove it from the maps.
  if (roles_map_.erase(role->id()) < 1) {
    PANIC_RPC(rpc, "Could not remove role from map, role name=" + role->id());
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();
  BuildRecursiveRolesUnlocked();

  // Remove all the permissions granted on the deleted role to any role. See DeleteTable() comment
  // for for more details.
  string canonical_resource = get_canonical_role(req->name());
  RETURN_NOT_OK(RemoveAllPermissionsForResourceUnlocked(canonical_resource, resp));

  LOG(INFO) << "Successfully deleted role " << role->ToString()
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::GrantRevokeRole(const GrantRevokeRoleRequestPB* req,
                                       GrantRevokeRoleResponsePB* resp,
                                       rpc::RpcContext* rpc) {

  LOG(INFO) << "Servicing " << (req->revoke() ? "RevokeRole" : "GrantRole")
            << " request from " << RequestorString(rpc) << ": " << req->ShortDebugString();
  RETURN_NOT_OK(CheckOnline());

  // Cannot grant or revoke itself.
  if (req->granted_role() == req->recipient_role()) {
    if (req->revoke()) {
      // Ignore the request. This is what Apache Cassandra does.
      return Status::OK();
    }
    auto s = STATUS(InvalidArgument,
                    Substitute("$0 is a member of $1", req->recipient_role(), req->granted_role()));
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  Status s;
  // If the request is revoke, we need to create a new list of the roles req->recipient_role()
  // is member of in which we exclude req->granted_role().
  if (!req->has_granted_role() || !req->has_recipient_role()) {
    s = STATUS(InvalidArgument, "No role name given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
  }

  {
    constexpr char role_not_found_msg_str[] = "$0 doesn't exist";
    TRACE("Acquired catalog manager lock");
    std::lock_guard<LockType> l_big(lock_);

    scoped_refptr<RoleInfo> granted_role;
    granted_role = FindPtrOrNull(roles_map_, req->granted_role());
    if (granted_role == nullptr) {
      s = STATUS(NotFound, Substitute(role_not_found_msg_str, req->granted_role()));
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
    }

    scoped_refptr<RoleInfo> recipient_role;
    recipient_role = FindPtrOrNull(roles_map_, req->recipient_role());
    if (recipient_role == nullptr) {
      s = STATUS(NotFound, Substitute(role_not_found_msg_str, req->recipient_role()));
      return SetupError(resp->mutable_error(), MasterErrorPB::ROLE_NOT_FOUND, s);
    }

    // Both roles are present.
    SysRoleEntryPB* metadata;
    {
      ScopedMutation<RoleInfo> role_info_mutation(recipient_role.get());
      metadata = &recipient_role->mutable_metadata()->mutable_dirty()->pb;

      // When revoking a role, the granted role has to be a direct member of the recipient role,
      // but when granting a role, if the recipient role has been granted to the granted role either
      // directly or through inheritance, we will return an error.
      if (req->revoke()) {
        bool direct_member = false;
        vector<string> member_of_new_list;
        for (const auto& member_of : metadata->member_of()) {
          if (member_of == req->granted_role()) {
            direct_member = true;
          } else if (req->revoke()) {
            member_of_new_list.push_back(member_of);
          }
        }

        if (!direct_member) {
          s = STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a member of $1",
                                req->recipient_role(), req->granted_role());
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
        }

        metadata->clear_member_of();
        for (auto member_of : member_of_new_list) {
          metadata->add_member_of(std::move(member_of));
        }
        s = sys_catalog_->UpdateItem(recipient_role.get(), leader_ready_term_);
      } else {
        // Let's make sure that we don't have circular dependencies.
        if (IsMemberOf(req->granted_role(), req->recipient_role()) ||
            IsMemberOf(req->recipient_role(), req->granted_role()) ||
            req->granted_role() == req->recipient_role()) {
          s = STATUS_SUBSTITUTE(InvalidArgument, "$0 is a member of $1",
                                req->recipient_role(), req->granted_role());
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
        }
        metadata->add_member_of(req->granted_role());
        s = sys_catalog_->UpdateItem(recipient_role.get(), leader_ready_term_);
      }
      if (!s.ok()) {
        s = s.CloneAndPrepend(Substitute(
            "An error occurred while updating roles in sys-catalog: $0", s.ToString()));
        LOG(WARNING) << s.ToString();
        return CheckIfNoLongerLeaderAndSetupError(s, resp);
      }

      LOG(INFO) << "Modified 'member of' field of role " << recipient_role->id();

      // Increment roles version before commiting the mutation.
      RETURN_NOT_OK(IncrementRolesVersionUnlocked());

      role_info_mutation.Commit();
      BuildRecursiveRolesUnlocked();
    }
    TRACE("Wrote grant/revoke role to sys-catalog");
  }

  return Status::OK();
}

Status CatalogManager::RedisConfigSet(
    const RedisConfigSetRequestPB* req, RedisConfigSetResponsePB* resp, rpc::RpcContext* rpc) {
  DCHECK(req->has_keyword());
  const auto& key = req->keyword();
  SysRedisConfigEntryPB config_entry;
  config_entry.set_key(key);
  *config_entry.mutable_args() = req->args();
  bool created = false;

  TRACE("Acquired catalog manager lock");
  std::lock_guard<LockType> l_big(lock_);
  scoped_refptr<RedisConfigInfo> cfg = FindPtrOrNull(redis_config_map_, req->keyword());
  if (cfg == nullptr) {
    created = true;
    cfg = new RedisConfigInfo(key);
    redis_config_map_[key] = cfg;
  }

  auto wl = cfg->LockForWrite();
  wl->mutable_data()->pb = std::move(config_entry);
  if (created) {
    CHECK_OK(sys_catalog_->AddItem(cfg.get(), leader_ready_term_));
  } else {
    CHECK_OK(sys_catalog_->UpdateItem(cfg.get(), leader_ready_term_));
  }
  wl->Commit();
  return Status::OK();
}

Status CatalogManager::RedisConfigGet(
    const RedisConfigGetRequestPB* req, RedisConfigGetResponsePB* resp, rpc::RpcContext* rpc) {
  DCHECK(req->has_keyword());
  resp->set_keyword(req->keyword());
  TRACE("Acquired catalog manager lock");
  std::lock_guard<LockType> l_big(lock_);
  scoped_refptr<RedisConfigInfo> cfg = FindPtrOrNull(redis_config_map_, req->keyword());
  if (cfg == nullptr) {
    Status s = STATUS(NotFound, Substitute("Redis config for $0 does not exists", req->keyword()));
    return SetupError(resp->mutable_error(), MasterErrorPB::REDIS_CONFIG_NOT_FOUND, s);
  }
  auto rci = cfg->LockForRead();
  resp->mutable_args()->CopyFrom(rci->data().pb.args());
  return Status::OK();
}

Status CatalogManager::CreateUDType(const CreateUDTypeRequestPB* req,
                                    CreateUDTypeResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "CreateUDType from " << RequestorString(rpc)
            << ": " << req->DebugString();

      RETURN_NOT_OK(CheckOnline());
  Status s;
  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  // Lookup the namespace and verify if it exists.
  if (req->has_namespace_()) {
    TRACE("Looking up namespace");
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);
  }

  {
    TRACE("Acquired catalog manager lock");
    std::lock_guard<LockType> l(lock_);

    // Verify that the type does not exist.
    tp = FindPtrOrNull(udtype_names_map_, std::make_pair(ns->id(), req->name()));

    if (tp != nullptr) {
      s = STATUS(AlreadyPresent,
          Substitute("Type $0.$1 already exists", ns->name(), req->name()),
          tp->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_ALREADY_PRESENT, s);
    }

    // Construct the new type (generate fresh name and set fields).
    UDTypeId new_id = GenerateId(SysRowEntry::UDTYPE);
    tp = new UDTypeInfo(new_id);
    tp->mutable_metadata()->StartMutation();
    SysUDTypeEntryPB *metadata = &tp->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_name(req->name());
    metadata->set_namespace_id(ns->id());
    for (const string& field_name : req->field_names()) {
      metadata->add_field_names(field_name);
    }

    for (const QLTypePB& field_type : req->field_types()) {
      metadata->add_field_types()->CopyFrom(field_type);
    }

    // Add the type to the in-memory maps.
    udtype_ids_map_[tp->id()] = tp;
    udtype_names_map_[std::make_pair(ns->id(), req->name())] = tp;
    resp->set_id(tp->id());
  }
  TRACE("Inserted new user-defined type info into CatalogManager maps");

  // Update the on-disk system catalog.
  s = sys_catalog_->AddItem(tp.get(), leader_ready_term_);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute(
        "An error occurred while inserting user-defined type to sys-catalog: $0", s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  TRACE("Wrote user-defined type to sys-catalog");

  // Commit the in-memory state.
  tp->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Created user-defined type " << tp->ToString();
  return Status::OK();
}

Status CatalogManager::DeleteUDType(const DeleteUDTypeRequestPB* req,
                                    DeleteUDTypeResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUDType request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

      RETURN_NOT_OK(CheckOnline());
  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  if (!req->has_type()) {
    Status s = STATUS(InvalidArgument, "No type given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  // Validate namespace.
  if (req->type().has_namespace_()) {
    // Lookup the namespace and verify if it exists.
    TRACE("Looking up namespace");
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->type().namespace_(), &ns), resp);
  }

  {
    std::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    if (req->type().has_type_id()) {
      tp = FindPtrOrNull(udtype_ids_map_, req->type().type_id());
    } else if (req->type().has_type_name()) {
      tp = FindPtrOrNull(udtype_names_map_, {ns->id(), req->type().type_name()});
    }

    if (tp == nullptr) {
      Status s = STATUS(NotFound, "The type does not exist", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
    }

    // Checking if any table uses this type.
    // TODO: this could be more efficient.
    for (const TableInfoMap::value_type& entry : table_ids_map_) {
      auto ltm = entry.second->LockForRead();
      if (!ltm->data().started_deleting()) {
        for (const auto &col : ltm->data().schema().columns()) {
          if (col.type().main() == DataType::USER_DEFINED_TYPE &&
              col.type().udtype_info().id() == tp->id()) {
            Status s = STATUS(InvalidArgument,
                Substitute("Cannot delete type '$0'. It is used in column $1 of table $2.$3",
                    tp->name(), col.name(), ns->name(), ltm->data().name()), req->DebugString());
            return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY, s);
          }
        }
      }
    }
  }

  auto l = tp->LockForWrite();

  Status s = sys_catalog_->DeleteItem(tp.get(), leader_ready_term_);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys-catalog: $0",
        s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // Remove it from the maps.
  {
    TRACE("Removing from maps");
    std::lock_guard<LockType> l_map(lock_);
    if (udtype_ids_map_.erase(tp->id()) < 1) {
      PANIC_RPC(rpc, "Could not remove user defined type from map, name=" + l->data().name());
    }
    if (udtype_names_map_.erase({ns->id(), tp->name()}) < 1) {
      PANIC_RPC(rpc, "Could not remove user defined type from map, name=" + l->data().name());
    }
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();

  LOG(INFO) << "Successfully deleted user-defined type " << tp->ToString()
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                                     GetUDTypeInfoResponsePB* resp,
                                     rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUDTypeInfo from " << RequestorString(rpc)
            << ": " << req->DebugString();
      RETURN_NOT_OK(CheckOnline());
  Status s;
  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  if (!req->has_type()) {
    s = STATUS(InvalidArgument, "Cannot get type, no type identifier given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
  }

  if (req->type().has_type_id()) {
    tp = FindPtrOrNull(udtype_ids_map_, req->type().type_id());
  } else if (req->type().has_type_name() && req->type().has_namespace_()) {
    // Lookup the type and verify if it exists.
    TRACE("Looking up namespace");
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->type().namespace_(), &ns), resp);

    tp = FindPtrOrNull(udtype_names_map_, std::make_pair(ns->id(), req->type().type_name()));
  }

  if (tp == nullptr) {
    s = STATUS(InvalidArgument, "Couldn't find type", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
  }

  {
    auto type_lock = tp->LockForRead();

    UDTypeInfoPB* type_info = resp->mutable_udtype();

    type_info->set_name(tp->name());
    type_info->set_id(tp->id());
    type_info->mutable_namespace_()->set_id(type_lock->data().namespace_id());

    for (int i = 0; i < type_lock->data().field_names_size(); i++) {
      type_info->add_field_names(type_lock->data().field_names(i));
    }
    for (int i = 0; i < type_lock->data().field_types_size(); i++) {
      type_info->add_field_types()->CopyFrom(type_lock->data().field_types(i));
    }

    LOG(INFO) << "Retrieved user-defined type " << tp->ToString();
  }
  return Status::OK();
}

Status CatalogManager::ListUDTypes(const ListUDTypesRequestPB* req,
                                   ListUDTypesResponsePB* resp) {

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<NamespaceInfo> ns;

  // Validate namespace.
  if (req->has_namespace_()) {
    scoped_refptr<NamespaceInfo> ns;

    // Lookup the namespace and verify that it exists.
    RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);
  }

  boost::shared_lock<LockType> l(lock_);

  for (const UDTypeInfoByNameMap::value_type& entry : udtype_names_map_) {
    auto ltm = entry.second->LockForRead();

    // key is a pair <namespace_id, type_name>.
    if (!ns->id().empty() && ns->id() != entry.first.first) {
      continue; // Skip types from other namespaces.
    }

    UDTypeInfoPB* udtype = resp->add_udtypes();
    udtype->set_id(entry.second->id());
    udtype->set_name(ltm->data().name());
    for (size_t i = 0; i <= ltm->data().field_names_size(); i++) {
      udtype->add_field_names(ltm->data().field_names(i));
    }
    for (size_t i = 0; i <= ltm->data().field_types_size(); i++) {
      udtype->add_field_types()->CopyFrom(ltm->data().field_types(i));
    }

    if (CHECK_NOTNULL(ns.get())) {
      auto l = ns->LockForRead();
      udtype->mutable_namespace_()->set_id(ns->id());
      udtype->mutable_namespace_()->set_name(ns->name());
    }
  }
  return Status::OK();
}

Status CatalogManager::ResetTabletReplicasFromReportedConfig(
    const ReportedTabletPB& report,
    const scoped_refptr<TabletInfo>& tablet,
    TabletInfo::lock_type* tablet_lock,
    TableInfo::lock_type* table_lock) {

  DCHECK(tablet_lock->is_write_locked());
  ConsensusStatePB prev_cstate = tablet_lock->mutable_data()->pb.committed_consensus_state();
  const ConsensusStatePB& cstate = report.committed_consensus_state();
  *tablet_lock->mutable_data()->pb.mutable_committed_consensus_state() = cstate;

  TabletInfo::ReplicaMap replica_locations;
  for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
    shared_ptr<TSDescriptor> ts_desc;
    if (!peer.has_permanent_uuid()) {
      return STATUS(InvalidArgument, "Missing UUID for peer", peer.ShortDebugString());
    }
    if (!master_->ts_manager()->LookupTSByUUID(peer.permanent_uuid(), &ts_desc)) {
      LOG_WITH_PREFIX(WARNING) << "Tablet server has never reported in. "
          << "Not including in replica locations map yet. Peer: " << peer.ShortDebugString()
          << "; Tablet: " << tablet->ToString();
      continue;
    }

    TabletReplica replica;
    NewReplica(ts_desc.get(), report, &replica);
    InsertOrDie(&replica_locations, replica.ts_desc->permanent_uuid(), replica);
  }
  tablet->SetReplicaLocations(std::move(replica_locations));

  if (FLAGS_master_tombstone_evicted_tablet_replicas) {
    unordered_set<string> current_member_uuids;
    for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
      InsertOrDie(&current_member_uuids, peer.permanent_uuid());
    }
    // Send a DeleteTablet() request to peers that are not in the new config.
    for (const consensus::RaftPeerPB& prev_peer : prev_cstate.config().peers()) {
      const string& peer_uuid = prev_peer.permanent_uuid();
      if (!ContainsKey(current_member_uuids, peer_uuid)) {
        shared_ptr<TSDescriptor> ts_desc;
        if (!master_->ts_manager()->LookupTSByUUID(peer_uuid, &ts_desc)) continue;
        SendDeleteTabletRequest(report.tablet_id(), TABLET_DATA_TOMBSTONED,
                                prev_cstate.config().opid_index(), tablet->table(), ts_desc.get(),
                                Substitute("TS $0 not found in new config with opid_index $1",
                                           peer_uuid, cstate.config().opid_index()));
      }
    }
  }

  return Status::OK();
}

void CatalogManager::AddReplicaToTabletIfNotFound(TSDescriptor* ts_desc,
                                                  const ReportedTabletPB& report,
                                                  const scoped_refptr<TabletInfo>& tablet) {
  TabletReplica replica;
  NewReplica(ts_desc, report, &replica);
  // Only inserts if a replica with a matching UUID was not already present.
  ignore_result(tablet->AddToReplicaLocations(replica));
}

void CatalogManager::NewReplica(TSDescriptor* ts_desc,
                                const ReportedTabletPB& report,
                                TabletReplica* replica) {
  CHECK(report.has_committed_consensus_state()) << "No cstate: " << report.ShortDebugString();
  replica->state = report.state();
  replica->role = GetConsensusRole(ts_desc->permanent_uuid(), report.committed_consensus_state());
  replica->ts_desc = ts_desc;
  replica->member_type = GetConsensusMemberType(ts_desc->permanent_uuid(),
                                                report.committed_consensus_state());
}

Status CatalogManager::GetTabletPeer(const TabletId& tablet_id,
                                     std::shared_ptr<TabletPeer>* ret_tablet_peer) const {
  // Note: CatalogManager has only one table, 'sys_catalog', with only
  // one tablet.

  if (PREDICT_FALSE(!IsInitialized())) {
    // Master puts up the consensus service first and then initiates catalog manager's creation
    // asynchronously. So this case is possible, but harmless. The RPC will simply be retried.
    // Previously, because we weren't checking for this condition, we would fatal down stream.
    const string& reason = "CatalogManager is not yet initialized";
    YB_LOG_EVERY_N(WARNING, 1000) << reason;
    return STATUS(ServiceUnavailable, reason);
  }

  CHECK(sys_catalog_) << "sys_catalog_ must be initialized!";

  if (master_->opts().IsShellMode()) {
    return STATUS(NotFound,
        Substitute("In shell mode: no tablet_id $0 exists in CatalogManager.", tablet_id));
  }

  if (sys_catalog_->tablet_id() == tablet_id && sys_catalog_->tablet_peer().get() != nullptr &&
      sys_catalog_->tablet_peer()->CheckRunning().ok()) {
    *ret_tablet_peer = tablet_peer();
  } else {
    return STATUS(NotFound, Substitute(
        "no SysTable in the RUNNING state exists with tablet_id $0 in CatalogManager", tablet_id));
  }
  return Status::OK();
}

const NodeInstancePB& CatalogManager::NodeInstance() const {
  return master_->instance_pb();
}

Status CatalogManager::GetRegistration(ServerRegistrationPB* reg) const {
  return master_->GetRegistration(reg, server::RpcOnly::kTrue);
}

Status CatalogManager::UpdateMastersListInMemoryAndDisk() {
  DCHECK(master_->opts().IsShellMode());

  if (!master_->opts().IsShellMode()) {
    return STATUS(IllegalState, "Cannot update master's info when process is not in shell mode.");
  }

  consensus::ConsensusStatePB consensus_state;
  RETURN_NOT_OK(GetCurrentConfig(&consensus_state));

  if (!consensus_state.has_config()) {
    return STATUS(NotFound, "No Raft config found.");
  }

  RETURN_NOT_OK(sys_catalog_->ConvertConfigToMasterAddresses(consensus_state.config()));
  RETURN_NOT_OK(sys_catalog_->CreateAndFlushConsensusMeta(master_->fs_manager(),
                                                          consensus_state.config(),
                                                          consensus_state.current_term()));

  return Status::OK();
}

Status CatalogManager::EnableBgTasks() {
  std::lock_guard<LockType> l(lock_);
  background_tasks_.reset(new CatalogManagerBgTasks(this));
  RETURN_NOT_OK_PREPEND(background_tasks_->Init(),
                        "Failed to initialize catalog manager background tasks");
  return Status::OK();
}

Status CatalogManager::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  const TabletId& tablet_id = req.tablet_id();
  std::unique_lock<std::mutex> l(remote_bootstrap_mtx_, std::try_to_lock);
  if (!l.owns_lock()) {
    return STATUS(AlreadyPresent,
                  Substitute("Remote bootstrap of tablet $0 already in progress", tablet_id));
  }

  if (!master_->opts().IsShellMode()) {
    return STATUS(IllegalState, "Cannot bootstrap a master which is not in shell mode.");
  }

  LOG(INFO) << "Starting remote bootstrap: " << req.ShortDebugString();

  HostPort bootstrap_peer_addr = HostPortFromPB(DesiredHostPort(
      req.source_broadcast_addr(), req.source_private_addr(), req.source_cloud_info(),
      master_->MakeCloudInfoPB()));

  const string& bootstrap_peer_uuid = req.bootstrap_peer_uuid();
  int64_t leader_term = req.caller_term();

  std::shared_ptr<TabletPeer> old_tablet_peer;
  scoped_refptr<TabletMetadata> meta;
  bool replacing_tablet = false;

  if (tablet_exists_) {
    old_tablet_peer = tablet_peer();
    // Nothing to recover if the remote bootstrap client start failed the last time.
    if (old_tablet_peer) {
      meta = old_tablet_peer->tablet_metadata();
      replacing_tablet = true;
    }
  }

  if (replacing_tablet) {
    // Make sure the existing tablet peer is shut down and tombstoned.
    RETURN_NOT_OK(tserver::HandleReplacingStaleTablet(meta,
                                                      old_tablet_peer,
                                                      tablet_id,
                                                      master_->fs_manager()->uuid(),
                                                      leader_term));
  }

  LOG(INFO) << Substitute("$0 Initiating remote bootstrap from peer $1 ($2).",
                          LogPrefix(), bootstrap_peer_uuid, bootstrap_peer_addr.ToString());

  gscoped_ptr<RemoteBootstrapClient> rb_client(
      new RemoteBootstrapClient(tablet_id,
                                master_->fs_manager(),
                                master_->fs_manager()->uuid()));

  // Download and persist the remote superblock in TABLET_DATA_COPYING state.
  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }
  RETURN_NOT_OK(rb_client->Start(
      bootstrap_peer_uuid, &master_->proxy_cache(), bootstrap_peer_addr, &meta));
  // This SetupTabletPeer is needed by rb_client to perform the remote bootstrap/fetch.
  // And the SetupTablet below to perform "local bootstrap" cannot be done until the remote fetch
  // has succeeded. So keeping them seperate for now.
  sys_catalog_->SetupTabletPeer(meta);
  if (PREDICT_FALSE(FLAGS_inject_latency_during_remote_bootstrap_secs)) {
    LOG(INFO) << "Injecting " << FLAGS_inject_latency_during_remote_bootstrap_secs
              << " seconds of latency for test";
    SleepFor(MonoDelta::FromSeconds(FLAGS_inject_latency_during_remote_bootstrap_secs));
  }

  // From this point onward, the superblock is persisted in TABLET_DATA_COPYING
  // state, and we need to tombstone the tablet if additional steps prior to
  // getting to a TABLET_DATA_READY state fail.
  tablet_exists_ = true;

  // Download all of the remote files.
  TOMBSTONE_NOT_OK(rb_client->FetchAll(tablet_peer()->status_listener()),
                   meta,
                   master_->fs_manager()->uuid(),
                   Substitute("Remote bootstrap: Unable to fetch data from remote peer $0 ($1)",
                              bootstrap_peer_uuid, bootstrap_peer_addr.ToString()),
                   nullptr);

  // Write out the last files to make the new replica visible and update the
  // TabletDataState in the superblock to TABLET_DATA_READY.
  // Finish() will call EndRemoteSession() and wait for the leader to successfully submit a
  // ChangeConfig request (to change this master's role from PRE_VOTER or PRE_OBSERVER to VOTER or
  // OBSERVER respectively). If the RPC times out, we will ignore the error (since the leader could
  // have successfully submitted the ChangeConfig request and failed to respond before in time)
  // and check the committed config until we find that this master's role has changed, or until we
  // time out which will cause us to tombstone the tablet.
  TOMBSTONE_NOT_OK(rb_client->Finish(),
                   meta,
                   master_->fs_manager()->uuid(),
                   "Remote bootstrap: Failed calling Finish()",
                   nullptr);

  // Synchronous tablet open for "local bootstrap".
  SHUTDOWN_AND_TOMBSTONE_TABLET_PEER_NOT_OK(sys_catalog_->OpenTablet(meta),
                                            sys_catalog_->tablet_peer(),
                                            meta,
                                            master_->fs_manager()->uuid(),
                                            "Remote bootstrap: Failed opening sys catalog",
                                            nullptr);

  // Set up the in-memory master list and also flush the cmeta.
  RETURN_NOT_OK(UpdateMastersListInMemoryAndDisk());

  master_->SetShellMode(false);

  // Call VerifyChangeRoleSucceeded only after we have set shell mode to false. Otherwise,
  // CatalogManager::GetTabletPeer will always return an error, and the consensus will never get
  // updated.
  auto status = rb_client->VerifyChangeRoleSucceeded(
      sys_catalog_->tablet_peer()->shared_consensus());

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Remote bootstrap finished. "
                             << "Failed calling VerifyChangeRoleSucceeded: "
                             << status.ToString();
  } else {
    LOG_WITH_PREFIX(INFO) << "Remote bootstrap finished successfully";
  }

  LOG(INFO) << "Master completed remote bootstrap and is out of shell mode.";

  RETURN_NOT_OK(EnableBgTasks());

  return Status::OK();
}

void CatalogManager::SendAlterTableRequest(const scoped_refptr<TableInfo>& table) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    SendAlterTabletRequest(tablet);
  }
}

void CatalogManager::SendAlterTabletRequest(const scoped_refptr<TabletInfo>& tablet) {
  auto call = std::make_shared<AsyncAlterTable>(master_, worker_pool_.get(), tablet);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send alter table request");
}

void CatalogManager::SendCopartitionTabletRequest(const scoped_refptr<TabletInfo>& tablet,
                                                  const scoped_refptr<TableInfo>& table) {
  auto call = std::make_shared<AsyncCopartitionTable>(master_, worker_pool_.get(), tablet, table);
  table->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send copartition table request");
}

void CatalogManager::DeleteTabletReplicas(
    const TabletInfo* tablet,
    const std::string& msg) {
  TabletInfo::ReplicaMap locations;
  tablet->GetReplicaLocations(&locations);
  LOG(INFO) << "Sending DeleteTablet for " << locations.size()
            << " replicas of tablet " << tablet->tablet_id();
  for (const TabletInfo::ReplicaMap::value_type& r : locations) {
    SendDeleteTabletRequest(tablet->tablet_id(), TABLET_DATA_DELETED,
                            boost::none, tablet->table(), r.second.ts_desc, msg);
  }
}

void CatalogManager::DeleteTabletsAndSendRequests(const scoped_refptr<TableInfo>& table) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  string deletion_msg = "Table deleted at " + LocalTimeAsString();

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    DeleteTabletReplicas(tablet.get(), deletion_msg);

    auto tablet_lock = tablet->LockForWrite();
    tablet_lock->mutable_data()->set_state(SysTabletsEntryPB::DELETED, deletion_msg);
    CHECK_OK(sys_catalog_->UpdateItem(tablet.get(), leader_ready_term_));
    tablet_lock->Commit();
  }
}

void CatalogManager::SendDeleteTabletRequest(
    const TabletId& tablet_id,
    TabletDataState delete_type,
    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
    const scoped_refptr<TableInfo>& table,
    TSDescriptor* ts_desc,
    const string& reason) {
  LOG_WITH_PREFIX(INFO) << Substitute("Deleting tablet $0 on peer $1 "
                                      "with delete type $2 ($3)",
                                      tablet_id, ts_desc->permanent_uuid(),
                                      TabletDataState_Name(delete_type),
                                      reason);
  auto call = std::make_shared<AsyncDeleteReplica>(master_, worker_pool_.get(),
      ts_desc->permanent_uuid(), table, tablet_id, delete_type,
      cas_config_opid_index_less_or_equal, reason);
  if (table != nullptr) {
    table->AddTask(call);
  }

  auto status = call->Run();
  WARN_NOT_OK(status, Substitute("Failed to send delete request for tablet $0", tablet_id));
  if (status.ok()) {
    ts_desc->AddPendingTabletDelete(tablet_id);
  }
}

bool CatalogManager::getLeaderUUID(const scoped_refptr<TabletInfo>& tablet,
                                   TabletServerId* leader_uuid) {
  shared_ptr<TSPicker> replica_picker = std::make_shared<PickLeaderReplica>(tablet);
  TSDescriptor* target_ts_desc;
  Status s = replica_picker->PickReplica(&target_ts_desc);
  if (!s.ok()) {
    return false;
  }
  *leader_uuid = target_ts_desc->permanent_uuid();
  return true;
}

void CatalogManager::SendLeaderStepDownRequest(
    const scoped_refptr<TabletInfo>& tablet, const ConsensusStatePB& cstate,
    const string& change_config_ts_uuid, bool should_remove, const string& new_leader_uuid) {
  auto task = std::make_shared<AsyncTryStepDown>(
      master_, worker_pool_.get(), tablet, cstate, change_config_ts_uuid, should_remove,
      new_leader_uuid);

  tablet->table()->AddTask(task);
  Status status = task->Run();
  WARN_NOT_OK(status, Substitute("Failed to send new $0 request", task->type_name()));
}

// TODO: refactor this into a joint method with the add one.
void CatalogManager::SendRemoveServerRequest(
    const scoped_refptr<TabletInfo>& tablet, const ConsensusStatePB& cstate,
    const string& change_config_ts_uuid) {
  // Check if the user wants the leader to be stepped down.
  auto task = std::make_shared<AsyncRemoveServerTask>(
      master_, worker_pool_.get(), tablet, cstate, change_config_ts_uuid);

  tablet->table()->AddTask(task);
  Status status = task->Run();
  WARN_NOT_OK(status, Substitute("Failed to send new $0 request", task->type_name()));
}

void CatalogManager::SendAddServerRequest(
    const scoped_refptr<TabletInfo>& tablet, RaftPeerPB::MemberType member_type,
    const ConsensusStatePB& cstate, const string& change_config_ts_uuid) {
  auto task = std::make_shared<AsyncAddServerTask>(master_, worker_pool_.get(), tablet, member_type,
      cstate, change_config_ts_uuid);
  tablet->table()->AddTask(task);
  Status status = task->Run();
  WARN_NOT_OK(status, Substitute("Failed to send AddServer of tserver $0 to tablet $1",
                                 change_config_ts_uuid, tablet.get()->ToString()));

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  if (status.ok())
    LOG(INFO) << "Started AddServer task: " << task->description();
}

void CatalogManager::GetPendingServerTasksUnlocked(const TableId &table_uuid,
    TabletToTabletServerMap *add_replica_tasks_map,
    TabletToTabletServerMap *remove_replica_tasks_map,
    TabletToTabletServerMap *stepdown_leader_tasks) {

  auto table = GetTableInfoUnlocked(table_uuid);
  for (const auto& task : table->GetTasks()) {
    TabletToTabletServerMap* outputMap = nullptr;
    TabletId tablet_id;
    if (task->type() == MonitoredTask::ASYNC_ADD_SERVER) {
      outputMap = add_replica_tasks_map;
    } else if (task->type() == MonitoredTask::ASYNC_REMOVE_SERVER) {
      outputMap = remove_replica_tasks_map;
    } else if (task->type() == MonitoredTask::ASYNC_TRY_STEP_DOWN) {
      outputMap = stepdown_leader_tasks;
    }
    if (outputMap) {
      auto raft_task = static_cast<CommonInfoForRaftTask*>(task.get());
      (*outputMap)[raft_task->tablet_id()] = raft_task->change_config_ts_uuid();
    }
  }
}

void CatalogManager::ExtractTabletsToProcess(
    TabletInfos *tablets_to_delete,
    TabletInfos *tablets_to_process) {
  boost::shared_lock<LockType> l(lock_);

  // TODO: At the moment we loop through all the tablets
  //       we can keep a set of tablets waiting for "assignment"
  //       or just a counter to avoid to take the lock and loop through the tablets
  //       if everything is "stable".

  for (const TabletInfoMap::value_type& entry : tablet_map_) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    auto tablet_lock = tablet->LockForRead();

    if (!tablet->table()) {
      // Tablet is orphaned or in preparing state, continue.
      continue;
    }

    auto table_lock = tablet->table()->LockForRead();

    // If the table is deleted or the tablet was replaced at table creation time.
    if (tablet_lock->data().is_deleted() || table_lock->data().started_deleting()) {
      tablets_to_delete->push_back(tablet);
      continue;
    }

    // Running tablets.
    if (tablet_lock->data().is_running()) {
      // TODO: handle last update > not responding timeout?
      continue;
    }

    // Tablets not yet assigned or with a report just received.
    tablets_to_process->push_back(tablet);
  }
}

struct DeferredAssignmentActions {
  vector<TabletInfo*> tablets_to_add;
  vector<TabletInfo*> tablets_to_update;
  vector<TabletInfo*> needs_create_rpc;
};

void CatalogManager::HandleAssignPreparingTablet(TabletInfo* tablet,
                                                 DeferredAssignmentActions* deferred) {
  // The tablet was just created (probably by a CreateTable RPC).
  // Update the state to "creating" to be ready for the creation request.
  tablet->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::CREATING, "Sending initial creation of tablet");
  deferred->tablets_to_update.push_back(tablet);
  deferred->needs_create_rpc.push_back(tablet);
  VLOG(1) << "Assign new tablet " << tablet->ToString();
}

void CatalogManager::HandleAssignCreatingTablet(TabletInfo* tablet,
                                                DeferredAssignmentActions* deferred,
                                                vector<scoped_refptr<TabletInfo>>* new_tablets) {
  MonoDelta time_since_updated =
      MonoTime::Now().GetDeltaSince(tablet->last_update_time());
  int64_t remaining_timeout_ms =
      FLAGS_tablet_creation_timeout_ms - time_since_updated.ToMilliseconds();

  // Skip the tablet if the assignment timeout is not yet expired.
  if (remaining_timeout_ms > 0) {
    VLOG(2) << "Tablet " << tablet->ToString() << " still being created. "
            << remaining_timeout_ms << "ms remain until timeout.";
    return;
  }

  const PersistentTabletInfo& old_info = tablet->metadata().state();

  // The "tablet creation" was already sent, but we didn't receive an answer
  // within the timeout. So the tablet will be replaced by a new one.
  TabletInfo *replacement = CreateTabletInfo(tablet->table().get(),
                                             old_info.pb.partition());
  LOG(WARNING) << "Tablet " << tablet->ToString() << " was not created within "
               << "the allowed timeout. Replacing with a new tablet "
               << replacement->tablet_id();

  tablet->table()->AddTablet(replacement);
  {
    std::lock_guard<LockType> l_maps(lock_);
    tablet_map_[replacement->tablet_id()] = replacement;
  }

  // Mark old tablet as replaced.
  tablet->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::REPLACED,
    Substitute("Replaced by $0 at $1",
               replacement->tablet_id(), LocalTimeAsString()));

  // Mark new tablet as being created.
  replacement->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::CREATING,
    Substitute("Replacement for $0", tablet->tablet_id()));

  deferred->tablets_to_update.push_back(tablet);
  deferred->tablets_to_add.push_back(replacement);
  deferred->needs_create_rpc.push_back(replacement);
  VLOG(1) << "Replaced tablet " << tablet->tablet_id()
          << " with " << replacement->tablet_id()
          << " (table " << tablet->table()->ToString() << ")";

  new_tablets->push_back(replacement);
}

// TODO: we could batch the IO onto a background thread.
//       but this is following the current HandleReportedTablet().
Status CatalogManager::HandleTabletSchemaVersionReport(TabletInfo *tablet, uint32_t version) {
  // Update the schema version if it's the latest.
  tablet->set_reported_schema_version(version);

  // Verify if it's the last tablet report, and the alter completed.
  TableInfo *table = tablet->table().get();
  auto l = table->LockForWrite();
  if (l->data().pb.state() != SysTablesEntryPB::ALTERING) {
    return Status::OK();
  }

  uint32_t current_version = l->data().pb.version();
  if (table->IsAlterInProgress(current_version)) {
    return Status::OK();
  }

  // Update the state from altering to running and remove the last fully
  // applied schema (if it exists).
  l->mutable_data()->pb.clear_fully_applied_schema();
  l->mutable_data()->set_state(SysTablesEntryPB::RUNNING,
                              Substitute("Current schema version=$0", current_version));

  Status s = sys_catalog_->UpdateItem(table, leader_ready_term_);
  if (!s.ok()) {
    LOG(WARNING) << "An error occurred while updating sys-tables: " << s.ToString();
    return s;
  }

  l->Commit();
  LOG(INFO) << table->ToString() << " - Alter table completed version=" << current_version;
  return Status::OK();
}

// Helper class to commit TabletInfo mutations at the end of a scope.
namespace {

class ScopedTabletInfoCommitter {
 public:
  explicit ScopedTabletInfoCommitter(const TabletInfos* tablets)
    : tablets_(DCHECK_NOTNULL(tablets)),
      aborted_(false) {
  }

  // This method is not thread safe. Must be called by the same thread
  // that would destroy this instance.
  void Abort() {
    for (const scoped_refptr<TabletInfo>& tablet : *tablets_) {
      tablet->mutable_metadata()->AbortMutation();
    }
    aborted_ = true;
  }

  // Commit the transactions.
  ~ScopedTabletInfoCommitter() {
    if (PREDICT_TRUE(!aborted_)) {
      for (const scoped_refptr<TabletInfo>& tablet : *tablets_) {
        tablet->mutable_metadata()->CommitMutation();
      }
    }
  }

 private:
  const TabletInfos* tablets_;
  bool aborted_;
};
}  // anonymous namespace

Status CatalogManager::ProcessPendingAssignments(const TabletInfos& tablets) {
  VLOG(1) << "Processing pending assignments";

  // Take write locks on all tablets to be processed, and ensure that they are
  // unlocked at the end of this scope.
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    tablet->mutable_metadata()->StartMutation();
  }
  ScopedTabletInfoCommitter unlocker_in(&tablets);

  // Any tablets created by the helper functions will also be created in a
  // locked state, so we must ensure they are unlocked before we return to
  // avoid deadlocks.
  TabletInfos new_tablets;
  ScopedTabletInfoCommitter unlocker_out(&new_tablets);

  DeferredAssignmentActions deferred;

  // Iterate over each of the tablets and handle it, whatever state
  // it may be in. The actions required for the tablet are collected
  // into 'deferred'.
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    SysTabletsEntryPB::State t_state = tablet->metadata().state().pb.state();

    switch (t_state) {
      case SysTabletsEntryPB::PREPARING:
        HandleAssignPreparingTablet(tablet.get(), &deferred);
        break;

      case SysTabletsEntryPB::CREATING:
        HandleAssignCreatingTablet(tablet.get(), &deferred, &new_tablets);
        break;

      default:
        VLOG(2) << "Nothing to do for tablet " << tablet->tablet_id() << ": state = "
                << SysTabletsEntryPB_State_Name(t_state);
        break;
    }
  }

  // Nothing to do.
  if (deferred.tablets_to_add.empty() &&
      deferred.tablets_to_update.empty() &&
      deferred.needs_create_rpc.empty()) {
    return Status::OK();
  }

  // For those tablets which need to be created in this round, assign replicas.
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs, blacklistState.tservers_);
  Status s;
  for (TabletInfo *tablet : deferred.needs_create_rpc) {
    // NOTE: if we fail to select replicas on the first pass (due to
    // insufficient Tablet Servers being online), we will still try
    // again unless the tablet/table creation is cancelled.
    s = SelectReplicasForTablet(ts_descs, tablet);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute(
          "An error occured while selecting replicas for tablet $0: $1",
          tablet->tablet_id(), s.ToString()));
      tablet->table()->SetCreateTableErrorStatus(s);
      break;
    }
  }

  // Update the sys catalog with the new set of tablets/metadata.
  if (s.ok()) {
    s = sys_catalog_->AddAndUpdateItems(deferred.tablets_to_add,
                                        deferred.tablets_to_update,
                                        leader_ready_term_);
    if (!s.ok()) {
      s = s.CloneAndPrepend("An error occurred while persisting the updated tablet metadata");
    }
  }

  if (!s.ok()) {
    LOG(WARNING) << "Aborting the current task due to error: " << s.ToString();
    // If there was an error, abort any mutations started by the
    // current task.
    vector<string> tablet_ids_to_remove;
    for (scoped_refptr<TabletInfo>& new_tablet : new_tablets) {
      TableInfo* table = new_tablet->table().get();
      auto l_table = table->LockForRead();
      if (table->RemoveTablet(
          new_tablet->metadata().dirty().pb.partition().partition_key_start())) {
        VLOG(1) << "Removed tablet " << new_tablet->tablet_id() << " from "
            "table " << l_table->data().name();
      }
      tablet_ids_to_remove.push_back(new_tablet->tablet_id());
    }
    std::lock_guard<LockType> l(lock_);
    unlocker_out.Abort();
    unlocker_in.Abort();
    for (const TabletId& tablet_id_to_remove : tablet_ids_to_remove) {
      CHECK_EQ(tablet_map_.erase(tablet_id_to_remove), 1)
          << "Unable to erase " << tablet_id_to_remove << " from tablet map.";
    }
    return s;
  }

  // Send DeleteTablet requests to tablet servers serving deleted tablets.
  // This is asynchronous / non-blocking.
  for (const TabletInfo* tablet : deferred.tablets_to_update) {
    if (tablet->metadata().dirty().is_deleted()) {
      DeleteTabletReplicas(tablet, tablet->metadata().dirty().pb.state_msg());
    }
  }
  // Send the CreateTablet() requests to the servers. This is asynchronous / non-blocking.
  SendCreateTabletRequests(deferred.needs_create_rpc);
  return Status::OK();
}

Status CatalogManager::SelectReplicasForTablet(const TSDescriptorVector& ts_descs,
                                               TabletInfo* tablet) {
  auto table_guard = tablet->table()->LockForRead();

  if (!table_guard->data().pb.IsInitialized()) {
    return STATUS(InvalidArgument,
        Substitute("TableInfo for tablet $0 is not initialized (aborted CreateTable attempt?)",
                   tablet->tablet_id()));
  }

  // Validate that we do not have placement blocks in both cluster and table data.
  RETURN_NOT_OK(ValidateTableReplicationInfo(table_guard->data().pb.replication_info()));

  // Default to the cluster placement object.
  ReplicationInfoPB replication_info;
  {
    auto l = cluster_config_->LockForRead();
    replication_info = l->data().pb.replication_info();
  }

  // Select the set of replicas for the tablet.
  ConsensusStatePB* cstate = tablet->mutable_metadata()->mutable_dirty()
          ->pb.mutable_committed_consensus_state();
  cstate->set_current_term(kMinimumTerm);
  consensus::RaftConfigPB *config = cstate->mutable_config();
  config->set_opid_index(consensus::kInvalidOpIdIndex);

  // TODO: we do this defaulting to cluster if no table data in two places, should refactor and
  // have a centralized getter, that will ultimately do the subsetting as well.

  Status s = HandlePlacementUsingReplicationInfo(replication_info, ts_descs, config);
  if (!s.ok()) {
    return s;
  }

  std::ostringstream out;
  out << Substitute("Initial tserver uuids for tablet $0: ", tablet->tablet_id());
  for (const RaftPeerPB& peer : config->peers()) {
    out << peer.permanent_uuid() << " ";
  }

  if (VLOG_IS_ON(0)) {
    out.str();
  }

  return Status::OK();
}

Status CatalogManager::HandlePlacementUsingReplicationInfo(
    const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& all_ts_descs,
    consensus::RaftConfigPB* config) {
  return HandlePlacementUsingPlacementInfo(replication_info.live_replicas(),
                                           all_ts_descs, RaftPeerPB::VOTER, config);
}

Status CatalogManager::HandlePlacementUsingPlacementInfo(const PlacementInfoPB& placement_info,
                                                         const TSDescriptorVector& ts_descs,
                                                         RaftPeerPB::MemberType member_type,
                                                         consensus::RaftConfigPB* config) {
  int nreplicas = GetNumReplicasFromPlacementInfo(placement_info);
  if (ts_descs.size() < nreplicas) {
    return STATUS(InvalidArgument, Substitute(
        "Not enough tablet servers in the requested placements. Need at least $0, have $1",
        nreplicas, ts_descs.size()));
  }
  // Keep track of servers we've already selected, so that we don't attempt to
  // put two replicas on the same host.
  set<shared_ptr<TSDescriptor>> already_selected_ts;
  if (placement_info.placement_blocks().empty()) {
    // If we don't have placement info, just place the replicas as before, distributed across the
    // whole cluster.
    SelectReplicas(ts_descs, nreplicas, config, &already_selected_ts, member_type);
  } else {
    // TODO(bogdan): move to separate function
    //
    // If we do have placement info, we'll try to use the same power of two algorithm, but also
    // match the requested policies. We'll assign the minimum requested replicas in each combination
    // of cloud.region.zone and then if we still have leftover replicas, we'll assign those
    // in any of the allowed areas.
    unordered_map<string, vector<shared_ptr<TSDescriptor>>> allowed_ts_by_pi;
    vector<shared_ptr<TSDescriptor>> all_allowed_ts;

    // Keep map from ID to PlacementBlockPB, as protos only have repeated, not maps.
    unordered_map<string, PlacementBlockPB> pb_by_id;
    for (const auto& pb : placement_info.placement_blocks()) {
      const auto& cloud_info = pb.cloud_info();
      string placement_id = TSDescriptor::generate_placement_id(cloud_info);
      pb_by_id[placement_id] = pb;
    }

    // Build the sets of allowed TSs.
    for (const auto& ts : ts_descs) {
      bool added_to_all = false;
      for (const auto& pi_entry : pb_by_id) {
        if (ts->MatchesCloudInfo(pi_entry.second.cloud_info())) {
          allowed_ts_by_pi[pi_entry.first].push_back(ts);

          if (!added_to_all) {
            added_to_all = true;
            all_allowed_ts.push_back(ts);
          }
        }
      }
    }

    // Fail early if we don't have enough tablet servers in the areas requested.
    if (all_allowed_ts.size() < nreplicas) {
      return STATUS(InvalidArgument, Substitute(
          "Not enough tablet servers in the requested placements. Need at least $0, have $1",
          nreplicas, all_allowed_ts.size()));
    }

    // Loop through placements and assign to respective available TSs.
    for (const auto& entry : allowed_ts_by_pi) {
      const auto& available_ts_descs = entry.second;
      int num_replicas = pb_by_id[entry.first].min_num_replicas();
      num_replicas = num_replicas > 0 ? num_replicas : FLAGS_replication_factor;
      if (available_ts_descs.size() < num_replicas) {
        return STATUS(InvalidArgument, Substitute(
            "Not enough tablet servers in $0. Need at least $1 but only have $2.", entry.first,
            num_replicas, available_ts_descs.size()));
      }
      SelectReplicas(available_ts_descs, num_replicas, config, &already_selected_ts, member_type);
    }

    int replicas_left = nreplicas - already_selected_ts.size();
    DCHECK_GE(replicas_left, 0);
    if (replicas_left > 0) {
      // No need to do an extra check here, as we checked early if we have enough to cover all
      // requested placements and checked individually per placement info, if we could cover the
      // minimums.
      SelectReplicas(all_allowed_ts, replicas_left, config, &already_selected_ts, member_type);
    }
  }
  return Status::OK();
}

void CatalogManager::SendCreateTabletRequests(const vector<TabletInfo*>& tablets) {
  for (TabletInfo *tablet : tablets) {
    const consensus::RaftConfigPB& config =
        tablet->metadata().dirty().pb.committed_consensus_state().config();
    tablet->set_last_update_time(MonoTime::Now());
    for (const RaftPeerPB& peer : config.peers()) {
      auto task = std::make_shared<AsyncCreateReplica>(master_, worker_pool_.get(),
          peer.permanent_uuid(), tablet);
      tablet->table()->AddTask(task);
      WARN_NOT_OK(task->Run(), "Failed to send new tablet request");
    }
  }
}

shared_ptr<TSDescriptor> CatalogManager::PickBetterReplicaLocation(
    const TSDescriptorVector& two_choices) {
  DCHECK_EQ(two_choices.size(), 2);

  const auto& a = two_choices[0];
  const auto& b = two_choices[1];

  // When creating replicas, we consider two aspects of load:
  //   (1) how many tablet replicas are already on the server, and
  //   (2) how often we've chosen this server recently.
  //
  // The first factor will attempt to put more replicas on servers that
  // are under-loaded (eg because they have newly joined an existing cluster, or have
  // been reformatted and re-joined).
  //
  // The second factor will ensure that we take into account the recent selection
  // decisions even if those replicas are still in the process of being created (and thus
  // not yet reported by the server). This is important because, while creating a table,
  // we batch the selection process before sending any creation commands to the
  // servers themselves.
  //
  // TODO: in the future we may want to factor in other items such as available disk space,
  // actual request load, etc.
  double load_a = a->RecentReplicaCreations() + a->num_live_replicas();
  double load_b = b->RecentReplicaCreations() + b->num_live_replicas();
  if (load_a < load_b) {
    return a;
  } else if (load_b < load_a) {
    return b;
  } else {
    // If the load is the same, we can just pick randomly.
    return two_choices[rng_.Uniform(2)];
  }
}

shared_ptr<TSDescriptor> CatalogManager::SelectReplica(
    const TSDescriptorVector& ts_descs,
    const set<shared_ptr<TSDescriptor>>& excluded) {
  // The replica selection algorithm follows the idea from
  // "Power of Two Choices in Randomized Load Balancing"[1]. For each replica,
  // we randomly select two tablet servers, and then assign the replica to the
  // less-loaded one of the two. This has some nice properties:
  //
  // 1) because the initial selection of two servers is random, we get good
  //    spreading of replicas across the cluster. In contrast if we sorted by
  //    load and always picked under-loaded servers first, we'd end up causing
  //    all tablets of a new table to be placed on an empty server. This wouldn't
  //    give good load balancing of that table.
  //
  // 2) because we pick the less-loaded of two random choices, we do end up with a
  //    weighting towards filling up the underloaded one over time, without
  //    the extreme scenario above.
  //
  // 3) because we don't follow any sequential pattern, every server is equally
  //    likely to replicate its tablets to every other server. In contrast, a
  //    round-robin design would enforce that each server only replicates to its
  //    adjacent nodes in the TS sort order, limiting recovery bandwidth (see
  //    KUDU-1317).
  //
  // [1] http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.pdf

  // Pick two random servers, excluding those we've already picked.
  // If we've only got one server left, 'two_choices' will actually
  // just contain one element.
  vector<shared_ptr<TSDescriptor>> two_choices;
  rng_.ReservoirSample(ts_descs, 2, excluded, &two_choices);

  if (two_choices.size() == 2) {
    // Pick the better of the two.
    return PickBetterReplicaLocation(two_choices);
  }

  // If we couldn't randomly sample two servers, it's because we only had one
  // more non-excluded choice left.
  CHECK_EQ(1, two_choices.size()) << "ts_descs: " << ts_descs.size()
                                  << " already_sel: " << excluded.size();
  return two_choices[0];
}

void CatalogManager::SelectReplicas(
    const TSDescriptorVector& ts_descs, int nreplicas, consensus::RaftConfigPB* config,
    set<shared_ptr<TSDescriptor>>* already_selected_ts, RaftPeerPB::MemberType member_type) {
  DCHECK_LE(nreplicas, ts_descs.size());

  for (int i = 0; i < nreplicas; ++i) {
    // We have to derefence already_selected_ts here, as the inner mechanics uses ReservoirSample,
    // which in turn accepts only a reference to the set, not a pointer. Alternatively, we could
    // have passed it in as a non-const reference, but that goes against our argument passing
    // convention.
    //
    // TODO(bogdan): see if we indeed want to switch back to non-const reference.
    shared_ptr<TSDescriptor> ts = SelectReplica(ts_descs, *already_selected_ts);
    InsertOrDie(already_selected_ts, ts);

    // Increment the number of pending replicas so that we take this selection into
    // account when assigning replicas for other tablets of the same table. This
    // value decays back to 0 over time.
    ts->IncrementRecentReplicaCreations();

    TSRegistrationPB reg;
    ts->GetRegistration(&reg);

    RaftPeerPB *peer = config->add_peers();
    peer->set_permanent_uuid(ts->permanent_uuid());

    // TODO: This is temporary, we will use only UUIDs.
    TakeRegistration(reg.mutable_common(), peer);
    peer->set_member_type(member_type);
  }
}

Status CatalogManager::ConsensusStateToTabletLocations(const consensus::ConsensusStatePB& cstate,
                                                       TabletLocationsPB* locs_pb) {
  for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
    TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
    if (!peer.has_permanent_uuid()) {
      return STATUS_SUBSTITUTE(IllegalState, "Missing UUID $0", peer.ShortDebugString());
    }
    replica_pb->set_role(GetConsensusRole(peer.permanent_uuid(), cstate));
    if (peer.has_member_type()) {
      replica_pb->set_member_type(peer.member_type());
    } else {
      replica_pb->set_member_type(RaftPeerPB::UNKNOWN_MEMBER_TYPE);
    }
    TSInfoPB* tsinfo_pb = replica_pb->mutable_ts_info();
    tsinfo_pb->set_permanent_uuid(peer.permanent_uuid());
    CopyRegistration(peer, tsinfo_pb);
  }
  return Status::OK();
}

Status CatalogManager::BuildLocationsForTablet(const scoped_refptr<TabletInfo>& tablet,
                                               TabletLocationsPB* locs_pb) {
  {
    auto l_tablet = tablet->LockForRead();
    locs_pb->set_table_id(l_tablet->data().pb.table_id());
    *locs_pb->mutable_table_ids() = l_tablet->data().pb.table_ids();
  }

  // For system tables, the set of replicas is always the set of masters.
  if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
    consensus::ConsensusStatePB master_consensus;
    RETURN_NOT_OK(GetCurrentConfig(&master_consensus));
    locs_pb->set_tablet_id(tablet->tablet_id());
    locs_pb->set_stale(false);
    RETURN_NOT_OK(ConsensusStateToTabletLocations(master_consensus, locs_pb));
    return Status::OK();
  }

  TSRegistrationPB reg;

  TabletInfo::ReplicaMap locs;
  consensus::ConsensusStatePB cstate;
  {
    auto l_tablet = tablet->LockForRead();
    if (PREDICT_FALSE(l_tablet->data().is_deleted())) {
      return STATUS(NotFound, "Tablet deleted", l_tablet->data().pb.state_msg());
    }

    if (PREDICT_FALSE(!l_tablet->data().is_running())) {
      return STATUS(ServiceUnavailable, "Tablet not running");
    }

    tablet->GetReplicaLocations(&locs);
    if (locs.empty() && l_tablet->data().pb.has_committed_consensus_state()) {
      cstate = l_tablet->data().pb.committed_consensus_state();
    }

    locs_pb->mutable_partition()->CopyFrom(tablet->metadata().state().pb.partition());
  }

  locs_pb->set_tablet_id(tablet->tablet_id());
  locs_pb->set_stale(locs.empty());

  // If the locations are cached.
  if (!locs.empty()) {
    if (cstate.IsInitialized() && locs.size() != cstate.config().peers_size()) {
      LOG(WARNING) << "Cached tablet replicas " << locs.size() << " does not match consensus "
                   << cstate.config().peers_size();
    }

    for (const TabletInfo::ReplicaMap::value_type& replica : locs) {
      TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
      replica_pb->set_role(replica.second.role);
      replica_pb->set_member_type(replica.second.member_type);
      TSInformationPB tsinfo_pb;
      replica.second.ts_desc->GetTSInformationPB(&tsinfo_pb);

      replica_pb->mutable_ts_info()->set_permanent_uuid(
          tsinfo_pb.tserver_instance().permanent_uuid());
      TakeRegistration(
          tsinfo_pb.mutable_registration()->mutable_common(), replica_pb->mutable_ts_info());
      replica_pb->mutable_ts_info()->set_placement_uuid(
          tsinfo_pb.registration().common().placement_uuid());
    }
    return Status::OK();
  }

  // If the locations were not cached.
  // TODO: Why would this ever happen? See KUDU-759.
  if (cstate.IsInitialized()) {
    RETURN_NOT_OK(ConsensusStateToTabletLocations(cstate, locs_pb));
  }

  return Status::OK();
}

Result<shared_ptr<tablet::AbstractTablet>> CatalogManager::GetSystemTablet(const TabletId& id) {
  RETURN_NOT_OK(CheckOnline());

  const auto iter = system_tablets_.find(id);
  if (iter == system_tablets_.end()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid system tablet id", id);
  }
  return iter->second;
}

Status CatalogManager::GetTabletLocations(const TabletId& tablet_id, TabletLocationsPB* locs_pb) {
  RETURN_NOT_OK(CheckOnline());

  locs_pb->mutable_replicas()->Clear();
  scoped_refptr<TabletInfo> tablet_info;
  {
    boost::shared_lock<LockType> l(lock_);
    if (!FindCopy(tablet_map_, tablet_id, &tablet_info)) {
      return STATUS(NotFound, Substitute("Unknown tablet $0", tablet_id));
    }
  }

  Status s = BuildLocationsForTablet(tablet_info, locs_pb);

  int num_replicas = 0;
  if (GetReplicationFactor(&num_replicas).ok() && num_replicas > 0 &&
      locs_pb->replicas().size() != num_replicas) {
    YB_LOG_EVERY_N(WARNING, 100) << "Expected replicas " << num_replicas << " but found "
        << locs_pb->replicas().size() << " for tablet " << tablet_id;
  }

  return s;
}

Status CatalogManager::GetTableLocations(const GetTableLocationsRequestPB* req,
                                         GetTableLocationsResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  // If start-key is > end-key report an error instead of swap the two
  // since probably there is something wrong app-side.
  if (req->has_partition_key_start() && req->has_partition_key_end()
      && req->partition_key_start() > req->partition_key_end()) {
    return STATUS(InvalidArgument, "start partition key is greater than the end partition key");
  }

  if (req->max_returned_locations() <= 0) {
    return STATUS(InvalidArgument, "max_returned_locations must be greater than 0");
  }

  scoped_refptr<TableInfo> table;
  RETURN_NOT_OK(FindTable(req->table(), &table));

  if (table == nullptr) {
    Status s = STATUS(NotFound, "The table does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  auto l = table->LockForRead();
  if (l->data().started_deleting()) {
    Status s = STATUS(NotFound, "The table was deleted",
                                l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  if (!l->data().is_running()) {
    Status s = STATUS(ServiceUnavailable, "The table is not running");
    return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
  }

  vector<scoped_refptr<TabletInfo>> tablets_in_range;
  table->GetTabletsInRange(req, &tablets_in_range);

  bool require_tablets_runnings = req->require_tablets_running();
  for (const scoped_refptr<TabletInfo>& tablet : tablets_in_range) {
    auto status = BuildLocationsForTablet(tablet, resp->add_tablet_locations());
    if (!status.ok()) {
      // Not running.
      if (require_tablets_runnings) {
        resp->mutable_tablet_locations()->Clear();
        return SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, status);
      }
      resp->mutable_tablet_locations()->RemoveLast();
    }
  }

  resp->set_table_type(table->metadata().state().pb.table_type());

  return Status::OK();
}

Status CatalogManager::GetCurrentConfig(consensus::ConsensusStatePB* cpb) const {
  if (!sys_catalog_->tablet_peer() || !sys_catalog_->tablet_peer()->consensus()) {
    std::string uuid = master_->fs_manager()->uuid();
    return STATUS_FORMAT(IllegalState, "Node $0 peer not initialized.", uuid);
  }

  Consensus* consensus = sys_catalog_->tablet_peer()->consensus();
  *cpb = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);

  return Status::OK();
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  NamespaceInfoMap namespace_ids_copy;
  TableInfoMap ids_copy;
  TableInfoByNameMap names_copy;
  TabletInfoMap tablets_copy;

  // Copy the internal state so that, if the output stream blocks,
  // we don't end up holding the lock for a long time.
  {
    boost::shared_lock<LockType> l(lock_);
    namespace_ids_copy = namespace_ids_map_;
    ids_copy = table_ids_map_;
    names_copy = table_names_map_;
    tablets_copy = tablet_map_;
  }

  *out << "Dumping Current state of master.\nNamespaces:\n";
  for (const NamespaceInfoMap::value_type& e : namespace_ids_copy) {
    NamespaceInfo* t = e.second.get();
    auto l = t->LockForRead();
    const NamespaceName& name = l->data().name();

    *out << t->id() << ":\n";
    *out << "  name: \"" << strings::CHexEscape(name) << "\"\n";
    *out << "  metadata: " << l->data().pb.ShortDebugString() << "\n";
  }

  *out << "Tables:\n";
  for (const TableInfoMap::value_type& e : ids_copy) {
    TableInfo* t = e.second.get();
    auto l = t->LockForRead();
    const TableName& name = l->data().name();
    const NamespaceId& namespace_id = l->data().namespace_id();
    // Find namespace by its ID.
    scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_copy, namespace_id);

    *out << t->id() << ":\n";
    *out << "  namespace id: \"" << strings::CHexEscape(namespace_id) << "\"\n";

    if (ns != nullptr) {
      *out << "  namespace name: \"" << strings::CHexEscape(ns->name()) << "\"\n";
    }

    *out << "  name: \"" << strings::CHexEscape(name) << "\"\n";
    // Erase from the map, so later we can check that we don't have
    // any orphaned tables in the by-name map that aren't in the
    // by-id map.
    if (names_copy.erase({namespace_id, name}) != 1) {
      *out << "  [not present in by-name map]\n";
    }
    *out << "  metadata: " << l->data().pb.ShortDebugString() << "\n";

    *out << "  tablets:\n";

    vector<scoped_refptr<TabletInfo>> table_tablets;
    t->GetAllTablets(&table_tablets);
    for (const scoped_refptr<TabletInfo>& tablet : table_tablets) {
      auto l_tablet = tablet->LockForRead();
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet->data().pb.ShortDebugString() << "\n";

      if (tablets_copy.erase(tablet->tablet_id()) != 1) {
        *out << "  [ERROR: not present in CM tablet map!]\n";
      }
    }
  }

  if (!tablets_copy.empty()) {
    *out << "Orphaned tablets (not referenced by any table):\n";
    for (const TabletInfoMap::value_type& entry : tablets_copy) {
      const scoped_refptr<TabletInfo>& tablet = entry.second;
      auto l_tablet = tablet->LockForRead();
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet->data().pb.ShortDebugString() << "\n";
    }
  }

  if (!names_copy.empty()) {
    *out << "Orphaned tables (in by-name map, but not id map):\n";
    for (const TableInfoByNameMap::value_type& e : names_copy) {
      *out << e.second->id() << ":\n";
      *out << "  namespace id: \"" << strings::CHexEscape(e.first.first) << "\"\n";
      *out << "  name: \"" << CHexEscape(e.first.second) << "\"\n";
    }
  }

  master_->DumpMasterOptionsInfo(out);

  if (on_disk_dump) {
    consensus::ConsensusStatePB cur_consensus_state;
    // TODO: proper error handling below.
    CHECK_OK(GetCurrentConfig(&cur_consensus_state));
    *out << "Current raft config: " << cur_consensus_state.ShortDebugString() << "\n";
  }
}

Status CatalogManager::PeerStateDump(const vector<RaftPeerPB>& peers, bool on_disk) {
  std::unique_ptr<MasterServiceProxy> peer_proxy;
  Endpoint sockaddr;
  MonoTime timeout = MonoTime::Now();
  DumpMasterStateRequestPB req;
  rpc::RpcController rpc;

  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
  rpc.set_deadline(timeout);
  req.set_on_disk(on_disk);

  for (const RaftPeerPB& peer : peers) {
    HostPort hostport = HostPortFromPB(DesiredHostPort(peer, master_->MakeCloudInfoPB()));
    peer_proxy.reset(new MasterServiceProxy(&master_->proxy_cache(), hostport));

    DumpMasterStateResponsePB resp;
    rpc.Reset();

    peer_proxy->DumpState(req, &resp, &rpc);

    if (resp.has_error()) {
      LOG(WARNING) << "Hit err " << resp.ShortDebugString() << " during peer "
        << peer.ShortDebugString() << " state dump.";
      return StatusFromPB(resp.error().status());
    }
  }

  return Status::OK();
}

void CatalogManager::ReportMetrics() {
  // Report metrics on how many tservers are alive.
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  metric_num_tablet_servers_live_->set_value(ts_descs.size());
}

std::string CatalogManager::LogPrefix() const {
  if (tablet_peer()) {
    return Substitute("T $0 P $1: ", tablet_peer()->tablet_id(), tablet_peer()->permanent_uuid());
  } else {
    return Substitute("T $0 P $1: ", kSysCatalogTabletId, master_->fs_manager()->uuid());
  }
}

void CatalogManager::SetLoadBalancerEnabled(bool is_enabled) {
  load_balance_policy_->SetLoadBalancerEnabled(is_enabled);
}

Status CatalogManager::GoIntoShellMode() {
  if (master_->IsShellMode()) {
    return STATUS(IllegalState, "Master is already in shell mode.");
  }

  LOG(INFO) << "Starting going into shell mode.";
  master_->SetShellMode(true);

  {
    std::lock_guard<LockType> l(lock_);
    RETURN_NOT_OK(sys_catalog_->GoIntoShellMode());
    background_tasks_->Shutdown();
    background_tasks_.reset();
  }
  {
    std::lock_guard<std::mutex> l(remote_bootstrap_mtx_);
    tablet_exists_ = false;
  }

  LOG(INFO) << "Done going into shell mode.";

  return Status::OK();
}

Status CatalogManager::GetClusterConfig(GetMasterClusterConfigResponsePB* resp) {
  return GetClusterConfig(resp->mutable_cluster_config());
}

Status CatalogManager::GetClusterConfig(SysClusterConfigEntryPB* config) {
  DCHECK(cluster_config_) << "Missing cluster config for master!";
  auto l = cluster_config_->LockForRead();
  *config = l->data().pb;
  return Status::OK();
}

Status CatalogManager::SetBlackList(const BlacklistPB& blacklist) {
  if (!blacklistState.tservers_.empty()) {
    LOG(WARNING) << Substitute("Overwriting $0 with new size $1 and initial load $2.",
                               blacklistState.ToString(), blacklist.hosts_size(),
                               blacklist.initial_replica_load());
    blacklistState.Reset();
  }

  LOG(INFO) << "Set blacklist size = " << blacklist.hosts_size() << " with load "
            << blacklist.initial_replica_load() << " for num_tablets = " << tablet_map_.size();

  for (const auto& pb : blacklist.hosts()) {
    blacklistState.tservers_.insert(HostPortFromPB(pb));
  }

  return Status::OK();
}

Status CatalogManager::SetClusterConfig(
    const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp) {
  SysClusterConfigEntryPB config(req->cluster_config());

  // Save the list of blacklisted servers to be used for completion checking.
  if (config.has_server_blacklist()) {
    RETURN_NOT_OK(SetBlackList(config.server_blacklist()));

    config.mutable_server_blacklist()->set_initial_replica_load(GetNumBlacklistReplicas());
    blacklistState.initial_load_ = config.server_blacklist().initial_replica_load();
  }

  auto l = cluster_config_->LockForWrite();
  // We should only set the config, if the caller provided us with a valid update to the
  // existing config.
  if (l->data().pb.version() != config.version()) {
    Status s = STATUS(IllegalState, Substitute(
      "Config version does not match, got $0, but most recent one is $1. Should call Get again",
      config.version(), l->data().pb.version()));
    return SetupError(resp->mutable_error(), MasterErrorPB::CONFIG_VERSION_MISMATCH, s);
  }

  if (config.cluster_uuid() != l->data().pb.cluster_uuid()) {
    Status s = STATUS(InvalidArgument, "Config cluster UUID cannot be updated");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
  }

  // TODO(bogdan): should this live here?
  const ReplicationInfoPB& replication_info = config.replication_info();
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    if (!replication_info.read_replicas(i).has_placement_uuid()) {
      Status s = STATUS(IllegalState,
                        "All read-only clusters must have a placement uuid specified");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
    }
  }

  l->mutable_data()->pb.CopyFrom(config);
  // Bump the config version, to indicate an update.
  l->mutable_data()->pb.set_version(config.version() + 1);

  LOG(INFO) << "Updating cluster config to " << config.version() + 1;

  RETURN_NOT_OK(sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_));

  l->Commit();

  return Status::OK();
}

Status CatalogManager::SetPreferredZones(
    const SetPreferredZonesRequestPB* req, SetPreferredZonesResponsePB* resp) {
  auto l = cluster_config_->LockForWrite();
  auto replication_info = l->mutable_data()->pb.mutable_replication_info();
  replication_info->clear_affinitized_leaders();

  Status s;
  for (const auto& cloud_info : req->preferred_zones()) {
    s = CatalogManagerUtil::DoesPlacementInfoContainCloudInfo(replication_info->live_replicas(),
                                                              cloud_info);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
    }
    *replication_info->add_affinitized_leaders() = cloud_info;
  }

  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);

  LOG(INFO) << "Updating cluster config to " << l->mutable_data()->pb.version();

  s = sys_catalog_->UpdateItem(cluster_config_.get(), leader_ready_term_);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_CLUSTER_CONFIG, s);
  }

  l->Commit();

  return Status::OK();
}

Status CatalogManager::GetReplicationFactor(int* num_replicas) {
  DCHECK(cluster_config_) << "Missing cluster config for master!";
  auto l = cluster_config_->LockForRead();
  const ReplicationInfoPB& replication_info = l->data().pb.replication_info();
  *num_replicas = GetNumReplicasFromPlacementInfo(replication_info.live_replicas());
  return Status::OK();
}

string CatalogManager::placement_uuid() const {
  DCHECK(cluster_config_) << "Missing cluster config for master!";
  auto l = cluster_config_->LockForRead();
  const ReplicationInfoPB& replication_info = l->data().pb.replication_info();
  return replication_info.live_replicas().placement_uuid();
}

Status CatalogManager::IsLoadBalanced(const IsLoadBalancedRequestPB* req,
                                      IsLoadBalancedResponsePB* resp) {
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);

  if (req->has_expected_num_servers() && req->expected_num_servers() > ts_descs.size()) {
    Status s = STATUS(IllegalState,
                      Substitute("Found $0, which is below the expected number of servers $1.",
                                 ts_descs.size(), req->expected_num_servers()));
    return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
  }

  Status s = CatalogManagerUtil::IsLoadBalanced(ts_descs);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
  }

  return Status::OK();
}

Status CatalogManager::AreLeadersOnPreferredOnly(const AreLeadersOnPreferredOnlyRequestPB* req,
                                                 AreLeadersOnPreferredOnlyResponsePB* resp) {
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);

  SysClusterConfigEntryPB config;
  RETURN_NOT_OK(GetClusterConfig(&config));

  Status s = CatalogManagerUtil::AreLeadersOnPreferredOnly(ts_descs, config.replication_info());
  if (!s.ok()) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::CAN_RETRY_ARE_LEADERS_ON_PREFERRED_ONLY_CHECK, s);
  }

  return Status::OK();
}

void BlacklistState::Reset() {
  tservers_.clear();
  initial_load_ = 0;
}

std::string BlacklistState::ToString() {
  return Substitute("Blacklist has $0 servers, initial load is $1.",
                    tservers_.size(), initial_load_);
}

int64_t CatalogManager::GetNumBlacklistReplicas() {
  int64_t blacklist_replicas = 0;
  std::lock_guard <LockType> tablet_map_lock(lock_);
  for (const TabletInfoMap::value_type& entry : tablet_map_) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    auto l = tablet->LockForRead();
    // Not checking being created on purpose as we do not want initial load to be under accounted.
    if (!tablet->table() ||
        PREDICT_FALSE(l->data().is_deleted())) {
      continue;
    }

    TabletInfo::ReplicaMap locs;
    TSRegistrationPB reg;
    tablet->GetReplicaLocations(&locs);
    for (const TabletInfo::ReplicaMap::value_type& replica : locs) {
      replica.second.ts_desc->GetRegistration(&reg);
      bool blacklisted = false;
      for (const auto& hp : reg.common().private_rpc_addresses()) {
        if (blacklistState.tservers_.count(HostPortFromPB(hp)) != 0) {
          blacklisted = true;
          break;
        }
      }
      if (!blacklisted) {
        for (const auto& hp : reg.common().broadcast_addresses()) {
          if (blacklistState.tservers_.count(HostPortFromPB(hp)) != 0) {
            blacklisted = true;
            break;
          }
        }
      }
      if (blacklisted) {
        blacklist_replicas++;
      }
    }
  }

  return blacklist_replicas;
}

Status CatalogManager::GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp) {
  int64_t blacklist_replicas = GetNumBlacklistReplicas();
  LOG(INFO) << "Blacklisted count " << blacklist_replicas << " in " << tablet_map_.size()
            << " tablets, across " << blacklistState.tservers_.size()
            << " servers, with initial load " << blacklistState.initial_load_;

  // Case when a blacklisted servers did not have any starting load.
  if (blacklistState.initial_load_ == 0) {
    resp->set_percent(100);
    return Status::OK();
  }

  resp->set_percent(
      100 - (static_cast<double>(blacklist_replicas) * 100 / blacklistState.initial_load_));

  return Status::OK();
}

void CatalogManager::AbortAndWaitForAllTasks(const vector<scoped_refptr<TableInfo>>& tables) {
  for (const auto& t : tables) {
    t->AbortTasks();
  }
  for (const auto& t : tables) {
    t->WaitTasksCompletion();
  }
}

////////////////////////////////////////////////////////////
// CatalogManager::ScopedLeaderSharedLock
////////////////////////////////////////////////////////////

CatalogManager::ScopedLeaderSharedLock::ScopedLeaderSharedLock(CatalogManager* catalog)
  : catalog_(DCHECK_NOTNULL(catalog)),
    leader_shared_lock_(catalog->leader_lock_, std::try_to_lock),
    start_(std::chrono::steady_clock::now()) {

  // Check if the catalog manager is running.
  std::lock_guard<simple_spinlock> l(catalog_->state_lock_);
  if (PREDICT_FALSE(catalog_->state_ != kRunning)) {
    catalog_status_ = STATUS(ServiceUnavailable,
                          Substitute("Catalog manager is not initialized. State: $0",
                                     catalog_->state_));
    return;
  }
  string uuid = catalog_->master_->fs_manager()->uuid();
  if (PREDICT_FALSE(catalog_->master_->IsShellMode())) {
    // Consensus and other internal fields should not be checked when in shell mode as they may be
    // in transition.
    leader_status_ = STATUS(IllegalState,
                         Substitute("Catalog manager of $0 is in shell mode, not the leader.",
                                    uuid));
    return;
  }
  // Check if the catalog manager is the leader.
  Consensus* consensus = catalog_->sys_catalog_->tablet_peer()->consensus();
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    leader_status_ = STATUS_FORMAT(IllegalState,
                                   "Not the leader. Local UUID: $0, Consensus state: $1",
                                   uuid, cstate);
    return;
  }
  // TODO: deduplicate the leadership check above and below (one is committed, one is active).
  const Status s = consensus->CheckIsActiveLeaderAndHasLease();
  if (!s.ok()) {
    leader_status_ = s;
    return;
  }
  if (PREDICT_FALSE(catalog_->leader_ready_term_ != cstate.current_term())) {
    // Normally we use LeaderNotReadyToServe to indicate that the leader has not replicated its
    // NO_OP entry or the previous leader's lease has not expired yet, and the handling logic is to
    // to retry on the same server.
    leader_status_ = STATUS(LeaderNotReadyToServe,
                         Substitute("Leader not yet ready to serve requests: "
                                    "leader_ready_term_ = $0; "
                                    "cstate.current_term = $1",
                                    catalog_->leader_ready_term_,
                                    cstate.current_term()));
    return;
  }
  if (PREDICT_FALSE(!leader_shared_lock_.owns_lock())) {
    leader_status_ = STATUS(ServiceUnavailable, "Couldn't get leader_lock_ in shared mode. "
                                                "Leader still loading catalog tables.");
    return;
  }
}

void CatalogManager::ScopedLeaderSharedLock::Unlock() {
  if (leader_shared_lock_.owns_lock()) {
    {
      decltype(leader_shared_lock_) lock;
      lock.swap(leader_shared_lock_);
    }
    auto finish = std::chrono::steady_clock::now();
    if (finish > start_ + kLockLongLimit) {
      LOG(WARNING) << "Long lock of catalog manager: " << yb::ToString(finish - start_) << "\n"
                   << GetStackTrace();
    }
  }
}

////////////////////////////////////////////////////////////
// TabletInfo
////////////////////////////////////////////////////////////

TabletInfo::TabletInfo(const scoped_refptr<TableInfo>& table, TabletId tablet_id)
    : tablet_id_(std::move(tablet_id)),
      table_(table),
      last_update_time_(MonoTime::Now()),
      reported_schema_version_(0) {}

TabletInfo::~TabletInfo() {
}

void TabletInfo::SetReplicaLocations(ReplicaMap replica_locations) {
  std::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = MonoTime::Now();
  replica_locations_ = std::move(replica_locations);
}

void TabletInfo::GetReplicaLocations(ReplicaMap* replica_locations) const {
  std::lock_guard<simple_spinlock> l(lock_);
  *replica_locations = replica_locations_;
}

bool TabletInfo::AddToReplicaLocations(const TabletReplica& replica) {
  std::lock_guard<simple_spinlock> l(lock_);
  return InsertIfNotPresent(&replica_locations_, replica.ts_desc->permanent_uuid(), replica);
}

void TabletInfo::set_last_update_time(const MonoTime& ts) {
  std::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = ts;
}

MonoTime TabletInfo::last_update_time() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return last_update_time_;
}

bool TabletInfo::set_reported_schema_version(uint32_t version) {
  std::lock_guard<simple_spinlock> l(lock_);
  if (version > reported_schema_version_) {
    reported_schema_version_ = version;
    return true;
  }
  return false;
}

uint32_t TabletInfo::reported_schema_version() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return reported_schema_version_;
}

std::string TabletInfo::ToString() const {
  return Substitute("$0 (table $1)", tablet_id_,
                    (table_ != nullptr ? table_->ToString() : "MISSING"));
}

void TabletInfo::RegisterLeaderStepDownFailure(const TabletServerId& dest_leader,
                                               MonoDelta time_since_stepdown_failure) {
  std::lock_guard<simple_spinlock> l(lock_);
  leader_stepdown_failure_times_[dest_leader] = MonoTime::Now() - time_since_stepdown_failure;
}

void TabletInfo::GetLeaderStepDownFailureTimes(MonoTime forget_failures_before,
                                               LeaderStepDownFailureTimes* dest) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (auto iter = leader_stepdown_failure_times_.begin();
       iter != leader_stepdown_failure_times_.end(); ) {
    if (iter->second < forget_failures_before) {
      iter = leader_stepdown_failure_times_.erase(iter);
    } else {
      iter++;
    }
  }
  *dest = leader_stepdown_failure_times_;
}

void PersistentTabletInfo::set_state(SysTabletsEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

////////////////////////////////////////////////////////////
// TableInfo
////////////////////////////////////////////////////////////

TableInfo::TableInfo(TableId table_id) : table_id_(std::move(table_id)) {}

TableInfo::~TableInfo() {
}

const TableName TableInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

bool TableInfo::is_running() const {
  auto l = LockForRead();
  return l->data().is_running();
}

std::string TableInfo::ToString() const {
  auto l = LockForRead();
  return Substitute("$0 [id=$1]", l->data().pb.name(), table_id_);
}

const NamespaceId TableInfo::namespace_id() const {
  auto l = LockForRead();
  return l->data().namespace_id();
}

const Status TableInfo::GetSchema(Schema* schema) const {
  auto l = LockForRead();
  return SchemaFromPB(l->data().schema(), schema);
}

const std::string TableInfo::indexed_table_id() const {
  auto l = LockForRead();
  return l->data().pb.has_indexed_table_id() ? l->data().pb.indexed_table_id() : "";
}

bool TableInfo::is_local_index() const {
  auto l = LockForRead();
  return l->data().pb.is_local_index();
}

bool TableInfo::is_unique_index() const {
  auto l = LockForRead();
  return l->data().pb.is_unique_index();
}

TableType TableInfo::GetTableType() const {
  auto l = LockForRead();
  return l->data().pb.table_type();
}

bool TableInfo::RemoveTablet(const std::string& partition_key_start) {
  std::lock_guard<simple_spinlock> l(lock_);
  return EraseKeyReturnValuePtr(&tablet_map_, partition_key_start) != NULL;
}

void TableInfo::AddTablet(TabletInfo *tablet) {
  std::lock_guard<simple_spinlock> l(lock_);
  AddTabletUnlocked(tablet);
}

void TableInfo::AddTablets(const vector<TabletInfo*>& tablets) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (TabletInfo *tablet : tablets) {
    AddTabletUnlocked(tablet);
  }
}

void TableInfo::AddTabletUnlocked(TabletInfo* tablet) {
  TabletInfo* old = nullptr;
  if (UpdateReturnCopy(&tablet_map_,
                       tablet->metadata().dirty().pb.partition().partition_key_start(),
                       tablet, &old)) {
    VLOG(1) << "Replaced tablet " << old->tablet_id() << " with " << tablet->tablet_id();
    // TODO: can we assert that the replaced tablet is not in Running state?
    // May be a little tricky since we don't know whether to look at its committed or
    // uncommitted state.
  }
}

void TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req, TabletInfos* ret) const {
  std::lock_guard<simple_spinlock> l(lock_);
  int32_t max_returned_locations = req->max_returned_locations();

  TableInfo::TabletInfoMap::const_iterator it, it_end;
  if (req->has_partition_key_start()) {
    it = tablet_map_.upper_bound(req->partition_key_start());
    if (it != tablet_map_.begin()) {
      --it;
    }
  } else {
    it = tablet_map_.begin();
  }
  if (req->has_partition_key_end()) {
    it_end = tablet_map_.upper_bound(req->partition_key_end());
  } else {
    it_end = tablet_map_.end();
  }

  int32_t count = 0;
  for (; it != it_end && count < max_returned_locations; ++it) {
    ret->push_back(make_scoped_refptr(it->second));
    count++;
  }
}

bool TableInfo::IsAlterInProgress(uint32_t version) const {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    if (e.second->reported_schema_version() < version) {
      VLOG(3) << "Table " << table_id_ << " ALTER in progress due to tablet "
              << e.second->ToString() << " because reported schema "
              << e.second->reported_schema_version() << " < expected " << version;
      return true;
    }
  }
  return false;
}

bool TableInfo::IsCreateInProgress() const {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    auto tablet_lock = e.second->LockForRead();
    if (!tablet_lock->data().is_running()) {
      return true;
    }
  }
  return false;
}

void TableInfo::SetCreateTableErrorStatus(const Status& status) {
  std::lock_guard<simple_spinlock> l(lock_);
  create_table_error_ = status;
}

Status TableInfo::GetCreateTableErrorStatus() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return create_table_error_;
}

std::size_t TableInfo::NumTasks() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return pending_tasks_.size();
}

bool TableInfo::HasTasks() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !pending_tasks_.empty();
}

bool TableInfo::HasTasks(MonitoredTask::Type type) const {
  std::lock_guard<simple_spinlock> l(lock_);
  for (auto task : pending_tasks_) {
    if (task->type() == type) {
      return true;
    }
  }
  return false;
}

void TableInfo::AddTask(std::shared_ptr<MonitoredTask> task) {
  std::lock_guard<simple_spinlock> l(lock_);
  pending_tasks_.insert(std::move(task));
}

void TableInfo::RemoveTask(const std::shared_ptr<MonitoredTask>& task) {
  std::lock_guard<simple_spinlock> l(lock_);
  pending_tasks_.erase(task);
}

// Aborts tasks which have their rpc in progress, rest of them are aborted and also erased
// from the pending list.
void TableInfo::AbortTasks() {
  std::vector<std::shared_ptr<MonitoredTask>> abort_tasks;
  {
    std::lock_guard<simple_spinlock> l(lock_);
    abort_tasks.reserve(pending_tasks_.size());
    abort_tasks.assign(pending_tasks_.cbegin(), pending_tasks_.cend());
  }
  // We need to abort these tasks without holding the lock because when a task is destroyed it tries
  // to acquire the same lock to remove itself from pending_tasks_.
  for (const auto& task : abort_tasks) {
    VLOG(1) << "Aborting task " << task->description();
    task->AbortAndReturnPrevState();
  }
}

void TableInfo::WaitTasksCompletion() {
  int wait_time = 5;
  while (1) {
    {
      std::lock_guard<simple_spinlock> l(lock_);
      if (pending_tasks_.empty()) {
        break;
      }
    }
    base::SleepForMilliseconds(wait_time);
    wait_time = std::min(wait_time * 5 / 4, 10000);
  }
}

std::unordered_set<std::shared_ptr<MonitoredTask>> TableInfo::GetTasks() {
  std::lock_guard<simple_spinlock> l(lock_);
  return pending_tasks_;
}

void TableInfo::GetAllTablets(vector<scoped_refptr<TabletInfo>> *ret) const {
  ret->clear();
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    ret->push_back(make_scoped_refptr(e.second));
  }
}

IndexInfo TableInfo::GetIndexInfo(const TableId& index_id) const {
  auto l = LockForRead();
  for (const auto& index_info_pb : l->data().pb.indexes()) {
    if (index_info_pb.table_id() == index_id) {
      return IndexInfo(index_info_pb);
    }
  }
  return IndexInfo();
}

void PersistentTableInfo::set_state(SysTablesEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

////////////////////////////////////////////////////////////
// DeletedTableInfo
////////////////////////////////////////////////////////////

DeletedTableInfo::DeletedTableInfo(const TableInfo* table) : table_id_(table->id()) {
  vector<scoped_refptr<TabletInfo>> tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    TabletInfo::ReplicaMap replica_locations;
    tablet->GetReplicaLocations(&replica_locations);

    for (const TabletInfo::ReplicaMap::value_type& r : replica_locations) {
      tablet_set_.insert(TabletSet::value_type(
          r.second.ts_desc->permanent_uuid(), tablet->id()));
    }
  }
}

std::size_t DeletedTableInfo::NumTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return tablet_set_.size();
}

bool DeletedTableInfo::HasTablets() const {
  std::lock_guard<simple_spinlock> l(lock_);
  return !tablet_set_.empty();
}

void DeletedTableInfo::DeleteTablet(const TabletKey& key) {
  std::lock_guard<simple_spinlock> l(lock_);
  tablet_set_.erase(key);
}

void DeletedTableInfo::AddTabletsToMap(DeletedTabletMap* tablet_map) {
  std::lock_guard<simple_spinlock> l(lock_);
  for (const TabletKey& key : tablet_set_) {
    tablet_map->insert(DeletedTabletMap::value_type(key, this));
  }
}

////////////////////////////////////////////////////////////
// NamespaceInfo
////////////////////////////////////////////////////////////

NamespaceInfo::NamespaceInfo(NamespaceId ns_id) : namespace_id_(std::move(ns_id)) {}

const NamespaceName& NamespaceInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

YQLDatabase NamespaceInfo::database_type() const {
  auto l = LockForRead();
  return l->data().pb.has_database_type() ? l->data().pb.database_type()
                                          : YQL_DATABASE_UNDEFINED;
}

std::string NamespaceInfo::ToString() const {
  return Substitute("$0 [id=$1]", name(), namespace_id_);
}

////////////////////////////////////////////////////////////
// UDTypeInfo
////////////////////////////////////////////////////////////

UDTypeInfo::UDTypeInfo(UDTypeId udtype_id) : udtype_id_(std::move(udtype_id)) { }

const UDTypeName& UDTypeInfo::name() const {
  auto l = LockForRead();
  return l->data().pb.name();
}

const NamespaceName& UDTypeInfo::namespace_id() const {
  auto l = LockForRead();
  return l->data().pb.namespace_id();
}

int UDTypeInfo::field_names_size() const {
  auto l = LockForRead();
  return l->data().pb.field_names_size();
}

const string& UDTypeInfo::field_names(int index) const {
  auto l = LockForRead();
  return l->data().pb.field_names(index);
}

int UDTypeInfo::field_types_size() const {
  auto l = LockForRead();
  return l->data().pb.field_types_size();
}

const QLTypePB& UDTypeInfo::field_types(int index) const {
  auto l = LockForRead();
  return l->data().pb.field_types(index);
}

std::string UDTypeInfo::ToString() const {
  auto l = LockForRead();
  return Substitute("$0 [id=$1] {metadata=$2} ", name(), udtype_id_, l->data().pb.DebugString());
}

}  // namespace master
}  // namespace yb
