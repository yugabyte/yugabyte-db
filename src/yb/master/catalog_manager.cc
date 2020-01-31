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
// ================================================================================================
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
//
// ================================================================================================

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
#include "yb/master/catalog_manager_bg_tasks.h"
#include "yb/master/catalog_loaders.h"
#include "yb/master/initial_sys_catalog_snapshot.h"
#include "yb/master/tasks_tracker.h"
#include "yb/master/encryption_manager.h"

#include "yb/tserver/ts_tablet_manager.h"
#include "yb/rpc/messenger.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"

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

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/tserver/remote_bootstrap_client.h"

#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/util/shared_lock.h"

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

DEFINE_int32(catalog_manager_inject_latency_in_delete_table_ms, 0,
             "Number of milliseconds that the master will sleep in DeleteTable.");
TAG_FLAG(catalog_manager_inject_latency_in_delete_table_ms, hidden);

DEFINE_int32(replication_factor, 3,
             "Default number of replicas for tables that do not have the num_replicas set.");
TAG_FLAG(replication_factor, advanced);

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

METRIC_DEFINE_gauge_uint32(cluster, num_tablet_servers_dead,
                           "Number of dead tservers in the cluster", yb::MetricUnit::kUnits,
                           "The number of tablet servers that have not responded or done a "
                           "heartbeat in the time interval defined by the gflag "
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

DEFINE_uint64(transaction_table_num_tablets, 0,
    "Number of tablets to use when creating the transaction status table."
    "0 to use the same default num tablets as for regular tables.");

DEFINE_bool(master_enable_metrics_snapshotter, false, "Should metrics snapshotter be enabled");

DEFINE_uint64(metrics_snapshots_table_num_tablets, 0,
    "Number of tablets to use when creating the metrics snapshots table."
    "0 to use the same default num tablets as for regular tables.");

DEFINE_bool(
    hide_pg_catalog_table_creation_logs, false,
    "Whether to hide detailed log messages for PostgreSQL catalog table creation. "
    "This cuts down test logs significantly.");
TAG_FLAG(hide_pg_catalog_table_creation_logs, hidden);

DEFINE_test_flag(int32, simulate_slow_table_create_secs, 0,
    "Simulates a slow table creation by sleeping after the table has been added to memory.");

DEFINE_bool(
    // TODO: switch the default to true after updating all external callers (yb-ctl, YugaWare)
    // and unit tests.
    master_auto_run_initdb, false,
    "Automatically run initdb on master leader initialization");

DEFINE_test_flag(int32, simulate_slow_system_tablet_bootstrap_secs, 0,
    "Simulates a slow tablet bootstrap by adding a sleep before system tablet init.");

DEFINE_test_flag(bool, return_error_if_namespace_not_found, false,
    "Return an error from ListTables if a namespace id is not found in the map");

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
using tablet::RaftGroupMetadata;
using tablet::RaftGroupMetadataPtr;
using tablet::TabletPeer;
using tablet::RaftGroupStatePB;
using tablet::TabletStatusListener;
using tablet::TabletStatusPB;
using tserver::HandleReplacingStaleTablet;
using tserver::enterprise::RemoteBootstrapClient;
using tserver::TabletServerErrorPB;
using master::MasterServiceProxy;
using yb::pgwrapper::PgWrapper;
using yb::server::MasterAddressesToString;

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBColumnSchema;
using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;
using yb::client::YBTable;
using yb::client::YBTableCreator;
using yb::client::YBTableName;

namespace {

// Macros to access index information in CATALOG.
//
// NOTES from file master.proto for SysTablesEntryPB.
// - For index table: [to be deprecated and replaced by "index_info"]
//     optional bytes indexed_table_id = 13; // Indexed table id of this index.
//     optional bool is_local_index = 14 [ default = false ];  // Whether this is a local index.
//     optional bool is_unique_index = 15 [ default = false ]; // Whether this is a unique index.
// - During transition period, we have to consider both fields and the following macros help
//   avoiding duplicate protobuf version check thru out our code.

#define PROTO_GET_INDEXED_TABLE_ID(tabpb) \
  (tabpb.has_index_info() ? tabpb.index_info().indexed_table_id() \
                          : tabpb.indexed_table_id())

#define PROTO_GET_IS_LOCAL(tabpb) \
  (tabpb.has_index_info() ? tabpb.index_info().is_local() \
                          : tabpb.is_local_index())

#define PROTO_GET_IS_UNIQUE(tabpb) \
  (tabpb.has_index_info() ? tabpb.index_info().is_unique() \
                          : tabpb.is_unique_index())

#define PROTO_IS_INDEX(tabpb) \
  (tabpb.has_index_info() || !tabpb.indexed_table_id().empty())

#define PROTO_IS_TABLE(tabpb) \
  (!tabpb.has_index_info() && tabpb.indexed_table_id().empty())

#define PROTO_PTR_IS_INDEX(tabpb) \
  (tabpb->has_index_info() || !tabpb->indexed_table_id().empty())

#define PROTO_PTR_IS_TABLE(tabpb) \
  (!tabpb->has_index_info() && tabpb->indexed_table_id().empty())

#if (0)
// Once the deprecated fields are obsolete, the above macros should be defined as the following.
#define PROTO_GET_INDEXED_TABLE_ID(tabpb) (tabpb.index_info().indexed_table_id())
#define PROTO_GET_IS_LOCAL(tabpb) (tabpb.index_info().is_local())
#define PROTO_GET_IS_UNIQUE(tabpb) (tabpb.index_info().is_unique())
#define PROTO_IS_INDEX(tabpb) (tabpb.has_index_info())
#define PROTO_IS_TABLE(tabpb) (!tabpb.has_index_info())
#define PROTO_PTR_IS_INDEX(tabpb) (tabpb->has_index_info())
#define PROTO_PTR_IS_TABLE(tabpb) (!tabpb->has_index_info())

#endif

class IndexInfoBuilder {
 public:
  explicit IndexInfoBuilder(IndexInfoPB* index_info) : index_info_(*index_info) {
  }

  void ApplyProperties(const TableId& indexed_table_id, bool is_local, bool is_unique) {
    index_info_.set_indexed_table_id(indexed_table_id);
    index_info_.set_version(0);
    index_info_.set_is_local(is_local);
    index_info_.set_is_unique(is_unique);
  }

  CHECKED_STATUS ApplyColumnMapping(const Schema& indexed_schema, const Schema& index_schema) {
    for (size_t i = 0; i < index_schema.num_columns(); i++) {
      const auto& col_name = index_schema.column(i).name();
      const auto indexed_col_idx = indexed_schema.find_column(col_name);
      if (PREDICT_FALSE(indexed_col_idx == Schema::kColumnNotFound)) {
        return STATUS(NotFound, "The indexed table column does not exist", col_name);
      }
      auto* col = index_info_.add_columns();
      col->set_column_id(index_schema.column_id(i));
      col->set_indexed_column_id(indexed_schema.column_id(indexed_col_idx));
    }
    index_info_.set_hash_column_count(index_schema.num_hash_key_columns());
    index_info_.set_range_column_count(index_schema.num_range_key_columns());

    for (size_t i = 0; i < indexed_schema.num_hash_key_columns(); i++) {
      index_info_.add_indexed_hash_column_ids(indexed_schema.column_id(i));
    }
    for (size_t i = indexed_schema.num_hash_key_columns(); i < indexed_schema.num_key_columns();
        i++) {
      index_info_.add_indexed_range_column_ids(indexed_schema.column_id(i));
    }
    return Status::OK();
  }

 private:
  IndexInfoPB& index_info_;
};

template<class RespClass>
Status CheckIfTableDeletedOrNotRunning(TableInfo::lock_type* lock, RespClass* resp) {
  // This covers both in progress and fully deleted objects.
  if (lock->data().started_deleting()) {
    Status s = STATUS_SUBSTITUTE(NotFound,
        "The table '$0.$1' does not exist", lock->data().namespace_id(), lock->data().name());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }
  if (!lock->data().is_running()) {
    Status s = STATUS_SUBSTITUTE(ServiceUnavailable,
        "The table '$0.$1' is not running", lock->data().namespace_id(), lock->data().name());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }
  return Status::OK();
}

} // namespace

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
// CatalogManager
////////////////////////////////////////////////////////////

YQLDatabase CatalogManager::GetDatabaseTypeForTable(const TableType table_type) {
  switch (table_type) {
    case TableType::YQL_TABLE_TYPE: return YQLDatabase::YQL_DATABASE_CQL;
    case TableType::REDIS_TABLE_TYPE: return YQLDatabase::YQL_DATABASE_REDIS;
    case TableType::PGSQL_TABLE_TYPE: return YQLDatabase::YQL_DATABASE_PGSQL;
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      // Transactions status table is created in "system" keyspace in CQL.
      return YQLDatabase::YQL_DATABASE_CQL;
  }
  return YQL_DATABASE_UNKNOWN;
}

CatalogManager::CatalogManager(Master* master)
    : master_(master),
      rng_(GetRandomSeed32()),
      tablet_exists_(false),
      state_(kConstructed),
      leader_ready_term_(-1),
      leader_lock_(RWMutex::Priority::PREFER_WRITING),
      load_balance_policy_(new enterprise::ClusterLoadBalancer(this)),
      permissions_manager_(std::make_unique<PermissionsManager>(this)),
      tasks_tracker_(new TasksTracker()),
      encryption_manager_(new EncryptionManager()) {
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

  metric_num_tablet_servers_dead_ =
    METRIC_num_tablet_servers_dead.Instantiate(master_->metric_entity_cluster(), 0);

  RETURN_NOT_OK_PREPEND(InitSysCatalogAsync(is_first_run),
                        "Failed to initialize sys tables async");

  if (PREDICT_FALSE(FLAGS_simulate_slow_system_tablet_bootstrap_secs > 0)) {
    LOG(INFO) << "Simulating slow system tablet bootstrap";
    SleepFor(MonoDelta::FromSeconds(FLAGS_simulate_slow_system_tablet_bootstrap_secs));
  }

  // WaitUntilRunning() must run outside of the lock as to prevent
  // deadlock. This is safe as WaitUntilRunning waits for another
  // thread to finish its work and doesn't itself depend on any state
  // within CatalogManager. Need not start sys catalog or background tasks
  // when we are started in shell mode.
  if (!master_->opts().IsShellMode()) {
    RETURN_NOT_OK_PREPEND(sys_catalog_->WaitUntilRunning(),
                          "Failed waiting for the catalog tablet to run");
    std::vector<consensus::RaftPeerPB> masters_raft;
    RETURN_NOT_OK(master_->ListRaftConfigMasters(&masters_raft));
    HostPortSet hps;
    for (const auto& peer : masters_raft) {
      if (master_->instance_pb().permanent_uuid() == peer.permanent_uuid()) {
        continue;
      }
      HostPort hp = HostPortFromPB(DesiredHostPort(peer, master_->MakeCloudInfoPB()));
      hps.insert(hp);
    }
    RETURN_NOT_OK(encryption_manager_->AddPeersToGetUniverseKeyFrom(hps));
    RETURN_NOT_OK(EnableBgTasks());
  }

  {
    std::lock_guard<simple_spinlock> l(state_lock_);
    CHECK_EQ(kStarting, state_);
    state_ = kRunning;
  }
  return Status::OK();
}

Status CatalogManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                            ChangeEncryptionInfoResponsePB* resp) {
  return STATUS(InvalidCommand, "Command only supported in enterprise build.");
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
    return STATUS_SUBSTITUTE(IllegalState,
        "Node $0 not leader. Consensus state: $1", uuid, cstate.ShortDebugString());
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
      if (status.IsShutdownInProgress()) {
        LOG(INFO) << "Error loading sys catalog; because shutdown is in progress. term " << term
                  << " status : " << status;
        return;
      }
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

  static const auto kLongLockAcquisitionLimit = RegularBuildVsSanitizers(100ms, 750ms);
  if (finish > start + kLongLockAcquisitionLimit) {
    LOG(WARNING) << "Long wait on leader_lock_: " << yb::ToString(finish - start);
  }

  LOG(INFO) << __func__ << ": Acquire catalog manager lock_ before loading sys catalog..";
  std::lock_guard<LockType> lock(lock_);
  VLOG(1) << __func__ << ": Acquired the catalog manager lock_";

  // Abort any outstanding tasks. All TableInfos are orphaned below, so
  // it's important to end their tasks now; otherwise Shutdown() will
  // destroy master state used by these tasks.
  std::vector<scoped_refptr<TableInfo>> tables;
  AppendValuesFromMap(*table_ids_map_, &tables);
  AbortAndWaitForAllTasks(tables);

  // Clear internal maps and run data loaders.
  RETURN_NOT_OK(RunLoaders());

  // Prepare various default system configurations.
  RETURN_NOT_OK(PrepareDefaultSysConfig(term));

  if ((FLAGS_use_initial_sys_catalog_snapshot || FLAGS_enable_ysql) &&
      !FLAGS_initial_sys_catalog_snapshot_path.empty() &&
      !FLAGS_create_initial_sys_catalog_snapshot) {
    if (!namespace_ids_map_.empty() || !system_tablets_.empty()) {
      LOG(INFO) << "This is an existing cluster, not initializing from a sys catalog snapshot.";
    } else {
      Result<bool> dir_exists =
          Env::Default()->DoesDirectoryExist(FLAGS_initial_sys_catalog_snapshot_path);
      if (dir_exists.ok() && *dir_exists) {
        bool initdb_was_already_done = false;
        {
          auto l = ysql_catalog_config_->LockForRead();
          initdb_was_already_done = l->data().pb.ysql_catalog_config().initdb_done();
        }
        if (initdb_was_already_done) {
          LOG(INFO) << "initdb has been run before, no need to restore sys catalog from "
                    << "the initial snapshot";
        } else {
          LOG(INFO) << "Restoring snapshot in sys catalog";
          Status restore_status = RestoreInitialSysCatalogSnapshot(
              FLAGS_initial_sys_catalog_snapshot_path,
              sys_catalog_->tablet_peer().get(),
              term);
          if (!restore_status.ok()) {
            LOG(ERROR) << "Failed restoring snapshot in sys catalog";
            return restore_status;
          }

          LOG(INFO) << "Re-initializing cluster config";
          cluster_config_.reset();
          RETURN_NOT_OK(PrepareDefaultClusterConfig(term));

          LOG(INFO) << "Restoring snapshot completed, considering initdb finished";
          RETURN_NOT_OK(InitDbFinished(Status::OK(), term));
          RETURN_NOT_OK(RunLoaders());
        }
      } else {
        LOG(WARNING) << "Initial sys catalog snapshot directory does not exist: "
                    << FLAGS_initial_sys_catalog_snapshot_path
                    << (dir_exists.ok() ? "" : ", status: " + dir_exists.status().ToString());
      }
    }
  }

  // Create the system namespaces (created only if they don't already exist).
  RETURN_NOT_OK(PrepareDefaultNamespaces(term));

  // Create the system tables (created only if they don't already exist).
  RETURN_NOT_OK(PrepareSystemTables(term));

  // Create the default cassandra (created only if they don't already exist).
  RETURN_NOT_OK(permissions_manager_->PrepareDefaultRoles(term));

  // If this is the first time we start up, we have no config information as default. We write an
  // empty version 0.
  RETURN_NOT_OK(PrepareDefaultClusterConfig(term));

  permissions_manager_->BuildRecursiveRolesUnlocked();

  // The error handling here is not for the initdb result -- that handling is done asynchronously.
  // This just starts initdb in the background and returns.
  return StartRunningInitDbIfNeeded(term);
}

template <class Loader>
Status CatalogManager::Load(const std::string& title) {
  LOG(INFO) << __func__ << ": Loading " << title << " into memory.";
  auto loader = std::make_unique<Loader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(loader.get()),
      "Failed while visiting " + title + " in sys catalog");
  return Status::OK();
}

Status CatalogManager::RunLoaders() {
  // Clear the table and tablet state.
  table_names_map_.clear();
  auto table_ids_map_checkout = table_ids_map_.CheckOut();
  table_ids_map_checkout->clear();

  auto tablet_map_checkout = tablet_map_.CheckOut();
  tablet_map_checkout->clear();

  // Clear the namespace mappings.
  namespace_ids_map_.clear();
  namespace_names_map_.clear();

  // Clear the type mappings.
  udtype_ids_map_.clear();
  udtype_names_map_.clear();

  // Clear the current cluster config.
  cluster_config_.reset();

  // Clear the roles mapping.
  permissions_manager()->ClearRolesUnlocked();

  // Clear redis config mapping.
  redis_config_map_.clear();

  // Clear ysql catalog config.
  ysql_catalog_config_.reset();

  // Clear recent tasks.
  tasks_tracker_->Reset();

  std::vector<std::shared_ptr<TSDescriptor>> descs;
  master_->ts_manager()->GetAllDescriptors(&descs);
  for (const auto& ts_desc : descs) {
    ts_desc->set_has_tablet_report(false);
  }

  RETURN_NOT_OK(Load<TableLoader>("tables"));
  RETURN_NOT_OK(Load<TabletLoader>("tablets"));
  RETURN_NOT_OK(Load<NamespaceLoader>("namespaces"));
  RETURN_NOT_OK(Load<UDTypeLoader>("user-defined types"));
  RETURN_NOT_OK(Load<ClusterConfigLoader>("cluster configuration"));
  RETURN_NOT_OK(Load<RoleLoader>("roles"));
  RETURN_NOT_OK(Load<RedisConfigLoader>("Redis config"));
  RETURN_NOT_OK(Load<SysConfigLoader>("sys config"));

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

  std::string cluster_uuid_source;
  if (!FLAGS_cluster_uuid.empty()) {
    Uuid uuid;
    RETURN_NOT_OK(uuid.FromString(FLAGS_cluster_uuid));
    config.set_cluster_uuid(FLAGS_cluster_uuid);
    cluster_uuid_source = "from the --cluster_uuid flag";
  } else {
    auto uuid = Uuid::Generate();
    config.set_cluster_uuid(to_string(uuid));
    cluster_uuid_source = "(randomly generated)";
  }
  LOG(INFO) << "Setting cluster UUID to " << config.cluster_uuid() << " " << cluster_uuid_source;

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
  if (!lock_.is_locked()) {
    return STATUS(IllegalState, "We don't have the catalog manager lock!");
  }

  RETURN_NOT_OK(permissions_manager()->PrepareDefaultSecurityConfigUnlocked(term));

  if (!ysql_catalog_config_) {
    SysYSQLCatalogConfigEntryPB ysql_catalog_config;
    ysql_catalog_config.set_version(0);

    // Create in memory objects.
    ysql_catalog_config_ = new SysConfigInfo(kYsqlCatalogConfigType);

    // Prepare write.
    auto l = ysql_catalog_config_->LockForWrite();
    *l->mutable_data()->pb.mutable_ysql_catalog_config() = std::move(ysql_catalog_config);

    // Write to sys_catalog and in memory.
    RETURN_NOT_OK(sys_catalog_->AddItem(ysql_catalog_config_.get(), term));
    l->Commit();
  }

  return Status::OK();
}

Status CatalogManager::StartRunningInitDbIfNeeded(int64_t term) {
  CHECK(lock_.is_locked());
  if (!FLAGS_master_auto_run_initdb) {
    return Status::OK();
  }

  {
    auto l = ysql_catalog_config_->LockForRead();
    if (l->data().pb.ysql_catalog_config().initdb_done()) {
      LOG(INFO) << "Cluster configuration indicates that initdb has already completed";
      return Status::OK();
    }
  }

  if (pg_proc_exists_) {
    LOG(INFO) << "Table pg_proc exists, assuming initdb has already been run";
    return Status::OK();
  }

  LOG(INFO) << "initdb has never been run on this cluster, running it";
  string master_addresses_str = MasterAddressesToString(
      *master_->opts().GetMasterAddresses());

  initdb_future_ = std::async(std::launch::async, [this, master_addresses_str, term] {
    if (FLAGS_create_initial_sys_catalog_snapshot) {
      initial_snapshot_writer_.emplace();
    }

    Status status = PgWrapper::InitDbForYSQL(master_addresses_str, "/tmp");

    if (FLAGS_create_initial_sys_catalog_snapshot && status.ok()) {
      Status write_snapshot_status = initial_snapshot_writer_->WriteSnapshot(
          sys_catalog_->tablet_peer()->tablet(),
          FLAGS_initial_sys_catalog_snapshot_path);
      if (!write_snapshot_status.ok()) {
        status = write_snapshot_status;
      }
    }
    Status finish_status = InitDbFinished(status, term);
    if (!finish_status.ok()) {
      if (status.ok()) {
        status = finish_status;
      }
      LOG(WARNING) << "Failed to set initdb as finished in sys catalog: " << finish_status;
    }
    return status;
  });
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

Status CatalogManager::PrepareSysCatalogTable(int64_t term) {
  // Prepare sys catalog table info.
  auto sys_catalog_table_iter = table_ids_map_->find(kSysCatalogTableId);
  if (sys_catalog_table_iter == table_ids_map_->end()) {
    scoped_refptr<TableInfo> table = NewTableInfo(kSysCatalogTableId);
    table->mutable_metadata()->StartMutation();
    SysTablesEntryPB& metadata = table->mutable_metadata()->mutable_dirty()->pb;
    metadata.set_state(SysTablesEntryPB::RUNNING);
    metadata.set_namespace_id(kSystemSchemaNamespaceId);
    metadata.set_name(kSysCatalogTableName);
    metadata.set_table_type(TableType::YQL_TABLE_TYPE);
    SchemaToPB(sys_catalog_->schema_with_ids_, metadata.mutable_schema());
    metadata.set_version(0);

    auto table_ids_map_checkout = table_ids_map_.CheckOut();
    sys_catalog_table_iter = table_ids_map_checkout->emplace(table->id(), table).first;
    table_names_map_[{kSystemSchemaNamespaceId, kSysCatalogTableName}] = table;

    RETURN_NOT_OK(sys_catalog_->AddItem(table.get(), term));
    table->mutable_metadata()->CommitMutation();
  }

  // Prepare sys catalog tablet info.
  if (tablet_map_->count(kSysCatalogTabletId) == 0) {
    scoped_refptr<TableInfo> table = sys_catalog_table_iter->second;
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

    auto tablet_map_checkout = tablet_map_.CheckOut();
    (*tablet_map_checkout)[tablet->tablet_id()] = tablet;

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
    LOG(INFO) << "Table " << namespace_name << "." << table_name << " already created";

    Schema persisted_schema;
    RETURN_NOT_OK(table->GetSchema(&persisted_schema));
    if (!persisted_schema.Equals(schema)) {
      LOG(INFO) << "Updating schema of " << namespace_name << "." << table_name << " ...";
      auto l = table->LockForWrite();
      SchemaToPB(schema, l->mutable_data()->pb.mutable_schema());
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
      LOG(INFO) << "Creating tablets for " << namespace_name << "." << table_name << " ...";
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
    LOG(INFO) << "Inserted new " << namespace_name << "." << table_name
              << " table info into CatalogManager maps";
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

bool CatalogManager::IsYcqlNamespace(const NamespaceInfo& ns) {
  return ns.database_type() == YQLDatabase::YQL_DATABASE_CQL;
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
    LOG(INFO) << "Keyspace " << name << " already created, skipping initialization";
    return Status::OK();
  }

  // Create entry.
  SysNamespaceEntryPB ns_entry;
  ns_entry.set_name(name);
  ns_entry.set_database_type(YQLDatabase::YQL_DATABASE_CQL);

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

  std::vector<Endpoint> resolved_addresses;
  Status s = server::ResolveMasterAddresses(master_->opts().GetMasterAddresses(),
                                            &resolved_addresses);
  RETURN_NOT_OK(s);

  for (auto const &addr : resolved_addresses) {
    if (addr.address().is_unspecified() ||
        std::find(local_addrs.begin(), local_addrs.end(), addr.address()) !=
            local_addrs.end()) {
      return Status::OK();
    }
  }
  return STATUS_SUBSTITUTE(IllegalState,
      "None of the local addresses are present in master_addresses $0.",
      master_->opts().master_addresses_flag);
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
    return STATUS_SUBSTITUTE(ServiceUnavailable,
        "Catalog manager is shutting down. State: $0", state_);
  }
  string uuid = master_->fs_manager()->uuid();
  if (master_->opts().IsShellMode()) {
    // Consensus and other internal fields should not be checked when is shell mode.
    return STATUS_SUBSTITUTE(IllegalState,
        "Catalog manager of $0 is in shell mode, not the leader", uuid);
  }
  Consensus* consensus = tablet_peer()->consensus();
  if (consensus == nullptr) {
    return STATUS(IllegalState, "Consensus has not been initialized yet");
  }
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    return STATUS_SUBSTITUTE(IllegalState,
        "Not the leader. Local UUID: $0, Consensus state: $1", uuid, cstate.ShortDebugString());
  }
  if (PREDICT_FALSE(leader_ready_term_ != cstate.current_term())) {
    return STATUS_SUBSTITUTE(ServiceUnavailable,
        "Leader not yet ready to serve requests: ready term $0 vs cstate term $1",
        leader_ready_term_, cstate.current_term());
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
  // Shutdown the Catalog Manager worker pool.
  if (worker_pool_) {
    worker_pool_->Shutdown();
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
    AppendValuesFromMap(*table_ids_map_, &copy);
  }
  AbortAndWaitForAllTasks(copy);

  // Shut down the underlying storage for tables and tablets.
  if (sys_catalog_) {
    sys_catalog_->Shutdown();
  }

  // Reset the tasks tracker.
  tasks_tracker_->Reset();

  if (initdb_future_ && initdb_future_->wait_for(0s) != std::future_status::ready) {
    LOG(WARNING) << "initdb is still running, waiting for it to complete.";
    initdb_future_->wait();
    LOG(INFO) << "Finished running initdb, proceeding with catalog manager shutdown.";
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
  table->AbortTasksAndClose();
  table->WaitTasksCompletion();

  std::lock_guard<LockType> l(lock_);

  // Call AbortMutation() manually, as otherwise the lock won't be released.
  for (TabletInfo* tablet : tablets) {
    tablet->mutable_metadata()->AbortMutation();
  }
  table->mutable_metadata()->AbortMutation();
  auto tablet_map_checkout = tablet_map_.CheckOut();
  for (const TabletId& tablet_id_to_erase : tablet_ids_to_erase) {
    CHECK_EQ(tablet_map_checkout->erase(tablet_id_to_erase), 1)
        << "Unable to erase tablet " << tablet_id_to_erase << " from tablet map.";
  }

  auto table_ids_map_checkout = table_ids_map_.CheckOut();
  CHECK_EQ(table_names_map_.erase({table_namespace_id, table_name}), 1)
      << "Unable to erase table named " << table_name << " from table names map.";
  CHECK_EQ(table_ids_map_checkout->erase(table_id), 1)
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

Status CatalogManager::AddIndexInfoToTable(const scoped_refptr<TableInfo>& indexed_table,
                                           const IndexInfoPB& index_info,
                                           CreateTableResponsePB* resp) {
  TRACE("Locking indexed table");
  auto l = DCHECK_NOTNULL(indexed_table)->LockForWrite();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
  PartitionSchema partition_schema;
  std::vector<Partition> partitions;

  std::lock_guard<LockType> l(lock_);
  TRACE("Acquired catalog manager lock");
  parent_table_info = FindPtrOrNull(*table_ids_map_,
                                    schema.table_properties().CopartitionTableId());
  if (parent_table_info == nullptr) {
    s = STATUS(NotFound, "The object does not exist",
               schema.table_properties().CopartitionTableId());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  scoped_refptr<TableInfo> this_table_info;
  std::vector<TabletInfo *> tablets;
  TabletInfos scoped_ref_tablets;
  // Verify that the table does not exist.
  this_table_info = FindPtrOrNull(table_names_map_, {namespace_id, req.name()});

  if (this_table_info != nullptr) {
    s = STATUS_SUBSTITUTE(AlreadyPresent,
        "Object '$0.$1' already exists",
        GetNamespaceNameUnlocked(this_table_info), this_table_info->name());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
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

  LOG(INFO) << "Successfully created table " << this_table_info->ToString()
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

  // Verify no hash partition schema is specified.
  if (req->partition_schema().has_hash_schema()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS(InvalidArgument,
                             "PostgreSQL system catalog tables are non-partitioned"));
  }

  if (req->table_type() != TableType::PGSQL_TABLE_TYPE) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS_FORMAT(
                          InvalidArgument,
                          "Expected table type to be PGSQL_TABLE_TYPE ($0), got $1 ($2)",
                          PGSQL_TABLE_TYPE,
                          TableType_Name(req->table_type())));

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
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT,
                        STATUS_SUBSTITUTE(AlreadyPresent,
                            "Object '$0.$1' already exists", ns->name(), table->name()));
    }

    RETURN_NOT_OK(CreateTableInMemory(*req, schema, partition_schema, false /* create_tablets */,
                                      namespace_id, partitions, nullptr /* index_info */,
                                      nullptr /* tablets */, resp, &table));

    scoped_refptr<TabletInfo> tablet = tablet_map_->find(kSysCatalogTabletId)->second;
    auto tablet_lock = tablet->LockForWrite();
    tablet_lock->mutable_data()->pb.add_table_ids(table->id());
    table->AddTablet(tablet.get());

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

  tserver::ChangeMetadataRequestPB change_req;
  change_req.set_tablet_id(kSysCatalogTabletId);
  auto& add_table = *change_req.mutable_add_table();

  add_table.set_table_id(req->table_id());
  add_table.set_table_type(TableType::PGSQL_TABLE_TYPE);
  add_table.set_table_name(req->name());
  SchemaToPB(schema, add_table.mutable_schema());
  add_table.set_schema_version(0);

  partition_schema.ToPB(add_table.mutable_partition_schema());

  RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
      &change_req, sys_catalog_->tablet_peer().get(), leader_ready_term_));

  if (initial_snapshot_writer_) {
    initial_snapshot_writer_->AddMetadataChange(change_req);
  }
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
    SharedLock<LockType> l(lock_);
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

Status CatalogManager::GetYsqlCatalogConfig(const GetYsqlCatalogConfigRequestPB* req,
                                            GetYsqlCatalogConfigResponsePB* resp,
                                            rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());
  VLOG(1) << "GetYsqlCatalogConfig request: " << req->ShortDebugString();
  auto l = CHECK_NOTNULL(ysql_catalog_config_.get())->LockForRead();
  resp->set_version(l->data().pb.ysql_catalog_config().version());

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

    if (PROTO_IS_INDEX(l->data().pb)) {
      const uint32_t indexed_table_oid =
        VERIFY_RESULT(GetPgsqlTableOid(PROTO_GET_INDEXED_TABLE_ID(l->data().pb)));
      const TableId indexed_table_id = GetPgsqlTableId(database_oid, indexed_table_oid);

      // Set index_info.
      // Previously created INDEX wouldn't have the attribute index_info.
      if (l->data().pb.has_index_info()) {
        table_req.mutable_index_info()->CopyFrom(l->data().pb.index_info());
        table_req.mutable_index_info()->set_indexed_table_id(indexed_table_id);
      }

      // Set depricated field for index_info.
      table_req.set_indexed_table_id(indexed_table_id);
      table_req.set_is_local_index(PROTO_GET_IS_LOCAL(l->data().pb));
      table_req.set_is_unique_index(PROTO_GET_IS_UNIQUE(l->data().pb));
    }

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

  const bool is_pg_table = orig_req->table_type() == PGSQL_TABLE_TYPE;
  const bool is_pg_catalog_table = is_pg_table && orig_req->is_pg_catalog_table();
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
  const char* const object_type = PROTO_PTR_IS_TABLE(orig_req) ? "table" : "index";

  // Copy the request, so we can fill in some defaults.
  CreateTableRequestPB req = *orig_req;

  // Lookup the namespace and verify if it exists.
  TRACE("Looking up namespace");
  scoped_refptr<NamespaceInfo> ns;
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req.namespace_(), &ns), resp);
  if (ns->database_type() != GetDatabaseTypeForTable(req.table_type())) {
    Status s = STATUS(NotFound, "Namespace not found");
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }
  NamespaceId namespace_id = ns->id();

  // For index table, find the table info
  scoped_refptr<TableInfo> indexed_table;
  if (PROTO_IS_INDEX(req)) {
    TRACE("Looking up indexed table");
    indexed_table = GetTableInfo(req.indexed_table_id());
    if (indexed_table == nullptr) {
      return STATUS_SUBSTITUTE(
            NotFound, "The indexed table $0 does not exist", req.indexed_table_id());
    }

    TRACE("Locking indexed table");
    auto l_indexed_tbl = indexed_table->LockForRead();
    RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l_indexed_tbl.get(), resp));
  }

  // Validate schema.
  Schema client_schema;
  RETURN_NOT_OK(SchemaFromPB(req.schema(), &client_schema));
  RETURN_NOT_OK(ValidateCreateTableSchema(client_schema, resp));

  // checking that referenced user-defined types (if any) exist.
  {
    SharedLock<LockType> l(lock_);
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

  // If neither hash nor range schema have been specified by the protobuf request, we assume the
  // table uses a hash schema, and we use the table_type and hash_key to determine the hashing
  // scheme (redis or multi-column) that should be used.
  if (!req.partition_schema().has_hash_schema() && !req.partition_schema().has_range_schema()) {
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
    num_tablets = ts_descs.size() * (is_pg_table ? FLAGS_ysql_num_shards_per_tserver
                                                 : FLAGS_yb_num_shards_per_tserver);
    LOG(INFO) << "Setting default tablets to " << num_tablets << " with "
              << ts_descs.size() << " primary servers";
  }

  // Create partitions.
  PartitionSchema partition_schema;
  vector<Partition> partitions;
  s = PartitionSchema::FromPB(req.partition_schema(), schema, &partition_schema);
  if (req.partition_schema().has_hash_schema()) {
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
  } else if (req.partition_schema().has_range_schema()) {
    vector<YBPartialRow> split_rows;
    RETURN_NOT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));
    DCHECK_EQ(1, partitions.size());
  } else {
    DFATAL_OR_RETURN_NOT_OK(STATUS(InvalidArgument, "Invalid partition method"));
  }

  // Validate the table placement rules are a subset of the cluster ones.
  s = ValidateTableReplicationInfo(req.replication_info());
  if (PREDICT_FALSE(!s.ok())) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  // For index table, populate the index info.
  IndexInfoPB index_info;

  if (req.has_index_info()) {
    // Current message format.
    index_info.CopyFrom(req.index_info());

    // Assign column-ids that have just been computed and assigned to "index_info".
    if (!is_pg_table) {
      DCHECK_EQ(index_info.columns().size(), schema.num_columns())
        << "Number of columns are not the same between index_info and index_schema";
      // int colidx = 0;
      for (int colidx = 0; colidx < schema.num_columns(); colidx++) {
        index_info.mutable_columns(colidx)->set_column_id(schema.column_id(colidx));
      }
    }
  } else if (req.has_indexed_table_id()) {
    // Old client message format when rolling upgrade (Not having "index_info").
    IndexInfoBuilder index_info_builder(&index_info);
    index_info_builder.ApplyProperties(req.indexed_table_id(),
        req.is_local_index(), req.is_unique_index());
    if (orig_req->table_type() != PGSQL_TABLE_TYPE) {
      Schema indexed_schema;
      RETURN_NOT_OK(indexed_table->GetSchema(&indexed_schema));
      RETURN_NOT_OK(index_info_builder.ApplyColumnMapping(indexed_schema, schema));
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
      s = STATUS_SUBSTITUTE(AlreadyPresent,
              "Object '$0.$1' already exists", ns->name(), table->name());
      // If the table already exists, we set the response table_id field to the id of the table that
      // already exists. This is necessary because before we return the error to the client (or
      // success in case of a "CREATE TABLE IF NOT EXISTS" request) we want to wait for the existing
      // table to be available to receive requests. And we need the table id for that.
      resp->set_table_id(table->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
    }

    RETURN_NOT_OK(CreateTableInMemory(req, schema, partition_schema, true /* create_tablets */,
                                      namespace_id, partitions, &index_info,
                                      &tablets, resp, &table));
  }

  if (PREDICT_FALSE(FLAGS_simulate_slow_table_create_secs > 0)) {
    LOG(INFO) << "Simulating slow table creation";
    SleepFor(MonoDelta::FromSeconds(FLAGS_simulate_slow_table_create_secs));
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
  if ((req.has_index_info() || req.has_indexed_table_id()) && !is_pg_table) {
    s = AddIndexInfoToTable(indexed_table, index_info, resp);
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
    RETURN_NOT_OK(permissions_manager_->GrantPermissions(
        req.creator_role_name(),
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

  if (FLAGS_master_enable_metrics_snapshotter &&
      !(req.table_type() == TableType::YQL_TABLE_TYPE &&
        namespace_id == kSystemNamespaceId &&
        req.name() == kMetricsSnapshotsTableName)) {
    Status s = CreateMetricsSnapshotsTableIfNeeded(rpc);
    if (!s.ok()) {
      return s.CloneAndPrepend("Error while creating metrics snapshots table");
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
  auto tablet_map_checkout = tablet_map_.CheckOut();
  for (TabletInfo* tablet : *tablets) {
    InsertOrDie(tablet_map_checkout.get_ptr(), tablet->tablet_id(), tablet);
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
    return SetupError(resp->mutable_error(), MasterErrorPB::TOO_MANY_TABLETS, s);
  }

  // Verify that the number of replicas isn't larger than the number of live tablet
  // servers.
  if (FLAGS_catalog_manager_check_ts_count_for_create_table &&
      num_replicas > num_live_tservers) {
    msg = Substitute("Not enough live tablet servers to create table with replication factor $0. "
                     "$1 tablet servers are alive.", num_replicas, num_live_tservers);
    LOG(WARNING) << msg;
    s = STATUS(InvalidArgument, msg);
    return SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s);
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
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
      }
    }

    if (minimum_sum > num_replicas) {
      msg = Substitute("Sum of minimum replicas per placement ($0) is greater than num_replicas "
                       " ($1)", minimum_sum, num_replicas);
      s = STATUS(InvalidArgument, msg);
      LOG(WARNING) << msg;
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
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
  *table = CreateTableInfo(req, schema, partition_schema, namespace_id, index_info);
  const TableId& table_id = (*table)->id();
  auto table_ids_map_checkout = table_ids_map_.CheckOut();
  (*table_ids_map_checkout)[table_id] = *table;
  // Do not add Postgres tables to the name map as the table name is not unique in a namespace.
  if (req.table_type() != PGSQL_TABLE_TYPE) {
    table_names_map_[{namespace_id, req.name()}] = *table;
  }

  if (create_tablets) {
    RETURN_NOT_OK(CreateTabletsFromTable(partitions, *table, tablets));
  }

  if (resp != nullptr) {
    resp->set_table_id(table_id);
  }

  HandleNewTableId(table_id);

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

Status CatalogManager::CreateMetricsSnapshotsTableIfNeeded(rpc::RpcContext *rpc) {
  TableIdentifierPB table_indentifier;
  table_indentifier.set_table_name(kMetricsSnapshotsTableName);
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
    req.set_name(kMetricsSnapshotsTableName);
    req.mutable_namespace_()->set_name(kSystemNamespaceName);
    req.set_table_type(TableType::YQL_TABLE_TYPE);

    // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
    // will use the same defaults as for regular tables.
    if (FLAGS_metrics_snapshots_table_num_tablets > 0) {
      req.set_num_tablets(FLAGS_metrics_snapshots_table_num_tablets);
    }

    // Schema description: "node" refers to tserver uuid. "entity_type" can be either
    // "tserver" or "table". "entity_id" is uuid of corresponding tserver or table.
    // "metric" is the name of the metric and "value" is its val. "ts" is time at
    // which the snapshot was recorded. "details" is a json column for future extensibility.

    YBSchemaBuilder schemaBuilder;
    schemaBuilder.AddColumn("node")->Type(STRING)->HashPrimaryKey()->NotNull();
    schemaBuilder.AddColumn("entity_type")->Type(STRING)->PrimaryKey()->NotNull();
    schemaBuilder.AddColumn("entity_id")->Type(STRING)->PrimaryKey()->NotNull();
    schemaBuilder.AddColumn("metric")->Type(STRING)->PrimaryKey()->NotNull();
    schemaBuilder.AddColumn("ts")->Type(TIMESTAMP)->PrimaryKey()->NotNull()->
      SetSortingType(ColumnSchema::SortingType::kDescending);
    schemaBuilder.AddColumn("value")->Type(INT64);
    schemaBuilder.AddColumn("details")->Type(JSONB);

    YBSchema ybschema;
    CHECK_OK(schemaBuilder.Build(&ybschema));

    auto schema = yb::client::internal::GetSchema(ybschema);
    SchemaToPB(schema, req.mutable_schema());

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
    Status s = STATUS(NotFound, "The object does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

  // 2. Verify if the create is in-progress.
  TRACE("Verify if the table creation is in progress for $0", table->ToString());
  resp->set_done(!table->IsCreateInProgress());

  // 3. Set any current errors, if we are experiencing issues creating the table. This will be
  // bubbled up to the MasterService layer. If it is an error, it gets wrapped around in
  // MasterErrorPB::UNKNOWN_ERROR.
  RETURN_NOT_OK(table->GetCreateTableErrorStatus());

  // 4. For index table, check if alter schema is done on the indexed table also.
  if (resp->done() && PROTO_IS_INDEX(l->data().pb)) {
    IsAlterTableDoneRequestPB alter_table_req;
    IsAlterTableDoneResponsePB alter_table_resp;
    alter_table_req.mutable_table()->set_table_id(PROTO_GET_INDEXED_TABLE_ID(l->data().pb));
    const Status s = IsAlterTableDone(&alter_table_req, &alter_table_resp);
    if (!s.ok()) {
      resp->mutable_error()->Swap(alter_table_resp.mutable_error());
      return s;
    }
    resp->set_done(alter_table_resp.done());
  }

  // If this is a transactional table we are not done until the transaction status table is created.
  if (resp->done() && l->data().pb.schema().table_properties().is_transactional()) {
    RETURN_NOT_OK(IsTransactionStatusTableCreated(resp));
  }

  // We are not done until the metrics snapshots table is created.
  if (FLAGS_master_enable_metrics_snapshotter && resp->done() &&
      !(table->GetTableType() == TableType::YQL_TABLE_TYPE &&
        table->namespace_id() == kSystemNamespaceId &&
        table->name() == kMetricsSnapshotsTableName)) {
    RETURN_NOT_OK(IsMetricsSnapshotsTableCreated(resp));
  }

  return Status::OK();
}

Status CatalogManager::IsTransactionStatusTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kTransactionsTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);

  return IsCreateTableDone(&req, resp);
}

Status CatalogManager::IsMetricsSnapshotsTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kMetricsSnapshotsTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);
  req.mutable_table()->mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_CQL);

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
        if (FindPtrOrNull(*table_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::TABLET:
        if (FindPtrOrNull(*tablet_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::UDTYPE:
        if (FindPtrOrNull(udtype_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntry::SNAPSHOT:
        return id;
      case SysRowEntry::CDC_STREAM:
        if (!CDCStreamExistsUnlocked(id)) return id;
        break;
      case SysRowEntry::UNKNOWN: FALLTHROUGH_INTENDED;
      case SysRowEntry::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntry::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntry::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
      case SysRowEntry::SYS_CONFIG:
        LOG(DFATAL) << "Invalid id type: " << *entity_type;
        return id;
    }
  }
}

scoped_refptr<TableInfo> CatalogManager::CreateTableInfo(const CreateTableRequestPB& req,
                                                         const Schema& schema,
                                                         const PartitionSchema& partition_schema,
                                                         const NamespaceId& namespace_id,
                                                         IndexInfoPB* index_info) {
  DCHECK(schema.has_column_ids());
  TableId table_id = !req.table_id().empty() ? req.table_id() : GenerateId(SysRowEntry::TABLE);
  scoped_refptr<TableInfo> table = NewTableInfo(table_id);
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
  SchemaToPB(schema, metadata->mutable_schema());
  partition_schema.ToPB(metadata->mutable_partition_schema());
  // For index table, set index details (indexed table id and whether the index is local).
  if (req.has_index_info()) {
    metadata->mutable_index_info()->CopyFrom(req.index_info());

    // Set the depricated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.index_info().indexed_table_id());
    metadata->set_is_local_index(req.index_info().is_local());
    metadata->set_is_unique_index(req.index_info().is_unique());

    // Setup index info.
    if (index_info != nullptr) {
      index_info->set_table_id(table->id());
      metadata->mutable_index_info()->CopyFrom(*index_info);
    }
  } else if (req.has_indexed_table_id()) {
    // Read data from the depricated field and update the new fields.
    metadata->mutable_index_info()->set_indexed_table_id(req.indexed_table_id());
    metadata->mutable_index_info()->set_is_local(req.is_local_index());
    metadata->mutable_index_info()->set_is_unique(req.is_unique_index());

    // Set the depricated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.indexed_table_id());
    metadata->set_is_local_index(req.is_local_index());
    metadata->set_is_unique_index(req.is_unique_index());

    // Setup index info.
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
  SharedLock<LockType> l(lock_);

  if (table_identifier.has_table_id()) {
    *table_info = FindPtrOrNull(*table_ids_map_, table_identifier.table_id());
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

Status CatalogManager::FindNamespaceUnlocked(const NamespaceIdentifierPB& ns_identifier,
                                             scoped_refptr<NamespaceInfo>* ns_info) const {
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

Status CatalogManager::FindNamespace(const NamespaceIdentifierPB& ns_identifier,
                                     scoped_refptr<NamespaceInfo>* ns_info) const {
  SharedLock<LockType> l(lock_);
  return FindNamespaceUnlocked(ns_identifier, ns_info);
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
    *error = MasterErrorPB::OBJECT_NOT_FOUND;
    return STATUS(NotFound, "Object does not exist", table_identifier.DebugString());
  }

  TRACE("Locking table");
  auto l = table_obj->LockForRead();

  if (table_obj->IsCreateInProgress()) {
    *error = MasterErrorPB::TABLE_CREATION_IS_IN_PROGRESS;
    return STATUS(IllegalState, "Table creation is in progress", table_obj->ToString());
  }

  if (ns) {
    TRACE("Looking up namespace");
    SharedLock<LockType> l(lock_);

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
  scoped_refptr<TableInfo> table = FindPtrOrNull(*table_ids_map_, table_id);
  if (table == nullptr) {
    Status s = STATUS_SUBSTITUTE(NotFound, "The object with id $0 does not exist", table_id);
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE(Substitute("Locking $0", table_type));
  auto l = table->LockForRead();
  DCHECK(is_index == PROTO_IS_INDEX(l->data().pb));
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
  LOG_WITH_PREFIX(INFO) << "Truncating tablet " << tablet->id();
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
  scoped_refptr<TableInfo> table = FindPtrOrNull(*table_ids_map_, req->table_id());

  if (table == nullptr) {
    Status s = STATUS(NotFound, "The object does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
    LOG(WARNING) << "Indexed table " << indexed_table_id << " for index "
                 << index_table_id << " not found";
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

  LOG(WARNING) << "Index " << index_table_id << " not found in indexed table " << indexed_table_id;
  return Status::OK();
}

// Delete a Table
//  - Update the table state to "DELETING".
//  - Issue DeleteTablet tasks to all said tablets.
//  - Update all the underlying tablet states as "DELETED".
//
// This order of events can help us guarantee that:
//  - If a table is DELETING/DELETED, we do not add further tasks to it.
//  - A DeleteTable is done when a table is either DELETING or DELETED and has no running tasks.
//  - If a table is DELETING and it has no tasks on it, then it is safe to mark DELETED.
//
// We are lazy about deletions.
//
// IMPORTANT: If modifying, consider updating DeleteYsqlDBTables(), the bulk deletion API.
Status CatalogManager::DeleteTable(const DeleteTableRequestPB* req,
                                   DeleteTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteTable request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  vector<scoped_refptr<TableInfo>> tables;
  vector<unique_ptr<TableInfo::lock_type>> table_locks;

  RETURN_NOT_OK(DeleteTableInMemory(req->table(), req->is_index_table(),
                                    true /* update_indexed_table */,
                                    &tables, &table_locks, resp, rpc));

  // Delete any CDC streams that are set up on this table.
  TRACE("Deleting CDC streams on table");
  RETURN_NOT_OK(DeleteCDCStreamsForTable(resp->table_id()));

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  for (int i = 0; i < table_locks.size(); i++) {
    table_locks[i]->Commit();
  }

  if (PREDICT_FALSE(FLAGS_catalog_manager_inject_latency_in_delete_table_ms > 0)) {
    LOG(INFO) << "Sleeping in CatalogManager::DeleteTable for " <<
        FLAGS_catalog_manager_inject_latency_in_delete_table_ms << " ms";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_inject_latency_in_delete_table_ms));
  }

  for (int i = 0; i < tables.size(); i++) {
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
  RETURN_NOT_OK(permissions_manager_->RemoveAllPermissionsForResource(canonical_resource, resp));

  LOG(INFO) << "Successfully initiated deletion of "
            << (req->is_index_table() ? "index" : "table") << " with "
            << req->table().DebugString() << " per request from " << RequestorString(rpc);
  // Asynchronously cleans up the final memory traces of the deleted database.
  background_tasks_->Wake();
  return Status::OK();
}

Status CatalogManager::DeleteTableInMemory(const TableIdentifierPB& table_identifier,
                                           const bool is_index_table,
                                           const bool update_indexed_table,
                                           vector<scoped_refptr<TableInfo>>* tables,
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
      LOG(WARNING) << "Index " << table_identifier.DebugString() << " not found";
      return Status::OK();
    } else {
      Status s = STATUS(NotFound, "The object does not exist", table_identifier.DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  TRACE(Substitute("Locking $0", object_type));
  auto l = table->LockForWrite();
  resp->set_table_id(table->id());

  if (is_index_table == PROTO_IS_TABLE(l->data().pb)) {
    Status s = STATUS(NotFound, "The object does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  if (l->data().started_deleting()) {
    if (cascade_delete_index) {
      LOG(WARNING) << "Index " << table_identifier.DebugString() << " was deleted";
      return Status::OK();
    } else {
      Status s = STATUS(NotFound, "The object was deleted", l->data().pb.state_msg());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
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

  // Update the internal table maps.
  // Exclude Postgres tables which are not in the name map.
  if (l->data().table_type() != PGSQL_TABLE_TYPE) {
    TRACE("Removing from by-name map");
    std::lock_guard<LockType> l_map(lock_);
    if (table_names_map_.erase({l->data().namespace_id(), l->data().name()}) != 1) {
      PANIC_RPC(rpc, "Could not remove table from map, name=" + table->ToString());
    }
    table_ids_map_.Commit();
  }

  // For regular (indexed) table, delete all its index tables if any. Else for index table, delete
  // index info from the indexed table.
  if (!is_index_table) {
    TableIdentifierPB index_identifier;
    for (auto index : l->data().pb.indexes()) {
      index_identifier.set_table_id(index.table_id());
      RETURN_NOT_OK(DeleteTableInMemory(index_identifier, true /* is_index_table */,
                                        false /* update_index_table */, tables,
                                        table_lcks, resp, rpc));
    }
  } else if (update_indexed_table) {
    s = DeleteIndexInfoFromTable(PROTO_GET_INDEXED_TABLE_ID(l->data().pb), table->id(), resp);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute("An error occurred while deleting index info: $0",
                                       s.ToString()));
      LOG(WARNING) << s.ToString();
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }
  }

  table->AbortTasks();

  // For regular (indexed) table, insert table info and lock in the front of the list. Else for
  // index table, append them to the end. We do so so that we will commit and delete the indexed
  // table first before its indexes.
  if (!is_index_table) {
    tables->insert(tables->begin(), table);
    table_lcks->insert(table_lcks->begin(), std::move(l));
  } else {
    tables->push_back(table);
    table_lcks->push_back(std::move(l));
  }

  return Status::OK();
}

void CatalogManager::CleanUpDeletedTables() {
  // TODO(bogdan): Cache tables being deleted to make this iterate only over those?
  vector<scoped_refptr<TableInfo>> tables_to_delete;
  {
    std::lock_guard<LockType> l_map(lock_);
    // Garbage collecting.
    // Going through all tables under the global lock.
    for (const auto& it : *table_ids_map_) {
      scoped_refptr<TableInfo> table(it.second);

      if (!table->HasTasks()) {
        // Lock the candidate table and check the tablets under the lock.
        auto l = table->LockForRead();

        // For normal runtime operations, this should only contain tables in DELETING state.
        // However, for master failover, the catalog loaders currently have to bring in tables in
        // memory even in DELETED state, for safely loading the respective tablets for them
        //
        // Eventually, for these DELETED tables, we'll want to also remove them from memory.
        if (l->data().started_deleting() && !l->data().is_deleted()) {
          // The current relevant order of operations during a DeleteTable is:
          // 1) Mark the table as DELETING
          // 2) Abort the current table tasks
          // 3) Per tablet, send DeleteTable requests to all TS, then mark that tablet as DELETED
          //
          // This creates a race, wherein, after 2, HasTasks can be false, but we still have not
          // gotten to point 3, which would add further tasks for the deletes.
          //
          // However, HasTasks is cheaper than AreAllTabletsDeleted...
          if (table->AreAllTabletsDeleted()) {
            tables_to_delete.push_back(table);
            // TODO(bogdan): uncomment this once we also untangle catalog loader logic.
            // Since we have lock_, this table cannot be in the map AND be DELETED.
            // DCHECK(!l->data().is_deleted());
          }
        }
      }
    }
  }
  // Mark the tables as DELETED and remove them from the in-memory maps.
  vector<TableInfo*> tables_to_update_on_disk;
  for (auto table : tables_to_delete) {
    table->mutable_metadata()->StartMutation();
    // TODO: Turn this into a DCHECK again once we fix the catalog loaders to not need to load
    // DELETED tables as well.
    if (table->metadata().state().pb.state() == SysTablesEntryPB::DELETING) {
      // Update the metadata for the on-disk state.
      LOG(INFO) << "Marking table as DELETED: " << table->ToString();
      table->mutable_metadata()->mutable_dirty()->set_state(SysTablesEntryPB::DELETED,
          Substitute("Deleted with tablets at $0", LocalTimeAsString()));
      tables_to_update_on_disk.push_back(table.get());
    } else {
      // TODO(bogdan): We need to abort here, until we remove it from the map, otherwise we'd leave
      // this lock locked...
      table->mutable_metadata()->AbortMutation();
    }
  }
  if (tables_to_update_on_disk.size() > 0) {
    Status s = sys_catalog_->UpdateItems(tables_to_update_on_disk, leader_ready_term_);
    if (!s.ok()) {
      LOG(WARNING) << "Error marking tables as DELETED: " << s.ToString();
      return;
    }
  }
  // Delete from in-memory maps.
  // TODO(bogdan):
  // - why do we not delete these from disk?
  // - do we even need to remove these? seems the loaders read them from disk into maps anyway...
  // - what about the tablets? is it ok to have TabletInfos with missing tables for them?
  {
    std::lock_guard<LockType> l_map(lock_);
    for (auto table : tables_to_delete) {
      // TODO(bogdan): Come back to this once we figure out all concurrency issues.
      // table_ids_map_.erase(table->id());
    }
  }
  // Update the table in-memory info as DELETED after we've removed them from the maps.
  for (auto table : tables_to_update_on_disk) {
    table->mutable_metadata()->CommitMutation();
  }
  // TODO: Check if we want to delete the totally deleted table from the sys_catalog here.
}

Status CatalogManager::IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                         IsDeleteTableDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  // Lookup the deleted table.
  TRACE("Looking up table $0", req->table_id());
  std::lock_guard<LockType> l_map(lock_);
  scoped_refptr<TableInfo> table = FindPtrOrNull(*table_ids_map_, req->table_id());

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
    Status s = STATUS(IllegalState, "The object was NOT deleted", l->data().pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  if (l->data().is_deleted()) {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": totally deleted";
      resp->set_done(true);
  } else {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": deleting tablets";
    std::vector<std::shared_ptr<TSDescriptor>> descs;
    master_->ts_manager()->GetAllDescriptors(&descs);
    for (auto& ts_desc : descs) {
      LOG(INFO) << "Deleting on " << ts_desc->permanent_uuid() << ": "
                << ts_desc->PendingTabletDeleteToString();
    }

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
          return STATUS_SUBSTITUTE(InvalidArgument,
              "column $0: client should not specify column id", new_col_pb.ShortDebugString());
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
        return STATUS_SUBSTITUTE(InvalidArgument, "Invalid alter step type: $0", step.type());
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
    Status s = STATUS(NotFound, "The object does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
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
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
      Status s = STATUS_SUBSTITUTE(AlreadyPresent,
          "Object '$0.$1' already exists",
          GetNamespaceNameUnlocked(new_namespace_id), other_table->name());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
    }

    // Acquire the new table name (now we have 2 name for the same table).
    table_names_map_[{new_namespace_id, new_table_name}] = table;
    l->mutable_data()->pb.set_namespace_id(new_namespace_id);
    l->mutable_data()->pb.set_name(new_table_name);

    has_changes = true;
  }

  // TODO(hector): Simplify the AlterSchema workflow to avoid doing the same checks on every layer
  // this request goes through: https://github.com/YugaByte/yugabyte-db/issues/1882.
  if (req->has_wal_retention_secs()) {
    if (has_changes) {
      const Status s = STATUS(InvalidArgument,
          "wal_retention_secs cannot be altered concurrently with other properties");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
    // TODO(hector): Handle co-partitioned tables:
    // https://github.com/YugaByte/yugabyte-db/issues/1905.
    l->mutable_data()->pb.set_wal_retention_secs(req->wal_retention_secs());
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
    SchemaToPB(new_schema, l->mutable_data()->pb.mutable_schema());
  }

  // Only increment the version number if it is a schema change (AddTable change goes through a
  // different path and it's not processed here).
  if (!req->has_wal_retention_secs()) {
    l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  }
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
    Status s = STATUS(NotFound, "The object does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
    Status s = STATUS(NotFound, "The object does not exist", req->table().DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

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
    resp->set_obsolete_indexed_table_id(PROTO_GET_INDEXED_TABLE_ID(l->data().pb));
  }

  // Get namespace name by id.
  SharedLock<LockType> l_map(lock_);
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

  bool has_rel_filter = req->relation_type_filter_size() > 0;
  bool include_user_table = has_rel_filter ? false : true;
  bool include_user_index = has_rel_filter ? false : true;
  bool include_system_table = req->exclude_system_tables() ? false
      : (has_rel_filter ? false : true);

  for (const auto &relation : req->relation_type_filter()) {
    if (relation == SYSTEM_TABLE_RELATION) {
      include_system_table = true;
    } else if (relation == USER_TABLE_RELATION) {
      include_user_table = true;
    } else if (relation == INDEX_TABLE_RELATION) {
      include_user_index = true;
    }
  }

  SharedLock<LockType> l(lock_);
  RelationType relation_type;

  for (const auto& entry : *table_ids_map_) {
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

    if (IsUserIndexUnlocked(table_info)) {
      if (!include_user_index) {
        continue;
      }
      relation_type = INDEX_TABLE_RELATION;
    } else if (IsUserTableUnlocked(table_info)) {
      if (!include_user_table) {
        continue;
      }
      relation_type = USER_TABLE_RELATION;
    } else {
      if (!include_system_table) {
        continue;
      }
      relation_type = SYSTEM_TABLE_RELATION;
    }

    scoped_refptr<NamespaceInfo> ns;
    NamespaceIdentifierPB ns_identifier;
    ns_identifier.set_id(ltm->data().namespace_id());
    auto s = FindNamespaceUnlocked(ns_identifier, &ns);
    if (ns.get() == nullptr) {
      if (PREDICT_FALSE(FLAGS_return_error_if_namespace_not_found)) {
        RETURN_NAMESPACE_NOT_FOUND(s, resp);
      }
      LOG(ERROR) << "Unable to find namespace with id " << ltm->data().namespace_id()
                 << " for table " << ltm->data().name();
      continue;
    }

    ListTablesResponsePB::TableInfo *table = resp->add_tables();
    {
      auto l = ns->LockForRead();
      table->mutable_namespace_()->set_id(ns->id());
      table->mutable_namespace_()->set_name(ns->name());
    }
    table->set_id(entry.second->id());
    table->set_name(ltm->data().name());
    table->set_table_type(ltm->data().table_type());
    table->set_relation_type(relation_type);
  }
  return Status::OK();
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfo(const TableId& table_id) {
  SharedLock<LockType> l(lock_);
  return FindPtrOrNull(*table_ids_map_, table_id);
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfoFromNamespaceNameAndTableName(
    const NamespaceName& namespace_name, const TableName& table_name) {

  SharedLock<LockType> l(lock_);
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_names_map_, namespace_name);
  if (ns == nullptr) {
    return nullptr;
  }
  return FindPtrOrNull(table_names_map_, {ns->id(), table_name});
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfoUnlocked(const TableId& table_id) {
  return FindPtrOrNull(*table_ids_map_, table_id);
}

void CatalogManager::GetAllTables(std::vector<scoped_refptr<TableInfo>> *tables,
                                  bool includeOnlyRunningTables) {
  tables->clear();
  SharedLock<LockType> l(lock_);
  for (const auto& e : *table_ids_map_) {
    if (includeOnlyRunningTables && !e.second->is_running()) {
      continue;
    }
    tables->push_back(e.second);
  }
}

void CatalogManager::GetAllNamespaces(std::vector<scoped_refptr<NamespaceInfo>>* namespaces) {
  namespaces->clear();
  SharedLock<LockType> l(lock_);
  for (const NamespaceInfoMap::value_type& e : namespace_ids_map_) {
    namespaces->push_back(e.second);
  }
}

void CatalogManager::GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo>>* types) {
  types->clear();
  SharedLock<LockType> l(lock_);
  for (const UDTypeInfoMap::value_type& e : udtype_ids_map_) {
    types->push_back(e.second);
  }
}

std::vector<std::shared_ptr<MonitoredTask>> CatalogManager::GetRecentTasks() {
  return tasks_tracker_->GetTasks();
}

NamespaceName CatalogManager::GetNamespaceNameUnlocked(const NamespaceId& id) const  {
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, id);
  return ns == nullptr ? NamespaceName() : ns->name();
}

NamespaceName CatalogManager::GetNamespaceName(const NamespaceId& id) const {
  TRACE("Acquired catalog manager lock");
  SharedLock<LockType> l(lock_);
  return GetNamespaceNameUnlocked(id);
}

NamespaceName CatalogManager::GetNamespaceNameUnlocked(
    const scoped_refptr<TableInfo>& table) const  {
  return GetNamespaceNameUnlocked(table->namespace_id());
}

NamespaceName CatalogManager::GetNamespaceName(const scoped_refptr<TableInfo>& table) const {
  return GetNamespaceName(table->namespace_id());
}

bool CatalogManager::IsSystemTableUnlocked(const TableInfo& table) const {
  TabletInfos tablets;
  table.GetAllTablets(&tablets);
  for (const auto& tablet : tablets) {
    if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
      return true;
    }
  }
  return false;
}

// True if table is created by user.
// Table can be regular table or index in this case.
bool CatalogManager::IsUserCreatedTable(const TableInfo& table) const {
  SharedLock<LockType> l(lock_);
  return IsUserCreatedTableUnlocked(table);
}

bool CatalogManager::IsUserCreatedTableUnlocked(const TableInfo& table) const {
  if (table.GetTableType() == PGSQL_TABLE_TYPE || table.GetTableType() == YQL_TABLE_TYPE) {
    if (!IsSystemTableUnlocked(table) && !IsSequencesSystemTable(table) &&
        GetNamespaceNameUnlocked(table.namespace_id()) != kSystemNamespaceName) {
      return true;
    }
  }
  return false;
}

bool CatalogManager::IsUserTable(const TableInfo& table) const {
  SharedLock<LockType> l(lock_);
  return IsUserTableUnlocked(table);
}

bool CatalogManager::IsUserTableUnlocked(const TableInfo& table) const {
  return IsUserCreatedTableUnlocked(table) && table.indexed_table_id().empty();
}

bool CatalogManager::IsUserIndex(const TableInfo& table) const {
  SharedLock<LockType> l(lock_);
  return IsUserIndexUnlocked(table);
}

bool CatalogManager::IsUserIndexUnlocked(const TableInfo& table) const {
  return IsUserCreatedTableUnlocked(table) && !table.indexed_table_id().empty();
}

bool CatalogManager::IsSequencesSystemTable(const TableInfo& table) const {
  if (table.GetTableType() == PGSQL_TABLE_TYPE) {
    // This case commonly occurs during unit testing. Avoid unnecessary assert within Get().
    if (!IsPgsqlId(table.namespace_id()) || !IsPgsqlId(table.id())) {
      LOG(WARNING) << "Not PGSQL IDs " << table.namespace_id() << ", " << table.id();
      return false;
    }
    Result<uint32_t> database_oid = GetPgsqlDatabaseOid(table.namespace_id());
    if (!database_oid.ok()) {
      LOG(WARNING) << "Invalid Namespace ID " << table.namespace_id();
      return false;
    }
    Result<uint32_t> table_oid = GetPgsqlTableOid(table.id());
    if (!table_oid.ok()) {
      LOG(WARNING) << "Invalid Table ID " << table.id();
      return false;
    }
    if (*database_oid == kPgSequencesDataDatabaseOid && *table_oid == kPgSequencesDataTableOid) {
      return true;
    }
  }
  return false;
}

void CatalogManager::NotifyTabletDeleteFinished(const TabletServerId& tserver_uuid,
                                                const TabletId& tablet_id) {
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
    RETURN_NOT_OK_PREPEND(HandleReportedTablet(ts_desc, reported, tablet_report,
                          report.is_incremental()),
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
                                            ReportedTabletUpdatesPB *report_updates,
                                            bool is_incremental) {
  TRACE_EVENT1("master", "HandleReportedTablet",
               "tablet_id", report.tablet_id());
  scoped_refptr<TabletInfo> tablet;
  {
    SharedLock<LockType> l(lock_);
    tablet = FindPtrOrNull(*tablet_map_, report.tablet_id());
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
  // If we received an incremental report, and the tablet is starting up, we will update the
  // replica so that the balancer knows how many tablets are in the middle of a remote bootstrap.
  if (report.has_committed_consensus_state()) {
    const ConsensusStatePB &prev_cstate = tablet_lock->data().pb.committed_consensus_state();
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
                << " from " << ts_desc->permanent_uuid();

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

      RETURN_NOT_OK(ResetTabletReplicasFromReportedConfig(
          *final_report, tablet, ts_desc->permanent_uuid(), tablet_lock.get(), table_lock.get()));

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
      LOG(INFO) << "Peer " << ts_desc->permanent_uuid() << " sent "
                << (is_incremental ? "incremental" : "full tablet")
                << " report for " << tablet->tablet_id()
                << ", prev state op id: " << prev_cstate.config().opid_index()
                << ", prev state term: " << prev_cstate.current_term()
                << ", prev state has_leader_uuid: " << prev_cstate.has_leader_uuid()
                << ". Consensus state: " << cstate.ShortDebugString();
      UpdateTabletReplica(ts_desc, report, tablet);
    }
  } else if (is_incremental &&
             (report.state() == tablet::NOT_STARTED || report.state() == tablet::BOOTSTRAPPING)) {
    // When a tablet server is restarted, it sends a full tablet report with all of its tablets in
    // the NOT_STARTED state, so this would make the load balancer think that all the tablets are
    // being remote bootstrapped at once. That's why we only process incremental reports here.
    UpdateTabletReplica(ts_desc, report, tablet);
    DCHECK(!tablet_lock->is_dirty()) << "Invalid modification of tablet";
  }

  table_lock->Unlock();

  if (tablet_lock->is_dirty()) {
    // We update the tablets each time that someone reports it.
    // This shouldn't be very frequent and should only happen when something in fact changed.
    Status s = sys_catalog_->UpdateItem(tablet.get(), leader_ready_term_);
    if (!s.ok()) {
      LOG(WARNING) << "Error updating tablets: " << s.ToString() << ". Tablet report was: "
                   << report.ShortDebugString();
      return s;
    }
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

    // Verify that the namespace does not exist except for namespace for YSQL database that is
    // identified by id only.
    if (req->database_type() != YQL_DATABASE_PGSQL) {
      ns = FindPtrOrNull(namespace_names_map_, req->name());
      if (ns != nullptr) {
        resp->set_id(ns->id());
        s = STATUS_SUBSTITUTE(AlreadyPresent, "Keyspace '$0' already exists", req->name());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_ALREADY_PRESENT, s);
      }
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

    // For namespace created for a Postgres database, save the list of tables and indexes for
    // for the database that need to be copied.
    if (req->database_type() == YQL_DATABASE_PGSQL) {
      if (req->source_namespace_id().empty()) {
        metadata->set_next_pg_oid(req->next_pg_oid());
      } else {
        const auto source_oid = GetPgsqlDatabaseOid(req->source_namespace_id());
        if (!source_oid.ok()) {
          return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                            source_oid.status());
        }
        for (const auto& iter : *table_ids_map_) {
          const auto& table_id = iter.first;
          const auto& table = iter.second;
          if (IsPgsqlId(table_id) && CHECK_RESULT(GetPgsqlDatabaseOid(table_id)) == *source_oid) {
            // Since indexes have dependencies on the base tables, put the tables in the front.
            const bool is_table = table->indexed_table_id().empty();
            pgsql_tables.insert(is_table ? pgsql_tables.begin() : pgsql_tables.end(), table);
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

    // Add the namespace to the in-memory map for the assignment. Namespaces for YSQL databases
    // are identified by the id only and should not be added to the name map.
    namespace_ids_map_[ns->id()] = ns;
    if (req->database_type() != YQL_DATABASE_PGSQL) {
      namespace_names_map_[req->name()] = ns;
    }

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
    RETURN_NOT_OK(permissions_manager_->GrantPermissions(
        req->creator_role_name(),
        get_canonical_keyspace(req->name()),
        req->name() /* resource name */,
        req->name() /* keyspace name */,
        all_permissions_for_resource(ResourceType::KEYSPACE),
        ResourceType::KEYSPACE,
        resp));
  }

  if (req->database_type() == YQL_DATABASE_PGSQL && !pgsql_tables.empty()) {
    RETURN_NOT_OK(CopyPgsqlSysTables(ns->id(), pgsql_tables, resp, rpc));
  }

  return Status::OK();
}

Status CatalogManager::DeleteNamespace(const DeleteNamespaceRequestPB* req,
                                       DeleteNamespaceResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteNamespace request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  if (req->has_database_type() && req->database_type() == YQL_DATABASE_PGSQL) {
    return DeleteYsqlDatabase(req, resp, rpc);
  }

  scoped_refptr<NamespaceInfo> ns;

  // Lookup the namespace and verify if it exists.
  TRACE("Looking up namespace");
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &ns), resp);

  if (ns->database_type() == YQL_DATABASE_PGSQL) {
    // A YSQL database is found, but this rpc requests to delete a non-PostgreSQL keyspace.
    Status s = STATUS(NotFound, "Namespace not found", ns->name());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  TRACE("Locking namespace");
  auto l = ns->LockForWrite();

  // Only empty namespace can be deleted.
  TRACE("Looking for tables in the namespace");
  {
    SharedLock<LockType> catalog_lock(lock_);

    for (const TableInfoMap::value_type& entry : *table_ids_map_) {
      auto ltm = entry.second->LockForRead();

      if (!ltm->data().started_deleting() && ltm->data().namespace_id() == ns->id()) {
        Status s = STATUS(InvalidArgument,
                          Substitute("Cannot delete namespace which has $0: $1 [id=$2]",
                                     PROTO_IS_TABLE(ltm->data().pb) ? "table" : "index",
                                     ltm->data().name(), entry.second->id()), req->DebugString());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_IS_NOT_EMPTY, s);
      }
    }
  }

  // Only empty namespace can be deleted.
  TRACE("Looking for types in the namespace");
  {
    SharedLock<LockType> catalog_lock(lock_);

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
  RETURN_NOT_OK(permissions_manager_->RemoveAllPermissionsForResource(canonical_resource, resp));

  LOG(INFO) << "Successfully deleted namespace " << ns->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::DeleteYsqlDatabase(const DeleteNamespaceRequestPB* req,
                                          DeleteNamespaceResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  // Lookup database.
  scoped_refptr<NamespaceInfo> database;
  RETURN_NAMESPACE_NOT_FOUND(FindNamespace(req->namespace_(), &database), resp);

  // Make sure this is a YSQL database.
  if (database->database_type() != YQL_DATABASE_PGSQL) {
    // A non-YSQL namespace is found, but the rpc requests to drop a YSQL database.
    Status s = STATUS(NotFound, "YSQL database not found", database->name());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  // Lock database before removing content.
  TRACE("Locking database");
  auto l = database->LockForWrite();

  // Delete all user tables in the database.
  TRACE("Delete all tables in YSQL database");
  RETURN_NOT_OK(DeleteYsqlDBTables(database, resp, rpc));

  // Dropping database.
  TRACE("Updating metadata on disk");
  // Update sys-catalog.
  Status s = sys_catalog_->DeleteItem(database.get(), leader_ready_term_);
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
    if (namespace_ids_map_.erase(database->id()) < 1) {
      PANIC_RPC(rpc, "Could not remove namespace from map, name=" + l->data().name());
    }
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l->Commit();

  // DROP completed. Return status.
  LOG(INFO) << "Successfully deleted namespace " << database->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::DeleteYsqlDBTables(const scoped_refptr<NamespaceInfo>& database,
                                          DeleteNamespaceResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  vector<pair<scoped_refptr<TableInfo>, unique_ptr<TableInfo::lock_type>>> user_tables;
  vector<scoped_refptr<TableInfo>> sys_tables;
  {
    // Lock the catalog to iterate over table_ids_map_.
    SharedLock<LockType> catalog_lock(lock_);

    // Delete tablets for each of user tables.
    for (const TableInfoMap::value_type& entry : *table_ids_map_) {
      scoped_refptr<TableInfo> table = entry.second;
      auto l = table->LockForWrite();
      if (l->data().namespace_id() != database->id() || l->data().started_deleting()) {
        continue;
      }

      if (IsSystemTableUnlocked(*table)) {
        sys_tables.push_back(table);
      } else {
        // For regular (indexed) table, insert table info and lock in the front of the list. Else
        // for index table, append them to the end. We do so so that we will commit and delete the
        // indexed table first before its indexes.
        if (PROTO_IS_TABLE(l->data().pb)) {
          user_tables.insert(user_tables.begin(), {table, std::move(l)});
        } else {
          user_tables.push_back({table, std::move(l)});
        }
      }
    }
  }
  // Remove the system tables from RAFT.
  TRACE("Sending system table delete RPCs");
  for (auto &table : sys_tables) {
    RETURN_NOT_OK(sys_catalog_->DeleteYsqlSystemTable(table->id()));
  }
  // Delete all tablets of user tables in the database as 1 batch RPC call.
  TRACE("Sending delete table batch RPC to sys catalog");
  vector<TableInfo *> user_tables_rpc;
  user_tables_rpc.reserve(user_tables.size());
  for (auto &tbl : user_tables) {
    user_tables_rpc.push_back(tbl.first.get());
  }
  Status s = sys_catalog_->UpdateItems(user_tables_rpc, leader_ready_term_);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  for (auto &table_and_lock : user_tables) {
    auto &table = table_and_lock.first;
    auto &l = table_and_lock.second;
    // cancel all table busywork and mark the table state as DELETING tablets.
    l->mutable_data()->set_state(SysTablesEntryPB::DELETING,
        Substitute("Started deleting at $0", LocalTimeAsString()));
    l->Commit();
    table->AbortTasks();
  }

  // Batch remove all CDC streams subscribed to the newly DELETING tables.
  TRACE("Deleting CDC streams on table");
  vector<TableId> id_list;
  id_list.reserve(user_tables.size());
  for (auto &table_and_lock : user_tables) {
    id_list.push_back(table_and_lock.first->id());
  }
  RETURN_NOT_OK(DeleteCDCStreamsForTables(id_list));

  // Send a DeleteTablet() RPC request to each tablet replica in the table.
  for (auto &table_and_lock : user_tables) {
    auto &table = table_and_lock.first;
    DeleteTabletsAndSendRequests(table);
  }

  // Invoke any background tasks and return (notably, table cleanup).
  background_tasks_->Wake();
  return Status::OK();
}

Status CatalogManager::ListNamespaces(const ListNamespacesRequestPB* req,
                                      ListNamespacesResponsePB* resp) {

  RETURN_NOT_OK(CheckOnline());

  SharedLock<LockType> l(lock_);

  for (const auto& entry : namespace_ids_map_) {
    const auto& namespace_info = *entry.second;
    auto ltm = namespace_info.LockForRead();
    // If the request asks for namespaces for a specific database type, filter by the type.
    if (req->has_database_type() && namespace_info.database_type() != req->database_type()) {
      continue;
    }

    NamespaceIdentifierPB *ns = resp->add_namespaces();
    ns->set_id(namespace_info.id());
    ns->set_name(namespace_info.name());
    ns->set_database_type(namespace_info.database_type());
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
    Status s = STATUS_SUBSTITUTE(NotFound, "Redis config for $0 does not exists", req->keyword());
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
    if (ns->database_type() != YQLDatabase::YQL_DATABASE_CQL) {
      Status s = STATUS(NotFound, "Namespace not found");
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
  }

  // Get all the referenced types (if any).
  std::vector<std::string> referenced_udts;
  for (const QLTypePB& field_type : req->field_types()) {
    QLType::GetUserDefinedTypeIds(field_type, /* transitive = */ true, &referenced_udts);
  }

  {
    TRACE("Acquired catalog manager lock");
    std::lock_guard<LockType> l(lock_);

    // Verify that the type does not exist.
    tp = FindPtrOrNull(udtype_names_map_, std::make_pair(ns->id(), req->name()));

    if (tp != nullptr) {
      s = STATUS_SUBSTITUTE(AlreadyPresent,
          "Type '$0.$1' already exists", ns->name(), req->name());
      return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_ALREADY_PRESENT, s);
    }

    // Verify that all referenced types actually exist.
    for (const auto& udt_id : referenced_udts) {
      if (FindPtrOrNull(udtype_ids_map_, udt_id) == nullptr) {
          // This may be caused by a stale cache (e.g. referenced type name resolves to an old,
          // deleted type). Return InvalidArgument so query layer will clear cache and retry.
          s = STATUS_SUBSTITUTE(InvalidArgument,
          "Type id '$0' referenced by type '$1' does not exist", udt_id, req->name());
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
      }
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
    for (const TableInfoMap::value_type& entry : *table_ids_map_) {
      auto ltm = entry.second->LockForRead();
      if (!ltm->data().started_deleting()) {
        for (const auto &col : ltm->data().schema().columns()) {
          if (col.type().main() == DataType::USER_DEFINED_TYPE &&
              col.type().udtype_info().id() == tp->id()) {
            Status s = STATUS(QLError,
                Substitute("Cannot delete type '$0.$1'. It is used in column $2 of table $3",
                    ns->name(), tp->name(), col.name(), ltm->data().name()));
            return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
          }
        }
      }
    }

    // Checking if any other type uses this type (i.e. in the case of nested types).
    // TODO: this could be more efficient.
    for (const UDTypeInfoMap::value_type& entry : udtype_ids_map_) {
      auto ltm = entry.second->LockForRead();

      for (int i = 0; i < ltm->data().field_types_size(); i++) {
        std::vector<std::string> referenced_udts;
        // Only need to check direct (non-transitive) type dependencies here.
        // This also means we report more precise errors for in-use types.
        QLType::GetUserDefinedTypeIds(ltm->data().field_types(i),
                                      false /* transitive */,
                                      &referenced_udts);
        auto it = std::find(referenced_udts.begin(), referenced_udts.end(), tp->id());
        if (it != referenced_udts.end()) {
          Status s = STATUS(QLError,
              Substitute("Cannot delete type '$0.$1'. It is used in field $2 of type '$3'",
                  ns->name(), tp->name(), ltm->data().field_names(i), ltm->data().name()));
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
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

  SharedLock<LockType> l(lock_);

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

// For non-enterprise builds, this is a no-op.
Status CatalogManager::DeleteCDCStreamsForTable(const TableId& table) {
  return Status::OK();
}

Status CatalogManager::DeleteCDCStreamsForTables(const vector<TableId>& table_ids) {
  return Status::OK();
}


bool CatalogManager::CDCStreamExistsUnlocked(const CDCStreamId& stream_id) {
  return false;
}

Result<uint64_t> CatalogManager::IncrementYsqlCatalogVersion() {

  auto l = CHECK_NOTNULL(ysql_catalog_config_.get())->LockForWrite();
  uint64_t new_version = l->data().pb.ysql_catalog_config().version() + 1;
  l->mutable_data()->pb.mutable_ysql_catalog_config()->set_version(new_version);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->UpdateItem(ysql_catalog_config_.get(), leader_ready_term_));
  l->Commit();

  return new_version;
}

Status CatalogManager::InitDbFinished(Status initdb_status, int64_t term) {
  if (initdb_status.ok()) {
    LOG(INFO) << "initdb completed successfully";
  } else {
    LOG(ERROR) << "initdb failed: " << initdb_status;
  }

  auto l = CHECK_NOTNULL(ysql_catalog_config_.get())->LockForWrite();
  auto* mutable_ysql_catalog_config = l->mutable_data()->pb.mutable_ysql_catalog_config();
  mutable_ysql_catalog_config->set_initdb_done(true);
  if (!initdb_status.ok()) {
    mutable_ysql_catalog_config->set_initdb_error(initdb_status.ToString());
  } else {
    mutable_ysql_catalog_config->clear_initdb_error();
  }

  RETURN_NOT_OK(sys_catalog_->AddItem(ysql_catalog_config_.get(), term));
  l->Commit();
  return Status::OK();
}

CHECKED_STATUS CatalogManager::IsInitDbDone(
    const IsInitDbDoneRequestPB* req,
    IsInitDbDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  auto l = CHECK_NOTNULL(ysql_catalog_config_.get())->LockForRead();
  const auto& ysql_catalog_config = l->data().pb.ysql_catalog_config();
  resp->set_pg_proc_exists(pg_proc_exists_.load(std::memory_order_acquire));
  resp->set_done(ysql_catalog_config.initdb_done());
  if (ysql_catalog_config.has_initdb_error() &&
      !ysql_catalog_config.initdb_error().empty()) {
    resp->set_initdb_error(ysql_catalog_config.initdb_error());
  }
  return Status::OK();
}

uint64_t CatalogManager::GetYsqlCatalogVersion() {
  auto l = ysql_catalog_config_->LockForRead();
  return l->data().pb.ysql_catalog_config().version();
}

Status CatalogManager::ResetTabletReplicasFromReportedConfig(
    const ReportedTabletPB& report,
    const scoped_refptr<TabletInfo>& tablet,
    const std::string& sender_uuid,
    TabletInfo::lock_type* tablet_lock,
    TableInfo::lock_type* table_lock) {

  DCHECK(tablet_lock->is_write_locked());
  ConsensusStatePB prev_cstate = tablet_lock->mutable_data()->pb.committed_consensus_state();
  const ConsensusStatePB& cstate = report.committed_consensus_state();
  *tablet_lock->mutable_data()->pb.mutable_committed_consensus_state() = cstate;

  TabletInfo::ReplicaMap replica_locations;
  TabletInfo::ReplicaMap rl;
  tablet->GetReplicaLocations(&rl);

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

    // Do not replace replicas in the NOT_STARTED or BOOTSTRAPPING state unless they are stale.
    bool create_new_replica = true;
    TabletReplica* existing_replica;
    auto it = peer.permanent_uuid() != sender_uuid ? rl.find(ts_desc->permanent_uuid()) : rl.end();
    if (it != rl.end()) {
      existing_replica = &it->second;
      // IsStarting returns true if state == NOT_STARTED or state == BOOTSTRAPPING.
      if (existing_replica->IsStarting() && !existing_replica->IsStale()) {
        create_new_replica = false;
      }
    }
    if (create_new_replica) {
      TabletReplica replica;
      NewReplica(ts_desc.get(), report, &replica);
      InsertOrDie(&replica_locations, replica.ts_desc->permanent_uuid(), replica);
    } else {
      InsertOrDie(&replica_locations, existing_replica->ts_desc->permanent_uuid(),
                  *existing_replica);
    }
  }

  tablet->SetReplicaLocations(std::move(replica_locations));
  tablet_locations_version_.fetch_add(1, std::memory_order_acq_rel);

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

void CatalogManager::UpdateTabletReplica(TSDescriptor* ts_desc,
                                         const ReportedTabletPB& report,
                                         const scoped_refptr<TabletInfo>& tablet) {
  TabletReplica replica;
  NewReplica(ts_desc, report, &replica);
  tablet->UpdateReplicaLocations(replica);
  tablet_locations_version_.fetch_add(1, std::memory_order_acq_rel);
}

void CatalogManager::NewReplica(TSDescriptor* ts_desc,
                                const ReportedTabletPB& report,
                                TabletReplica* replica) {
  // Tablets in state NOT_STARTED or BOOTSTRAPPING don't have a consensus.
  if (report.state() == tablet::NOT_STARTED || report.state() == tablet::BOOTSTRAPPING) {
    replica->role = RaftPeerPB::NON_PARTICIPANT;
    replica->member_type = RaftPeerPB::UNKNOWN_MEMBER_TYPE;
  } else {
    CHECK(report.has_committed_consensus_state()) << "No cstate: " << report.ShortDebugString();
    replica->role = GetConsensusRole(ts_desc->permanent_uuid(), report.committed_consensus_state());
    replica->member_type = GetConsensusMemberType(ts_desc->permanent_uuid(),
                                                  report.committed_consensus_state());
  }
  replica->state = report.state();
  replica->ts_desc = ts_desc;
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
    return STATUS_SUBSTITUTE(NotFound,
        "In shell mode: no tablet_id $0 exists in CatalogManager.", tablet_id);
  }

  if (sys_catalog_->tablet_id() == tablet_id && sys_catalog_->tablet_peer().get() != nullptr &&
      sys_catalog_->tablet_peer()->CheckRunning().ok()) {
    *ret_tablet_peer = tablet_peer();
  } else {
    return STATUS_SUBSTITUTE(NotFound,
        "no SysTable in the RUNNING state exists with tablet_id $0 in CatalogManager", tablet_id);
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
    return STATUS_SUBSTITUTE(AlreadyPresent,
        "Remote bootstrap of tablet $0 already in progress", tablet_id);
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
  RaftGroupMetadataPtr meta;
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

  LOG_WITH_PREFIX(INFO) << " Initiating remote bootstrap from peer " << bootstrap_peer_uuid
            << " (" << bootstrap_peer_addr.ToString() << ").";

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
  RETURN_NOT_OK(tserver::ShutdownAndTombstoneTabletPeerNotOk(
      sys_catalog_->OpenTablet(meta), sys_catalog_->tablet_peer(), meta,
      master_->fs_manager()->uuid(), "Remote bootstrap: Failed opening sys catalog"));

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
  LOG_WITH_PREFIX(INFO) << "Deleting tablet " << tablet_id << " on peer "
                        << ts_desc->permanent_uuid() << " with delete type "
                        << TabletDataState_Name(delete_type) << " (" << reason << ")";
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
  SharedLock<LockType> l(lock_);

  // TODO: At the moment we loop through all the tablets
  //       we can keep a set of tablets waiting for "assignment"
  //       or just a counter to avoid to take the lock and loop through the tablets
  //       if everything is "stable".

  for (const TabletInfoMap::value_type& entry : *tablet_map_) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    auto tablet_lock = tablet->LockForRead();

    if (!tablet->table()) {
      // Tablet is orphaned or in preparing state, continue.
      continue;
    }

    auto table_lock = tablet->table()->LockForRead();

    // If the table is deleted or the tablet was replaced at table creation time.
    if (tablet_lock->data().is_deleted() || table_lock->data().started_deleting()) {
      // Process this table deletion only once (tombstones for table may remain longer).
      if (table_ids_map_->find(tablet->table()->id()) != table_ids_map_->end()) {
        tablets_to_delete->push_back(tablet);
      }
      // Don't process deleted tables regardless.
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
    auto tablet_map_checkout = tablet_map_.CheckOut();
    (*tablet_map_checkout)[replacement->tablet_id()] = replacement;
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

  void Commit() {
    if (PREDICT_TRUE(!aborted_)) {
      for (const scoped_refptr<TabletInfo>& tablet : *tablets_) {
        tablet->mutable_metadata()->CommitMutation();
      }
    }
  }

  // Commit the transactions.
  ~ScopedTabletInfoCommitter() {
    Commit();
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
          "An error occurred while selecting replicas for tablet $0: $1",
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
    auto tablet_map_checkout = tablet_map_.CheckOut();
    for (const TabletId& tablet_id_to_remove : tablet_ids_to_remove) {
      CHECK_EQ(tablet_map_checkout->erase(tablet_id_to_remove), 1)
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
    return STATUS_SUBSTITUTE(InvalidArgument,
        "TableInfo for tablet $0 is not initialized (aborted CreateTable attempt?)",
        tablet->tablet_id());
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
  out << "Initial tserver uuids for tablet " << tablet->tablet_id() << ": ";
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
    return STATUS_SUBSTITUTE(InvalidArgument,
        "Not enough tablet servers in the requested placements. Need at least $0, have $1",
        nreplicas, ts_descs.size());
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
      return STATUS_SUBSTITUTE(InvalidArgument,
          "Not enough tablet servers in the requested placements. Need at least $0, have $1",
          nreplicas, all_allowed_ts.size());
    }

    // Loop through placements and assign to respective available TSs.
    for (const auto& entry : allowed_ts_by_pi) {
      const auto& available_ts_descs = entry.second;
      int num_replicas = pb_by_id[entry.first].min_num_replicas();
      num_replicas = num_replicas > 0 ? num_replicas : FLAGS_replication_factor;
      if (available_ts_descs.size() < num_replicas) {
        return STATUS_SUBSTITUTE(InvalidArgument,
            "Not enough tablet servers in $0. Need at least $1 but only have $2.", entry.first,
            num_replicas, available_ts_descs.size());
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

    TSRegistrationPB reg = ts->GetRegistration();

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
      TSInformationPB tsinfo_pb = *replica.second.ts_desc->GetTSInformationPB();

      TSInfoPB* out_ts_info = replica_pb->mutable_ts_info();
      out_ts_info->set_permanent_uuid(tsinfo_pb.tserver_instance().permanent_uuid());
      TakeRegistration(tsinfo_pb.mutable_registration()->mutable_common(), out_ts_info);
      out_ts_info->set_placement_uuid(tsinfo_pb.registration().common().placement_uuid());
      *out_ts_info->mutable_capabilities() = std::move(
          *tsinfo_pb.mutable_registration()->mutable_capabilities());
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
    SharedLock<LockType> l(lock_);
    if (!FindCopy(*tablet_map_, tablet_id, &tablet_info)) {
      return STATUS_SUBSTITUTE(NotFound, "Unknown tablet $0", tablet_id);
    }
  }

  Status s = BuildLocationsForTablet(tablet_info, locs_pb);

  int num_replicas = 0;
  if (GetReplicationFactorForTablet(tablet_info, &num_replicas).ok() && num_replicas > 0 &&
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
    Status s = STATUS(NotFound, "The object does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  auto l = table->LockForRead();
  RETURN_NOT_OK(CheckIfTableDeletedOrNotRunning(l.get(), resp));

  vector<scoped_refptr<TabletInfo>> tablets_in_range;
  table->GetTabletsInRange(req, &tablets_in_range);

  bool require_tablets_runnings = req->require_tablets_running();
  for (const scoped_refptr<TabletInfo>& tablet : tablets_in_range) {
    auto status = BuildLocationsForTablet(tablet, resp->add_tablet_locations());
    if (!status.ok()) {
      // Not running.
      if (require_tablets_runnings) {
        resp->mutable_tablet_locations()->Clear();
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, status);
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
    SharedLock<LockType> l(lock_);
    namespace_ids_copy = namespace_ids_map_;
    ids_copy = *table_ids_map_;
    names_copy = table_names_map_;
    tablets_copy = *tablet_map_;
  }

  *out << "Dumping current state of master.\nNamespaces:\n";
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

Status CatalogManager::PeerStateDump(const vector<RaftPeerPB>& peers,
                                     const DumpMasterStateRequestPB* req,
                                     DumpMasterStateResponsePB* resp) {
  std::unique_ptr<MasterServiceProxy> peer_proxy;
  Endpoint sockaddr;
  MonoTime timeout = MonoTime::Now();
  DumpMasterStateRequestPB peer_req;
  rpc::RpcController rpc;

  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
  rpc.set_deadline(timeout);
  peer_req.set_on_disk(req->on_disk());
  peer_req.set_return_dump_as_string(req->return_dump_as_string());
  string dump;

  for (const RaftPeerPB& peer : peers) {
    HostPort hostport = HostPortFromPB(DesiredHostPort(peer, master_->MakeCloudInfoPB()));
    peer_proxy.reset(new MasterServiceProxy(&master_->proxy_cache(), hostport));

    DumpMasterStateResponsePB peer_resp;
    rpc.Reset();

    peer_proxy->DumpState(peer_req, &peer_resp, &rpc);

    if (peer_resp.has_error()) {
      LOG(WARNING) << "Hit err " << peer_resp.ShortDebugString() << " during peer "
        << peer.ShortDebugString() << " state dump.";
      return StatusFromPB(peer_resp.error().status());
    } else if (req->return_dump_as_string()) {
      dump += peer_resp.dump();
    }
  }

  if (req->return_dump_as_string()) {
    resp->set_dump(resp->dump() + dump);
  }
  return Status::OK();
}

void CatalogManager::ReportMetrics() {
  // Report metrics on how many tservers are alive.
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  const int32 num_live_servers = ts_descs.size();
  metric_num_tablet_servers_live_->set_value(num_live_servers);

  master_->ts_manager()->GetAllDescriptors(&ts_descs);
  metric_num_tablet_servers_dead_->set_value(ts_descs.size() - num_live_servers);
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
    LOG(WARNING) << "Overwriting " << blacklistState.ToString()
                 << " with new size " << blacklist.hosts_size()
                 << " and initial load " << blacklist.initial_replica_load() << ".";
    blacklistState.Reset();
  }

  LOG(INFO) << "Set blacklist size = " << blacklist.hosts_size() << " with load "
            << blacklist.initial_replica_load() << " for num_tablets = " << tablet_map_->size();

  for (const auto& pb : blacklist.hosts()) {
    blacklistState.tservers_.insert(HostPortFromPB(pb));
  }

  return Status::OK();
}

Status CatalogManager::SetLeaderBlacklist(const BlacklistPB& leader_blacklist) {
  if (!leaderBlacklistState.tservers_.empty()) {
    LOG(WARNING) << "Overwriting " << leaderBlacklistState.ToString()
                 << " with new size " << leader_blacklist.hosts_size()
                 << " and initial load " << leader_blacklist.initial_leader_load() << ".";
    leaderBlacklistState.Reset();
  }

  LOG(INFO) << "Set leader blacklist size = " << leader_blacklist.hosts_size() << " with load "
            << leader_blacklist.initial_leader_load() << " for num_tablets = "
            << tablet_map_->size();

  for (const auto& pb : leader_blacklist.hosts()) {
    leaderBlacklistState.tservers_.insert(HostPortFromPB(pb));
  }

  return Status::OK();
}

Status CatalogManager::SetClusterConfig(
    const ChangeMasterClusterConfigRequestPB* req, ChangeMasterClusterConfigResponsePB* resp) {
  SysClusterConfigEntryPB config(req->cluster_config());

  // Save the list of blacklisted servers to be used for completion checking.
  if (config.has_server_blacklist()) {
    RETURN_NOT_OK(SetBlackList(config.server_blacklist()));

    config.mutable_server_blacklist()->set_initial_replica_load(
        GetNumRelevantReplicas(blacklistState, false /* leaders_only */));
    blacklistState.initial_load_ = config.server_blacklist().initial_replica_load();
  }

  // Save the list of leader blacklist to be used for completion checking.
  if (config.has_leader_blacklist()) {
    RETURN_NOT_OK(SetLeaderBlacklist(config.leader_blacklist()));

    config.mutable_leader_blacklist()->set_initial_leader_load(
        GetNumRelevantReplicas(leaderBlacklistState, true /* leaders_only */));
    leaderBlacklistState.initial_load_ = config.leader_blacklist().initial_leader_load();
  }

  auto l = cluster_config_->LockForWrite();
  // We should only set the config, if the caller provided us with a valid update to the
  // existing config.
  if (l->data().pb.version() != config.version()) {
    Status s = STATUS_SUBSTITUTE(IllegalState,
      "Config version does not match, got $0, but most recent one is $1. Should call Get again",
      config.version(), l->data().pb.version());
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

Status CatalogManager::GetReplicationFactorForTablet(const scoped_refptr<TabletInfo>& tablet,
    int* num_replicas) {
  // For system tables, the set of replicas is always the set of masters.
  if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
    consensus::ConsensusStatePB master_consensus;
    RETURN_NOT_OK(GetCurrentConfig(&master_consensus));
    *num_replicas = master_consensus.config().peers().size();
    return Status::OK();
  }
  return GetReplicationFactor(num_replicas);
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
    Status s = STATUS_SUBSTITUTE(IllegalState,
        "Found $0, which is below the expected number of servers $1.",
        ts_descs.size(), req->expected_num_servers());
    return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
  }

  Status s = load_balance_policy_->IsIdle();
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
  }

  return Status::OK();
}

Status CatalogManager::IsLoadBalancerIdle(const IsLoadBalancerIdleRequestPB* req,
                                          IsLoadBalancerIdleResponsePB* resp) {
  Status s = load_balance_policy_->IsIdle();
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::LOAD_BALANCER_RECENTLY_ACTIVE, s);
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

int64_t CatalogManager::GetNumRelevantReplicas(const BlacklistState& state, bool leaders_only) {
  int64_t res = 0;
  std::lock_guard <LockType> tablet_map_lock(lock_);
  for (const TabletInfoMap::value_type& entry : *tablet_map_) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    auto l = tablet->LockForRead();
    // Not checking being created on purpose as we do not want initial load to be under accounted.
    if (!tablet->table() ||
        PREDICT_FALSE(l->data().is_deleted())) {
      continue;
    }

    TabletInfo::ReplicaMap locs;
    tablet->GetReplicaLocations(&locs);
    for (const TabletInfo::ReplicaMap::value_type& replica : locs) {
      if (leaders_only && replica.second.role != RaftPeerPB::LEADER) {
        continue;
      }
      TSRegistrationPB reg = replica.second.ts_desc->GetRegistration();
      bool found = false;
      for (const auto& hp : reg.common().private_rpc_addresses()) {
        if (state.tservers_.count(HostPortFromPB(hp)) != 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        for (const auto& hp : reg.common().broadcast_addresses()) {
          if (state.tservers_.count(HostPortFromPB(hp)) != 0) {
            found = true;
            break;
          }
        }
      }
      if (found) {
        res++;
      }
    }
  }

  return res;
}

Status CatalogManager::FillHeartbeatResponse(const TSHeartbeatRequestPB* req,
                                             TSHeartbeatResponsePB* resp) {
  return Status::OK();
}

Status CatalogManager::GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp) {
  return GetLoadMoveCompletionPercent(resp, false);
}

Status CatalogManager::GetLeaderBlacklistCompletionPercent(GetLoadMovePercentResponsePB* resp) {
  return GetLoadMoveCompletionPercent(resp, true);
}

Status CatalogManager::GetLoadMoveCompletionPercent(GetLoadMovePercentResponsePB* resp,
    bool blacklist_leader) {
  BlacklistState& state = (blacklist_leader) ? leaderBlacklistState : blacklistState;
  int64_t blacklist_replicas = GetNumRelevantReplicas(state, blacklist_leader);

  // On change of master leader, initial_load_ information may be lost temporarily. Reset to
  // current value to avoid reporting progress percent as 100. Note that doing so will report
  // progress percent as 0 instead.
  if (state.initial_load_ < blacklist_replicas) {
    LOG(INFO) << "Reset blacklist initial load from " << state.initial_load_
      <<  " to " << blacklist_replicas;
    state.initial_load_ = blacklist_replicas;
  }

  LOG(INFO) << "Blacklisted count " << blacklist_replicas << " in " << tablet_map_->size()
            << " tablets, across " << state.tservers_.size()
            << " servers, with initial load " << state.initial_load_;

  // Case when a blacklisted servers did not have any starting load.
  if (state.initial_load_ == 0) {
    resp->set_percent(100);
    return Status::OK();
  }

  resp->set_percent(
      100 - (static_cast<double>(blacklist_replicas) * 100 / state.initial_load_));
  resp->set_remaining(blacklist_replicas);
  resp->set_total(state.initial_load_);

  return Status::OK();
}

void CatalogManager::AbortAndWaitForAllTasks(const vector<scoped_refptr<TableInfo>>& tables) {
  for (const auto& t : tables) {
    VLOG(1) << "Aborting tasks for table " << t;
    t->AbortTasksAndClose();
  }
  for (const auto& t : tables) {
    VLOG(1) << "Waiting on Aborting tasks for table " << t;
    t->WaitTasksCompletion();
  }
  VLOG(1) << "Waiting on Aborting tasks done";
}

void CatalogManager::HandleNewTableId(const TableId& table_id) {
  if (table_id == kPgProcTableId) {
    // Needed to track whether initdb has started running.
    pg_proc_exists_.store(true, std::memory_order_release);
  }
}

scoped_refptr<TableInfo> CatalogManager::NewTableInfo(TableId id) {
  return make_scoped_refptr<TableInfo>(id, tasks_tracker_);
}

}  // namespace master
}  // namespace yb
