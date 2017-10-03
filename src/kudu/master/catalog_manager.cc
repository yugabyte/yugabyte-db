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

#include "kudu/master/catalog_manager.h"

#include <boost/optional.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <glog/logging.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "kudu/cfile/type_encodings.h"
#include "kudu/common/partial_row.h"
#include "kudu/common/partition.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus.proxy.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/escaping.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/sysinfo.h"
#include "kudu/gutil/walltime.h"
#include "kudu/master/master.h"
#include "kudu/master/master.pb.h"
#include "kudu/master/sys_catalog.h"
#include "kudu/master/ts_descriptor.h"
#include "kudu/master/ts_manager.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/tserver/tserver_admin.proxy.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/logging.h"
#include "kudu/util/monotime.h"
#include "kudu/util/random_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/thread.h"
#include "kudu/util/threadpool.h"
#include "kudu/util/thread_restrictions.h"
#include "kudu/util/trace.h"

DEFINE_int32(master_ts_rpc_timeout_ms, 30 * 1000, // 30 sec
             "Timeout used for the Master->TS async rpc calls.");
TAG_FLAG(master_ts_rpc_timeout_ms, advanced);

DEFINE_int32(tablet_creation_timeout_ms, 30 * 1000, // 30 sec
             "Timeout used by the master when attempting to create tablet "
             "replicas during table creation.");
TAG_FLAG(tablet_creation_timeout_ms, advanced);

DEFINE_bool(catalog_manager_wait_for_new_tablets_to_elect_leader, true,
            "Whether the catalog manager should wait for a newly created tablet to "
            "elect a leader before considering it successfully created. "
            "This is disabled in some tests where we explicitly manage leader "
            "election.");
TAG_FLAG(catalog_manager_wait_for_new_tablets_to_elect_leader, hidden);

DEFINE_int32(unresponsive_ts_rpc_timeout_ms, 60 * 60 * 1000, // 1 hour
             "After this amount of time, the master will stop attempting to contact "
             "a tablet server in order to perform operations such as deleting a tablet.");
TAG_FLAG(unresponsive_ts_rpc_timeout_ms, advanced);

DEFINE_int32(default_num_replicas, 3,
             "Default number of replicas for tables that do not have the num_replicas set.");
TAG_FLAG(default_num_replicas, advanced);

DEFINE_int32(catalog_manager_bg_task_wait_ms, 1000,
             "Amount of time the catalog manager background task thread waits "
             "between runs");
TAG_FLAG(catalog_manager_bg_task_wait_ms, hidden);

DEFINE_int32(max_create_tablets_per_ts, 20,
             "The number of tablets per TS that can be requested for a new table.");
TAG_FLAG(max_create_tablets_per_ts, advanced);

DEFINE_bool(catalog_manager_allow_local_consensus, true,
            "Use local consensus when config size == 1");
TAG_FLAG(catalog_manager_allow_local_consensus, hidden);

DEFINE_int32(master_failover_catchup_timeout_ms, 30 * 1000, // 30 sec
             "Amount of time to give a newly-elected leader master to load"
             " the previous master's metadata and become active. If this time"
             " is exceeded, the node crashes.");
TAG_FLAG(master_failover_catchup_timeout_ms, advanced);
TAG_FLAG(master_failover_catchup_timeout_ms, experimental);

DEFINE_bool(master_tombstone_evicted_tablet_replicas, true,
            "Whether the Master should tombstone (delete) tablet replicas that "
            "are no longer part of the latest reported raft config.");
TAG_FLAG(master_tombstone_evicted_tablet_replicas, hidden);

DEFINE_bool(master_add_server_when_underreplicated, true,
            "Whether the master should attempt to add a new server to a tablet "
            "config when it detects that the tablet is under-replicated.");
TAG_FLAG(master_add_server_when_underreplicated, hidden);

DEFINE_bool(catalog_manager_check_ts_count_for_create_table, true,
            "Whether the master should ensure that there are enough live tablet "
            "servers to satisfy the provided replication count before allowing "
            "a table to be created.");
TAG_FLAG(catalog_manager_check_ts_count_for_create_table, hidden);

using std::shared_ptr;
using std::string;
using std::vector;

namespace kudu {
namespace master {

using base::subtle::NoBarrier_Load;
using base::subtle::NoBarrier_CompareAndSwap;
using cfile::TypeEncodingInfo;
using consensus::kMinimumTerm;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::Consensus;
using consensus::ConsensusServiceProxy;
using consensus::ConsensusStatePB;
using consensus::GetConsensusRole;
using consensus::OpId;
using consensus::RaftPeerPB;
using consensus::StartRemoteBootstrapRequestPB;
using rpc::RpcContext;
using strings::Substitute;
using tablet::TABLET_DATA_DELETED;
using tablet::TABLET_DATA_TOMBSTONED;
using tablet::TabletDataState;
using tablet::TabletPeer;
using tablet::TabletStatePB;
using tserver::TabletServerErrorPB;

////////////////////////////////////////////////////////////
// Table Loader
////////////////////////////////////////////////////////////

class TableLoader : public TableVisitor {
 public:
  explicit TableLoader(CatalogManager *catalog_manager)
    : catalog_manager_(catalog_manager) {
  }

  virtual Status VisitTable(const std::string& table_id,
                            const SysTablesEntryPB& metadata) OVERRIDE {
    CHECK(!ContainsKey(catalog_manager_->table_ids_map_, table_id))
          << "Table already exists: " << table_id;

    // Setup the table info
    TableInfo *table = new TableInfo(table_id);
    TableMetadataLock l(table, TableMetadataLock::WRITE);
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the tablet to the IDs map and to the name map (if the table is not deleted)
    catalog_manager_->table_ids_map_[table->id()] = table;
    if (!l.data().is_deleted()) {
      catalog_manager_->table_names_map_[l.data().name()] = table;
    }

    LOG(INFO) << "Loaded metadata for table " << table->ToString();
    VLOG(1) << "Metadata for table " << table->ToString() << ": " << metadata.ShortDebugString();
    l.Commit();
    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TableLoader);
};

////////////////////////////////////////////////////////////
// Tablet Loader
////////////////////////////////////////////////////////////

class TabletLoader : public TabletVisitor {
 public:
  explicit TabletLoader(CatalogManager *catalog_manager)
    : catalog_manager_(catalog_manager) {
  }

  virtual Status VisitTablet(const std::string& table_id,
                             const std::string& tablet_id,
                             const SysTabletsEntryPB& metadata) OVERRIDE {
    // Lookup the table
    scoped_refptr<TableInfo> table(FindPtrOrNull(
                                     catalog_manager_->table_ids_map_, table_id));

    // Setup the tablet info
    TabletInfo* tablet = new TabletInfo(table, tablet_id);
    TabletMetadataLock l(tablet, TabletMetadataLock::WRITE);
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the tablet to the tablet manager
    catalog_manager_->tablet_map_[tablet->tablet_id()] = tablet;

    if (table == nullptr) {
      // if the table is missing and the tablet is in "preparing" state
      // may mean that the table was not created (maybe due to a failed write
      // for the sys-tablets). The cleaner will remove
      if (l.data().pb.state() == SysTabletsEntryPB::PREPARING) {
        LOG(WARNING) << "Missing Table " << table_id << " required by tablet " << tablet_id
                     << " (probably a failed table creation: the tablet was not assigned)";
        return Status::OK();
      }

      // if the tablet is not in a "preparing" state, something is wrong...
      LOG(ERROR) << "Missing Table " << table_id << " required by tablet " << tablet_id;
      LOG(ERROR) << "Metadata: " << metadata.DebugString();
      return Status::Corruption("Missing table for tablet: ", tablet_id);
    }

    // Add the tablet to the Table
    if (!l.mutable_data()->is_deleted()) {
      table->AddTablet(tablet);
    }
    l.Commit();

    // TODO(KUDU-1070): if we see a running tablet under a deleted table,
    // we should "roll forward" the deletion of the tablet here.

    TableMetadataLock table_lock(table.get(), TableMetadataLock::READ);

    LOG(INFO) << "Loaded metadata for tablet " << tablet_id
              << " (table " << table->ToString() << ")";
    VLOG(2) << "Metadata for tablet " << tablet_id << ": " << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TabletLoader);
};

////////////////////////////////////////////////////////////
// Background Tasks
////////////////////////////////////////////////////////////

class CatalogManagerBgTasks {
 public:
  explicit CatalogManagerBgTasks(CatalogManager *catalog_manager)
    : closing_(false), pending_updates_(false),
      thread_(nullptr), catalog_manager_(catalog_manager) {
  }

  ~CatalogManagerBgTasks() {}

  Status Init();
  void Shutdown();

  void Wake() {
    boost::lock_guard<boost::mutex> lock(lock_);
    pending_updates_ = true;
    cond_.notify_all();
  }

  void Wait(int msec) {
    boost::unique_lock<boost::mutex> lock(lock_);
    if (closing_) return;
    if (!pending_updates_) {
      boost::system_time wtime = boost::get_system_time() + boost::posix_time::milliseconds(msec);
      cond_.timed_wait(lock, wtime);
    }
    pending_updates_ = false;
  }

  void WakeIfHasPendingUpdates() {
    boost::lock_guard<boost::mutex> lock(lock_);
    if (pending_updates_) {
      cond_.notify_all();
    }
  }

 private:
  void Run();

 private:
  Atomic32 closing_;
  bool pending_updates_;
  mutable boost::mutex lock_;
  boost::condition_variable cond_;
  scoped_refptr<kudu::Thread> thread_;
  CatalogManager *catalog_manager_;
};

Status CatalogManagerBgTasks::Init() {
  RETURN_NOT_OK(kudu::Thread::Create("catalog manager", "bgtasks",
      &CatalogManagerBgTasks::Run, this, &thread_));
  return Status::OK();
}

void CatalogManagerBgTasks::Shutdown() {
  if (Acquire_CompareAndSwap(&closing_, false, true) != false) {
    VLOG(2) << "CatalogManagerBgTasks already shut down";
    return;
  }

  Wake();
  if (thread_ != nullptr) {
    CHECK_OK(ThreadJoiner(thread_.get()).Join());
  }
}

void CatalogManagerBgTasks::Run() {
  while (!NoBarrier_Load(&closing_)) {
    // Perform assignment processing.
    if (!catalog_manager_->IsInitialized()) {
      LOG(WARNING) << "Catalog manager is not initialized!";
    } else if (catalog_manager_->CheckIsLeaderAndReady().ok()) {
      std::vector<scoped_refptr<TabletInfo> > to_delete;
      std::vector<scoped_refptr<TabletInfo> > to_process;

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
      }
    } else {
      VLOG(1) << "We are no longer the leader, aborting the current task...";
    }

    //if (!to_delete.empty()) {
      // TODO: Run the cleaner
    //}

    // Wait for a notification or a timeout expiration.
    //  - CreateTable will call Wake() to notify about the tablets to add
    //  - HandleReportedTablet/ProcessPendingAssignments will call WakeIfHasPendingUpdates()
    //    to notify about tablets creation.
    Wait(FLAGS_catalog_manager_bg_task_wait_ms);
  }
  VLOG(1) << "Catalog manager background task thread shutting down";
}

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

namespace {

string RequestorString(RpcContext* rpc) {
  if (rpc) {
    return rpc->requestor_string();
  } else {
    return "internal request";
  }
}

// If 's' indicates that the node is no longer the leader, setup
// Service::UnavailableError as the error, set NOT_THE_LEADER as the
// error code and return true.
template<class RespClass>
void CheckIfNoLongerLeaderAndSetupError(Status s, RespClass* resp) {
  // TODO (KUDU-591): This is a bit of a hack, as right now
  // there's no way to propagate why a write to a consensus configuration has
  // failed. However, since we use Status::IllegalState()/IsAborted() to
  // indicate the situation where a write was issued on a node
  // that is no longer the leader, this suffices until we
  // distinguish this cause of write failure more explicitly.
  if (s.IsIllegalState() || s.IsAborted()) {
    Status new_status = Status::ServiceUnavailable(
        "operation requested can only be executed on a leader master, but this"
        " master is no longer the leader", s.ToString());
    SetupError(resp->mutable_error(), MasterErrorPB::NOT_THE_LEADER, new_status);
  }
}

} // anonymous namespace

CatalogManager::CatalogManager(Master *master)
  : master_(master),
    rng_(GetRandomSeed32()),
    state_(kConstructed),
    leader_ready_term_(-1) {
  CHECK_OK(ThreadPoolBuilder("leader-initialization")
           .set_max_threads(1)
           .Build(&worker_pool_));
}

CatalogManager::~CatalogManager() {
  Shutdown();
}

Status CatalogManager::Init(bool is_first_run) {
  {
    boost::lock_guard<simple_spinlock> l(state_lock_);
    CHECK_EQ(kConstructed, state_);
    state_ = kStarting;
  }

  RETURN_NOT_OK_PREPEND(InitSysCatalogAsync(is_first_run),
                        "Failed to initialize sys tables async");

  // WaitUntilRunning() must run outside of the lock as to prevent
  // deadlock. This is safe as WaitUntilRunning waits for another
  // thread to finish its work and doesn't itself depend on any state
  // within CatalogManager.

  RETURN_NOT_OK_PREPEND(sys_catalog_->WaitUntilRunning(),
                        "Failed waiting for the catalog tablet to run");

  boost::lock_guard<LockType> l(lock_);
  background_tasks_.reset(new CatalogManagerBgTasks(this));
  RETURN_NOT_OK_PREPEND(background_tasks_->Init(),
                        "Failed to initialize catalog manager background tasks");

  {
    boost::lock_guard<simple_spinlock> l(state_lock_);
    CHECK_EQ(kStarting, state_);
    state_ = kRunning;
  }

  return Status::OK();
}

Status CatalogManager::ElectedAsLeaderCb() {
  boost::lock_guard<simple_spinlock> l(state_lock_);
  return worker_pool_->SubmitClosure(
      Bind(&CatalogManager::VisitTablesAndTabletsTask, Unretained(this)));
}

Status CatalogManager::WaitUntilCaughtUpAsLeader(const MonoDelta& timeout) {
  string uuid = master_->fs_manager()->uuid();
  Consensus* consensus = sys_catalog_->tablet_peer()->consensus();
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  if (!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid) {
    return Status::IllegalState(
        Substitute("Node $0 not leader. Consensus state: $1",
                    uuid, cstate.ShortDebugString()));
  }

  // Wait for all transactions to be committed.
  RETURN_NOT_OK(sys_catalog_->tablet_peer()->transaction_tracker()->WaitForAllToFinish(timeout));
  return Status::OK();
}

void CatalogManager::VisitTablesAndTabletsTask() {

  Consensus* consensus = sys_catalog_->tablet_peer()->consensus();
  int64_t term = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED).current_term();
  Status s = WaitUntilCaughtUpAsLeader(
      MonoDelta::FromMilliseconds(FLAGS_master_failover_catchup_timeout_ms));
  if (!s.ok()) {
    WARN_NOT_OK(s, "Failed waiting for node to catch up after master election");
    // TODO: Abdicate on timeout instead of crashing.
    if (s.IsTimedOut()) {
      LOG(FATAL) << "Shutting down due to unavailability of other masters after"
                 << " election. TODO: Abdicate instead.";
    }
    return;
  }

  {
    boost::lock_guard<LockType> lock(lock_);
    int64_t term_after_wait = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED).current_term();
    if (term_after_wait != term) {
      // If we got elected leader again while waiting to catch up then we will
      // get another callback to visit the tables and tablets, so bail.
      LOG(INFO) << "Term change from " << term << " to " << term_after_wait
                << " while waiting for master leader catchup. Not loading sys catalog metadata";
      return;
    }

    LOG(INFO) << "Loading table and tablet metadata into memory...";
    LOG_SLOW_EXECUTION(WARNING, 1000, LogPrefix() + "Loading metadata into memory") {
      CHECK_OK(VisitTablesAndTabletsUnlocked());
    }
  }
  boost::lock_guard<simple_spinlock> l(state_lock_);
  leader_ready_term_ = term;
}

Status CatalogManager::VisitTablesAndTabletsUnlocked() {
  DCHECK(lock_.is_locked());

  // Clear the existing state.
  table_names_map_.clear();
  table_ids_map_.clear();
  tablet_map_.clear();

  // Visit tables and tablets, load them into memory.
  TableLoader table_loader(this);
  RETURN_NOT_OK_PREPEND(sys_catalog_->VisitTables(&table_loader),
                        "Failed while visiting tables in sys catalog");
  TabletLoader tablet_loader(this);
  RETURN_NOT_OK_PREPEND(sys_catalog_->VisitTablets(&tablet_loader),
                        "Failed while visiting tablets in sys catalog");
  return Status::OK();
}

Status CatalogManager::InitSysCatalogAsync(bool is_first_run) {
  boost::lock_guard<LockType> l(lock_);
  sys_catalog_.reset(new SysCatalogTable(master_,
                                         master_->metric_registry(),
                                         Bind(&CatalogManager::ElectedAsLeaderCb,
                                              Unretained(this))));
  if (is_first_run) {
    RETURN_NOT_OK(sys_catalog_->CreateNew(master_->fs_manager()));
  } else {
    RETURN_NOT_OK(sys_catalog_->Load(master_->fs_manager()));
  }
  return Status::OK();
}

bool CatalogManager::IsInitialized() const {
  boost::lock_guard<simple_spinlock> l(state_lock_);
  return state_ == kRunning;
}

Status CatalogManager::CheckIsLeaderAndReady() const {
  boost::lock_guard<simple_spinlock> l(state_lock_);
  if (PREDICT_FALSE(state_ != kRunning)) {
    return Status::ServiceUnavailable(
        Substitute("Catalog manager is shutting down. State: $0", state_));
  }
  Consensus* consensus = sys_catalog_->tablet_peer_->consensus();
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  string uuid = master_->fs_manager()->uuid();
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    return Status::IllegalState(
        Substitute("Not the leader. Local UUID: $0, Consensus state: $1",
                   uuid, cstate.ShortDebugString()));
  }
  if (PREDICT_FALSE(leader_ready_term_ != cstate.current_term())) {
    return Status::ServiceUnavailable("Leader not yet ready to serve requests");
  }
  return Status::OK();
}

RaftPeerPB::Role CatalogManager::Role() const {
  CHECK(IsInitialized());
  return sys_catalog_->tablet_peer_->consensus()->role();
}

void CatalogManager::Shutdown() {
  {
    boost::lock_guard<simple_spinlock> l(state_lock_);
    if (state_ == kClosing) {
      VLOG(2) << "CatalogManager already shut down";
      return;
    }
    state_ = kClosing;
  }

  // Shutdown the Catalog Manager background thread
  if (background_tasks_) {
    background_tasks_->Shutdown();
  }

  // Abort and Wait tables task completion
  for (const TableInfoMap::value_type& e : table_ids_map_) {
    e.second->AbortTasks();
    e.second->WaitTasksCompletion();
  }

  // Shut down the underlying storage for tables and tablets.
  if (sys_catalog_) {
    sys_catalog_->Shutdown();
  }
}

static void SetupError(MasterErrorPB* error,
                       MasterErrorPB::Code code,
                       const Status& s) {
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
}

Status CatalogManager::CheckOnline() const {
  if (PREDICT_FALSE(!IsInitialized())) {
    return Status::ServiceUnavailable("CatalogManager is not running");
  }
  return Status::OK();
}

void CatalogManager::AbortTableCreation(TableInfo* table,
                                        const vector<TabletInfo*>& tablets) {
  string table_id = table->id();
  string table_name = table->mutable_metadata()->mutable_dirty()->pb.name();
  vector<string> tablet_ids_to_erase;
  for (TabletInfo* tablet : tablets) {
    tablet_ids_to_erase.push_back(tablet->tablet_id());
  }

  LOG(INFO) << "Aborting creation of table '" << table_name << "', erasing table and tablets (" <<
      JoinStrings(tablet_ids_to_erase, ",") << ") from in-memory state.";

  // Since this is a failed creation attempt, it's safe to just abort
  // all tasks, as (by definition) no tasks may be pending against a
  // table that has failed to succesfully create.
  table->AbortTasks();
  table->WaitTasksCompletion();

  boost::lock_guard<LockType> l(lock_);

  // Call AbortMutation() manually, as otherwise the lock won't be
  // released.
  for (TabletInfo* tablet : tablets) {
    tablet->mutable_metadata()->AbortMutation();
  }
  table->mutable_metadata()->AbortMutation();
  for (const string& tablet_id_to_erase : tablet_ids_to_erase) {
    CHECK_EQ(tablet_map_.erase(tablet_id_to_erase), 1)
        << "Unable to erase tablet " << tablet_id_to_erase << " from tablet map.";
  }
  CHECK_EQ(table_names_map_.erase(table_name), 1)
      << "Unable to erase table named " << table_name << " from table names map.";
  CHECK_EQ(table_ids_map_.erase(table_id), 1)
      << "Unable to erase tablet with id " << table_id << " from tablet ids map.";
}

// Create a new table.
// See README file in this directory for a description of the design.
Status CatalogManager::CreateTable(const CreateTableRequestPB* orig_req,
                                   CreateTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());
  Status s;

  // Copy the request, so we can fill in some defaults.
  CreateTableRequestPB req = *orig_req;
  LOG(INFO) << "CreateTable from " << RequestorString(rpc)
            << ":\n" << req.DebugString();

  // a. Validate the user request.
  Schema client_schema;
  RETURN_NOT_OK(SchemaFromPB(req.schema(), &client_schema));
  if (client_schema.has_column_ids()) {
    s = Status::InvalidArgument("User requests should not have Column IDs");
    SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    return s;
  }
  if (PREDICT_FALSE(client_schema.num_key_columns() <= 0)) {
    s = Status::InvalidArgument("Must specify at least one key column");
    SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
        return s;
  }
  for (int i = 0; i < client_schema.num_key_columns(); i++) {
    if (!IsTypeAllowableInKey(client_schema.column(i).type_info())) {
        Status s = Status::InvalidArgument(
            "Key column may not have type of BOOL, FLOAT, or DOUBLE");
        SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
        return s;
    }
  }
  Schema schema = client_schema.CopyWithColumnIds();

  // If the client did not set a partition schema in the create table request,
  // the default partition schema (no hash bucket components and a range
  // partitioned on the primary key columns) will be used.
  PartitionSchema partition_schema;
  s = PartitionSchema::FromPB(req.partition_schema(), schema, &partition_schema);
  if (!s.ok()) {
    SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    return s;
  }

  // Decode split rows.
  vector<KuduPartialRow> split_rows;

  RowOperationsPBDecoder decoder(&req.split_rows(), &client_schema, &schema, nullptr);
  vector<DecodedRowOperation> ops;
  RETURN_NOT_OK(decoder.DecodeOperations(&ops));

  for (const DecodedRowOperation& op : ops) {
    if (op.type != RowOperationsPB::SPLIT_ROW) {
      Status s = Status::InvalidArgument(
          "Split rows must be specified as RowOperationsPB::SPLIT_ROW");
      SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
      return s;
    }

    split_rows.push_back(*op.split_row);
  }

  // Create partitions based on specified partition schema and split rows.
  vector<Partition> partitions;
  RETURN_NOT_OK(partition_schema.CreatePartitions(split_rows, schema, &partitions));

  // If they didn't specify a num_replicas, set it based on the default.
  if (!req.has_num_replicas()) {
    req.set_num_replicas(FLAGS_default_num_replicas);
  }

  // Verify that the total number of tablets is reasonable, relative to the number
  // of live tablet servers.
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  int num_live_tservers = ts_descs.size();
  int max_tablets = FLAGS_max_create_tablets_per_ts * num_live_tservers;
  if (req.num_replicas() > 1 && max_tablets > 0 && partitions.size() > max_tablets) {
    s = Status::InvalidArgument(Substitute("The requested number of tablets is over the "
                                           "permitted maximum ($0)", max_tablets));
    SetupError(resp->mutable_error(), MasterErrorPB::TOO_MANY_TABLETS, s);
    return s;
  }

  // Verify that the number of replicas isn't larger than the number of live tablet
  // servers.
  if (FLAGS_catalog_manager_check_ts_count_for_create_table &&
      req.num_replicas() > num_live_tservers) {
    s = Status::InvalidArgument(Substitute(
        "Not enough live tablet servers to create a table with the requested replication "
        "factor $0. $1 tablet servers are alive.", req.num_replicas(), num_live_tservers));
    SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s);
    return s;
  }

  scoped_refptr<TableInfo> table;
  vector<TabletInfo*> tablets;
  {
    boost::lock_guard<LockType> l(lock_);
    TRACE("Acquired catalog manager lock");

    // b. Verify that the table does not exist.
    table = FindPtrOrNull(table_names_map_, req.name());
    if (table != nullptr) {
      s = Status::AlreadyPresent("Table already exists", table->id());
      SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT, s);
      return s;
    }

    // c. Add the new table in "preparing" state.
    table = CreateTableInfo(req, schema, partition_schema);
    table_ids_map_[table->id()] = table;
    table_names_map_[req.name()] = table;

    // d. Create the TabletInfo objects in state PREPARING.
    for (const Partition& partition : partitions) {
      PartitionPB partition_pb;
      partition.ToPB(&partition_pb);
      tablets.push_back(CreateTabletInfo(table.get(), partition_pb));
    }

    // Add the table/tablets to the in-memory map for the assignment.
    resp->set_table_id(table->id());
    table->AddTablets(tablets);
    for (TabletInfo* tablet : tablets) {
      InsertOrDie(&tablet_map_, tablet->tablet_id(), tablet);
    }
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

  // e. Write Tablets to sys-tablets (in "preparing" state)
  s = sys_catalog_->AddTablets(tablets);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute("An error occurred while inserting to sys-tablets: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    AbortTableCreation(table.get(), tablets);
    CheckIfNoLongerLeaderAndSetupError(s, resp);
    return s;
  }
  TRACE("Wrote tablets to system table");

  // f. Update the on-disk table state to "running".
  table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
  s = sys_catalog_->AddTable(table.get());
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute("An error occurred while inserting to sys-tablets: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    AbortTableCreation(table.get(), tablets);
    CheckIfNoLongerLeaderAndSetupError(s, resp);
    return s;
  }
  TRACE("Wrote table to system table");

  // g. Commit the in-memory state.
  table->mutable_metadata()->CommitMutation();

  for (TabletInfo *tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
  }

  VLOG(1) << "Created table " << table->ToString();
  background_tasks_->Wake();
  return Status::OK();
}

Status CatalogManager::IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                         IsCreateTableDoneResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist", req->table().DebugString());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Locking table");
  TableMetadataLock l(table.get(), TableMetadataLock::READ);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted", l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  // 2. Verify if the create is in-progress
  TRACE("Verify if the table creation is in progress for $0", table->ToString());
  resp->set_done(!table->IsCreateInProgress());

  return Status::OK();
}

TableInfo *CatalogManager::CreateTableInfo(const CreateTableRequestPB& req,
                                           const Schema& schema,
                                           const PartitionSchema& partition_schema) {
  DCHECK(schema.has_column_ids());
  TableInfo* table = new TableInfo(GenerateId());
  table->mutable_metadata()->StartMutation();
  SysTablesEntryPB *metadata = &table->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTablesEntryPB::PREPARING);
  metadata->set_name(req.name());
  metadata->set_version(0);
  metadata->set_next_column_id(ColumnId(schema.max_col_id() + 1));
  metadata->set_num_replicas(req.num_replicas());
  // Use the Schema object passed in, since it has the column IDs already assigned,
  // whereas the user request PB does not.
  CHECK_OK(SchemaToPB(schema, metadata->mutable_schema()));
  partition_schema.ToPB(metadata->mutable_partition_schema());
  return table;
}

TabletInfo* CatalogManager::CreateTabletInfo(TableInfo* table,
                                             const PartitionPB& partition) {
  TabletInfo* tablet = new TabletInfo(table, GenerateId());
  tablet->mutable_metadata()->StartMutation();
  SysTabletsEntryPB *metadata = &tablet->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTabletsEntryPB::PREPARING);
  metadata->mutable_partition()->CopyFrom(partition);
  metadata->set_table_id(table->id());
  return tablet;
}

Status CatalogManager::FindTable(const TableIdentifierPB& table_identifier,
                                 scoped_refptr<TableInfo> *table_info) {
  boost::shared_lock<LockType> l(lock_);

  if (table_identifier.has_table_id()) {
    *table_info = FindPtrOrNull(table_ids_map_, table_identifier.table_id());
  } else if (table_identifier.has_table_name()) {
    *table_info = FindPtrOrNull(table_names_map_, table_identifier.table_name());
  } else {
    return Status::InvalidArgument("Missing Table ID or Table Name");
  }
  return Status::OK();
}

// Delete a Table
//  - Update the table state to "removed"
//  - Write the updated table metadata to sys-table
//
// we are lazy about deletions...
// the cleaner will remove tables and tablets marked as "removed"
Status CatalogManager::DeleteTable(const DeleteTableRequestPB* req,
                                   DeleteTableResponsePB* resp,
                                   rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteTable request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist", req->table().DebugString());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Locking table");
  TableMetadataLock l(table.get(), TableMetadataLock::WRITE);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted", l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Updating metadata on disk");
  // 2. Update the metadata for the on-disk state
  l.mutable_data()->set_state(SysTablesEntryPB::REMOVED,
                              Substitute("Deleted at $0", LocalTimeAsString()));

  // 3. Update sys-catalog with the removed table state.
  Status s = sys_catalog_->UpdateTable(table.get());
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    CheckIfNoLongerLeaderAndSetupError(s, resp);
    return s;
  }

  // 4. Remove it from the by-name map
  {
    TRACE("Removing from by-name map");
    boost::lock_guard<LockType> l_map(lock_);
    if (table_names_map_.erase(l.data().name()) != 1) {
      PANIC_RPC(rpc, "Could not remove table from map, name=" + l.data().name());
    }
  }

  table->AbortTasks();

  // 5. Update the in-memory state
  TRACE("Committing in-memory state");
  l.Commit();

  // Send a DeleteTablet() request to each tablet replica in the table.
  DeleteTabletsAndSendRequests(table);

  LOG(INFO) << "Successfully deleted table " << table->ToString()
            << " per request from " << RequestorString(rpc);
  background_tasks_->Wake();
  return Status::OK();
}

static Status ApplyAlterSteps(const SysTablesEntryPB& current_pb,
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
          return Status::InvalidArgument("ADD_COLUMN missing column info");
        }

        // Verify that encoding is appropriate for the new column's
        // type
        ColumnSchemaPB new_col_pb = step.add_column().schema();
        if (new_col_pb.has_id()) {
          return Status::InvalidArgument("column $0: client should not specify column ID",
                                         new_col_pb.ShortDebugString());
        }
        ColumnSchema new_col = ColumnSchemaFromPB(new_col_pb);
        const TypeEncodingInfo *dummy;
        RETURN_NOT_OK(TypeEncodingInfo::Get(new_col.type_info(),
                                            new_col.attributes().encoding,
                                            &dummy));

        // can't accept a NOT NULL column without read default
        if (!new_col.is_nullable() && !new_col.has_read_default()) {
          return Status::InvalidArgument(
              Substitute("column `$0`: NOT NULL columns must have a default", new_col.name()));
        }

        RETURN_NOT_OK(builder.AddColumn(new_col, false));
        break;
      }

      case AlterTableRequestPB::DROP_COLUMN: {
        if (!step.has_drop_column()) {
          return Status::InvalidArgument("DROP_COLUMN missing column info");
        }

        if (cur_schema.is_key_column(step.drop_column().name())) {
          return Status::InvalidArgument("cannot remove a key column");
        }

        RETURN_NOT_OK(builder.RemoveColumn(step.drop_column().name()));
        break;
      }

      case AlterTableRequestPB::RENAME_COLUMN: {
        if (!step.has_rename_column()) {
          return Status::InvalidArgument("RENAME_COLUMN missing column info");
        }

        // TODO: In theory we can rename a key
        if (cur_schema.is_key_column(step.rename_column().old_name())) {
          return Status::InvalidArgument("cannot rename a key column");
        }

        RETURN_NOT_OK(builder.RenameColumn(
                        step.rename_column().old_name(),
                        step.rename_column().new_name()));
        break;
      }

      // TODO: EDIT_COLUMN

      default: {
        return Status::InvalidArgument(
          Substitute("Invalid alter step type: $0", step.type()));
      }
    }
  }
  *new_schema = builder.Build();
  *next_col_id = builder.next_column_id();
  return Status::OK();
}

Status CatalogManager::AlterTable(const AlterTableRequestPB* req,
                                  AlterTableResponsePB* resp,
                                  rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterTable request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist", req->table().DebugString());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Locking table");
  TableMetadataLock l(table.get(), TableMetadataLock::WRITE);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted", l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  bool has_changes = false;
  string table_name = l.data().name();

  // 2. Calculate new schema for the on-disk state, not persisted yet
  Schema new_schema;
  ColumnId next_col_id = ColumnId(l.data().pb.next_column_id());
  if (req->alter_schema_steps_size()) {
    TRACE("Apply alter schema");
    Status s = ApplyAlterSteps(l.data().pb, req, &new_schema, &next_col_id);
    if (!s.ok()) {
      SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
      return s;
    }
    DCHECK_NE(next_col_id, 0);
    DCHECK_EQ(new_schema.find_column_by_id(next_col_id),
              static_cast<int>(Schema::kColumnNotFound));
    has_changes = true;
  }

  // 3. Try to acquire the new table name
  if (req->has_new_table_name()) {
    boost::lock_guard<LockType> catalog_lock(lock_);

    TRACE("Acquired catalog manager lock");

    // Verify that the table does not exist
    scoped_refptr<TableInfo> other_table = FindPtrOrNull(table_names_map_, req->new_table_name());
    if (other_table != nullptr) {
      Status s = Status::AlreadyPresent("Table already exists", other_table->id());
      SetupError(resp->mutable_error(), MasterErrorPB::TABLE_ALREADY_PRESENT, s);
      return s;
    }

    // Acquire the new table name (now we have 2 name for the same table)
    table_names_map_[req->new_table_name()] = table;
    l.mutable_data()->pb.set_name(req->new_table_name());

    has_changes = true;
  }

  // Skip empty requests...
  if (!has_changes) {
    return Status::OK();
  }

  // 4. Serialize the schema Increment the version number
  if (new_schema.initialized()) {
    if (!l.data().pb.has_fully_applied_schema()) {
      l.mutable_data()->pb.mutable_fully_applied_schema()->CopyFrom(l.data().pb.schema());
    }
    CHECK_OK(SchemaToPB(new_schema, l.mutable_data()->pb.mutable_schema()));
  }
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  l.mutable_data()->pb.set_next_column_id(next_col_id);
  l.mutable_data()->set_state(SysTablesEntryPB::ALTERING,
                              Substitute("Alter Table version=$0 ts=$1",
                                         l.mutable_data()->pb.version(),
                                         LocalTimeAsString()));

  // 5. Update sys-catalog with the new table schema.
  TRACE("Updating metadata on disk");
  Status s = sys_catalog_->UpdateTable(table.get());
  if (!s.ok()) {
    s = s.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog tables entry: $0",
                   s.ToString()));
    LOG(WARNING) << s.ToString();
    if (req->has_new_table_name()) {
      boost::lock_guard<LockType> catalog_lock(lock_);
      CHECK_EQ(table_names_map_.erase(req->new_table_name()), 1);
    }
    CheckIfNoLongerLeaderAndSetupError(s, resp);
    // TableMetadaLock follows RAII paradigm: when it leaves scope,
    // 'l' will be unlocked, and the mutation will be aborted.
    return s;
  }

  // 6. Remove the old name
  if (req->has_new_table_name()) {
    TRACE("Removing old-name $0 from by-name map", table_name);
    boost::lock_guard<LockType> l_map(lock_);
    if (table_names_map_.erase(table_name) != 1) {
      PANIC_RPC(rpc, "Could not remove table from map, name=" + l.data().name());
    }
  }

  // 7. Update the in-memory state
  TRACE("Committing in-memory state");
  l.Commit();

  SendAlterTableRequest(table);
  return Status::OK();
}

Status CatalogManager::IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                        IsAlterTableDoneResponsePB* resp,
                                        rpc::RpcContext* rpc) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist", req->table().DebugString());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Locking table");
  TableMetadataLock l(table.get(), TableMetadataLock::READ);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted", l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  // 2. Verify if the alter is in-progress
  TRACE("Verify if there is an alter operation in progress for $0", table->ToString());
  resp->set_schema_version(l.data().pb.version());
  resp->set_done(l.data().pb.state() != SysTablesEntryPB::ALTERING);

  return Status::OK();
}

Status CatalogManager::GetTableSchema(const GetTableSchemaRequestPB* req,
                                      GetTableSchemaResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  scoped_refptr<TableInfo> table;

  // 1. Lookup the table and verify if it exists
  TRACE("Looking up table");
  RETURN_NOT_OK(FindTable(req->table(), &table));
  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist", req->table().DebugString());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TRACE("Locking table");
  TableMetadataLock l(table.get(), TableMetadataLock::READ);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted", l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  if (l.data().pb.has_fully_applied_schema()) {
    // An AlterTable is in progress; fully_applied_schema is the last
    // schema that has reached every TS.
    CHECK(l.data().pb.state() == SysTablesEntryPB::ALTERING);
    resp->mutable_schema()->CopyFrom(l.data().pb.fully_applied_schema());
  } else {
    // There's no AlterTable, the regular schema is "fully applied".
    resp->mutable_schema()->CopyFrom(l.data().pb.schema());
  }
  resp->set_num_replicas(l.data().pb.num_replicas());
  resp->set_table_id(table->id());
  resp->mutable_partition_schema()->CopyFrom(l.data().pb.partition_schema());
  resp->set_create_table_done(!table->IsCreateInProgress());

  return Status::OK();
}

Status CatalogManager::ListTables(const ListTablesRequestPB* req,
                                  ListTablesResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  boost::shared_lock<LockType> l(lock_);

  for (const TableInfoMap::value_type& entry : table_names_map_) {
    TableMetadataLock ltm(entry.second.get(), TableMetadataLock::READ);
    if (!ltm.data().is_running()) continue;

    if (req->has_name_filter()) {
      size_t found = ltm.data().name().find(req->name_filter());
      if (found == string::npos) {
        continue;
      }
    }

    ListTablesResponsePB::TableInfo *table = resp->add_tables();
    table->set_id(entry.second->id());
    table->set_name(ltm.data().name());
  }

  return Status::OK();
}

bool CatalogManager::GetTableInfo(const string& table_id, scoped_refptr<TableInfo> *table) {
  boost::shared_lock<LockType> l(lock_);
  *table = FindPtrOrNull(table_ids_map_, table_id);
  return *table != nullptr;
}

void CatalogManager::GetAllTables(std::vector<scoped_refptr<TableInfo> > *tables) {
  tables->clear();
  boost::shared_lock<LockType> l(lock_);
  for (const TableInfoMap::value_type& e : table_ids_map_) {
    tables->push_back(e.second);
  }
}

bool CatalogManager::TableNameExists(const string& table_name) {
  boost::shared_lock<LockType> l(lock_);
  return table_names_map_.find(table_name) != table_names_map_.end();
}

void CatalogManager::NotifyTabletDeleteSuccess(const string& permanent_uuid,
                                               const string& tablet_id) {
  // TODO: Clean up the stale deleted tablet data once all relevant tablet
  // servers have responded that they have removed the remnants of the deleted
  // tablet.
}

Status CatalogManager::ProcessTabletReport(TSDescriptor* ts_desc,
                                           const TabletReportPB& report,
                                           TabletReportUpdatesPB *report_update,
                                           RpcContext* rpc) {
  TRACE_EVENT2("master", "ProcessTabletReport",
               "requestor", rpc->requestor_string(),
               "num_tablets", report.updated_tablets_size());

  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Received tablet report from " <<
      RequestorString(rpc) << ": " << report.DebugString();
  }
  if (!ts_desc->has_tablet_report() && report.is_incremental()) {
    string msg = "Received an incremental tablet report when a full one was needed";
    LOG(WARNING) << "Invalid tablet report from " << RequestorString(rpc) << ": "
                 << msg;
    return Status::IllegalState(msg);
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

  ts_desc->set_has_tablet_report(true);

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
} // anonymous namespace

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
  TableMetadataLock table_lock(tablet->table().get(), TableMetadataLock::READ);
  TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::WRITE);

  // If the TS is reporting a tablet which has been deleted, or a tablet from
  // a table which has been deleted, send it an RPC to delete it.
  // NOTE: when a table is deleted, we don't currently iterate over all of the
  // tablets and mark them as deleted. Hence, we have to check the table state,
  // not just the tablet state.
  if (tablet_lock.data().is_deleted() ||
      table_lock.data().is_deleted()) {
    report_updates->set_state_msg(tablet_lock.data().pb.state_msg());
    const string msg = tablet_lock.data().pb.state_msg();
    LOG(INFO) << "Got report from deleted tablet " << tablet->ToString()
              << " (" << msg << "): Sending delete request for this tablet";
    // TODO: Cancel tablet creation, instead of deleting, in cases where
    // that might be possible (tablet creation timeout & replacement).
    SendDeleteTabletRequest(tablet->tablet_id(), TABLET_DATA_DELETED, boost::none,
                            tablet->table(), ts_desc,
                            Substitute("Tablet deleted: $0", msg));
    return Status::OK();
  }

  if (!table_lock.data().is_running()) {
    LOG(INFO) << "Got report from tablet " << tablet->tablet_id()
              << " for non-running table " << tablet->table()->ToString() << ": "
              << tablet_lock.data().pb.state_msg();
    report_updates->set_state_msg(tablet_lock.data().pb.state_msg());
    return Status::OK();
  }

  // Check if the tablet requires an "alter table" call
  bool tablet_needs_alter = false;
  if (report.has_schema_version() &&
      table_lock.data().pb.version() != report.schema_version()) {
    if (report.schema_version() > table_lock.data().pb.version()) {
      LOG(ERROR) << "TS " << ts_desc->permanent_uuid()
                 << " has reported a schema version greater than the current one "
                 << " for tablet " << tablet->ToString()
                 << ". Expected version " << table_lock.data().pb.version()
                 << " got " << report.schema_version()
                 << " (corruption)";
    } else {
      LOG(INFO) << "TS " << ts_desc->permanent_uuid()
            << " does not have the latest schema for tablet " << tablet->ToString()
            << ". Expected version " << table_lock.data().pb.version()
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
    const ConsensusStatePB& prev_cstate = tablet_lock.data().pb.committed_consensus_state();
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
    if (!tablet_lock.data().is_running() && ShouldTransitionTabletToRunning(report)) {
      DCHECK_EQ(SysTabletsEntryPB::CREATING, tablet_lock.data().pb.state())
          << "Tablet in unexpected state: " << tablet->ToString()
          << ": " << tablet_lock.data().pb.ShortDebugString();
      // Mark the tablet as running
      // TODO: we could batch the IO onto a background thread, or at least
      // across multiple tablets in the same report.
      VLOG(1) << "Tablet " << tablet->ToString() << " is now online";
      tablet_lock.mutable_data()->set_state(SysTabletsEntryPB::RUNNING,
                                            "Tablet reported with an active leader");
    }

    // The Master only accepts committed consensus configurations since it needs the committed index
    // to only cache the most up-to-date config.
    if (PREDICT_FALSE(!cstate.config().has_opid_index())) {
      LOG(DFATAL) << "Missing opid_index in reported config:\n" << report.DebugString();
      return Status::InvalidArgument("Missing opid_index in reported config");
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
          return Status::InvalidArgument(msg);
        }
      }

      // If a replica is reporting a new consensus configuration, reset the tablet's replicas.
      // Note that we leave out replicas who live in tablet servers who have not heartbeated to
      // master yet.
      LOG(INFO) << "Tablet: " << tablet->tablet_id() << " reported consensus state change."
                << " New consensus state: " << cstate.ShortDebugString();

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
                                                          &tablet_lock, &table_lock));

    } else {
      // Report opid_index is equal to the previous opid_index. If some
      // replica is reporting the same consensus configuration we already know about and hasn't
      // been added as replica, add it.
      DVLOG(2) << "Peer " << ts_desc->permanent_uuid() << " sent full tablet report"
              << " with data we have already received. Ensuring replica is being tracked."
              << " Replica consensus state: " << cstate.ShortDebugString();
      AddReplicaToTabletIfNotFound(ts_desc, report, tablet);
    }
  }

  table_lock.Unlock();
  // We update the tablets each time that someone reports it.
  // This shouldn't be very frequent and should only happen when something in fact changed.
  Status s = sys_catalog_->UpdateTablets({ tablet.get() });
  if (!s.ok()) {
    LOG(WARNING) << "Error updating tablets: " << s.ToString() << ". Tablet report was: "
                 << report.ShortDebugString();
    return s;
  }
  tablet_lock.Commit();

  // Need to defer the AlterTable command to after we've committed the new tablet data,
  // since the tablet report may also be updating the raft config, and the Alter Table
  // request needs to know who the most recent leader is.
  if (tablet_needs_alter) {
    SendAlterTabletRequest(tablet);
  } else if (report.has_schema_version()) {
    HandleTabletSchemaVersionReport(tablet.get(), report.schema_version());
  }

  return Status::OK();
}

Status CatalogManager::ResetTabletReplicasFromReportedConfig(
    const ReportedTabletPB& report,
    const scoped_refptr<TabletInfo>& tablet,
    TabletMetadataLock* tablet_lock,
    TableMetadataLock* table_lock) {

  DCHECK(tablet_lock->is_write_locked());
  ConsensusStatePB prev_cstate = tablet_lock->mutable_data()->pb.committed_consensus_state();
  const ConsensusStatePB& cstate = report.committed_consensus_state();
  *tablet_lock->mutable_data()->pb.mutable_committed_consensus_state() = cstate;

  TabletInfo::ReplicaMap replica_locations;
  for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
    shared_ptr<TSDescriptor> ts_desc;
    if (!peer.has_permanent_uuid()) {
      return Status::InvalidArgument("Missing UUID for peer", peer.ShortDebugString());
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
  tablet->SetReplicaLocations(replica_locations);

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

  // If the config is under-replicated, add a server to the config.
  if (FLAGS_master_add_server_when_underreplicated &&
      CountVoters(cstate.config()) < table_lock->data().pb.num_replicas()) {
    SendAddServerRequest(tablet, cstate);
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
}

Status CatalogManager::GetTabletPeer(const string& tablet_id,
                                     scoped_refptr<TabletPeer>* tablet_peer) const {
  // Note: CatalogManager has only one table, 'sys_catalog', with only
  // one tablet.
  boost::shared_lock<LockType> l(lock_);
  CHECK(sys_catalog_.get() != nullptr) << "sys_catalog_ must be initialized!";
  if (sys_catalog_->tablet_id() == tablet_id) {
    *tablet_peer = sys_catalog_->tablet_peer();
  } else {
    return Status::NotFound(Substitute("no SysTable exists with tablet_id $0 in CatalogManager",
                                       tablet_id));
  }
  return Status::OK();
}

const NodeInstancePB& CatalogManager::NodeInstance() const {
  return master_->instance_pb();
}

Status CatalogManager::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  return Status::NotSupported("Remote bootstrap not yet implemented for the master tablet");
}

// Interface used by RetryingTSRpcTask to pick the tablet server to
// send the next RPC to.
class TSPicker {
 public:
  TSPicker() {}
  virtual ~TSPicker() {}

  // Sets *ts_desc to the tablet server to contact for the next RPC.
  //
  // This assumes that TSDescriptors are never deleted by the master,
  // so the caller does not take ownership of the returned pointer.
  virtual Status PickReplica(TSDescriptor** ts_desc) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TSPicker);
};

// Implementation of TSPicker which sends to a specific tablet server,
// identified by its UUID.
class PickSpecificUUID : public TSPicker {
 public:
  PickSpecificUUID(Master* master, string ts_uuid)
      : master_(master), ts_uuid_(std::move(ts_uuid)) {}

  virtual Status PickReplica(TSDescriptor** ts_desc) OVERRIDE {
    shared_ptr<TSDescriptor> ts;
    if (!master_->ts_manager()->LookupTSByUUID(ts_uuid_, &ts)) {
      return Status::NotFound("unknown tablet server ID", ts_uuid_);
    }
    *ts_desc = ts.get();
    return Status::OK();
  }

 private:
  Master* const master_;
  const string ts_uuid_;

  DISALLOW_COPY_AND_ASSIGN(PickSpecificUUID);
};

// Implementation of TSPicker which locates the current leader replica,
// and sends the RPC to that server.
class PickLeaderReplica : public TSPicker {
 public:
  explicit PickLeaderReplica(const scoped_refptr<TabletInfo>& tablet) :
    tablet_(tablet) {
  }

  virtual Status PickReplica(TSDescriptor** ts_desc) OVERRIDE {
    TabletInfo::ReplicaMap replica_locations;
    tablet_->GetReplicaLocations(&replica_locations);
    for (const TabletInfo::ReplicaMap::value_type& r : replica_locations) {
      if (r.second.role == consensus::RaftPeerPB::LEADER) {
        *ts_desc = r.second.ts_desc;
        return Status::OK();
      }
    }
    return Status::NotFound("no leader");
  }

 private:
  const scoped_refptr<TabletInfo> tablet_;
};

// A background task which continuously retries sending an RPC to a tablet server.
//
// The target tablet server is refreshed before each RPC by consulting the provided
// TSPicker implementation.
class RetryingTSRpcTask : public MonitoredTask {
 public:
  RetryingTSRpcTask(Master *master,
                    ThreadPool* callback_pool,
                    gscoped_ptr<TSPicker> replica_picker,
                    const scoped_refptr<TableInfo>& table)
    : master_(master),
      callback_pool_(callback_pool),
      replica_picker_(replica_picker.Pass()),
      table_(table),
      start_ts_(MonoTime::Now(MonoTime::FINE)),
      attempt_(0),
      state_(kStateRunning) {
    deadline_ = start_ts_;
    deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_unresponsive_ts_rpc_timeout_ms));
  }

  // Send the subclass RPC request.
  Status Run() {
    Status s = ResetTSProxy();
    if (!s.ok()) {
      LOG(WARNING) << "Unable to reset TS proxy: " << s.ToString();
      MarkFailed();
      UnregisterAsyncTask(); // May delete this.
      return s.CloneAndPrepend("Failed to reset TS proxy");
    }

    // Calculate and set the timeout deadline.
    MonoTime timeout = MonoTime::Now(MonoTime::FINE);
    timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
    const MonoTime& deadline = MonoTime::Earliest(timeout, deadline_);
    rpc_.set_deadline(deadline);

    if (!SendRequest(++attempt_)) {
      if (!RescheduleWithBackoffDelay()) {
        UnregisterAsyncTask();  // May call 'delete this'.
      }
    }
    return Status::OK();
  }

  // Abort this task.
  virtual void Abort() OVERRIDE {
    MarkAborted();
  }

  virtual State state() const OVERRIDE {
    return static_cast<State>(NoBarrier_Load(&state_));
  }

  virtual MonoTime start_timestamp() const OVERRIDE { return start_ts_; }
  virtual MonoTime completion_timestamp() const OVERRIDE { return end_ts_; }

 protected:
  // Send an RPC request and register a callback.
  // The implementation must return true if the callback was registered, and
  // false if an error occurred and no callback will occur.
  virtual bool SendRequest(int attempt) = 0;

  // Handle the response from the RPC request. On success, MarkSuccess() must
  // be called to mutate the state_ variable. If retry is desired, then
  // no state change is made. Retries will automatically be attempted as long
  // as the state is kStateRunning and deadline_ has not yet passed.
  virtual void HandleResponse(int attempt) = 0;

  // Return the id of the tablet that is the subject of the async request.
  virtual string tablet_id() const = 0;

  // Overridable log prefix with reasonable default.
  virtual string LogPrefix() const {
    return Substitute("$0: ", description());
  }

  // Transition from running -> complete.
  void MarkComplete() {
    NoBarrier_CompareAndSwap(&state_, kStateRunning, kStateComplete);
  }

  // Transition from running -> aborted.
  void MarkAborted() {
    NoBarrier_CompareAndSwap(&state_, kStateRunning, kStateAborted);
  }

  // Transition from running -> failed.
  void MarkFailed() {
    NoBarrier_CompareAndSwap(&state_, kStateRunning, kStateFailed);
  }

  // Callback meant to be invoked from asynchronous RPC service proxy calls.
  void RpcCallback() {
    // Defer the actual work of the callback off of the reactor thread.
    // This is necessary because our callbacks often do synchronous writes to
    // the catalog table, and we can't do synchronous IO on the reactor.
    CHECK_OK(callback_pool_->SubmitClosure(
                 Bind(&RetryingTSRpcTask::DoRpcCallback,
                      Unretained(this))));
  }

  // Handle the actual work of the RPC callback. This is run on the master's worker
  // pool, rather than a reactor thread, so it may do blocking IO operations.
  void DoRpcCallback() {
    if (!rpc_.status().ok()) {
      LOG(WARNING) << "TS " << target_ts_desc_->permanent_uuid() << ": "
                   << type_name() << " RPC failed for tablet "
                   << tablet_id() << ": " << rpc_.status().ToString();
    } else if (state() != kStateAborted) {
      HandleResponse(attempt_); // Modifies state_.
    }

    // Schedule a retry if the RPC call was not successful.
    if (RescheduleWithBackoffDelay()) {
      return;
    }

    UnregisterAsyncTask();  // May call 'delete this'.
  }

  Master * const master_;
  ThreadPool* const callback_pool_;
  const gscoped_ptr<TSPicker> replica_picker_;
  const scoped_refptr<TableInfo> table_;

  MonoTime start_ts_;
  MonoTime end_ts_;
  MonoTime deadline_;

  int attempt_;
  rpc::RpcController rpc_;
  TSDescriptor* target_ts_desc_;
  shared_ptr<tserver::TabletServerAdminServiceProxy> ts_proxy_;
  shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy_;

 private:
  // Reschedules the current task after a backoff delay.
  // Returns false if the task was not rescheduled due to reaching the maximum
  // timeout or because the task is no longer in a running state.
  // Returns true if rescheduling the task was successful.
  bool RescheduleWithBackoffDelay() {
    if (state() != kStateRunning) return false;
    MonoTime now = MonoTime::Now(MonoTime::FINE);
    // We assume it might take 10ms to process the request in the best case,
    // fail if we have less than that amount of time remaining.
    int64_t millis_remaining = deadline_.GetDeltaSince(now).ToMilliseconds() - 10;
    // Exponential backoff with jitter.
    int64_t base_delay_ms;
    if (attempt_ <= 12) {
      base_delay_ms = 1 << (attempt_ + 3);  // 1st retry delayed 2^4 ms, 2nd 2^5, etc.
    } else {
      base_delay_ms = 60 * 1000; // cap at 1 minute
    }
    int64_t jitter_ms = rand() % 50;              // Add up to 50ms of additional random delay.
    int64_t delay_millis = std::min<int64_t>(base_delay_ms + jitter_ms, millis_remaining);

    if (delay_millis <= 0) {
      LOG(WARNING) << "Request timed out: " << description();
      MarkFailed();
    } else {
      MonoTime new_start_time = now;
      new_start_time.AddDelta(MonoDelta::FromMilliseconds(delay_millis));
      LOG(INFO) << "Scheduling retry of " << description() << " with a delay"
                << " of " << delay_millis << "ms (attempt = " << attempt_ << ")...";
      master_->messenger()->ScheduleOnReactor(
          boost::bind(&RetryingTSRpcTask::RunDelayedTask, this, _1),
          MonoDelta::FromMilliseconds(delay_millis));
      return true;
    }
    return false;
  }

  // Callback for Reactor delayed task mechanism. Called either when it is time
  // to execute the delayed task (with status == OK) or when the task
  // is cancelled, i.e. when the scheduling timer is shut down (status != OK).
  void RunDelayedTask(const Status& status) {
    if (!status.ok()) {
      LOG(WARNING) << "Async tablet task " << description() << " failed or was cancelled: "
                   << status.ToString();
      UnregisterAsyncTask();   // May delete this.
      return;
    }

    string desc = description();  // Save in case we need to log after deletion.
    Status s = Run();             // May delete this.
    if (!s.ok()) {
      LOG(WARNING) << "Async tablet task " << desc << " failed: " << s.ToString();
    }
  }

  // Clean up request and release resources. May call 'delete this'.
  void UnregisterAsyncTask() {
    end_ts_ = MonoTime::Now(MonoTime::FINE);
    if (table_ != nullptr) {
      table_->RemoveTask(this);
    } else {
      // This is a floating task (since the table does not exist)
      // created as response to a tablet report.
      Release();  // May call "delete this";
    }
  }

  Status ResetTSProxy() {
    // TODO: if there is no replica available, should we still keep the task running?
    RETURN_NOT_OK(replica_picker_->PickReplica(&target_ts_desc_));

    shared_ptr<tserver::TabletServerAdminServiceProxy> ts_proxy;
    RETURN_NOT_OK(target_ts_desc_->GetTSAdminProxy(master_->messenger(), &ts_proxy));
    ts_proxy_.swap(ts_proxy);

    shared_ptr<consensus::ConsensusServiceProxy> consensus_proxy;
    RETURN_NOT_OK(target_ts_desc_->GetConsensusProxy(master_->messenger(), &consensus_proxy));
    consensus_proxy_.swap(consensus_proxy);

    rpc_.Reset();
    return Status::OK();
  }

  // Use state() and MarkX() accessors.
  AtomicWord state_;
};

// RetryingTSRpcTask subclass which always retries the same tablet server,
// identified by its UUID.
class RetrySpecificTSRpcTask : public RetryingTSRpcTask {
 public:
  RetrySpecificTSRpcTask(Master* master,
                         ThreadPool* callback_pool,
                         const string& permanent_uuid,
                         const scoped_refptr<TableInfo>& table)
    : RetryingTSRpcTask(master,
                        callback_pool,
                        gscoped_ptr<TSPicker>(new PickSpecificUUID(master, permanent_uuid)),
                        table),
      permanent_uuid_(permanent_uuid) {
  }

 protected:
  const string permanent_uuid_;
};

// Fire off the async create tablet.
// This requires that the new tablet info is locked for write, and the
// consensus configuration information has been filled into the 'dirty' data.
class AsyncCreateReplica : public RetrySpecificTSRpcTask {
 public:
  AsyncCreateReplica(Master *master,
                     ThreadPool *callback_pool,
                     const string& permanent_uuid,
                     const scoped_refptr<TabletInfo>& tablet)
    : RetrySpecificTSRpcTask(master, callback_pool, permanent_uuid, tablet->table().get()),
      tablet_id_(tablet->tablet_id()) {
    deadline_ = start_ts_;
    deadline_.AddDelta(MonoDelta::FromMilliseconds(FLAGS_tablet_creation_timeout_ms));

    TableMetadataLock table_lock(tablet->table().get(), TableMetadataLock::READ);
    const SysTabletsEntryPB& tablet_pb = tablet->metadata().dirty().pb;

    req_.set_dest_uuid(permanent_uuid);
    req_.set_table_id(tablet->table()->id());
    req_.set_tablet_id(tablet->tablet_id());
    req_.mutable_partition()->CopyFrom(tablet_pb.partition());
    req_.set_table_name(table_lock.data().pb.name());
    req_.mutable_schema()->CopyFrom(table_lock.data().pb.schema());
    req_.mutable_partition_schema()->CopyFrom(table_lock.data().pb.partition_schema());
    req_.mutable_config()->CopyFrom(tablet_pb.committed_consensus_state().config());
  }

  virtual string type_name() const OVERRIDE { return "Create Tablet"; }

  virtual string description() const OVERRIDE {
    return "CreateTablet RPC for tablet " + tablet_id_ + " on TS " + permanent_uuid_;
  }

 protected:
  virtual string tablet_id() const OVERRIDE { return tablet_id_; }

  virtual void HandleResponse(int attempt) OVERRIDE {
    if (!resp_.has_error()) {
      MarkComplete();
    } else {
      Status s = StatusFromPB(resp_.error().status());
      if (s.IsAlreadyPresent()) {
        LOG(INFO) << "CreateTablet RPC for tablet " << tablet_id_
                  << " on TS " << permanent_uuid_ << " returned already present: "
                  << s.ToString();
        MarkComplete();
      } else {
        LOG(WARNING) << "CreateTablet RPC for tablet " << tablet_id_
                     << " on TS " << permanent_uuid_ << " failed: " << s.ToString();
      }
    }
  }

  virtual bool SendRequest(int attempt) OVERRIDE {
    ts_proxy_->CreateTabletAsync(req_, &resp_, &rpc_,
                                 boost::bind(&AsyncCreateReplica::RpcCallback, this));
    VLOG(1) << "Send create tablet request to " << permanent_uuid_ << ":\n"
            << " (attempt " << attempt << "):\n"
            << req_.DebugString();
    return true;
  }

 private:
  const string tablet_id_;
  tserver::CreateTabletRequestPB req_;
  tserver::CreateTabletResponsePB resp_;
};

// Send a DeleteTablet() RPC request.
class AsyncDeleteReplica : public RetrySpecificTSRpcTask {
 public:
  AsyncDeleteReplica(
      Master* master, ThreadPool* callback_pool, const string& permanent_uuid,
      const scoped_refptr<TableInfo>& table, std::string tablet_id,
      TabletDataState delete_type,
      boost::optional<int64_t> cas_config_opid_index_less_or_equal,
      string reason)
      : RetrySpecificTSRpcTask(master, callback_pool, permanent_uuid, table),
        tablet_id_(std::move(tablet_id)),
        delete_type_(delete_type),
        cas_config_opid_index_less_or_equal_(
            std::move(cas_config_opid_index_less_or_equal)),
        reason_(std::move(reason)) {}

  virtual string type_name() const OVERRIDE { return "Delete Tablet"; }

  virtual string description() const OVERRIDE {
    return tablet_id_ + " Delete Tablet RPC for TS=" + permanent_uuid_;
  }

 protected:
  virtual string tablet_id() const OVERRIDE { return tablet_id_; }

  virtual void HandleResponse(int attempt) OVERRIDE {
    if (resp_.has_error()) {
      Status status = StatusFromPB(resp_.error().status());

      // Do not retry on a fatal error
      TabletServerErrorPB::Code code = resp_.error().code();
      switch (code) {
        case TabletServerErrorPB::TABLET_NOT_FOUND:
          LOG(WARNING) << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
                       << " because the tablet was not found. No further retry: "
                       << status.ToString();
          MarkComplete();
          break;
        case TabletServerErrorPB::CAS_FAILED:
          LOG(WARNING) << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
                       << " due to a CAS failure. No further retry: " << status.ToString();
          MarkComplete();
          break;
        default:
          LOG(WARNING) << "TS " << permanent_uuid_ << ": delete failed for tablet " << tablet_id_
                       << " with error code " << TabletServerErrorPB::Code_Name(code)
                       << ": " << status.ToString();
          break;
      }
    } else {
      master_->catalog_manager()->NotifyTabletDeleteSuccess(permanent_uuid_, tablet_id_);
      if (table_) {
        LOG(INFO) << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
                  << " (table " << table_->ToString() << ") successfully deleted";
      } else {
        LOG(WARNING) << "TS " << permanent_uuid_ << ": tablet " << tablet_id_
                     << " did not belong to a known table, but was successfully deleted";
      }
      MarkComplete();
      VLOG(1) << "TS " << permanent_uuid_ << ": delete complete on tablet " << tablet_id_;
    }
  }

  virtual bool SendRequest(int attempt) OVERRIDE {
    tserver::DeleteTabletRequestPB req;
    req.set_dest_uuid(permanent_uuid_);
    req.set_tablet_id(tablet_id_);
    req.set_reason(reason_);
    req.set_delete_type(delete_type_);
    if (cas_config_opid_index_less_or_equal_) {
      req.set_cas_config_opid_index_less_or_equal(*cas_config_opid_index_less_or_equal_);
    }

    ts_proxy_->DeleteTabletAsync(req, &resp_, &rpc_,
                                 boost::bind(&AsyncDeleteReplica::RpcCallback, this));
    VLOG(1) << "Send delete tablet request to " << permanent_uuid_
            << " (attempt " << attempt << "):\n"
            << req.DebugString();
    return true;
  }

  const std::string tablet_id_;
  const TabletDataState delete_type_;
  const boost::optional<int64_t> cas_config_opid_index_less_or_equal_;
  const std::string reason_;
  tserver::DeleteTabletResponsePB resp_;
};

// Send the "Alter Table" with the latest table schema to the leader replica
// for the tablet.
// Keeps retrying until we get an "ok" response.
//  - Alter completed
//  - Tablet has already a newer version
//    (which may happen in case of concurrent alters, or in case a previous attempt timed
//     out but was actually applied).
class AsyncAlterTable : public RetryingTSRpcTask {
 public:
  AsyncAlterTable(Master *master,
                  ThreadPool* callback_pool,
                  const scoped_refptr<TabletInfo>& tablet)
    : RetryingTSRpcTask(master,
                        callback_pool,
                        gscoped_ptr<TSPicker>(new PickLeaderReplica(tablet)),
                        tablet->table().get()),
      tablet_(tablet) {
  }

  virtual string type_name() const OVERRIDE { return "Alter Table"; }

  virtual string description() const OVERRIDE {
    return tablet_->ToString() + " Alter Table RPC";
  }

 private:
  virtual string tablet_id() const OVERRIDE { return tablet_->tablet_id(); }
  string permanent_uuid() const {
    return target_ts_desc_->permanent_uuid();
  }

  virtual void HandleResponse(int attempt) OVERRIDE {
    if (resp_.has_error()) {
      Status status = StatusFromPB(resp_.error().status());

      // Do not retry on a fatal error
      switch (resp_.error().code()) {
        case TabletServerErrorPB::TABLET_NOT_FOUND:
        case TabletServerErrorPB::MISMATCHED_SCHEMA:
        case TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA:
          LOG(WARNING) << "TS " << permanent_uuid() << ": alter failed for tablet "
                       << tablet_->ToString() << " no further retry: " << status.ToString();
          MarkComplete();
          break;
        default:
          LOG(WARNING) << "TS " << permanent_uuid() << ": alter failed for tablet "
                       << tablet_->ToString() << ": " << status.ToString();
          break;
      }
    } else {
      MarkComplete();
      VLOG(1) << "TS " << permanent_uuid() << ": alter complete on tablet " << tablet_->ToString();
    }

    if (state() == kStateComplete) {
      master_->catalog_manager()->HandleTabletSchemaVersionReport(tablet_.get(), schema_version_);
    } else {
      VLOG(1) << "Still waiting for other tablets to finish ALTER";
    }
  }

  virtual bool SendRequest(int attempt) OVERRIDE {
    TableMetadataLock l(tablet_->table().get(), TableMetadataLock::READ);

    tserver::AlterSchemaRequestPB req;
    req.set_dest_uuid(permanent_uuid());
    req.set_tablet_id(tablet_->tablet_id());
    req.set_new_table_name(l.data().pb.name());
    req.set_schema_version(l.data().pb.version());
    req.mutable_schema()->CopyFrom(l.data().pb.schema());
    schema_version_ = l.data().pb.version();

    l.Unlock();

    ts_proxy_->AlterSchemaAsync(req, &resp_, &rpc_,
                                boost::bind(&AsyncAlterTable::RpcCallback, this));
    VLOG(1) << "Send alter table request to " << permanent_uuid()
            << " (attempt " << attempt << "):\n"
            << req.DebugString();
    return true;
  }

  uint32_t schema_version_;
  scoped_refptr<TabletInfo> tablet_;
  tserver::AlterSchemaResponsePB resp_;
};

namespace {

// Select a random TS not in the 'exclude_uuids' list.
// Will not select tablet servers that have not heartbeated recently.
// Returns true iff it was possible to select a replica.
bool SelectRandomTSForReplica(const TSDescriptorVector& ts_descs,
                              const unordered_set<string>& exclude_uuids,
                              shared_ptr<TSDescriptor>* selection) {
  TSDescriptorVector tablet_servers;
  for (const shared_ptr<TSDescriptor>& ts : ts_descs) {
    if (!ContainsKey(exclude_uuids, ts->permanent_uuid())) {
      tablet_servers.push_back(ts);
    }
  }
  if (tablet_servers.empty()) {
    return false;
  }
  *selection = tablet_servers[rand() % tablet_servers.size()];
  return true;
}

} // anonymous namespace

class AsyncAddServerTask : public RetryingTSRpcTask {
 public:
  AsyncAddServerTask(Master *master,
                     ThreadPool* callback_pool,
                     const scoped_refptr<TabletInfo>& tablet,
                     const ConsensusStatePB& cstate)
    : RetryingTSRpcTask(master,
                        callback_pool,
                        gscoped_ptr<TSPicker>(new PickLeaderReplica(tablet)),
                        tablet->table()),
      tablet_(tablet),
      cstate_(cstate) {
    deadline_ = MonoTime::Max(); // Never time out.
  }

  virtual string type_name() const OVERRIDE { return "AddServer ChangeConfig"; }

  virtual string description() const OVERRIDE {
    return Substitute("AddServer ChangeConfig RPC for tablet $0 on peer $1 "
                      "with cas_config_opid_index $2",
                      tablet_->tablet_id(), permanent_uuid(), cstate_.config().opid_index());
  }

 protected:
  virtual bool SendRequest(int attempt) OVERRIDE;
  virtual void HandleResponse(int attempt) OVERRIDE;

 private:
  virtual string tablet_id() const OVERRIDE { return tablet_->tablet_id(); }
  string permanent_uuid() const {
    return target_ts_desc_->permanent_uuid();
  }

  const scoped_refptr<TabletInfo> tablet_;
  const ConsensusStatePB cstate_;

  consensus::ChangeConfigRequestPB req_;
  consensus::ChangeConfigResponsePB resp_;
};

bool AsyncAddServerTask::SendRequest(int attempt) {
  // Bail if we're retrying in vain.
  int64_t latest_index;
  {
    TabletMetadataLock tablet_lock(tablet_.get(), TabletMetadataLock::READ);
    latest_index = tablet_lock.data().pb.committed_consensus_state().config().opid_index();
  }
  if (latest_index > cstate_.config().opid_index()) {
    LOG_WITH_PREFIX(INFO) << "Latest config for has opid_index of " << latest_index
                          << " while this task has opid_index of "
                          << cstate_.config().opid_index() << ". Aborting task.";
    MarkAborted();
    return false;
  }

  // Select the replica we wish to add to the config.
  // Do not include current members of the config.
  unordered_set<string> replica_uuids;
  for (const RaftPeerPB& peer : cstate_.config().peers()) {
    InsertOrDie(&replica_uuids, peer.permanent_uuid());
  }
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  shared_ptr<TSDescriptor> replacement_replica;
  if (PREDICT_FALSE(!SelectRandomTSForReplica(ts_descs, replica_uuids, &replacement_replica))) {
    KLOG_EVERY_N(WARNING, 100) << LogPrefix() << "No candidate replacement replica found "
                               << "for tablet " << tablet_->ToString();
    return false;
  }

  req_.set_dest_uuid(permanent_uuid());
  req_.set_tablet_id(tablet_->tablet_id());
  req_.set_type(consensus::ADD_SERVER);
  req_.set_cas_config_opid_index(cstate_.config().opid_index());
  RaftPeerPB* peer = req_.mutable_server();
  peer->set_permanent_uuid(replacement_replica->permanent_uuid());
  TSRegistrationPB peer_reg;
  replacement_replica->GetRegistration(&peer_reg);
  if (peer_reg.rpc_addresses_size() == 0) {
    KLOG_EVERY_N(WARNING, 100) << LogPrefix() << "Candidate replacement "
                               << replacement_replica->permanent_uuid()
                               << " has no registered rpc address: "
                               << peer_reg.ShortDebugString();
    return false;
  }
  *peer->mutable_last_known_addr() = peer_reg.rpc_addresses(0);
  peer->set_member_type(RaftPeerPB::VOTER);
  consensus_proxy_->ChangeConfigAsync(req_, &resp_, &rpc_,
                                      boost::bind(&AsyncAddServerTask::RpcCallback, this));
  VLOG(1) << "Sent AddServer ChangeConfig request to " << permanent_uuid() << ":\n"
          << req_.DebugString();
  return true;
}

void AsyncAddServerTask::HandleResponse(int attempt) {
  if (!resp_.has_error()) {
    MarkComplete();
    LOG_WITH_PREFIX(INFO) << "Change config succeeded";
    return;
  }

  Status status = StatusFromPB(resp_.error().status());

  // Do not retry on a CAS error, otherwise retry forever or until cancelled.
  switch (resp_.error().code()) {
    case TabletServerErrorPB::CAS_FAILED:
      LOG_WITH_PREFIX(WARNING) << "ChangeConfig() failed with leader " << permanent_uuid()
                               << " due to CAS failure. No further retry: "
                               << status.ToString();
      MarkFailed();
      break;
    default:
      LOG_WITH_PREFIX(INFO) << "ChangeConfig() failed with leader " << permanent_uuid()
                            << " due to error "
                            << TabletServerErrorPB::Code_Name(resp_.error().code())
                            << ". This operation will be retried. Error detail: "
                            << status.ToString();
      break;
  }
}

void CatalogManager::SendAlterTableRequest(const scoped_refptr<TableInfo>& table) {
  vector<scoped_refptr<TabletInfo> > tablets;
  table->GetAllTablets(&tablets);

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    SendAlterTabletRequest(tablet);
  }
}

void CatalogManager::SendAlterTabletRequest(const scoped_refptr<TabletInfo>& tablet) {
  auto call = new AsyncAlterTable(master_, worker_pool_.get(), tablet);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(call->Run(), "Failed to send alter table request");
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
  vector<scoped_refptr<TabletInfo> > tablets;
  table->GetAllTablets(&tablets);

  string deletion_msg = "Table deleted at " + LocalTimeAsString();

  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    DeleteTabletReplicas(tablet.get(), deletion_msg);

    TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::WRITE);
    tablet_lock.mutable_data()->set_state(SysTabletsEntryPB::DELETED, deletion_msg);
    CHECK_OK(sys_catalog_->UpdateTablets({ tablet.get() }));
    tablet_lock.Commit();
  }
}

void CatalogManager::SendDeleteTabletRequest(
    const std::string& tablet_id,
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
  AsyncDeleteReplica* call =
      new AsyncDeleteReplica(master_, worker_pool_.get(), ts_desc->permanent_uuid(), table,
                             tablet_id, delete_type, cas_config_opid_index_less_or_equal,
                             reason);
  if (table != nullptr) {
    table->AddTask(call);
  } else {
    // This is a floating task (since the table does not exist)
    // created as response to a tablet report.
    call->AddRef();
  }
  WARN_NOT_OK(call->Run(), "Failed to send delete tablet request");
}

void CatalogManager::SendAddServerRequest(const scoped_refptr<TabletInfo>& tablet,
                                          const ConsensusStatePB& cstate) {
  auto task = new AsyncAddServerTask(master_, worker_pool_.get(), tablet, cstate);
  tablet->table()->AddTask(task);
  WARN_NOT_OK(task->Run(), "Failed to send new AddServer request");

  // Need to print this after Run() because that's where it picks the TS which description()
  // needs.
  LOG(INFO) << "Started AddServer task: " << task->description();
}

void CatalogManager::ExtractTabletsToProcess(
    std::vector<scoped_refptr<TabletInfo> > *tablets_to_delete,
    std::vector<scoped_refptr<TabletInfo> > *tablets_to_process) {

  boost::shared_lock<LockType> l(lock_);

  // TODO: At the moment we loop through all the tablets
  //       we can keep a set of tablets waiting for "assignment"
  //       or just a counter to avoid to take the lock and loop through the tablets
  //       if everything is "stable".

  for (const TabletInfoMap::value_type& entry : tablet_map_) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::READ);

    if (!tablet->table()) {
      // Tablet is orphaned or in preparing state, continue.
      continue;
    }

    TableMetadataLock table_lock(tablet->table().get(), TableMetadataLock::READ);

    // If the table is deleted or the tablet was replaced at table creation time.
    if (tablet_lock.data().is_deleted() || table_lock.data().is_deleted()) {
      tablets_to_delete->push_back(tablet);
      continue;
    }

    // Running tablets.
    if (tablet_lock.data().is_running()) {
      // TODO: handle last update > not responding timeout?
      continue;
    }

    // Tablets not yet assigned or with a report just received
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
                                                vector<scoped_refptr<TabletInfo> >* new_tablets) {
  MonoDelta time_since_updated =
      MonoTime::Now(MonoTime::FINE).GetDeltaSince(tablet->last_update_time());
  int64_t remaining_timeout_ms =
      FLAGS_tablet_creation_timeout_ms - time_since_updated.ToMilliseconds();

  // Skip the tablet if the assignment timeout is not yet expired
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
    boost::lock_guard<LockType> l_maps(lock_);
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
          << " (Table " << tablet->table()->ToString() << ")";

  new_tablets->push_back(replacement);
}

// TODO: we could batch the IO onto a background thread.
//       but this is following the current HandleReportedTablet()
Status CatalogManager::HandleTabletSchemaVersionReport(TabletInfo *tablet, uint32_t version) {
  // Update the schema version if it's the latest
  tablet->set_reported_schema_version(version);

  // Verify if it's the last tablet report, and the alter completed.
  TableInfo *table = tablet->table().get();
  TableMetadataLock l(table, TableMetadataLock::WRITE);
  if (l.data().is_deleted() || l.data().pb.state() != SysTablesEntryPB::ALTERING) {
    return Status::OK();
  }

  uint32_t current_version = l.data().pb.version();
  if (table->IsAlterInProgress(current_version)) {
    return Status::OK();
  }

  // Update the state from altering to running and remove the last fully
  // applied schema (if it exists).
  l.mutable_data()->pb.clear_fully_applied_schema();
  l.mutable_data()->set_state(SysTablesEntryPB::RUNNING,
                              Substitute("Current schema version=$0", current_version));

  Status s = sys_catalog_->UpdateTable(table);
  if (!s.ok()) {
    LOG(WARNING) << "An error occurred while updating sys-tables: " << s.ToString();
    return s;
  }

  l.Commit();
  LOG(INFO) << table->ToString() << " - Alter table completed version=" << current_version;
  return Status::OK();
}

// Helper class to commit TabletInfo mutations at the end of a scope.
namespace {

class ScopedTabletInfoCommitter {
 public:
  explicit ScopedTabletInfoCommitter(const std::vector<scoped_refptr<TabletInfo> >* tablets)
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
  const std::vector<scoped_refptr<TabletInfo> >* tablets_;
  bool aborted_;
};
} // anonymous namespace

Status CatalogManager::ProcessPendingAssignments(
    const std::vector<scoped_refptr<TabletInfo> >& tablets) {
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
  std::vector<scoped_refptr<TabletInfo> > new_tablets;
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

  // Nothing to do
  if (deferred.tablets_to_add.empty() &&
      deferred.tablets_to_update.empty() &&
      deferred.needs_create_rpc.empty()) {
    return Status::OK();
  }

  // For those tablets which need to be created in this round, assign replicas.
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);

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
      break;
    }
  }

  // Update the sys catalog with the new set of tablets/metadata.
  if (s.ok()) {
    s = sys_catalog_->AddAndUpdateTablets(deferred.tablets_to_add,
                                          deferred.tablets_to_update);
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
      TableMetadataLock l_table(table, TableMetadataLock::READ);
      if (table->RemoveTablet(
          new_tablet->metadata().dirty().pb.partition().partition_key_start())) {
        VLOG(1) << "Removed tablet " << new_tablet->tablet_id() << " from "
            "table " << l_table.data().name();
      }
      tablet_ids_to_remove.push_back(new_tablet->tablet_id());
    }
    boost::lock_guard<LockType> l(lock_);
    unlocker_out.Abort();
    unlocker_in.Abort();
    for (const string& tablet_id_to_remove : tablet_ids_to_remove) {
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
  TableMetadataLock table_guard(tablet->table().get(), TableMetadataLock::READ);

  if (!table_guard.data().pb.IsInitialized()) {
    return Status::InvalidArgument(
        Substitute("TableInfo for tablet $0 is not initialized (aborted CreateTable attempt?)",
                   tablet->tablet_id()));
  }

  int nreplicas = table_guard.data().pb.num_replicas();

  if (ts_descs.size() < nreplicas) {
    return Status::InvalidArgument(
        Substitute("Not enough tablet servers are online for table '$0'. Need at least $1 "
                   "replicas, but only $2 tablet servers are available",
                   table_guard.data().name(), nreplicas, ts_descs.size()));
  }

  // Select the set of replicas for the tablet.
  ConsensusStatePB* cstate = tablet->mutable_metadata()->mutable_dirty()
          ->pb.mutable_committed_consensus_state();
  cstate->set_current_term(kMinimumTerm);
  consensus::RaftConfigPB *config = cstate->mutable_config();

  if (nreplicas == 1 && FLAGS_catalog_manager_allow_local_consensus) {
    config->set_local(true);
  } else {
    config->set_local(false);
  }
  config->set_opid_index(consensus::kInvalidOpIdIndex);
  SelectReplicas(ts_descs, nreplicas, config);
  return Status::OK();
}

void CatalogManager::SendCreateTabletRequests(const vector<TabletInfo*>& tablets) {
  for (TabletInfo *tablet : tablets) {
    const consensus::RaftConfigPB& config =
        tablet->metadata().dirty().pb.committed_consensus_state().config();
    tablet->set_last_update_time(MonoTime::Now(MonoTime::FINE));
    for (const RaftPeerPB& peer : config.peers()) {
      AsyncCreateReplica* task = new AsyncCreateReplica(master_, worker_pool_.get(),
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
  vector<shared_ptr<TSDescriptor> > two_choices;
  rng_.ReservoirSample(ts_descs, 2, excluded, &two_choices);

  if (two_choices.size() == 2) {
    // Pick the better of the two.
    return PickBetterReplicaLocation(two_choices);
  }

  // If we couldn't randomly sample two servers, it's because we only had one
  // more non-excluded choice left.
  CHECK_EQ(1, ts_descs.size() - excluded.size())
      << "ts_descs: " << ts_descs.size() << " already_sel: " << excluded.size();
  return two_choices[0];
}

void CatalogManager::SelectReplicas(const TSDescriptorVector& ts_descs,
                                    int nreplicas,
                                    consensus::RaftConfigPB *config) {
  DCHECK_EQ(0, config->peers_size()) << "RaftConfig not empty: " << config->ShortDebugString();
  DCHECK_LE(nreplicas, ts_descs.size());

  // Keep track of servers we've already selected, so that we don't attempt to
  // put two replicas on the same host.
  set<shared_ptr<TSDescriptor> > already_selected;
  for (int i = 0; i < nreplicas; ++i) {
    shared_ptr<TSDescriptor> ts = SelectReplica(ts_descs, already_selected);
    InsertOrDie(&already_selected, ts);

    // Increment the number of pending replicas so that we take this selection into
    // account when assigning replicas for other tablets of the same table. This
    // value decays back to 0 over time.
    ts->IncrementRecentReplicaCreations();

    TSRegistrationPB reg;
    ts->GetRegistration(&reg);

    RaftPeerPB *peer = config->add_peers();
    peer->set_member_type(RaftPeerPB::VOTER);
    peer->set_permanent_uuid(ts->permanent_uuid());

    // TODO: This is temporary, we will use only UUIDs
    for (const HostPortPB& addr : reg.rpc_addresses()) {
      peer->mutable_last_known_addr()->CopyFrom(addr);
    }
  }
}

Status CatalogManager::BuildLocationsForTablet(const scoped_refptr<TabletInfo>& tablet,
                                               TabletLocationsPB* locs_pb) {
  TSRegistrationPB reg;

  TabletInfo::ReplicaMap locs;
  consensus::ConsensusStatePB cstate;
  {
    TabletMetadataLock l_tablet(tablet.get(), TabletMetadataLock::READ);
    if (PREDICT_FALSE(l_tablet.data().is_deleted())) {
      return Status::NotFound("Tablet deleted", l_tablet.data().pb.state_msg());
    }

    if (PREDICT_FALSE(!l_tablet.data().is_running())) {
      return Status::ServiceUnavailable("Tablet not running");
    }

    tablet->GetReplicaLocations(&locs);
    if (locs.empty() && l_tablet.data().pb.has_committed_consensus_state()) {
      cstate = l_tablet.data().pb.committed_consensus_state();
    }

    locs_pb->mutable_partition()->CopyFrom(tablet->metadata().state().pb.partition());
  }

  locs_pb->set_tablet_id(tablet->tablet_id());
  locs_pb->set_stale(locs.empty());

  // If the locations are cached.
  if (!locs.empty()) {
    for (const TabletInfo::ReplicaMap::value_type& replica : locs) {
      TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
      replica_pb->set_role(replica.second.role);

      TSInfoPB* tsinfo_pb = replica_pb->mutable_ts_info();
      tsinfo_pb->set_permanent_uuid(replica.second.ts_desc->permanent_uuid());

      replica.second.ts_desc->GetRegistration(&reg);
      tsinfo_pb->mutable_rpc_addresses()->Swap(reg.mutable_rpc_addresses());
    }
    return Status::OK();
  }

  // If the locations were not cached.
  // TODO: Why would this ever happen? See KUDU-759.
  if (cstate.IsInitialized()) {
    for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
      TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
      CHECK(peer.has_permanent_uuid()) << "Missing UUID: " << peer.ShortDebugString();
      replica_pb->set_role(GetConsensusRole(peer.permanent_uuid(), cstate));

      TSInfoPB* tsinfo_pb = replica_pb->mutable_ts_info();
      tsinfo_pb->set_permanent_uuid(peer.permanent_uuid());
      tsinfo_pb->add_rpc_addresses()->CopyFrom(peer.last_known_addr());
    }
  }

  return Status::OK();
}

Status CatalogManager::GetTabletLocations(const std::string& tablet_id,
                                          TabletLocationsPB* locs_pb) {
  RETURN_NOT_OK(CheckOnline());

  locs_pb->mutable_replicas()->Clear();
  scoped_refptr<TabletInfo> tablet_info;
  {
    boost::shared_lock<LockType> l(lock_);
    if (!FindCopy(tablet_map_, tablet_id, &tablet_info)) {
      return Status::NotFound(Substitute("Unknown tablet $0", tablet_id));
    }
  }

  return BuildLocationsForTablet(tablet_info, locs_pb);
}

Status CatalogManager::GetTableLocations(const GetTableLocationsRequestPB* req,
                                         GetTableLocationsResponsePB* resp) {
  RETURN_NOT_OK(CheckOnline());

  // If start-key is > end-key report an error instead of swap the two
  // since probably there is something wrong app-side.
  if (req->has_partition_key_start() && req->has_partition_key_end()
      && req->partition_key_start() > req->partition_key_end()) {
    return Status::InvalidArgument("start partition key is greater than the end partition key");
  }

  if (req->max_returned_locations() <= 0) {
    return Status::InvalidArgument("max_returned_locations must be greater than 0");
  }

  scoped_refptr<TableInfo> table;
  RETURN_NOT_OK(FindTable(req->table(), &table));

  if (table == nullptr) {
    Status s = Status::NotFound("The table does not exist");
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  TableMetadataLock l(table.get(), TableMetadataLock::READ);
  if (l.data().is_deleted()) {
    Status s = Status::NotFound("The table was deleted",
                                l.data().pb.state_msg());
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  if (!l.data().is_running()) {
    Status s = Status::ServiceUnavailable("The table is not running");
    SetupError(resp->mutable_error(), MasterErrorPB::TABLE_NOT_FOUND, s);
    return s;
  }

  vector<scoped_refptr<TabletInfo> > tablets_in_range;
  table->GetTabletsInRange(req, &tablets_in_range);

  TSRegistrationPB reg;
  vector<TabletReplica> locs;
  for (const scoped_refptr<TabletInfo>& tablet : tablets_in_range) {
    if (!BuildLocationsForTablet(tablet, resp->add_tablet_locations()).ok()) {
      // Not running.
      resp->mutable_tablet_locations()->RemoveLast();
    }
  }
  return Status::OK();
}

void CatalogManager::DumpState(std::ostream* out) const {
  TableInfoMap ids_copy, names_copy;
  TabletInfoMap tablets_copy;

  // Copy the internal state so that, if the output stream blocks,
  // we don't end up holding the lock for a long time.
  {
    boost::shared_lock<LockType> l(lock_);
    ids_copy = table_ids_map_;
    names_copy = table_names_map_;
    tablets_copy = tablet_map_;
  }

  *out << "Tables:\n";
  for (const TableInfoMap::value_type& e : ids_copy) {
    TableInfo* t = e.second.get();
    TableMetadataLock l(t, TableMetadataLock::READ);
    const string& name = l.data().name();

    *out << t->id() << ":\n";
    *out << "  name: \"" << strings::CHexEscape(name) << "\"\n";
    // Erase from the map, so later we can check that we don't have
    // any orphaned tables in the by-name map that aren't in the
    // by-id map.
    if (names_copy.erase(name) != 1) {
      *out << "  [not present in by-name map]\n";
    }
    *out << "  metadata: " << l.data().pb.ShortDebugString() << "\n";

    *out << "  tablets:\n";

    vector<scoped_refptr<TabletInfo> > table_tablets;
    t->GetAllTablets(&table_tablets);
    for (const scoped_refptr<TabletInfo>& tablet : table_tablets) {
      TabletMetadataLock l_tablet(tablet.get(), TabletMetadataLock::READ);
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet.data().pb.ShortDebugString() << "\n";

      if (tablets_copy.erase(tablet->tablet_id()) != 1) {
        *out << "  [ERROR: not present in CM tablet map!]\n";
      }
    }
  }

  if (!tablets_copy.empty()) {
    *out << "Orphaned tablets (not referenced by any table):\n";
    for (const TabletInfoMap::value_type& entry : tablets_copy) {
      const scoped_refptr<TabletInfo>& tablet = entry.second;
      TabletMetadataLock l_tablet(tablet.get(), TabletMetadataLock::READ);
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet.data().pb.ShortDebugString() << "\n";
    }
  }

  if (!names_copy.empty()) {
    *out << "Orphaned tables (in by-name map, but not id map):\n";
    for (const TableInfoMap::value_type& e : names_copy) {
      *out << e.second->id() << ":\n";
      *out << "  name: \"" << CHexEscape(e.first) << "\"\n";
    }
  }
}

std::string CatalogManager::LogPrefix() const {
  return Substitute("T $0 P $1: ",
                    sys_catalog_->tablet_peer()->tablet_id(),
                    sys_catalog_->tablet_peer()->permanent_uuid());
}

////////////////////////////////////////////////////////////
// TabletInfo
////////////////////////////////////////////////////////////

TabletInfo::TabletInfo(const scoped_refptr<TableInfo>& table,
                       std::string tablet_id)
    : tablet_id_(std::move(tablet_id)),
      table_(table),
      last_update_time_(MonoTime::Now(MonoTime::FINE)),
      reported_schema_version_(0) {}

TabletInfo::~TabletInfo() {
}

void TabletInfo::SetReplicaLocations(const ReplicaMap& replica_locations) {
  boost::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = MonoTime::Now(MonoTime::FINE);
  replica_locations_ = replica_locations;
}

void TabletInfo::GetReplicaLocations(ReplicaMap* replica_locations) const {
  boost::lock_guard<simple_spinlock> l(lock_);
  *replica_locations = replica_locations_;
}

bool TabletInfo::AddToReplicaLocations(const TabletReplica& replica) {
  boost::lock_guard<simple_spinlock> l(lock_);
  return InsertIfNotPresent(&replica_locations_, replica.ts_desc->permanent_uuid(), replica);
}

void TabletInfo::set_last_update_time(const MonoTime& ts) {
  boost::lock_guard<simple_spinlock> l(lock_);
  last_update_time_ = ts;
}

MonoTime TabletInfo::last_update_time() const {
  boost::lock_guard<simple_spinlock> l(lock_);
  return last_update_time_;
}

bool TabletInfo::set_reported_schema_version(uint32_t version) {
  boost::lock_guard<simple_spinlock> l(lock_);
  if (version > reported_schema_version_) {
    reported_schema_version_ = version;
    return true;
  }
  return false;
}

uint32_t TabletInfo::reported_schema_version() const {
  boost::lock_guard<simple_spinlock> l(lock_);
  return reported_schema_version_;
}

std::string TabletInfo::ToString() const {
  return Substitute("$0 (table $1)", tablet_id_,
                    (table_ != nullptr ? table_->ToString() : "MISSING"));
}

void PersistentTabletInfo::set_state(SysTabletsEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

////////////////////////////////////////////////////////////
// TableInfo
////////////////////////////////////////////////////////////

TableInfo::TableInfo(std::string table_id) : table_id_(std::move(table_id)) {}

TableInfo::~TableInfo() {
}

std::string TableInfo::ToString() const {
  TableMetadataLock l(this, TableMetadataLock::READ);
  return Substitute("$0 [id=$1]", l.data().pb.name(), table_id_);
}

bool TableInfo::RemoveTablet(const std::string& partition_key_start) {
  boost::lock_guard<simple_spinlock> l(lock_);
  return EraseKeyReturnValuePtr(&tablet_map_, partition_key_start) != NULL;
}

void TableInfo::AddTablet(TabletInfo *tablet) {
  boost::lock_guard<simple_spinlock> l(lock_);
  AddTabletUnlocked(tablet);
}

void TableInfo::AddTablets(const vector<TabletInfo*>& tablets) {
  boost::lock_guard<simple_spinlock> l(lock_);
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

void TableInfo::GetTabletsInRange(const GetTableLocationsRequestPB* req,
                                  vector<scoped_refptr<TabletInfo> > *ret) const {
  boost::lock_guard<simple_spinlock> l(lock_);
  int max_returned_locations = req->max_returned_locations();

  TableInfo::TabletInfoMap::const_iterator it, it_end;
  if (req->has_partition_key_start()) {
    it = tablet_map_.upper_bound(req->partition_key_start());
    --it;
  } else {
    it = tablet_map_.begin();
  }

  if (req->has_partition_key_end()) {
    it_end = tablet_map_.upper_bound(req->partition_key_end());
  } else {
    it_end = tablet_map_.end();
  }

  int count = 0;
  for (; it != it_end && count < max_returned_locations; ++it) {
    ret->push_back(make_scoped_refptr(it->second));
    count++;
  }
}

bool TableInfo::IsAlterInProgress(uint32_t version) const {
  boost::lock_guard<simple_spinlock> l(lock_);
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
  boost::lock_guard<simple_spinlock> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    TabletMetadataLock tablet_lock(e.second, TabletMetadataLock::READ);
    if (!tablet_lock.data().is_running()) {
      return true;
    }
  }
  return false;
}

void TableInfo::AddTask(MonitoredTask* task) {
  boost::lock_guard<simple_spinlock> l(lock_);
  task->AddRef();
  pending_tasks_.insert(task);
}

void TableInfo::RemoveTask(MonitoredTask* task) {
  boost::lock_guard<simple_spinlock> l(lock_);
  pending_tasks_.erase(task);
  task->Release();
}

void TableInfo::AbortTasks() {
  boost::lock_guard<simple_spinlock> l(lock_);
  for (MonitoredTask* task : pending_tasks_) {
    task->Abort();
  }
}

void TableInfo::WaitTasksCompletion() {
  int wait_time = 5;
  while (1) {
    {
      boost::lock_guard<simple_spinlock> l(lock_);
      if (pending_tasks_.empty()) {
        break;
      }
    }
    base::SleepForMilliseconds(wait_time);
    wait_time = std::min(wait_time * 5 / 4, 10000);
  }
}

void TableInfo::GetTaskList(std::vector<scoped_refptr<MonitoredTask> > *ret) {
  boost::lock_guard<simple_spinlock> l(lock_);
  for (MonitoredTask* task : pending_tasks_) {
    ret->push_back(make_scoped_refptr(task));
  }
}

void TableInfo::GetAllTablets(vector<scoped_refptr<TabletInfo> > *ret) const {
  ret->clear();
  boost::lock_guard<simple_spinlock> l(lock_);
  for (const TableInfo::TabletInfoMap::value_type& e : tablet_map_) {
    ret->push_back(make_scoped_refptr(e.second));
  }
}

void PersistentTableInfo::set_state(SysTablesEntryPB::State state, const string& msg) {
  pb.set_state(state);
  pb.set_state_msg(msg);
}

} // namespace master
} // namespace kudu
