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

#include <memory>
#include <queue>
#include <regex>
#include <set>
#include <unordered_set>
#include <google/protobuf/util/message_differencer.h>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/pg_system_attr.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/master/ysql_tablegroup_manager.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_service.h"

#include "yb/client/client-internal.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_name.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_type_util.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb_pgapi.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/async_snapshot_tasks.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/restore_sys_catalog_state.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/service_util.h"

#include "yb/util/cast.h"
#include "yb/util/date_time.h"
#include "yb/util/debug-util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/service_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"
#include "yb/util/tostring.h"
#include "yb/util/string_util.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/ql/util/statement_result.h"

#include "ybgate/ybgate_api.h"

using namespace std::literals;
using namespace std::placeholders;

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::unordered_set;
using std::vector;
using std::set;

using strings::Substitute;

DEFINE_int32(cdc_state_table_num_tablets, 0,
             "Number of tablets to use when creating the CDC state table. "
             "0 to use the same default num tablets as for regular tables.");

DEFINE_int32(cdc_wal_retention_time_secs, 4 * 3600,
             "WAL retention time in seconds to be used for tables for which a CDC stream was "
             "created.");
DECLARE_int32(master_rpc_timeout_ms);

DEFINE_bool(enable_transaction_snapshots, true,
            "The flag enables usage of transaction aware snapshots.");
TAG_FLAG(enable_transaction_snapshots, hidden);
TAG_FLAG(enable_transaction_snapshots, advanced);
TAG_FLAG(enable_transaction_snapshots, runtime);

DEFINE_test_flag(bool, disable_cdc_state_insert_on_setup, false,
                 "Disable inserting new entries into cdc state as part of the setup flow.");

DECLARE_bool(xcluster_wait_on_ddl_alter);

DEFINE_bool(xcluster_skip_schema_compatibility_checks_on_alter, false,
            "When xCluster replication sends a DDL change, skip checks "
            "for any schema compatibility");
TAG_FLAG(xcluster_skip_schema_compatibility_checks_on_alter, runtime);

DEFINE_bool(allow_consecutive_restore, true,
            "DEPRECATED. Has no effect, use ForwardRestoreCheck to disallow any forward restores.");
TAG_FLAG(allow_consecutive_restore, runtime);

DEFINE_bool(check_bootstrap_required, false,
            "Is it necessary to check whether bootstrap is required for Universe Replication.");

DEFINE_test_flag(bool, exit_unfinished_deleting, false,
                 "Whether to exit part way through the deleting universe process.");

DEFINE_test_flag(bool, exit_unfinished_merging, false,
                 "Whether to exit part way through the merging universe process.");

DEFINE_bool(disable_universe_gc, false,
            "Whether to run the GC on universes or not.");
TAG_FLAG(disable_universe_gc, runtime);
DEFINE_test_flag(double, crash_during_sys_catalog_restoration, 0.0,
                 "Probability of crash during the RESTORE_SYS_CATALOG phase.");
TAG_FLAG(TEST_crash_during_sys_catalog_restoration, runtime);

DEFINE_bool(enable_replicate_transaction_status_table, false,
            "Whether to enable xCluster replication of the transaction status table.");

DEFINE_int32(
    cdc_parent_tablet_deletion_task_retry_secs, 30,
    "Frequency at which the background task will verify parent tablets retained for xCluster or "
    "CDCSDK replication and determine if they can be cleaned up.");

DEFINE_int32(wait_replication_drain_retry_timeout_ms, 2000,
             "Timeout in milliseconds in between CheckReplicationDrain calls to tservers "
             "in case of retries.");
DEFINE_test_flag(bool, hang_wait_replication_drain, false,
                 "Used in tests to temporarily block WaitForReplicationDrain.");
DEFINE_test_flag(bool, import_snapshot_failed, false,
                 "Return a error from ImportSnapshotMeta RPC for testing the RPC failure.");
DEFINE_int32(ns_replication_sync_retry_secs, 5,
             "Frequency at which the bg task will try to sync with producer and add tables to "
             "the current NS-level replication, when there are non-replicated consumer tables.");
DEFINE_int32(ns_replication_sync_backoff_secs, 60,
             "Frequency of the add table task for a NS-level replication, when there are no "
             "non-replicated consumer tables.");
DEFINE_int32(ns_replication_sync_error_backoff_secs, 300,
             "Frequency of the add table task for a NS-level replication, when there are too "
             "many consecutive errors happening for the replication.");

DEFINE_uint64(import_snapshot_max_concurrent_create_table_requests, 20,
             "Maximum number of create table requests to the master that can be outstanding "
             "during the import snapshot metadata phase of restore.");
TAG_FLAG(import_snapshot_max_concurrent_create_table_requests, runtime);

DEFINE_int32(inflight_splits_completion_timeout_secs, 600,
             "Total time to wait for all inflight splits to complete during Restore.");
TAG_FLAG(inflight_splits_completion_timeout_secs, advanced);
TAG_FLAG(inflight_splits_completion_timeout_secs, runtime);

DEFINE_int32(pitr_max_restore_duration_secs, 600,
             "Maximum amount of time to complete a PITR restore.");
TAG_FLAG(pitr_max_restore_duration_secs, advanced);
TAG_FLAG(pitr_max_restore_duration_secs, runtime);

DEFINE_int32(pitr_split_disable_check_freq_ms, 500,
             "Delay before retrying to see if inflight tablet split operations have completed "
             "after which PITR restore can be performed.");
TAG_FLAG(pitr_split_disable_check_freq_ms, advanced);
TAG_FLAG(pitr_split_disable_check_freq_ms, runtime);

DEFINE_int32(
    cdcsdk_table_processing_limit_per_run, 2,
    "The number of newly added tables we will add to CDCSDK streams, per run of the background "
    "task.");

DEFINE_RUNTIME_bool(
    enable_fast_pitr, true,
    "Whether fast restore of sys catalog on the master is enabled.");

namespace yb {

using rpc::RpcContext;
using pb_util::ParseFromSlice;
using client::internal::RemoteTabletServer;
using client::internal::RemoteTabletPtr;

namespace master {
namespace enterprise {

static const string kSystemXClusterReplicationId = "system";

namespace {

Status CheckStatus(const Status& status, const char* action) {
  if (status.ok()) {
    return status;
  }

  const Status s = status.CloneAndPrepend(std::string("An error occurred while ") + action);
  LOG(WARNING) << s;
  return s;
}

Status CheckLeaderStatus(const Status& status, const char* action) {
  return CheckIfNoLongerLeader(CheckStatus(status, action));
}

template<class RespClass>
Status CheckLeaderStatusAndSetupError(
    const Status& status, const char* action, RespClass* resp) {
  return CheckIfNoLongerLeaderAndSetupError(CheckStatus(status, action), resp);
}

} // namespace

////////////////////////////////////////////////////////////
// Snapshot Loader
////////////////////////////////////////////////////////////

class SnapshotLoader : public Visitor<PersistentSnapshotInfo> {
 public:
  explicit SnapshotLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  Status Visit(const SnapshotId& snapshot_id, const SysSnapshotEntryPB& metadata) override {
    if (TryFullyDecodeTxnSnapshotId(snapshot_id)) {
      // Transaction aware snapshots should be already loaded.
      return Status::OK();
    }
    return VisitNonTransactionAwareSnapshot(snapshot_id, metadata);
  }

  Status VisitNonTransactionAwareSnapshot(
      const SnapshotId& snapshot_id, const SysSnapshotEntryPB& metadata) {

    // Setup the snapshot info.
    auto snapshot_info = make_scoped_refptr<SnapshotInfo>(snapshot_id);
    auto l = snapshot_info->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the snapshot to the IDs map (if the snapshot is not deleted).
    auto emplace_result = catalog_manager_->non_txn_snapshot_ids_map_.emplace(
        snapshot_id, std::move(snapshot_info));
    CHECK(emplace_result.second) << "Snapshot already exists: " << snapshot_id;

    LOG(INFO) << "Loaded metadata for snapshot (id=" << snapshot_id << "): "
              << emplace_result.first->second->ToString() << ": " << metadata.ShortDebugString();
    l.Commit();
    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(SnapshotLoader);
};


////////////////////////////////////////////////////////////
// CDC Stream Loader
////////////////////////////////////////////////////////////

class CDCStreamLoader : public Visitor<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  void AddDefaultValuesIfMissing(const SysCDCStreamEntryPB& metadata,
                                 CDCStreamInfo::WriteLock* l) {
    bool source_type_present = false;
    bool checkpoint_type_present = false;

    // Iterate over all the options to check if checkpoint_type and source_type are present.
    for (auto option : metadata.options()) {
      if (option.key() == cdc::kSourceType) {
        source_type_present = true;
      }
      if (option.key() == cdc::kCheckpointType) {
        checkpoint_type_present = true;
      }
    }

    if (!source_type_present) {
      auto source_type_opt = l->mutable_data()->pb.add_options();
      source_type_opt->set_key(cdc::kSourceType);
      source_type_opt->set_value(cdc::CDCRequestSource_Name(cdc::XCLUSTER));
    }

    if (!checkpoint_type_present) {
      auto checkpoint_type_opt = l->mutable_data()->pb.add_options();
      checkpoint_type_opt->set_key(cdc::kCheckpointType);
      checkpoint_type_opt->set_value(cdc::CDCCheckpointType_Name(cdc::IMPLICIT));
    }
  }

  Status Visit(const CDCStreamId& stream_id, const SysCDCStreamEntryPB& metadata)
      REQUIRES(catalog_manager_->mutex_) {
    DCHECK(!ContainsKey(catalog_manager_->cdc_stream_map_, stream_id))
        << "CDC stream already exists: " << stream_id;

    // If CDCStream entry exists, then the current cluster is a producer.
    catalog_manager_->SetCDCServiceEnabled();

    scoped_refptr<NamespaceInfo> ns;
    scoped_refptr<TableInfo> table;

    if (metadata.has_namespace_id()) {
      ns = FindPtrOrNull(catalog_manager_->namespace_ids_map_, metadata.namespace_id());

      if (!ns) {
        LOG(DFATAL) << "Invalid namespace ID " << metadata.namespace_id() << " for stream "
                   << stream_id;
        // TODO (#2059): Potentially signals a race condition that namesapce got deleted
        // while stream was being created.
        // Log error and continue without loading the stream.
        return Status::OK();
      }
    } else {
      table = FindPtrOrNull(*catalog_manager_->table_ids_map_, metadata.table_id(0));
      if (!table) {
        LOG(ERROR) << "Invalid table ID " << metadata.table_id(0) << " for stream " << stream_id;
        // TODO (#2059): Potentially signals a race condition that table got deleted while stream
        //  was being created.
        // Log error and continue without loading the stream.
        return Status::OK();
      }
    }

    // Setup the CDC stream info.
    auto stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // If no source_type and checkpoint_type is present, that means the stream was created in
    // a previous version where these options were not present.
    AddDefaultValuesIfMissing(metadata, &l);

    // If the table has been deleted, then mark this stream as DELETING so it can be deleted by the
    // catalog manager background thread. Otherwise if this stream is missing an entry
    // for state, then mark its state as Active.

    if (((table && table->LockForRead()->is_deleting()) ||
         (ns && ns->state() == SysNamespaceEntryPB::DELETING)) &&
        !l.data().is_deleting()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::DELETING);
    } else if (!l.mutable_data()->pb.has_state()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::ACTIVE);
    }

    // Add the CDC stream to the CDC stream map.
    catalog_manager_->cdc_stream_map_[stream->id()] = stream;
    if (table) {
      catalog_manager_->
          xcluster_producer_tables_to_stream_map_[metadata.table_id(0)].insert(stream->id());
    }
    if (ns) {
      for (const auto& table_id : metadata.table_id()) {
        catalog_manager_->cdcsdk_tables_to_stream_map_[table_id].insert(stream->id());
      }

      // For CDCSDK Streams, we scan all the tables in the namespace, and compare it with all the
      // tables associated with the stream.
      if ((l->pb.state() == SysCDCStreamEntryPB::ACTIVE ||
           l->pb.state() == SysCDCStreamEntryPB::DELETING_METADATA) &&
          ns->state() == SysNamespaceEntryPB::RUNNING) {
        catalog_manager_->FindAllTablesMissingInCDCSDKStream(stream, &l);
      }
    }

    l.Commit();

    LOG(INFO) << "Loaded metadata for CDC stream " << stream->ToString() << ": "
              << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamLoader);
};

////////////////////////////////////////////////////////////
// Universe Replication Loader
////////////////////////////////////////////////////////////

class UniverseReplicationLoader : public Visitor<PersistentUniverseReplicationInfo> {
 public:
  explicit UniverseReplicationLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  Status Visit(const std::string& producer_id, const SysUniverseReplicationEntryPB& metadata)
      REQUIRES(catalog_manager_->mutex_) {
    DCHECK(!ContainsKey(catalog_manager_->universe_replication_map_, producer_id))
        << "Producer universe already exists: " << producer_id;

    // Setup the universe replication info.
    scoped_refptr<UniverseReplicationInfo> const ri = new UniverseReplicationInfo(producer_id);
    {
      auto l = ri->LockForWrite();
      l.mutable_data()->pb.CopyFrom(metadata);

      if (!l->is_active() && !l->is_deleted_or_failed()) {
        // Replication was not fully setup.
        LOG(WARNING) << "Universe replication in transient state: " << producer_id;

        // TODO: Should we delete all failed universe replication items?
      }

      // Add universe replication info to the universe replication map.
      catalog_manager_->universe_replication_map_[ri->id()] = ri;

      // Add any failed universes to be cleared
      if (l->is_deleted_or_failed() ||
          l->pb.state() == SysUniverseReplicationEntryPB::DELETING ||
          cdc::IsAlterReplicationUniverseId(l->pb.producer_id())) {
        catalog_manager_->universes_to_clear_.push_back(ri->id());
      }

      // Check if this is a namespace-level replication.
      if (l->pb.has_is_ns_replication() && l->pb.is_ns_replication()) {
        DCHECK(!ContainsKey(catalog_manager_->namespace_replication_map_, producer_id))
            << "Duplicated namespace-level replication producer universe:" << producer_id;
        catalog_manager_->namespace_replication_enabled_.store(true, std::memory_order_release);

        // Force the consumer to sync with producer immediately.
        auto& metadata = catalog_manager_->namespace_replication_map_[producer_id];
        metadata.next_add_table_task_time = CoarseMonoClock::Now();
      }

      l.Commit();
    }

    // Also keep track of consumer tables.
    for (const auto& table : metadata.validated_tables()) {
      CDCStreamId stream_id = FindWithDefault(metadata.table_streams(), table.first, "");
      if (stream_id.empty()) {
        LOG(WARNING) << "Unable to find stream id for table: " << table.first;
        continue;
      }
      catalog_manager_->xcluster_consumer_tables_to_stream_map_[table.second].emplace(
          metadata.producer_id(), stream_id);
    }

    LOG(INFO) << "Loaded metadata for universe replication " << ri->ToString();
    VLOG(1) << "Metadata for universe replication " << ri->ToString() << ": "
            << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(UniverseReplicationLoader);
};

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

CatalogManager::~CatalogManager() {
  if (StartShutdown()) {
    CompleteShutdown();
  }
}

void CatalogManager::CompleteShutdown() {
  snapshot_coordinator_.Shutdown();
  // Call shutdown on base class before exiting derived class destructor
  // because BgTasks is part of base & uses this derived class on Shutdown.
  super::CompleteShutdown();
}

Status CatalogManager::RunLoaders(int64_t term, SysCatalogLoadingState* state) {
  RETURN_NOT_OK(super::RunLoaders(term, state));

  // Clear the snapshots.
  non_txn_snapshot_ids_map_.clear();

  // Clear CDC stream map.
  cdc_stream_map_.clear();
  xcluster_producer_tables_to_stream_map_.clear();

  // Clear CDCSDK stream map.
  cdcsdk_tables_to_stream_map_.clear();

  // Clear universe replication map.
  universe_replication_map_.clear();
  xcluster_consumer_tables_to_stream_map_.clear();

  LOG_WITH_FUNC(INFO) << "Loading snapshots into memory.";
  unique_ptr<SnapshotLoader> snapshot_loader(new SnapshotLoader(this));
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(snapshot_loader.get()),
      "Failed while visiting snapshots in sys catalog");

  LOG_WITH_FUNC(INFO) << "Loading CDC streams into memory.";
  auto cdc_stream_loader = std::make_unique<CDCStreamLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(cdc_stream_loader.get()),
      "Failed while visiting CDC streams in sys catalog");
  // Load retained_by_xcluster_ and retained_by_cdcsdk_ only after loading all CDC streams to reduce
  // loops.
  LoadCDCRetainedTabletsSet();

  LOG_WITH_FUNC(INFO) << "Loading universe replication info into memory.";
  auto universe_replication_loader = std::make_unique<UniverseReplicationLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(universe_replication_loader.get()),
      "Failed while visiting universe replication info in sys catalog");

  return Status::OK();
}

void CatalogManager::LoadCDCRetainedTabletsSet() {
  for (const auto& hidden_tablet : hidden_tablets_) {
    // Only keep track of hidden tablets that have been split and that are part of a CDC stream.
    if (hidden_tablet->old_pb().split_tablet_ids_size() > 0) {
      bool is_table_cdc_producer = IsTableCdcProducer(*hidden_tablet->table());
      bool is_table_part_of_cdcsdk = IsTablePartOfCDCSDK(*hidden_tablet->table());
      if (is_table_cdc_producer || is_table_part_of_cdcsdk) {
        auto tablet_lock = hidden_tablet->LockForRead();
        HiddenReplicationParentTabletInfo info{
            .table_id_ = hidden_tablet->table()->id(),
            .parent_tablet_id_ = tablet_lock->pb.has_split_parent_tablet_id()
                                     ? tablet_lock->pb.split_parent_tablet_id()
                                     : "",
            .split_tablets_ = {
                tablet_lock->pb.split_tablet_ids(0), tablet_lock->pb.split_tablet_ids(1)}};

        if (is_table_cdc_producer) {
          retained_by_xcluster_.emplace(hidden_tablet->id(), info);
        }
        if (is_table_part_of_cdcsdk) {
          retained_by_cdcsdk_.emplace(hidden_tablet->id(), info);
        }
      }
    }
  }
}

Status CatalogManager::CreateSnapshot(const CreateSnapshotRequestPB* req,
                                      CreateSnapshotResponsePB* resp,
                                      RpcContext* rpc) {
  LOG(INFO) << "Servicing CreateSnapshot request: " << req->ShortDebugString();

  if (FLAGS_enable_transaction_snapshots && req->transaction_aware()) {
    return CreateTransactionAwareSnapshot(*req, resp, rpc);
  }

  if (req->has_schedule_id()) {
    auto schedule_id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(req->schedule_id()));
    auto snapshot_id = snapshot_coordinator_.CreateForSchedule(
        schedule_id, leader_ready_term(), rpc->GetClientDeadline());
    if (!snapshot_id.ok()) {
      LOG(INFO) << "Create snapshot failed: " << snapshot_id.status();
      return snapshot_id.status();
    }
    resp->set_snapshot_id(snapshot_id->data(), snapshot_id->size());
    return Status::OK();
  }

  return CreateNonTransactionAwareSnapshot(req, resp, rpc);
}

Status CatalogManager::CreateNonTransactionAwareSnapshot(
    const CreateSnapshotRequestPB* req,
    CreateSnapshotResponsePB* resp,
    RpcContext* rpc) {
  SnapshotId snapshot_id;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Verify that the system is not in snapshot creating/restoring state.
    if (!current_snapshot_id_.empty()) {
      return STATUS(IllegalState,
                    Format(
                        "Current snapshot id: $0. Parallel snapshot operations are not supported"
                        ": $1", current_snapshot_id_, req),
                    MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
    }

    // Create a new snapshot UUID.
    snapshot_id = GenerateIdUnlocked(SysRowEntryType::SNAPSHOT);
  }

  vector<scoped_refptr<TabletInfo>> all_tablets;

  // Create in memory snapshot data descriptor.
  scoped_refptr<SnapshotInfo> snapshot(new SnapshotInfo(snapshot_id));
  snapshot->mutable_metadata()->StartMutation();
  snapshot->mutable_metadata()->mutable_dirty()->pb.set_state(SysSnapshotEntryPB::CREATING);

  auto tables = VERIFY_RESULT(CollectTables(req->tables(),
                                            req->add_indexes(),
                                            true /* include_parent_colocated_table */));
  unordered_set<NamespaceId> added_namespaces;
  SysSnapshotEntryPB& pb = snapshot->mutable_metadata()->mutable_dirty()->pb;
  // Note: SysSnapshotEntryPB includes PBs for stored (1) namespaces (2) tables (3) tablets.
  RETURN_NOT_OK(AddNamespaceEntriesToPB(tables, pb.mutable_entries(), &added_namespaces));
  RETURN_NOT_OK(AddTableAndTabletEntriesToPB(
      tables, pb.mutable_entries(), pb.mutable_tablet_snapshots(), &all_tablets));

  VLOG(1) << "Snapshot " << snapshot->ToString()
          << ": PB=" << snapshot->mutable_metadata()->mutable_dirty()->pb.DebugString();

  // Write the snapshot data descriptor to the system catalog (in "creating" state).
  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "inserting snapshot into sys-catalog"));
  TRACE("Wrote snapshot to system catalog");

  // Commit in memory snapshot data descriptor.
  snapshot->mutable_metadata()->CommitMutation();

  // Put the snapshot data descriptor to the catalog manager.
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Verify that the snapshot does not exist.
    auto inserted = non_txn_snapshot_ids_map_.emplace(snapshot_id, snapshot).second;
    RSTATUS_DCHECK(inserted, IllegalState, Format("Snapshot already exists: $0", snapshot_id));
    current_snapshot_id_ = snapshot_id;
  }

  // Send CreateSnapshot requests to all TServers (one tablet - one request).
  for (const scoped_refptr<TabletInfo>& tablet : all_tablets) {
    TRACE("Locking tablet");
    auto l = tablet->LockForRead();

    LOG(INFO) << "Sending CreateTabletSnapshot to tablet: " << tablet->ToString();

    // Send Create Tablet Snapshot request to each tablet leader.
    auto call = CreateAsyncTabletSnapshotOp(
        tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::CREATE_ON_TABLET,
        TabletSnapshotOperationCallback());
    ScheduleTabletSnapshotOp(call);
  }

  resp->set_snapshot_id(snapshot_id);
  LOG(INFO) << "Successfully started snapshot " << snapshot_id << " creation";
  return Status::OK();
}

void CatalogManager::Submit(std::unique_ptr<tablet::Operation> operation, int64_t leader_term) {
  auto tablet_result = tablet_peer()->shared_tablet_safe();
  // TODO(tablet_ptr) The operation needs to hold a refcount for the tablet.
  // https://github.com/yugabyte/yugabyte-db/issues/14546
  LOG_IF(DFATAL, !tablet_result.ok()) << "Tablet is not running: " << tablet_result.status();
  operation->SetTablet(tablet_result->get());
  tablet_peer()->Submit(std::move(operation), leader_term);
}

Status CatalogManager::AddNamespaceEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    unordered_set<NamespaceId>* namespaces) {
  // Add provided namespaces.
  if (!DCHECK_NOTNULL(namespaces)->empty()) {
    SharedLock lock(mutex_);
    for (const NamespaceId& ns_id : *namespaces) {
      auto ns_info = VERIFY_RESULT(FindNamespaceByIdUnlocked(ns_id));
      TRACE("Locking namespace");
      AddInfoEntryToPB(ns_info.get(), out);
    }
  }

  for (const TableDescription& table : tables) {
    // Add namespace entry.
    if (namespaces->emplace(table.namespace_info->id()).second) {
      TRACE("Locking namespace");
      AddInfoEntryToPB(table.namespace_info.get(), out);
    }
  }

  return Status::OK();
}

Status CatalogManager::AddUDTypeEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    const unordered_set<NamespaceId>& namespaces) {
  // Collect all UDType entries.
  unordered_set<UDTypeId> type_ids;
  Schema schema;
  for (const TableDescription& table : tables) {
    RETURN_NOT_OK(table.table_info->GetSchema(&schema));
    for (size_t i = 0; i < schema.num_columns(); ++i) {
      for (const auto &udt_id : schema.column(i).type()->GetUserDefinedTypeIds()) {
        type_ids.insert(udt_id);
      }
    }
  }

  if (!type_ids.empty()) {
    // Add UDType entries.
    SharedLock lock(mutex_);
    for (const UDTypeId& udt_id : type_ids) {
      auto udt_info = VERIFY_RESULT(FindUDTypeByIdUnlocked(udt_id));
      TRACE("Locking user defined type");
      auto l = AddInfoEntryToPB(udt_info.get(), out);

      if (namespaces.find(udt_info->namespace_id()) == namespaces.end()) {
        return STATUS(
            NotSupported, "UDType from another keyspace is not supported",
            udt_info->namespace_id(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::AddTableAndTabletEntriesToPB(
    const vector<TableDescription>& tables,
    google::protobuf::RepeatedPtrField<SysRowEntry>* out,
    google::protobuf::RepeatedPtrField<SysSnapshotEntryPB::TabletSnapshotPB>* tablet_snapshot_info,
    vector<scoped_refptr<TabletInfo>>* all_tablets) {
  unordered_set<TabletId> added_tablets;
  for (const TableDescription& table : tables) {
    // Add table entry.
    TRACE("Locking table");
    AddInfoEntryToPB(table.table_info.get(), out);

    // Add tablet entries.
    for (const scoped_refptr<TabletInfo>& tablet : table.tablet_infos) {
      // For colocated tables there could be duplicate tablets, so insert them only once.
      if (added_tablets.insert(tablet->id()).second) {
        TRACE("Locking tablet");
        auto l = AddInfoEntryToPB(tablet.get(), out);

        if (tablet_snapshot_info) {
          SysSnapshotEntryPB::TabletSnapshotPB* const tablet_info = tablet_snapshot_info->Add();
          tablet_info->set_id(tablet->id());
          tablet_info->set_state(SysSnapshotEntryPB::CREATING);
        }

        if (all_tablets) {
          all_tablets->push_back(tablet);
        }
      }
    }
  }

  return Status::OK();
}

Result<SysRowEntries> CatalogManager::CollectEntries(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
    CollectFlags flags) {
  RETURN_NOT_OK(CheckIsLeaderAndReady());
  SysRowEntries entries;
  unordered_set<NamespaceId> namespaces;
  auto tables = VERIFY_RESULT(CollectTables(table_identifiers, flags, &namespaces));

  // Note: the list of entries includes: (1) namespaces (2) UD types (3) tables (4) tablets.
  RETURN_NOT_OK(AddNamespaceEntriesToPB(tables, entries.mutable_entries(), &namespaces));
  if (flags.Test(CollectFlag::kAddUDTypes)) {
    RETURN_NOT_OK(AddUDTypeEntriesToPB(tables, entries.mutable_entries(), namespaces));
  }
  // TODO(txn_snapshot) use single lock to resolve all tables to tablets
  RETURN_NOT_OK(AddTableAndTabletEntriesToPB(tables, entries.mutable_entries()));
  return entries;
}

Result<SysRowEntries> CatalogManager::CollectEntriesForSequencesDataTable() {
  auto sequence_entries_result = CollectEntries(
      CatalogManagerUtil::SequenceDataFilter(),
      CollectFlags{CollectFlag::kSucceedIfCreateInProgress});
  // If there are no sequences yet, then we won't be able to find the table.
  // It is ok and we shouldn't crash. Return an empty SysRowEntries in such a case.
  if (!sequence_entries_result.ok() && sequence_entries_result.status().IsNotFound()) {
    LOG(INFO) << "No sequences_data table created yet, so not including it in snapshot";
    return SysRowEntries();
  }
  return sequence_entries_result;
}

Result<SysRowEntries> CatalogManager::CollectEntriesForSnapshot(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) {
  SysRowEntries entries = VERIFY_RESULT(CollectEntries(
      tables,
      CollectFlags{CollectFlag::kAddIndexes, CollectFlag::kIncludeParentColocatedTable,
                   CollectFlag::kSucceedIfCreateInProgress}));
  // Include sequences_data table if the filter is on a ysql database.
  // For sequences, we have a special sequences_data (id=0000ffff00003000800000000000ffff)
  // table in the system_postgres database.
  // It is a normal YB table that has data partitioned into tablets and replicated using raft.
  // These tablets reside on the tservers. This table is created when the first
  // sequence is created. It stores one row per sequence and also needs to be restored.
  for (const auto& table : tables) {
    if (table.namespace_().database_type() == YQL_DATABASE_PGSQL) {
      auto seq_entries = VERIFY_RESULT(CollectEntriesForSequencesDataTable());
      entries.mutable_entries()->MergeFrom(seq_entries.entries());
      break;
    }
  }
  return entries;
}

server::Clock* CatalogManager::Clock() {
  return master_->clock();
}

Status CatalogManager::CreateTransactionAwareSnapshot(
    const CreateSnapshotRequestPB& req, CreateSnapshotResponsePB* resp, rpc::RpcContext* rpc) {
  CollectFlags flags{CollectFlag::kIncludeParentColocatedTable};
  flags.SetIf(CollectFlag::kAddIndexes, req.add_indexes())
       .SetIf(CollectFlag::kAddUDTypes, req.add_ud_types());
  SysRowEntries entries = VERIFY_RESULT(CollectEntries(req.tables(), flags));

  auto snapshot_id = VERIFY_RESULT(snapshot_coordinator_.Create(
      entries, req.imported(), leader_ready_term(), rpc->GetClientDeadline()));
  resp->set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  return Status::OK();
}

Status CatalogManager::ListSnapshots(const ListSnapshotsRequestPB* req,
                                     ListSnapshotsResponsePB* resp) {
  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (!current_snapshot_id_.empty()) {
      resp->set_current_snapshot_id(current_snapshot_id_);
    }

    auto setup_snapshot_pb_lambda = [resp](scoped_refptr<SnapshotInfo> snapshot_info) {
      auto snapshot_lock = snapshot_info->LockForRead();

      SnapshotInfoPB* const snapshot = resp->add_snapshots();
      snapshot->set_id(snapshot_info->id());
      *snapshot->mutable_entry() = snapshot_info->metadata().state().pb;
    };

    if (req->has_snapshot_id()) {
      if (!txn_snapshot_id) {
        TRACE("Looking up snapshot");
        scoped_refptr<SnapshotInfo> snapshot_info =
            FindPtrOrNull(non_txn_snapshot_ids_map_, req->snapshot_id());
        if (snapshot_info == nullptr) {
          return STATUS(InvalidArgument, "Could not find snapshot", req->snapshot_id(),
                        MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
        }

        setup_snapshot_pb_lambda(snapshot_info);
      }
    } else {
      for (const SnapshotInfoMap::value_type& entry : non_txn_snapshot_ids_map_) {
        setup_snapshot_pb_lambda(entry.second);
      }
    }
  }

  if (req->prepare_for_backup() && (!req->has_snapshot_id() || !txn_snapshot_id)) {
    return STATUS(
        InvalidArgument, "Request must have correct snapshot_id", (req->has_snapshot_id() ?
        req->snapshot_id() : "None"), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }
  RETURN_NOT_OK(snapshot_coordinator_.ListSnapshots(
      txn_snapshot_id, req->list_deleted_snapshots(), req->detail_options(), resp));
  if (req->prepare_for_backup()) {
    RETURN_NOT_OK(RepackSnapshotsForBackup(resp));
  }

  return Status::OK();
}

Status CatalogManager::RepackSnapshotsForBackup(ListSnapshotsResponsePB* resp) {
  SharedLock lock(mutex_);
  TRACE("Acquired catalog manager lock");

  // Repack & extend the backup row entries.
  for (SnapshotInfoPB& snapshot : *resp->mutable_snapshots()) {
    snapshot.set_format_version(2);
    SysSnapshotEntryPB& sys_entry = *snapshot.mutable_entry();
    snapshot.mutable_backup_entries()->Reserve(sys_entry.entries_size());

    unordered_set<TableId> tables_to_skip;
    for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      BackupRowEntryPB* const backup_entry = snapshot.add_backup_entries();

      // Setup BackupRowEntryPB fields.
      // Set BackupRowEntryPB::pg_schema_name for YSQL table to disambiguate in case tables
      // in different schema have same name.
      if (entry.type() == SysRowEntryType::TABLE) {
        TRACE("Looking up table");
        scoped_refptr<TableInfo> table_info = FindPtrOrNull(*table_ids_map_, entry.id());
        if (table_info == nullptr) {
          return STATUS(
              InvalidArgument, "Table not found by ID", entry.id(),
              MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
        }

        TRACE("Locking table");
        auto l = table_info->LockForRead();
        // PG schema name is available for YSQL table only, except for colocation parent tables.
        if (l->table_type() == PGSQL_TABLE_TYPE && !IsColocationParentTableId(entry.id())) {
          const auto res = GetPgSchemaName(table_info);
          if (!res.ok()) {
            // Check for the scenario where the table is dropped by YSQL but not docdb - this can
            // happen due to a bug with the async nature of drops in PG with docdb.
            // If this occurs don't block the entire backup, instead skip this table(see gh #13361).
            if (res.status().IsNotFound() &&
                res.status().message().ToBuffer().find(kRelnamespaceNotFoundErrorStr)
                    != string::npos) {
              LOG(WARNING) << "Skipping backup of table " << table_info->id() << " : " << res;
              snapshot.mutable_backup_entries()->RemoveLast();
              // Keep track of table so we skip its tablets as well. Note, since tablets always
              // follow their table in sys_entry, we don't need to check previous tablet entries.
              tables_to_skip.insert(table_info->id());
              continue;
            }

            // Other errors cannot be skipped.
            return res.status();
          }
          const string pg_schema_name = res.get();
          VLOG(1) << "PG Schema: " << pg_schema_name << " for table " << table_info->ToString();
          backup_entry->set_pg_schema_name(pg_schema_name);
        }
      } else if (!tables_to_skip.empty() && entry.type() == SysRowEntryType::TABLET) {
        // Note: Ordering here is important, we expect tablet entries only after their table entry.
        SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));
        if (tables_to_skip.contains(meta.table_id())) {
          LOG(WARNING) << "Skipping backup of tablet " << entry.id() << " since its table "
                       << meta.table_id() << " was skipped.";
          snapshot.mutable_backup_entries()->RemoveLast();
          continue;
        }
      }

      // Init BackupRowEntryPB::entry.
      backup_entry->mutable_entry()->Swap(&entry);
    }

    // Clear out redundant/unused fields for backups (reduces size of SnapshotInfoPB file):
    // - Can remove the tablet_snapshots as if the main snapshot state is COMPLETE, then all of the
    //   tablet records are also COMPLETE.
    // - Can remove entries, since all the valid entries are already in backup_entries.
    sys_entry.clear_tablet_snapshots();
    sys_entry.clear_entries();
  }

  return Status::OK();
}

Status CatalogManager::ListSnapshotRestorations(const ListSnapshotRestorationsRequestPB* req,
                                                ListSnapshotRestorationsResponsePB* resp) {
  TxnSnapshotRestorationId restoration_id = TxnSnapshotRestorationId::Nil();
  if (!req->restoration_id().empty()) {
    restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(req->restoration_id()));
  }
  TxnSnapshotId snapshot_id = TxnSnapshotId::Nil();
  if (!req->snapshot_id().empty()) {
    snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(req->snapshot_id()));
  }

  return snapshot_coordinator_.ListRestorations(restoration_id, snapshot_id, resp);
}

Status CatalogManager::RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                       RestoreSnapshotResponsePB* resp) {
  LOG(INFO) << "Servicing RestoreSnapshot request: " << req->ShortDebugString();

  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    HybridTime ht;
    if (req->has_restore_ht()) {
      ht = HybridTime(req->restore_ht());
    }
    TxnSnapshotRestorationId id = VERIFY_RESULT(snapshot_coordinator_.Restore(
        txn_snapshot_id, ht, leader_ready_term()));
    resp->set_restoration_id(id.data(), id.size());
    return Status::OK();
  }

  return RestoreNonTransactionAwareSnapshot(req->snapshot_id());
}

Status CatalogManager::RestoreNonTransactionAwareSnapshot(const string& snapshot_id) {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  if (!current_snapshot_id_.empty()) {
    return STATUS(
        IllegalState,
        Format(
            "Current snapshot id: $0. Parallel snapshot operations are not supported: $1",
            current_snapshot_id_, snapshot_id),
        MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
  }

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, snapshot_id);
  if (snapshot == nullptr) {
    return STATUS(InvalidArgument, "Could not find snapshot", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->started_deleting()) {
    return STATUS(NotFound, "The snapshot was deleted", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  if (!snapshot_lock->is_complete()) {
    return STATUS(IllegalState, "The snapshot state is not complete", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_IS_NOT_READY));
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock.mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::RESTORING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::RESTORING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "updating snapshot in sys-catalog"));

  // CatalogManager lock 'lock_' is still locked here.
  current_snapshot_id_ = snapshot_id;

  // Restore all entries.
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    RETURN_NOT_OK(RestoreEntry(entry, snapshot_id));
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock.Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " restoring";
  return Status::OK();
}

Status CatalogManager::RestoreEntry(const SysRowEntry& entry, const SnapshotId& snapshot_id) {
  switch (entry.type()) {
    case SysRowEntryType::NAMESPACE: { // Restore NAMESPACES.
      TRACE("Looking up namespace");
      scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, entry.id());
      if (ns == nullptr) {
        // Restore Namespace.
        // TODO: implement
        LOG(INFO) << "Restoring: NAMESPACE id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring namespace: id=$0", entry.type()));
      }
      break;
    }
    case SysRowEntryType::TABLE: { // Restore TABLES.
      TRACE("Looking up table");
      scoped_refptr<TableInfo> table = FindPtrOrNull(*table_ids_map_, entry.id());
      if (table == nullptr) {
        // Restore Table.
        // TODO: implement
        LOG(INFO) << "Restoring: TABLE id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring table: id=$0", entry.type()));
      }
      break;
    }
    case SysRowEntryType::TABLET: { // Restore TABLETS.
      TRACE("Looking up tablet");
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());
      if (tablet == nullptr) {
        // Restore Tablet.
        // TODO: implement
        LOG(INFO) << "Restoring: TABLET id = " << entry.id();

        return STATUS(NotSupported, Substitute(
            "Not implemented: restoring tablet: id=$0", entry.type()));
      } else {
        TRACE("Locking tablet");
        auto l = tablet->LockForRead();

        LOG(INFO) << "Sending RestoreTabletSnapshot to tablet: " << tablet->ToString();
        // Send RestoreSnapshot requests to all TServers (one tablet - one request).
        auto task = CreateAsyncTabletSnapshotOp(
            tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::RESTORE_ON_TABLET,
            TabletSnapshotOperationCallback());
        ScheduleTabletSnapshotOp(task);
      }
      break;
    }
    default:
      return STATUS_FORMAT(
          InternalError, "Unexpected entry type in the snapshot: $0", entry.type());
  }

  return Status::OK();
}

Status CatalogManager::DeleteSnapshot(const DeleteSnapshotRequestPB* req,
                                      DeleteSnapshotResponsePB* resp,
                                      RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteSnapshot request: " << req->ShortDebugString();

  auto txn_snapshot_id = TryFullyDecodeTxnSnapshotId(req->snapshot_id());
  if (txn_snapshot_id) {
    return snapshot_coordinator_.Delete(
        txn_snapshot_id, leader_ready_term(), rpc->GetClientDeadline());
  }

  return DeleteNonTransactionAwareSnapshot(req->snapshot_id());
}

Status CatalogManager::DeleteNonTransactionAwareSnapshot(const SnapshotId& snapshot_id) {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  TRACE("Looking up snapshot");
  scoped_refptr<SnapshotInfo> snapshot = FindPtrOrNull(
      non_txn_snapshot_ids_map_, snapshot_id);
  if (snapshot == nullptr) {
    return STATUS(InvalidArgument, "Could not find snapshot", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  auto snapshot_lock = snapshot->LockForWrite();

  if (snapshot_lock->started_deleting()) {
    return STATUS(NotFound, "The snapshot was deleted", snapshot_id,
                  MasterError(MasterErrorPB::SNAPSHOT_NOT_FOUND));
  }

  if (snapshot_lock->is_restoring()) {
    return STATUS(InvalidArgument, "The snapshot is being restored now", snapshot_id,
                  MasterError(MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION));
  }

  TRACE("Updating snapshot metadata on disk");
  SysSnapshotEntryPB& snapshot_pb = snapshot_lock.mutable_data()->pb;
  snapshot_pb.set_state(SysSnapshotEntryPB::DELETING);

  // Update tablet states.
  SetTabletSnapshotsState(SysSnapshotEntryPB::DELETING, &snapshot_pb);

  // Update sys-catalog with the updated snapshot state.
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), snapshot),
      "updating snapshot in sys-catalog"));

  // Send DeleteSnapshot requests to all TServers (one tablet - one request).
  for (const SysRowEntry& entry : snapshot_pb.entries()) {
    if (entry.type() == SysRowEntryType::TABLET) {
      TRACE("Looking up tablet");
      scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());
      if (tablet == nullptr) {
        LOG(WARNING) << "Deleting tablet not found " << entry.id();
      } else {
        TRACE("Locking tablet");
        auto l = tablet->LockForRead();

        LOG(INFO) << "Sending DeleteTabletSnapshot to tablet: " << tablet->ToString();
        // Send DeleteSnapshot requests to all TServers (one tablet - one request).
        auto task = CreateAsyncTabletSnapshotOp(
            tablet, snapshot_id, tserver::TabletSnapshotOpRequestPB::DELETE_ON_TABLET,
            TabletSnapshotOperationCallback());
        ScheduleTabletSnapshotOp(task);
      }
    }
  }

  // Commit in memory snapshot data descriptor.
  TRACE("Committing in-memory snapshot state");
  snapshot_lock.Commit();

  LOG(INFO) << "Successfully started snapshot " << snapshot->ToString() << " deletion";
  return Status::OK();
}

Status CatalogManager::ImportSnapshotPreprocess(
    const SnapshotInfoPB& snapshot_pb,
    NamespaceMap* namespace_map,
    UDTypeMap* type_map,
    ExternalTableSnapshotDataMap* tables_data) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: // Recreate NAMESPACE.
        RETURN_NOT_OK(ImportNamespaceEntry(entry, namespace_map));
        break;
      case SysRowEntryType::UDTYPE: // Create TYPE metadata.
        LOG_IF(DFATAL, entry.id().empty()) << "Empty entry id";

        if (type_map->find(entry.id()) != type_map->end()) {
          LOG_WITH_FUNC(WARNING) << "Ignoring duplicate type with id " << entry.id();
        } else {
          ExternalUDTypeSnapshotData& data = (*type_map)[entry.id()];
          data.type_entry_pb = VERIFY_RESULT(ParseFromSlice<SysUDTypeEntryPB>(entry.data()));
          // The value 'new_type_id' will be filled in ImportUDTypeEntry()
          // when the UDT will be found or recreated. Now it's empty value.
        }
        break;
      case SysRowEntryType::TABLE: { // Create TABLE metadata.
          LOG_IF(DFATAL, entry.id().empty()) << "Empty entry id";
          ExternalTableSnapshotData& data = (*tables_data)[entry.id()];

          if (data.old_table_id.empty()) {
            data.old_table_id = entry.id();
            data.table_meta = ImportSnapshotMetaResponsePB::TableMetaPB();
            data.table_entry_pb = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));

            if (backup_entry.has_pg_schema_name()) {
              data.pg_schema_name = backup_entry.pg_schema_name();
            }
          } else {
            LOG_WITH_FUNC(WARNING) << "Ignoring duplicate table with id " << entry.id();
          }

          LOG_IF(DFATAL, data.old_table_id.empty()) << "Not initialized table id";
        }
        break;
      case SysRowEntryType::TABLET: // Preprocess original tablets.
        RETURN_NOT_OK(PreprocessTabletEntry(entry, tables_data));
        break;
      case SysRowEntryType::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SYS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::CDC_STREAM: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT:  FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_SCHEDULE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::DDL_LOG_ENTRY: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_RESTORATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_SAFE_TIME: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNKNOWN:
        FATAL_INVALID_ENUM_VALUE(SysRowEntryType, entry.type());
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotProcessUDTypes(const SnapshotInfoPB& snapshot_pb,
                                                    UDTypeMap* type_map,
                                                    const NamespaceMap& namespace_map) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::UDTYPE) {
      // Create UD type.
      RETURN_NOT_OK(ImportUDTypeEntry(entry.id(), type_map, namespace_map));
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotCreateIndexes(const SnapshotInfoPB& snapshot_pb,
                                                   const NamespaceMap& namespace_map,
                                                   const UDTypeMap& type_map,
                                                   ExternalTableSnapshotDataMap* tables_data) {
  // Create ONLY INDEXES.
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLE) {
      ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
      if (data.is_index()) {
        // YSQL indices can be in an invalid state. In this state they are omitted by ysql_dump.
        // Assume this is an invalid index that wasn't part of the ysql_dump instead of failing the
        // import here.
        auto s = ImportTableEntry(namespace_map, type_map, *tables_data, &data);
        if (s.IsInvalidArgument() && MasterError(s) == MasterErrorPB::OBJECT_NOT_FOUND) {
          continue;
        } else if (!s.ok()) {
          return s;
        }
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotCreateAndWaitForTables(
    const SnapshotInfoPB& snapshot_pb, const NamespaceMap& namespace_map,
    const UDTypeMap& type_map, ExternalTableSnapshotDataMap* tables_data,
    CoarseTimePoint deadline) {
  std::queue<TableId> pending_creates;
  for (const auto& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    // Only for tables that are not indexes.
    if (entry.type() != SysRowEntryType::TABLE) {
      continue;
    }
    // ExternalTableSnapshotData only contains entries for tables, so
    // we access it after the entry type check above.
    ExternalTableSnapshotData& data = (*tables_data)[entry.id()];
    if (data.is_index()) {
      continue;
    }
    // If we are at the limit, wait for the oldest table to be created
    // so that we can send create request for the current table.
    DCHECK_LE(pending_creates.size(), FLAGS_import_snapshot_max_concurrent_create_table_requests);
    while (pending_creates.size() >= FLAGS_import_snapshot_max_concurrent_create_table_requests) {
      RETURN_NOT_OK(WaitForCreateTableToFinish(pending_creates.front(), deadline));
      LOG(INFO) << "ImportSnapshot: Create table finished for " << pending_creates.front()
                << ", time remaining " << ToSeconds(deadline - CoarseMonoClock::Now()) << " secs";
      pending_creates.pop();
    }
    // Ready to send request for this table now.
    RETURN_NOT_OK(ImportTableEntry(namespace_map, type_map, *tables_data, &data));
    pending_creates.push(data.new_table_id);
  }

  // Pop from queue and wait for those tables to be created.
  while (!pending_creates.empty()) {
    RETURN_NOT_OK(WaitForCreateTableToFinish(pending_creates.front(), deadline));
    LOG(INFO) << "ImportSnapshot: Create table finished for " << pending_creates.front()
              << ", time remaining " << ToSeconds(deadline - CoarseMonoClock::Now()) << " secs";
    pending_creates.pop();
  }

  return Status::OK();
}

Status CatalogManager::ImportSnapshotProcessTablets(const SnapshotInfoPB& snapshot_pb,
                                                    ExternalTableSnapshotDataMap* tables_data) {
  for (const BackupRowEntryPB& backup_entry : snapshot_pb.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    if (entry.type() == SysRowEntryType::TABLET) {
      // Create tablets IDs map.
      RETURN_NOT_OK(ImportTabletEntry(entry, tables_data));
    }
  }

  return Status::OK();
}

template <class RespClass>
void ProcessDeleteObjectStatus(const string& obj_name,
                               const string& id,
                               const RespClass& resp,
                               const Status& s) {
  Status result = s;
  if (result.ok() && resp.has_error()) {
    result = StatusFromPB(resp.error().status());
    LOG_IF(DFATAL, result.ok()) << "Expecting error status";
  }

  if (!result.ok()) {
    LOG_WITH_FUNC(WARNING) << "Failed to delete new " << obj_name << " with id=" << id
                           << ": " << result;
  }
}

void CatalogManager::DeleteNewUDtype(const UDTypeId& udt_id,
                                     const unordered_set<UDTypeId>& type_ids_to_delete) {
  auto res_udt = FindUDTypeById(udt_id);
  if (!res_udt.ok()) {
    return; // Already deleted.
  }

  auto type_info = *res_udt;
  LOG_WITH_FUNC(INFO) << "Deleting new UD type '" << type_info->name() << "' with id=" << udt_id;

  // Try to delete sub-types.
  unordered_set<UDTypeId> sub_type_ids;
  for (int i = 0; i < type_info->field_types_size(); ++i) {
    const Status s = IterateAndDoForUDT(
        type_info->field_types(i),
        [&sub_type_ids](const QLTypePB::UDTypeInfo& udtype_info) -> Status {
          sub_type_ids.insert(udtype_info.id());
          return Status::OK();
        });

    if (!s.ok()) {
      LOG_WITH_FUNC(WARNING) << "Failed IterateAndDoForUDT for type " << udt_id << ": " << s;
    }
  }

  DeleteUDTypeRequestPB req;
  DeleteUDTypeResponsePB resp;
  req.mutable_type()->mutable_namespace_()->set_id(type_info->namespace_id());
  req.mutable_type()->set_type_id(udt_id);
  ProcessDeleteObjectStatus("ud-type", udt_id, resp, DeleteUDType(&req, &resp, nullptr));

  for (const UDTypeId& sub_udt_id : sub_type_ids) {
    // Delete only NEW re-created types. Keep old ones.
    if (type_ids_to_delete.find(sub_udt_id) != type_ids_to_delete.end()) {
      DeleteNewUDtype(sub_udt_id, type_ids_to_delete);
    }
  }
}

void CatalogManager::DeleteNewSnapshotObjects(const NamespaceMap& namespace_map,
                                              const UDTypeMap& type_map,
                                              const ExternalTableSnapshotDataMap& tables_data) {
  for (const ExternalTableSnapshotDataMap::value_type& entry : tables_data) {
    const TableId& old_id = entry.first;
    const TableId& new_id = entry.second.new_table_id;
    const TableType type = entry.second.table_entry_pb.table_type();

    // Do not delete YSQL objects - it must be deleted via PG API.
    if (new_id.empty() || new_id == old_id || type == TableType::PGSQL_TABLE_TYPE) {
      continue;
    }

    LOG_WITH_FUNC(INFO) << "Deleting new table with id=" << new_id << " old id=" << old_id;
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_id(new_id);
    req.set_is_index_table(entry.second.is_index());
    ProcessDeleteObjectStatus("table", new_id, resp, DeleteTable(&req, &resp, nullptr));
  }

  unordered_set<UDTypeId> type_ids_to_delete;
  for (const UDTypeMap::value_type& entry : type_map) {
    const UDTypeId& old_id = entry.first;
    const UDTypeId& new_id = entry.second.new_type_id;
    const bool existing = !entry.second.just_created;

    if (existing || new_id.empty() || new_id == old_id) {
      continue;
    }

    type_ids_to_delete.insert(new_id);
  }

  for (auto type_id : type_ids_to_delete) {
    // The UD types are creating a tree. Order in the set collection of ids is random.
    // Recursively delete sub-types together with this type to simplify the code.
    //
    // Example: udt2 --uses--> udt1
    //     DROP udt1 - failed (referenced by udt2)
    //     DROP udt2 - success - drop subtypes:
    //         DROP udt1 - success
    DeleteNewUDtype(type_id, type_ids_to_delete);
  }

  for (const NamespaceMap::value_type& entry : namespace_map) {
    const NamespaceId& old_id = entry.first;
    const NamespaceId& new_id = entry.second.new_namespace_id;
    const YQLDatabase& db_type = entry.second.db_type;
    const bool existing = !entry.second.just_created;

    // Do not delete YSQL objects - it must be deleted via PG API.
    if (existing || new_id.empty() || new_id == old_id || db_type == YQL_DATABASE_PGSQL) {
      continue;
    }

    LOG_WITH_FUNC(INFO) << "Deleting new namespace with id=" << new_id << " old id=" << old_id;
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_id(new_id);
    ProcessDeleteObjectStatus("namespace", new_id, resp, DeleteNamespace(&req, &resp, nullptr));
  }
}

Status CatalogManager::ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                          ImportSnapshotMetaResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing ImportSnapshotMeta request: " << req->ShortDebugString();
  NamespaceMap namespace_map;
  UDTypeMap type_map;
  ExternalTableSnapshotDataMap tables_data;
  bool successful_exit = false;

  auto se = ScopeExit([this, &namespace_map, &type_map, &tables_data, &successful_exit] {
    if (!successful_exit) {
      DeleteNewSnapshotObjects(namespace_map, type_map, tables_data);
    }
  });

  const SnapshotInfoPB& snapshot_pb = req->snapshot();

  if (!snapshot_pb.has_format_version() || snapshot_pb.format_version() != 2) {
    return STATUS(InternalError, "Expected snapshot data in format 2",
        snapshot_pb.ShortDebugString(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  if (snapshot_pb.backup_entries_size() == 0) {
    return STATUS(InternalError, "Expected snapshot data prepared for backup",
        snapshot_pb.ShortDebugString(), MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  // PHASE 1: Recreate namespaces, create type's & table's meta data.
  RETURN_NOT_OK(ImportSnapshotPreprocess(snapshot_pb, &namespace_map, &type_map, &tables_data));

  // PHASE 2: Recreate UD types.
  RETURN_NOT_OK(ImportSnapshotProcessUDTypes(snapshot_pb, &type_map, namespace_map));

  // PHASE 3: Recreate ONLY tables.
  RETURN_NOT_OK(ImportSnapshotCreateAndWaitForTables(
      snapshot_pb, namespace_map, type_map, &tables_data, rpc->GetClientDeadline()));

  // PHASE 4: Recreate ONLY indexes.
  RETURN_NOT_OK(ImportSnapshotCreateIndexes(
      snapshot_pb, namespace_map, type_map, &tables_data));

  // PHASE 5: Restore tablets.
  RETURN_NOT_OK(ImportSnapshotProcessTablets(snapshot_pb, &tables_data));

  if (PREDICT_FALSE(FLAGS_TEST_import_snapshot_failed)) {
     const string msg = "ImportSnapshotMeta interrupted due to test flag";
     LOG_WITH_FUNC(WARNING) << msg;
     return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  successful_exit = true;
  // Copy the table mapping into the response.
  for (auto& [_, table_data] : tables_data) {
    if (table_data.table_meta) {
      resp->mutable_tables_meta()->Add()->Swap(&*table_data.table_meta);
    }
  }
  return Status::OK();
}

Status CatalogManager::ChangeEncryptionInfo(const ChangeEncryptionInfoRequestPB* req,
                                            ChangeEncryptionInfoResponsePB* resp) {
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto encryption_info = l.mutable_data()->pb.mutable_encryption_info();

  RETURN_NOT_OK(encryption_manager_->ChangeEncryptionInfo(req, encryption_info));

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "updating cluster config in sys-catalog"));
  l.Commit();

  std::lock_guard<simple_spinlock> lock(should_send_universe_key_registry_mutex_);
  for (auto& entry : should_send_universe_key_registry_) {
    entry.second = true;
  }

  return Status::OK();
}

Status CatalogManager::IsEncryptionEnabled(const IsEncryptionEnabledRequestPB* req,
                                           IsEncryptionEnabledResponsePB* resp) {
  return encryption_manager_->IsEncryptionEnabled(
      ClusterConfig()->LockForRead()->pb.encryption_info(), resp);
}

Status CatalogManager::ImportNamespaceEntry(const SysRowEntry& entry,
                                            NamespaceMap* namespace_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::NAMESPACE)
      << "Unexpected entry type: " << entry.type();

  SysNamespaceEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
  ExternalNamespaceSnapshotData& ns_data = (*namespace_map)[entry.id()];
  ns_data.db_type = GetDatabaseType(meta);

  TRACE("Looking up namespace");
  // First of all try to find the namespace by ID. It will work if we are restoring the backup
  // on the original cluster where the backup was created.
  scoped_refptr<NamespaceInfo> ns;
  {
    SharedLock lock(mutex_);
    ns = FindPtrOrNull(namespace_ids_map_, entry.id());
  }

  if (ns != nullptr && ns->name() == meta.name() && ns->state() == SysNamespaceEntryPB::RUNNING) {
    ns_data.new_namespace_id = entry.id();
    return Status::OK();
  }

  // If the namespace was not found by ID, it's ok on a new cluster OR if the namespace was
  // deleted and created again. In both cases the namespace can be found by NAME.
  if (ns_data.db_type == YQL_DATABASE_PGSQL) {
    // YSQL database must be created via external call. Find it by name.
    {
      SharedLock lock(mutex_);
      ns = FindPtrOrNull(namespace_names_mapper_[ns_data.db_type], meta.name());
    }

    if (ns == nullptr) {
      const string msg = Format("YSQL database must exist: $0", meta.name());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
    }
    if (ns->state() != SysNamespaceEntryPB::RUNNING) {
      const string msg = Format("Found YSQL database must be running: $0", meta.name());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
    }

    auto ns_lock = ns->LockForRead();
    ns_data.new_namespace_id = ns->id();
  } else {
    CreateNamespaceRequestPB req;
    CreateNamespaceResponsePB resp;
    req.set_name(meta.name());
    const Status s = CreateNamespace(&req, &resp, nullptr);

    if (s.ok()) {
      // The namespace was successfully re-created.
      ns_data.just_created = true;
    } else if (s.IsAlreadyPresent()) {
      LOG_WITH_FUNC(INFO) << "Using existing namespace '" << meta.name() << "': " << resp.id();
    } else {
      return s.CloneAndAppend("Failed to create namespace");
    }

    ns_data.new_namespace_id = resp.id();
  }
  return Status::OK();
}

Status CatalogManager::UpdateUDTypes(QLTypePB* pb_type, const UDTypeMap& type_map) {
  return IterateAndDoForUDT(
      pb_type,
      [&type_map](QLTypePB::UDTypeInfo* udtype_info) -> Status {
        const UDTypeId& old_udt_id = udtype_info->id();
        auto udt_it = type_map.find(old_udt_id);
        if (udt_it == type_map.end()) {
          const string msg = Format("Not found referenced type id $0", old_udt_id);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }

        const UDTypeId& new_udt_id = udt_it->second.new_type_id;
        if (new_udt_id.empty()) {
          const string msg = Format("Unknown new id for UD type $0 old id $1",
              udtype_info->name(), old_udt_id);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }

        if (old_udt_id != new_udt_id) {
          LOG(INFO) << "Replacing UD type '" << udtype_info->name()
                    << "' id from " << old_udt_id << " to " << new_udt_id;
          udtype_info->set_id(new_udt_id);
        }
        return Status::OK();
      });
}

Status CatalogManager::ImportUDTypeEntry(const UDTypeId& udt_id,
                                         UDTypeMap* type_map,
                                         const NamespaceMap& namespace_map) {
  auto udt_it = DCHECK_NOTNULL(type_map)->find(udt_id);
  if (udt_it == type_map->end()) {
    const string msg = Format("Not found metadata for referenced type id $0", udt_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  ExternalUDTypeSnapshotData& udt_data = udt_it->second;

  // If the type has been already processed: found or re-created.
  if (!udt_data.new_type_id.empty()) {
    return Status::OK();
  }

  SysUDTypeEntryPB& meta = udt_data.type_entry_pb;

  // First of all find and check referenced namespace.
  auto ns_it = namespace_map.find(meta.namespace_id());
  if (ns_it == namespace_map.end()) {
    const string msg = Format("Unknown keyspace $0 referenced in UD type $1 id $2",
        meta.namespace_id(), meta.name(), udt_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
  }

  const ExternalNamespaceSnapshotData& ns_data = ns_it->second;
  if (ns_data.db_type != YQL_DATABASE_CQL) {
    const string msg = Format(
        "UD type $0 id $1 references non CQL namespace: $2 type $3 (old id $4)",
        meta.name(), udt_id, ns_data.new_namespace_id, ns_data.db_type, meta.namespace_id());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  if (meta.field_names_size() != meta.field_types_size()) {
    const string msg = Format(
        "UD type $0 id $1 has $2 names and $3 types",
        meta.name(), udt_id, meta.field_names_size(), meta.field_types_size());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  // There are 3 cases:
  // Case 1: Find UDT by ID.
  //         Restoring the backup on the original cluster where the backup was created.
  // Case 2: Find UDT by name in the needed keyspace.
  //         Restoring the backup on the new cluster, but UDT was already created
  //         by the user or in the previous backup restoration.
  // Case 3: Re-create the UDT.
  //         Restoring the backup on the new empty cluster.

  // Case 1: try to find the type by ID.
  scoped_refptr<UDTypeInfo> udt;
  Result<scoped_refptr<UDTypeInfo>> res_udt = FindUDTypeById(udt_id);
  if (res_udt.ok() && (*res_udt)->name() == meta.name() &&
      (*res_udt)->namespace_id() == ns_data.new_namespace_id) {
    // Use found by ID UD type.
    udt_data.new_type_id = udt_id;
    LOG_WITH_FUNC(INFO) << "Using found by id UD type '" << meta.name() << "' in namespace "
                        << ns_data.new_namespace_id << ": " << udt_data.new_type_id;
    udt = *res_udt;
  } else {
    // Case 2 & 3: Try to create the new UD type.

    // Recursively create all referenced sub-types.
    unordered_set<UDTypeId> sub_type_ids;
    for (int i = 0; i < meta.field_types_size(); ++i) {
      RETURN_NOT_OK(
        IterateAndDoForUDT(
          meta.field_types(i),
          [&sub_type_ids](const QLTypePB::UDTypeInfo& udtype_info) -> Status {
            sub_type_ids.insert(udtype_info.id());
            return Status::OK();
          }));
    }

    for (const UDTypeId& sub_udt_id : sub_type_ids) {
      RETURN_NOT_OK(ImportUDTypeEntry(sub_udt_id, type_map, namespace_map));
    }

    // If the type was not found by ID, it's ok on a new cluster OR if the type was
    // deleted and created again. In both cases the type can be found by NAME.
    // By the moment all referenced sub-types must be available (already existing or re-created).
    CreateUDTypeRequestPB req;
    CreateUDTypeResponsePB resp;
    req.mutable_namespace_()->set_id(ns_data.new_namespace_id);
    req.mutable_namespace_()->set_database_type(ns_data.db_type);
    req.set_name(meta.name());
    for (int i = 0; i < meta.field_names_size(); ++i) {
      req.add_field_names(meta.field_names(i));

      QLTypePB* const param = meta.mutable_field_types(i);
      RETURN_NOT_OK(UpdateUDTypes(param, *type_map));
      req.add_field_types()->CopyFrom(*param);
    }

    const Status s = CreateUDType(&req, &resp, nullptr);

    if (s.ok()) {
      // Case 3: UDT was successfully re-created.
      udt_data.just_created = true;
    } else if (s.IsAlreadyPresent()) {
      // Case 2: UDT is found by name.
      LOG_WITH_FUNC(INFO) << "Using existing UD type '" << meta.name() << "': " << resp.id();
    } else {
      return s.CloneAndAppend("Failed to create UD type");
    }

    udt_data.new_type_id = resp.id();
    udt = VERIFY_RESULT(FindUDTypeById(udt_data.new_type_id));
  }

  // Check UDT field names & types.
  // Checking for all cases: found by ID, found by name (AlreadyPresent), re-created.
  bool correct = udt->field_names_size() == meta.field_names_size() &&
                 udt->field_types_size() == meta.field_types_size();
  if (correct) {
    for (int i = 0; i < udt->field_names_size(); ++i) {
      shared_ptr<QLType> found_type = QLType::FromQLTypePB(udt->field_types(i));
      shared_ptr<QLType> src_type = QLType::FromQLTypePB(meta.field_types(i));
      if (udt->field_names(i) != meta.field_names(i) || *found_type != *src_type) {
        correct = false;
        break;
      }
    }
  }

  if (!correct) {
    const string msg = Format(
        "UD type $0 id $1 was changed: {$2} expected {$3}",
        meta.name(), udt_data.new_type_id, udt->ToString(), meta.ShortDebugString());
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
  }

  return Status::OK();
}

Status CatalogManager::RecreateTable(const NamespaceId& new_namespace_id,
                                     const UDTypeMap& type_map,
                                     const ExternalTableSnapshotDataMap& table_map,
                                     ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;

  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(meta.name());
  req.set_table_type(meta.table_type());
  req.set_num_tablets(narrow_cast<int32_t>(table_data->num_tablets));
  for (const auto& p : table_data->partitions) {
    *req.add_partitions() = p;
  }
  req.mutable_namespace_()->set_id(new_namespace_id);
  *req.mutable_partition_schema() = meta.partition_schema();
  *req.mutable_replication_info() = meta.replication_info();

  SchemaPB* const schema = req.mutable_schema();
  *schema = meta.schema();
  // Recursively update ids in used user-defined types.
  for (int i = 0; i < schema->columns_size(); ++i) {
    QLTypePB* const pb_type = schema->mutable_columns(i)->mutable_type();
    RETURN_NOT_OK(UpdateUDTypes(pb_type, type_map));
  }

  // Setup Index info.
  if (table_data->is_index()) {
    TRACE("Looking up indexed table");
    // First of all try to attach to the new copy of the referenced table,
    // because the table restored from the snapshot is preferred.
    // For that try to map old indexed table ID into new table ID.
    ExternalTableSnapshotDataMap::const_iterator it = table_map.find(meta.indexed_table_id());
    const bool using_existing_table = (it == table_map.end());

    if (using_existing_table) {
      LOG_WITH_FUNC(INFO) << "Try to use old indexed table id " << meta.indexed_table_id();
      req.set_indexed_table_id(meta.indexed_table_id());
    } else {
      LOG_WITH_FUNC(INFO) << "Found new table id " << it->second.new_table_id
                          << " for old table id " << meta.indexed_table_id()
                          << " from the snapshot";
      req.set_indexed_table_id(it->second.new_table_id);
    }

    scoped_refptr<TableInfo> indexed_table;
    {
      SharedLock lock(mutex_);
      // Try to find the specified indexed table by id.
      indexed_table = FindPtrOrNull(*table_ids_map_, req.indexed_table_id());
    }

    if (indexed_table == nullptr) {
      const string msg = Format("Indexed table not found by id: $0", req.indexed_table_id());
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }

    LOG_WITH_FUNC(INFO) << "Found indexed table by id " << req.indexed_table_id();

    // Ensure the main table schema (including column ids) was not changed.
    if (!using_existing_table) {
      Schema new_indexed_schema, src_indexed_schema;
      RETURN_NOT_OK(indexed_table->GetSchema(&new_indexed_schema));
      RETURN_NOT_OK(SchemaFromPB(it->second.table_entry_pb.schema(), &src_indexed_schema));

      if (!new_indexed_schema.Equals(src_indexed_schema)) {
          const string msg = Format(
              "Recreated table has changes in schema: new schema={$0}, source schema={$1}",
              new_indexed_schema.ToString(), src_indexed_schema.ToString());
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }

      if (new_indexed_schema.column_ids() != src_indexed_schema.column_ids()) {
          const string msg = Format(
              "Recreated table has changes in column ids: new ids=$0, source ids=$1",
              ToString(new_indexed_schema.column_ids()), ToString(src_indexed_schema.column_ids()));
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
      }
    }

    req.set_is_local_index(meta.is_local_index());
    req.set_is_unique_index(meta.is_unique_index());
    req.set_skip_index_backfill(true);
    // Setup IndexInfoPB - self descriptor.
    IndexInfoPB* const index_info_pb = req.mutable_index_info();
    *index_info_pb = meta.index_info();
    index_info_pb->clear_table_id();
    index_info_pb->set_indexed_table_id(req.indexed_table_id());

    // Reset column ids.
    for (int i = 0; i < index_info_pb->columns_size(); ++i) {
      index_info_pb->mutable_columns(i)->clear_column_id();
    }
  }

  req.set_is_matview(meta.is_matview());

  if (meta.has_matview_pg_table_id()) {
    req.set_matview_pg_table_id(meta.matview_pg_table_id());
  }

  RETURN_NOT_OK(CreateTable(&req, &resp, /* RpcContext */nullptr));
  table_data->new_table_id = resp.table_id();
  LOG_WITH_FUNC(INFO) << "New table id " << table_data->new_table_id << " for "
                      << table_data->old_table_id;
  return Status::OK();
}

Status CatalogManager::RepartitionTable(const scoped_refptr<TableInfo> table,
                                        const ExternalTableSnapshotData* table_data) {
  DCHECK_EQ(table->id(), table_data->new_table_id);
  if (table->GetTableType() != PGSQL_TABLE_TYPE) {
    return STATUS_FORMAT(InvalidArgument,
                         "Cannot repartition non-YSQL table: got $0",
                         TableType_Name(table->GetTableType()));
  }
  LOG_WITH_FUNC(INFO) << "Repartition table " << table->id()
                      << " using external snapshot table " << table_data->old_table_id;

  // Get partitions from external snapshot.
  size_t i = 0;
  vector<Partition> partitions(table_data->partitions.size());
  for (const auto& partition_pb : table_data->partitions) {
    Partition::FromPB(partition_pb, &partitions[i++]);
  }
  VLOG_WITH_FUNC(3) << "Got " << partitions.size()
                    << " partitions from external snapshot for table " << table->id();

  // Change TableInfo to point to the new tablets.
  string deletion_msg;
  vector<scoped_refptr<TabletInfo>> new_tablets;
  vector<scoped_refptr<TabletInfo>> old_tablets;
  {
    // Acquire the TableInfo pb write lock. Although it is not required for some of the individual
    // steps, we want to hold it through so that we guarantee the state does not change during the
    // whole process. Consequently, we hold it through some steps that require mutex_, but since
    // taking mutex_ after TableInfo pb lock is prohibited for deadlock reasons, acquire mutex_
    // first, then release it when it is no longer needed, still holding table pb lock.
    TableInfo::WriteLock table_lock;
    {
      LockGuard lock(mutex_);
      TRACE("Acquired catalog manager lock");

      // Make sure the table is in RUNNING state.
      // This by itself doesn't need a pb write lock: just a read lock. However, we want to prevent
      // other writers from entering from this point forward, so take the write lock now.
      table_lock = table->LockForWrite();
      if (table->old_pb().state() != SysTablesEntryPB::RUNNING) {
        return STATUS_FORMAT(IllegalState,
                             "Table $0 not running: $1",
                             table->ToString(),
                             SysTablesEntryPB_State_Name(table->old_pb().state()));
      }
      // Make sure the table's tablets can be deleted.
      RETURN_NOT_OK_PREPEND(CheckIfForbiddenToDeleteTabletOf(table),
                            Format("Cannot repartition table $0", table->id()));

      // Create and mark new tablets for creation.

      // Use partitions from external snapshot to create new tablets in state PREPARING. The tablets
      // will start CREATING once they are committed in memory.
      for (const auto& partition : partitions) {
        PartitionPB partition_pb;
        partition.ToPB(&partition_pb);
        new_tablets.push_back(CreateTabletInfo(table.get(), partition_pb));
      }

      // Add tablets to catalog manager tablet_map_. This should be safe to do after creating
      // tablets since we're still holding mutex_.
      auto tablet_map_checkout = tablet_map_.CheckOut();
      for (auto& new_tablet : new_tablets) {
        InsertOrDie(tablet_map_checkout.get_ptr(), new_tablet->tablet_id(), new_tablet);
      }
      VLOG_WITH_FUNC(3) << "Prepared creation of " << new_tablets.size()
                        << " new tablets for table " << table->id();

      // mutex_ is no longer needed, so release by going out of scope.
    }
    // The table pb write lock is still held, ensuring that the table state does not change. Later
    // steps, like GetTablets or AddTablets, will acquire/release the TableInfo lock_, but it's
    // probably fine that they are released between the steps since the table pb write lock is held
    // throughout. In other words, there should be no risk that TableInfo tablets_ changes between
    // GetTablets and RemoveTablets.

    // Abort tablet mutations in case of early returns.
    ScopedInfoCommitter<TabletInfo> unlocker_new(&new_tablets);

    // Mark old tablets for deletion.
    old_tablets = table->GetTablets(IncludeInactive::kTrue);
    // Sort so that locking can be done in a deterministic order.
    std::sort(old_tablets.begin(), old_tablets.end(), [](const auto& lhs, const auto& rhs) {
      return lhs->tablet_id() < rhs->tablet_id();
    });
    deletion_msg = Format("Old tablets of table $0 deleted at $1",
                          table->id(), LocalTimeAsString());
    for (auto& old_tablet : old_tablets) {
      old_tablet->mutable_metadata()->StartMutation();
      old_tablet->mutable_metadata()->mutable_dirty()->set_state(
          SysTabletsEntryPB::DELETED, deletion_msg);
    }
    VLOG_WITH_FUNC(3) << "Prepared deletion of " << old_tablets.size() << " old tablets for table "
                      << table->id();

    // Abort tablet mutations in case of early returns.
    ScopedInfoCommitter<TabletInfo> unlocker_old(&old_tablets);

    // Change table's partition schema to the external snapshot's.
    auto& table_pb = table_lock.mutable_data()->pb;
    table_pb.mutable_partition_schema()->CopyFrom(
        table_data->table_entry_pb.partition_schema());
    table_pb.set_partition_list_version(table_pb.partition_list_version() + 1);

    // Remove old tablets from TableInfo.
    table->RemoveTablets(old_tablets);
    // Add new tablets to TableInfo. This must be done after removing tablets because
    // TableInfo::partitions_ has key PartitionKey, which old and new tablets may conflict on.
    table->AddTablets(new_tablets);

    // Commit table and tablets to disk.
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table, new_tablets, old_tablets));
    VLOG_WITH_FUNC(2) << "Committed to disk: table " << table->id() << " repartition from "
                      << old_tablets.size() << " tablets to " << new_tablets.size() << " tablets";

    // Commit to memory. Commit new tablets (addition) first since that doesn't break anything.
    // Commit table next since new tablets are already committed and ready to be referenced. Commit
    // old tablets (deletion) last since the table is not referencing them anymore.
    unlocker_new.Commit();
    table_lock.Commit();
    unlocker_old.Commit();
    VLOG_WITH_FUNC(1) << "Committed to memory: table " << table->id() << " repartition from "
                      << old_tablets.size() << " tablets to " << new_tablets.size() << " tablets";
  }

  // Finally, now that everything is committed, send the delete tablet requests.
  for (auto& old_tablet : old_tablets) {
    DeleteTabletReplicas(old_tablet.get(), deletion_msg, HideOnly::kFalse, KeepData::kFalse);
  }
  VLOG_WITH_FUNC(2) << "Sent delete tablet requests for " << old_tablets.size() << " old tablets"
                    << " of table " << table->id();
  // The create tablet requests should be handled by bg tasks which find the PREPARING tablets after
  // commit.

  return Status::OK();
}

// Helper function for ImportTableEntry.
//
// Given an internal table and an external table snapshot, do some checks to determine if we should
// move forward with using this internal table for import.
//
// table: internal table's info
// snapshot_data: external table's snapshot data
Result<bool> CatalogManager::CheckTableForImport(scoped_refptr<TableInfo> table,
                                                 ExternalTableSnapshotData* snapshot_data) {
  auto table_lock = table->LockForRead();

  // Check if table is live.
  if (!table_lock->visible_to_client()) {
    VLOG_WITH_FUNC(2) << "Table not visible to client: " << table->ToString();
    return false;
  }
  // Check if table names match.
  const string& external_table_name = snapshot_data->table_entry_pb.name();
  if (table_lock->name() != external_table_name) {
    VLOG_WITH_FUNC(2) << "Table names do not match: "
                      << table_lock->name() << " vs " << external_table_name
                      << " for " << table->ToString();
    return false;
  }
  // Check index vs table.
  if (snapshot_data->is_index() ? table->indexed_table_id().empty()
                                : !table->indexed_table_id().empty()) {
    VLOG_WITH_FUNC(2) << "External snapshot table is " << (snapshot_data->is_index() ? "" : "not ")
                      << "index but internal table is the opposite: " << table->ToString();
    return false;
  }
  // Check if table schemas match (if present in snapshot).
  if (!snapshot_data->pg_schema_name.empty()) {
    if (table->GetTableType() != PGSQL_TABLE_TYPE) {
      LOG_WITH_FUNC(DFATAL) << "ExternalTableSnapshotData.pg_schema_name set when table type is not"
          << " PGSQL: schema name: " << snapshot_data->pg_schema_name
          << ", table type: " << TableType_Name(table->GetTableType());
      // If not a debug build, ignore pg_schema_name.
    } else {
      const string internal_schema_name = VERIFY_RESULT(GetPgSchemaName(table));
      const string& external_schema_name = snapshot_data->pg_schema_name;
      if (internal_schema_name != external_schema_name) {
        LOG_WITH_FUNC(INFO) << "Schema names do not match: "
                            << internal_schema_name << " vs " << external_schema_name
                            << " for " << table->ToString();
        return false;
      }
    }
  }

  return true;
}

Status CatalogManager::ImportTableEntry(const NamespaceMap& namespace_map,
                                        const UDTypeMap& type_map,
                                        const ExternalTableSnapshotDataMap& table_map,
                                        ExternalTableSnapshotData* table_data) {
  const SysTablesEntryPB& meta = DCHECK_NOTNULL(table_data)->table_entry_pb;
  bool is_parent_colocated_table = false;

  table_data->old_namespace_id = meta.namespace_id();
  LOG_IF(DFATAL, table_data->old_namespace_id.empty()) << "No namespace id";

  auto ns_it = namespace_map.find(table_data->old_namespace_id);
  LOG_IF(DFATAL, ns_it == namespace_map.end())
      << "Namespace not found: " << table_data->old_namespace_id;
  const NamespaceId new_namespace_id = ns_it->second.new_namespace_id;
  LOG_IF(DFATAL, new_namespace_id.empty()) << "No namespace id";

  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(meta.schema(), &schema));
  const vector<ColumnId>& column_ids = schema.column_ids();
  scoped_refptr<TableInfo> table;

  // First, check if namespace id and table id match. If, in addition, other properties match, we
  // found the destination table.
  if (new_namespace_id == table_data->old_namespace_id) {
    TRACE("Looking up table");
    {
      SharedLock lock(mutex_);
      table = FindPtrOrNull(*table_ids_map_, table_data->old_table_id);
    }

    if (table != nullptr) {
      VLOG_WITH_PREFIX(3) << "Begin first search";
      // At this point, namespace id and table id match. Check other properties, like whether the
      // table is active and whether table name matches.
      SharedLock lock(mutex_);
      if (VERIFY_RESULT(CheckTableForImport(table, table_data))) {
        LOG_WITH_FUNC(INFO) << "Found existing table: '" << table->ToString() << "'";
        if (meta.colocated() && IsColocationParentTableId(table_data->old_table_id)) {
          // Parent colocated tables don't have partition info, so make sure to mark them.
          is_parent_colocated_table = true;
        }
      } else {
        // A property did not match, so this search by ids failed.
        auto table_lock = table->LockForRead();
        LOG_WITH_FUNC(WARNING) << "Existing table " << table->ToString() << " not suitable: "
                               << table_lock->pb.ShortDebugString()
                               << ", name: " << table->name() << " vs " << meta.name();
        table.reset();
      }
    }
  }

  // Second, if we still didn't find a match...
  if (table == nullptr) {
    VLOG_WITH_PREFIX(3) << "Begin second search";
    switch (meta.table_type()) {
      case TableType::YQL_TABLE_TYPE: FALLTHROUGH_INTENDED;
      case TableType::REDIS_TABLE_TYPE: {
        // For YCQL and YEDIS, simply create the missing table.
        RETURN_NOT_OK(RecreateTable(new_namespace_id, type_map, table_map, table_data));
        break;
      }
      case TableType::PGSQL_TABLE_TYPE: {
        // For YSQL, the table must be created via external call. Therefore, continue the search for
        // the table, this time checking for name matches rather than id matches.

        // TODO(alex): Handle tablegroups in #11632
        if (meta.colocated() && IsColocatedDbParentTableId(table_data->old_table_id)) {
          // For the parent colocated table we need to generate the new_table_id ourselves
          // since the names will not match.
          // For normal colocated tables, we are still able to follow the normal table flow, so no
          // need to generate the new_table_id ourselves.
          table_data->new_table_id = GetColocatedDbParentTableId(new_namespace_id);
          is_parent_colocated_table = true;
        } else {
          if (!table_data->new_table_id.empty()) {
            const string msg = Format(
                "$0 expected empty new table id but $1 found", __func__, table_data->new_table_id);
            LOG_WITH_FUNC(WARNING) << msg;
            return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
          }
          SharedLock lock(mutex_);

          for (const auto& entry : *table_ids_map_) {
            table = entry.second;

            if (new_namespace_id != table->namespace_id()) {
              VLOG_WITH_FUNC(3) << "Namespace ids do not match: "
                                << table->namespace_id() << " vs " << new_namespace_id
                                << " for " << table->ToString();
              continue;
            }
            if (!VERIFY_RESULT(CheckTableForImport(table, table_data))) {
              // Some other check failed.
              continue;
            }
            // Also check if table is user-created.
            if (!IsUserCreatedTableUnlocked(*table)) {
              VLOG_WITH_FUNC(2) << "Table not user created: " << table->ToString();
              continue;
            }

            // Found the new YSQL table by name.
            if (table_data->new_table_id.empty()) {
              LOG_WITH_FUNC(INFO) << "Found existing table " << entry.first << " for "
                                  << new_namespace_id << "/" << meta.name() << " (old table "
                                  << table_data->old_table_id << ") with schema "
                                  << table_data->pg_schema_name;
              table_data->new_table_id = entry.first;
            } else if (table_data->new_table_id != entry.first) {
              const string msg = Format(
                  "Found 2 YSQL tables with the same name: $0 - $1, $2",
                  meta.name(), table_data->new_table_id, entry.first);
              LOG_WITH_FUNC(WARNING) << msg;
              return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
            }
          }

          if (table_data->new_table_id.empty()) {
            const string msg = Format("YSQL table not found: $0", meta.name());
            LOG_WITH_FUNC(WARNING) << msg;
            table_data->table_meta = std::nullopt;
            return STATUS(InvalidArgument, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
          }
        }
        break;
      }
      case TableType::TRANSACTION_STATUS_TABLE_TYPE: {
        return STATUS(
            InvalidArgument,
            Format("Unexpected table type: $0", TableType_Name(meta.table_type())),
            MasterError(MasterErrorPB::INVALID_TABLE_TYPE));
      }
    }
  } else {
    table_data->new_table_id = table_data->old_table_id;
    LOG_WITH_FUNC(INFO) << "Use existing table " << table_data->new_table_id;
  }

  // The destination table should be found or created by now.
  TRACE("Looking up new table");
  {
    SharedLock lock(mutex_);
    table = FindPtrOrNull(*table_ids_map_, table_data->new_table_id);
  }
  if (table == nullptr) {
    const string msg = Format("Created table not found: $0", table_data->new_table_id);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(InternalError, msg, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  // Don't do schema validation/column updates on the parent colocated table.
  // However, still do the validation for regular colocated tables.
  if (!is_parent_colocated_table) {
    Schema persisted_schema;
    size_t new_num_tablets = 0;
    {
      TRACE("Locking table");
      auto table_lock = table->LockForRead();
      RETURN_NOT_OK(table->GetSchema(&persisted_schema));
      new_num_tablets = table->NumPartitions();
    }

    // Ignore 'nullable' attribute - due to difference in implementation
    // of PgCreateTable::AddColumn() and PgAlterTable::AddColumn().
    struct CompareColumnsExceptNullable {
      bool operator ()(const ColumnSchema& a, const ColumnSchema& b) {
        return ColumnSchema::CompHashKey(a, b) && ColumnSchema::CompSortingType(a, b) &&
            ColumnSchema::CompTypeInfo(a, b) && ColumnSchema::CompName(a, b);
      }
    } comparator;
    // Schema::Equals() compares only column names & types. It does not compare the column ids.
    if (!persisted_schema.Equals(schema, comparator)
        || persisted_schema.column_ids().size() != column_ids.size()) {
      const string msg = Format(
          "Invalid created $0 table '$1' in namespace id $2: schema={$3}, expected={$4}",
          TableType_Name(meta.table_type()), meta.name(), new_namespace_id,
          persisted_schema, schema);
      LOG_WITH_FUNC(WARNING) << msg;
      return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
    }

    if (table_data->num_tablets > 0) {
      if (meta.table_type() == TableType::PGSQL_TABLE_TYPE) {
        bool partitions_match = true;
        if (new_num_tablets != table_data->num_tablets) {
          partitions_match = false;
        } else {
          // Check if partition boundaries match.  Only check the starts; assume the ends are fine.
          size_t i = 0;
          vector<PartitionKey> partition_starts(table_data->num_tablets);
          for (const auto& partition_pb : table_data->partitions) {
            partition_starts[i] = partition_pb.partition_key_start();
            LOG_IF(DFATAL, (i == 0) ? partition_starts[i] != ""
                                    : partition_starts[i] <= partition_starts[i-1])
                << "Wrong partition key start: " << b2a_hex(partition_starts[i]);
            i++;
          }
          if (!table->HasPartitions(partition_starts)) {
            LOG_WITH_FUNC(INFO) << "Partition boundaries mismatch for table " << table->id();
            partitions_match = false;
          }
        }

        if (!partitions_match) {
          RETURN_NOT_OK(RepartitionTable(table, table_data));
        }
      } else { // not PGSQL_TABLE_TYPE
        if (new_num_tablets != table_data->num_tablets) {
          const string msg = Format(
              "Wrong number of tablets in created $0 table '$1' in namespace id $2:"
              " $3 (expected $4)",
              TableType_Name(meta.table_type()), meta.name(), new_namespace_id,
              new_num_tablets, table_data->num_tablets);
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }
      }
    }

    // Table schema update depending on different conditions.
    bool notify_ts_for_schema_change = false;

    // Update the table column ids if it's not equal to the stored ids.
    if (persisted_schema.column_ids() != column_ids) {
      if (meta.table_type() != TableType::PGSQL_TABLE_TYPE) {
        LOG_WITH_FUNC(WARNING) << "Unexpected wrong column ids in "
                               << TableType_Name(meta.table_type()) << " table '" << meta.name()
                               << "' in namespace id " << new_namespace_id;
      }

      LOG_WITH_FUNC(INFO) << "Restoring column ids in " << TableType_Name(meta.table_type())
                          << " table '" << meta.name() << "' in namespace id "
                          << new_namespace_id;
      auto l = table->LockForWrite();
      size_t col_idx = 0;
      for (auto& column : *l.mutable_data()->pb.mutable_schema()->mutable_columns()) {
        // Expecting here correct schema (columns - order, names, types), but with only wrong
        // column ids. Checking correct column order and column names below.
        if (column.name() != schema.column(col_idx).name()) {
          const string msg = Format(
              "Unexpected column name for index=$0: name=$1, expected name=$2",
              col_idx, schema.column(col_idx).name(), column.name());
          LOG_WITH_FUNC(WARNING) << msg;
          return STATUS(InternalError, msg, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
        }
        // Copy the column id from imported (original) schema.
        column.set_id(column_ids[col_idx++]);
      }

      l.mutable_data()->pb.set_next_column_id(schema.max_col_id() + 1);
      l.mutable_data()->pb.set_version(l->pb.version() + 1);
      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Restore partition key version.
    if (persisted_schema.table_properties().partitioning_version() !=
        schema.table_properties().partitioning_version()) {
      auto l = table->LockForWrite();
      auto table_props = l.mutable_data()->pb.mutable_schema()->mutable_table_properties();
      table_props->set_partitioning_version(schema.table_properties().partitioning_version());

      l.mutable_data()->pb.set_next_column_id(schema.max_col_id() + 1);
      l.mutable_data()->pb.set_version(l->pb.version() + 1);
      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Bump up the schema version to the version of the snapshot if it is less.
    if (meta.version() > table->LockForRead()->pb.version()) {
      auto l = table->LockForWrite();
      l.mutable_data()->pb.set_version(meta.version());
      RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), table));
      l.Commit();
      notify_ts_for_schema_change = true;
    }

    // Update the new table schema in tablets.
    if (notify_ts_for_schema_change) {
      RETURN_NOT_OK(SendAlterTableRequest(table));
    }
  }

  // Set the type of the table in the response pb (default is TABLE so only set if colocated).
  if (meta.colocated()) {
    if (is_parent_colocated_table) {
      table_data->table_meta->set_table_type(
          ImportSnapshotMetaResponsePB_TableType_PARENT_COLOCATED_TABLE);
    } else {
      table_data->table_meta->set_table_type(
          ImportSnapshotMetaResponsePB_TableType_COLOCATED_TABLE);
    }
  }

  TabletInfos new_tablets;
  {
    TRACE("Locking table");
    auto table_lock = table->LockForRead();
    new_tablets = table->GetTablets();
  }

  for (const scoped_refptr<TabletInfo>& tablet : new_tablets) {
    auto tablet_lock = tablet->LockForRead();
    const PartitionPB& partition_pb = tablet->metadata().state().pb.partition();
    const ExternalTableSnapshotData::PartitionKeys key(
        partition_pb.partition_key_start(), partition_pb.partition_key_end());
    table_data->new_tablets_map[key] = tablet->id();
  }

  IdPairPB* const namespace_ids = table_data->table_meta->mutable_namespace_ids();
  namespace_ids->set_new_id(new_namespace_id);
  namespace_ids->set_old_id(table_data->old_namespace_id);

  IdPairPB* const table_ids = table_data->table_meta->mutable_table_ids();
  table_ids->set_new_id(table_data->new_table_id);
  table_ids->set_old_id(table_data->old_table_id);

  // Recursively collect ids for used user-defined types.
  unordered_set<UDTypeId> type_ids;
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    for (const auto &udt_id : schema.column(i).type()->GetUserDefinedTypeIds()) {
      type_ids.insert(udt_id);
    }
  }

  for (const UDTypeId& udt_id : type_ids) {
    auto type_it = type_map.find(udt_id);
    if (type_it == type_map.end()) {
        return STATUS(InternalError, "UDType was not imported",
            udt_id, MasterError(MasterErrorPB::SNAPSHOT_FAILED));
    }

    IdPairPB* const udt_ids = table_data->table_meta->add_ud_types_ids();
    udt_ids->set_new_id(type_it->second.new_type_id);
    udt_ids->set_old_id(udt_id);
  }

  return Status::OK();
}

Status CatalogManager::PreprocessTabletEntry(const SysRowEntry& entry,
                                             ExternalTableSnapshotDataMap* table_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::TABLET)
      << "Unexpected entry type: " << entry.type();

  SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));

  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  if (meta.colocated()) {
    table_data.num_tablets = 1;
  } else {
    ++table_data.num_tablets;
  }
  if (meta.has_partition()) {
    table_data.partitions.push_back(meta.partition());
  }
  return Status::OK();
}

Status CatalogManager::ImportTabletEntry(const SysRowEntry& entry,
                                         ExternalTableSnapshotDataMap* table_map) {
  LOG_IF(DFATAL, entry.type() != SysRowEntryType::TABLET)
      << "Unexpected entry type: " << entry.type();

  SysTabletsEntryPB meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));

  LOG_IF(DFATAL, table_map->find(meta.table_id()) == table_map->end())
      << "Table not found: " << meta.table_id();
  if (table_map->find(meta.table_id()) == table_map->end()) {
    return STATUS_FORMAT(
        InvalidArgument, "Cannot find table with id $0 hosted on tablet $1", meta.table_id(),
        entry.id());
  }
  ExternalTableSnapshotData& table_data = (*table_map)[meta.table_id()];
  if (!table_data.table_meta) {
    if (table_data.is_index()) {
      // The metadata for this index was not initialized in ImportTableEntry. We assume it was
      // missing from the ysql_dump and is an invalid YSQL index. Ignore.
      return Status::OK();
    }
    auto msg = Format("Missing metadata for table corresponding to snapshot table $0.$1, id $2",
        table_data.table_entry_pb.namespace_name(), table_data.table_entry_pb.name(),
        table_data.old_table_id);
    DCHECK(false) << msg;
    return STATUS(IllegalState, msg);
  }

  if (meta.colocated() && table_data.table_meta->tablets_ids_size() >= 1) {
    LOG_WITH_FUNC(INFO) << "Already processed this colocated tablet: " << entry.id();
    return Status::OK();
  }

  // Update tablets IDs map.
  if (table_data.new_table_id == table_data.old_table_id) {
    TRACE("Looking up tablet");
    SharedLock lock(mutex_);
    scoped_refptr<TabletInfo> tablet = FindPtrOrNull(*tablet_map_, entry.id());

    if (tablet != nullptr) {
      IdPairPB* const pair = table_data.table_meta->add_tablets_ids();
      pair->set_old_id(entry.id());
      pair->set_new_id(entry.id());
      return Status::OK();
    }
  }

  const PartitionPB& partition_pb = meta.partition();
  const ExternalTableSnapshotData::PartitionKeys key(
      partition_pb.partition_key_start(), partition_pb.partition_key_end());
  const ExternalTableSnapshotData::PartitionToIdMap::const_iterator it =
      table_data.new_tablets_map.find(key);

  if (it == table_data.new_tablets_map.end()) {
    const string msg = Format(
        "For new table $0 (old table $1, expecting $2 tablets) not found new tablet with "
        "expected [$3]", table_data.new_table_id, table_data.old_table_id,
        table_data.num_tablets, partition_pb);
    LOG_WITH_FUNC(WARNING) << msg;
    return STATUS(NotFound, msg, MasterError(MasterErrorPB::INTERNAL_ERROR));
  }

  IdPairPB* const pair = table_data.table_meta->add_tablets_ids();
  pair->set_old_id(entry.id());
  pair->set_new_id(it->second);
  return Status::OK();
}

const Schema& CatalogManager::schema() {
  return sys_catalog()->schema();
}

const docdb::DocReadContext& CatalogManager::doc_read_context() {
  return sys_catalog()->doc_read_context();
}

TabletInfos CatalogManager::GetTabletInfos(const std::vector<TabletId>& ids) {
  TabletInfos result;
  result.reserve(ids.size());
  SharedLock lock(mutex_);
  for (const auto& id : ids) {
    auto it = tablet_map_->find(id);
    result.push_back(it != tablet_map_->end() ? it->second : nullptr);
  }
  return result;
}

Result<SchemaVersion> CatalogManager::GetTableSchemaVersion(const TableId& table_id) {
  auto table = VERIFY_RESULT(FindTableById(table_id));
  auto lock = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(lock));
  return lock->pb.version();
}

Result<std::map<std::string, KeyRange>> CatalogManager::GetTableKeyRanges(const TableId& table_id) {
  auto table = VERIFY_RESULT(FindTableById(table_id));
  auto lock = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(lock));

  auto tablets = table->GetTablets();

  std::map<std::string, KeyRange> result;
  for (const scoped_refptr<TabletInfo>& tablet : tablets) {
    auto tablet_lock = tablet->LockForRead();
    const auto& partition = tablet_lock->pb.partition();
    result[tablet->tablet_id()].start_key = partition.partition_key_start();
    result[tablet->tablet_id()].end_key = partition.partition_key_end();
  }

  return result;
}

AsyncTabletSnapshotOpPtr CatalogManager::CreateAsyncTabletSnapshotOp(
    const TabletInfoPtr& tablet, const std::string& snapshot_id,
    tserver::TabletSnapshotOpRequestPB::Operation operation,
    TabletSnapshotOperationCallback callback) {
  auto result = std::make_shared<AsyncTabletSnapshotOp>(
      master_, AsyncTaskPool(), tablet, snapshot_id, operation);
  result->SetCallback(std::move(callback));
  tablet->table()->AddTask(result);
  return result;
}

void CatalogManager::ScheduleTabletSnapshotOp(const AsyncTabletSnapshotOpPtr& task) {
  WARN_NOT_OK(ScheduleTask(task), "Failed to send create snapshot request");
}

Status CatalogManager::RestoreSysCatalogCommon(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet,
    RestoreSysCatalogState* state, docdb::DocWriteBatch* write_batch,
    docdb::KeyValuePairPB* restore_kv) {
  // Restore master snapshot and load it to RocksDB.
  auto dir = VERIFY_RESULT(tablet->snapshots().RestoreToTemporary(
      restoration->snapshot_id, restoration->restore_at));
  rocksdb::Options rocksdb_options;
  std::string log_prefix = LogPrefix();
  // Remove ": " to patch suffix.
  log_prefix.erase(log_prefix.size() - 2);
  tablet->InitRocksDBOptions(&rocksdb_options, log_prefix + " [TMP]: ");
  auto db = VERIFY_RESULT(rocksdb::DB::Open(rocksdb_options, dir));

  auto doc_db = docdb::DocDB::FromRegularUnbounded(db.get());

  // Load objects to restore and determine obsolete objects.
  RETURN_NOT_OK(state->LoadRestoringObjects(doc_read_context(), doc_db));
  // Load existing objects from RocksDB because on followers they are NOT present in loaded sys
  // catalog state.
  RETURN_NOT_OK(
      state->LoadExistingObjects(doc_read_context(), tablet->doc_db()));
  RETURN_NOT_OK(state->Process());

  // Restore the pg_catalog tables.
  if (FLAGS_enable_ysql && state->IsYsqlRestoration()) {
    // Restore sequences_data table.
    RETURN_NOT_OK(state->PatchSequencesDataObjects());

    RETURN_NOT_OK(state->ProcessPgCatalogRestores(
        doc_db, tablet->doc_db(),
        write_batch, doc_read_context(), tablet->metadata()));
  }

  // Crash for tests.
  MAYBE_FAULT(FLAGS_TEST_crash_during_sys_catalog_restoration);

  // Restore the other tables.
  RETURN_NOT_OK(state->PrepareWriteBatch(schema(), write_batch, master_->clock()->Now()));

  // Updates the restoration state to indicate that sys catalog phase has completed.
  // Also, initializes the master side perceived list of tables/tablets/namespaces
  // that need to be restored for verification post sys catalog load.
  // Also, re-initializes the tablet list since it could have been changed from the
  // time of snapshot creation.
  // Also, generates the restoration state entry.
  // This is to persist the restoration so that on restarts the RESTORE_ON_TABLET
  // rpcs can be retried.
  *restore_kv = VERIFY_RESULT(
      snapshot_coordinator_.UpdateRestorationAndGetWritePair(restoration));

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalogSlowPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << restoration->restoration_id;

  bool restore_successful = false;
  // If sys catalog restoration fails then unblock other RPCs.
  auto scope_exit = ScopeExit([this, &restore_successful] {
    if (!restore_successful) {
      LOG(INFO) << "PITR: Accepting RPCs to the master leader";
      std::lock_guard l(state_lock_);
      is_catalog_loaded_ = true;
    }
  });

  RestoreSysCatalogState state(restoration);
  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      restoration, tablet, &state, &write_batch, &restore_kv));

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(
      &write_batch, restore_kv, restoration->write_time, restoration->op_id, tablet);

  LOG_WITH_PREFIX(INFO) << "PITR: In leader term " << LeaderTerm()
                        << ", wrote " << write_batch.size() << " entries to rocksdb";

  if (LeaderTerm() >= 0) {
    RETURN_NOT_OK(ElectedAsLeaderCb());
  }

  restore_successful = true;

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalogFastPitr(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << restoration->restoration_id;
  bool restore_successful = false;

  // If sys catalog restoration fails then unblock other RPCs.
  auto scope_exit = ScopeExit([this, &restore_successful] {
    if (!restore_successful) {
      LOG(INFO) << "PITR: Accepting RPCs to the master leader";
      std::lock_guard<simple_spinlock> l(state_lock_);
      is_catalog_loaded_ = true;
    }
  });

  docdb::DocWriteBatch write_batch(
      tablet->doc_db(), docdb::InitMarkerBehavior::kOptional);
  RestoreSysCatalogState state(restoration);
  docdb::KeyValuePairPB restore_kv;

  RETURN_NOT_OK(RestoreSysCatalogCommon(
      restoration, tablet, &state, &write_batch, &restore_kv));

  if (state.IsYsqlRestoration()) {
    // Set Hybrid Time filter for pg catalog tables.
    tablet::TabletScopedRWOperationPauses op_pauses = tablet->StartShutdownRocksDBs(
        tablet::DisableFlushOnShutdown::kFalse);

    std::lock_guard<std::mutex> lock(tablet->create_checkpoint_lock_);

    tablet->CompleteShutdownRocksDBs(op_pauses);

    rocksdb::Options rocksdb_opts;
    tablet->InitRocksDBOptions(&rocksdb_opts, tablet->LogPrefix());
    docdb::RocksDBPatcher patcher(tablet->metadata()->rocksdb_dir(), rocksdb_opts);
    RETURN_NOT_OK(patcher.Load());
    RETURN_NOT_OK(patcher.SetHybridTimeFilter(restoration->db_oid, restoration->restore_at));

    RETURN_NOT_OK(tablet->OpenKeyValueTablet());
    RETURN_NOT_OK(tablet->EnableCompactions(&op_pauses.non_abortable));

    // Ensure that op_pauses stays in scope throughout this function.
    for (auto* op_pause : op_pauses.AsArray()) {
      DFATAL_OR_RETURN_NOT_OK(op_pause->status());
    }
  }

  // Apply write batch to RocksDB.
  state.WriteToRocksDB(
      &write_batch, restore_kv, restoration->write_time, restoration->op_id, tablet);

  LOG_WITH_PREFIX(INFO) << "PITR: In leader term " << LeaderTerm() << ", wrote "
                        << write_batch.size() << " entries to rocksdb";

  if (LeaderTerm() >= 0) {
    RETURN_NOT_OK(ElectedAsLeaderCb());
  }

  restore_successful = true;

  return Status::OK();
}

Status CatalogManager::RestoreSysCatalog(
    SnapshotScheduleRestoration* restoration, tablet::Tablet* tablet, Status* complete_status) {
  Status s;
  if (GetAtomicFlag(&FLAGS_enable_fast_pitr)) {
    s = RestoreSysCatalogFastPitr(restoration, tablet);
  } else {
    s = RestoreSysCatalogSlowPitr(restoration, tablet);
  }
  // As RestoreSysCatalog is synchronous on Master it should be ok to set the completion
  // status in case of validation failures so that it gets propagated back to the client before
  // doing any write operations.
  if (s.IsNotSupported()) {
    *complete_status = s;
    return Status::OK();
  }
  return s;
}

Status CatalogManager::VerifyRestoredObjects(
    const std::unordered_map<std::string, SysRowEntryType>& objects,
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& tables) {
  std::unordered_map<std::string, SysRowEntryType> objects_to_restore = objects;
  auto entries = VERIFY_RESULT(CollectEntriesForSnapshot(tables));
  // auto objects_to_restore = restoration.non_system_objects_to_restore;
  VLOG_WITH_PREFIX(1) << "Objects to restore: " << AsString(objects_to_restore);
  // There could be duplicate entries collected, for instance in the case of
  // colocated tables.
  std::unordered_set<std::string> unique_entries;
  for (const auto& entry : entries.entries()) {
    if (!unique_entries.insert(entry.id()).second) {
      continue;
    }
    VLOG_WITH_PREFIX(1)
        << "Alive " << SysRowEntryType_Name(entry.type()) << ": " << entry.id();
    auto it = objects_to_restore.find(entry.id());
    if (it == objects_to_restore.end()) {
      return STATUS_FORMAT(IllegalState, "Object $0/$1 present, but should not be restored",
                           SysRowEntryType_Name(entry.type()), entry.id());
    }
    if (it->second != entry.type()) {
      return STATUS_FORMAT(
          IllegalState, "Restored object $0 has wrong type $1, while $2 expected",
          entry.id(), SysRowEntryType_Name(entry.type()), SysRowEntryType_Name(it->second));
    }
    objects_to_restore.erase(it);
  }
  for (const auto& id_and_type : objects_to_restore) {
    return STATUS_FORMAT(
        IllegalState, "Expected to restore $0/$1, but it is not present after restoration",
        SysRowEntryType_Name(id_and_type.second), id_and_type.first);
  }
  return Status::OK();
}

void CatalogManager::CleanupHiddenObjects(const ScheduleMinRestoreTime& schedule_min_restore_time) {
  VLOG_WITH_PREFIX_AND_FUNC(4) << AsString(schedule_min_restore_time);

  std::vector<TabletInfoPtr> hidden_tablets;
  std::vector<TableInfoPtr> tables;
  {
    SharedLock lock(mutex_);
    hidden_tablets = hidden_tablets_;
    tables.reserve(table_ids_map_->size());
    for (const auto& p : *table_ids_map_) {
      if (!p.second->is_system()) {
        tables.push_back(p.second);
      }
    }
  }
  CleanupHiddenTablets(hidden_tablets, schedule_min_restore_time);
  CleanupHiddenTables(std::move(tables), schedule_min_restore_time);
}

void CatalogManager::CleanupHiddenTablets(
    const std::vector<TabletInfoPtr>& hidden_tablets,
    const ScheduleMinRestoreTime& schedule_min_restore_time) {
  if (hidden_tablets.empty()) {
    return;
  }
  std::vector<TabletInfoPtr> tablets_to_delete;
  std::vector<TabletInfoPtr> tablets_to_remove_from_hidden;

  for (const auto& tablet : hidden_tablets) {
    auto lock = tablet->LockForRead();
    if (!lock->ListedAsHidden()) {
      tablets_to_remove_from_hidden.push_back(tablet);
      continue;
    }
    auto hide_hybrid_time = HybridTime::FromPB(lock->pb.hide_hybrid_time());
    bool cleanup = true;
    for (const auto& schedule_id_str : lock->pb.retained_by_snapshot_schedules()) {
      auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
      auto it = schedule_min_restore_time.find(schedule_id);
      // If schedule is not present in schedule_min_restore_time then it means that schedule
      // was deleted, so it should not retain the tablet.
      if (it != schedule_min_restore_time.end() && it->second <= hide_hybrid_time) {
        VLOG_WITH_PREFIX(1)
            << "Retaining tablet: " << tablet->tablet_id() << ", hide hybrid time: "
            << hide_hybrid_time << ", because of schedule: " << schedule_id
            << ", min restore time: " << it->second;
        cleanup = false;
        break;
      }
    }
    if (cleanup) {
      SharedLock read_lock(mutex_);
      // Also need to check if this tablet is being kept for xcluster replication or for cdcsdk.
      cleanup = !(retained_by_xcluster_.contains(tablet->id()) ||
                  retained_by_cdcsdk_.contains(tablet->id()));
    }
    if (cleanup) {
      tablets_to_delete.push_back(tablet);
    }
  }
  if (!tablets_to_delete.empty()) {
    LOG_WITH_PREFIX(INFO) << "Cleanup hidden tablets: " << AsString(tablets_to_delete);
    WARN_NOT_OK(DeleteTabletListAndSendRequests(
        tablets_to_delete, "Cleanup hidden tablets", {} /* retained_by_snapshot_schedules */,
        false /* transaction_status_tablets */),
        "Failed to cleanup hidden tablets");
  }

  if (!tablets_to_remove_from_hidden.empty()) {
    auto it = tablets_to_remove_from_hidden.begin();
    LockGuard lock(mutex_);
    // Order of tablets in tablets_to_remove_from_hidden matches order in hidden_tablets_,
    // so we could avoid searching in tablets_to_remove_from_hidden.
    auto filter = [&it, end = tablets_to_remove_from_hidden.end()](const TabletInfoPtr& tablet) {
      if (it != end && tablet.get() == it->get()) {
        ++it;
        return true;
      }
      return false;
    };
    hidden_tablets_.erase(std::remove_if(hidden_tablets_.begin(), hidden_tablets_.end(), filter),
                          hidden_tablets_.end());
  }
}

void CatalogManager::CleanupHiddenTables(
    std::vector<TableInfoPtr> tables,
    const ScheduleMinRestoreTime& schedule_min_restore_time) {
  std::vector<TableInfo::WriteLock> locks;
  EraseIf([this, &locks, &schedule_min_restore_time](const TableInfoPtr& table) {
    {
      auto lock = table->LockForRead();
      // If the table is colocated and hidden then remove it from its colocated tablet if
      // it has expired.
      if (lock->is_hidden() && !lock->started_deleting()) {
        auto tablet_info = table->GetColocatedUserTablet();
        if (tablet_info) {
          auto tablet_lock = tablet_info->LockForRead();
          bool cleanup = true;
          auto hide_hybrid_time = HybridTime::FromPB(lock->pb.hide_hybrid_time());

          for (const auto& schedule_id_str : tablet_lock->pb.retained_by_snapshot_schedules()) {
            auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
            auto it = schedule_min_restore_time.find(schedule_id);
            // If schedule is not present in schedule_min_restore_time then it means that schedule
            // was deleted, so it should not retain the tablet.
            if (it != schedule_min_restore_time.end() && it->second <= hide_hybrid_time) {
              VLOG_WITH_PREFIX(1)
                  << "Retaining colocated table: " << table->id() << ", hide hybrid time: "
                  << hide_hybrid_time << ", because of schedule: " << schedule_id
                  << ", min restore time: " << it->second;
              cleanup = false;
              break;
            }
          }

          if (!cleanup) {
            return true;
          }
          LOG(INFO) << "Cleaning up HIDDEN colocated table " << table->name();
          auto call = std::make_shared<AsyncRemoveTableFromTablet>(
              master_, AsyncTaskPool(), tablet_info, table);
          table->AddTask(call);
          WARN_NOT_OK(ScheduleTask(call), "Failed to send RemoveTableFromTablet request");
          table->ClearTabletMaps();
        }
      }
      if (!lock->is_hidden() || lock->started_deleting() || !table->AreAllTabletsDeleted()) {
        return true;
      }
    }
    auto lock = table->LockForWrite();
    if (lock->started_deleting()) {
      return true;
    }
    LOG_WITH_PREFIX(INFO) << "Should delete table: " << AsString(table);
    lock.mutable_data()->set_state(
        SysTablesEntryPB::DELETED, Format("Cleanup hidden table at $0", LocalTimeAsString()));
    locks.push_back(std::move(lock));
    return false;
  }, &tables);
  if (tables.empty()) {
    return;
  }

  Status s = sys_catalog_->Upsert(leader_ready_term(), tables);
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Failed to mark tables as deleted: " << s;
    return;
  }
  for (auto& lock : locks) {
    lock.Commit();
  }
}

rpc::Scheduler& CatalogManager::Scheduler() {
  return master_->messenger()->scheduler();
}

int64_t CatalogManager::LeaderTerm() {
  auto peer = tablet_peer();
  if (!peer) {
    return -1;
  }
  auto consensus = peer->shared_consensus();
  if (!consensus) {
    return -1;
  }
  return consensus->GetLeaderState(/* allow_stale= */ true).term;
}

void CatalogManager::HandleCreateTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Create Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No active snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, current_snapshot_id_);

    if (!snapshot) {
      LOG(WARNING) << "Snapshot not found: " << current_snapshot_id_;
      return;
    }
  }

  if (!snapshot->IsCreateInProgress()) {
    LOG(WARNING) << "Snapshot is not in creating state: " << snapshot->id();
    return;
  }

  auto tablet_l = tablet->LockForRead();
  auto l = snapshot->LockForWrite();
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
  int num_tablets_complete = 0;

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);

    if (tablet_info->id() == tablet->id()) {
      tablet_info->set_state(error ? SysSnapshotEntryPB::FAILED : SysSnapshotEntryPB::COMPLETE);
    }

    if (tablet_info->state() == SysSnapshotEntryPB::COMPLETE) {
      ++num_tablets_complete;
    }
  }

  // Finish the snapshot.
  bool finished = true;
  if (error) {
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " on tablet " << tablet->id();
  } else if (num_tablets_complete == tablet_snapshots->size()) {
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
    LOG(INFO) << "Completed snapshot " << snapshot->id();
  } else {
    finished = false;
  }

  if (finished) {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");
    current_snapshot_id_ = "";
  }

  VLOG(1) << "Snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();

  const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);

  l.CommitOrWarn(s, "updating snapshot in sys-catalog");
}

void CatalogManager::HandleRestoreTabletSnapshotResponse(TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Restore Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_.empty()) {
      LOG(WARNING) << "No restoring snapshot: " << current_snapshot_id_;
      return;
    }

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, current_snapshot_id_);

    if (!snapshot) {
      LOG(WARNING) << "Restoring snapshot not found: " << current_snapshot_id_;
      return;
    }
  }

  if (!snapshot->IsRestoreInProgress()) {
    LOG(WARNING) << "Snapshot is not in restoring state: " << snapshot->id();
    return;
  }

  auto tablet_l = tablet->LockForRead();
  auto l = snapshot->LockForWrite();
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
  int num_tablets_complete = 0;

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);

    if (tablet_info->id() == tablet->id()) {
      tablet_info->set_state(error ? SysSnapshotEntryPB::FAILED : SysSnapshotEntryPB::COMPLETE);
    }

    if (tablet_info->state() == SysSnapshotEntryPB::COMPLETE) {
      ++num_tablets_complete;
    }
  }

  // Finish the snapshot.
  if (error || num_tablets_complete == tablet_snapshots->size()) {
    if (error) {
      l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
      LOG(WARNING) << "Failed restoring snapshot " << snapshot->id()
                   << " on tablet " << tablet->id();
    } else {
      LOG_IF(DFATAL, num_tablets_complete != tablet_snapshots->size())
          << "Wrong number of tablets";
      l.mutable_data()->pb.set_state(SysSnapshotEntryPB::COMPLETE);
      LOG(INFO) << "Restored snapshot " << snapshot->id();
    }

    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");
    current_snapshot_id_ = "";
  }

  VLOG(1) << "Snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();

  const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);

  l.CommitOrWarn(s, "updating snapshot in sys-catalog");
}

void CatalogManager::HandleDeleteTabletSnapshotResponse(
    const SnapshotId& snapshot_id, TabletInfo *tablet, bool error) {
  LOG(INFO) << "Handling Delete Tablet Snapshot Response for tablet "
            << DCHECK_NOTNULL(tablet)->ToString() << (error ? "  ERROR" : "  OK");

  // Get the snapshot data descriptor from the catalog manager.
  scoped_refptr<SnapshotInfo> snapshot;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    snapshot = FindPtrOrNull(non_txn_snapshot_ids_map_, snapshot_id);

    if (!snapshot) {
      LOG(WARNING) << __func__ << " Snapshot not found: " << snapshot_id;
      return;
    }
  }

  if (!snapshot->IsDeleteInProgress()) {
    LOG(WARNING) << "Snapshot is not in deleting state: " << snapshot->id();
    return;
  }

  auto tablet_l = tablet->LockForRead();
  auto l = snapshot->LockForWrite();
  auto* tablet_snapshots = l.mutable_data()->pb.mutable_tablet_snapshots();
  int num_tablets_complete = 0;

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);

    if (tablet_info->id() == tablet->id()) {
      tablet_info->set_state(error ? SysSnapshotEntryPB::FAILED : SysSnapshotEntryPB::DELETED);
    }

    if (tablet_info->state() != SysSnapshotEntryPB::DELETING) {
      ++num_tablets_complete;
    }
  }

  if (num_tablets_complete == tablet_snapshots->size()) {
    // Delete the snapshot.
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::DELETED);
    LOG(INFO) << "Deleted snapshot " << snapshot->id();

    const Status s = sys_catalog_->Delete(leader_ready_term(), snapshot);

    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (current_snapshot_id_ == snapshot_id) {
      current_snapshot_id_ = "";
    }

    // Remove it from the maps.
    TRACE("Removing from maps");
    if (non_txn_snapshot_ids_map_.erase(snapshot_id) < 1) {
      LOG(WARNING) << "Could not remove snapshot " << snapshot_id << " from map";
    }

    l.CommitOrWarn(s, "deleting snapshot from sys-catalog");
  } else if (error) {
    l.mutable_data()->pb.set_state(SysSnapshotEntryPB::FAILED);
    LOG(WARNING) << "Failed snapshot " << snapshot->id() << " deletion on tablet " << tablet->id();

    const Status s = sys_catalog_->Upsert(leader_ready_term(), snapshot);
    l.CommitOrWarn(s, "updating snapshot in sys-catalog");
  }

  VLOG(1) << "Deleting snapshot: " << snapshot->id()
          << " PB: " << l.mutable_data()->pb.DebugString()
          << " Complete " << num_tablets_complete << " tablets from " << tablet_snapshots->size();
}

Status CatalogManager::CreateSnapshotSchedule(const CreateSnapshotScheduleRequestPB* req,
                                              CreateSnapshotScheduleResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing CreateSnapshotSchedule " << req->ShortDebugString();

  auto id = VERIFY_RESULT(snapshot_coordinator_.CreateSchedule(
      *req, leader_ready_term(), rpc->GetClientDeadline()));
  resp->set_snapshot_schedule_id(id.data(), id.size());
  return Status::OK();
}

Status CatalogManager::ListSnapshotSchedules(const ListSnapshotSchedulesRequestPB* req,
                                             ListSnapshotSchedulesResponsePB* resp,
                                             rpc::RpcContext* rpc) {
  auto snapshot_schedule_id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());

  return snapshot_coordinator_.ListSnapshotSchedules(snapshot_schedule_id, resp);
}

Status CatalogManager::DeleteSnapshotSchedule(const DeleteSnapshotScheduleRequestPB* req,
                                              DeleteSnapshotScheduleResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  auto snapshot_schedule_id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());

  return snapshot_coordinator_.DeleteSnapshotSchedule(
      snapshot_schedule_id, leader_ready_term(), rpc->GetClientDeadline());
}

Status CatalogManager::EditSnapshotSchedule(
    const EditSnapshotScheduleRequestPB* req,
    EditSnapshotScheduleResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());
  *resp->mutable_schedule() = VERIFY_RESULT(snapshot_coordinator_.EditSnapshotSchedule(
      id, *req, leader_ready_term(), rpc->GetClientDeadline()));
  return Status::OK();
}

Status CatalogManager::RestoreSnapshotSchedule(
    const RestoreSnapshotScheduleRequestPB* req,
    RestoreSnapshotScheduleResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto id = TryFullyDecodeSnapshotScheduleId(req->snapshot_schedule_id());
  HybridTime ht = HybridTime(req->restore_ht());
  auto deadline = rpc->GetClientDeadline();

  const auto disable_duration_ms = MonoDelta::FromMilliseconds(1000 *
      (FLAGS_inflight_splits_completion_timeout_secs + FLAGS_pitr_max_restore_duration_secs));
  const auto wait_inflight_splitting_until = CoarseMonoClock::Now() +
      MonoDelta::FromMilliseconds(1000 * FLAGS_inflight_splits_completion_timeout_secs);

  // Disable splitting and then wait for all pending splits to complete before starting restoration.
  DisableTabletSplittingInternal(disable_duration_ms, "PITR");

  bool inflight_splits_finished = false;
  while (CoarseMonoClock::Now() < std::min(wait_inflight_splitting_until, deadline)) {
    // Wait for existing split operations to complete.
    if (IsTabletSplittingCompleteInternal(true /* wait_for_parent_deletion */)) {
      inflight_splits_finished = true;
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_pitr_split_disable_check_freq_ms));
  }

  if (!inflight_splits_finished) {
    EnableTabletSplitting("PITR");
    return STATUS(TimedOut, "Timed out waiting for inflight tablet splitting to complete.");
  }

  return snapshot_coordinator_.RestoreSnapshotSchedule(id, ht, resp, leader_ready_term(), deadline);
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  super::DumpState(out, on_disk_dump);

  // TODO: dump snapshots
}

template <typename Registry, typename Mutex>
bool ShouldResendRegistry(
    const std::string& ts_uuid, bool has_registration, Registry* registry, Mutex* mutex) {
  bool should_resend_registry;
  {
    std::lock_guard<Mutex> lock(*mutex);
    auto it = registry->find(ts_uuid);
    should_resend_registry = (it == registry->end() || it->second || has_registration);
    if (it == registry->end()) {
      registry->emplace(ts_uuid, false);
    } else {
      it->second = false;
    }
  }
  return should_resend_registry;
}

Status CatalogManager::FillHeartbeatResponse(const TSHeartbeatRequestPB* req,
                                             TSHeartbeatResponsePB* resp) {
  SysClusterConfigEntryPB cluster_config;
  RETURN_NOT_OK(GetClusterConfig(&cluster_config));
  RETURN_NOT_OK(FillHeartbeatResponseEncryption(cluster_config, req, resp));
  RETURN_NOT_OK(snapshot_coordinator_.FillHeartbeatResponse(resp));
  return FillHeartbeatResponseCDC(cluster_config, req, resp);
}

Status CatalogManager::FillHeartbeatResponseCDC(const SysClusterConfigEntryPB& cluster_config,
                                                const TSHeartbeatRequestPB* req,
                                                TSHeartbeatResponsePB* resp) {
  if (cdc_enabled_.load(std::memory_order_acquire)) {
    resp->set_xcluster_enabled_on_producer(true);
  }
  resp->set_cluster_config_version(cluster_config.version());
  if (cluster_config.has_consumer_registry()) {
    {
      auto l = xcluster_safe_time_info_.LockForRead();
      *resp->mutable_xcluster_namespace_to_safe_time() = l->pb.safe_time_map();
    }

    if (req->cluster_config_version() < cluster_config.version()) {
      *resp->mutable_consumer_registry() = cluster_config.consumer_registry();
    }
  }

  return Status::OK();
}

Status CatalogManager::FillHeartbeatResponseEncryption(
    const SysClusterConfigEntryPB& cluster_config,
    const TSHeartbeatRequestPB* req,
    TSHeartbeatResponsePB* resp) {
  const auto& ts_uuid = req->common().ts_instance().permanent_uuid();
  if (!cluster_config.has_encryption_info() ||
      !ShouldResendRegistry(ts_uuid, req->has_registration(), &should_send_universe_key_registry_,
                            &should_send_universe_key_registry_mutex_)) {
    return Status::OK();
  }

  const auto& encryption_info = cluster_config.encryption_info();
  RETURN_NOT_OK(encryption_manager_->FillHeartbeatResponseEncryption(encryption_info, resp));

  return Status::OK();
}

void CatalogManager::SetTabletSnapshotsState(SysSnapshotEntryPB::State state,
                                             SysSnapshotEntryPB* snapshot_pb) {
  auto* tablet_snapshots = snapshot_pb->mutable_tablet_snapshots();

  for (int i = 0; i < tablet_snapshots->size(); ++i) {
    SysSnapshotEntryPB_TabletSnapshotPB* tablet_info = tablet_snapshots->Mutable(i);
    tablet_info->set_state(state);
  }
}

Status CatalogManager::CreateCdcStateTableIfNeeded(rpc::RpcContext *rpc) {
  // If CDC state table exists do nothing, otherwise create it.
  if (VERIFY_RESULT(TableExists(kSystemNamespaceName, kCdcStateTableName))) {
    return Status::OK();
  }
  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(kCdcStateTableName);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(master::kCdcTabletId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcStreamId)->PrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcCheckpoint)->Type(DataType::STRING);
  schema_builder.AddColumn(master::kCdcData)->Type(QLType::CreateTypeMap(
      DataType::STRING, DataType::STRING));
  schema_builder.AddColumn(master::kCdcLastReplicationTime)->Type(DataType::TIMESTAMP);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto schema = yb::client::internal::GetSchema(yb_schema);
  SchemaToPB(schema, req.mutable_schema());
  // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
  // will use the same defaults as for regular tables.
  if (FLAGS_cdc_state_table_num_tablets > 0) {
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(
        FLAGS_cdc_state_table_num_tablets);
  }

  Status s = CreateTable(&req, &resp, rpc);
  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }
  return Status::OK();
}

Status CatalogManager::IsCdcStateTableCreated(IsCreateTableDoneResponsePB* resp) {
  IsCreateTableDoneRequestPB req;

  req.mutable_table()->set_table_name(kCdcStateTableName);
  req.mutable_table()->mutable_namespace_()->set_name(kSystemNamespaceName);

  return IsCreateTableDone(&req, resp);
}

// Helper class to print a vector of CDCStreamInfo pointers.
namespace {

template<class CDCStreamInfoPointer>
std::string JoinStreamsCSVLine(std::vector<CDCStreamInfoPointer> cdc_streams) {
  std::vector<CDCStreamId> cdc_stream_ids;
  for (const auto& cdc_stream : cdc_streams) {
    cdc_stream_ids.push_back(cdc_stream->id());
  }
  return JoinCSVLine(cdc_stream_ids);
}

} // namespace

Status CatalogManager::DeleteCDCStreamsForTable(const TableId& table_id) {
  return DeleteCDCStreamsForTables({table_id});
}

Status CatalogManager::DeleteCDCStreamsForTables(const unordered_set<TableId>& table_ids) {
  std::ostringstream tid_stream;
  for (const auto& tid : table_ids) {
    tid_stream << " " << tid;
  }
  LOG(INFO) << "Deleting CDC streams for tables:" << tid_stream.str();

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  if (!table_ids.empty()) {
    SharedLock lock(mutex_);
    for (const auto& tid : table_ids) {
      auto newstreams = FindCDCStreamsForTableUnlocked(tid, cdc::XCLUSTER);
      streams.insert(streams.end(), newstreams.begin(), newstreams.end());
    }
  }

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  return MarkCDCStreamsForMetadataCleanup(streams, SysCDCStreamEntryPB::DELETING);
}

Status CatalogManager::DeleteCDCStreamsMetadataForTable(const TableId& table_id) {
  return DeleteCDCStreamsMetadataForTables({table_id});
}

Status CatalogManager::DeleteCDCStreamsMetadataForTables(const unordered_set<TableId>& table_ids) {
  std::ostringstream tid_stream;
  for (const auto& tid : table_ids) {
    tid_stream << " " << tid;
  }
  LOG(INFO) << "Deleting CDC streams metadata for tables:" << tid_stream.str();

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  {
    LockGuard lock(mutex_);
    for (const auto& table_id : table_ids) {
      cdcsdk_tables_to_stream_map_.erase(table_id);
    }
    streams = FindCDCStreamsForTablesToDeleteMetadata(table_ids);
  }

  // Do not delete them here, just mark them as DELETING_METADATA and the catalog manager background
  // thread will handle the deletion.
  return MarkCDCStreamsForMetadataCleanup(streams, SysCDCStreamEntryPB::DELETING_METADATA);
}

Status CatalogManager::AddNewTableToCDCDKStreamsMetadata(
    const TableId& table_id, const NamespaceId& ns_id) {
  SharedLock lock(mutex_);
  for (const auto& entry : cdc_stream_map_) {
    // We only look for streams on the same namespace as the table.
    if (entry.second->namespace_id() == ns_id) {
      // We get the write lock before modifying 'cdcsdk_unprocessed_tables'.
      auto stream_lock = entry.second->LockForWrite();
      if (!stream_lock->is_deleting()) {
        // We only update the in-memory cache of cdc_state with the unprocessed tables. In events of
        // master restart and leadership change, the catalog manager bg thread will scan all streams
        // to find all missing tables and repopulate this 'cdcsdk_unprocessed_tables' field, through
        // the method: FindAllCDCSDKStreamsMissingTables.
        entry.second->cdcsdk_unprocessed_tables.insert(table_id);
      }
    }
  }

  return Status::OK();
}

std::vector<scoped_refptr<CDCStreamInfo>> CatalogManager::FindCDCStreamsForTableUnlocked(
    const TableId& table_id, const cdc::CDCRequestSource cdc_request_source) const {
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  for (const auto& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (!ltm->table_id().empty() &&
        (std::find(ltm->table_id().begin(), ltm->table_id().end(), table_id) !=
         ltm->table_id().end()) &&
        !ltm->started_deleting()) {
      if ((cdc_request_source == cdc::CDCSDK && !ltm->namespace_id().empty()) ||
          (cdc_request_source == cdc::XCLUSTER && ltm->namespace_id().empty())) {
        streams.push_back(entry.second);
      }
    }
  }
  return streams;
}

std::vector<scoped_refptr<CDCStreamInfo>> CatalogManager::FindCDCStreamsForTablesToDeleteMetadata(
    const std::unordered_set<TableId>& table_ids) const {
  std::vector<scoped_refptr<CDCStreamInfo>> streams;

  for (const auto& [_, stream_info] : cdc_stream_map_) {
    auto ltm = stream_info->LockForRead();
    if (ltm->is_deleting_metadata() || ltm->namespace_id().empty()) {
      continue;
    }
    if (std::any_of(ltm->table_id().begin(),
                    ltm->table_id().end(),
                    [&table_ids](const auto& table_id) {
                      return table_ids.contains(table_id);
                    })) {
      streams.push_back(stream_info);
    }
  }
  return streams;
}

void CatalogManager::GetAllCDCStreams(std::vector<scoped_refptr<CDCStreamInfo>>* streams) {
  streams->clear();
  SharedLock lock(mutex_);
  streams->reserve(cdc_stream_map_.size());
  for (const CDCStreamInfoMap::value_type& e : cdc_stream_map_) {
    if (!e.second->LockForRead()->is_deleting()) {
      streams->push_back(e.second);
    }
  }
}

Status CatalogManager::BackfillMetadataForCDC(
    scoped_refptr<TableInfo> table, rpc::RpcContext* rpc) {
  TableId table_id;
  AlterTableRequestPB alter_table_req_pg_type;
  bool backfill_required = false;
  {
    SharedLock lock(mutex_);
    auto l = table->LockForRead();
    table_id = table->id();
    if (table->GetTableType() == PGSQL_TABLE_TYPE) {
      if (!table->has_pg_type_oid()) {
        LOG_WITH_FUNC(INFO) << "backfilling pg_type_oid for table " << table_id;
        auto const att_name_typid_map = VERIFY_RESULT(GetPgAttNameTypidMap(table));
        vector<uint32_t> type_oids;
        for (const auto& entry : att_name_typid_map) {
          type_oids.push_back(entry.second);
        }
        auto ns = VERIFY_RESULT(FindNamespaceByIdUnlocked(table->namespace_id()));
        auto const type_oid_info_map = VERIFY_RESULT(GetPgTypeInfo(ns, &type_oids));
        for (const auto& entry : att_name_typid_map) {
          VLOG(1) << "For table:" << table->name() << " column:" << entry.first
                  << ", pg_type_oid: " << entry.second;
          auto* step = alter_table_req_pg_type.add_alter_schema_steps();
          step->set_type(::yb::master::AlterTableRequestPB_StepType::
                             AlterTableRequestPB_StepType_SET_COLUMN_PG_TYPE);
          auto set_column_pg_type = step->mutable_set_column_pg_type();
          set_column_pg_type->set_name(entry.first);
          uint32_t pg_type_oid = entry.second;

          const YBCPgTypeEntity* type_entity =
              docdb::DocPgGetTypeEntity({(int32_t)pg_type_oid, -1});

          if (type_entity == nullptr &&
              type_oid_info_map.find(pg_type_oid) != type_oid_info_map.end()) {
            VLOG(1) << "Looking up primitive type for: " << pg_type_oid;
            PgTypeInfo pg_type_info = type_oid_info_map.at(pg_type_oid);
            YbgGetPrimitiveTypeOid(
                pg_type_oid, pg_type_info.typtype, pg_type_info.typbasetype, &pg_type_oid);
            VLOG(1) << "Found primitive type oid: " << pg_type_oid;
          }
          set_column_pg_type->set_pg_type_oid(pg_type_oid);
        }
        backfill_required = true;
      }

      // If pg_type_oid has to be backfilled, we backfill the pgschema_name irrespective of whether
      // it is present or not. It is a safeguard against
      // https://phabricator.dev.yugabyte.com/D17099 which fills the pgschema_name in memory if it
      // is not present without backfilling it to master's disk or tservers.
      // Skip this check for colocated parent tables as they do not have pgschema names.
      if (!IsColocationParentTableId(table_id) &&
          (backfill_required || table->pgschema_name().empty())) {
        LOG_WITH_FUNC(INFO) << "backfilling pgschema_name for table " << table_id;
        string pgschema_name = VERIFY_RESULT(GetPgSchemaName(table));
        VLOG(1) << "For table: " << table->name() << " found pgschema_name: " << pgschema_name;
        alter_table_req_pg_type.set_pgschema_name(pgschema_name);
        backfill_required = true;
      }
    }
  }

  if (backfill_required) {
    // The alter table asynchrnously propagates the change to the tablets. It is okay here as these
    // fields are only required at stream consumption and there is a gap between stream creation and
    // consumption because the former is generally done manually.
    alter_table_req_pg_type.mutable_table()->set_table_id(table_id);
    AlterTableResponsePB alter_table_resp_pg_type;
    return this->AlterTable(&alter_table_req_pg_type, &alter_table_resp_pg_type, rpc);
  } else {
    LOG_WITH_FUNC(INFO)
        << "found pgschema_name and pg_type_oid, no backfilling required for table id: "
        << table_id;
    return Status::OK();
  }
}

Status CatalogManager::GetTableSchemaFromSysCatalog(
    const GetTableSchemaFromSysCatalogRequestPB* req, GetTableSchemaFromSysCatalogResponsePB* resp,
    rpc::RpcContext* rpc) {
  uint64_t read_time = std::numeric_limits<uint64_t>::max();
  if (!req->has_read_time()) {
    LOG(INFO) << "Reading latest schema version for: " << req->table().table_id()
              << " from system catalog table";
  } else {
    read_time = req->read_time();
  }
  VLOG(1) << "Get the table: " << req->table().table_id()
          << " specific schema from system catalog with read hybrid time: " << req->read_time();
  Schema schema;
  uint32_t schema_version;
  auto status = sys_catalog_->GetTableSchema(
      req->table().table_id(), ReadHybridTime::FromUint64(read_time), &schema, &schema_version);
  if (!status.ok()) {
    Status s = STATUS_SUBSTITUTE(
        NotFound, "Could not find specific schema from system catalog for request $0.",
        req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }
  SchemaToPB(schema, resp->mutable_schema());
  resp->set_version(schema_version);
  return Status::OK();
}

Status CatalogManager::CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                       CreateCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "CreateCDCStream from " << RequestorString(rpc) << ": " << req->ShortDebugString();
  std::string id_type_option_value(cdc::kTableId);

  for (auto option : req->options()) {
    if (option.key() == cdc::kIdType) {
      id_type_option_value = option.value();
    }
  }

  if (id_type_option_value != cdc::kNamespaceId) {
    scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(req->table_id()));

    {
      auto l = table->LockForRead();
      if (l->started_deleting()) {
        return STATUS(
            NotFound, "Table does not exist", req->table_id(),
            MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
    }

    AlterTableRequestPB alter_table_req;
    alter_table_req.mutable_table()->set_table_id(req->table_id());
    alter_table_req.set_wal_retention_secs(FLAGS_cdc_wal_retention_time_secs);
    AlterTableResponsePB alter_table_resp;
    Status s = this->AlterTable(&alter_table_req, &alter_table_resp, rpc);
    if (!s.ok()) {
      return STATUS(InternalError,
                    "Unable to change the WAL retention time for table", req->table_id(),
                    MasterError(MasterErrorPB::INTERNAL_ERROR));
    }

    Status status = BackfillMetadataForCDC(table, rpc);
    if (!status.ok()) {
      return STATUS(
          InternalError, "Unable to backfill pgschema_name and/or pg_type_oid", req->table_id(),
          MasterError(MasterErrorPB::INTERNAL_ERROR));
    }
  }

  scoped_refptr<CDCStreamInfo> stream;
  if (!req->has_db_stream_id()) {
    {
      TRACE("Acquired catalog manager lock");
      LockGuard lock(mutex_);
      // Construct the CDC stream if the producer wasn't bootstrapped.
      CDCStreamId stream_id;
      stream_id = GenerateIdUnlocked(SysRowEntryType::CDC_STREAM);

      stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
      stream->mutable_metadata()->StartMutation();
      SysCDCStreamEntryPB* metadata = &stream->mutable_metadata()->mutable_dirty()->pb;
      bool create_namespace = id_type_option_value == cdc::kNamespaceId;
      if (create_namespace) {
        metadata->set_namespace_id(req->table_id());
      } else {
        metadata->add_table_id(req->table_id());
      }
      metadata->mutable_options()->CopyFrom(req->options());
      metadata->set_state(
          req->has_initial_state() ? req->initial_state() : SysCDCStreamEntryPB::ACTIVE);

      // Add the stream to the in-memory map.
      cdc_stream_map_[stream->id()] = stream;
      if (!create_namespace) {
        xcluster_producer_tables_to_stream_map_[req->table_id()].insert(stream->id());
      }
      resp->set_stream_id(stream->id());
    }
    TRACE("Inserted new CDC stream into CatalogManager maps");

    // Update the on-disk system catalog.
    RETURN_NOT_OK(CheckLeaderStatusAndSetupError(
        sys_catalog_->Upsert(leader_ready_term(), stream),
        "inserting CDC stream into sys-catalog", resp));
    TRACE("Wrote CDC stream to sys-catalog");

    // Commit the in-memory state.
    stream->mutable_metadata()->CommitMutation();
    LOG(INFO) << "Created CDC stream " << stream->ToString();

    RETURN_NOT_OK(CreateCdcStateTableIfNeeded(rpc));
    TRACE("Created CDC state table");

    if (!PREDICT_FALSE(FLAGS_TEST_disable_cdc_state_insert_on_setup) &&
        (!req->has_initial_state() ||
         (req->initial_state() == master::SysCDCStreamEntryPB::ACTIVE)) &&
        (id_type_option_value != cdc::kNamespaceId)) {
      // Create the cdc state entries for the tablets in this table from scratch since we have no
      // data to bootstrap. If we data to bootstrap, let the BootstrapProducer logic take care of
      // populating entries in cdc_state.
      auto ybclient = master_->cdc_state_client_initializer().client();
      if (!ybclient) {
        return STATUS(IllegalState, "Client not initialized or shutting down");
      }
      client::TableHandle cdc_table;
      const client::YBTableName cdc_state_table_name(
          YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
      RETURN_NOT_OK(ybclient->WaitForCreateTableToFinish(cdc_state_table_name));
      RETURN_NOT_OK(cdc_table.Open(cdc_state_table_name, ybclient));
      std::shared_ptr<client::YBSession> session = ybclient->NewSession();
      scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(req->table_id()));
      auto tablets = table->GetTablets();
      for (const auto& tablet : tablets) {
        const auto op = cdc_table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
        auto* const req = op->mutable_request();
        QLAddStringHashValue(req, tablet->id());
        QLAddStringRangeValue(req, stream->id());
        cdc_table.AddStringColumnValue(req, master::kCdcCheckpoint, OpId().ToString());
        cdc_table.AddTimestampColumnValue(
            req, master::kCdcLastReplicationTime, GetCurrentTimeMicros());

        if (id_type_option_value == cdc::kNamespaceId) {
          // For cdcsdk cases, we also need to persist last_active_time in the 'cdc_state' table. We
          // will store this info in the map in the 'kCdcData' column.
          auto column_id = cdc_table.ColumnId(master::kCdcData);
          auto map_value_pb = client::AddMapColumn(req, column_id);
          client::AddMapEntryToColumn(map_value_pb, "active_time", "0");
          client::AddMapEntryToColumn(map_value_pb, "cdc_sdk_safe_time", "0");
        }

        session->Apply(op);
      }
      // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
      RETURN_NOT_OK(session->TEST_Flush());
      TRACE("Created CDC state entries");
    }
  } else {
    // Update and add table_id.
    {
      SharedLock lock(mutex_);
      stream = FindPtrOrNull(cdc_stream_map_, req->db_stream_id());
    }

    if (stream == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC stream", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }

    auto stream_lock = stream->LockForWrite();
    if (stream_lock->is_deleting()) {
      return STATUS(
          NotFound, "CDC stream has been deleted", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    stream_lock.mutable_data()->pb.add_table_id(req->table_id());

    if (req->has_initial_state()) {
      stream_lock.mutable_data()->pb.set_state(req->initial_state());
    }

    // Also need to persist changes in sys catalog.
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), stream));
    stream_lock.Commit();
    TRACE("Updated CDC stream in sys-catalog");
    // Add the stream to the in-memory map.
    {
      LockGuard lock(mutex_);
      cdcsdk_tables_to_stream_map_[req->table_id()].insert(stream->id());
    }
  }

  // Now that the stream is set up, mark the entire cluster as a cdc enabled.
  SetCDCServiceEnabled();

  return Status::OK();
}

Status CatalogManager::DeleteCDCStream(const DeleteCDCStreamRequestPB* req,
                                       DeleteCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteCDCStream request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  if (req->stream_id_size() < 1) {
    return STATUS(InvalidArgument, "No CDC Stream ID given", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  {
    SharedLock lock(mutex_);
    for (const auto& stream_id : req->stream_id()) {
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);

      if (stream == nullptr || stream->LockForRead()->is_deleting()) {
        resp->add_not_found_stream_ids(stream_id);
        LOG(WARNING) << "CDC stream does not exist: " << stream_id;
      } else {
        auto ltm = stream->LockForRead();
        if (req->has_force_delete() && req->force_delete() == false) {
          bool active = (ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE);
          bool is_WAL = false;
          for (const auto& option : ltm->pb.options()) {
            if (option.key() == "record_format" && option.value() == "WAL") {
              is_WAL = true;
            }
          }
          if (is_WAL && active) {
            return STATUS(NotSupported,
                "Cannot delete an xCluster Stream in replication. "
                "Use 'force_delete' to override",
                req->ShortDebugString(),
                MasterError(MasterErrorPB::INVALID_REQUEST));
          }
        }
        streams.push_back(stream);
      }
    }
  }

  if (!resp->not_found_stream_ids().empty() && !req->ignore_errors()) {
    string missing_streams = JoinElementsIterator(resp->not_found_stream_ids().begin(),
                                                  resp->not_found_stream_ids().end(),
                                                  ",");
    return STATUS(
        NotFound,
        Substitute("Did not find all requested CDC streams. Missing streams: [$0]. Request: $1",
                   missing_streams, req->ShortDebugString()),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  Status s = MarkCDCStreamsForMetadataCleanup(streams, SysCDCStreamEntryPB::DELETING);
  if (!s.ok()) {
    if (s.IsIllegalState()) {
      PANIC_RPC(rpc, s.message().ToString());
    }
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  LOG(INFO) << "Successfully deleted CDC streams " << JoinStreamsCSVLine(streams)
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::MarkCDCStreamsForMetadataCleanup(
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams, SysCDCStreamEntryPB::State state) {
  if (streams.empty()) {
    return Status::OK();
  }
  std::vector<CDCStreamInfo::WriteLock> locks;
  std::vector<CDCStreamInfo*> streams_to_mark;
  locks.reserve(streams.size());
  for (auto& stream : streams) {
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.set_state(state);
    locks.push_back(std::move(l));
    streams_to_mark.push_back(stream.get());
  }
  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), streams_to_mark),
      "updating CDC streams in sys-catalog"));
  if (state == SysCDCStreamEntryPB::DELETING_METADATA) {
    LOG(INFO) << "Successfully marked streams " << JoinStreamsCSVLine(streams_to_mark)
              << " as DELETING_METADATA in sys catalog";
  } else if (state == SysCDCStreamEntryPB::DELETING) {
    LOG(INFO) << "Successfully marked streams " << JoinStreamsCSVLine(streams_to_mark)
              << " as DELETING in sys catalog";
  }
  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::FindCDCSDKStreamsForAddedTables(
    TableStreamIdsMap* table_to_unprocessed_streams_map) {
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  for (const auto& [stream_id, stream_info] : cdc_stream_map_) {
    if (stream_info->namespace_id().empty()) {
      continue;
    }

    auto ltm = stream_info->LockForRead();
    if (ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE ||
        ltm->pb.state() == SysCDCStreamEntryPB::DELETING_METADATA) {
      const auto cdcsdk_unprocessed_tables = stream_info->cdcsdk_unprocessed_tables;
      for (const auto& table_id : cdcsdk_unprocessed_tables) {
        auto table = FindPtrOrNull(*table_ids_map_, table_id);
        Schema schema;
        auto status = table->GetSchema(&schema);
        if (!status.ok()) {
          LOG(WARNING) << "Error while getting schema for table: " << table->name();
          continue;
        }
        bool has_pk = true;
        for (const auto& col : schema.columns()) {
          if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
            // ybrowid column is added for tables that don't have user-specified primary key.
            RemoveTableFromCDCSDKUnprocessedSet(table_id, stream_info);
            VLOG(1) << "Table: " << table_id
                    << ", will not be added to CDCSDK stream, since it does not have a primary key";
            has_pk = false;
            break;
          }
        }
        if (!has_pk) {
          continue;
        }

        if (std::find(ltm->table_id().begin(), ltm->table_id().end(), table_id) ==
            ltm->table_id().end()) {
          (*table_to_unprocessed_streams_map)[table_id].push_back(stream_info);
        } else {
          // This means we have already added the table_id to the stream's metadata.
          RemoveTableFromCDCSDKUnprocessedSet(table_id, stream_info);
        }
      }
    }
  }

  return Status::OK();
}

void CatalogManager::FindAllTablesMissingInCDCSDKStream(
    scoped_refptr<CDCStreamInfo> stream_info,
    yb::master::MetadataCowWrapper<yb::master::PersistentCDCStreamInfo>::WriteLock* stream_lock) {
  std::unordered_set<TableId> stream_table_ids;
  // Store all table_ids associated with the stram in 'stream_table_ids'.
  for (const auto& table_id : (*stream_lock)->table_id()) {
    stream_table_ids.insert(table_id);
  }

  // Get all the tables associated with the namespace.
  // If we find any table present only in the namespace, but not in the namespace, we add the table
  // id to 'cdcsdk_unprocessed_tables'.
  for (const auto& entry : *table_ids_map_) {
    auto& table_info = *entry.second;
    {
      auto ltm = table_info.LockForRead();
      if (!ltm->visible_to_client()) {
        continue;
      }
      if (ltm->namespace_id() != (*stream_lock)->namespace_id()) {
        continue;
      }

      bool has_pk = true;
      for (const auto& col : ltm->schema().columns()) {
        if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
          // ybrowid column is added for tables that don't have user-specified primary key.
          VLOG(1) << "Table: " << table_info.id()
                  << ", will not be added to CDCSDK stream, since it does not have a primary key";
          has_pk = false;
          break;
        }
      }
      if (!has_pk) {
        continue;
      }
    }

    if (IsMatviewTable(table_info)) {
      continue;
    }
    if (!IsUserTableUnlocked(table_info)) {
      continue;
    }

    if (!stream_table_ids.contains(table_info.id())) {
      LOG(INFO) << "Found unprocessed table: " << table_info.id()
                << ", for stream: " << stream_info;
      stream_info->cdcsdk_unprocessed_tables.insert(table_info.id());
    }
  }
}

Status CatalogManager::AddTabletEntriesToCDCSDKStreamsForNewTables(
    const TableStreamIdsMap& table_to_unprocessed_streams_map) {
  client::TableHandle cdc_table;
  const client::YBTableName cdc_state_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  auto ybclient = master_->cdc_state_client_initializer().client();
  if (!ybclient) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }
  RETURN_NOT_OK(cdc_table.Open(cdc_state_table_name, ybclient));
  std::shared_ptr<client::YBSession> session = ybclient->NewSession();

  int32_t processed_tables = 0;
  for (const auto& [table_id, streams] : table_to_unprocessed_streams_map) {
    if (processed_tables++ >= FLAGS_cdcsdk_table_processing_limit_per_run) {
      VLOG(1) << "Reached the limit of number of newly added tables to process per iteration. Will "
                 "process the reamining tables in the next iteration.";
      break;
    }
    GetTableLocationsRequestPB req;
    GetTableLocationsResponsePB resp;
    req.mutable_table()->set_table_id(table_id);
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
    req.set_require_tablets_running(true);
    req.set_include_inactive(false);

    auto s = GetTableLocations(&req, &resp);

    if (!s.ok()) {
      if (s.IsNotFound()) {
        // The table has been deleted. We will remove the table's entry from the stream's metadata.
        RemoveTableFromCDCSDKUnprocessedSet(table_id, streams);
      } else {
        LOG(WARNING) << "Encountered error calling: 'GetTableLocations' for table: " << table_id
                     << "while trying to add tablet details to cdc_state table. Error: " << s;
      }
      continue;
    }

    if (!resp.IsInitialized()) {
      VLOG(2) << "The table: " << table_id
              << ", is not initialised yet. Will add entries for tablets to cdc_state table once "
                 "all tablets are up and running";
      continue;
    }

    for (const auto& stream : streams) {
      if PREDICT_FALSE (stream == nullptr) {
        LOG(WARNING) << "Could not find CDC stream: " << stream->id();
        continue;
      }

      for (const auto& tablet_pb : resp.tablet_locations()) {
        const auto insert_op = cdc_table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
        auto* insert_req = insert_op->mutable_request();
        auto* const condition = insert_req->mutable_if_expr()->mutable_condition();
        condition->set_op(QL_OP_NOT_EXISTS);
        QLAddStringHashValue(insert_req, tablet_pb.tablet_id());
        QLAddStringRangeValue(insert_req, stream->id());
        cdc_table.AddStringColumnValue(
            insert_req, master::kCdcCheckpoint, OpId::Invalid().ToString());
        auto map_value_pb = client::AddMapColumn(insert_req, cdc_table.ColumnId(master::kCdcData));
        client::AddMapEntryToColumn(map_value_pb, "active_time", "0");
        client::AddMapEntryToColumn(map_value_pb, "cdc_sdk_safe_time", "0");
        session->Apply(insert_op);
      }

      auto status = session->TEST_Flush();
      if (!status.ok()) {
        LOG(WARNING) << "Encoutered error while trying to add tablets of table: " << table_id
                     << ", to cdc_state table for stream" << stream->id();
        continue;
      }

      auto stream_lock = stream->LockForWrite();
      if (stream_lock->is_deleting()) {
        continue;
      }
      stream_lock.mutable_data()->pb.add_table_id(table_id);
      // Also need to persist changes in sys catalog.
      status = sys_catalog_->Upsert(leader_ready_term(), stream);
      if (!status.ok()) {
        LOG(WARNING) << "Encountered error while trying to update sys_catalog of stream: "
                     << stream->id() << ", with table: " << table_id;
        continue;
      }

      // Add the table/ stream pair details to 'cdcsdk_tables_to_stream_map_', so that parent
      // tablets on which tablet split is successful will be hidden rather than deleted straight
      // away, as needed.
      {
        LockGuard lock(mutex_);
        cdcsdk_tables_to_stream_map_[table_id].insert(stream->id());
      }

      stream_lock.Commit();
      stream->cdcsdk_unprocessed_tables.erase(table_id);
      LOG(INFO) << "Added tablets of table: " << table_id
                << ", to cdc_state table for stream: " << stream->id();
    }
  }

  return Status::OK();
}

void CatalogManager::RemoveTableFromCDCSDKUnprocessedSet(
    const TableId& table_id, const std::list<scoped_refptr<CDCStreamInfo>>& streams) {
  for (const auto& stream : streams) {
    RemoveTableFromCDCSDKUnprocessedSet(table_id, stream);
  }
}

void CatalogManager::RemoveTableFromCDCSDKUnprocessedSet(
    const TableId& table_id, const scoped_refptr<CDCStreamInfo>& stream) {
  // We get the write lock on the stream before modifying 'cdcsdk_unprocessed_tables'
  auto stream_lock = stream->LockForWrite();
  stream->cdcsdk_unprocessed_tables.erase(table_id);
}

Status CatalogManager::FindCDCStreamsMarkedAsDeleting(
    std::vector<scoped_refptr<CDCStreamInfo>>* streams) {
  return FindCDCStreamsMarkedForMetadataDeletion(streams, SysCDCStreamEntryPB::DELETING);
}

Status CatalogManager::FindCDCStreamsMarkedForMetadataDeletion(
    std::vector<scoped_refptr<CDCStreamInfo>>* streams, SysCDCStreamEntryPB::State state) {
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();
    if (state == SysCDCStreamEntryPB::DELETING_METADATA && ltm->is_deleting_metadata()) {
      LOG(INFO) << "Stream " << entry.second->id() << " was marked as DELETING_METADATA";
      streams->push_back(entry.second);
    } else if (state == SysCDCStreamEntryPB::DELETING && ltm->is_deleting()) {
      LOG(INFO) << "Stream " << entry.second->id() << " was marked as DELETING";
      streams->push_back(entry.second);
    }
  }
  return Status::OK();
}

void CatalogManager::GetValidTabletsAndDroppedTablesForStream(
    const scoped_refptr<CDCStreamInfo> stream, set<TabletId>* tablets_with_streams,
    set<TableId>* dropped_tables) {
  for (const auto& table_id : stream->table_id()) {
    TabletInfos tablets;
    scoped_refptr<TableInfo> table;
    {
      TRACE("Acquired catalog manager lock");
      SharedLock lock(mutex_);
      table = FindPtrOrNull(*table_ids_map_, table_id);
    }
    // GetTablets locks lock_ in shared mode.
    if (table) {
      tablets = table->GetTablets();
    }

    // For the table dropped, GetTablets() will be empty.
    // For all other tables, GetTablets() will be non-empty.
    for (const auto& tablet : tablets) {
      tablets_with_streams->insert(tablet->tablet_id());
    }

    if (tablets.size() == 0) {
      dropped_tables->insert(table_id);
    }
  }
}

Result<std::shared_ptr<client::TableHandle>> CatalogManager::GetCDCStateTable() {
  auto ybclient = master_->cdc_state_client_initializer().client();
  const client::YBTableName cdc_state_table_name(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
  auto cdc_state_table = std::make_shared<yb::client::TableHandle>();
  auto s = cdc_state_table->Open(cdc_state_table_name, ybclient);
  RETURN_NOT_OK(s);
  if (!cdc_state_table) {
    return STATUS_FORMAT(
        IllegalState, "Unable to open table $0.", cdc_state_table_name.table_name());
  }
  return cdc_state_table;
}

Status CatalogManager::DeleteFromCDCStateTable(
    std::shared_ptr<yb::client::TableHandle> cdc_state_table_result,
    std::shared_ptr<client::YBSession> session, const TabletId& tablet_id,
    const CDCStreamId& stream_id) {
  const auto delete_op = cdc_state_table_result->NewDeleteOp();
  auto* const delete_req = delete_op->mutable_request();
  QLAddStringHashValue(delete_req, tablet_id);
  QLAddStringRangeValue(delete_req, stream_id);
  session->Apply(delete_op);
  // Don't remove the stream from the system catalog as well as master cdc_stream_map_
  // cache, if there is an error during a row delete for the corresponding stream-id,
  // tablet-id combination from cdc_state table.
  if (!delete_op->succeeded()) {
    return STATUS_FORMAT(QLError, "$0", delete_op->response().status());
  }
  return Status::OK();
}

Status CatalogManager::CleanUpCDCMetadataFromSystemCatalog(
    const StreamTablesMap& drop_stream_tablelist) {
  std::vector<scoped_refptr<CDCStreamInfo>> streams_to_delete;
  std::vector<scoped_refptr<CDCStreamInfo>> streams_to_update;
  std::vector<CDCStreamInfo::WriteLock> locks;

  TRACE("Cleaning CDC streams from map and system catalog.");
  {
    LockGuard lock(mutex_);
    for (auto& [delete_stream_id, drop_table_list] : drop_stream_tablelist) {
      if (cdc_stream_map_.find(delete_stream_id) != cdc_stream_map_.end()) {
        scoped_refptr<CDCStreamInfo> cdc_stream_info = cdc_stream_map_[delete_stream_id];
        auto ltm = cdc_stream_info->LockForWrite();
        // Delete the stream from cdc_stream_map_ if all tables associated with stream are dropped.
        if (ltm->table_id().size() == static_cast<int>(drop_table_list.size())) {
          if (!cdc_stream_map_.erase(cdc_stream_info->id())) {
            return STATUS(
                IllegalState, "Could not remove CDC stream from map", cdc_stream_info->id());
          }
          streams_to_delete.push_back(cdc_stream_info);
        } else {
          // Remove those tables info, that are dropped from the cdc_stream_map_ and update the
          // system catalog.
          for (auto table_id : drop_table_list) {
            auto table_id_iter = find(ltm->table_id().begin(), ltm->table_id().end(), table_id);
            if (table_id_iter != ltm->table_id().end()) {
              ltm.mutable_data()->pb.mutable_table_id()->erase(table_id_iter);
            }
          }
          streams_to_update.push_back(cdc_stream_info);
        }
        locks.push_back(std::move(ltm));
      }
    }
  }

  // Do system catalog UPDATE and DELETE based on the streams_to_update and streams_to_delete.
  auto writer = sys_catalog_->NewWriter(leader_ready_term());
  RETURN_NOT_OK(writer->Mutate(QLWriteRequestPB::QL_STMT_DELETE, streams_to_delete));
  RETURN_NOT_OK(writer->Mutate(QLWriteRequestPB::QL_STMT_UPDATE, streams_to_update));
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->SyncWrite(writer.get()), "Cleaning CDC streams from system catalog"));
  LOG(INFO) << "Successfully cleaned up the streams " << JoinStreamsCSVLine(streams_to_delete)
            << " from system catalog";

  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::CleanUpCDCStreamsMetadata(
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  auto ybclient = master_->cdc_state_client_initializer().client();
  if (!ybclient) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }
  std::shared_ptr<yb::client::TableHandle> cdc_state_table = VERIFY_RESULT(GetCDCStateTable());
  client::TableIteratorOptions options;
  Status failer_status;
  options.error_handler = [&failer_status](const Status& status) {
    LOG(WARNING) << "Scan of table failed: " << status;
    failer_status = status;
  };
  options.columns = std::vector<std::string>{
      master::kCdcTabletId, master::kCdcStreamId, master::kCdcCheckpoint,
      master::kCdcLastReplicationTime};
  std::shared_ptr<client::YBSession> session = ybclient->NewSession();
  // Map to identify the list of drop tables for the stream.
  StreamTablesMap drop_stream_tablelist;
  for (const auto& stream : streams) {
    // The set "tablets_with_streams" consists of all tablets not associated with the table
    // dropped. Tablets belonging to this set will not be deleted from cdc_state.
    // The set "drop_table_list" consists of all the tables those were associated with the stream,
    // but dropped.
    std::set<TabletId> tablets_with_streams;
    std::set<TableId> drop_table_list;
    bool should_delete_from_map = true;
    GetValidTabletsAndDroppedTablesForStream(stream, &tablets_with_streams, &drop_table_list);

    for (const auto& row : client::TableRange(*cdc_state_table, options)) {
      auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
      auto stream_id = row.column(master::kCdcStreamIdIdx).string_value();
      // 1. stream id matches the one marked for deleting.
      // 2. And tablet id is not contained in the set "tablets_with_streams".
      if ((stream_id == stream->id()) &&
          (tablets_with_streams.find(tablet_id) == tablets_with_streams.end())) {
        auto result = DeleteFromCDCStateTable(cdc_state_table, session, tablet_id, stream_id);
        if (!result.ok()) {
          LOG(WARNING) << "Error deleting cdc_state row with tablet id " << tablet_id
                       << " and stream id " << stream_id << " : " << result.message().cdata();
          should_delete_from_map = false;
          break;
        }
        LOG(INFO) << "Deleted cdc_state table entry for stream " << stream_id << " for tablet "
                  << tablet_id;
      }
    }

    if (should_delete_from_map) {
      // Track those succeed delete stream in the map.
      drop_stream_tablelist[stream->id()] = drop_table_list;
    }
  }

  Status s = session->TEST_Flush();
  if (!s.ok()) {
    LOG(ERROR) << "Unable to flush operations to delete cdc streams: " << s;
    return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
  }

  // Cleanup the streams from system catalog and from internal maps.
  return CleanUpCDCMetadataFromSystemCatalog(drop_stream_tablelist);
}

Status CatalogManager::CleanUpDeletedCDCStreams(
    const std::vector<scoped_refptr<CDCStreamInfo>>& streams) {
  auto ybclient = master_->cdc_state_client_initializer().client();
  if (!ybclient) {
    return STATUS(IllegalState, "Client not initialized or shutting down");
  }

  // First. For each deleted stream, delete the cdc state rows.
  // Delete all the entries in cdc_state table that contain all the deleted cdc streams.

  // We only want to iterate through cdc_state once, so create a map here to efficiently check if
  // a row belongs to a stream that should be deleted.
  std::unordered_map<CDCStreamId, CDCStreamInfo*> stream_id_to_stream_info_map;
  for (const auto& stream : streams) {
    stream_id_to_stream_info_map.emplace(stream->id(), stream.get());
  }

  std::shared_ptr<yb::client::TableHandle> cdc_table = VERIFY_RESULT(GetCDCStateTable());
  client::TableIteratorOptions options;
  Status failure_status;
  options.error_handler = [&failure_status](const Status& status) {
    LOG(WARNING) << "Scan of table failed: " << status;
    failure_status = status;
  };
  options.columns = std::vector<std::string>{master::kCdcTabletId, master::kCdcStreamId};

  std::shared_ptr<client::YBSession> session = ybclient->NewSession();
  std::vector<std::pair<CDCStreamId, std::shared_ptr<client::YBqlWriteOp>>> stream_ops;
  std::set<CDCStreamId> failed_streams;
  cdc::CDCRequestSource streams_type = cdc::XCLUSTER;

  // Remove all entries from cdc_state with the given stream ids.
  for (const auto& row : client::TableRange(*cdc_table, options)) {
    auto stream_id = row.column(master::kCdcStreamIdIdx).string_value();
    auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();

    const auto stream = FindPtrOrNull(stream_id_to_stream_info_map, stream_id);
    if (stream) {
      if (!stream->namespace_id().empty()) {
        // CDCSDK stream.
        streams_type = cdc::CDCSDK;
        const auto update_op = cdc_table->NewUpdateOp();
        auto* const update_req = update_op->mutable_request();
        QLAddStringHashValue(update_req, tablet_id);
        QLAddStringRangeValue(update_req, stream->id());
        cdc_table->AddStringColumnValue(
            update_req, master::kCdcCheckpoint, OpId::Max().ToString());
        auto* condition = update_req->mutable_if_expr()->mutable_condition();
        condition->set_op(QL_OP_EXISTS);
        session->Apply(update_op);
        stream_ops.push_back(std::make_pair(stream->id(), update_op));
        LOG(INFO) << "Setting checkpoint to OpId::Max() for stream " << stream->id()
                  << " and tablet " << tablet_id << " with request "
                  << update_req->ShortDebugString();
      } else {
        // XCluster stream.
        const auto delete_op = cdc_table->NewDeleteOp();
        auto* delete_req = delete_op->mutable_request();

        QLAddStringHashValue(delete_req, tablet_id);
        QLAddStringRangeValue(delete_req, stream->id());
        session->Apply(delete_op);
        stream_ops.push_back(std::make_pair(stream->id(), delete_op));
        LOG(INFO) << "Deleting stream " << stream->id() << " for tablet " << tablet_id
                  << " with request " << delete_req->ShortDebugString();
      }
    }
  }

  if (!failure_status.ok()) {
    return STATUS_FORMAT(
        IllegalState, "Failed to scan table $0: $1", kCdcStateTableName, failure_status);
  }

  // Flush all the delete operations.
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  Status s = session->TEST_Flush();
  if (!s.ok()) {
    LOG(ERROR) << "Unable to flush operations to delete cdc streams: " << s;
    if (streams_type == cdc::CDCSDK) {
      return s.CloneAndPrepend("Error setting checkpoint to OpId::Max() in cdc_state table");
    } else {
      return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
    }
  }

  for (const auto& e : stream_ops) {
    if (!e.second->succeeded()) {
      LOG(WARNING) << "Error deleting cdc_state row with tablet id "
                   << e.second->request().hashed_column_values(0).value().string_value()
                   << " and stream id "
                   << e.second->request().range_column_values(0).value().string_value()
                   << ": " << e.second->response().status();
      failed_streams.insert(e.first);
    }
  }

  std::vector<CDCStreamInfo::WriteLock> locks;
  locks.reserve(streams.size() - failed_streams.size());
  std::vector<CDCStreamInfo*> streams_to_delete;
  streams_to_delete.reserve(streams.size() - failed_streams.size());

  // Delete from sys catalog only those streams that were successfully deleted from cdc_state.
  for (auto& stream : streams) {
    if (failed_streams.find(stream->id()) == failed_streams.end()) {
      locks.push_back(stream->LockForWrite());
      streams_to_delete.push_back(stream.get());
    }
  }

  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Delete(leader_ready_term(), streams_to_delete),
      "deleting CDC streams from sys-catalog"));
  LOG(INFO) << "Successfully deleted streams " << JoinStreamsCSVLine(streams_to_delete)
            << " from sys catalog";

  // Remove it from the map.
  TRACE("Removing from CDC stream maps");
  {
    LockGuard lock(mutex_);
    for (const auto& stream : streams_to_delete) {
      if (cdc_stream_map_.erase(stream->id()) < 1) {
        return STATUS(IllegalState, "Could not remove CDC stream from map", stream->id());
      }
      for (auto& id : stream->table_id()) {
        xcluster_producer_tables_to_stream_map_[id].erase(stream->id());
        cdcsdk_tables_to_stream_map_[id].erase(stream->id());
      }
    }
  }
  LOG(INFO) << "Successfully deleted streams " << JoinStreamsCSVLine(streams_to_delete)
            << " from stream map";

  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::GetCDCStream(const GetCDCStreamRequestPB* req,
                                    GetCDCStreamResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_stream_id()) {
    return STATUS(InvalidArgument, "CDC Stream ID must be provided", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->stream_id());
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(NotFound, "Could not find CDC stream", req->ShortDebugString(),
                  MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForRead();

  CDCStreamInfoPB* stream_info = resp->mutable_stream();

  stream_info->set_stream_id(stream->id());
  std::string id_type_option_value(cdc::kTableId);

  for (auto option : stream_lock->options()) {
    if (option.has_key() && option.key() == cdc::kIdType)
      id_type_option_value = option.value();
  }

  if (id_type_option_value == cdc::kNamespaceId) {
    stream_info->set_namespace_id(stream_lock->namespace_id());
  }

  for (auto& table_id : stream_lock->table_id()) {
    stream_info->add_table_id(table_id);
  }

  stream_info->mutable_options()->CopyFrom(stream_lock->options());

  if (stream_lock->pb.has_state() && id_type_option_value == cdc::kNamespaceId) {
    auto state_option = stream_info->add_options();
    state_option->set_key(cdc::kStreamState);
    state_option->set_value(master::SysCDCStreamEntryPB::State_Name(stream_lock->pb.state()));
  }

  return Status::OK();
}

Status CatalogManager::GetCDCDBStreamInfo(const GetCDCDBStreamInfoRequestPB* req,
                                          GetCDCDBStreamInfoResponsePB* resp) {

  if (!req->has_db_stream_id()) {
    return STATUS(InvalidArgument, "CDC DB Stream ID must be provided", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->db_stream_id());
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(NotFound, "Could not find CDC stream", req->ShortDebugString(),
                  MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForRead();

  if (!stream->namespace_id().empty()) {
    resp->set_namespace_id(stream->namespace_id());
  }

  for (const auto& table_id : stream_lock->table_id()) {
    const auto table_info = resp->add_table_info();
    table_info->set_stream_id(req->db_stream_id());
    table_info->set_table_id(table_id);
  }

  return Status::OK();
}

Status CatalogManager::ListCDCStreams(const ListCDCStreamsRequestPB* req,
                                      ListCDCStreamsResponsePB* resp) {

  scoped_refptr<TableInfo> table;
  bool filter_table = req->has_table_id();
  if (filter_table) {
    table = VERIFY_RESULT(FindTableById(req->table_id()));
  }

  SharedLock lock(mutex_);

  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    bool skip_stream = false;
    bool id_type_option_present = false;

    // if the request is to list the DB streams of a specific namespace then the other namespaces
    // should not be considered
    if (req->has_namespace_id() && (req->namespace_id() != entry.second->namespace_id())) {
      continue;
    }

    if (filter_table &&
        entry.second->table_id().size() > 0 &&
        table->id() != entry.second->table_id().Get(0)) {
      continue; // Skip deleting/deleted streams and streams from other tables.
    }

    auto ltm = entry.second->LockForRead();

    if (ltm->is_deleting()) {
      continue;
    }

    for (const auto& option : ltm->options()) {
      if (option.key() == cdc::kIdType) {
        id_type_option_present = true;
        if (req->has_id_type()) {
          if (req->id_type() == IdTypePB::NAMESPACE_ID &&
              option.value() != cdc::kNamespaceId) {
            skip_stream = true;
            break;
          }
          if (req->id_type() == IdTypePB::TABLE_ID &&
              option.value() == cdc::kNamespaceId) {
            skip_stream = true;
            break;
          }
        }
      }
    }

    if ((!id_type_option_present && req->id_type() == IdTypePB::NAMESPACE_ID) ||
        skip_stream)
      continue;

    CDCStreamInfoPB* stream = resp->add_streams();
    stream->set_stream_id(entry.second->id());
    for (const auto& table_id : ltm->table_id()) {
      stream->add_table_id(table_id);
    }
    stream->mutable_options()->CopyFrom(ltm->options());
    // Also add an option for the current state.
    if (ltm->pb.has_state()) {
      auto state_option = stream->add_options();
      state_option->set_key("state");
      state_option->set_value(master::SysCDCStreamEntryPB::State_Name(ltm->pb.state()));
    }
  }
  return Status::OK();
}

bool CatalogManager::CDCStreamExistsUnlocked(const CDCStreamId& stream_id) {
  scoped_refptr<CDCStreamInfo> stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return false;
  }
  return true;
}

Status CatalogManager::UpdateCDCStreams(
    const std::vector<CDCStreamId>& stream_ids,
    const std::vector<yb::master::SysCDCStreamEntryPB>& update_entries) {
  RSTATUS_DCHECK(stream_ids.size() > 0, InvalidArgument, "No stream ID provided.");
  RSTATUS_DCHECK(stream_ids.size() == update_entries.size(), InvalidArgument,
                 "Mismatched number of stream IDs and update entries provided.");

  // Map CDCStreamId to (CDCStreamInfo, SysCDCStreamEntryPB). CDCStreamId is sorted in
  // increasing order in the map.
  std::map<CDCStreamId, std::pair<scoped_refptr<CDCStreamInfo>,
                                  yb::master::SysCDCStreamEntryPB>> id_to_update_infos;
  {
    SharedLock lock(mutex_);
    for (size_t i = 0; i < stream_ids.size(); i++) {
      auto stream_id = stream_ids[i];
      auto entry = update_entries[i];
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
      if (stream == nullptr) {
        return STATUS(NotFound, "Could not find CDC stream", stream_id,
                      MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      id_to_update_infos[stream_id] = {stream, entry};
    }
  }

  // Acquire CDCStreamInfo::WriteLock in increasing order of CDCStreamId to avoid deadlock.
  std::vector<CDCStreamInfo::WriteLock> stream_locks;
  std::vector<scoped_refptr<CDCStreamInfo>> streams_to_update;
  stream_locks.reserve(stream_ids.size());
  streams_to_update.reserve(stream_ids.size());
  for (const auto& id_to_update_info : id_to_update_infos) {
    auto stream = id_to_update_info.second.first;
    auto entry = id_to_update_info.second.second;

    stream_locks.push_back(stream->LockForWrite());
    auto& stream_lock = stream_locks.back();
    if (stream_lock->is_deleting()) {
      return STATUS(NotFound, "CDC stream has been deleted", stream->id(),
                    MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    stream_lock.mutable_data()->pb.CopyFrom(entry);
    streams_to_update.push_back(stream);
  }

  // First persist changes in sys catalog, then commit changes in the order of lock acquiring.
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), streams_to_update));
  for (auto& stream_lock : stream_locks) {
    stream_lock.Commit();
  }

  return Status::OK();
}

Status CatalogManager::UpdateCDCStream(const UpdateCDCStreamRequestPB *req,
                                       UpdateCDCStreamResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  LOG(INFO) << "UpdateCDCStream from " << RequestorString(rpc)
            << ": " << req->DebugString();

  std::vector<CDCStreamId> stream_ids;
  std::vector<yb::master::SysCDCStreamEntryPB> update_entries;
  stream_ids.reserve(req->streams_size() > 0 ? req->streams_size() : 1);
  update_entries.reserve(req->streams_size() > 0 ? req->streams_size() : 1);

  if (req->streams_size() == 0) {
    // Support backwards compatibility for single stream update.
    if (!req->has_stream_id()) {
      return STATUS(InvalidArgument, "Stream ID must be provided",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    if (!req->has_entry()) {
      return STATUS(InvalidArgument, "CDC Stream Entry must be provided",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    stream_ids.push_back(req->stream_id());
    update_entries.push_back(req->entry());
  } else {
    // Process batch update.
    for (const auto& stream : req->streams()) {
      stream_ids.push_back(stream.stream_id());
      update_entries.push_back(stream.entry());
    }
  }

  RETURN_NOT_OK(UpdateCDCStreams(stream_ids, update_entries));
  return Status::OK();
}

// Query if Bootstrapping is required for a CDC stream (e.g. Are we missing logs).
Status CatalogManager::IsBootstrapRequired(const IsBootstrapRequiredRequestPB* req,
                                           IsBootstrapRequiredResponsePB* resp,
                                           rpc::RpcContext* rpc) {
  LOG(INFO) << "IsBootstrapRequired from " << RequestorString(rpc) << ": " << req->DebugString();
  RSTATUS_DCHECK(req->table_ids_size() > 0, InvalidArgument, "Table ID required");
  RSTATUS_DCHECK(req->stream_ids_size() == 0 || req->stream_ids_size() == req->table_ids_size(),
                 InvalidArgument, "Stream ID optional, but must match table IDs if specified");
  bool streams_given = req->stream_ids_size() > 0;
  CoarseTimePoint deadline = rpc->GetClientDeadline();

  // To be updated by asynchronous callbacks. All these variables are allocated on the heap
  // because we could short-circuit and go out of scope while callbacks are still on the fly.
  auto data_lock = std::make_shared<std::mutex>();
  auto table_bootstrap_required = std::make_shared<std::unordered_map<TableId, bool>>();

  // For thread joining. See XClusterAsyncPromiseCallback.
  auto promise = std::make_shared<std::promise<Status>>();
  auto future = promise->get_future();
  auto task_completed = std::make_shared<bool>(false); // Protected by data_lock.
  auto finished_tasks = std::make_shared<size_t>(0); // Protected by data_lock.
  size_t total_tasks = req->table_ids_size();

  for (int t = 0; t < req->table_ids_size(); t++) {
    auto table_id = req->table_ids(t);
    auto stream_id = streams_given ? req->stream_ids(t) : "";

    // TODO: Submit the task to a thread pool.
    // Capture everything by value to increase their refcounts.
    scoped_refptr<Thread> async_task;
    RETURN_NOT_OK(Thread::Create(
        "catalog_manager_ent", "is_bootstrap_required",
        [this, table_id, stream_id, deadline, data_lock, task_completed, table_bootstrap_required,
         finished_tasks, total_tasks, promise] {
          bool bootstrap_required = false;
          auto status =
              IsTableBootstrapRequired(table_id, stream_id, deadline, &bootstrap_required);
          std::lock_guard lock(*data_lock);
          if (*task_completed) {
            return;  // Prevent calling set_value below twice.
          }
          (*table_bootstrap_required)[table_id] = bootstrap_required;
          if (!status.ok() || ++(*finished_tasks) == total_tasks) {
            // Short-circuit if error already encountered.
            *task_completed = true;
            promise->set_value(status);
          }
        },
        &async_task));
  }

  // Wait until the first promise is raised, and prepare response.
  if (future.wait_until(deadline) == std::future_status::timeout) {
    return SetupError(resp->mutable_error(),
        STATUS(TimedOut, "Timed out waiting for IsTableBootstrapRequired to finish"));
  }
  RETURN_NOT_OK(future.get());
  for (const auto& table_bool : *table_bootstrap_required) {
    auto new_result = resp->add_results();
    new_result->set_table_id(table_bool.first);
    new_result->set_bootstrap_required(table_bool.second);
  }
  return Status::OK();
}

Status CatalogManager::GetUDTypeMetadata(
    const GetUDTypeMetadataRequestPB* req, GetUDTypeMetadataResponsePB* resp,
    rpc::RpcContext* rpc) {
  auto namespace_info = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);
  uint32_t database_oid;
  {
    namespace_info->LockForRead();
    RSTATUS_DCHECK_EQ(
        namespace_info->database_type(), YQL_DATABASE_PGSQL, InternalError,
        Format("Expected YSQL database, got: $0", namespace_info->database_type()));
    database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_info->id()));
  }
  if (req->pg_enum_info()) {
    std::unordered_map<uint32_t, string> enum_oid_label_map;
    if (req->has_pg_type_oid()) {
      enum_oid_label_map =
          VERIFY_RESULT(sys_catalog_->ReadPgEnum(database_oid, req->pg_type_oid()));
    } else {
      enum_oid_label_map = VERIFY_RESULT(sys_catalog_->ReadPgEnum(database_oid));
    }
    for (const auto& [oid, label] : enum_oid_label_map) {
      PgEnumInfoPB* pg_enum_info_pb = resp->add_enums();
      pg_enum_info_pb->set_oid(oid);
      pg_enum_info_pb->set_label(label);
    }
  } else if (req->pg_composite_info()) {
    RelTypeOIDMap reltype_oid_map;
    if (req->has_pg_type_oid()) {
      reltype_oid_map = VERIFY_RESULT(
          sys_catalog_->ReadCompositeTypeFromPgClass(database_oid, req->pg_type_oid()));
    } else {
      reltype_oid_map = VERIFY_RESULT(sys_catalog_->ReadCompositeTypeFromPgClass(database_oid));
    }

    std::vector<uint32_t> table_oids;
    for (const auto& [reltype, oid] : reltype_oid_map) {
      table_oids.push_back(oid);
    }

    sort(table_oids.begin(), table_oids.end());

    RelIdToAttributesMap attributes_map =
        VERIFY_RESULT(sys_catalog_->ReadPgAttributeInfo(database_oid, table_oids));

    for (const auto& [reltype, oid] : reltype_oid_map) {
      if (attributes_map.find(oid) != attributes_map.end()) {
        PgCompositeInfoPB* pg_composite_info_pb = resp->add_composites();
        pg_composite_info_pb->set_oid(reltype);
        for (auto const& attribute : attributes_map[oid]) {
          *(pg_composite_info_pb->add_attributes()) = attribute;
        }
      } else {
        LOG_WITH_FUNC(INFO) << "No attributes found for attrelid: " << oid
                            << " corresponding to composite type of id: " << reltype;
      }
    }
  }
  return Status::OK();
}

Result<scoped_refptr<UniverseReplicationInfo>>
CatalogManager::CreateUniverseReplicationInfoForProducer(
    const std::string& producer_id,
    const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
    const google::protobuf::RepeatedPtrField<std::string>& table_ids) {
  scoped_refptr<UniverseReplicationInfo> ri;
  {
    TRACE("Acquired catalog manager lock");
    SharedLock lock(mutex_);

    if (FindPtrOrNull(universe_replication_map_, producer_id) != nullptr) {
      return STATUS(InvalidArgument, "Producer already present", producer_id,
                    MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  // Create an entry in the system catalog DocDB for this new universe replication.
  ri = new UniverseReplicationInfo(producer_id);
  ri->mutable_metadata()->StartMutation();
  SysUniverseReplicationEntryPB *metadata = &ri->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_producer_id(producer_id);
  metadata->mutable_producer_master_addresses()->CopyFrom(master_addresses);
  metadata->mutable_tables()->CopyFrom(table_ids);
  metadata->set_state(SysUniverseReplicationEntryPB::INITIALIZING);

  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_->Upsert(leader_ready_term(), ri),
      "inserting universe replication info into sys-catalog"));

  TRACE("Wrote universe replication info to sys-catalog");
  // Commit the in-memory state now that it's added to the persistent catalog.
  ri->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Setup universe replication from producer " << ri->ToString();

  {
    LockGuard lock(mutex_);
    universe_replication_map_[ri->id()] = ri;
  }
  return ri;
}

/*
 * UniverseReplication is setup in 4 stages within the Catalog Manager
 * 1. SetupUniverseReplication: Validates user input & requests Producer schema.
 * 2. GetTableSchemaCallback:   Validates Schema compatibility & requests Producer CDC init.
 * 3. AddCDCStreamToUniverseAndInitConsumer:  Setup RPC connections for CDC Streaming
 * 4. InitCDCConsumer:          Initializes the Consumer settings to begin tailing data
 */
Status CatalogManager::SetupUniverseReplication(const SetupUniverseReplicationRequestPB* req,
                                                SetupUniverseReplicationResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  // Sanity checking section.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (GetAtomicFlag(&FLAGS_enable_replicate_transaction_status_table) &&
      req->producer_id() == kSystemXClusterReplicationId) {
     return STATUS(InvalidArgument, Format("Producer universe ID cannot equal reserved string $0.",
                                           kSystemXClusterReplicationId) ,
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_master_addresses_size() <= 0) {
    return STATUS(InvalidArgument, "Producer master address must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_bootstrap_ids().size() > 0 &&
      req->producer_bootstrap_ids().size() != req->producer_table_ids().size()) {
    return STATUS(InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    auto l = ClusterConfig()->LockForRead();
    if (l->pb.cluster_uuid() == req->producer_id()) {
      return STATUS(InvalidArgument, "The request UUID and cluster UUID are identical.",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  {
    auto request_master_addresses = req->producer_master_addresses();
    std::vector<ServerEntryPB> cluster_master_addresses;
    RETURN_NOT_OK(master_->ListMasters(&cluster_master_addresses));
    for (const auto &req_elem : request_master_addresses) {
      for (const auto &cluster_elem : cluster_master_addresses) {
        if (cluster_elem.has_registration()) {
          auto p_rpc_addresses = cluster_elem.registration().private_rpc_addresses();
          for (const auto &p_rpc_elem : p_rpc_addresses) {
            if (req_elem.host() == p_rpc_elem.host() && req_elem.port() == p_rpc_elem.port()) {
              return STATUS(InvalidArgument,
                "Duplicate between request master addresses and private RPC addresses",
                req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }

          auto broadcast_addresses = cluster_elem.registration().broadcast_addresses();
          for (const auto &bc_elem : broadcast_addresses) {
            if (req_elem.host() == bc_elem.host() && req_elem.port() == bc_elem.port()) {
              return STATUS(InvalidArgument,
                "Duplicate between request master addresses and broadcast addresses",
                req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }
        }
      }
    }
  }

  std::unordered_map<TableId, std::string> table_id_to_bootstrap_id;

  if (req->producer_bootstrap_ids_size() > 0) {
    for (int i = 0; i < req->producer_table_ids().size(); i++) {
      table_id_to_bootstrap_id[req->producer_table_ids(i)] = req->producer_bootstrap_ids(i);
    }
  }

  // We assume that the list of table ids is unique.
  if (req->producer_bootstrap_ids().size() > 0 &&
      implicit_cast<size_t>(req->producer_table_ids().size()) != table_id_to_bootstrap_id.size()) {
    return STATUS(InvalidArgument, "When providing bootstrap ids, "
                  "the list of tables must be unique", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  auto ri = VERIFY_RESULT(CreateUniverseReplicationInfoForProducer(
      req->producer_id(), req->producer_master_addresses(), req->producer_table_ids()));

  // Initialize the CDC Stream by querying the Producer server for RPC sanity checks.
  auto result = ri->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
  if (!result.ok()) {
    MarkUniverseReplicationFailed(ri, ResultToStatus(result));
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, result.status());
  }
  std::shared_ptr<CDCRpcTasks> cdc_rpc = *result;

  // For each table, run an async RPC task to verify a sufficient Producer:Consumer schema match.
  for (int i = 0; i < req->producer_table_ids_size(); i++) {

    // SETUP CONTINUES after this async call.
    Status s;
    if (IsColocatedDbParentTableId(req->producer_table_ids(i))) {
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = cdc_rpc->client()->GetColocatedTabletSchemaByParentTableId(
          req->producer_table_ids(i), tables_info,
          Bind(&enterprise::CatalogManager::GetColocatedTabletSchemaCallback, Unretained(this),
               ri->id(), tables_info, table_id_to_bootstrap_id));
    } else if (IsTablegroupParentTableId(req->producer_table_ids(i))) {
      auto tablegroup_id = GetTablegroupIdFromParentTableId(req->producer_table_ids(i));
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = cdc_rpc->client()->GetTablegroupSchemaById(
          tablegroup_id, tables_info,
          Bind(&enterprise::CatalogManager::GetTablegroupSchemaCallback, Unretained(this),
               ri->id(), tables_info, tablegroup_id, table_id_to_bootstrap_id));
    } else {
      auto table_info = std::make_shared<client::YBTableInfo>();
      s = cdc_rpc->client()->GetTableSchemaById(
          req->producer_table_ids(i), table_info,
          Bind(&enterprise::CatalogManager::GetTableSchemaCallback, Unretained(this),
               ri->id(), table_info, table_id_to_bootstrap_id));
    }

    if (!s.ok()) {
      MarkUniverseReplicationFailed(ri, s);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
  }

  LOG(INFO) << "Started schema validation for universe replication " << ri->ToString();

  // Call setup on the transaction status table if it doesn't already exist.
  if (GetAtomicFlag(&FLAGS_enable_replicate_transaction_status_table) &&
      FindPtrOrNull(universe_replication_map_, kSystemXClusterReplicationId) == nullptr) {
    // Create an entry in the system catalog DocDB for this new universe replication.
    auto system_ri = VERIFY_RESULT(CreateUniverseReplicationInfoForProducer(
        kSystemXClusterReplicationId, req->producer_master_addresses(), {}));
    auto table_info = std::make_shared<client::YBTableInfo>();
    auto transaction_status_table = client::YBTableName(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    Status s = cdc_rpc->client()->GetYBTableInfo(transaction_status_table, table_info,
              Bind(&enterprise::CatalogManager::GetTableSchemaCallback, Unretained(this),
                    system_ri->id(), table_info, table_id_to_bootstrap_id));
    if (!s.ok()) {
      MarkUniverseReplicationFailed(system_ri, s);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }

    LOG(INFO) << "Started schema validation for universe replication " << system_ri->ToString();
  }
  return Status::OK();
}

void CatalogManager::MarkUniverseReplicationFailed(
    scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status) {
  auto l = universe->LockForWrite();
  if (l->pb.state() == SysUniverseReplicationEntryPB::DELETED) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED_ERROR);
  } else {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
  }

  universe->SetSetupUniverseReplicationErrorStatus(failure_status);

  // Update sys_catalog.
  const Status s = sys_catalog_->Upsert(leader_ready_term(), universe);

  l.CommitOrWarn(s, "updating universe replication info in sys-catalog");
}

Status CatalogManager::ValidateTableSchema(
    const std::shared_ptr<client::YBTableInfo>& info,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
    GetTableSchemaResponsePB* resp) {
  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB req;

  auto* table = req.mutable_table();
  table->set_table_name(info->table_name.table_name());
  table->mutable_namespace_()->set_name(info->table_name.namespace_name());
  table->mutable_namespace_()->set_database_type(
      GetDatabaseTypeForTable(client::ClientToPBTableType(info->table_type)));

  // Since YSQL tables are not present in table map, we first need to list tables to get the table
  // ID and then get table schema.
  // Remove this once table maps are fixed for YSQL.
  ListTablesRequestPB list_req;
  ListTablesResponsePB list_resp;

  list_req.set_name_filter(info->table_name.table_name());
  Status status = ListTables(&list_req, &list_resp);
  SCHECK(status.ok() && !list_resp.has_error(), NotFound,
         Substitute("Error while listing table: $0", status.ToString()));

  const auto& source_schema = client::internal::GetSchema(info->schema);
  bool is_ysql_table = info->table_type == client::YBTableType::PGSQL_TABLE_TYPE;
  for (const auto& t : list_resp.tables()) {
    // Check that table name and namespace both match.
    if (t.name() != info->table_name.table_name() ||
        t.namespace_().name() != info->table_name.namespace_name()) {
      continue;
    }

    // Check that schema name matches for YSQL tables, if the field is empty, fill in that
    // information during GetTableSchema call later.
    bool has_valid_pgschema_name = !t.pgschema_name().empty();
    if (is_ysql_table && has_valid_pgschema_name &&
        t.pgschema_name() != source_schema.SchemaName()) {
      continue;
    }

    // Get the table schema.
    table->set_table_id(t.id());
    status = GetTableSchema(&req, resp);
    SCHECK(status.ok() && !resp->has_error(), NotFound,
           Substitute("Error while getting table schema: $0", status.ToString()));

    // Double-check schema name here if the previous check was skipped.
    if (is_ysql_table && !has_valid_pgschema_name) {
      std::string target_schema_name = resp->schema().pgschema_name();
      if (target_schema_name != source_schema.SchemaName()) {
        table->clear_table_id();
        continue;
      }
    }

    // Verify that the table on the target side supports replication.
    if (is_ysql_table && t.has_relation_type() && t.relation_type() == MATVIEW_TABLE_RELATION) {
      return STATUS_FORMAT(NotSupported,
          "Replication is not supported for materialized view: $0",
          info->table_name.ToString());
    }

    Schema consumer_schema;
    auto result = SchemaFromPB(resp->schema(), &consumer_schema);

    // We now have a table match. Validate the schema.
    SCHECK(result.ok() && consumer_schema.EquivalentForDataCopy(source_schema), IllegalState,
           Substitute("Source and target schemas don't match: "
                      "Source: $0, Target: $1, Source schema: $2, Target schema: $3",
               info->table_id, resp->identifier().table_id(),
               info->schema.ToString(), resp->schema().DebugString()));
    break;
  }

  SCHECK(table->has_table_id(), NotFound, Substitute(
      "Could not find matching table for $0$1", info->table_name.ToString(),
      (is_ysql_table ? " pgschema_name: " + source_schema.SchemaName() : "")));

  // Still need to make map of table id to resp table id (to add to validated map)
  // For colocated tables, only add the parent table since we only added the parent table to the
  // original pb (we use the number of tables in the pb to determine when validation is done).
  if (info->colocated) {
    // We require that colocated tables have the same colocation ID.
    //
    // Backward compatibility: tables created prior to #7378 use YSQL table OID as a colocation ID.
    auto source_clc_id = info->schema.has_colocation_id()
        ? info->schema.colocation_id()
        : CHECK_RESULT(GetPgsqlTableOid(info->table_id));
    auto target_clc_id = (resp->schema().has_colocated_table_id() &&
                          resp->schema().colocated_table_id().has_colocation_id())
        ? resp->schema().colocated_table_id().colocation_id()
        : CHECK_RESULT(GetPgsqlTableOid(resp->identifier().table_id()));
    SCHECK(source_clc_id == target_clc_id, IllegalState,
           Substitute("Source and target colocation IDs don't match for colocated table: "
                      "Source: $0, Target: $1, Source colocation ID: $2, Target colocation ID: $3",
                      info->table_id, resp->identifier().table_id(), source_clc_id, target_clc_id));
  }

  {
    SharedLock lock(mutex_);
    if (xcluster_consumer_tables_to_stream_map_.contains(table->table_id())) {
      return STATUS(IllegalState, "N:1 replication topology not supported");
    }
  }

  return Status::OK();
}

Result<RemoteTabletServer *> CatalogManager::GetLeaderTServer(
    client::internal::RemoteTabletPtr tablet) {
  auto ts = tablet->LeaderTServer();
  if (ts == nullptr) {
    return STATUS(NotFound, "Tablet leader not found for tablet", tablet->tablet_id());
  }
  return ts;
}

Status CatalogManager::IsBootstrapRequiredOnProducer(
    scoped_refptr<UniverseReplicationInfo> universe, const TableId& producer_table,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids) {
  if (!FLAGS_check_bootstrap_required) {
    return Status::OK();
  }
  auto master_addresses = universe->LockForRead()->pb.producer_master_addresses();
  std::string bootstrap_id;
  if (table_bootstrap_ids.count(producer_table) > 0) {
    bootstrap_id = table_bootstrap_ids.at(producer_table);
  }

  auto cdc_rpc = VERIFY_RESULT(universe->GetOrCreateCDCRpcTasks(master_addresses));
  if (VERIFY_RESULT(cdc_rpc->client()->IsBootstrapRequired({producer_table}, bootstrap_id))) {
    return STATUS(IllegalState, Substitute(
      "Error Missing Data in Logs. Bootstrap is required for producer $0", universe->id()));
  }
  return Status::OK();
}

Status CatalogManager::IsTableBootstrapRequired(
    const TableId& table_id,
    const CDCStreamId& stream_id,
    CoarseTimePoint deadline,
    bool* const bootstrap_required) {
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(table_id));
  RSTATUS_DCHECK(table != nullptr, NotFound, "Table ID not found: " + table_id);

  // Make a batch call for IsBootstrapRequired on every relevant TServer.
  std::map<std::shared_ptr<cdc::CDCServiceProxy>, cdc::IsBootstrapRequiredRequestPB>
      proxy_to_request;
  for (const auto& tablet : table->GetTablets()) {
    auto ts = VERIFY_RESULT(tablet->GetLeader());
    std::shared_ptr<cdc::CDCServiceProxy> proxy;
    RETURN_NOT_OK(ts->GetProxy(&proxy));
    proxy_to_request[proxy].add_tablet_ids(tablet->id());
  }

  // TODO: Make the RPCs async and parallel.
  *bootstrap_required = false;
  for (auto& proxy_request : proxy_to_request) {
    auto& tablet_req = proxy_request.second;
    cdc::IsBootstrapRequiredResponsePB tablet_resp;
    rpc::RpcController rpc;
    rpc.set_deadline(deadline);
    if (!stream_id.empty()) {
      tablet_req.set_stream_id(stream_id);
    }
    auto& cdc_service = proxy_request.first;

    RETURN_NOT_OK(cdc_service->IsBootstrapRequired(tablet_req, &tablet_resp, &rpc));
    if (tablet_resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(tablet_resp.error().status()));
    } else if (tablet_resp.has_bootstrap_required() &&
               tablet_resp.bootstrap_required()) {
      *bootstrap_required = true;
      break;
    }
  }

  return Status::OK();
}


Status CatalogManager::AddValidatedTableAndCreateCdcStreams(
      scoped_refptr<UniverseReplicationInfo> universe,
      const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
      const TableId& producer_table,
      const TableId& consumer_table) {
  auto l = universe->LockForWrite();
  if (universe->id() == kSystemXClusterReplicationId) {
    // We do this because the target doesn't know the source txn status table id until this
    // callback.
    *(l.mutable_data()->pb.add_tables()) = producer_table;
  }
  auto master_addresses = l->pb.producer_master_addresses();

  auto res = universe->GetOrCreateCDCRpcTasks(master_addresses);
  if (!res.ok()) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
    const Status s = sys_catalog_->Upsert(leader_ready_term(), universe);
    if (!s.ok()) {
      return CheckStatus(s, "updating universe replication info in sys-catalog");
    }
    l.Commit();
    return STATUS(InternalError,
        Substitute("Error while setting up client for producer $0", universe->id()));
  }
  std::shared_ptr<CDCRpcTasks> cdc_rpc = *res;
  vector<TableId> validated_tables;

  if (l->is_deleted_or_failed()) {
    // Nothing to do since universe is being deleted.
    return STATUS(Aborted, "Universe is being deleted");
  }

  auto map = l.mutable_data()->pb.mutable_validated_tables();
  (*map)[producer_table] = consumer_table;

  // Now, all tables are validated.
  if (l.mutable_data()->pb.validated_tables_size() == l.mutable_data()->pb.tables_size()) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::VALIDATED);
    auto tbl_iter = l->pb.tables();
    validated_tables.insert(validated_tables.begin(), tbl_iter.begin(), tbl_iter.end());
  }

  // TODO: end of config validation should be where SetupUniverseReplication exits back to user
  LOG(INFO) << "UpdateItem in AddValidatedTable";

  // Update sys_catalog.
  Status status = sys_catalog_->Upsert(leader_ready_term(), universe);
  if (!status.ok()) {
    LOG(ERROR) << "Error during UpdateItem: " << status;
    return CheckStatus(status, "updating universe replication info in sys-catalog");
  }
  l.Commit();

  // Create CDC stream for each validated table, after persisting the replication state change.
  if (!validated_tables.empty()) {
    std::unordered_map<std::string, std::string> options;
    options.reserve(4);
    options.emplace(cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));
    options.emplace(cdc::kRecordFormat, CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));
    options.emplace(cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));
    options.emplace(cdc::kCheckpointType, CDCCheckpointType_Name(cdc::CDCCheckpointType::IMPLICIT));

    // Keep track of the bootstrap_id, table_id, and options of streams to update after
    // the last GetCDCStreamCallback finishes. Will be updated by multiple async
    // GetCDCStreamCallback.
    auto stream_update_infos = std::make_shared<StreamUpdateInfos>();
    stream_update_infos->reserve(validated_tables.size());
    auto update_infos_lock = std::make_shared<std::mutex>();

    for (const auto& table : validated_tables) {
      string producer_bootstrap_id;
      auto it = table_bootstrap_ids.find(table);
      if (it != table_bootstrap_ids.end()) {
        producer_bootstrap_id = it->second;
      }
      if (!producer_bootstrap_id.empty()) {
        auto table_id = std::make_shared<TableId>();
        auto stream_options = std::make_shared<std::unordered_map<std::string, std::string>>();
        cdc_rpc->client()->GetCDCStream(producer_bootstrap_id, table_id, stream_options,
            std::bind(&enterprise::CatalogManager::GetCDCStreamCallback, this,
                producer_bootstrap_id, table_id, stream_options, universe->id(), table, cdc_rpc,
                std::placeholders::_1, stream_update_infos, update_infos_lock));
      } else {
        cdc_rpc->client()->CreateCDCStream(
            table, options,
            std::bind(&enterprise::CatalogManager::AddCDCStreamToUniverseAndInitConsumer, this,
                universe->id(), table, std::placeholders::_1, nullptr /* on_success_cb */));
      }
    }
  }
  return Status::OK();
}

void CatalogManager::GetTableSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<client::YBTableInfo>& info,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids, const Status& s) {
  // First get the universe.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
    LOG(ERROR) << "Error getting schema for table " << info->table_id << ": " << s;
    return;
  }

  // Validate the table schema.
  GetTableSchemaResponsePB resp;
  Status status = ValidateTableSchema(info, table_bootstrap_ids, &resp);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while validating table schema for table " << info->table_id
               << ": " << status;
    return;
  }

  if (universe->id() != kSystemXClusterReplicationId) {
    // There is no bootstrap mechanism for the transaction status table, so don't check whether
    // it is required.
    status = IsBootstrapRequiredOnProducer(universe, info->table_id, table_bootstrap_ids);
  }

  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table " << info->table_id
               << ": " << status;
  }

  status = AddValidatedTableAndCreateCdcStreams(universe,
                                                table_bootstrap_ids,
                                                info->table_id,
                                                resp.identifier().table_id());
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: " << info->table_id
               << ": " << status;
    return;
  }
}

void CatalogManager::GetTablegroupSchemaCallback(
    const std::string& universe_id,
    const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const TablegroupId& producer_tablegroup_id,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
    const Status& s) {
  // First get the universe.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
    std::ostringstream oss;
    for (size_t i = 0; i < infos->size(); ++i) {
      oss << ((i == 0) ? "" : ", ") << (*infos)[i].table_id;
    }
    LOG(ERROR) << "Error getting schema for tables: [ " << oss.str() << " ]: " << s;
    return;
  }

  if (infos->empty()) {
    LOG(WARNING) << "Received empty list of tables to validate: " << s;
    return;
  }

  // validated_consumer_tables contains the table IDs corresponding to that
  // from the producer tables.
  std::unordered_set<TableId> validated_consumer_tables;
  for (const auto& info : *infos) {
    // Validate each of the member table in the tablegroup.
    GetTableSchemaResponsePB resp;
    Status table_status = ValidateTableSchema(std::make_shared<client::YBTableInfo>(info),
                                              table_bootstrap_ids,
                                              &resp);

    if (!table_status.ok()) {
      MarkUniverseReplicationFailed(universe, table_status);
      LOG(ERROR) << "Found error while validating table schema for table " << info.table_id
                 << ": " << table_status;
      return;
    }

    validated_consumer_tables.insert(resp.identifier().table_id());
  }

  // Get the consumer tablegroup ID. Since this call is expensive (one needs to reverse lookup
  // the tablegroup ID from table ID), we only do this call once and do validation afterward.
  TablegroupId consumer_tablegroup_id;
  {
    SharedLock lock(mutex_);
    const auto* tablegroup = tablegroup_manager_->FindByTable(*validated_consumer_tables.begin());
    if (!tablegroup) {
      std::string message =
          Format("No consumer tablegroup found for producer tablegroup: $0",
                 producer_tablegroup_id);
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }
    consumer_tablegroup_id = tablegroup->id();
  }

  // tables_in_consumer_tablegroup are the tables listed within the consumer_tablegroup_id.
  // We need validated_consumer_tables and tables_in_consumer_tablegroup to be identical.
  std::unordered_set<TableId> tables_in_consumer_tablegroup;
  {
    GetTablegroupSchemaRequestPB req;
    GetTablegroupSchemaResponsePB resp;
    req.mutable_tablegroup()->set_id(consumer_tablegroup_id);
    Status status = GetTablegroupSchema(&req, &resp);
    if (!status.ok() || resp.has_error()) {
      std::string message = Format("Error when getting consumer tablegroup schema: $0",
                                   consumer_tablegroup_id);
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }

    for (const auto& info : resp.get_table_schema_response_pbs()) {
      tables_in_consumer_tablegroup.insert(info.identifier().table_id());
    }
  }

  if (validated_consumer_tables != tables_in_consumer_tablegroup) {
    std::ostringstream validated_tables_oss;
    for (auto it = validated_consumer_tables.begin();
        it != validated_consumer_tables.end(); it++) {
      validated_tables_oss << (it == validated_consumer_tables.begin() ? "" : ",") << *it;
    }
    std::ostringstream consumer_tables_oss;
    for (auto it = tables_in_consumer_tablegroup.begin();
        it != tables_in_consumer_tablegroup.end(); it++) {
      consumer_tables_oss << (it == tables_in_consumer_tablegroup.begin() ? "" : ",") << *it;
    }

    std::string message =
        Format("Mismatch between tables associated with producer tablegroup $0 and "
               "tables in consumer tablegroup $1: ($2) vs ($3).",
               producer_tablegroup_id, consumer_tablegroup_id,
               validated_tables_oss.str(), consumer_tables_oss.str());
    MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
    LOG(ERROR) << message;
    return;
  }

  Status status = IsBootstrapRequiredOnProducer(universe,
                                                producer_tablegroup_id, table_bootstrap_ids);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table "
               << producer_tablegroup_id << ": " << status;
  }

  const auto consumer_parent_table_id = GetTablegroupParentTableId(consumer_tablegroup_id);
  {
    SharedLock lock(mutex_);
    if (xcluster_consumer_tables_to_stream_map_.contains(consumer_parent_table_id)) {
      std::string message = "N:1 replication topology not supported";
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }
  }

  status = AddValidatedTableAndCreateCdcStreams(
      universe,
      table_bootstrap_ids,
      GetTablegroupParentTableId(producer_tablegroup_id),
      consumer_parent_table_id);
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << producer_tablegroup_id << ": " << status;
    return;
  }
}

void CatalogManager::GetColocatedTabletSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids, const Status& s) {
  // First get the universe.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
    std::ostringstream oss;
    for (size_t i = 0; i < infos->size(); ++i) {
      oss << ((i == 0) ? "" : ", ") << (*infos)[i].table_id;
    }
    LOG(ERROR) << "Error getting schema for tables: [ " << oss.str() << " ]: " << s;
    return;
  }

  if (infos->empty()) {
    LOG(WARNING) << "Received empty list of tables to validate: " << s;
    return;
  }

  // Validate table schemas.
  std::unordered_set<TableId> producer_parent_table_ids;
  std::unordered_set<TableId> consumer_parent_table_ids;
  for (const auto& info : *infos) {
    // Verify that we have a colocated table.
    if (!info.colocated) {
      MarkUniverseReplicationFailed(universe,
          STATUS(InvalidArgument, Substitute("Received non-colocated table: $0", info.table_id)));
      LOG(ERROR) << "Received non-colocated table: " << info.table_id;
      return;
    }
    // Validate each table, and get the parent colocated table id for the consumer.
    GetTableSchemaResponsePB resp;
    Status table_status = ValidateTableSchema(std::make_shared<client::YBTableInfo>(info),
                                              table_bootstrap_ids,
                                              &resp);
    if (!table_status.ok()) {
      MarkUniverseReplicationFailed(universe, table_status);
      LOG(ERROR) << "Found error while validating table schema for table " << info.table_id
                 << ": " << table_status;
      return;
    }
    // Store the parent table ids.
    producer_parent_table_ids.insert(
        GetColocatedDbParentTableId(info.table_name.namespace_id()));
    consumer_parent_table_ids.insert(
        GetColocatedDbParentTableId(resp.identifier().namespace_().id()));
  }

  // Verify that we only found one producer and one consumer colocated parent table id.
  if (producer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = producer_parent_table_ids.begin(); it != producer_parent_table_ids.end(); ++it) {
      oss << ((it == producer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument,
        Substitute("Found incorrect number of producer colocated parent table ids. "
                   "Expected 1, but found: [ $0 ]", oss.str())));
    LOG(ERROR) << "Found incorrect number of producer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
  }
  if (consumer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = consumer_parent_table_ids.begin(); it != consumer_parent_table_ids.end(); ++it) {
      oss << ((it == consumer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument,
        Substitute("Found incorrect number of consumer colocated parent table ids. "
                   "Expected 1, but found: [ $0 ]", oss.str())));
    LOG(ERROR) << "Found incorrect number of consumer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
  }

  Status status = IsBootstrapRequiredOnProducer(universe, *producer_parent_table_ids.begin(),
                                                table_bootstrap_ids);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table "
               << *producer_parent_table_ids.begin() << ": " << status;
  }

  {
    SharedLock lock(mutex_);
    if (xcluster_consumer_tables_to_stream_map_.contains(*consumer_parent_table_ids.begin())) {
      std::string message = "N:1 replication topology not supported";
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }
  }

  status = AddValidatedTableAndCreateCdcStreams(universe,
                                                table_bootstrap_ids,
                                                *producer_parent_table_ids.begin(),
                                                *consumer_parent_table_ids.begin());
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << *producer_parent_table_ids.begin() << ": " << status;
    return;
  }
}

void CatalogManager::GetCDCStreamCallback(
    const CDCStreamId& bootstrap_id,
    std::shared_ptr<TableId> table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options,
    const std::string& universe_id,
    const TableId& table,
    std::shared_ptr<CDCRpcTasks> cdc_rpc,
    const Status& s,
    std::shared_ptr<StreamUpdateInfos> stream_update_infos,
    std::shared_ptr<std::mutex> update_infos_lock) {
  if (!s.ok()) {
    LOG(ERROR) << "Unable to find bootstrap id " << bootstrap_id;
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, s);
  } else {
    if (*table_id != table) {
      const Status invalid_bootstrap_id_status = STATUS_FORMAT(
          InvalidArgument, "Invalid bootstrap id for table $0. Bootstrap id $1 belongs to table $2",
          table, bootstrap_id, *table_id);
      LOG(ERROR) << invalid_bootstrap_id_status;
      AddCDCStreamToUniverseAndInitConsumer(universe_id, table, invalid_bootstrap_id_status);
      return;
    }
    // todo check options
    {
      std::lock_guard<std::mutex> lock(*update_infos_lock);
      stream_update_infos->push_back({bootstrap_id, *table_id, *options});
    }
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, bootstrap_id,
        [&] () {
          // Extra callback on universe setup success - update the producer to let it know that
          // the bootstrapping is complete. This callback will only be called once among all
          // the GetCDCStreamCallback calls, and we update all streams in batch at once.
          std::lock_guard<std::mutex> lock(*update_infos_lock);

          std::vector<CDCStreamId> update_bootstrap_ids;
          std::vector<SysCDCStreamEntryPB> update_entries;
          for (const auto& update_info : *stream_update_infos) {
            auto update_bootstrap_id = std::get<0>(update_info);
            auto update_table_id = std::get<1>(update_info);
            auto update_options = std::get<2>(update_info);
            SysCDCStreamEntryPB new_entry;
            new_entry.add_table_id(update_table_id);
            new_entry.mutable_options()->Reserve(narrow_cast<int>(update_options.size()));
            for (const auto& option : update_options) {
              auto new_option = new_entry.add_options();
              new_option->set_key(option.first);
              new_option->set_value(option.second);
            }
            new_entry.set_state(master::SysCDCStreamEntryPB::ACTIVE);

            update_bootstrap_ids.push_back(update_bootstrap_id);
            update_entries.push_back(new_entry);
          }
          WARN_NOT_OK(cdc_rpc->client()->UpdateCDCStream(update_bootstrap_ids, update_entries),
                      "Unable to update CDC stream options");
          stream_update_infos->clear();
      });
  }
}

void CatalogManager::AddCDCStreamToUniverseAndInitConsumer(
    const std::string& universe_id, const TableId& table_id, const Result<CDCStreamId>& stream_id,
    std::function<void()> on_success_cb) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << universe_id;
      return;
    }
  }

  if (!stream_id.ok()) {
    LOG(ERROR) << "Error setting up CDC stream for table " << table_id;
    MarkUniverseReplicationFailed(universe, ResultToStatus(stream_id));
    return;
  }

  bool merge_alter = false;
  bool validated_all_tables = false;
  std::vector<CDCConsumerStreamInfo> consumer_info;
  {
    auto l = universe->LockForWrite();
    if (l->is_deleted_or_failed()) {
      // Nothing to do if universe is being deleted.
      return;
    }

    auto map = l.mutable_data()->pb.mutable_table_streams();
    (*map)[table_id] = *stream_id;

    // This functions as a barrier: waiting for the last RPC call from GetTableSchemaCallback.
    if (l.mutable_data()->pb.table_streams_size() == l->pb.tables_size()) {
      // All tables successfully validated! Register CDC consumers & start replication.
      validated_all_tables = true;
      LOG(INFO) << "Registering CDC consumers for universe " << universe->id();

      auto& validated_tables = l->pb.validated_tables();

      consumer_info.reserve(l->pb.tables_size());
      for (const auto& table : validated_tables) {
        CDCConsumerStreamInfo info;
        info.producer_table_id = table.first;
        info.consumer_table_id = table.second;
        info.stream_id = (*map)[info.producer_table_id];
        consumer_info.push_back(info);
      }

      std::vector<HostPort> hp;
      HostPortsFromPBs(l->pb.producer_master_addresses(), &hp);

      auto cdc_rpc_tasks_result =
          universe->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
      if (!cdc_rpc_tasks_result.ok()) {
        LOG(WARNING) << "CDC streams won't be created: " << cdc_rpc_tasks_result;
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
      } else {
        auto cdc_rpc_tasks = *cdc_rpc_tasks_result;
        Status s = InitCDCConsumer(
            consumer_info, HostPort::ToCommaSeparatedString(hp), l->pb.producer_id(),
            cdc_rpc_tasks);
        if (!s.ok()) {
          LOG(ERROR) << "Error registering subscriber: " << s;
          l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
        } else {
          if (cdc::IsAlterReplicationUniverseId(universe->id())) {
            // Don't enable ALTER universes, merge them into the main universe instead.
            merge_alter = true;
          } else {
            l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
          }
        }
      }
    }

    // Update sys_catalog with new producer table id info.
    Status status = sys_catalog_->Upsert(leader_ready_term(), universe);

    // Before committing, run any callbacks on success.
    if (status.ok() && on_success_cb &&
        (l.mutable_data()->pb.state() == SysUniverseReplicationEntryPB::ACTIVE || merge_alter)) {
      on_success_cb();
    }

    l.CommitOrWarn(status, "updating universe replication info in sys-catalog");
  }

  if (validated_all_tables) {
    string final_id = cdc::GetOriginalReplicationUniverseId(universe->id());
    // If this is an 'alter', merge back into primary command now that setup is a success.
    if (merge_alter) {
      MergeUniverseReplication(universe, final_id);
    }
    // Update the in-memory cache of consumer tables.
    LockGuard lock(mutex_);
    for (const auto& info : consumer_info) {
      auto c_table_id = info.consumer_table_id;
      auto c_stream_id = info.stream_id;
      xcluster_consumer_tables_to_stream_map_[c_table_id].emplace(final_id, c_stream_id);
    }
  }
}

/*
 * UpdateXClusterConsumerOnTabletSplit updates the consumer -> producer tablet mapping after a local
 * tablet split.
 */
Status CatalogManager::UpdateXClusterConsumerOnTabletSplit(
    const TableId& consumer_table_id,
    const SplitTabletIds& split_tablet_ids) {
  // Check if this table is consuming a stream.
  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(consumer_table_id);
  if (stream_infos.empty()) {
    return Status::OK();
  }

  auto consumer_tablet_keys = VERIFY_RESULT(GetTableKeyRanges(consumer_table_id));
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  for (const auto& stream_info : stream_infos) {
    std::string universe_id = stream_info.first;
    CDCStreamId stream_id = stream_info.second;
    // Fetch the stream entry so we can update the mappings.
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*producer_map, universe_id);
    // If we can't find the entries, then the stream has been deleted.
    if (!producer_entry) {
      LOG(WARNING) << "Unable to find the producer entry for universe " << universe_id;
      continue;
    }
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id);
    if (!stream_entry) {
      LOG(WARNING) << "Unable to find the producer entry for universe " << universe_id
                   << ", stream " << stream_id;
      continue;
    }
    DCHECK(stream_entry->consumer_table_id() == consumer_table_id);

    RETURN_NOT_OK(
        UpdateTabletMappingOnConsumerSplit(consumer_tablet_keys, split_tablet_ids, stream_entry));
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers.
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
                            "Updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::UpdateCDCProducerOnTabletSplit(
    const TableId& producer_table_id,
    const SplitTabletIds& split_tablet_ids) {
  // First check if this table has any streams associated with it.
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  std::vector<scoped_refptr<CDCStreamInfo>> cdcsdk_streams;
  {
    SharedLock lock(mutex_);
    streams = FindCDCStreamsForTableUnlocked(producer_table_id, cdc::XCLUSTER);
    cdcsdk_streams = FindCDCStreamsForTableUnlocked(producer_table_id, cdc::CDCSDK);
    // Combine cdcsdk streams and xcluster streams into a single vector: 'streams'.
    streams.insert(std::end(streams), std::begin(cdcsdk_streams), std::end(cdcsdk_streams));
  }

  if (!streams.empty()) {
    // For each stream, need to add in the children entries to the cdc_state table.
    client::TableHandle cdc_table;
    const client::YBTableName cdc_state_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    auto ybclient = master_->cdc_state_client_initializer().client();
    if (!ybclient) {
      return STATUS(IllegalState, "Client not initialized or shutting down");
    }
    RETURN_NOT_OK(cdc_table.Open(cdc_state_table_name, ybclient));
    std::shared_ptr<client::YBSession> session = ybclient->NewSession();

    for (const auto& stream : streams) {
      bool is_cdcsdk_stream =
          std::find(cdcsdk_streams.begin(), cdcsdk_streams.end(), stream) != cdcsdk_streams.end();

      for (const auto& child_tablet_id :
           {split_tablet_ids.children.first, split_tablet_ids.children.second}) {
        // Insert children entries into cdc_state now, set the opid to 0.0 and the timestamp to
        // NULL. When we process the parent's SPLIT_OP in GetChanges, we will update the opid to
        // the SPLIT_OP so that the children pollers continue from the next records. When we process
        // the first GetChanges for the children, then their timestamp value will be set. We use
        // this information to know that the children has been polled for. Once both children have
        // been polled for, then we can delete the parent tablet via the bg task
        // DoProcessXClusterParentTabletDeletion.
        const auto insert_op = cdc_table.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
        auto* insert_req = insert_op->mutable_request();
        auto* const condition = insert_req->mutable_if_expr()->mutable_condition();
        condition->set_op(QL_OP_NOT_EXISTS);
        QLAddStringHashValue(insert_req, child_tablet_id);
        QLAddStringRangeValue(insert_req, stream->id());
        cdc_table.AddStringColumnValue(insert_req, master::kCdcCheckpoint, OpId().ToString());
        if (is_cdcsdk_stream) {
          auto last_active_time = GetCurrentTimeMicros();
          auto column_id = cdc_table.ColumnId(master::kCdcData);
          auto map_value_pb = client::AddMapColumn(insert_req, column_id);
          client::AddMapEntryToColumn(
              map_value_pb, "active_time", std::to_string(last_active_time));
          client::AddMapEntryToColumn(
              map_value_pb, "cdc_sdk_safe_time", std::to_string(last_active_time));
        }
        session->Apply(insert_op);
      }
    }
    // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
    RETURN_NOT_OK(session->TEST_Flush());
  }

  return Status::OK();
}

Status CatalogManager::InitCDCConsumer(
    const std::vector<CDCConsumerStreamInfo>& consumer_info,
    const std::string& master_addrs,
    const std::string& producer_universe_uuid,
    std::shared_ptr<CDCRpcTasks> cdc_rpc_tasks) {

  // Get the tablets in the consumer table.
  cdc::ProducerEntryPB producer_entry;
  for (const auto& stream_info : consumer_info) {
    auto consumer_tablet_keys = VERIFY_RESULT(GetTableKeyRanges(stream_info.consumer_table_id));
    auto schema_version = VERIFY_RESULT(GetTableSchemaVersion(stream_info.consumer_table_id));

    cdc::StreamEntryPB stream_entry;
    // Get producer tablets and map them to the consumer tablets
    RETURN_NOT_OK(InitCDCStream(
        stream_info.producer_table_id, stream_info.consumer_table_id, consumer_tablet_keys,
        &stream_entry, cdc_rpc_tasks));
    // Set the validated consumer schema version
    auto* producer_schema_pb = stream_entry.mutable_producer_schema();
    producer_schema_pb->set_last_compatible_consumer_schema_version(schema_version);
    (*producer_entry.mutable_stream_map())[stream_info.stream_id] = std::move(stream_entry);
  }

  // Log the Network topology of the Producer Cluster
  auto master_addrs_list = StringSplit(master_addrs, ',');
  producer_entry.mutable_master_addrs()->Reserve(narrow_cast<int>(master_addrs_list.size()));
  for (const auto& addr : master_addrs_list) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, 0));
    HostPortToPB(hp, producer_entry.add_master_addrs());
  }

  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto* consumer_registry = l.mutable_data()->pb.mutable_consumer_registry();
  if (consumer_registry->producer_map().empty()) {
    // There are no active streams, so use --enable_replicate_transaction_status_table to determine
    // whether replication of the transaction status table is enabled.
    consumer_registry->set_enable_replicate_transaction_status_table(
       GetAtomicFlag(&FLAGS_enable_replicate_transaction_status_table));
  }

  auto* producer_map = consumer_registry->mutable_producer_map();
  auto it = producer_map->find(producer_universe_uuid);
  if (it != producer_map->end()) {
    return STATUS(InvalidArgument, "Already created a consumer for this universe");
  }

  // TServers will use the ClusterConfig to create CDC Consumers for applicable local tablets.
  (*producer_map)[producer_universe_uuid] = std::move(producer_entry);
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

void CatalogManager::MergeUniverseReplication(scoped_refptr<UniverseReplicationInfo> universe,
                                              std::string original_id) {
  // Merge back into primary command now that setup is a success.
  LOG(INFO) << "Merging CDC universe: " << universe->id() << " into " << original_id;

  scoped_refptr<UniverseReplicationInfo> original_universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    original_universe = FindPtrOrNull(universe_replication_map_, original_id);
    if (original_universe == nullptr) {
      LOG(ERROR) << "Universe not found: " << original_id;
      return;
    }
  }

  {
    auto cluster_config = ClusterConfig();
    // Acquire Locks in order of Original Universe, Cluster Config, New Universe
    auto original_lock = original_universe->LockForWrite();
    auto alter_lock = universe->LockForWrite();
    auto cl = cluster_config->LockForWrite();

    // Merge Cluster Config for TServers.
    auto pm = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto original_producer_entry = pm->find(original_universe->id());
    auto alter_producer_entry = pm->find(universe->id());
    if (original_producer_entry != pm->end() && alter_producer_entry != pm->end()) {
      // Merge the Tables from the Alter into the original.
      auto as = alter_producer_entry->second.stream_map();
      original_producer_entry->second.mutable_stream_map()->insert(as.begin(), as.end());
      // Delete the Alter
      pm->erase(alter_producer_entry);
    } else {
      LOG(WARNING) << "Could not find both universes in Cluster Config: " << universe->id();
    }
    cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);

    // Merge Master Config on Consumer. (no need for Producer changes, since it uses stream_id)
    // Merge Table->StreamID mapping.
    auto at = alter_lock.mutable_data()->pb.mutable_tables();
    original_lock.mutable_data()->pb.mutable_tables()->MergeFrom(*at);
    at->Clear();
    auto as = alter_lock.mutable_data()->pb.mutable_table_streams();
    original_lock.mutable_data()->pb.mutable_table_streams()->insert(as->begin(), as->end());
    as->clear();
    auto av = alter_lock.mutable_data()->pb.mutable_validated_tables();
    original_lock.mutable_data()->pb.mutable_validated_tables()->insert(av->begin(), av->end());
    av->clear();
    alter_lock.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

    if (PREDICT_FALSE(FLAGS_TEST_exit_unfinished_merging)) {
      // Exit for texting services
      return;
    }

    {
      // Need both these updates to be atomic.
      auto w = sys_catalog_->NewWriter(leader_ready_term());
      auto s = w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE,
                         original_universe.get(),
                         universe.get(),
                         cluster_config.get());
      s = CheckStatus(
          sys_catalog_->SyncWrite(w.get()),
          "Updating universe replication entries and cluster config in sys-catalog");
    }
      alter_lock.Commit();
      cl.Commit();
      original_lock.Commit();
  }

  // Add alter temp universe to GC.
  {
    LockGuard lock(mutex_);
    universes_to_clear_.push_back(universe->id());
  }

  LOG(INFO) << "Done with Merging " << universe->id() << " into " << original_universe->id();

  CreateXClusterSafeTimeTableAndStartService();
}

Status ReturnErrorOrAddWarning(const Status& s,
                               bool ignore_errors,
                               DeleteUniverseReplicationResponsePB* resp) {
  if (!s.ok()) {
    if (ignore_errors) {
      // Continue executing, save the status as a warning.
      AppStatusPB* warning = resp->add_warnings();
      StatusToPB(s, warning);
      return Status::OK();
    }
    return s.CloneAndAppend("\nUse 'ignore-errors' to ignore this error.");
  }
  return s;
}

Status CatalogManager::DeleteUniverseReplication(const std::string& producer_id,
                                                 bool ignore_errors,
                                                 DeleteUniverseReplicationResponsePB* resp) {
  scoped_refptr<UniverseReplicationInfo> ri;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    ri = FindPtrOrNull(universe_replication_map_, producer_id);
    if (ri == nullptr) {
      return STATUS(NotFound, "Universe replication info does not exist",
                    producer_id, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  {
    auto l = ri->LockForWrite();
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETING);
    Status s = sys_catalog_->Upsert(leader_ready_term(), ri);
    RETURN_NOT_OK(
        CheckLeaderStatus(s, "Updating delete universe replication info into sys-catalog"));
    TRACE("Wrote universe replication info to sys-catalog");
    l.Commit();
  }

  auto l = ri->LockForWrite();
  l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED);

  // Delete subscribers on the Consumer Registry (removes from TServers).
  LOG(INFO) << "Deleting subscribers for producer " << producer_id;
  {
    auto cluster_config = ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(producer_id);
    if (it != producer_map->end()) {
      producer_map->erase(it);
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
          "updating cluster config in sys-catalog"));
      cl.Commit();
    }
  }

  // Delete CDC stream config on the Producer.
  if (!l->pb.table_streams().empty()) {
    auto result = ri->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
    if (!result.ok()) {
      LOG(WARNING) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
    } else {
      auto cdc_rpc = *result;
      vector<CDCStreamId> streams;
      std::unordered_map<CDCStreamId, TableId> stream_to_producer_table_id;
      for (const auto& table : l->pb.table_streams()) {
        streams.push_back(table.second);
        stream_to_producer_table_id.emplace(table.second, table.first);
      }

      DeleteCDCStreamResponsePB delete_cdc_stream_resp;
      // Set force_delete=true since we are deleting active xCluster streams.
      auto s = cdc_rpc->client()->DeleteCDCStream(streams,
                                                  true, /* force_delete */
                                                  ignore_errors /* ignore_errors */,
                                                  &delete_cdc_stream_resp);

      if (delete_cdc_stream_resp.not_found_stream_ids().size() > 0) {
        std::ostringstream missing_streams;
        for (auto it = delete_cdc_stream_resp.not_found_stream_ids().begin();
               it != delete_cdc_stream_resp.not_found_stream_ids().end();
               ++it) {
          if (it != delete_cdc_stream_resp.not_found_stream_ids().begin()) {
            missing_streams << ",";
          }
          missing_streams << *it << " (table_id: " << stream_to_producer_table_id[*it] << ")";
        }
        if (s.ok()) {
          // Returned but did not find some streams, so still need to warn the user about those.
          s = STATUS(NotFound,
                     "Could not find the following streams: [" + missing_streams.str() + "].");
        } else {
          s = s.CloneAndPrepend(
              "Could not find the following streams: [" + missing_streams.str() + "].");
        }
      }
      RETURN_NOT_OK(ReturnErrorOrAddWarning(s, ignore_errors, resp));
    }
  }

  if (PREDICT_FALSE(FLAGS_TEST_exit_unfinished_deleting)) {
      // Exit for texting services
      return Status::OK();
  }

  // Delete universe in the Universe Config.
  RETURN_NOT_OK(ReturnErrorOrAddWarning(
      DeleteUniverseReplicationUnlocked(ri), ignore_errors, resp));
  l.Commit();
  LOG(INFO) << "Processed delete universe replication of " << ri->ToString();

  // Run the safe time task as it may need to perform cleanups of it own
  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplication(const DeleteUniverseReplicationRequestPB* req,
                                                 DeleteUniverseReplicationResponsePB* resp,
                                                 rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUniverseReplication request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID required", req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  RETURN_NOT_OK(DeleteUniverseReplication(req->producer_id(), req->ignore_errors(), resp));
  bool delete_system_replication_id = false;
  {
     SharedLock lock(mutex_);
     delete_system_replication_id =
        universe_replication_map_.size() == 1 &&
        universe_replication_map_.count(kSystemXClusterReplicationId) == 1;
  }
  if (delete_system_replication_id) {
    // The only entry left in the universe replication map is the transaction status table, so
    // it can be removed.
    RETURN_NOT_OK(DeleteUniverseReplication(
        kSystemXClusterReplicationId, req->ignore_errors(), resp));
  }

  LOG(INFO) << "Successfully completed DeleteUniverseReplication request from " <<
               RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplicationUnlocked(
    scoped_refptr<UniverseReplicationInfo> universe) {
  // Assumes that caller has locked universe.
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Delete(leader_ready_term(), universe),
      Substitute("An error occurred while updating sys-catalog, universe_id: $0", universe->id()));

  // Remove it from the map.
  LockGuard lock(mutex_);
  if (universe_replication_map_.erase(universe->id()) < 1) {
    LOG(WARNING) << "Failed to remove replication info from map: universe_id: " << universe->id();
  }
  // If replication is at namespace-level, also remove from the namespace-level map.
  namespace_replication_map_.erase(universe->id());
  // Also update the mapping of consumer tables.
  for (const auto& table : universe->metadata().state().pb.validated_tables()) {
    if (xcluster_consumer_tables_to_stream_map_[table.second].erase(universe->id()) < 1) {
      LOG(WARNING) << "Failed to remove consumer table from mapping. "
                   << "table_id: " << table.second << ": universe_id: " << universe->id();
    }
    if (xcluster_consumer_tables_to_stream_map_[table.second].empty()) {
      xcluster_consumer_tables_to_stream_map_.erase(table.second);
    }
  }
  return Status::OK();
}

Status CatalogManager::ChangeXClusterRole(const ChangeXClusterRoleRequestPB* req,
                                          ChangeXClusterRoleResponsePB* resp,
                                          rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing ChangeXClusterRole request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  auto new_role = req->role();
  // Get the current role from the cluster config
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto consumer_registry = l.mutable_data()->pb.mutable_consumer_registry();
  auto current_role = consumer_registry->role();
  if (current_role == new_role) {
    return STATUS(InvalidArgument, "New role must be different than existing role",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (new_role == cdc::XClusterRole::STANDBY) {
    if (!consumer_registry->enable_replicate_transaction_status_table()) {
      return STATUS(InvalidArgument, "This universe replication does not support xCluster roles. "
                                     "Recreate all existing streams with "
                                     "--enable_replicate_transaction_status_table=true to enable "
                                     " STANDBY mode",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }
  consumer_registry->set_role(new_role);
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  // Commit the change to the consumer registry.
  RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
        "updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  LOG(INFO) << "Successfully completed ChangeXClusterRole request from "
          << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::SetUniverseReplicationEnabled(
    const SetUniverseReplicationEnabledRequestPB* req,
    SetUniverseReplicationEnabledResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing SetUniverseReplicationEnabled request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_is_enabled()) {
    return STATUS(InvalidArgument, "Must explicitly set whether to enable",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Update the Master's Universe Config with the new state.
  {
    auto l = universe->LockForWrite();
    if (l->pb.state() != SysUniverseReplicationEntryPB::DISABLED &&
        l->pb.state() != SysUniverseReplicationEntryPB::ACTIVE) {
      return STATUS(
          InvalidArgument,
          Format("Universe Replication in invalid state: $0.  Retry or Delete.",
              SysUniverseReplicationEntryPB::State_Name(l->pb.state())),
          req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    if (req->is_enabled()) {
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
    } else { // DISABLE.
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DISABLED);
    }
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), universe),
        "updating universe replication info in sys-catalog"));
    l.Commit();
  }

  // Modify the Consumer Registry, which will fan out this info to all TServers on heartbeat.
  {
    auto cluster_config = ClusterConfig();
    auto l = cluster_config->LockForWrite();
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it == producer_map->end()) {
      LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    (*it).second.set_disable_stream(!req->is_enabled());
    l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
        "updating cluster config in sys-catalog"));
    l.Commit();
  }

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::AlterUniverseReplication(const AlterUniverseReplicationRequestPB* req,
                                                AlterUniverseReplicationResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterUniverseReplication request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Verify that there is an existing Universe config
  scoped_refptr<UniverseReplicationInfo> original_ri;
  {
    SharedLock lock(mutex_);

    original_ri = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (original_ri == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Currently, config options are mutually exclusive to simplify transactionality.
  int config_count = (req->producer_master_addresses_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_remove_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_add_size() > 0 ? 1 : 0) +
                     (req->has_new_producer_universe_id() ? 1 : 0);
  if (config_count != 1) {
    return STATUS(InvalidArgument, "Only 1 Alter operation per request currently supported",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Config logic...
  if (req->producer_master_addresses_size() > 0) {
    // 'set_master_addresses'
    // TODO: Verify the input. Setup an RPC Task, ListTables, ensure same.

    {
      // 1a. Persistent Config: Update the Universe Config for Master.
      auto l = original_ri->LockForWrite();
      l.mutable_data()->pb.mutable_producer_master_addresses()->CopyFrom(
          req->producer_master_addresses());

      // 1b. Persistent Config: Update the Consumer Registry (updates TServers)
      auto cluster_config = ClusterConfig();
      auto cl = cluster_config->LockForWrite();
      auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
      auto it = producer_map->find(req->producer_id());
      if (it == producer_map->end()) {
        LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
        return STATUS(NotFound, "Could not find CDC producer universe",
                      req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      (*it).second.mutable_master_addrs()->CopyFrom(req->producer_master_addresses());
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);

      {
        // Need both these updates to be atomic.
        auto w = sys_catalog_->NewWriter(leader_ready_term());
        RETURN_NOT_OK(w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE,
                                original_ri.get(),
                                cluster_config.get()));
        RETURN_NOT_OK(CheckStatus(
            sys_catalog_->SyncWrite(w.get()),
            "Updating universe replication info and cluster config in sys-catalog"));
      }
      l.Commit();
      cl.Commit();
    }

    // 2. Memory Update: Change cdc_rpc_tasks (Master cache)
    {
      auto result = original_ri->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
      if (!result.ok()) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, result.status());
      }
    }
  } else if (req->producer_table_ids_to_remove_size() > 0) {
    // 'remove_table'
    auto it = req->producer_table_ids_to_remove();
    std::set<string> table_ids_to_remove(it.begin(), it.end());
    std::set<string> consumer_table_ids_to_remove;
    // Filter out any tables that aren't in the existing replication config.
    {
      auto l = original_ri->LockForRead();
      auto tbl_iter = l->pb.tables();
      std::set<string> existing_tables(tbl_iter.begin(), tbl_iter.end()), filtered_list;
      set_intersection(table_ids_to_remove.begin(), table_ids_to_remove.end(),
                       existing_tables.begin(), existing_tables.end(),
                       std::inserter(filtered_list, filtered_list.begin()));
      filtered_list.swap(table_ids_to_remove);
    }

    vector<CDCStreamId> streams_to_remove;

    {
      auto l = original_ri->LockForWrite();
      auto cluster_config = ClusterConfig();

      // 1. Update the Consumer Registry (removes from TServers).

      auto cl = cluster_config->LockForWrite();
      auto pm = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
      auto producer_entry = pm->find(req->producer_id());
      if (producer_entry != pm->end()) {
        // Remove the Tables Specified (not part of the key).
        auto stream_map = producer_entry->second.mutable_stream_map();
        for (auto& p : *stream_map) {
          if (table_ids_to_remove.count(p.second.producer_table_id()) > 0) {
            streams_to_remove.push_back(p.first);
            // Also fetch the consumer table ids here so we can clean the in-memory maps after.
            consumer_table_ids_to_remove.insert(p.second.consumer_table_id());
          }
        }
        if (streams_to_remove.size() == stream_map->size()) {
          // If this ends with an empty Map, disallow and force user to delete.
          LOG(WARNING) << "CDC 'remove_table' tried to remove all tables." << req->producer_id();
          return STATUS(
              InvalidArgument,
              "Cannot remove all tables with alter. Use delete_universe_replication instead.",
              req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        } else if (streams_to_remove.empty()) {
          // If this doesn't delete anything, notify the user.
          return STATUS(InvalidArgument, "Removal matched no entries.",
                        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        }
        for (auto& key : streams_to_remove) {
          stream_map->erase(stream_map->find(key));
        }
      }
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);

      // 2. Remove from Master Configs on Producer and Consumer.

      Status producer_status = Status::OK();
      if (!l->pb.table_streams().empty()) {
        // Delete Relevant Table->StreamID mappings on Consumer.
        auto table_streams = l.mutable_data()->pb.mutable_table_streams();
        auto validated_tables = l.mutable_data()->pb.mutable_validated_tables();
        for (auto& key : table_ids_to_remove) {
          table_streams->erase(table_streams->find(key));
          validated_tables->erase(validated_tables->find(key));
        }
        for (int i = 0; i < l.mutable_data()->pb.tables_size(); i++) {
          if (table_ids_to_remove.count(l.mutable_data()->pb.tables(i)) > 0) {
            l.mutable_data()->pb.mutable_tables()->DeleteSubrange(i, 1);
            --i;
          }
        }
        // Delete CDC stream config on the Producer.
        auto result = original_ri->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
        if (!result.ok()) {
          LOG(ERROR) << "Unable to create cdc rpc task. CDC streams won't be deleted: "
                     << result;
          producer_status = STATUS(InternalError, "Cannot create cdc rpc task.",
                                   req->ShortDebugString(),
                                   MasterError(MasterErrorPB::INTERNAL_ERROR));
        } else {
          producer_status = (*result)->client()->DeleteCDCStream(streams_to_remove,
                                                                 true /* force_delete */,
                                                                 req->remove_table_ignore_errors());
          if (!producer_status.ok()) {
            std::stringstream os;
            std::copy(streams_to_remove.begin(), streams_to_remove.end(),
                      std::ostream_iterator<CDCStreamId>(os, ", "));
            LOG(ERROR) << "Unable to delete CDC streams: " << os.str()
                        << " on producer due to error: " << producer_status
                        << ". Try setting the ignore-errors option.";
          }
        }
      }

      // Currently, due to the sys_catalog write below, atomicity cannot be guaranteed for
      // both producer and consumer deletion, and the atomicity of producer is compromised.
      if (!producer_status.ok()) {
        return SetupError(resp->mutable_error(), producer_status);
      }

      {
        // Need both these updates to be atomic.
        auto w = sys_catalog_->NewWriter(leader_ready_term());
        auto s = w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE,
                           original_ri.get(),
                           cluster_config.get());
        if (s.ok()) {
          s = sys_catalog_->SyncWrite(w.get());
        }
        if (!s.ok()) {
          LOG(DFATAL) << "Updating universe replication info and cluster config in sys-catalog "
                         "failed. However, the deletion of streams on the producer has been issued."
                         " Please retry the command with the ignore-errors option to make sure that"
                         " streams are deleted properly on the consumer.";
          return SetupError(resp->mutable_error(), s);
        }
      }

      l.Commit();
      cl.Commit();

      // Also remove it from the in-memory map of consumer tables.
      LockGuard lock(mutex_);
      for (const auto& table : consumer_table_ids_to_remove) {
        if (xcluster_consumer_tables_to_stream_map_[table].erase(req->producer_id()) < 1) {
          LOG(WARNING) << "Failed to remove consumer table from mapping. "
                       << "table_id: " << table << ": universe_id: " << req->producer_id();
        }
        if (xcluster_consumer_tables_to_stream_map_[table].empty()) {
          xcluster_consumer_tables_to_stream_map_.erase(table);
        }
      }
    }
  } else if (req->producer_table_ids_to_add_size() > 0) {
    // 'add_table'
    string alter_producer_id = req->producer_id() + ".ALTER";

    // If user passed in bootstrap ids, check that there is a bootstrap id for every table.
    if (req->producer_bootstrap_ids_to_add().size() > 0 &&
      req->producer_table_ids_to_add().size() != req->producer_bootstrap_ids_to_add().size()) {
      return STATUS(InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // Verify no 'alter' command running.
    scoped_refptr<UniverseReplicationInfo> alter_ri;
    {
      SharedLock lock(mutex_);
      alter_ri = FindPtrOrNull(universe_replication_map_, alter_producer_id);
    }
    {
      if (alter_ri != nullptr) {
        LOG(INFO) << "Found " << alter_producer_id << "... Removing";
        if (alter_ri->LockForRead()->is_deleted_or_failed()) {
          // Delete previous Alter if it's completed but failed.
          master::DeleteUniverseReplicationRequestPB delete_req;
          delete_req.set_producer_id(alter_ri->id());
          master::DeleteUniverseReplicationResponsePB delete_resp;
          Status s = DeleteUniverseReplication(&delete_req, &delete_resp, rpc);
          if (!s.ok()) {
            if (delete_resp.has_error()) {
              resp->mutable_error()->Swap(delete_resp.mutable_error());
              return s;
            }
            return SetupError(resp->mutable_error(), s);
          }
        } else {
          return STATUS(InvalidArgument, "Alter for CDC producer currently running",
                        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
        }
      }
    }

    // Map each table id to its corresponding bootstrap id.
    std::unordered_map<TableId, std::string> table_id_to_bootstrap_id;
    if (req->producer_bootstrap_ids_to_add().size() > 0) {
      for (int i = 0; i < req->producer_table_ids_to_add().size(); i++) {
        table_id_to_bootstrap_id[req->producer_table_ids_to_add(i)]
          = req->producer_bootstrap_ids_to_add(i);
      }

      // Ensure that table ids are unique. We need to do this here even though
      // the same check is performed by SetupUniverseReplication because
      // duplicate table ids can cause a bootstrap id entry in table_id_to_bootstrap_id
      // to be overwritten.
      if (table_id_to_bootstrap_id.size() !=
              implicit_cast<size_t>(req->producer_table_ids_to_add().size())) {
        return STATUS(InvalidArgument, "When providing bootstrap ids, "
                      "the list of tables must be unique", req->ShortDebugString(),
                      MasterError(MasterErrorPB::INVALID_REQUEST));
      }
    }

    // Only add new tables.  Ignore tables that are currently being replicated.
    auto tid_iter = req->producer_table_ids_to_add();
    std::unordered_set<string> new_tables(tid_iter.begin(), tid_iter.end());
    {
      auto l = original_ri->LockForRead();
      for(auto t : l->pb.tables()) {
        auto pos = new_tables.find(t);
        if (pos != new_tables.end()) {
          new_tables.erase(pos);
        }
      }
    }
    if (new_tables.empty()) {
      return STATUS(InvalidArgument, "CDC producer already contains all requested tables",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // 1. create an ALTER table request that mirrors the original 'setup_replication'.
    master::SetupUniverseReplicationRequestPB setup_req;
    master::SetupUniverseReplicationResponsePB setup_resp;
    setup_req.set_producer_id(alter_producer_id);
    setup_req.mutable_producer_master_addresses()->CopyFrom(
        original_ri->LockForRead()->pb.producer_master_addresses());
    for (auto t : new_tables) {
      setup_req.add_producer_table_ids(t);

      // Add bootstrap id to request if it exists.
      auto bootstrap_id_lookup_result = table_id_to_bootstrap_id.find(t);
      if (bootstrap_id_lookup_result != table_id_to_bootstrap_id.end()) {
        setup_req.add_producer_bootstrap_ids(bootstrap_id_lookup_result->second);
      }
    }

    // 2. run the 'setup_replication' pipeline on the ALTER Table
    Status s = SetupUniverseReplication(&setup_req, &setup_resp, rpc);
    if (!s.ok()) {
      if (setup_resp.has_error()) {
        resp->mutable_error()->Swap(setup_resp.mutable_error());
        return s;
      }
      return SetupError(resp->mutable_error(), s);
    }
    // NOTE: ALTER merges back into original after completion.
  } else if (req->has_new_producer_universe_id()) {
    Status s = RenameUniverseReplication(original_ri, req, resp, rpc);
    if (!s.ok()) {
      return SetupError(resp->mutable_error(), s);
    }
  }

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::RenameUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe,
    const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  const string old_universe_replication_id = universe->id();
  const string new_producer_universe_id = req->new_producer_universe_id();
  if (old_universe_replication_id == new_producer_universe_id) {
    return STATUS(InvalidArgument, "Old and new replication ids must be different",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    LockGuard lock(mutex_);
    auto l = universe->LockForWrite();
    scoped_refptr<UniverseReplicationInfo> new_ri;

    // Assert that new_replication_name isn't already in use.
    if (FindPtrOrNull(universe_replication_map_, new_producer_universe_id) != nullptr) {
      return STATUS(InvalidArgument, "New replication id is already in use",
                    req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // Since the producer_id is used as the key, we need to create a new UniverseReplicationInfo.
    new_ri = new UniverseReplicationInfo(new_producer_universe_id);
    new_ri->mutable_metadata()->StartMutation();
    SysUniverseReplicationEntryPB *metadata = &new_ri->mutable_metadata()->mutable_dirty()->pb;
    metadata->CopyFrom(l->pb);
    metadata->set_producer_id(new_producer_universe_id);

    // Also need to update internal maps.
    auto cluster_config = ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    (*producer_map)[new_producer_universe_id] =
        std::move((*producer_map)[old_universe_replication_id]);
    producer_map->erase(old_universe_replication_id);

    {
      // Need both these updates to be atomic.
      auto w = sys_catalog_->NewWriter(leader_ready_term());
      RETURN_NOT_OK(w->Mutate(QLWriteRequestPB::QL_STMT_DELETE, universe.get()));
      RETURN_NOT_OK(w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE,
                              new_ri.get(),
                              cluster_config.get()));
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->SyncWrite(w.get()),
          "Updating universe replication info and cluster config in sys-catalog"));
    }
    new_ri->mutable_metadata()->CommitMutation();
    cl.Commit();

    // Update universe_replication_map after persistent data is saved.
    universe_replication_map_[new_producer_universe_id] = new_ri;
    universe_replication_map_.erase(old_universe_replication_id);
  }

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::GetUniverseReplication(const GetUniverseReplicationRequestPB* req,
                                              GetUniverseReplicationResponsePB* resp,
                                              rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    req->ShortDebugString(), MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  resp->mutable_entry()->CopyFrom(universe->LockForRead()->pb);
  return Status::OK();
}

/*
 * Checks if the universe replication setup has completed.
 * Returns Status::OK() if this call succeeds, and uses resp->done() to determine if the setup has
 * completed (either failed or succeeded). If the setup has failed, then resp->replication_error()
 * is also set. If it succeeds, replication_error() gets set to OK.
 */
Status CatalogManager::IsSetupUniverseReplicationDone(
    const IsSetupUniverseReplicationDoneRequestPB* req,
    IsSetupUniverseReplicationDoneResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "IsSetupUniverseReplicationDone from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  bool isAlterRequest = cdc::IsAlterReplicationUniverseId(req->producer_id());

  GetUniverseReplicationRequestPB universe_req;
  GetUniverseReplicationResponsePB universe_resp;
  universe_req.set_producer_id(req->producer_id());

  auto s = GetUniverseReplication(&universe_req, &universe_resp, /* RpcContext */ nullptr);
  // If the universe was deleted, we're done.  This is normal with ALTER tmp files.
  if (s.IsNotFound()) {
    resp->set_done(true);
    if (isAlterRequest) {
      s = Status::OK();
      StatusToPB(s, resp->mutable_replication_error());
    }
    return s;
  }
  RETURN_NOT_OK(s);
  if (universe_resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(universe_resp.error().status()));
  }

  // Two cases for completion:
  //  - For a regular SetupUniverseReplication, we want to wait for the universe to become ACTIVE.
  //  - For an AlterUniverseReplication, we need to wait until the .ALTER universe gets merged with
  //    the main universe - at which point the .ALTER universe is deleted.
  auto terminal_state = isAlterRequest ? SysUniverseReplicationEntryPB::DELETED
                                       : SysUniverseReplicationEntryPB::ACTIVE;
  if (universe_resp.entry().state() == terminal_state) {
    resp->set_done(true);
    StatusToPB(Status::OK(), resp->mutable_replication_error());
    return Status::OK();
  }

  // Otherwise we have either failed (see MarkUniverseReplicationFailed), or are still working.
  if (universe_resp.entry().state() == SysUniverseReplicationEntryPB::DELETED_ERROR ||
      universe_resp.entry().state() == SysUniverseReplicationEntryPB::FAILED) {
    resp->set_done(true);

    // Get the more detailed error.
    scoped_refptr<UniverseReplicationInfo> universe;
    {
      SharedLock lock(mutex_);
      universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
      if (universe == nullptr) {
        StatusToPB(
            STATUS(InternalError, "Could not find CDC producer universe after having failed."),
            resp->mutable_replication_error());
        return Status::OK();
      }
    }
    if (!universe->GetSetupUniverseReplicationErrorStatus().ok()) {
      StatusToPB(universe->GetSetupUniverseReplicationErrorStatus(),
                 resp->mutable_replication_error());
    } else {
      LOG(WARNING) << "Did not find setup universe replication error status.";
      StatusToPB(STATUS(InternalError, "unknown error"), resp->mutable_replication_error());
    }

    // Add failed universe to GC now that we've responded to the user.
    {
      LockGuard lock(mutex_);
      universes_to_clear_.push_back(universe->id());
    }

    return Status::OK();
  }

  // Not done yet.
  resp->set_done(false);
  return Status::OK();
}

Status CatalogManager::UpdateConsumerOnProducerSplit(
    const UpdateConsumerOnProducerSplitRequestPB* req,
    UpdateConsumerOnProducerSplitResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "UpdateConsumerOnProducerSplit from " << RequestorString(rpc)
            << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(InvalidArgument, "Producer universe ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_stream_id()) {
    return STATUS(InvalidArgument, "Stream ID must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_producer_split_tablet_info()) {
    return STATUS(InvalidArgument, "Producer split tablet info must be provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto producer_entry = FindOrNull(*producer_map, req->producer_id());
  if (!producer_entry) {
    return STATUS_FORMAT(
        NotFound, "Unable to find the producer entry for universe $0", req->producer_id());
  }
  auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), req->stream_id());
  if (!stream_entry) {
    return STATUS_FORMAT(
        NotFound, "Unable to find the stream entry for universe $0, stream $1",
        req->producer_id(), req->stream_id());
  }

  SplitTabletIds split_tablet_id{
      .source = req->producer_split_tablet_info().tablet_id(),
      .children = {
          req->producer_split_tablet_info().new_tablet1_id(),
          req->producer_split_tablet_info().new_tablet2_id()}};

  auto split_key = req->producer_split_tablet_info().split_partition_key();
  auto consumer_tablet_keys = VERIFY_RESULT(GetTableKeyRanges(stream_entry->consumer_table_id()));
  bool found_source = false, found_all_split_children = false;
  RETURN_NOT_OK(UpdateTabletMappingOnProducerSplit(
      consumer_tablet_keys, split_tablet_id, split_key, &found_source, &found_all_split_children,
      stream_entry));

  if (!found_source) {
    // Did not find the source tablet, but did find the children - means that we have already
    // processed this SPLIT_OP, so for idempotency, we can return OK.
    if (found_all_split_children) {
      LOG(INFO) << "Already processed this tablet split: " << req->DebugString();
      return Status::OK();
    }

    // When there are sequential SPLIT_OPs, we may try to reprocess an older SPLIT_OP. However, if
    // one or both of those children have also already been split and processed, then we'll end up
    // here (!found_source && !found_all_split_childs).
    // This is alright, we can log a warning, and then continue (to not block later records).
    LOG(WARNING)
        << "Unable to find matching source tablet " << req->producer_split_tablet_info().tablet_id()
        << " for universe " << req->producer_id() << " stream " << req->stream_id();

    return Status::OK();
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers (and new
  // pollers are created for the new tablets).
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
                            "Updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

// Related function: PlayChangeMetadataRequest() in tablet_bootstrap.cc.
Status CatalogManager::UpdateConsumerOnProducerMetadata(
    const UpdateConsumerOnProducerMetadataRequestPB* req,
    UpdateConsumerOnProducerMetadataResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG_WITH_FUNC(INFO) << " from " << RequestorString(rpc) << ": " << req->DebugString();

  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_xcluster_skip_schema_compatibility_checks_on_alter))) {
    resp->set_should_wait(false);
    return Status::OK();
  }

  auto u_id = req->producer_id();
  auto stream_id = req->stream_id();

  // Get corresponding local data for this stream.
  std::string consumer_table_id;
  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);
    auto producer_stream_id = req->stream_id();
    auto iter = std::find_if(xcluster_consumer_tables_to_stream_map_.begin(),
        xcluster_consumer_tables_to_stream_map_.end(),
        [&u_id, &producer_stream_id](auto& id_map){
          auto consumer_stream_id = id_map.second.find(u_id);
          return (consumer_stream_id != id_map.second.end() &&
                 (*consumer_stream_id).second == producer_stream_id);
        });
    SCHECK(iter != xcluster_consumer_tables_to_stream_map_.end(),
           NotFound, Substitute("Unable to find the stream id $0", stream_id));
    consumer_table_id = iter->first;

    // The destination table should be found or created by now.
    table = FindPtrOrNull(*table_ids_map_, consumer_table_id);
  }
  SCHECK(table, NotFound, Substitute("Missing table id $0", consumer_table_id));

  // Colocated, Tablegroup schema changes are not handled yet.
  if (IsColocationParentTableId(consumer_table_id)) {
    LOG(INFO) << "XCluster Ignoring schema changes on parent colocated/tablegroup id: "
              << consumer_table_id;
    resp->set_should_wait(false);
    return Status::OK();
  }

  auto current_consumer_schema_version = VERIFY_RESULT(GetTableSchemaVersion(consumer_table_id));

  {
    // Use the stream ID to find ClusterConfig entry.
    auto cluster_config = ClusterConfig();
    auto l = cluster_config->LockForWrite();
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*producer_map, u_id);
    SCHECK(producer_entry, NotFound, Substitute("Missing universe $0", u_id));
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id);
    SCHECK(stream_entry, NotFound, Substitute("Missing universe $0, stream $1", u_id, stream_id));

    auto schema_cached = stream_entry->mutable_producer_schema();
    auto version_validated = schema_cached->validated_schema_version();

    auto& producer_meta_pb = req->producer_change_metadata_request();
    auto version_received =  producer_meta_pb.schema_version();

    if (version_validated > 0 && version_received <= version_validated) {
      LOG(INFO) << "Received known schema (v" << version_received << "). Continuing Replication.";
      resp->set_should_wait(false);
      // The first cdc poller to process a compatible schema update will cause the
      // replication to resume, but all subsequent pollers should also be sent the
      // the last compatible consumer schema version
      resp->set_last_compatible_consumer_schema_version(
          schema_cached->last_compatible_consumer_schema_version());
      return Status::OK();
    }

    // If we have a full schema, then we can do a schema comparison.
    if (producer_meta_pb.has_schema()) {
      // Grab the local Consumer schema and compare it to the Producer's schema.
      auto& producer_schema_pb = producer_meta_pb.schema();
      Schema consumer_schema, producer_schema;
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb, &producer_schema));
      RETURN_NOT_OK(table->GetSchema(&consumer_schema));

      if (consumer_schema.EquivalentForDataCopy(producer_schema)) {
        resp->set_should_wait(false);
        // NOTE: If the consumer schema is first updated, followed by the Producer
        // schema, then the replication never really halts, so we need to let the
        // pollers know about the updated consumer schema version immediately so that
        // subsequent records can use the update consumer schema version for rewriting packed rows.
        resp->set_last_compatible_consumer_schema_version(current_consumer_schema_version);

        LOG(INFO) << "Received Compatible Producer schema version: " << version_received;
        // Update the schema version if we're functionally equivalent.
        if (version_received > version_validated) {
          DCHECK(!schema_cached->has_pending_schema());
          schema_cached->set_validated_schema_version(version_received);
          schema_cached->set_last_compatible_consumer_schema_version(
              current_consumer_schema_version);
        } else {
          // Nothing to modify.  Don't write to sys catalog.
          // When would this even happen given that we already check if we have already
          // received this version
          return Status::OK();
        }
      } else {
        resp->set_should_wait(GetAtomicFlag(&FLAGS_xcluster_wait_on_ddl_alter));
        std::string error_msg =
          Format("XCluster Schema mismatch $0 \n Consumer={$1} \n Producer={$2}",
                 consumer_table_id, consumer_schema.ToString(), producer_schema.ToString());
        LOG(WARNING) << error_msg;

        WARN_NOT_OK(
          StoreReplicationErrors(
            u_id, consumer_table_id, stream_id,
            {std::make_pair(REPLICATION_SCHEMA_MISMATCH, std::move(error_msg))}),
          "Failed to store schema mismatch replication error");

        // Incompatible schema: store, wait for all tablet reports, then make the DDL change.
        auto producer_schema = stream_entry->mutable_producer_schema();
        if (!producer_schema->has_pending_schema()) {
          // Copy the schema.
          producer_schema->mutable_pending_schema()->CopyFrom(producer_schema_pb);
          producer_schema->set_pending_schema_version(version_received);
        } else {
          // Why would we be getting different schema versions across tablets? Partial apply?
          DCHECK_EQ(version_received, producer_schema->pending_schema_version());
          // If we reach here, we have already processed and persisted the pending schema
          // as a result of an update from a different tablet, so it should be sufficient to
          // send the response back of whether or not to halt replication.
          return Status::OK();
        }
      }

      // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
      l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
          "Updating cluster config in sys-catalog"));
      l.Commit();
    } else {
      resp->set_should_wait(false);
      // TODO (#14234): Support colocated tables / tablegroups.
      // Need producer_meta_pb.has_add_table(), add_multiple_tables(),
      //                       remove_table_id(), alter_table_id().
    }
  }

  return Status::OK();
}

Status CatalogManager::StoreReplicationErrors(
  const std::string& universe_id,
  const std::string& consumer_table_id,
  const std::string& stream_id,
  const std::vector<std::pair<ReplicationErrorPb, std::string>>& replication_errors) {

  SharedLock lock(mutex_);
  return StoreReplicationErrorsUnlocked(
      universe_id, consumer_table_id, stream_id, replication_errors);
}

Status CatalogManager::StoreReplicationErrorsUnlocked(
  const std::string& universe_id,
  const std::string& consumer_table_id,
  const std::string& stream_id,
  const std::vector<std::pair<ReplicationErrorPb, std::string>>& replication_errors) {

  const auto& universe = FindPtrOrNull(universe_replication_map_, universe_id);
  if (universe == nullptr) {
    return STATUS(NotFound, Format("Could not locate universe $0", universe_id),
                  MasterError(MasterErrorPB::UNKNOWN_ERROR));
  }

  for (const auto& error_kv : replication_errors) {
    const ReplicationErrorPb& replication_error = error_kv.first;
    const std::string& replication_error_detail = error_kv.second;
    universe->StoreReplicationError(
      consumer_table_id, stream_id, replication_error, replication_error_detail);
  }

  return Status::OK();
}

Status CatalogManager::ClearReplicationErrors(
  const std::string& universe_id,
  const std::string& consumer_table_id,
  const std::string& stream_id,
  const std::vector<ReplicationErrorPb>& replication_error_codes) {

  SharedLock lock(mutex_);
  return ClearReplicationErrorsUnlocked(
      universe_id, consumer_table_id, stream_id, replication_error_codes);
}

Status CatalogManager::ClearReplicationErrorsUnlocked(
  const std::string& universe_id,
  const std::string& consumer_table_id,
  const std::string& stream_id,
  const std::vector<ReplicationErrorPb>& replication_error_codes) {

  const auto& universe = FindPtrOrNull(universe_replication_map_, universe_id);
  if (universe == nullptr) {
    return STATUS(NotFound, Format("Could not locate universe $0", universe_id),
                  MasterError(MasterErrorPB::UNKNOWN_ERROR));
  }

  for (const auto& replication_error_code : replication_error_codes) {
    universe->ClearReplicationError(consumer_table_id, stream_id, replication_error_code);
  }

  return Status::OK();
}

Status CatalogManager::WaitForReplicationDrain(const WaitForReplicationDrainRequestPB *req,
                                               WaitForReplicationDrainResponsePB *resp,
                                               rpc::RpcContext *rpc) {
  LOG(INFO) << "WaitForReplicationDrain from " << RequestorString(rpc)
            << ": " << req->DebugString();
  if (req->stream_ids_size() == 0) {
    return STATUS(InvalidArgument, "No stream ID provided",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  MicrosecondsInt64 target_time = req->has_target_time()
      ? req->target_time()
      : GetCurrentTimeMicros();
  if (!req->target_time()) {
    LOG(INFO) << "WaitForReplicationDrain: target_time unspecified. Default to " << target_time;
  }

  // Find all streams to check for replication drain.
  std::unordered_set<CDCStreamId> filter_stream_ids(req->stream_ids().begin(),
                                                    req->stream_ids().end());
  std::unordered_set<CDCStreamId> found_stream_ids;
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  {
    std::vector<scoped_refptr<CDCStreamInfo>> all_streams;
    GetAllCDCStreams(&all_streams);
    for (const auto& stream : all_streams) {
      if (filter_stream_ids.find(stream->id()) == filter_stream_ids.end()) {
        continue;
      }
      streams.push_back(stream);
      found_stream_ids.insert(stream->id());
    }
  }

  // Verify that all specified stream_ids are found.
  std::ostringstream not_found_streams;
  for (const auto& stream_id : filter_stream_ids) {
    if (found_stream_ids.find(stream_id) == found_stream_ids.end()) {
      not_found_streams << stream_id << ",";
    }
  }
  if (!not_found_streams.str().empty()) {
    string stream_ids = not_found_streams.str();
    stream_ids.pop_back();  // Remove the last comma.
    return STATUS(InvalidArgument, Format("Streams not found: $0", stream_ids),
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Keep track of the drained (stream_id, tablet_id) tuples.
  std::unordered_set<StreamTabletIdPair, StreamTabletIdHash> drained_stream_tablet_ids;

  // Calculate deadline and interval for each CallReplicationDrain call to tservers.
  CoarseTimePoint deadline = rpc->GetClientDeadline();
  if (deadline == CoarseTimePoint::max()) {
    deadline = CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_master_rpc_timeout_ms);
  }
  auto timeout = MonoDelta::FromMilliseconds(
      GetAtomicFlag(&FLAGS_wait_replication_drain_retry_timeout_ms));

  while (true) {
    // 1. Construct the request to be sent to each tserver. Meanwhile, collect all tuples that
    //    are not marked as drained in previous iterations.
    std::unordered_set<StreamTabletIdPair, StreamTabletIdHash> undrained_stream_tablet_ids;
    std::unordered_map<std::shared_ptr<cdc::CDCServiceProxy>,
                       cdc::CheckReplicationDrainRequestPB> proxy_to_request;
    for (const auto& stream : streams) {
      for (const auto& table_id : stream->table_id()) {
        auto table_info = VERIFY_RESULT(FindTableById(table_id));
        RSTATUS_DCHECK(table_info != nullptr, NotFound, "Table ID not found: " + table_id);

        for (const auto& tablet : table_info->GetTablets()) {
          // (1) If tuple is marked as drained in a previous iteration, skip it.
          // (2) Otherwise, check if it is drained in the current iteration.
          if (drained_stream_tablet_ids.find({stream->id(), tablet->id()}) !=
              drained_stream_tablet_ids.end()) {
            continue;
          }
          undrained_stream_tablet_ids.insert({stream->id(), tablet->id()});

          // Update the relevant request. Skip if relevant tserver/proxy is not ready yet.
          auto ts_result = tablet->GetLeader();
          if (ts_result.ok()) {
            std::shared_ptr<cdc::CDCServiceProxy> proxy;
            auto s = (*ts_result)->GetProxy(&proxy);
            if (s.ok()) {
              auto& tablet_req = proxy_to_request[proxy];
              auto stream_info = tablet_req.add_stream_info();
              stream_info->set_stream_id(stream->id());
              stream_info->set_tablet_id(tablet->id());
            }
          }
        }
      }
    }

    // For testing tserver leadership changes.
    TEST_PAUSE_IF_FLAG(TEST_hang_wait_replication_drain);

    // 2. Call CheckReplicationDrain on each tserver.
    for (auto& proxy_request : proxy_to_request) {
      if (deadline - CoarseMonoClock::Now() <= timeout) {
        break;  // Too close to deadline.
      }
      auto& cdc_service = proxy_request.first;
      auto& tablet_req = proxy_request.second;
      tablet_req.set_target_time(target_time);
      cdc::CheckReplicationDrainResponsePB tablet_resp;
      rpc::RpcController tablet_rpc;
      tablet_rpc.set_timeout(timeout);

      Status s = cdc_service->CheckReplicationDrain(tablet_req, &tablet_resp, &tablet_rpc);
      if (!s.ok()) {
        LOG(WARNING) << "CheckReplicationDrain responded with non-ok status: " << s;
      } else if (tablet_resp.has_error()) {
        LOG(WARNING) << "CheckReplicationDrain responded with error: "
                     << tablet_resp.error().DebugString();
      } else {
        // Update the two lists of (stream ID, tablet ID) pairs.
        for (const auto& stream_info : tablet_resp.drained_stream_info()) {
          undrained_stream_tablet_ids.erase({stream_info.stream_id(), stream_info.tablet_id()});
          drained_stream_tablet_ids.insert({stream_info.stream_id(), stream_info.tablet_id()});
        }
      }
    }

    // 3. Check if all current undrained tuples are marked as drained, or it is too close
    //    to deadline. If so, prepare the response and terminate the loop.
    if (undrained_stream_tablet_ids.empty() ||
        deadline - CoarseMonoClock::Now() <= timeout * 2) {
      std::ostringstream output_stream;
      output_stream << "WaitForReplicationDrain from " << RequestorString(rpc) << " finished.";
      if (!undrained_stream_tablet_ids.empty()) {
        output_stream << " Found undrained streams:";
      }

      for (const auto& stream_tablet_id : undrained_stream_tablet_ids) {
        output_stream << "\n\tStream: " << stream_tablet_id.first
                      << ", Tablet: " << stream_tablet_id.second;
        auto undrained_stream_info = resp->add_undrained_stream_info();
        undrained_stream_info->set_stream_id(stream_tablet_id.first);
        undrained_stream_info->set_tablet_id(stream_tablet_id.second);
      }
      LOG(INFO) << output_stream.str();
      break;
    }
    SleepFor(timeout);
  }

  return Status::OK();
}

Status CatalogManager::SetupNSUniverseReplication(const SetupNSUniverseReplicationRequestPB* req,
                                                  SetupNSUniverseReplicationResponsePB* resp,
                                                  rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupNSUniverseReplication from " << RequestorString(rpc)
            << ": " << req->DebugString();

  SCHECK(req->has_producer_id() && !req->producer_id().empty(), InvalidArgument,
         "Producer universe ID must be provided");
  SCHECK(req->has_producer_ns_name() && !req->producer_ns_name().empty(), InvalidArgument,
         "Producer universe namespace name must be provided");
  SCHECK(req->has_producer_ns_type(), InvalidArgument,
         "Producer universe namespace type must be provided");
  SCHECK(req->producer_master_addresses_size() > 0, InvalidArgument,
         "Producer master address must be provided");

  std::string ns_name = req->producer_ns_name();
  YQLDatabase ns_type = req->producer_ns_type();
  switch (ns_type) {
    case YQLDatabase::YQL_DATABASE_CQL:
      break;
    case YQLDatabase::YQL_DATABASE_PGSQL:
      return STATUS(InvalidArgument,
          "YSQL not currently supported for namespace-level replication setup");
    default:
      return STATUS(InvalidArgument, Format("Unrecognized namespace type: $0", ns_type));
  }

  // 1. Find all producer tables with a name-matching consumer table. Ensure that no
  //    bootstrapping is required for these producer tables.
  std::vector<TableId> producer_tables;
  NamespaceIdentifierPB producer_namespace;
  NamespaceIdentifierPB consumer_namespace;
  // namespace_id will be filled in XClusterFindProducerConsumerOverlap.
  producer_namespace.set_name(ns_name);
  producer_namespace.set_database_type(ns_type);
  consumer_namespace.set_name(ns_name);
  consumer_namespace.set_database_type(ns_type);
  size_t num_non_matched_consumer_tables = 0;
  {
    std::vector<HostPort> hp;
    HostPortsFromPBs(req->producer_master_addresses(), &hp);
    std::string producer_addrs = HostPort::ToCommaSeparatedString(hp);
    auto cdc_rpc = VERIFY_RESULT(CDCRpcTasks::CreateWithMasterAddrs(
        req->producer_id(), producer_addrs));
    producer_tables = VERIFY_RESULT(XClusterFindProducerConsumerOverlap(
        cdc_rpc, &producer_namespace, &consumer_namespace,
        &num_non_matched_consumer_tables));

    // TODO: Remove this check after NS-level bootstrap is implemented.
    auto bootstrap_required = VERIFY_RESULT(
        cdc_rpc->client()->IsBootstrapRequired(producer_tables));
    SCHECK(!bootstrap_required, IllegalState,
           Format("Producer tables under namespace $0 require bootstrapping.", ns_name));
  }
  SCHECK(!producer_tables.empty(), NotFound, Format(
      "No producer tables under namespace $0 can be set up for replication. Please make "
      "sure that there are at least one pair of (producer, consumer) table with matching "
      "name and schema in order to initialize the namespace-level replication.", ns_name));

  // 2. Setup universe replication for these producer tables.
  {
    SetupUniverseReplicationRequestPB setup_req;
    SetupUniverseReplicationResponsePB setup_resp;
    setup_req.set_producer_id(req->producer_id());
    setup_req.mutable_producer_master_addresses()->CopyFrom(req->producer_master_addresses());
    for (const auto& tid : producer_tables) {
      setup_req.add_producer_table_ids(tid);
    }
    auto s = SetupUniverseReplication(&setup_req, &setup_resp, rpc);
    if (!s.ok()) {
      if (setup_resp.has_error()) {
        resp->mutable_error()->Swap(setup_resp.mutable_error());
        return s;
      }
      return SetupError(resp->mutable_error(), s);
    }
  }

  // 3. Wait for the universe replication setup to finish.
  // TODO: Put all the following code in an async task to avoid this expensive wait.
  CoarseTimePoint deadline = rpc->GetClientDeadline();
  auto s = WaitForSetupUniverseReplicationToFinish(req->producer_id(), deadline);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), s);
  }

  // 4. Update the persisted data.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");
    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find universe after SetupUniverseReplication",
                    req->ShortDebugString(), MasterError(MasterErrorPB::UNKNOWN_ERROR));
    }
  }
  auto l = universe->LockForWrite();
  l.mutable_data()->pb.set_is_ns_replication(true);
  l.mutable_data()->pb.mutable_producer_namespace()->CopyFrom(producer_namespace);
  l.mutable_data()->pb.mutable_consumer_namespace()->CopyFrom(consumer_namespace);
  l.Commit();

  // 5. Initialize in-memory entry and start the periodic task.
  {
    LockGuard lock(mutex_);
    auto& metadata = namespace_replication_map_[req->producer_id()];
    if (num_non_matched_consumer_tables > 0) {
      // Start the periodic sync immediately.
      metadata.next_add_table_task_time = CoarseMonoClock::Now() +
          MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_retry_secs));
    } else {
      // Delay the sync since there are currently no non-replicated consumer tables.
      metadata.next_add_table_task_time = CoarseMonoClock::Now() +
          MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_backoff_secs));
    }
  }
  namespace_replication_enabled_.store(true, std::memory_order_release);

  return Status::OK();
}

Status CatalogManager::GetReplicationStatus(
    const GetReplicationStatusRequestPB* req,
    GetReplicationStatusResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetReplicationStatus from " << RequestorString(rpc)
            << ": " << req->DebugString();

  // If the 'universe_id' is given, only populate the status for the streams in that universe.
  // Otherwise, populate all the status for all streams.
  if (!req->universe_id().empty()) {
    SharedLock lock(mutex_);
    auto universe = FindPtrOrNull(universe_replication_map_, req->universe_id());
    SCHECK(universe, InvalidArgument, Substitute("Could not find universe $0", req->universe_id()));
    PopulateUniverseReplicationStatus(*universe, resp);
  } else {
    SharedLock lock(mutex_);
    for (const auto& kv : universe_replication_map_) {
      PopulateUniverseReplicationStatus(*kv.second, resp);
    }
  }

  return Status::OK();
}

void CatalogManager::PopulateUniverseReplicationStatus(
  const UniverseReplicationInfo& universe,
  GetReplicationStatusResponsePB* resp) const {

  // Fetch the replication error map for this universe.
  auto table_replication_error_map = universe.GetReplicationErrors();

  // Populate an entry for each table/stream pair that belongs to 'universe'.
  for (const auto& table_stream : xcluster_consumer_tables_to_stream_map_) {
    const auto& table_id = table_stream.first;
    const auto& stream_map = table_stream.second;

    auto stream_map_iter = stream_map.find(universe.id());
    if (stream_map_iter == stream_map.end()) {
      continue;
    }

    const auto& stream_id = stream_map_iter->second;

    auto resp_status = resp->add_statuses();
    resp_status->set_table_id(table_id);
    resp_status->set_stream_id(stream_id);

    // Store any replication errors associated with this table/stream pair.
    auto table_error_map_iter = table_replication_error_map.find(table_id);
    if (table_error_map_iter != table_replication_error_map.end()) {
      const auto& stream_replication_error_map = table_error_map_iter->second;
      auto stream_error_map_iter = stream_replication_error_map.find(stream_id);
      if (stream_error_map_iter != stream_replication_error_map.end()) {
        const auto& error_map = stream_error_map_iter->second;
        for (const auto& error_kv : error_map) {
          const auto& error = error_kv.first;
          const auto& detail = error_kv.second;

          auto status_error = resp_status->add_errors();
          status_error->set_error(error);
          status_error->set_error_detail(detail);
        }
      }
    }
  }
}

bool CatalogManager::IsTableCdcProducer(const TableInfo& table_info) const {
  auto it = xcluster_producer_tables_to_stream_map_.find(table_info.id());
  if (it != xcluster_producer_tables_to_stream_map_.end()) {
    // Check that at least one of these streams is active (ie not being deleted).
    for (const auto& stream : it->second) {
      auto stream_it = cdc_stream_map_.find(stream);
      if (stream_it != cdc_stream_map_.end()) {
        auto s = stream_it->second->LockForRead();
        if (!s->started_deleting()) {
          return true;
        }
      }
    }
  }
  return false;
}

bool CatalogManager::IsTableCdcConsumer(const TableInfo& table_info) const {
  auto it = xcluster_consumer_tables_to_stream_map_.find(table_info.id());
  if (it == xcluster_consumer_tables_to_stream_map_.end()) {
    return false;
  }
  return !it->second.empty();
}

bool CatalogManager::IsTablePartOfCDCSDK(const TableInfo& table_info) const {
  auto it = cdcsdk_tables_to_stream_map_.find(table_info.id());
  if (it != cdcsdk_tables_to_stream_map_.end()) {
    for (const auto& stream : it->second) {
      auto stream_it = cdc_stream_map_.find(stream);
      if (stream_it != cdc_stream_map_.end()) {
        auto s = stream_it->second->LockForRead();
        if (!s->is_deleting()) {
          VLOG(1) << "Found an active CDCSDK stream: " << stream
                  << ",for table: " << table_info.id();
          return true;
        }
      }
    }
  }

  return false;
}

std::unordered_set<CDCStreamId> CatalogManager::GetCDCSDKStreamsForTable(
    const TableId& table_id) const {
  SharedLock lock(mutex_);
  auto it = cdcsdk_tables_to_stream_map_.find(table_id);
  if (it == cdcsdk_tables_to_stream_map_.end()) {
    return {};
  }
  return it->second;
}

std::unordered_set<CDCStreamId> CatalogManager::GetCdcStreamsForProducerTable(
    const TableId& table_id) const {
  SharedLock lock(mutex_);
  auto it = xcluster_producer_tables_to_stream_map_.find(table_id);
  if (it == xcluster_producer_tables_to_stream_map_.end()) {
    return {};
  }
  return it->second;
}

CatalogManager::XClusterConsumerTableStreamInfoMap
    CatalogManager::GetXClusterStreamInfoForConsumerTable(const TableId& table_id) const {
  SharedLock lock(mutex_);
  return GetXClusterStreamInfoForConsumerTableUnlocked(table_id);
}

CatalogManager::XClusterConsumerTableStreamInfoMap
    CatalogManager::GetXClusterStreamInfoForConsumerTableUnlocked(const TableId& table_id) const {
  auto it = xcluster_consumer_tables_to_stream_map_.find(table_id);
  if (it == xcluster_consumer_tables_to_stream_map_.end()) {
    return {};
  }

  return it->second;
}

bool CatalogManager::IsCdcEnabled(
    const TableInfo& table_info) const {
  SharedLock lock(mutex_);
  return IsCdcEnabledUnlocked(table_info);
}

bool CatalogManager::IsCdcEnabledUnlocked(
    const TableInfo& table_info) const {
  return IsTableCdcProducer(table_info) || IsTableCdcConsumer(table_info);
}

// This function will be replaced with IsTablePartOfCDCSDK when PR for
// tablet split will be merged: https://phabricator.dev.yugabyte.com/D18638
bool CatalogManager::IsCdcSdkEnabled(const TableInfo& table_info) {
  master::ListCDCStreamsRequestPB list_req;
  master::ListCDCStreamsResponsePB list_resp;
  list_req.set_id_type(master::IdTypePB::NAMESPACE_ID);
  list_req.set_namespace_id(table_info.namespace_id());
  RETURN_NOT_OK_RET(ListCDCStreams(&list_req, &list_resp), false);
  if (list_resp.streams().size() != 0) {
    for (auto stream : list_resp.streams()) {
      for (auto table_id : stream.table_id()) {
        if (table_id == table_info.id()) {
          return true;
        }
      }
    }
  }
  return false;
}

bool CatalogManager::IsTablePartOfBootstrappingCdcStream(const TableInfo& table_info) const {
  SharedLock lock(mutex_);
  return IsTablePartOfBootstrappingCdcStreamUnlocked(table_info);
}

bool CatalogManager::IsTablePartOfBootstrappingCdcStreamUnlocked(
    const TableInfo& table_info) const {
  auto it = xcluster_producer_tables_to_stream_map_.find(table_info.id());
  if (it != xcluster_producer_tables_to_stream_map_.end()) {
    // Check that at least one of these streams is being bootstrapped.
    for (const auto& stream : it->second) {
      auto stream_it = cdc_stream_map_.find(stream);
      if (stream_it != cdc_stream_map_.end()) {
        auto s = stream_it->second->LockForRead();
        if (s->pb.state() == SysCDCStreamEntryPB::INITIATED) {
          return true;
        }
      }
    }
  }
  return false;
}

Status CatalogManager::ValidateNewSchemaWithCdc(const TableInfo& table_info,
                                                const Schema& consumer_schema) const {
  // Check if this table is consuming a stream.
  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(table_info.id());
  if (stream_infos.empty()) {
    return Status::OK();
  }

  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForRead();
  for (const auto& stream_info : stream_infos) {
    std::string universe_id = stream_info.first;
    CDCStreamId stream_id = stream_info.second;
    // Fetch the stream entry to get Schema information.
    auto& producer_map = l.data().pb.consumer_registry().producer_map();
    auto producer_entry = FindOrNull(producer_map, universe_id);
    SCHECK(producer_entry, NotFound, Substitute("Missing universe $0", universe_id));
    auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id);
    SCHECK(stream_entry, NotFound, Substitute("Missing stream $0:$1", universe_id, stream_id));

    auto& producer_schema_pb = stream_entry->producer_schema();
    if (producer_schema_pb.has_pending_schema()) {
      // Compare the local Consumer schema to the Producer's schema.
      Schema producer_schema;
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb.pending_schema(), &producer_schema));

      // This new schema update should either make the data source copy equivalent
      // OR be a subset of the changes we need.
      bool can_apply = consumer_schema.IsSubsetOf(producer_schema) ||
                       producer_schema.IsSubsetOf(consumer_schema);
      SCHECK(can_apply, IllegalState, Substitute(
             "New Schema not compatible with XCluster Producer Schema:\n new={$0}\n producer={$1}",
             consumer_schema.ToString(), producer_schema.ToString()));
    }
  }

  return Status::OK();
}

Status CatalogManager::ResumeCdcAfterNewSchema(const TableInfo& table_info,
                                               SchemaVersion consumer_schema_version) {
  if (PREDICT_FALSE(!GetAtomicFlag(&FLAGS_xcluster_wait_on_ddl_alter))) {
    return Status::OK();
  }

  // Verify that this table is consuming a stream.
  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(table_info.id());
  if (stream_infos.empty()) {
    return Status::OK();
  }

  bool found_schema = false, resuming_replication =  false;

  // Now that we've applied the new schema: find pending replication, clear state, resume.
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  for (const auto& stream_info : stream_infos) {
    std::string u_id = stream_info.first;
    CDCStreamId stream_id = stream_info.second;
    // Fetch the stream entry to get Schema information.
    auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto producer_entry = FindOrNull(*producer_map, u_id);
    if (!producer_entry) {
      continue;
    }
    auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id);
    if (!stream_entry) {
      continue;
    }

    auto producer_schema_pb = stream_entry->mutable_producer_schema();
    if (producer_schema_pb->has_pending_schema()) {
      found_schema = true;
      Schema consumer_schema, producer_schema;
      RETURN_NOT_OK(table_info.GetSchema(&consumer_schema));
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb->pending_schema(), &producer_schema));
      if (consumer_schema.EquivalentForDataCopy(producer_schema)) {
        resuming_replication = true;
        auto pending_version = producer_schema_pb->pending_schema_version();
        LOG(INFO) << "Consumer schema @ version " << consumer_schema_version
                  << " is now data copy compatible with Producer: "
                  << stream_id << " @ schema version " << pending_version;
        // Clear meta we use to track progress on receiving all WAL entries with old schema.
        producer_schema_pb->set_validated_schema_version(
            std::max(producer_schema_pb->validated_schema_version(), pending_version));
        producer_schema_pb->set_last_compatible_consumer_schema_version(consumer_schema_version);
        producer_schema_pb->clear_pending_schema();
        // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
        l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

        WARN_NOT_OK(
          ClearReplicationErrors(
            u_id, table_info.id(), stream_id, {REPLICATION_SCHEMA_MISMATCH}),
          "Failed to store schema mismatch replication error");
      } else  {
        LOG(INFO) << "Consumer schema not compatible for data copy of next Producer schema.";
      }
    }
  }

  if (resuming_replication) {
    RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
                              "updating cluster config after Schema for CDC"));
    l.Commit();
    LOG(INFO) << "Resuming Replication on " << table_info.id() << " after Consumer ALTER.";
  } else if (!found_schema) {
    LOG(INFO) << "No pending schema change from Producer.";
  }

  return Status::OK();
}

void CatalogManager::Started() {
  super::Started();
  snapshot_coordinator_.Start();
}

Result<SnapshotSchedulesToObjectIdsMap> CatalogManager::MakeSnapshotSchedulesToObjectIdsMap(
    SysRowEntryType type) {
  return snapshot_coordinator_.MakeSnapshotSchedulesToObjectIdsMap(type);
}

Result<bool> CatalogManager::IsTableUndergoingPitrRestore(const TableInfo& table_info) {
  return snapshot_coordinator_.IsTableUndergoingPitrRestore(table_info);
}

Result<bool> CatalogManager::IsTablePartOfSomeSnapshotSchedule(const TableInfo& table_info) {
  return snapshot_coordinator_.IsTableCoveredBySomeSnapshotSchedule(table_info);
}

bool CatalogManager::IsPitrActive() {
  return snapshot_coordinator_.IsPitrActive();
}

void CatalogManager::SysCatalogLoaded(int64_t term, SysCatalogLoadingState&& state) {
  super::SysCatalogLoaded(term, std::move(state));
  snapshot_coordinator_.SysCatalogLoaded(term);
}

Result<size_t> CatalogManager::GetNumLiveTServersForActiveCluster() {
  BlacklistSet blacklist = VERIFY_RESULT(BlacklistSetFromPB());
  TSDescriptorVector ts_descs;
  auto uuid = VERIFY_RESULT(placement_uuid());
  master_->ts_manager()->GetAllLiveDescriptorsInCluster(&ts_descs, uuid, blacklist);
  return ts_descs.size();
}

Status CatalogManager::RunXClusterBgTasks() {
  // Clean up Deleted CDC Streams on the Producer.
  std::vector<scoped_refptr<CDCStreamInfo>> streams;
  WARN_NOT_OK(FindCDCStreamsMarkedAsDeleting(&streams), "Failed Finding Deleting CDC Streams");
  if (!streams.empty()) {
    WARN_NOT_OK(CleanUpDeletedCDCStreams(streams), "Failed Cleaning Deleted CDC Streams");
  }

  // Clean up Failed Universes on the Consumer.
  WARN_NOT_OK(ClearFailedUniverse(), "Failed Clearing Failed Universe");

  // DELETING_METADATA special state is used by CDC, to do CDC streams metadata cleanup from
  // cache as well as from the system catalog for the drop table scenario.
  std::vector<scoped_refptr<CDCStreamInfo>> cdcsdk_streams;
  WARN_NOT_OK(FindCDCStreamsMarkedForMetadataDeletion(&cdcsdk_streams,
              SysCDCStreamEntryPB::DELETING_METADATA), "Failed CDC Stream Metadata Deletion");
  if (!cdcsdk_streams.empty()) {
    WARN_NOT_OK(CleanUpCDCStreamsMetadata(cdcsdk_streams), "Failed Cleanup CDC Streams Metadata");
  }

  // Restart xCluster and CDCSDK parent tablet deletion bg task.
  StartCDCParentTabletDeletionTaskIfStopped();

  // Run periodic task for namespace-level replications.
  ScheduleXClusterNSReplicationAddTableTask();

  if (PREDICT_FALSE(!GetAtomicFlag(&FLAGS_xcluster_wait_on_ddl_alter))) {
    // See if any Streams are waiting on a pending_schema.
    bool found_pending_schema = false;
    auto cluster_config = ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    // For each user entry.
    for (auto& producer_id_and_entry : *producer_map) {
      // For each CDC stream in that Universe.
      for (auto& stream_id_and_entry : *producer_id_and_entry.second.mutable_stream_map()) {
        auto& stream_entry = stream_id_and_entry.second;
        if (stream_entry.has_producer_schema() &&
            stream_entry.producer_schema().has_pending_schema()) {
          // Force resume this stream.
          auto schema = stream_entry.mutable_producer_schema();
          schema->set_validated_schema_version(
              std::max(schema->validated_schema_version(), schema->pending_schema_version()));
          schema->clear_pending_schema();

          found_pending_schema = true;
          LOG(INFO) << "Force Resume Consumer schema: " << stream_id_and_entry.first
                    << " @ schema version " << schema->pending_schema_version();
        }
      }
    }

    if (found_pending_schema) {
      // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
      cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);
      RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
                    "updating cluster config after Schema for CDC"));
      cl.Commit();
    }
  }
  return Status::OK();
}

Status CatalogManager::ClearFailedUniverse() {
  // Delete a single failed universe from universes_to_clear_.
  if (PREDICT_FALSE(FLAGS_disable_universe_gc)) {
    return Status::OK();
  }

  std::string universe_id;
  {
    LockGuard lock(mutex_);

    if (universes_to_clear_.empty()) {
      return Status::OK();
    }
    // Get the first universe.  Only try once to avoid failure loops.
    universe_id = universes_to_clear_.front();
    universes_to_clear_.pop_front();
  }

  GetUniverseReplicationRequestPB universe_req;
  GetUniverseReplicationResponsePB universe_resp;
  universe_req.set_producer_id(universe_id);

  RETURN_NOT_OK(GetUniverseReplication(&universe_req, &universe_resp, /* RpcContext */ nullptr));

  DeleteUniverseReplicationRequestPB req;
  DeleteUniverseReplicationResponsePB resp;
  req.set_producer_id(universe_id);
  req.set_ignore_errors(true);

  RETURN_NOT_OK(DeleteUniverseReplication(&req, &resp, /* RpcContext */ nullptr));

  return Status::OK();
}

void CatalogManager::StartCDCParentTabletDeletionTaskIfStopped() {
  if (GetAtomicFlag(&FLAGS_cdc_parent_tablet_deletion_task_retry_secs) <= 0) {
    // Task is disabled.
    return;
  }
  const bool is_already_running = cdc_parent_tablet_deletion_task_running_.exchange(true);
  if (!is_already_running) {
    ScheduleCDCParentTabletDeletionTask();
  }
}

void CatalogManager::ScheduleCDCParentTabletDeletionTask() {
  int wait_time = GetAtomicFlag(&FLAGS_cdc_parent_tablet_deletion_task_retry_secs);
  if (wait_time <= 0) {
    // Task has been disabled.
    cdc_parent_tablet_deletion_task_running_ = false;
    return;
  }

  // Submit to run async in diff thread pool, since this involves accessing cdc_state.
  cdc_parent_tablet_deletion_task_.Schedule(
      [this](const Status& status) {
        Status s = background_tasks_thread_pool_->SubmitFunc(
            std::bind(&CatalogManager::ProcessCDCParentTabletDeletionPeriodically, this));
        if (!s.IsOk()) {
          // Failed to submit task to the thread pool. Mark that the task is now no longer running.
          LOG(WARNING) << "Failed to schedule: ProcessCDCParentTabletDeletionPeriodically";
          cdc_parent_tablet_deletion_task_running_ = false;
        }
      },
      wait_time * 1s);
}

void CatalogManager::ProcessCDCParentTabletDeletionPeriodically() {
  if (!CheckIsLeaderAndReady().IsOk()) {
    cdc_parent_tablet_deletion_task_running_ = false;
    return;
  }
  WARN_NOT_OK(
      DoProcessCDCClusterTabletDeletion(cdc::CDCSDK),
      "Failed to run DoProcessCDCClusterTabletDeletion task for CDCSDK.");
  WARN_NOT_OK(
      DoProcessCDCClusterTabletDeletion(cdc::XCLUSTER),
      "Failed to run DoProcessCDCClusterTabletDeletion task for XCLUSTER.");

  // Schedule the next iteration of the task.
  ScheduleCDCParentTabletDeletionTask();
}

Status CatalogManager::DoProcessCDCClusterTabletDeletion(
    const cdc::CDCRequestSource request_source) {
  std::unordered_map<TabletId, HiddenReplicationParentTabletInfo> hidden_tablets;
  {
    SharedLock lock(mutex_);
    hidden_tablets = (request_source == cdc::CDCSDK) ? retained_by_cdcsdk_ : retained_by_xcluster_;
  }

  if (!hidden_tablets.empty()) {
    std::unordered_set<TabletId> tablets_to_delete;

    // Check cdc_state table to see if the children tablets being polled.
    client::TableHandle cdc_state_table;
    const client::YBTableName cdc_state_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kCdcStateTableName);
    auto ybclient = master_->cdc_state_client_initializer().client();
    if (!ybclient) {
      return STATUS(IllegalState, "Client not initialized or shutting down");
    }
    RETURN_NOT_OK(cdc_state_table.Open(cdc_state_table_name, ybclient));
    std::shared_ptr<client::YBSession> session = ybclient->NewSession();

    for (auto& [tablet_id, hidden_tablet] : hidden_tablets) {
      // If our parent tablet is still around, need to process that one first.
      const auto parent_tablet_id = hidden_tablet.parent_tablet_id_;
      if (!parent_tablet_id.empty() && hidden_tablets.contains(parent_tablet_id)) {
        continue;
      }

      // For each hidden tablet, check if for each stream we have an entry in the mapping for them.
      const auto streams = (request_source == cdc::CDCSDK)
                               ? GetCDCSDKStreamsForTable(hidden_tablet.table_id_)
                               : GetCdcStreamsForProducerTable(hidden_tablet.table_id_);

      vector<CDCStreamId> streams_to_delete;
      vector<CDCStreamId> streams_already_deleted;
      for (const auto& stream : streams) {
        // Check parent entry, if it doesn't exist, then it was already deleted.
        // If the entry for the tablet does not exist, then we can go ahead with deletion of the
        // tablet.
        auto read_op = cdc_state_table.NewReadOp();
        auto* read_req = read_op->mutable_request();
        QLAddStringHashValue(read_req, tablet_id);
        auto cond = read_req->mutable_where_expr()->mutable_condition();
        cond->set_op(QLOperator::QL_OP_AND);
        QLAddStringCondition(
            cond, Schema::first_column_id() + master::kCdcStreamIdIdx, QL_OP_EQUAL, stream);
        cdc_state_table.AddColumns({master::kCdcLastReplicationTime}, read_req);
        cdc_state_table.AddColumns({master::kCdcCheckpoint}, read_req);

        // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
        RETURN_NOT_OK(session->TEST_ReadSync(read_op));
        auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();

        // This means we already deleted the entry for this stream in a previous iteration.
        if (row_block->row_count() == 0) {
          VLOG(2) << "Did not find an entry corresponding to the tablet: " << tablet_id
                  << ", and stream: " << stream << ", in the cdc_state table";
          streams_already_deleted.push_back(stream);
          continue;
        }
        if (request_source == cdc::XCLUSTER) {
          if (row_block->row(0).column(0).IsNull()) {
            // Still haven't processed this tablet since timestamp is null, no need to check
            // children.
            break;
          }
        } else if (request_source == cdc::CDCSDK) {
          // We check if there is any stream where the CDCSDK client has started streaming from the
          // hidden tablet, if not we can delete the tablet. There are two ways to verify that the
          // client has not started streaming:
          // 1. The checkpoint is -1.-1 (which is the case when a stream is bootstrapped)
          // 2. The checkpoint is 0.0 and 'CdcLastReplicationTime' is Null (when the tablet was a
          // result of a tablet split, and was added to the cdc_state table when the tablet split is
          // initiated.)
          auto checkpoint_result = OpId::FromString(row_block->row(0).column(1).string_value());
          if (checkpoint_result.ok()) {
            OpId checkpoint = *checkpoint_result;

            if (checkpoint == OpId::Invalid() ||
                (checkpoint == OpId::Min() && row_block->row(0).column(0).IsNull())) {
              VLOG(2) << "The stream: " << stream << ", is not active for tablet: " << tablet_id;
              streams_to_delete.push_back(stream);
              continue;
            }
          } else {
            LOG(WARNING) << "Read invalid op id " << row_block->row(0).column(1).string_value()
                         << " for tablet " << tablet_id << ": " << checkpoint_result.status()
                         << "from cdc_state table.";
          }
        }

        // This means there was an active stream for the source tablet. In which case if we see
        // that all children tablet entries have started streaming, we can delete the parent
        // tablet.
        bool found_all_children = true;
        for (auto& child_tablet : hidden_tablet.split_tablets_) {
          auto read_op = cdc_state_table.NewReadOp();
          auto read_req = read_op->mutable_request();
          QLAddStringHashValue(read_req, child_tablet);
          auto cond = read_req->mutable_where_expr()->mutable_condition();
          cond->set_op(QLOperator::QL_OP_AND);
          QLAddStringCondition(
              cond, Schema::first_column_id() + master::kCdcStreamIdIdx, QL_OP_EQUAL, stream);
          cdc_state_table.AddColumns({master::kCdcLastReplicationTime}, read_req);
          // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
          RETURN_NOT_OK(session->TEST_ReadSync(read_op));
          row_block = ql::RowsResult(read_op.get()).GetRowBlock();

          if (row_block->row_count() < 1) {
            // This tablet stream still hasn't been found yet.
            found_all_children = false;
            break;
          }

          const auto& last_replicated_time = row_block->row(0).column(0);
          // Check checkpoint to ensure that there has been a poll for this tablet, or if the
          // split has been reported.
          if (last_replicated_time.IsNull()) {
            // No poll yet, so do not delete the parent tablet for now.
            VLOG(2) << "The stream: " << stream
                    << ", has not started polling for the child tablet: " << child_tablet
                    << ".Hence we will not delete the hidden parent tablet: " << tablet_id;
            found_all_children = false;
            break;
          }
        }
        if (found_all_children) {
          streams_to_delete.push_back(stream);
        }
      }

      // Also delete the parent tablet from cdc_state for all completed streams.
      for (const auto& stream : streams_to_delete) {
        const auto delete_op = cdc_state_table.NewDeleteOp();
        auto* delete_req = delete_op->mutable_request();

        QLAddStringHashValue(delete_req, tablet_id);
        QLAddStringRangeValue(delete_req, stream);
        session->Apply(delete_op);
        LOG(INFO) << "Deleting tablet " << tablet_id << " from stream " << stream
                  << ". Reason: Consumer finished processing parent tablet after split.";
      }

      // Flush all the delete operations.
      // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
      Status s = session->TEST_Flush();
      if (!s.ok()) {
        LOG(ERROR) << "Unable to flush operations to delete cdc streams: " << s;
        return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
      }

      if (streams_to_delete.size() + streams_already_deleted.size() == streams.size()) {
        tablets_to_delete.insert(tablet_id);
      }
    }

    // Delete tablets from retained_by_cdcsdk_, CleanupHiddenTablets will do the
    // actual tablet deletion.
    {
      LockGuard lock(mutex_);
      for (const auto& tablet_to_delete : tablets_to_delete) {
        if (request_source == cdc::CDCSDK) {
          retained_by_cdcsdk_.erase(tablet_to_delete);
        } else if (request_source == cdc::XCLUSTER) {
          retained_by_xcluster_.erase(tablet_to_delete);
        }
      }
    }
  }

  return Status::OK();
}

std::shared_ptr<cdc::CDCServiceProxy> CatalogManager::GetCDCServiceProxy(RemoteTabletServer* ts) {
  auto ybclient = master_->cdc_state_client_initializer().client();
  auto hostport = HostPortFromPB(ts->DesiredHostPort(ybclient->cloud_info()));
  DCHECK(!hostport.host().empty());

  auto cdc_service = std::make_shared<cdc::CDCServiceProxy>(&ybclient->proxy_cache(), hostport);

  return cdc_service;
}

void CatalogManager::SetCDCServiceEnabled() {
  cdc_enabled_.store(true, std::memory_order_release);
}

void CatalogManager::PrepareRestore() {
  LOG_WITH_PREFIX(INFO) << "Disabling concurrent RPCs since restoration is ongoing";
  std::lock_guard<simple_spinlock> l(state_lock_);
  is_catalog_loaded_ = false;
}

void CatalogManager::EnableTabletSplitting(const std::string& feature) {
  DisableTabletSplittingInternal(MonoDelta::FromMilliseconds(0), feature);
}

void CatalogManager::ScheduleXClusterNSReplicationAddTableTask() {
  if (!namespace_replication_enabled_.load(std::memory_order_acquire)) {
    return;
  }

  LockGuard lock(mutex_);
  for (auto& map_entry : namespace_replication_map_) {
    auto& metadata = map_entry.second;
    if (CoarseMonoClock::Now() <= metadata.next_add_table_task_time) {
      continue;
    }
    // Enqueue the async add table task, which involves syncing with producer and adding
    // tables to the existing replication.
    const auto& universe_id = map_entry.first;
    CoarseTimePoint deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(60);
    auto s = background_tasks_thread_pool_->SubmitFunc(
        std::bind(&CatalogManager::XClusterAddTableToNSReplication, this, universe_id, deadline));
    if (!s.ok()) {
      // By not setting next_add_table_task_time, this enforces the task to be resheduled the
      // next time the background thread runs.
      LOG(WARNING) << "Failed to schedule: XClusterAddTableToNSReplication";
    } else {
      // Prevent new tasks from being scheduled when the current task is running.
      metadata.next_add_table_task_time = deadline;
    }
  }
}

void CatalogManager::XClusterAddTableToNSReplication(string universe_id,
                                                    CoarseTimePoint deadline) {
  // TODO: In ScopeExit, find a way to report non-OK task_status to user.
  bool has_non_replicated_consumer_table = true;
  Status task_status = Status::OK();
  auto scope_exit = ScopeExit([&, this] {
    LockGuard lock(mutex_);
    auto metadata_iter = namespace_replication_map_.find(universe_id);

    // Only update metadata if we are the most recent task for this universe.
    if (metadata_iter != namespace_replication_map_.end() &&
        metadata_iter->second.next_add_table_task_time == deadline) {
      auto& metadata = metadata_iter->second;
      // a. If there are error, emit to prometheus (TODO) and force another round of syncing.
      //    When there are too many consecutive errors, stop the task for a long period.
      // b. Else if there is non-replicated consumer table, force another round of syncing.
      // c. Else, stop the task temporarily.
      if (!task_status.ok()) {
        metadata.num_accumulated_errors++;
        if (metadata.num_accumulated_errors == 5) {
          metadata.num_accumulated_errors = 0;
          metadata.next_add_table_task_time = CoarseMonoClock::now() +
              MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_error_backoff_secs));
        } else {
          metadata.next_add_table_task_time = CoarseMonoClock::now() +
              MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_retry_secs));
        }
      } else {
        metadata.num_accumulated_errors = 0;
        metadata.next_add_table_task_time = CoarseMonoClock::now() +
            MonoDelta::FromSeconds(has_non_replicated_consumer_table
                ? GetAtomicFlag(&FLAGS_ns_replication_sync_retry_secs)
                : GetAtomicFlag(&FLAGS_ns_replication_sync_backoff_secs));
      }
    }
  });

  if (deadline - CoarseMonoClock::Now() <= 1ms || !CheckIsLeaderAndReady().ok()) {
    return;
  }

  // 1. Sync with producer to find new producer tables that can be added to the current
  //    replication, and verify that these tables do not require bootstrapping.
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      task_status = STATUS(NotFound, "Universe not found: " + universe_id);
      LOG_WITH_FUNC(WARNING) << task_status;
      return;
    }
  }
  std::vector<TableId> tables_to_add;
  task_status = XClusterNSReplicationSyncWithProducer(
      universe, &tables_to_add, &has_non_replicated_consumer_table);
  if (!task_status.ok()) {
    LOG_WITH_FUNC(WARNING) << "Error finding producer tables to add to universe "
                           << universe->id() << " : " << task_status;
    return;
  }
  if (tables_to_add.empty()) {
    return;
  }

  // 2. Run AlterUniverseReplication to add the new tables to the current replication.
  AlterUniverseReplicationRequestPB alter_req;
  AlterUniverseReplicationResponsePB alter_resp;
  alter_req.set_producer_id(universe_id);
  for (const auto& table : tables_to_add) {
    alter_req.add_producer_table_ids_to_add(table);
  }

  task_status = AlterUniverseReplication(&alter_req, &alter_resp, /* RpcContext */ nullptr);
  if (task_status.ok() && alter_resp.has_error()) {
    task_status = StatusFromPB(alter_resp.error().status());
  }
  if (!task_status.ok()) {
    LOG_WITH_FUNC(WARNING) << "Unable to add producer tables to namespace-level replication: "
                           << task_status;
    return;
  }

  // 3. Wait for AlterUniverseReplication to finish.
  task_status = WaitForSetupUniverseReplicationToFinish(universe_id + ".ALTER", deadline);
  if (!task_status.ok()) {
    LOG_WITH_FUNC(WARNING) << "Error while waiting for AlterUniverseReplication on "
                           << universe_id << " to complete: " << task_status;
    return;
  }
  LOG_WITH_FUNC(INFO) << "Tables added to namespace-level replication " << universe->id()
                      << " : " << alter_req.ShortDebugString();
}

Status CatalogManager::XClusterNSReplicationSyncWithProducer(
    scoped_refptr<UniverseReplicationInfo> universe,
    std::vector<TableId>* producer_tables_to_add,
    bool* has_non_replicated_consumer_table) {
  auto l = universe->LockForRead();
  size_t num_non_matched_consumer_tables = 0;

  // 1. Find producer tables with a name-matching consumer table.
  auto cdc_rpc = VERIFY_RESULT(
      universe->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses()));
  auto producer_namespace = l->pb.producer_namespace();
  auto consumer_namespace = l->pb.consumer_namespace();

  auto producer_tables = VERIFY_RESULT(XClusterFindProducerConsumerOverlap(
      cdc_rpc, &producer_namespace, &consumer_namespace, &num_non_matched_consumer_tables));

  // 2. Filter out producer tables that are already in the replication.
  for (const auto& tid : producer_tables) {
    if (ContainsKey(l->pb.validated_tables(), tid)) {
      continue;
    }
    producer_tables_to_add->push_back(tid);
  }

  // 3. If all consumer tables have a name-matching producer tables, and there is no additional
  //    producer table to add to the replication, this means that all consumer tables are
  //    currently replicated and we can stop the periodic sync temporarily.
  *has_non_replicated_consumer_table =
      num_non_matched_consumer_tables > 0 || !producer_tables_to_add->empty();

  // 4. Finally, verify that all producer tables to be added do not require bootstrapping.
  // TODO: Remove this check after NS-level bootstrap is implemented.
  if (!producer_tables_to_add->empty()) {
    auto bootstrap_required = VERIFY_RESULT(
        cdc_rpc->client()->IsBootstrapRequired(*producer_tables_to_add));
    if (bootstrap_required) {
      std::ostringstream ptable_stream;
      for (const auto& ptable : *producer_tables_to_add) {
        ptable_stream << ptable << ",";
      }
      std::string ptable_str = ptable_stream.str();
      ptable_str.pop_back(); // Remove the last comma.
      return STATUS(IllegalState,
          Format("Producer tables [$0] require bootstrapping, which is not currently "
                 "supported by the namespace-level replication setup.", ptable_str));
    }
  }
  return Status::OK();
}

Result<std::vector<TableId>> CatalogManager::XClusterFindProducerConsumerOverlap(
    std::shared_ptr<CDCRpcTasks> producer_cdc_rpc,
    NamespaceIdentifierPB* producer_namespace,
    NamespaceIdentifierPB* consumer_namespace,
    size_t* num_non_matched_consumer_tables) {
  // TODO: Add support for colocated (parent) tables. Currently they are not supported because
  // parent colocated tables are system tables and are therefore excluded by ListUserTables.
  SCHECK(producer_cdc_rpc != nullptr, InternalError, "Producer CDC RPC is null");

  // 1. Find all producer tables. Also record the producer namespace ID.
  auto producer_tables = VERIFY_RESULT(
      producer_cdc_rpc->client()->ListUserTables(*producer_namespace, true /* include_indexes */));
  SCHECK(!producer_tables.empty(), NotFound,
         "No producer table found under namespace " + producer_namespace->ShortDebugString());

  if (!producer_tables.empty()) {
    producer_namespace->set_id(producer_tables[0].namespace_id());
  }

  // 2. Find all consumer tables. Only collect the table names as we are doing name matching.
  //    Also record the consumer namespace ID.
  std::unordered_set<std::string> consumer_tables;
  {
    ListTablesRequestPB list_req;
    ListTablesResponsePB list_resp;
    list_req.add_relation_type_filter(USER_TABLE_RELATION);
    list_req.add_relation_type_filter(INDEX_TABLE_RELATION);
    list_req.mutable_namespace_()->CopyFrom(*consumer_namespace);

    auto s = ListTables(&list_req, &list_resp);
    std::ostringstream error_stream;
    if (!s.ok() || list_resp.has_error()) {
      error_stream << (!s.ok() ? s.ToString() : list_resp.error().status().message());
    }
    SCHECK(list_resp.tables_size() > 0, NotFound,
           Format("No consumer table found under namespace $0. Error: $1",
                  consumer_namespace->ShortDebugString(), error_stream.str()));
    for (const auto& table : list_resp.tables()) {
      auto table_name = Format("$0.$1.$2",
          table.namespace_().name(),
          table.pgschema_name(), // Empty for YCQL tables.
          table.name());
      consumer_tables.insert(table_name);
    }
    consumer_namespace->set_id(list_resp.tables(0).namespace_().id());
  }

  // 3. Find producer tables with a name-matching consumer table.
  std::vector<TableId> overlap_tables;
  for (const auto& table : producer_tables) {
    auto table_name = Format("$0.$1.$2",
        table.namespace_name(),
        table.pgschema_name(), // Empty for YCQL tables.
        table.table_name());
    if (consumer_tables.find(table_name) != consumer_tables.end()) {
      overlap_tables.push_back(table.table_id());
      consumer_tables.erase(table_name);
    }
  }

  // 4. Count the number of consumer tables without a name-matching producer table.
  *num_non_matched_consumer_tables = consumer_tables.size();

  return overlap_tables;
}

Status CatalogManager::WaitForSetupUniverseReplicationToFinish(const string& producer_uuid,
                                                               CoarseTimePoint deadline) {
  while (true) {
    if (deadline - CoarseMonoClock::Now() <= 1ms) {
      return STATUS(TimedOut, "Timed out while waiting for SetupUniverseReplication to finish");
    }
    IsSetupUniverseReplicationDoneRequestPB check_req;
    IsSetupUniverseReplicationDoneResponsePB check_resp;
    check_req.set_producer_id(producer_uuid);
    auto s = IsSetupUniverseReplicationDone(&check_req, &check_resp, /* RpcContext */ nullptr);
    if (!s.ok() || check_resp.has_error()) {
      return !s.ok() ? s : StatusFromPB(check_resp.error().status());
    }
    if (check_resp.has_done() && check_resp.done()) {
      return StatusFromPB(check_resp.replication_error());
    }
    SleepFor(MonoDelta::FromMilliseconds(200));
  }
}

Result<scoped_refptr<TableInfo>> CatalogManager::GetTableById(const TableId& table_id) const {
  return FindTableById(table_id);
}

Status CatalogManager::ProcessTabletReplicationStatus(
    const TabletReplicationStatusPB& tablet_replication_state) {

  // Lookup the tablet info for the given tablet id.
  const string& tablet_id = tablet_replication_state.tablet_id();
  scoped_refptr<TabletInfo> tablet;
  {
    SharedLock lock(mutex_);
    tablet = FindPtrOrNull(*tablet_map_, tablet_id);
    SCHECK(tablet, NotFound, Format("Could not locate tablet info for tablet id $0", tablet_id));

    // Lookup the streams that this tablet belongs to.
    const std::string& consumer_table_id = tablet->table()->id();
    auto stream_map_iter = xcluster_consumer_tables_to_stream_map_.find(consumer_table_id);
    SCHECK(stream_map_iter != xcluster_consumer_tables_to_stream_map_.end(), NotFound,
           Substitute("Could not locate stream map for table id $0", consumer_table_id));

    for (const auto& kv : stream_map_iter->second) {
      const auto& producer_id = kv.first;

      for (const auto& tablet_stream_kv : tablet_replication_state.stream_replication_statuses()) {
        const auto& tablet_stream_id = tablet_stream_kv.first;
        const auto& tablet_stream_replication_status = tablet_stream_kv.second;

        // Build a list of replication errors to store for this stream.
        std::vector<std::pair<ReplicationErrorPb, std::string>> replication_errors;
        replication_errors.reserve(tablet_stream_replication_status.replication_errors_size());
        for (const auto& kv : tablet_stream_replication_status.replication_errors()) {
          const auto& error_code = kv.first;
          const auto& error_detail = kv.second;

          // The protobuf spec does not allow enums to be used as keys in maps. To work around this,
          // the keys in 'replication_errors' are of type int32_t that correspond to values in the
          // 'ReplicationErrorPb' enum.
          if (ReplicationErrorPb_IsValid(error_code)) {
            replication_errors.emplace_back(
              static_cast<ReplicationErrorPb>(error_code), error_detail);
          } else {
            LOG(WARNING) << Format("Received replication status error code ($0) that does not "
                                  "correspond to an enum value in ReplicationErrorPb.");
            replication_errors.emplace_back(REPLICATION_UNKNOWN_ERROR, error_detail);
          }
        }

        WARN_NOT_OK(
          StoreReplicationErrorsUnlocked(
            producer_id, consumer_table_id, tablet_stream_id, replication_errors),
          Format("Failed to store replication error for universe=$0 table=$1 stream=$2",
                 producer_id, consumer_table_id, tablet_stream_id));
      }
    }
  }

  return Status::OK();
}

docdb::HistoryCutoff CatalogManager::AllowedHistoryCutoffProvider(
    tablet::RaftGroupMetadata* metadata) {
  return snapshot_coordinator_.AllowedHistoryCutoffProvider(metadata);
}

}  // namespace enterprise
}  // namespace master
}  // namespace yb
