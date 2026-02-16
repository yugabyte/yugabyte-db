// Copyright (c) YugabyteDB, Inc.
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

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/xcluster_client.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common_util.h"
#include "yb/common/common.pb.h"
#include "yb/common/common_flags.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/xcluster_util.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_util.h"
#include "yb/master/master.h"
#include "yb/master/snapshot_transfer_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/ysql/ysql_manager_if.h"

#include "yb/rpc/scheduler.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug-util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

using std::string;
using namespace std::literals;
using std::vector;

DEFINE_test_flag(bool, disable_cdc_state_insert_on_setup, false,
    "Disable inserting new entries into cdc state as part of the setup flow.");

DEFINE_RUNTIME_int32(cdcsdk_table_processing_limit_per_run, 2,
    "The number of newly added tables we will add to CDCSDK streams, per run of the background "
    "task.");

DEFINE_RUNTIME_bool(xcluster_skip_schema_compatibility_checks_on_alter, false,
    "When xCluster replication sends a DDL change, skip checks "
    "for any schema compatibility");

DEFINE_RUNTIME_int32(wait_replication_drain_retry_timeout_ms, 2000,
    "Timeout in milliseconds in between CheckReplicationDrain calls to tservers "
    "in case of retries.");

DEFINE_RUNTIME_bool(disable_universe_gc, false, "Whether to run the GC on universes or not.");

DEFINE_RUNTIME_int32(cdc_parent_tablet_deletion_task_retry_secs, 30,
    "Frequency at which the background task will verify parent tablets retained for xCluster or "
    "CDCSDK replication and determine if they can be cleaned up.");

DEFINE_test_flag(bool, hang_wait_replication_drain, false,
    "Used in tests to temporarily block WaitForReplicationDrain.");

DEFINE_RUNTIME_AUTO_bool(cdc_enable_postgres_replica_identity, kLocalPersisted, false, true,
    "Enable new record types in CDC streams");

DEFINE_RUNTIME_bool(enable_backfilling_cdc_stream_with_replication_slot, false,
    "When enabled, allows adding a replication slot name to an existing CDC stream via the yb-admin"
    " ysql_backfill_change_data_stream_with_replication_slot command."
    "Intended to be used for making CDC streams created before replication slot support work with"
    " the replication slot commands.");

DEFINE_test_flag(bool, xcluster_fail_setup_stream_update, false, "Fail UpdateCDCStream RPC call");

DEFINE_RUNTIME_AUTO_bool(cdcsdk_enable_dynamic_tables_disable_option,
                        kLocalPersisted,
                        false,
                        true,
                        "This flag needs to be true in order to disable addition of dynamic tables "
                        "to CDC stream. This flag is required to be to true for execution of "
                        "yb-admin commands - "
                        "\'disable_dynamic_table_addition_on_change_data_stream\', "
                        "\'remove_user_table_from_change_data_stream\'");
TAG_FLAG(cdcsdk_enable_dynamic_tables_disable_option, advanced);
TAG_FLAG(cdcsdk_enable_dynamic_tables_disable_option, hidden);

DEFINE_test_flag(bool, cdcsdk_skip_updating_cdc_state_entries_on_table_removal, false,
    "Skip updating checkpoint to max for cdc state table entries while removing a user table from "
    "CDCSDK stream.");

DEFINE_test_flag(bool, cdcsdk_add_indexes_to_stream, false, "Allows addition of index to a stream");

DEPRECATE_FLAG(bool, cdcsdk_enable_cleanup_of_non_eligible_tables_from_stream, "09_2024");

DEFINE_test_flag(bool, cdcsdk_disable_drop_table_cleanup, false,
                 "When enabled, cleanup of dropped tables from CDC streams will be skipped.");

DEFINE_test_flag(bool, cdcsdk_disable_deleted_stream_cleanup, false,
                 "When enabled, cleanup of deleted CDCSDK streams will be skipped.");

DEFINE_RUNTIME_AUTO_bool(cdcsdk_enable_identification_of_non_eligible_tables,
                         kLocalPersisted,
                         false,
                         true,
                         "This flag, when true, identifies all non-eligible tables that are part of"
                         " a CDC stream metadata while loading the CDC streams on a master "
                         "restart/leadership change. This identification happens on all CDC "
                         "streams in the universe");
TAG_FLAG(cdcsdk_enable_identification_of_non_eligible_tables, advanced);
TAG_FLAG(cdcsdk_enable_identification_of_non_eligible_tables, hidden);

DEFINE_test_flag(bool, cdcsdk_skip_table_removal_from_qualified_list, false,
                 "When enabled, table would not be removed from the qualified table list as part "
                 "of the table removal process from CDC stream");

DEFINE_RUNTIME_bool(enable_truncate_cdcsdk_table, false,
    "When set, enables truncating tables currently part of a CDCSDK Stream");

DEFINE_RUNTIME_AUTO_bool(xcluster_store_older_schema_versions, kLocalPersisted, false, true,
    "When set, enables storing multiple older schema versions in xCluster replication stream "
    "metadata instead of just storing the current and previous schema versions.");

DEFINE_RUNTIME_uint32(xcluster_max_old_schema_versions, 50,
    "Maximum number of old schema versions to keep in xCluster replication stream metadata");

DEFINE_RUNTIME_AUTO_bool(cdc_enable_dynamic_schema_changes, kLocalPersisted, false, true,
    "When set, enables streaming of dynamic schema changes via CDC. The dynamic schema changes "
    "include any changes made to the publications and all the DDLs including those which cause "
    "table rewrites.");

DECLARE_int32(master_rpc_timeout_ms);
DECLARE_bool(ysql_yb_enable_replication_commands);
DECLARE_bool(ysql_yb_allow_replication_slot_lsn_types);
DECLARE_bool(ysql_yb_allow_replication_slot_ordering_modes);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_yb_enable_replica_identity);
DECLARE_uint32(cdc_wal_retention_time_secs);
DECLARE_uint32(max_replication_slots);
DECLARE_uint64(cdc_intent_retention_ms);
DECLARE_uint32(cdcsdk_tablet_not_of_interest_timeout_secs);
DECLARE_bool(cdcsdk_enable_dynamic_table_addition_with_table_cleanup);
DECLARE_bool(ysql_yb_enable_implicit_dynamic_tables_logical_replication);
DECLARE_bool(ysql_yb_cdcsdk_stream_tables_without_primary_key);
DECLARE_bool(TEST_enable_table_rewrite_for_cdcsdk_table);

#define RETURN_ACTION_NOT_OK(expr, action) \
  RETURN_NOT_OK_PREPEND((expr), Format("An error occurred while $0", action))

#define RETURN_INVALID_REQUEST_STATUS(error_msg) \
return STATUS( \
      InvalidArgument, error_msg, \
      MasterError(MasterErrorPB::INVALID_REQUEST))

namespace yb {
using client::internal::RemoteTabletServer;

namespace master {

////////////////////////////////////////////////////////////
// CDC Stream Loader
////////////////////////////////////////////////////////////

class CDCStreamLoader : public Visitor<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamLoader(CatalogManager* catalog_manager, XClusterManager& xcluster_manager)
      : catalog_manager_(catalog_manager), xcluster_manager_(xcluster_manager) {}

  void AddDefaultValuesIfMissing(const SysCDCStreamEntryPB& metadata, CDCStreamInfo::WriteLock* l) {
    bool source_type_present = false;
    bool checkpoint_type_present = false;

    // Iterate over all the options to check if checkpoint_type and source_type are present.
    // (DEPRECATE_EOL 2024.1) This can be removed since XClusterSourceManager creates all new
    // streams with these options from 2024.1, and older streams were backfilled.
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

  Status Visit(const std::string& stream_id_str, const SysCDCStreamEntryPB& metadata) override
      REQUIRES(catalog_manager_->mutex_) {
    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
    {
      std::lock_guard l(catalog_manager_->xrepl_stream_ids_in_use_mutex_);
      InsertOrDie(&catalog_manager_->xrepl_stream_ids_in_use_, stream_id);
    }
    DCHECK(!ContainsKey(catalog_manager_->cdc_stream_map_, stream_id))
        << "CDC stream already exists: " << stream_id;

    // If CDCStream entry exists, then the current cluster is a producer.
    catalog_manager_->SetCDCServiceEnabled();

    scoped_refptr<NamespaceInfo> ns;
    scoped_refptr<TableInfo> table;

    bool should_mark_cdcsdk_stream_as_deleting = false;
    if (metadata.has_namespace_id()) {
      ns = FindPtrOrNull(catalog_manager_->namespace_ids_map_, metadata.namespace_id());

      // If the namespace is not found then ideally this CDCSDK stream should have been deleted. We
      // will load such stream metadata and mark it as DELETING. The background task will then
      // cleanup this stream.
      if (!ns) {
        should_mark_cdcsdk_stream_as_deleting = true;
      }
    } else {
      table = catalog_manager_->tables_->FindTableOrNull(
          xcluster::StripSequencesDataAliasIfPresent(metadata.table_id(0)));
      if (!table) {
        LOG(DFATAL) << "Invalid table ID " << metadata.table_id(0) << " for stream " << stream_id;
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

    // No matter what the state of this CDCSDK stream is persisted in sys catalog, we will mark the
    // stream as DELETING if its namespace is not found.
    if (should_mark_cdcsdk_stream_as_deleting) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::DELETING);
    }

    // If the table asscoiated with this xcluster stream is in deleting state or the namespace
    // associated with this CDCSDK stream is in deleting state, then mark this stream as DELETING so
    // it can be deleted by the catalog manager background thread. Otherwise if this stream is
    // missing an entry for state, then mark its state as Active.
    if (((table && table->LockForRead()->is_deleting()) ||
         (ns && ns->state() == SysNamespaceEntryPB::DELETING)) &&
        !l.data().is_deleting()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::DELETING);
    } else if (!l.mutable_data()->pb.has_state()) {
      l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::ACTIVE);
    }

    // Add the CDC stream to the CDC stream map.
    catalog_manager_->cdc_stream_map_[stream->StreamId()] = stream;
    if (table) {
      xcluster_manager_.RecordOutboundStream(stream, metadata.table_id(0));
    }
    if (ns) {
      for (const auto& table_id : metadata.table_id()) {
        catalog_manager_->cdcsdk_tables_to_stream_map_[table_id].insert(stream->StreamId());
      }
      if (metadata.has_cdcsdk_ysql_replication_slot_name()) {
        catalog_manager_->cdcsdk_replication_slots_to_stream_map_.insert_or_assign(
            ReplicationSlotName(metadata.cdcsdk_ysql_replication_slot_name()),
                                stream->StreamId());
      }
    }

    l.Commit();

    // For CDCSDK Streams, we scan all the tables in the namespace, and compare it with all the
    // tables associated with the stream.
    if ((metadata.state() == SysCDCStreamEntryPB::ACTIVE ||
         metadata.state() == SysCDCStreamEntryPB::DELETING_METADATA) &&
        ns && ns->state() == SysNamespaceEntryPB::RUNNING) {
      auto eligible_tables_info = catalog_manager_->FindAllTablesForCDCSDK(
          metadata.namespace_id(), metadata.allow_tables_without_primary_key());
      catalog_manager_->FindAllTablesMissingInCDCSDKStream(
          stream_id, metadata.table_id(), eligible_tables_info, metadata.unqualified_table_id());

      if (stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
        // Check for any non-eligible tables like indexes, matview etc in CDC stream only if the
        // stream is not associated with a replication slot.
        catalog_manager_->FindAllNonEligibleTablesInCDCSDKStream(
            stream_id, metadata.table_id(), eligible_tables_info);

        // Check for any unprocessed unqualified tables that needs to be removed from CDCSDK
        // streams.
        catalog_manager_->FindAllUnproccesedUnqualifiedTablesInCDCSDKStream(
            stream_id, metadata.table_id(), metadata.unqualified_table_id(), eligible_tables_info);
      }
    }

    LOG(INFO) << "Loaded metadata for CDC stream " << stream->ToString() << ": "
              << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;
  XClusterManager& xcluster_manager_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamLoader);
};

void CatalogManager::ClearXReplState() {
  // Clear CDC stream map.
  xrepl_maps_loaded_ = false;
  {
    std::lock_guard l(xrepl_stream_ids_in_use_mutex_);
    xrepl_stream_ids_in_use_.clear();
  }
  cdc_stream_map_.clear();

  // Clear CDCSDK stream map.
  cdcsdk_tables_to_stream_map_.clear();
  cdcsdk_replication_slots_to_stream_map_.clear();

  // Clear universe replication map.
  universe_replication_map_.clear();
}

Status CatalogManager::LoadXReplStream() {
  LOG_WITH_FUNC(INFO) << "Loading CDC streams into memory.";
  auto cdc_stream_loader = std::make_unique<CDCStreamLoader>(this, *xcluster_manager_);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(cdc_stream_loader.get()),
      "Failed while visiting CDC streams in sys catalog");
  xrepl_maps_loaded_ = true;

  // Load retained_by_cdcsdk_ only after loading all CDC streams.
  for (const auto& tablet : hidden_tablets_) {
    TabletDeleteRetainerInfo delete_retainer;
    CDCSDKPopulateDeleteRetainerInfoForTabletDrop(*tablet, delete_retainer);
    if (FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table) {
      CDCSDKPopulateDeleteRetainerInfoForTableDrop(*tablet->table(), delete_retainer);
    }
    RecordCDCSDKHiddenTablets({tablet}, delete_retainer);
  }

  return Status::OK();
}

void CatalogManager::RecordCDCSDKHiddenTablets(
    const std::vector<TabletInfoPtr>& tablets, const TabletDeleteRetainerInfo& delete_retainer) {
  if (!delete_retainer.active_cdcsdk) {
    return;
  }

  for (const auto& hidden_tablet : tablets) {
    auto tablet_lock = hidden_tablet->LockForRead();
    auto& tablet_pb = tablet_lock->pb;
    const std::vector<TabletId> split_tablets(
        tablet_pb.split_tablet_ids().begin(), tablet_pb.split_tablet_ids().end());
    HiddenReplicationTabletInfo info{
        .table_id_ = hidden_tablet->table()->id(),
        .parent_tablet_id_ =
            tablet_pb.has_split_parent_tablet_id() ? tablet_pb.split_parent_tablet_id() : "",
        .split_tablets_ = std::move(split_tablets),
        .hide_time_ = HybridTime(tablet_pb.hide_hybrid_time()),
        .hide_reason_ = HiddenReplicationTabletInfo::HideReason::kTabletSplit};

    if (hidden_tablet->table()->started_hiding()) {
      info.hide_reason_ = HiddenReplicationTabletInfo::HideReason::kTableDeleted;
    }

    retained_by_cdcsdk_.emplace(hidden_tablet->id(), std::move(info));
  }
}

////////////////////////////////////////////////////////////
// Universe Replication Loader
////////////////////////////////////////////////////////////

class UniverseReplicationLoader : public Visitor<PersistentUniverseReplicationInfo> {
 public:
  explicit UniverseReplicationLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  Status Visit(
      const std::string& replication_group_id_str,
      const SysUniverseReplicationEntryPB& metadata) override REQUIRES(catalog_manager_->mutex_) {
    const xcluster::ReplicationGroupId replication_group_id(replication_group_id_str);
    DCHECK(!ContainsKey(
        catalog_manager_->universe_replication_map_,
        xcluster::ReplicationGroupId(replication_group_id)))
        << "Producer universe already exists: " << replication_group_id;

    // Setup the universe replication info.
    scoped_refptr<UniverseReplicationInfo> const ri =
        new UniverseReplicationInfo(replication_group_id);
    {
      auto l = ri->LockForWrite();
      l.mutable_data()->pb.CopyFrom(metadata);

      if (!l->is_active() && !l->is_deleted_or_failed()) {
        // Replication was not fully setup.
        LOG(WARNING) << "Universe replication in transient state: " << replication_group_id;

        // TODO: Should we delete all failed universe replication items?
      }

      // Add universe replication info to the universe replication map.
      catalog_manager_->universe_replication_map_[ri->ReplicationGroupId()] = ri;

      // Add any failed universes to be cleared
      if (l->is_deleted_or_failed() || l->pb.state() == SysUniverseReplicationEntryPB::DELETING ||
          xcluster::IsAlterReplicationGroupId(
              xcluster::ReplicationGroupId(l->pb.replication_group_id()))) {
        catalog_manager_->universes_to_clear_.push_back(ri->ReplicationGroupId());
      }

      l.Commit();
        }

    // Also keep track of consumer tables.
    for (const auto& table : metadata.validated_tables()) {
      auto stream_id = FindWithDefault(metadata.table_streams(), table.first, "");
      if (stream_id.empty()) {
        LOG(WARNING) << "Unable to find stream id for table: " << table.first;
        continue;
      }
      catalog_manager_->GetXClusterManagerImpl()->RecordTableConsumerStream(
          table.second, ri->ReplicationGroupId(),
          VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
    }

    LOG(INFO) << "Loaded metadata for universe replication " << ri->ToString();
    VLOG(1) << "Metadata for universe replication " << ri->ToString() << ": "
            << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(UniverseReplicationLoader);
};

Status CatalogManager::LoadUniverseReplication() {
  LOG_WITH_FUNC(INFO) << "Loading universe replication info into memory.";
  auto universe_replication_loader = std::make_unique<UniverseReplicationLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(universe_replication_loader.get()),
      "Failed while visiting universe replication info in sys catalog");

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Universe Replication Bootstrap Loader
////////////////////////////////////////////////////////////

class UniverseReplicationBootstrapLoader
    : public Visitor<PersistentUniverseReplicationBootstrapInfo> {
 public:
  explicit UniverseReplicationBootstrapLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  Status Visit(
      const std::string& replication_group_id_str,
      const SysUniverseReplicationBootstrapEntryPB& metadata) override
      REQUIRES(catalog_manager_->mutex_) {
    const xcluster::ReplicationGroupId replication_group_id(replication_group_id_str);
    DCHECK(!ContainsKey(
        catalog_manager_->universe_replication_bootstrap_map_,
        xcluster::ReplicationGroupId(replication_group_id)))
        << "Producer universe already exists: " << replication_group_id;

    // Setup the universe replication info.
    scoped_refptr<UniverseReplicationBootstrapInfo> const bootstrap_info =
        new UniverseReplicationBootstrapInfo(replication_group_id);
    {
      auto l = bootstrap_info->LockForWrite();
      l.mutable_data()->pb.CopyFrom(metadata);

      if (!l->is_done() && !l->is_deleted_or_failed()) {
        // Replication was not fully setup.
        LOG(WARNING) << "Universe replication bootstrap in transient state: "
                     << replication_group_id;

        // Delete tasks in transient state.
        l.mutable_data()->pb.set_failed_on(l->state());
        l.mutable_data()->pb.set_state(SysUniverseReplicationBootstrapEntryPB::DELETING);
        catalog_manager_->replication_bootstraps_to_clear_.push_back(
            bootstrap_info->ReplicationGroupId());
      }

      // Add universe replication bootstrap info to the universe replication map.
      catalog_manager_->universe_replication_bootstrap_map_[bootstrap_info->ReplicationGroupId()] =
          bootstrap_info;

      // Add any failed bootstraps to be cleared
      if (l->is_deleted_or_failed() ||
          l->pb.state() == SysUniverseReplicationBootstrapEntryPB::DELETING) {
        catalog_manager_->replication_bootstraps_to_clear_.push_back(
            bootstrap_info->ReplicationGroupId());
      }
      l.Commit();
    }

    LOG(INFO) << "Loaded metadata for universe replication bootstrap" << bootstrap_info->ToString();
    VLOG(1) << "Metadata for universe replication bootstrap " << bootstrap_info->ToString() << ": "
            << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;
};

Status CatalogManager::LoadUniverseReplicationBootstrap() {
  LOG_WITH_FUNC(INFO) << "Loading universe replication bootstrap info into memory.";
  auto loader = std::make_unique<UniverseReplicationBootstrapLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(loader.get()),
      "Failed while visiting universe replication bootstrap info in sys catalog");

  return Status::OK();
}

// Helper class to print a vector of CDCStreamInfo pointers.
namespace {

template <class CDCStreamInfoPointer>
std::string CDCStreamInfosAsString(const std::vector<CDCStreamInfoPointer>& cdc_streams) {
  std::vector<std::string> cdc_stream_ids;
  for (const auto& cdc_stream : cdc_streams) {
    cdc_stream_ids.push_back(cdc_stream->id());
  }
  return AsString(cdc_stream_ids);
}

}  // namespace

Status CatalogManager::DropXClusterStreamsOfTables(const std::unordered_set<TableId>& table_ids) {
  if (table_ids.empty()) {
    return Status::OK();
  }

  std::vector<CDCStreamInfoPtr> streams;
  {
    SharedLock lock(mutex_);
    for (const auto& tid : table_ids) {
      auto table_streams = GetXReplStreamsForTable(tid, cdc::XCLUSTER);
      streams.insert(streams.end(), table_streams.begin(), table_streams.end());
    }
  }

  if (streams.empty()) {
    return Status::OK();
  }

  LOG(INFO) << "Deleting xCluster streams for tables:" << AsString(table_ids);

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  return DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING);
}

Status CatalogManager::DropCDCSDKStreams(const std::unordered_set<TableId>& table_ids) {
  if (table_ids.empty()) {
    return Status::OK();
  }

  std::vector<CDCStreamInfoPtr> streams;
  {
    LockGuard lock(mutex_);
    for (const auto& table_id : table_ids) {
      cdcsdk_tables_to_stream_map_.erase(table_id);
    }
    streams = FindCDCSDKStreamsToDeleteMetadata(table_ids);
  }
  if (streams.empty()) {
    return Status::OK();
  }

  LOG(INFO) << "Deleting CDCSDK streams metadata for tables:" << AsString(table_ids);

  // Do not delete them here, just mark them as DELETING_METADATA and the catalog manager background
  // thread will handle the deletion.
  return DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING_METADATA);
}

Status CatalogManager::DropAllCDCSDKStreams(const NamespaceId& ns_id) {
  DCHECK(!ns_id.empty()) << "Namespace ID should not be empty for CDCSDK streams deletion";

  std::vector<CDCStreamInfoPtr> streams;
  {
    SharedLock lock(mutex_);
    for (const auto& entry : cdc_stream_map_) {
      auto ltm = entry.second->LockForRead();
      if (!ltm->namespace_id().empty() && ltm->namespace_id() == ns_id) {
        streams.push_back(entry.second);
      }
    }
  }

  if (streams.empty()) {
    return Status::OK();
  }

  LOG(INFO) << "Deleting all CDCSDK streams for namespace: " << ns_id;
  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  return DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING);
}

Status CatalogManager::AddNewTableToCDCDKStreamsMetadata(
    const TableId& table_id, const NamespaceId& ns_id) {
  LockGuard lock(cdcsdk_unprocessed_table_mutex_);
  VLOG(1) << "Added table: " << table_id << ", under namesapce: " << ns_id
          << ", to namespace_to_cdcsdk_unprocessed_table_map_ to be processed by CDC streams";
  namespace_to_cdcsdk_unprocessed_table_map_[ns_id].insert(table_id);

  return Status::OK();
}

std::vector<CDCStreamInfoPtr> CatalogManager::GetXReplStreamsForTable(
    const TableId& table_id, const cdc::CDCRequestSource cdc_request_source) const {
  std::vector<CDCStreamInfoPtr> streams;
  for (const auto& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (!ltm->table_id().empty() &&
        (std::find(ltm->table_id().begin(), ltm->table_id().end(), table_id) !=
         ltm->table_id().end()) &&
        !ltm->started_deleting()) {
      if (cdc_request_source == cdc::XCLUSTER && ltm->namespace_id().empty()) {
        streams.push_back(entry.second);
      } else if (cdc_request_source == cdc::CDCSDK && !ltm->namespace_id().empty()) {
        // For CDCSDK, the table should exclusively belong to qualified table list.
        if (ltm->unqualified_table_id().empty() ||
            std::find(
                ltm->unqualified_table_id().begin(), ltm->unqualified_table_id().end(), table_id) ==
                ltm->unqualified_table_id().end()) {
          streams.push_back(entry.second);
        }
      }
    }
  }
  return streams;
}

std::vector<CDCStreamInfoPtr> CatalogManager::FindCDCSDKStreamsToDeleteMetadata(
    const std::unordered_set<TableId>& table_ids) const {
  std::vector<CDCStreamInfoPtr> streams;

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
    } else if (ltm->pb.unqualified_table_id_size() > 0) {
      if (std::any_of(
              ltm->unqualified_table_id().begin(), ltm->unqualified_table_id().end(),
              [&table_ids](const auto& unqualified_table_id) {
                return table_ids.contains(unqualified_table_id);
              })) {
        streams.push_back(stream_info);
      }
    }
  }
  return streams;
}

void CatalogManager::GetAllCDCStreams(std::vector<CDCStreamInfoPtr>* streams) {
  streams->clear();
  SharedLock lock(mutex_);
  streams->reserve(cdc_stream_map_.size());
  for (const CDCStreamInfoMap::value_type& e : cdc_stream_map_) {
    if (!e.second->LockForRead()->is_deleting()) {
      streams->push_back(e.second);
    }
  }
}

Status CatalogManager::BackfillMetadataForXRepl(
    const TableInfoPtr& table, const LeaderEpoch& epoch) {
  auto& table_id = table->id();
  VLOG_WITH_FUNC(4) << "Backfilling CDC Metadata for table: " << table_id;

  AlterTableRequestPB alter_table_req_pg_type;
  bool backfill_required = false;
  {
    SharedLock catalog_manager_lock(mutex_);
    auto table_lock = table->LockForRead();
    if (table->IsSequencesSystemTable(table_lock)) {
      // Postgres doesn't know about the sequences_data table so it has neither an OID or PG schema
      return Status::OK();
    }
    if (table_lock->GetTableType() == PGSQL_TABLE_TYPE) {
      if (!table_lock->has_pg_type_oid()) {
        LOG_WITH_FUNC(INFO) << "backfilling pg_type_oid for table " << table_id;
        auto const att_name_typid_map = VERIFY_RESULT(
            GetPgAttNameTypidMap(table_id, table_lock.data()));
        vector<uint32_t> type_oids;
        for (const auto& entry : att_name_typid_map) {
          type_oids.push_back(entry.second);
        }
        auto ns = VERIFY_RESULT(FindNamespaceByIdUnlocked(table_lock->namespace_id()));
        auto const type_oid_info_map = VERIFY_RESULT(GetPgTypeInfo(ns, &type_oids));
        for (const auto& entry : att_name_typid_map) {
          VLOG(1) << "For table:" << table_lock->name() << " column:" << entry.first
                  << ", pg_type_oid: " << entry.second;
          auto* step = alter_table_req_pg_type.add_alter_schema_steps();
          step->set_type(::yb::master::AlterTableRequestPB_StepType::
                             AlterTableRequestPB_StepType_SET_COLUMN_PG_TYPE);
          auto set_column_pg_type = step->mutable_set_column_pg_type();
          set_column_pg_type->set_name(entry.first);
          uint32_t pg_type_oid = entry.second;

          const YbcPgTypeEntity* type_entity =
              docdb::DocPgGetTypeEntity({.type_id = (int32_t)pg_type_oid, .type_mod = -1});

          if (type_entity == nullptr && type_oid_info_map.contains(pg_type_oid)) {
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
          (backfill_required || table_lock->schema().deprecated_pgschema_name().empty())) {
        LOG_WITH_FUNC(INFO) << "backfilling pgschema_name for table " << table_id;
        const auto pgschema_name = VERIFY_RESULT(GetYsqlManager().GetPgSchemaName(
            VERIFY_RESULT(table->GetPgTableAllOids())));
        VLOG(1) << "For table: " << table_lock->name() << " found pgschema_name: " << pgschema_name;
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
    return this->AlterTable(
        &alter_table_req_pg_type, &alter_table_resp_pg_type, /*rpc=*/nullptr, epoch);
  } else {
    LOG_WITH_FUNC(INFO)
        << "found pgschema_name and pg_type_oid, no backfilling required for table id: "
        << table_id;
    return Status::OK();
  }
}

Status CatalogManager::CreateCDCStream(
    const CreateCDCStreamRequestPB* req, CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "CreateCDCStream from " << RequestorString(rpc) << ": " << req->ShortDebugString();

  if (!req->has_table_id() && !req->has_namespace_id()) {
    RETURN_INVALID_REQUEST_STATUS("One of table_id or namespace_id must be provided");
  }

  std::string id_type_option_value(cdc::kTableId);
  std::string record_type_option_value;
  std::string source_type_option_value(CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));

  for (auto option : req->options()) {
    if (option.key() == cdc::kIdType) {
      id_type_option_value = option.value();
    }
    if (option.key() == cdc::kSourceType) {
      source_type_option_value = option.value();
    }
    if (option.key() == cdc::kRecordType ) {
      if (FLAGS_ysql_yb_enable_replica_identity && req->has_cdcsdk_ysql_replication_slot_name()) {
        LOG(WARNING) << " The value for Before Image RecordType will be ignored for replication "
                        "slot consumption. The RecordType for each table will be determined by the "
                        "replica identity of the table at the time of stream creation.";
      }
      record_type_option_value = option.value();
    }
  }

  if (source_type_option_value == CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER) ||
      (req->has_table_id() && id_type_option_value != cdc::kNamespaceId)) {
    // xCluster mode.
    SCHECK_PB_FIELDS_NOT_EMPTY(*req, table_id);
    SCHECK_NE(
        id_type_option_value, cdc::kNamespaceId, InvalidArgument,
        "NamespaceId option should not be set for xCluster streams");

    // User specified req->options() are ignored. xCluster sets its own predefined static set of
    // options.

    std::optional<SysCDCStreamEntryPB::State> initial_state = std::nullopt;
    if (req->has_initial_state()) {
      initial_state = req->initial_state();
    }

    Synchronizer sync;
    auto stream_id = VERIFY_RESULT(xcluster_manager_->CreateNewXClusterStreamForTable(
        req->table_id(), cdc::StreamModeTransactional(req->transactional()), initial_state, epoch,
        [&sync](const Status& status) { sync.AsStdStatusCallback()(status); }));
    RETURN_NOT_OK(sync.Wait());

    resp->set_stream_id(stream_id.ToString());
    return Status::OK();
  }

  // CDCSDK mode.
  RETURN_NOT_OK(ValidateCDCSDKRequestProperties(
      *req, source_type_option_value, record_type_option_value, id_type_option_value));

  RETURN_NOT_OK(CreateNewCDCStreamForNamespace(*req, resp, rpc, epoch));

  return Status::OK();
}

Status CatalogManager::CreateNewCDCStreamForNamespace(
    const CreateCDCStreamRequestPB& req, CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  std::string namespace_id;
  // The namespace_id field was added to the request as part of the replication slots feature in
  // YSQL. When the replication slot feature is disabled, read from the table_id field for backwards
  // compatibility so that we still support atomic creation of namespace level CDCSDK streams in
  // yb-master independently of the replication slot feature.
  if (FLAGS_ysql_yb_enable_replication_commands) {
    namespace_id = req.namespace_id();
  } else {
    namespace_id = req.table_id();
  }

  // We support streaming of tables without primary key only for logical replication streams
  // (controlled via flag ysql_yb_cdcsdk_stream_tables_without_primary_key).
  const bool allow_tables_without_primary_key =
      req.has_cdcsdk_ysql_replication_slot_name() &&
      FLAGS_ysql_yb_cdcsdk_stream_tables_without_primary_key;

  // TODO(#19211): Validate that if the ns type is PGSQL, it must have the replication slot name in
  // the request. This can only be done after we have ensured that YSQL is the only client
  // requesting to create CDC streams.
  std::vector<TableInfoPtr> tables;
  {
    SharedLock lock(mutex_);
    // Sanity check this id corresponds to a namespace.
    VERIFY_RESULT(FindNamespaceByIdUnlocked(namespace_id));
    tables = FindAllTablesForCDCSDK(namespace_id, allow_tables_without_primary_key);
  }

  std::vector<TableId> table_ids;
  table_ids.reserve(tables.size());
  for (const auto& table : tables) {
    RETURN_NOT_OK(BackfillMetadataForXRepl(table, epoch));
    table_ids.push_back(table->id());
  }

  // We add the pg_class and pg_publication_rel catalog tables to the stream metadata as we will
  // poll them to figure out changes to the publications. This will not be done for gRPC streams.
  if (FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication &&
      FLAGS_cdc_enable_dynamic_schema_changes && req.has_cdcsdk_ysql_replication_slot_name()) {
    auto database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id));
    table_ids.push_back(GetPgsqlTableId(database_oid, kPgClassTableOid));
    table_ids.push_back(GetPgsqlTableId(database_oid, kPgPublicationRelOid));
    VLOG_WITH_FUNC(1) << "Added the catalog tables pg_class and pg_publication_rel to the stream "
                         "metadata tables list.";
  }

  VLOG_WITH_FUNC(1) << Format("Creating CDCSDK stream for $0 tables", table_ids.size());

  return CreateNewCdcsdkStream(req, table_ids, namespace_id, resp, epoch, rpc);
}

xrepl::StreamId CatalogManager::GenerateNewXreplStreamId() {
  std::lock_guard l(xrepl_stream_ids_in_use_mutex_);

  while (true) {
    auto stream_id = xrepl::StreamId::GenerateRandom();
    if (!xrepl_stream_ids_in_use_.contains(stream_id)) {
      return stream_id;
    }
  }
}

void CatalogManager::RecoverXreplStreamId(const xrepl::StreamId& stream_id) {
  std::lock_guard l(xrepl_stream_ids_in_use_mutex_);
  xrepl_stream_ids_in_use_.erase(stream_id);
}

Status CatalogManager::CreateNewCdcsdkStream(
    const CreateCDCStreamRequestPB& req, const std::vector<TableId>& table_ids,
    const std::optional<const NamespaceId>& namespace_id, CreateCDCStreamResponsePB* resp,
    const LeaderEpoch& epoch, rpc::RpcContext* rpc) {
  VLOG_WITH_FUNC(1) << "table_ids: " << AsString(table_ids)
                    << ", namespace_id: " << AsString(namespace_id);

  auto start_time = MonoTime::Now();

  bool has_consistent_snapshot_option = false;
  bool consistent_snapshot_option_use = false;
  bool is_history_required_for_replica_identity = false;
  bool disable_dynamic_tables = false;

  CDCStreamInfoPtr stream;
  xrepl::StreamId stream_id = xrepl::StreamId::Nil();

  // Kick-off the CDC state table creation before any other logic.
  RETURN_NOT_OK(CreateCdcStateTableIfNotFound(epoch));

  // TODO(#18934): Move to the DDL transactional atomicity model.
  CDCSDKStreamCreationState cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kInitialized;
  auto se_rollback_failed_create = ScopeExit(
      [this, &stream_id, &cdcsdk_stream_creation_state, &stream] {
    WARN_NOT_OK(
        RollbackFailedCreateCDCSDKStream(cdcsdk_stream_creation_state, stream),
        Format(
            "Failed to cleanup failed CDC stream $0 at state $1",
            stream_id, cdcsdk_stream_creation_state));
  });

  ReplicationSlotName slot_name;
  auto has_replication_slot_name = req.has_cdcsdk_ysql_replication_slot_name();
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (has_replication_slot_name) {
      slot_name = ReplicationSlotName(req.cdcsdk_ysql_replication_slot_name());

      // Duplicate detection.
      if (cdcsdk_replication_slots_to_stream_map_.contains(
              ReplicationSlotName(req.cdcsdk_ysql_replication_slot_name()))) {
        auto stream_id = FindOrNull(cdcsdk_replication_slots_to_stream_map_, slot_name);
        SCHECK(
            stream_id, IllegalState, "Stream with slot name $0 was not found unexpectedly",
            slot_name);
        auto stream = FindOrNull(cdc_stream_map_, *stream_id);
        SCHECK(stream, IllegalState, "Stream with id $0 was not found unexpectedly", stream_id);
        if (!(*stream)->LockForRead()->is_deleting()) {
          return STATUS(
              AlreadyPresent, "CDC stream with the given replication slot name already exists",
              MasterError(MasterErrorPB::OBJECT_ALREADY_PRESENT));
        }

        // A prior replication slot with the same name exists which is in the DELETING state. Remove
        // from the map early so that we don't have to fail this request.
        cdcsdk_replication_slots_to_stream_map_.erase(slot_name);
      }

      if (cdcsdk_replication_slots_to_stream_map_.size() >= FLAGS_max_replication_slots) {
        return STATUS(
            ReplicationSlotLimitReached, "Replication slot limit reached",
            MasterError(MasterErrorPB::REPLICATION_SLOT_LIMIT_REACHED));
      }
    }

    // On a given namespace we allow either new model (replicaion slot) streams or old model
    // (yb-admin) streams. Streams of both types cannot be present on the same namespace.
    for (const auto& entry : cdc_stream_map_) {
      const auto& stream = entry.second;
      if (stream->namespace_id() == namespace_id) {
        if (has_replication_slot_name && stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
          return STATUS(
              IllegalState,
              "Cannot create a replication slot on the same namespace which already has a yb-admin "
              "stream on it. ",
              MasterError(MasterErrorPB::INVALID_REQUEST));
        } else if (
            !has_replication_slot_name && !stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
          return STATUS(
              IllegalState,
              "Cannot create a stream on the same namespace which already has replication slot on "
              "it. ",
              MasterError(MasterErrorPB::INVALID_REQUEST));
        }
      }
    }
  }

  // Check for consistent snapshot option
  if (req.has_cdcsdk_consistent_snapshot_option()) {
    has_consistent_snapshot_option = true;
    consistent_snapshot_option_use =
        req.cdcsdk_consistent_snapshot_option() == CDCSDKSnapshotOption::USE_SNAPSHOT ||
        req.cdcsdk_consistent_snapshot_option() == CDCSDKSnapshotOption::EXPORT_SNAPSHOT;
  }
  has_consistent_snapshot_option =
      has_consistent_snapshot_option && FLAGS_yb_enable_cdc_consistent_snapshot_streams;

  // Check for dynamic tables option
  if (req.has_cdcsdk_stream_create_options() &&
      req.cdcsdk_stream_create_options().has_cdcsdk_dynamic_tables_option()) {
    disable_dynamic_tables = req.cdcsdk_stream_create_options().cdcsdk_dynamic_tables_option() ==
                             CDCSDKDynamicTablesOption::DYNAMIC_TABLES_DISABLED;
  }

  stream_id = GenerateNewXreplStreamId();
  auto se_recover_stream_id = CancelableScopeExit(
      [&stream_id, this] { RecoverXreplStreamId(stream_id); });

  stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
  stream->mutable_metadata()->StartMutation();
  auto* metadata = &stream->mutable_metadata()->mutable_dirty()->pb;
  DCHECK(namespace_id) << "namespace_id is unexpectedly none";
  metadata->set_namespace_id(*namespace_id);
  for (const auto& table_id : table_ids) {
    metadata->add_table_id(table_id);
    if (FLAGS_ysql_yb_enable_replica_identity && has_replication_slot_name) {
      auto table = VERIFY_RESULT(FindTableById(table_id));
      auto schema = VERIFY_RESULT(table->GetSchema());
      // For catalog tables we will use replica identity CHANGE.
      PgReplicaIdentity replica_identity = PgReplicaIdentity::CHANGE;
      if (!table->is_system()) {
        replica_identity = schema.table_properties().replica_identity();
      }

      // If atleast one of the tables in the stream has replica identity other than CHANGE &
      // NOTHING, we will set the history cutoff. UpdatepPeersAndMetrics thread will remove the
      // retention barriers for the tablets belonging to the tables with replica identity CHANGE or
      // NOTHING.
      is_history_required_for_replica_identity |=
          (replica_identity != PgReplicaIdentity::CHANGE &&
           replica_identity != PgReplicaIdentity::NOTHING);

      metadata->mutable_replica_identity_map()->insert({table_id, replica_identity});
      VLOG(1) << "Storing replica identity: " << replica_identity
              << " for table: " << table_id
              << " for stream_id: " << stream_id;
    }
  }

  metadata->set_transactional(req.transactional());

  metadata->mutable_options()->CopyFrom(req.options());

  SysCDCStreamEntryPB::State state = SysCDCStreamEntryPB::ACTIVE;
  if (req.has_initial_state()) {
    state = req.initial_state();
  } else if (has_consistent_snapshot_option) {
    // In case of consistent snapshot option, set state to INITIATED.
    state = SysCDCStreamEntryPB::INITIATED;
  }
  metadata->set_state(state);

  if (has_replication_slot_name) {
    metadata->set_cdcsdk_ysql_replication_slot_name(req.cdcsdk_ysql_replication_slot_name());
    metadata->set_allow_tables_without_primary_key(
        FLAGS_ysql_yb_cdcsdk_stream_tables_without_primary_key);
    metadata->set_detect_publication_changes_implicitly(
        FLAGS_ysql_yb_enable_implicit_dynamic_tables_logical_replication &&
        FLAGS_cdc_enable_dynamic_schema_changes);
  }

  metadata->set_cdcsdk_disable_dynamic_table_addition(disable_dynamic_tables);

  if (req.has_cdcsdk_ysql_replication_slot_plugin_name()) {
    metadata->set_cdcsdk_ysql_replication_slot_plugin_name(
        req.cdcsdk_ysql_replication_slot_plugin_name());
  }

  if (FLAGS_ysql_yb_allow_replication_slot_lsn_types &&
      req.has_cdcsdk_ysql_replication_slot_name() && req.has_cdcsdk_stream_create_options()) {
    RSTATUS_DCHECK(
        req.cdcsdk_stream_create_options().has_lsn_type() &&
            req.cdcsdk_stream_create_options().lsn_type() != ReplicationSlotLsnType_UNSPECIFIED,
        InvalidArgument, "LSN type not present CDC stream creation request");

    metadata->set_cdcsdk_ysql_replication_slot_lsn_type(
        req.cdcsdk_stream_create_options().lsn_type());
  }

  if (FLAGS_ysql_yb_allow_replication_slot_ordering_modes &&
      req.has_cdcsdk_ysql_replication_slot_name() && req.has_cdcsdk_stream_create_options()) {
    RSTATUS_DCHECK(
        req.cdcsdk_stream_create_options().has_ordering_mode() &&
            req.cdcsdk_stream_create_options().ordering_mode() !=
                ReplicationSlotOrderingMode_UNSPECIFIED,
        InvalidArgument, "Ordering mode not present in CDC stream creation request");

    metadata->set_cdcsdk_ysql_replication_slot_ordering_mode(
        req.cdcsdk_stream_create_options().ordering_mode());
  }

  RETURN_NOT_OK(
      TEST_CDCSDKFailCreateStreamRequestIfNeeded("CreateCDCSDKStream::kBeforeSysCatalogEntry"));

  // Update the on-disk system catalog.
  RETURN_NOT_OK(CheckLeaderStatusAndSetupError(
      sys_catalog_->Upsert(leader_ready_term(), stream), "inserting CDC stream into sys-catalog",
      resp));

  cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kPreCommitMutation;
  TRACE("Wrote CDC stream to sys-catalog");

  RETURN_NOT_OK(
      TEST_CDCSDKFailCreateStreamRequestIfNeeded("CreateCDCSDKStream::kBeforeInMemoryStateCommit"));

  // Commit the in-memory state.
  stream->mutable_metadata()->CommitMutation();
  cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kPostCommitMutation;

  {
    // Add the stream to the in-memory map.
    TRACE("Acquired catalog manager lock");
    LockGuard lock(mutex_);
    // Check again before inserting to handle concurrent creates.
    if (has_replication_slot_name && cdcsdk_replication_slots_to_stream_map_.contains(slot_name)) {
      return STATUS(
          AlreadyPresent, "CDC stream with the given replication slot name already exists",
          MasterError(MasterErrorPB::OBJECT_ALREADY_PRESENT));
    }

    cdc_stream_map_[stream->StreamId()] = stream;
    se_recover_stream_id.Cancel();

    for (const auto& table_id : table_ids) {
      cdcsdk_tables_to_stream_map_[table_id].insert(stream->StreamId());
    }
    if (has_replication_slot_name) {
      InsertOrDie(&cdcsdk_replication_slots_to_stream_map_, slot_name, stream->StreamId());
    }
  }
  TRACE("Inserted new CDC stream into CatalogManager maps");

  cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kAddedToMaps;

  resp->set_stream_id(stream->id());

  LOG(INFO) << "Created CDC stream " << stream->ToString();

  RETURN_NOT_OK(
      TEST_CDCSDKFailCreateStreamRequestIfNeeded("CreateCDCSDKStream::kAfterInMemoryStateCommit"));

  // Skip if disable_cdc_state_insert_on_setup is set.
  // If this is a bootstrap (initial state not ACTIVE), let the BootstrapProducer logic take care
  // of populating entries in cdc_state.
  if (PREDICT_FALSE(FLAGS_TEST_disable_cdc_state_insert_on_setup) ||
      (req.has_initial_state() && req.initial_state() != master::SysCDCStreamEntryPB::ACTIVE)) {
    cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kReady;
    return Status::OK();
  }

  // At this point, perform all the ALTER TABLE operations to set all retention barriers
  // This will be called synchronously. That is, once this function returns, we are sure
  // that all of the ALTER TABLE operations have completed.

  uint64 consistent_snapshot_time = 0;
  bool is_history_required_for_record_type = false;
  if (!FLAGS_ysql_yb_enable_replica_identity || !has_replication_slot_name) {
    for (auto option : req.options()) {
      if (option.key() == cdc::kRecordType) {
        is_history_required_for_record_type =
            option.value() != CDCRecordType_Name(cdc::CDCRecordType::CHANGE) &&
            option.value() != CDCRecordType_Name(cdc::CDCRecordType::PG_NOTHING);
      }
    }
  }

  // Step 1: Insert checkpoint Invalid in cdc state table.
  // The rollback mechanism relies on finding entries in the CDC state table, so we do this insert
  // before any change that we would like to revert in case of failures.
  // These inserts are treated as non-consistent snapshot since we haven't yet established the
  // consistent snapshot time.
  RETURN_NOT_OK(PopulateCDCStateTable(
      stream->StreamId(), table_ids, false /* has_consistent_snapshot_option */,
      false /* consistent_snapshot_option_use */, 0 /* ignored */, 0 /* ignored */,
      has_replication_slot_name));

  RETURN_NOT_OK(
      TEST_CDCSDKFailCreateStreamRequestIfNeeded("CreateCDCSDKStream::kAfterDummyCDCStateEntries"));

    // Step 2: Set retention barriers for all tables.
  auto require_history_cutoff = consistent_snapshot_option_use ||
                                is_history_required_for_record_type ||
                                is_history_required_for_replica_identity;
  RETURN_NOT_OK(SetAllCDCSDKRetentionBarriers(
      req, rpc, epoch, table_ids, stream->StreamId(), has_consistent_snapshot_option,
      require_history_cutoff));

  RETURN_NOT_OK(
      TEST_CDCSDKFailCreateStreamRequestIfNeeded("CreateCDCSDKStream::kAfterRetentionBarriers"));

  // Step 3: At this stage, the retention barriers have been set using ALTER TABLE and the
  // SnapshotSafeOpId details have been written to the CDC state table via callback.
  // Establish the consistent snapshot time.
  // This time is the same across all involved tablets and is the mechanism through which
  // consistency is established
  auto stream_creation_time = GetCurrentTimeMicros();
  if (has_consistent_snapshot_option) {
    auto cs_hybrid_time = Clock()->MaxGlobalNow();
    consistent_snapshot_time = cs_hybrid_time.ToUint64();
    LOG(INFO) << "Consistent Snapshot Time for stream " << stream->StreamId().ToString()
              << " is: " << consistent_snapshot_time << " = " << cs_hybrid_time;
      resp->set_cdcsdk_consistent_snapshot_time(consistent_snapshot_time);

    // Save the consistent_snapshot_time in the SysCDCStreamEntryPB catalog
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.mutable_cdcsdk_stream_metadata()->set_snapshot_time(
        consistent_snapshot_time);
    l.mutable_data()->pb.mutable_cdcsdk_stream_metadata()->set_consistent_snapshot_option(
        req.cdcsdk_consistent_snapshot_option());
    l.mutable_data()->pb.set_stream_creation_time(stream_creation_time);
    l.mutable_data()->pb.set_state(SysCDCStreamEntryPB::ACTIVE);
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), stream));
    l.Commit();

    LOG(INFO) << "Updating stream metadata with snapshot time " << stream->ToString();
  }
  RETURN_NOT_OK(PopulateCDCStateTable(
      stream->StreamId(), table_ids, has_consistent_snapshot_option, consistent_snapshot_option_use,
      consistent_snapshot_time, stream_creation_time, has_replication_slot_name));

  RETURN_NOT_OK(TEST_CDCSDKFailCreateStreamRequestIfNeeded(
      "CreateCDCSDKStream::kAfterStoringConsistentSnapshotDetails"));

  cdcsdk_stream_creation_state = CDCSDKStreamCreationState::kReady;

  LOG(INFO) << "Stream " << stream_id << " creation took "
            << MonoTime::Now().GetDeltaSince(start_time).ToMilliseconds() << "ms";

  TRACE("Created CDC state entries");
  return Status::OK();
}

Status CatalogManager::RollbackFailedCreateCDCSDKStream(
    CDCSDKStreamCreationState cdcsdk_stream_creation_state, const CDCStreamInfoPtr& stream) {
  if (cdcsdk_stream_creation_state == CDCSDKStreamCreationState::kInitialized ||
      cdcsdk_stream_creation_state == CDCSDKStreamCreationState::kReady) {
    return Status::OK();
  }

  LOG(WARNING) << "Rolling back the CDC stream creation for stream = " << AsString(stream)
               << ", cdcsdk_stream_creation_state = " << cdcsdk_stream_creation_state;

  switch (cdcsdk_stream_creation_state) {
    case CDCSDKStreamCreationState::kAddedToMaps: {
      std::vector streams{stream};
      RETURN_NOT_OK(DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING));
      LockGuard lock(mutex_);
      return CleanupXReplStreamFromMaps(stream);
    }
    case CDCSDKStreamCreationState::kPreCommitMutation:
      // Call AbortMutation since we didn't commit the in-memory changes so that the write lock
      // is released.
      stream->mutable_metadata()->AbortMutation();
      FALLTHROUGH_INTENDED;
    case CDCSDKStreamCreationState::kPostCommitMutation: {
      std::vector streams(1, stream);
      return DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING);
    }

    case CDCSDKStreamCreationState::kInitialized: FALLTHROUGH_INTENDED;
    case CDCSDKStreamCreationState::kReady:
      VLOG(2) << "Nothing to rollback";
      return Status::OK();
  }

  FATAL_INVALID_ENUM_VALUE(CDCSDKStreamCreationState, cdcsdk_stream_creation_state);
}

Status CatalogManager::PopulateCDCStateTable(const xrepl::StreamId& stream_id,
                                             const std::vector<TableId>& table_ids,
                                             bool has_consistent_snapshot_option,
                                             bool consistent_snapshot_option_use,
                                             uint64_t consistent_snapshot_time,
                                             uint64_t stream_creation_time,
                                             bool has_replication_slot_name) {
  // Validate that the AlterTable callback has populated the checkpoint i.e. it is no longer
  // OpId::Invalid().
  std::unordered_set<TabletId> seen_tablet_ids;
  if (has_consistent_snapshot_option) {
    std::vector<cdc::CDCStateTableKey> cdc_state_entries;
    Status iteration_status;
    auto all_entry_keys = VERIFY_RESULT(cdc_state_table_->GetTableRange(
        cdc::CDCStateTableEntrySelector().IncludeCheckpoint(), &iteration_status));
    for (const auto& entry_result : all_entry_keys) {
      RETURN_NOT_OK(entry_result);
      const auto& entry = *entry_result;

      if (stream_id == entry.key.stream_id) {
        seen_tablet_ids.insert(entry.key.tablet_id);
        SCHECK(
           *entry.checkpoint != OpId().Invalid(), IllegalState,
            Format(
                "Checkpoint for tablet id $0 unexpectedly found Invalid for stream id $1",
                entry.key.tablet_id, stream_id));
      }
    }
    RETURN_NOT_OK(iteration_status);
  }

  std::vector<cdc::CDCStateTableEntry> entries;
  for (const auto& table_id : table_ids) {
    auto table = VERIFY_RESULT(FindTableById(table_id));
    for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
      cdc::CDCStateTableEntry entry(tablet->id(), stream_id);
      if (has_consistent_snapshot_option) {
        // We must have seen this tablet id in the above check for Invalid checkpoint. If not, this
        // means that the list of tablets is different from what it was at the start of the stream
        // creation which indicates a tablet split. In that case, fail the creation and let the
        // client retry the creation again.
        if (!seen_tablet_ids.contains(tablet->id())) {
          return STATUS_FORMAT(
              IllegalState, "CDC State Table entry unexpectedly not found for tablet id $0",
              tablet->id());
        }

        // For USE_SNAPSHOT option, leave entry in POST_SNAPSHOT_BOOTSTRAP state
        // For NOEXPORT_SNAPSHOT option, leave entry in SNAPSHOT_DONE state
        if (consistent_snapshot_option_use)
          entry.snapshot_key = "";

        entry.active_time = stream_creation_time;
        entry.cdc_sdk_safe_time = consistent_snapshot_time;
      } else {
        entry.checkpoint = OpId().Invalid();
        entry.active_time = 0;
        entry.cdc_sdk_safe_time = 0;
      }
      entries.push_back(std::move(entry));

      // For a consistent snapshot streamm, if it is a Colocated table,
      // add the colocated table snapshot entry also
      if (has_consistent_snapshot_option && table->colocated()) {
        cdc::CDCStateTableEntry col_entry(tablet->id(), stream_id, table_id);
        if (consistent_snapshot_option_use)
          col_entry.snapshot_key = "";

        col_entry.active_time = GetCurrentTimeMicros();
        col_entry.cdc_sdk_safe_time = consistent_snapshot_time;
        entries.push_back(std::move(col_entry));
      }
    }
  }

  // Add a new entry in cdc_state table representing the replication slot for the associated stream.
  // This entry holds metadata for two main usages:
  // 1. Represent the slot's consistent point i.e. first record sent in the streaming phase will
  // have LSN & txnID set to 2.
  // 2. Initialize components (LSN & txnID generators) of the CDCSDK Virtual WAL on restarts.
  //
  // If these values are changed here, also update the consistent point sent as part of the
  // creation of logical replication slot in walsender.c and slotfuncs.c.
  if (FLAGS_ysql_yb_enable_replication_slot_consumption && has_consistent_snapshot_option &&
      has_replication_slot_name) {
    cdc::CDCStateTableEntry entry(kCDCSDKSlotEntryTabletId, stream_id);
    std::ostringstream oss;
    oss << consistent_snapshot_time << 'F';
    entry.confirmed_flush_lsn = 2;
    entry.restart_lsn = 1;
    entry.xmin = 1;
    entry.record_id_commit_time = consistent_snapshot_time;
    entry.cdc_sdk_safe_time = consistent_snapshot_time;
    entry.last_pub_refresh_time = consistent_snapshot_time;
    entry.pub_refresh_times = "";
    entry.last_decided_pub_refresh_time = oss.str();
    entries.push_back(entry);
    VLOG(1) << "Added entry in cdc_state for the replication slot with tablet_id: "
            << kCDCSDKSlotEntryTabletId << " stream_id: " << stream_id;
  }

  return cdc_state_table_->UpsertEntries(entries);
}

Status CatalogManager::SetAllCDCSDKRetentionBarriers(
  const CreateCDCStreamRequestPB& req, rpc::RpcContext* rpc, const LeaderEpoch& epoch,
  const std::vector<TableId>& table_ids, const xrepl::StreamId& stream_id,
  const bool has_consistent_snapshot_option, const bool require_history_cutoff) {
  VLOG_WITH_FUNC(4) << "Setting All retention barriers for stream: " << stream_id;

  for (const auto& table_id : table_ids) {
    auto table = VERIFY_RESULT(FindTableById(table_id));
    {
      auto l = table->LockForRead();
      if (l->started_deleting()) {
        return STATUS(
            NotFound, "Table does not exist", table_id,
            MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
    }

    AlterTableRequestPB alter_table_req;
    alter_table_req.mutable_table()->set_table_id(table_id);
    alter_table_req.set_wal_retention_secs(GetAtomicFlag(&FLAGS_cdc_wal_retention_time_secs));

    if (has_consistent_snapshot_option) {
      alter_table_req.set_cdc_sdk_stream_id(stream_id.ToString());
      alter_table_req.set_cdc_sdk_require_history_cutoff(require_history_cutoff);
    }

    AlterTableResponsePB alter_table_resp;
    Status s = this->AlterTable(&alter_table_req, &alter_table_resp, rpc, epoch);
    if (!s.ok()) {
      return STATUS(
          InternalError,
          Format("Unable to set retention barries for table, error: $0", s.message()),
          table_id, MasterError(MasterErrorPB::INTERNAL_ERROR));
    }
  }

  if (has_consistent_snapshot_option) {
    auto deadline = rpc->GetClientDeadline();
    // TODO(#18934): Handle partial failures by rolling back all changes.
    for (const auto& table_id : table_ids) {
      RETURN_NOT_OK(WaitForAlterTableToFinish(table_id, deadline));
    }
    RETURN_NOT_OK(WaitForSnapshotSafeOpIdToBePopulated(stream_id, table_ids, deadline));
  }

  return Status::OK();
}

// This function sets the initial retention barriers on the sys catalog tablet. This is called only
// in the CreateCDCStream context, i.e this will be always called on the master leader. It follows
// the following steps:
//    Step 1: Get the last committed OpId (safe_op_id) from the sys catalog tablet.
//    Step 2: Create and submit a CHANGE_METADATA_OP. The retention barriers will be set on all the
//    peers when this OP is applied.
//    Step 3: Populate the safe_op_id in the cdc_state table upon successful apply of the
//    CHANGE_METADATA_OP in the callback.
Status CatalogManager::SetAllInitialCDCSDKRetentionBarriersOnCatalogTable(
    const TableInfoPtr& table, const xrepl::StreamId& stream_id) {
  auto tablet_peer = sys_catalog_->tablet_peer();

  tablet::RemoveIntentsData data;
  RETURN_NOT_OK(tablet_peer->GetLastReplicatedData(&data));
  OpIdPB safe_op_id;
  safe_op_id.set_term(data.op_id.term);
  safe_op_id.set_index(data.op_id.index);

  // Perform a raft operation (CHANGE_METADATA_OP) to set retention barriers. The barriers will be
  // set in the ChangeMetadataOperation::Apply() path.
  tablet::ChangeMetadataRequestPB cm_req;
  tserver::ChangeMetadataResponsePB resp;
  {
    auto l = table->LockForRead();

    cm_req.set_schema_version(l->pb.version());
    cm_req.set_dest_uuid(master_->permanent_uuid());
    cm_req.set_tablet_id(sys_catalog_->tablet_id());
    cm_req.set_alter_table_id(table->id());
    cm_req.mutable_schema()->CopyFrom(l->pb.schema());

    if (l->pb.has_wal_retention_secs()) {
      cm_req.set_wal_retention_secs(l->pb.wal_retention_secs());
    }

    cm_req.set_retention_requester_id(stream_id.ToString());

    // TODO(#26427): Here we are setting the history retention barriers on the sys catalog tablet.
    // However these barriers will not be moved by UpdatePeersAndMetrics thread but instead will be
    // governed by the flag timestamp_syscatalog_history_retention_interval_sec.
    cm_req.set_cdc_sdk_require_history_cutoff(true);
  }
  auto sys_catalog_tablet = VERIFY_RESULT(sys_catalog_->Tablet());
  auto operation =
      std::make_unique<tablet::ChangeMetadataOperation>(sys_catalog_tablet, tablet_peer->log());
  auto request = operation->AllocateRequest();
  request->CopyFrom(cm_req);

  operation->set_completion_callback([this, table, stream_id, safe_op_id](Status s) {
    // If the apply of CHANGE_METADATA_OP has returned non-ok status, then do not populate the state
    // table entry for the sys catalog tablet. If safe_op_id is not populated in the state table
    // then WaitForSnapshotSafeOpIdToBePopulated() will fail, hence failing stream creation.
    if (!s.ok()) {
      LOG(WARNING) << "Failed to set retention barriers on the sys catalog tablet. Will not "
                      "populate its state table entry for stream: "
                   << stream_id.ToString();
      return;
    }

    WARN_NOT_OK(
        PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
            table, sys_catalog_->tablet_id(), stream_id, safe_op_id,
            master_->clock()->Now() /* proposed_snapshot_time */,
            true /* require_history_cutoff */),
        "Failed to populate the CDC state table entry for the sys catalog tablet. Will fail stream "
        "creation for the stream: " + stream_id.ToString());
  });

  // Submit the CHANGE_METADATA_OP and return. We wait in the CreateCDCStream processing for all
  // retention barriers to be set.
  tablet_peer->Submit(std::move(operation), tablet_peer->LeaderTerm());
  return Status::OK();
}

Status CatalogManager::PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails(
    const scoped_refptr<TableInfo>& table,
    const yb::TabletId& tablet_id,
    const xrepl::StreamId& cdc_sdk_stream_id,
    const yb::OpIdPB& snapshot_safe_opid,
    const yb::HybridTime& proposed_snapshot_time,
    bool require_history_cutoff) {

  TEST_SYNC_POINT("PopulateCDCStateTableWithCDCSDKSnapshotSafeOpIdDetails::Start");

  LOG_WITH_FUNC(INFO) << "Table id: " << table->id()
                      << ", Tablet id: " << tablet_id
                      << ", Stream id:" << cdc_sdk_stream_id.ToString()
                      << ", snapshot safe opid: " << snapshot_safe_opid.term()
                      << " and " << snapshot_safe_opid.index()
                      << ", proposed snapshot time: " << proposed_snapshot_time.ToUint64()
                      << ", require history cutoff: " << require_history_cutoff;

  std::vector<cdc::CDCStateTableEntry> entries;

  cdc::CDCStateTableEntry entry(tablet_id, cdc_sdk_stream_id);
  entry.checkpoint = OpId::FromPB(snapshot_safe_opid);
  entry.cdc_sdk_safe_time = proposed_snapshot_time.ToUint64();
  if (require_history_cutoff)
    entry.snapshot_key = "";

  entry.active_time = GetCurrentTimeMicros();
  entry.last_replication_time = proposed_snapshot_time.GetPhysicalValueMicros();
  entries.push_back(std::move(entry));

  // add the colocated table snapshot row if it is a colocated table
  if (table->colocated()) {
    cdc::CDCStateTableEntry col_entry(tablet_id, cdc_sdk_stream_id, table->id());
    col_entry.checkpoint = OpId::FromPB(snapshot_safe_opid);
    col_entry.cdc_sdk_safe_time = proposed_snapshot_time.ToUint64();
    if (require_history_cutoff)
      col_entry.snapshot_key = "";

    col_entry.active_time = GetCurrentTimeMicros();
    col_entry.last_replication_time = proposed_snapshot_time.GetPhysicalValueMicros();
    entries.push_back(std::move(col_entry));
  }

  RETURN_NOT_OK(TEST_CDCSDKFailCreateStreamRequestIfNeeded(
      "CreateCDCSDKStream::kWhileStoringConsistentSnapshotDetails"));

  return cdc_state_table_->UpsertEntries(entries);
}

Status CatalogManager::PopulateCDCStateTableOnNewTableCreation(
    const scoped_refptr<TableInfo>& table,
    const TabletId& tablet_id,
    const OpId& safe_opid) {

  TEST_SYNC_POINT("PopulateCDCStateTableOnNewTableCreation::Start");

  auto namespace_id = table->namespace_id();
  std::vector<CDCStreamInfoPtr> streams;

  // Get all the CDCSDK streams on the namespace
  {
    SharedLock lock(mutex_);
    for (const auto& entry : cdc_stream_map_) {
      const auto& stream_info = entry.second;
      if (stream_info->IsCDCSDKStream() && stream_info->namespace_id() == namespace_id) {
        streams.emplace_back(stream_info);
      }
    }
  }

  // This is not expected to happen since we check atleast one stream exists before calling create
  // tablet rpc
  RSTATUS_DCHECK(
      !streams.empty(), NotFound, "Did not find any stream on the namespace: $0", namespace_id);

  std::vector<cdc::CDCStateTableEntry> entries;
  entries.reserve(streams.size());

  for (auto const& stream : streams) {
    entries.emplace_back(tablet_id, stream->StreamId());
    auto& entry = entries.back();
    if (stream->IsConsistentSnapshotStream()) {
      auto consistent_snapshot_time = stream->GetConsistentSnapshotHybridTime();
      entry.checkpoint = safe_opid;
      entry.active_time = GetCurrentTimeMicros();
      entry.cdc_sdk_safe_time = consistent_snapshot_time.ToUint64();
      entry.last_replication_time = consistent_snapshot_time.GetPhysicalValueMicros();
    } else {
      entry.checkpoint = OpId::Invalid();
      entry.active_time = 0;
      entry.cdc_sdk_safe_time = 0;
    }
    LOG_WITH_FUNC(INFO) << "Table id: " << table->id() << ", tablet id: " << tablet_id
                        << ", stream id: " << stream->StreamId()
                        << ", Safe OpId: " << safe_opid.term << " and " << safe_opid.index
                        << ", cdc_sdk_safe_time: " << *(entry.cdc_sdk_safe_time);
  }

  auto status = cdc_state_table_->InsertEntries(entries);
  if (!status.ok()) {
    LOG(WARNING) << "Encoutered error while trying to add tablet:" << tablet_id
                 << " of table: " << table->id() << ", to cdc_state table: " << status;
    return status;
  }

  TEST_SYNC_POINT("PopulateCDCStateTableOnNewTableCreation::End");
  return Status::OK();
}

Status CatalogManager::WaitForSnapshotSafeOpIdToBePopulated(
    const xrepl::StreamId& stream_id, const std::vector<TableId>& table_ids,
    CoarseTimePoint deadline) {

  auto num_expected_tablets = 0;
  auto sys_catalog_tablet_seen = false;
  for (const auto& table_id : table_ids) {
    auto table = VERIFY_RESULT(FindTableById(table_id));
    if (table->is_system()) {
      // We do not add snapshot entries for catalog tables. Since all the catalog tables reside on
      // the same tablet we count them only once.
      if (!sys_catalog_tablet_seen) {
        num_expected_tablets += table->TabletCount();
        sys_catalog_tablet_seen = true;
      }
    } else {
      num_expected_tablets += table->TabletCount();
    }
  }

  return WaitFor(
      [&stream_id, &num_expected_tablets, this]() -> Result<bool> {
        VLOG(1) << "Checking snapshot safe opids for stream: " << stream_id;

        std::vector<cdc::CDCStateTableKey> cdc_state_entries;
        Status iteration_status;
        auto all_entry_keys = VERIFY_RESULT(cdc_state_table_->GetTableRange(
            cdc::CDCStateTableEntrySelector().IncludeCheckpoint(), &iteration_status));

        auto num_rows = 0;
        for (const auto& entry_result : all_entry_keys) {
          RETURN_NOT_OK(entry_result);
          const auto& entry = *entry_result;

          if (stream_id == entry.key.stream_id) {
            num_rows++;
            if (!entry.checkpoint.has_value() || *entry.checkpoint == OpId().Invalid()) {
              return false;
            }
          }
        }

        RETURN_NOT_OK(iteration_status);
        VLOG(1) << "num_rows=" << num_rows << ", num_expected_tablets=" << num_expected_tablets;
        // In case of colocated tables, there would be extra rows, check for >=
        return (num_rows >= num_expected_tablets);
      },
      deadline - CoarseMonoClock::now(),
      Format("Waiting for snapshot safe opids to be populated for stream_id: $0", stream_id),
      500ms /* initial_delay */, 1 /* delay_multiplier */);
}

Status CatalogManager::DeleteCDCStream(
    const DeleteCDCStreamRequestPB* req, DeleteCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteCDCStream request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  if (req->stream_id_size() == 0 && req->cdcsdk_ysql_replication_slot_name_size() == 0) {
    return STATUS(
        InvalidArgument, "No CDC Stream ID or YSQL Replication Slot Name given",
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  std::vector<CDCStreamInfoPtr> streams;
  {
    SharedLock lock(mutex_);

    for (const auto& stream_id : req->stream_id()) {
      auto stream_opt = VERIFY_RESULT(GetStreamIfValidForDelete(
          VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)), req->force_delete()));
      if (stream_opt) {
        streams.emplace_back(std::move(*stream_opt));
      } else {
        resp->add_not_found_stream_ids(stream_id);
      }
    }

    for (const auto& replication_slot_name : req->cdcsdk_ysql_replication_slot_name()) {
      auto slot_name = ReplicationSlotName(replication_slot_name);
      auto stream_it = FindOrNull(cdcsdk_replication_slots_to_stream_map_, slot_name);
      auto stream_id = stream_it ? *stream_it : xrepl::StreamId::Nil();
      auto stream_opt =
          VERIFY_RESULT(GetStreamIfValidForDelete(std::move(stream_id), req->force_delete()));
      if (stream_opt) {
        streams.emplace_back(std::move(*stream_opt));
      } else {
        resp->add_not_found_cdcsdk_ysql_replication_slot_names(replication_slot_name);
      }
    }
  }

  const auto& not_found_stream_ids = resp->not_found_stream_ids();
  const auto& not_found_cdcsdk_ysql_replication_slot_names =
      resp->not_found_cdcsdk_ysql_replication_slot_names();
  if ((!not_found_stream_ids.empty() || !not_found_cdcsdk_ysql_replication_slot_names.empty()) &&
      !req->ignore_errors()) {
    std::vector<std::string> missing_streams(
        resp->not_found_stream_ids_size() +
        resp->not_found_cdcsdk_ysql_replication_slot_names_size());
    missing_streams.insert(
        missing_streams.end(), not_found_stream_ids.begin(), not_found_stream_ids.end());
    missing_streams.insert(
        missing_streams.end(), not_found_cdcsdk_ysql_replication_slot_names.begin(),
        not_found_cdcsdk_ysql_replication_slot_names.end());
    return STATUS(
        NotFound,
        Format(
            "Did not find all requested CDC streams. Missing streams: [$0]. Request: $1",
            JoinStrings(missing_streams, ","), req->ShortDebugString()),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  // Do not delete them here, just mark them as DELETING and the catalog manager background thread
  // will handle the deletion.
  Status s = DropXReplStreams(streams, SysCDCStreamEntryPB::DELETING);
  if (!s.ok()) {
    if (s.IsIllegalState()) {
      PANIC_RPC(rpc, s.message().ToString());
    }
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  LOG(INFO) << "Successfully deleted CDC streams " << CDCStreamInfosAsString(streams)
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Result<std::optional<CDCStreamInfoPtr>> CatalogManager::GetStreamIfValidForDelete(
    const xrepl::StreamId& stream_id, bool force_delete) {
  auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  if (stream == nullptr || stream->LockForRead()->started_deleting()) {
    return std::nullopt;
  }

  auto ltm = stream->LockForRead();
  if (!force_delete && ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE) {
    for (const auto& option : ltm->pb.options()) {
      if (option.key() == "record_format") {
        if (option.value() == "WAL") {
          return STATUS(
              NotSupported,
              "Cannot delete an xCluster Stream in replication. "
              "Use 'force_delete' to override",
              MasterError(MasterErrorPB::INVALID_REQUEST));
        }
        break;
      }
    }
  }
  return stream;
}

Status CatalogManager::DropXReplStreams(
    std::vector<CDCStreamInfoPtr>& streams, SysCDCStreamEntryPB::State delete_state) {
  if (streams.empty()) {
    return Status::OK();
  }
  std::sort(streams.begin(), streams.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->StreamId() < rhs->StreamId();
  });
  RSTATUS_DCHECK(
      delete_state == SysCDCStreamEntryPB::DELETING_METADATA ||
          delete_state == SysCDCStreamEntryPB::DELETING,
      IllegalState,
      Format("Invalid delete state $0 provided", SysCDCStreamEntryPB::State_Name(delete_state)));

  std::vector<CDCStreamInfo::WriteLock> locks;
  std::vector<CDCStreamInfo*> streams_to_mark;
  locks.reserve(streams.size());
  for (auto& stream : streams) {
    auto l = stream->LockForWrite();
    if (l.data().is_deleting()) {
      LOG(WARNING) << "Stream " << stream->ToString() << " is already in "
                   << SysCDCStreamEntryPB::State_Name(l.data().pb.state())
                   << " state. Cannot change it's state to "
                   << SysCDCStreamEntryPB::State_Name(delete_state);
      continue;
    } else {
      l.mutable_data()->pb.set_state(delete_state);
    }
    locks.push_back(std::move(l));
    streams_to_mark.push_back(stream.get());
  }

  if (streams_to_mark.empty()) {
    return Status::OK();
  }

  bool TEST_fail = false;
  TEST_SYNC_POINT_CALLBACK("DropXReplStreams::FailBeforeStreamsStateChangesPersisted", &TEST_fail);
  if (TEST_fail) {
    LOG(INFO) << "Failed before streams " << CDCStreamInfosAsString(streams_to_mark)
              << " states are persisted to " << SysCDCStreamEntryPB::State_Name(delete_state)
              << " in sys catalog";
  }
  SCHECK(!TEST_fail, IllegalState, "Failing for TESTING");

  // The mutation will be aborted when 'l' exits the scope on early return.
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), streams_to_mark),
      "updating XRepl streams in sys-catalog"));
  LOG(INFO) << "Successfully marked XRepl streams " << CDCStreamInfosAsString(streams_to_mark)
            << " as " << SysCDCStreamEntryPB::State_Name(delete_state) << " in sys catalog";
  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::FindCDCSDKStreamsForAddedTables(
    TableStreamIdsMap* table_to_unprocessed_streams_map) {
  std::unordered_map<NamespaceId, std::unordered_set<TableId>> namespace_to_unprocessed_table_map;
  {
    SharedLock lock(cdcsdk_unprocessed_table_mutex_);
    int32_t found_unprocessed_tables = 0;
    for (const auto& [ns_id, table_ids] : namespace_to_cdcsdk_unprocessed_table_map_) {
      for (const auto& table_id : table_ids) {
        namespace_to_unprocessed_table_map[ns_id].insert(table_id);
        if (++found_unprocessed_tables >= FLAGS_cdcsdk_table_processing_limit_per_run) {
          break;
        }
      }
      if (found_unprocessed_tables == FLAGS_cdcsdk_table_processing_limit_per_run) {
        break;
      }
    }
  }

  SharedLock lock(mutex_);
  for (const auto& [stream_id, stream_info] : cdc_stream_map_) {
    if (stream_info->namespace_id().empty()) {
      continue;
    }

    // skip streams on which dynamic table addition is disabled.
    if(stream_info->IsDynamicTableAdditionDisabled()) {
      continue;
    }

    auto const unprocessed_tables =
        FindOrNull(namespace_to_unprocessed_table_map, stream_info->namespace_id());
    if (!unprocessed_tables) {
      continue;
    }

    auto ltm = stream_info->LockForRead();
    if (ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE ||
        ltm->pb.state() == SysCDCStreamEntryPB::DELETING_METADATA) {
      for (const auto& unprocessed_table_id : *unprocessed_tables) {
        auto table = tables_->FindTableOrNull(unprocessed_table_id);
        if (!table) {
          LOG_WITH_FUNC(WARNING) << "Table " << unprocessed_table_id
                                 << " deleted before it could be processed";
          continue;
        }

        if (!IsTableEligibleForCDCSDKStream(
                table, table->LockForRead(), /*check_schema=*/true,
                stream_info->IsTablesWithoutPrimaryKeyAllowed())) {
          continue;
        }

        bool present_in_qualified_table_list =
            std::find(ltm->table_id().begin(), ltm->table_id().end(), unprocessed_table_id) !=
            ltm->table_id().end();
        bool present_in_unqualified_table_list = false;
        if (ltm->pb.unqualified_table_id_size() > 0) {
          present_in_unqualified_table_list =
              std::find(
                  ltm->unqualified_table_id().begin(), ltm->unqualified_table_id().end(),
                  unprocessed_table_id) != ltm->unqualified_table_id().end();
        }

        if (!present_in_qualified_table_list && !present_in_unqualified_table_list) {
          (*table_to_unprocessed_streams_map)[unprocessed_table_id].push_back(stream_info);
          VLOG(1) << "Will try and add table: " << unprocessed_table_id
                  << ", to stream: " << stream_info->id();
        }
      }
    }
  }

  for (const auto& [ns_id, unprocessed_table_ids] : namespace_to_unprocessed_table_map) {
    for (const auto& unprocessed_table_id : unprocessed_table_ids) {
      if (!table_to_unprocessed_streams_map->contains(unprocessed_table_id)) {
        // This means we found no active CDCSDK stream where this table was missing, hence we can
        // remove this table from 'RemoveTableFromCDCSDKUnprocessedMap'.
        RemoveTableFromCDCSDKUnprocessedMap(unprocessed_table_id, ns_id);
      }
    }
  }

  return Status::OK();
}

void CatalogManager::FindAllTablesMissingInCDCSDKStream(
    const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<std::string>& table_ids,
    const std::vector<TableInfoPtr>& eligible_tables_info,
    const google::protobuf::RepeatedPtrField<std::string>& unqualified_table_ids) {
  std::unordered_set<TableId> stream_qualified_table_ids;
  std::unordered_set<TableId> stream_unqualified_table_ids;
  // Store all qualified table_ids associated with the stream in 'stream_qualified_table_ids'.
  for (const auto& table_id : table_ids) {
    stream_qualified_table_ids.insert(table_id);
  }

  for (const auto& table_id : unqualified_table_ids) {
    stream_unqualified_table_ids.insert(table_id);
  }

  // Get all the tables associated with the namespace.
  // If we find any table present only in the namespace, but not in the stream's qualified &
  // unqualified table list, we add the table id to 'cdcsdk_unprocessed_tables'.
  for (const auto& table_info : eligible_tables_info) {
    auto ltm = table_info->LockForRead();
    if (!stream_qualified_table_ids.contains(table_info->id()) &&
        !stream_unqualified_table_ids.contains(table_info->id())) {
      LOG(INFO) << "Found unprocessed table: " << table_info->id()
                << ", for stream: " << stream_id;
      LockGuard lock(cdcsdk_unprocessed_table_mutex_);
      namespace_to_cdcsdk_unprocessed_table_map_[table_info->namespace_id()].insert(
          table_info->id());
    }
  }
}

Status CatalogManager::FindCDCSDKStreamsForNonEligibleTables(
    TableStreamIdsMap* non_user_tables_to_streams_map) {
  std::unordered_map<NamespaceId, std::unordered_set<TableId>> namespace_to_non_user_table_map;
  {
    SharedLock lock(cdcsdk_non_eligible_table_mutex_);
    int32_t found_non_user_tables = 0;
    for (const auto& [ns_id, table_ids] : namespace_to_cdcsdk_non_eligible_table_map_) {
      for (const auto& table_id : table_ids) {
        namespace_to_non_user_table_map[ns_id].insert(table_id);
        if (++found_non_user_tables >= FLAGS_cdcsdk_table_processing_limit_per_run) {
          break;
        }
      }

      if (found_non_user_tables == FLAGS_cdcsdk_table_processing_limit_per_run) {
        break;
      }
    }
  }

  if (namespace_to_non_user_table_map.empty()) {
    return Status::OK();
  }

  {
    SharedLock lock(mutex_);
    for (const auto& [stream_id, stream_info] : cdc_stream_map_) {
      if (stream_info->namespace_id().empty()) {
        continue;
      }

      // Removal of non-eligible tables will only be done on CDC stream that are not associated
      // with a replication slot.
      if (!stream_info->GetCdcsdkYsqlReplicationSlotName().empty()) {
        continue;
      }

      const auto non_user_tables =
          FindOrNull(namespace_to_non_user_table_map, stream_info->namespace_id());
      if (!non_user_tables) {
        continue;
      }

      auto ltm = stream_info->LockForRead();
      if (ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE ||
          ltm->pb.state() == SysCDCStreamEntryPB::DELETING_METADATA) {
        for (const auto& non_user_table_id : *non_user_tables) {
          auto table = tables_->FindTableOrNull(non_user_table_id);
          if (!table) {
            LOG_WITH_FUNC(WARNING)
                << "Table " << non_user_table_id << " deleted before it could be removed";
            continue;
          }

          if (std::find(ltm->table_id().begin(), ltm->table_id().end(), non_user_table_id) !=
              ltm->table_id().end()) {
            (*non_user_tables_to_streams_map)[non_user_table_id].push_back(stream_info);
            VLOG(1) << "Will try and remove table: " << non_user_table_id
                    << ", from stream: " << stream_info->id();
          }
        }
      }
    }
  }

  for (const auto& [ns_id, non_user_table_ids] : namespace_to_non_user_table_map) {
    for (const auto& non_user_table_id : non_user_table_ids) {
      if (!non_user_tables_to_streams_map->contains(non_user_table_id)) {
        // This means we found no active CDCSDK stream where this table was present, hence we can
        // remove this table from 'namespace_to_cdcsdk_non_eligible_table_map_'.
        RemoveTableFromCDCSDKNonEligibleTableMap(non_user_table_id, ns_id);
      }
    }
  }

  return Status::OK();
}

void CatalogManager::FindAllNonEligibleTablesInCDCSDKStream(
    const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<std::string>& table_ids,
    const std::vector<TableInfoPtr>& eligible_tables_info) {
  // If we find any table present only in the the stream, but not in the list of eligible tables in
  // namespace for CDC, we add the table id to 'namespace_to_cdcsdk_non_eligible_table_map_'.
  std::unordered_set<TableId> user_table_ids;
  for (const auto& table_info : eligible_tables_info) {
    user_table_ids.insert(table_info->id());
  }

  auto stream_info = FindPtrOrNull(cdc_stream_map_, stream_id);
  DCHECK(stream_info);
  std::unordered_set<TableId> stream_table_ids;
  // Store all table_ids associated with the stream in 'stream_table_ids'.
  for (const auto& table_id : table_ids) {
    if (!user_table_ids.contains(table_id)) {
      auto table_info = GetTableInfoUnlocked(table_id);
      if (table_info) {
        // Re-confirm this table is not meant to be part of a CDC stream.
        if (!IsTableEligibleForCDCSDKStream(
                table_info, table_info->LockForRead(), /*check_schema=*/true,
                stream_info->IsTablesWithoutPrimaryKeyAllowed())) {
          LOG(INFO) << "Found a non-eligible table: " << table_info->id()
                    << ", for stream: " << stream_id;
          LockGuard lock(cdcsdk_non_eligible_table_mutex_);
          namespace_to_cdcsdk_non_eligible_table_map_[table_info->namespace_id()].insert(
              table_info->id());
        } else {
          // Ideally we are not expected to enter the else clause.
          LOG(WARNING) << "Found table " << table_id << " in metadata of stream " << stream_id
                       << " that is not present in the eligible list of tables "
                          "from the namespace for CDC";
        }
      } else {
        LOG(INFO) << "Found table " << table_id << " in stream " << stream_id
                  << " metadata that is not present in master.";
      }
    }
  }
}

void CatalogManager::FindAllUnproccesedUnqualifiedTablesInCDCSDKStream(
    const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<std::string>& qualified_table_ids,
    const google::protobuf::RepeatedPtrField<std::string>& unqualified_table_ids,
    const std::vector<TableInfoPtr>& eligible_tables_info) {
  std::unordered_set<TableId> eligible_tables_for_stream;
  std::unordered_set<TableId> qualified_tables_in_stream;
  for (const auto& table : eligible_tables_info) {
    eligible_tables_for_stream.insert(table->id());
  }

  qualified_tables_in_stream.insert(qualified_table_ids.begin(), qualified_table_ids.end());

  // Unprocessed unqualified tables will be present in both the lists (qualified & unqualified).
  for (const auto& unqualified_table_id : unqualified_table_ids) {
    if (qualified_tables_in_stream.contains(unqualified_table_id)) {
      DCHECK(eligible_tables_for_stream.contains(unqualified_table_id));
      LOG(INFO) << "Found an unprocessed unqualified table " << unqualified_table_id
                << " for stream: " << stream_id;
      LockGuard lock(cdcsdk_unqualified_table_removal_mutex_);
      cdcsdk_unprocessed_unqualified_tables_to_streams_[unqualified_table_id].insert(stream_id);
    }
  }
}

Status CatalogManager::FindCDCSDKStreamsForUnprocessedUnqualifiedTables(
    TableStreamIdsMap* tables_to_be_removed_streams_map) {
  std::unordered_map<TableId, std::unordered_set<xrepl::StreamId>> unprocessed_table_to_streams_map;
  {
    SharedLock l(cdcsdk_unqualified_table_removal_mutex_);
    int unprocessed_tables = 0;
    for (const auto& [table_id, streams] : cdcsdk_unprocessed_unqualified_tables_to_streams_) {
      unprocessed_table_to_streams_map[table_id] = streams;
      if (++unprocessed_tables >= FLAGS_cdcsdk_table_processing_limit_per_run) {
        break;
      }
    }
  }

  if (unprocessed_table_to_streams_map.empty()) {
    return Status::OK();
  }

  std::unordered_map<TableId, std::unordered_set<xrepl::StreamId>> streams_not_to_be_processed;
  {
    SharedLock lock(mutex_);
    for (const auto& [table_id, streams] : unprocessed_table_to_streams_map) {
      for (const auto& stream_id : streams) {
        CDCStreamInfoPtr stream;
        stream = FindPtrOrNull(cdc_stream_map_, stream_id);

        Status s = ValidateStreamForTableRemoval(stream);
        if (!s.ok()) {
          // This stream cannot be processed for removal of tables, therefore delete the stream from
          // the set.
          streams_not_to_be_processed[table_id].insert(stream_id);
          continue;
        }

        (*tables_to_be_removed_streams_map)[table_id].push_back(stream);
        VLOG(1) << "Will try to remove table: " << table_id
                << ", from stream: " << stream->StreamId();
      }
    }
  }

  for (const auto& [table_id, streams] : streams_not_to_be_processed) {
    // For each table, remove all streams that cannot not be processed from
    // 'cdcsdk_unprocessed_unqualified_tables_to_streams_' map.
    RemoveStreamsFromUnprocessedRemovedTableMap(table_id, streams);
  }

  return Status::OK();
}

Status CatalogManager::ValidateCDCSDKRequestProperties(
    const CreateCDCStreamRequestPB& req, const std::string& source_type_option_value,
    const std::string& record_type_option_value, const std::string& id_type_option_value) {
  if (source_type_option_value != CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK)) {
    RETURN_INVALID_REQUEST_STATUS("Namespace CDC stream is only supported for CDCSDK");
  }

  if (id_type_option_value != cdc::kNamespaceId) {
    RETURN_INVALID_REQUEST_STATUS(
        "Invalid id_type in options. Expected to be NAMESPACEID for all CDCSDK streams");
  }

  if (!FLAGS_ysql_yb_enable_replication_commands &&
      req.has_cdcsdk_ysql_replication_slot_name()) {
    // Should never happen since the YSQL commands also check the flag.
    RETURN_INVALID_REQUEST_STATUS(
        "Creation of CDCSDK stream with a replication slot name is disallowed");
  }

  if (!FLAGS_ysql_yb_allow_replication_slot_lsn_types && req.has_cdcsdk_stream_create_options() &&
      req.cdcsdk_stream_create_options().has_lsn_type()) {
    RETURN_INVALID_REQUEST_STATUS(
        "Creation of CDCSDK stream with a replication slot having LSN type is disallowed because "
        "the flag ysql_yb_allow_replication_slot_lsn_types is disabled");
  }

  if (!FLAGS_ysql_yb_allow_replication_slot_ordering_modes &&
      req.has_cdcsdk_stream_create_options() &&
      req.cdcsdk_stream_create_options().has_ordering_mode()) {
    RETURN_INVALID_REQUEST_STATUS(
        "Creation of CDCSDK stream with a replication slot having ordering mode is disallowed "
        "because the flag ysql_yb_allow_replication_slot_ordering_modes is disabled");
  }

  // TODO: Validate that the replication slot output plugin name is provided if
  // ysql_yb_enable_replication_slot_consumption is true. This can only be done after we have
  // fully deprecated the yb-admin commands for CDC stream creation.

  // No need to validate the record_type for replication slot consumption.
  if (FLAGS_ysql_yb_enable_replica_identity && req.has_cdcsdk_ysql_replication_slot_name()) {
    return Status::OK();
  }

  cdc::CDCRecordType record_type_pb;
  if (!cdc::CDCRecordType_Parse(record_type_option_value, &record_type_pb)) {
    return STATUS(InvalidArgument, "Invalid CDCRecordType value", record_type_option_value);
  }

  switch (record_type_pb) {
    case cdc::CDCRecordType::PG_FULL:
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::PG_CHANGE_OLD_NEW:
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::PG_DEFAULT:
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::PG_NOTHING: {
      SCHECK(
          FLAGS_cdc_enable_postgres_replica_identity, InvalidArgument,
          "Using new record types is disallowed in the middle of an upgrade. Finalize the upgrade "
          "and try again.",
          (req));
      break;
    }
    case cdc::CDCRecordType::ALL:
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::FULL_ROW_NEW_IMAGE:
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES:
      // TODO(#19930): Disallow older record types once we have disallowed the YSQL CDC commands in
      // yb-admin.
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::CHANGE: {
      return Status::OK();
    }
  }

  return Status::OK();
}

std::vector<TableInfoPtr> CatalogManager::FindAllTablesForCDCSDK(
    const NamespaceId& ns_id, const bool allow_tables_without_primary_key) {
  std::vector<TableInfoPtr> tables;

  for (const auto& table_info : tables_->GetAllTables()) {
    auto ltm = table_info->LockForRead();
    if (!ltm->visible_to_client() || ltm->namespace_id() != ns_id) {
      continue;
    }

    if (!IsTableEligibleForCDCSDKStream(
            table_info.get(), ltm, /*check_schema=*/true, allow_tables_without_primary_key)) {
      continue;
    }

    tables.push_back(table_info);
  }

  return tables;
}

bool CatalogManager::IsTableEligibleForCDCSDKStream(
    const TableInfoPtr& table_info, const TableInfo::ReadLock& lock, bool check_schema,
    const bool allow_tables_without_primary_key) const {
  if (check_schema) {
    if (!allow_tables_without_primary_key && !table_info->HasUserSpecifiedPrimaryKey(lock)) {
      VLOG(1) << "Table: " << table_info->id()
              << ", will not be added to CDCSDK stream, since it does not have a primary key";
      return false;
    }

    // Allow adding user created indexes to CDC stream.
    if (FLAGS_TEST_cdcsdk_add_indexes_to_stream && table_info->IsUserIndex(lock)) {
      return true;
    }
  }

  if (lock->pb.is_matview()) {
    // Materialized view should not be added as they are not supported for streaming.
    return false;
  }

  if (!table_info->IsUserTable(lock)) {
    // Non-user tables like indexes, system tables etc should not be added as they are not
    // supported for streaming.
    return false;
  }

  return true;
}

/*
 * Processing for relevant tables that have been added after the creation of a stream
 * This involves
 *   1) Enabling the WAL retention for the tablets of the table
 *   2) INSERTING records for the tablets of this table and each stream for which
 *      this table is relevant into the cdc_state table. This is not requirred for replication slot
 *      consumption since setting up of retention barriers and inserting state table entries is done
 *      at the time of table creation.
 *   3) Storing the replica identity of the table in the stream metadata
 */
Status CatalogManager::ProcessNewTablesForCDCSDKStreams(
    const TableStreamIdsMap& table_to_unprocessed_streams_map,
    const LeaderEpoch& epoch) {
  int32_t processed_tables = 0;
  for (const auto& [table_id, streams] : table_to_unprocessed_streams_map) {
    if (processed_tables++ >= FLAGS_cdcsdk_table_processing_limit_per_run) {
      VLOG(1) << "Reached the limit of number of newly added tables to process per iteration. Will "
                 "process the reamining tables in the next iteration.";
      break;
    }

    GetTableLocationsRequestPB req;
    GetTableLocationsResponsePB resp;
    Status s;
    req.mutable_table()->set_table_id(table_id);
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
    req.set_require_tablets_running(true);
    req.set_include_inactive(false);

    s = GetTableLocations(&req, &resp);

    TEST_SYNC_POINT("ProcessNewTablesForCDCSDKStreams::Start");
    if (!s.ok()) {
      if (s.IsNotFound()) {
        // The table has been deleted. We will remove the table's entry from the stream's
        // metadata.
        RemoveTableFromCDCSDKUnprocessedMap(table_id, streams.begin()->get()->namespace_id());
        VLOG(1) << "Removed table: " << table_id
                << ", from namespace_to_cdcsdk_unprocessed_table_map_ , beacuse table not found";
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

    // Since an entry is made to table_to_unprocessed_streams_map only when there exists a stream on
    // the namespace of dynamically created table, each table in table_to_unprocessed_streams_map
    // will have atleast one corresponding stream.
    DCHECK(!streams.empty());

    // Since for a given namespace all the streams on it can either belong to the replication slot
    // consumption model or the older (YB connector) consumption model, we check the first stream
    // for each table in table_to_unprocessed_streams_map to determine which replication model is
    // active on namespace to which the table belongs.
    bool has_replication_slot_consumption =
        !streams.front()->GetCdcsdkYsqlReplicationSlotName().empty();

    if (!FLAGS_ysql_yb_enable_replication_slot_consumption || !has_replication_slot_consumption) {
      // Set the WAL retention for this new table
      // Make asynchronous ALTER TABLE requests to do this, just as was done during stream creation
      AlterTableRequestPB alter_table_req;
      alter_table_req.mutable_table()->set_table_id(table_id);
      alter_table_req.set_wal_retention_secs(FLAGS_cdc_wal_retention_time_secs);
      AlterTableResponsePB alter_table_resp;
      s = this->AlterTable(&alter_table_req, &alter_table_resp, /*rpc=*/nullptr, epoch);
      if (!s.ok()) {
        LOG(WARNING) << "Unable to change the WAL retention time for table " << table_id;
        continue;
      }
    }

    NamespaceId namespace_id;
    bool stream_pending = false;
    Status status;
    for (const auto& stream : streams) {
      if PREDICT_FALSE (stream == nullptr) {
        LOG(WARNING) << "Could not find CDC stream: " << stream->id();
        continue;
      }

      // INSERT the required cdc_state table entries. This is not requirred for replication slot
      // consumption since setting up of retention barriers and inserting state table entries is
      // done at the time of table creation.
      if (!FLAGS_ysql_yb_enable_replication_slot_consumption ||
          !has_replication_slot_consumption) {
        const auto& tablets = resp.tablet_locations();
        std::vector<cdc::CDCStateTableEntry> entries;
        entries.reserve(tablets.size());

        for (const auto& tablet : tablets) {
          cdc::CDCStateTableEntry entry(tablet.tablet_id(), stream->StreamId());
          entry.checkpoint = OpId::Invalid();
          entry.active_time = 0;
          entry.cdc_sdk_safe_time = 0;
          entries.push_back(std::move(entry));
        }

        status = cdc_state_table_->InsertEntries(entries);

        if (!status.ok()) {
          LOG(WARNING) << "Encoutered error while trying to add tablets of table: " << table_id
                       << ", to cdc_state table for stream" << stream->id() << ": " << status;
          stream_pending = true;
          continue;
        }
      }

      auto stream_lock = stream->LockForWrite();
      if (stream_lock->is_deleting()) {
        continue;
      }

      if (stream_lock->pb.unqualified_table_id_size() > 0) {
        // Skip adding the table to qualified table list if the table_id is present in the
        // unqualified table list for the stream.
        auto table_id_itr = std::find(
            stream_lock->unqualified_table_id().begin(), stream_lock->unqualified_table_id().end(),
            table_id);
        if (table_id_itr != stream_lock->unqualified_table_id().end()) {
          continue;
        }
      }

      stream_lock.mutable_data()->pb.add_table_id(table_id);

      // Store the replica identity information of the table in the stream metadata for replication
      // slot consumption.
      if (FLAGS_ysql_yb_enable_replica_identity && has_replication_slot_consumption) {
        auto table = VERIFY_RESULT(FindTableById(table_id));
        auto schema = VERIFY_RESULT(table->GetSchema());
        PgReplicaIdentity replica_identity = schema.table_properties().replica_identity();

        stream_lock.mutable_data()->pb.mutable_replica_identity_map()->insert(
            {table_id, replica_identity});
        VLOG(1) << "Storing replica identity: " << replica_identity
                << " for table: " << table_id
                << " for stream_id: " << stream->StreamId();
      }

      // Also need to persist changes in sys catalog.
      status = sys_catalog_->Upsert(leader_ready_term(), stream);
      if (!status.ok()) {
        LOG(WARNING) << "Encountered error while trying to update sys_catalog of stream: "
                     << stream->id() << ", with table: " << table_id;
        stream_pending = true;
        continue;
      }

      // Add the table/ stream pair details to 'cdcsdk_tables_to_stream_map_', so that parent
      // tablets on which tablet split is successful will be hidden rather than deleted straight
      // away, as needed.
      {
        LockGuard lock(mutex_);
        cdcsdk_tables_to_stream_map_[table_id].insert(stream->StreamId());
      }
      stream_lock.Commit();
      LOG(INFO) << "Added tablets of table: " << table_id
                << ", to cdc_state table for stream: " << stream->id();

      namespace_id = stream->namespace_id();
    }

    // Remove processed tables from 'namespace_to_unprocessed_table_map_'.
    if (!stream_pending) {
      RemoveTableFromCDCSDKUnprocessedMap(table_id, namespace_id);
    }
  }

  return Status::OK();
}

Status CatalogManager::ValidateStreamForTableRemoval(const CDCStreamInfoPtr& stream) {
  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(
        NotFound, "Could not find CDC stream", MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  if (!stream->IsCDCSDKStream()) {
    return STATUS(NotSupported, "Not a CDC stream");
  }

  if (!stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
    return STATUS(
        NotSupported,
        "Operation not supported on CDC streams that are associated with a replication slot");
  }

  return Status::OK();
}

Status CatalogManager::ValidateTableForRemovalFromCDCSDKStream(
    const scoped_refptr<TableInfo>& table, bool check_for_ineligibility) {
  auto lock = table != nullptr ? table->LockForRead() : TableInfo::ReadLock();
  if (table == nullptr || lock->is_deleting()) {
    return STATUS(NotFound, "Could not find table", MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  if (check_for_ineligibility) {
    if (!IsTableEligibleForCDCSDKStream(
            table, lock, /*check_schema=*/true, /*allow_tables_without_primary_key=*/false)) {
      return STATUS(InvalidArgument, "Only allowed to remove user tables from CDC streams");
    }
  }

  return Status::OK();
}

Status CatalogManager::ProcessTablesToBeRemovedFromCDCSDKStreams(
    const TableStreamIdsMap& unprocessed_tables_to_streams_map, bool non_eligible_table_cleanup,
    const LeaderEpoch& epoch) {
  int32_t removed_tables = 0;
  for (const auto& [table_id, streams] : unprocessed_tables_to_streams_map) {
    if (removed_tables >= FLAGS_cdcsdk_table_processing_limit_per_run) {
      VLOG(1) << "Reached the limit of number of tables to be removed per iteration. Will "
                 "remove the remaining tables in the next iteration.";
      break;
    }

    auto table_result = FindTableById(table_id);
    std::unordered_set<xrepl::StreamId> streams_successfully_processed;
    Status s = table_result.ok() ? ValidateTableForRemovalFromCDCSDKStream(
                                       *table_result, !non_eligible_table_cleanup)
                                 : table_result.status();
    if (!s.ok()) {
      LOG(WARNING) << "Table " << table_id
                   << " not available for removal from CDC streams: " << s;
      // Table is not available for cleanup. We can remove the entry from the map.
      if (non_eligible_table_cleanup) {
        RemoveTableFromCDCSDKNonEligibleTableMap(table_id, streams.begin()->get()->namespace_id());
      } else {
        for (const auto& stream : streams) {
          streams_successfully_processed.insert(stream->StreamId());
        }
        RemoveStreamsFromUnprocessedRemovedTableMap(table_id, streams_successfully_processed);
      }
      ++removed_tables;
      continue;
    }

    // Delete the table from all streams now.
    NamespaceId namespace_id;
    Status status;
    for (const auto& stream : streams) {
      auto stream_id = stream->StreamId();
      status = ValidateStreamForTableRemoval(stream);
      if (!status.ok()) {
        LOG(WARNING) << "Stream " << stream_id << " not available for table removal: " << status;
        streams_successfully_processed.insert(stream_id);
        continue;
      }

      TEST_SYNC_POINT("ProcessTablesToBeRemovedFromCDCSDKStreams::ValidationCompleted");

      TEST_SYNC_POINT("ProcessTablesToBeRemovedFromCDCSDKStreams::StartStateTableEntryUpdate");

      if (!FLAGS_TEST_cdcsdk_skip_updating_cdc_state_entries_on_table_removal) {
        std::unordered_set<TableId> tables_in_stream_metadata;
        {
          auto stream_lock = stream->LockForRead();
          for (const auto& table_id : stream_lock->table_id()) {
            tables_in_stream_metadata.insert(table_id);
          }
        }

        // Explicitly remove the table from the set since we want to remove the tablet entries of
        // this table from the cdc state table.
        tables_in_stream_metadata.erase(table_id);
        auto result =
            RemoveTabletEntriesInCDCState(stream_id, tables_in_stream_metadata, *table_result);

        if (!result.ok()) {
          LOG(WARNING) << "Encountered error while trying to delete tablets entries of table: "
                       << table_id << ", from cdc_state table for stream: " << stream_id << " - "
                       << result;
          continue;
        }
      }

      TEST_SYNC_POINT("ProcessTablesToBeRemovedFromCDCSDKStreams::StateTableEntryUpdateCompleted");

      TEST_SYNC_POINT(
          "ProcessTablesToBeRemovedFromCDCSDKStreams::StartRemovalFromQualifiedTableList");

      if (!FLAGS_TEST_cdcsdk_skip_table_removal_from_qualified_list) {
        Status status = RemoveTableFromCDCStreamMetadataAndMaps(stream, table_id, epoch);
        if (!status.ok()) {
          LOG(WARNING) << "Encountered error while trying to remove table " << table_id
                       << " from qualified table list of stream " << stream_id << " and maps. - "
                       << status;
          continue;
        }
      }

      LOG(INFO) << "Successfully removed table " << table_id
                << " from qualified table list and updated corresponding cdc_state table entries "
                   "for stream: "
                << stream_id;

      namespace_id = stream->namespace_id();
      streams_successfully_processed.insert(stream_id);
    }

    if (non_eligible_table_cleanup) {
      // Remove non_user tables from 'namespace_to_cdcsdk_non_user_table_map_'.
      if (streams_successfully_processed.size() == streams.size()) {
        RemoveTableFromCDCSDKNonEligibleTableMap(table_id, namespace_id);
      }
    } else {
      // Remove streams for the table from 'cdcsdk_unprocessed_unqualified_tables_to_streams_' map.
      RemoveStreamsFromUnprocessedRemovedTableMap(table_id, streams_successfully_processed);
    }

    ++removed_tables;
  }

  return Status::OK();
}

Status CatalogManager::AddTableForRemovalFromCDCSDKStream(
    const std::unordered_set<TableId>& table_ids, const CDCStreamInfoPtr& stream) {
  std::unordered_set<TableId> tables_added_to_unqualified_list;
  auto ltm = stream->LockForWrite();
  for (const auto& table_id : table_ids) {
    auto itr =
        std::find(ltm->unqualified_table_id().begin(), ltm->unqualified_table_id().end(), table_id);
    if (itr == ltm->unqualified_table_id().end()) {
      tables_added_to_unqualified_list.insert(table_id);
      ltm.mutable_data()->pb.add_unqualified_table_id(table_id);
    }
  }

  if (tables_added_to_unqualified_list.empty()) {
    return Status::OK();
  }

  RETURN_ACTION_NOT_OK(
      sys_catalog_->Upsert(leader_ready_term(), stream), "Updating CDC stream in system catalog");

  ltm.Commit();

  {
    LockGuard lock(cdcsdk_unqualified_table_removal_mutex_);
    for (const auto& table_id : tables_added_to_unqualified_list) {
      cdcsdk_unprocessed_unqualified_tables_to_streams_[table_id].insert(stream->StreamId());
      VLOG(1) << "Added table: " << table_id << " under stream: " << stream->StreamId()
              << ", to cdcsdk_unprocessed_unqualified_tables_to_streams_ for removal from the "
                 "stream.";
    }
  }

  return Status::OK();
}

void CatalogManager::RemoveTableFromCDCSDKUnprocessedMap(
    const TableId& table_id, const NamespaceId& ns_id) {
  LockGuard lock(cdcsdk_unprocessed_table_mutex_);
  auto unprocessed_tables = FindOrNull(namespace_to_cdcsdk_unprocessed_table_map_, ns_id);
  if (unprocessed_tables) {
    unprocessed_tables->erase(table_id);
    if (unprocessed_tables->empty()) {
      namespace_to_cdcsdk_unprocessed_table_map_.erase(ns_id);
    }
  }
}

void CatalogManager::RemoveTableFromCDCSDKNonEligibleTableMap(
    const TableId& table_id, const NamespaceId& ns_id) {
  LockGuard lock(cdcsdk_non_eligible_table_mutex_);
  auto non_user_tables = FindOrNull(namespace_to_cdcsdk_non_eligible_table_map_, ns_id);
  if (!non_user_tables) {
    return;
  }

  non_user_tables->erase(table_id);
  if (non_user_tables->empty()) {
    namespace_to_cdcsdk_non_eligible_table_map_.erase(ns_id);
  }
}

void CatalogManager::RemoveStreamsFromUnprocessedRemovedTableMap(
    const TableId& table_id, const std::unordered_set<xrepl::StreamId>& stream_ids) {
  LockGuard lock(cdcsdk_unqualified_table_removal_mutex_);
  auto streams = FindOrNull(cdcsdk_unprocessed_unqualified_tables_to_streams_, table_id);
  if (!streams) {
    return;
  }

  for (const auto& stream_id : stream_ids) {
    streams->erase(stream_id);
  }
  if (streams->empty()) {
    cdcsdk_unprocessed_unqualified_tables_to_streams_.erase(table_id);
  }
}

Result<std::vector<CDCStreamInfoPtr>> CatalogManager::FindXReplStreamsMarkedForDeletion(
    SysCDCStreamEntryPB::State deletion_state) {
  std::vector<CDCStreamInfoPtr> streams;
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  for (const CDCStreamInfoMap::value_type& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();
    if (deletion_state == SysCDCStreamEntryPB::DELETING_METADATA && ltm->is_deleting_metadata()) {
      VLOG(2) << "Stream " << entry.second->id() << " was marked as DELETING_METADATA";
      streams.push_back(entry.second);
    } else if (deletion_state == SysCDCStreamEntryPB::DELETING && ltm->is_deleting()) {
      VLOG(2) << "Stream " << entry.second->id() << " was marked as DELETING";
      streams.push_back(entry.second);
    }
  }
  return streams;
}

Status CatalogManager::GetDroppedTablesFromCDCSDKStream(
    const std::unordered_set<TableId>& table_ids, std::set<TabletId>* tablets_with_streams,
    std::set<TableId>* dropped_tables) {
  for (const auto& table_id : table_ids) {
    TabletInfos tablets;
    auto table_result = FindTableById(table_id);
    if (table_result.ok()) {
      tablets = VERIFY_RESULT((*table_result)->GetTabletsIncludeInactive());
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

  return Status::OK();
}

Status CatalogManager::GetValidTabletsAndDroppedTablesForStream(
    const CDCStreamInfoPtr stream, std::set<TabletId>* tablets_with_streams,
    std::set<TableId>* dropped_tables) {
  std::unordered_set<TableId> qualified_tables;
  std::unordered_set<TableId> unqualified_tables;
  {
    auto stream_lock = stream->LockForRead();
    for (const auto& table_id : stream_lock->table_id()) {
      qualified_tables.insert(table_id);
    }

    if (stream_lock->pb.unqualified_table_id_size() > 0) {
      for (const auto& table_id : stream_lock->unqualified_table_id()) {
        unqualified_tables.insert(table_id);
      }
    }
  }

  RETURN_NOT_OK(
      GetDroppedTablesFromCDCSDKStream(qualified_tables, tablets_with_streams, dropped_tables));

  if (!unqualified_tables.empty()) {
    RETURN_NOT_OK(
        GetDroppedTablesFromCDCSDKStream(unqualified_tables, tablets_with_streams, dropped_tables));
  }

  return Status::OK();
}

Result<CDCStreamInfoPtr> CatalogManager::GetXReplStreamInfo(const xrepl::StreamId& stream_id) {
  SharedLock lock(mutex_);
  auto stream_info = FindPtrOrNull(cdc_stream_map_, stream_id);
  SCHECK(stream_info, NotFound, Format("XRepl Stream $0 not found", stream_id));
  return stream_info;
}

Status CatalogManager::CleanupCDCSDKDroppedTablesFromStreamInfo(
    const LeaderEpoch& epoch,
    const StreamTablesMap& drop_stream_tablelist) {
  std::vector<CDCStreamInfoPtr> streams_to_update;
  std::vector<CDCStreamInfo::WriteLock> locks;

  TRACE("Cleaning CDCSDK streams from map and system catalog.");
  {
    for (auto& [stream_id, drop_table_list] : drop_stream_tablelist) {
      auto cdc_stream_info = VERIFY_RESULT(GetXReplStreamInfo(stream_id));
      auto ltm = cdc_stream_info->LockForWrite();
      bool need_to_update_stream = false;

      // Remove those tables info, that are dropped from the cdc_stream_map_ and update the
      // system catalog.
      for (auto table_id : drop_table_list) {
        auto table_id_iter = std::find(ltm->table_id().begin(), ltm->table_id().end(), table_id);
        if (table_id_iter != ltm->table_id().end()) {
          need_to_update_stream = true;
          ltm.mutable_data()->pb.mutable_table_id()->erase(table_id_iter);
          ltm.mutable_data()->pb.mutable_replica_identity_map()->erase(table_id);
        }

        if (ltm->pb.unqualified_table_id_size() > 0) {
          auto unqualified_table_id_iter = std::find(
              ltm->unqualified_table_id().begin(), ltm->unqualified_table_id().end(), table_id);
          if (unqualified_table_id_iter != ltm->unqualified_table_id().end()) {
            need_to_update_stream = true;
            ltm.mutable_data()->pb.mutable_unqualified_table_id()->erase(unqualified_table_id_iter);
            ltm.mutable_data()->pb.mutable_replica_identity_map()->erase(table_id);
          }
        }
      }
      if (need_to_update_stream) {
        streams_to_update.push_back(cdc_stream_info);
        locks.push_back(std::move(ltm));
      }
    }
    // Return if there are no stream to update.
    if (streams_to_update.size() == 0) {
      return Status::OK();
    }
  }

  // Do system catalog UPDATE and DELETE based on the streams_to_update and streams_to_delete.
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Upsert(epoch, streams_to_update),
      "Updating CDC streams in system catalog");

  for (auto& lock : locks) {
    lock.Commit();
  }
  return Status::OK();
}

Status CatalogManager::CleanUpCDCSDKStreamsMetadata(const LeaderEpoch& epoch) {
  // DELETING_METADATA special state is used by CDCSDK, to do CDCSDK streams metadata cleanup from
  // cache as well as from the system catalog for the drop table scenario.
  auto streams =
      VERIFY_RESULT(FindXReplStreamsMarkedForDeletion(SysCDCStreamEntryPB::DELETING_METADATA));
  if (streams.empty()) {
    return Status::OK();
  }

  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::StartStep1");
  // for efficient filtering of cdc_state table entries to only the list received in streams.
  std::unordered_set<xrepl::StreamId> stream_ids_metadata_to_be_cleaned_up;
  for(const auto& stream : streams) {
    stream_ids_metadata_to_be_cleaned_up.insert(stream->StreamId());
  }

  // Step-1: Get entries from cdc_state table.
  std::vector<cdc::CDCStateTableKey> cdc_state_entries;
  Status iteration_status;
  auto all_entry_keys =
      VERIFY_RESULT(cdc_state_table_->GetTableRange({} /* just key columns */, &iteration_status));
  for (const auto& entry_result : all_entry_keys) {
    RETURN_NOT_OK(entry_result);
    const auto& entry = *entry_result;
    // Only add those entries that belong to the received list of streams and does not represent the
    // replication slot's state table entry. Replication slot's entry is skipped in order to avoid
    // its deletion since it does not represent a real tablet_id and the cleanup algorithm works
    // under the assumption that all cdc state entires are representing real tablet_ids.
    //
    // Also skip processing the entries corresponding to sys catalog tablet, since it will never be
    // deleted.
    if (entry.key.tablet_id != kCDCSDKSlotEntryTabletId &&
        entry.key.tablet_id != kSysCatalogTabletId &&
        stream_ids_metadata_to_be_cleaned_up.contains(entry.key.stream_id)) {
      cdc_state_entries.emplace_back(entry.key);
    }
  }
  RETURN_NOT_OK(iteration_status);
  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::CompletedStep1");

  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::StartStep2");
  // Step-2: Get list of tablets to keep for each stream.
  // Map of valid tablets to keep for each stream.
  std::unordered_map<xrepl::StreamId, std::set<TabletId>> tablets_to_keep_per_stream;
  // Map to identify the list of dropped tables for the stream.
  StreamTablesMap drop_stream_table_list;
  for (const auto& stream : streams) {
    const auto& stream_id = stream->StreamId();
    // Get the set of all tablets not associated with the table dropped. Tablets belonging to this
    // set will not be deleted from cdc_state.
    // The second set consists of all the tables that were associated with the stream, but dropped.
    RETURN_NOT_OK(GetValidTabletsAndDroppedTablesForStream(
        stream, &tablets_to_keep_per_stream[stream_id], &drop_stream_table_list[stream_id]));
  }

  std::vector<cdc::CDCStateTableKey> keys_to_delete;
  for (const auto& entry : cdc_state_entries) {
    const auto tablets = FindOrNull(tablets_to_keep_per_stream, entry.stream_id);

    RSTATUS_DCHECK(tablets, IllegalState,
      "No entry found in tablets_to_keep_per_stream map for the stream");

    if (!tablets->contains(entry.tablet_id)) {
      // Either this tablet belongs to a dropped table or a dynamic table. If the corresponding
      // table of the tablet (all the tables in case of a colocated tablet) belongs to the dropped
      // table list computed previously, then the cdc_state entry will be deleted.
      // There is a possible race if the tablet is not found for split tablets.
      // So we require the tablet is found, or it is known to have been deleted.
      auto tablet_info_result = GetTabletInfo(entry.tablet_id);
      if (!tablet_info_result.ok()) {
        if (tablet_info_result.status().IsDeleted()) {
          keys_to_delete.emplace_back(entry.tablet_id, entry.stream_id);
        } else {
          LOG_WITH_FUNC(WARNING) << "Did not find tablet info for tablet_id: " << entry.tablet_id
                                 << " , will not delete its cdc_state entry for stream id:"
                                 << entry.stream_id << "in this iteration";
        }
        continue;
      }

      auto table_ids = (*tablet_info_result)->GetTableIds();
      DCHECK_GT(table_ids.size(), 0);
      bool all_tables_on_tablet_dropped = true;
      auto stream_info = VERIFY_RESULT(GetXReplStreamInfo(entry.stream_id));
      for (const auto& table_id : table_ids) {
        if (drop_stream_table_list[entry.stream_id].contains(table_id)) {
          continue;
        }
        // In a race scenario between cleanup of non eligible tables from CDC stream & same table
        // being dropped, removal of cdc state entries for this non eligible table is done via drop
        // table metadata cleanup flow.
        auto table_info = GetTableInfo(table_id);
        if (table_info && !IsTableEligibleForCDCSDKStream(
                              table_info, table_info->LockForRead(), /* check_schema */ true,
                              stream_info->IsTablesWithoutPrimaryKeyAllowed())) {
          continue;
        }
        all_tables_on_tablet_dropped = false;
        break;
      }

      // Skip deleting the cdc state table entry if atleast one table is still present.
      if (all_tables_on_tablet_dropped) {
        keys_to_delete.emplace_back(entry.tablet_id, entry.stream_id);
      }
    }
  }

  if (!keys_to_delete.empty()) {
    LOG(INFO) << "Deleting cdc_state table entries " << AsString(keys_to_delete);
    RETURN_NOT_OK(cdc_state_table_->DeleteEntries(keys_to_delete));
  }

  // Cleanup the streams from system catalog and from internal maps.
  return CleanupCDCSDKDroppedTablesFromStreamInfo(epoch, drop_stream_table_list);
}

Status CatalogManager::CleanUpDeletedXReplStreams(const LeaderEpoch& epoch) {
  auto streams = VERIFY_RESULT(FindXReplStreamsMarkedForDeletion(SysCDCStreamEntryPB::DELETING));
  if (streams.empty()) {
    return Status::OK();
  }

  // First. For each deleted stream, delete the cdc state rows.
  // Delete all the entries in cdc_state table that contain all the deleted cdc streams.

  // We only want to iterate through cdc_state once, so create a map here to efficiently check if
  // a row belongs to a stream that should be deleted.
  std::unordered_map<xrepl::StreamId, CDCStreamInfo*> stream_id_to_stream_info_map;
  for (const auto& stream : streams) {
    stream_id_to_stream_info_map.emplace(stream->StreamId(), stream.get());
  }

  // We use GetTableRangeAsync here since it could be that we came here to rollback a CDCSDK stream
  // with the CDC state table creation still in progress. This can happen in case the stream being
  // rolled back is the first CDC stream in the universe. In this case, we skip the rollback and the
  // caller (CatalogManagerBgTasks) is expected to retry this cleanup at a later time.
  Status iteration_status;
  auto all_entry_keys = VERIFY_RESULT(
      cdc_state_table_->GetTableRangeAsync({} /* just key columns */, &iteration_status));
  std::vector<cdc::CDCStateTableKey> entries_to_delete;

  // Remove all entries from cdc_state with the given stream ids.
  for (const auto& entry_result : all_entry_keys) {
    RETURN_NOT_OK(entry_result);
    const auto& entry = *entry_result;
    const auto stream = FindPtrOrNull(stream_id_to_stream_info_map, entry.key.stream_id);
    if (!stream) {
      continue;
    }

    entries_to_delete.emplace_back(entry.key);
    LOG(INFO) << "Deleting stream " << entry.key.ToString();
  }
  RETURN_NOT_OK(iteration_status);

  RETURN_NOT_OK_PREPEND(
      cdc_state_table_->DeleteEntries(entries_to_delete),
      "Error deleting XRepl stream rows from cdc_state table");

  std::vector<CDCStreamInfo::WriteLock> locks;
  locks.reserve(streams.size());
  std::vector<CDCStreamInfo*> streams_to_delete;
  streams_to_delete.reserve(streams.size());

  for (auto& stream : streams) {
    locks.push_back(stream->LockForWrite());
    streams_to_delete.push_back(stream.get());
  }

  RETURN_NOT_OK(xcluster_manager_->RemoveStreamsFromSysCatalog(epoch, streams_to_delete));

  bool TEST_fail = false;
  TEST_SYNC_POINT_CALLBACK("CleanUpDeletedXReplStreams::FailBeforeStreamDeletion", &TEST_fail);
  if (TEST_fail) {
    LOG(INFO) << "Failed before streams " << CDCStreamInfosAsString(streams_to_delete)
              << " are deleted from sys catalog";
  }
  SCHECK(!TEST_fail, IllegalState, "Failing for TESTING");

  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Delete(epoch, streams_to_delete),
      "Error deleting XRepl streams from sys-catalog");

  for (auto& lock : locks) {
    lock.Commit();
  }
  TRACE("Removing from maps");
  {
    LockGuard lock(mutex_);
    for (const auto& stream : streams_to_delete) {
      RETURN_NOT_OK(CleanupXReplStreamFromMaps(stream));
    }
  }
  LOG(INFO) << "Successfully deleted XRepl streams: " << CDCStreamInfosAsString(streams_to_delete);

  return Status::OK();
}

Status CatalogManager::CleanupXReplStreamFromMaps(CDCStreamInfoPtr stream) {
  const auto& stream_id = stream->StreamId();
  if (cdc_stream_map_.erase(stream_id) < 1) {
    return STATUS(IllegalState, "XRepl stream not found in map", stream_id.ToString());
  }

  xcluster_manager_->CleanupStreamFromMaps(*stream);

  for (auto& id : stream->table_id()) {
    cdcsdk_tables_to_stream_map_[id].erase(stream_id);
  }

  // Delete entry from cdcsdk_replication_slots_to_stream_map_ if the map contains the same
  // stream_id for the replication_slot_name key.
  // It can contain a different stream_id in scenarios where a CreateCDCStream with same
  // replication slot name was immediately invoked after DeleteCDCStream before the background
  // cleanup task was executed.
  auto cdcsdk_ysql_replication_slot_name = stream->GetCdcsdkYsqlReplicationSlotName();
  if (!cdcsdk_ysql_replication_slot_name.empty() &&
      cdcsdk_replication_slots_to_stream_map_.contains(cdcsdk_ysql_replication_slot_name) &&
      cdcsdk_replication_slots_to_stream_map_.at(cdcsdk_ysql_replication_slot_name) == stream_id) {
    cdcsdk_replication_slots_to_stream_map_.erase(cdcsdk_ysql_replication_slot_name);
  }

  RecoverXreplStreamId(stream_id);

  return Status::OK();
}

Status CatalogManager::GetCDCStream(
    const GetCDCStreamRequestPB* req, GetCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  VLOG(2) << "GetCDCStream from " << RequestorString(rpc) << ": " << req->DebugString();

  if (!req->has_stream_id() && !req->has_cdcsdk_ysql_replication_slot_name()) {
    return STATUS(
        InvalidArgument, "One of CDC Stream ID or Replication slot name must be provided",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    xrepl::StreamId stream_id = xrepl::StreamId::Nil();
    if (req->has_stream_id()) {
      stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));
    } else {
      auto replication_slot_name = ReplicationSlotName(req->cdcsdk_ysql_replication_slot_name());
      if (!cdcsdk_replication_slots_to_stream_map_.contains(replication_slot_name)) {
        LOG_WITH_FUNC(WARNING) << "Did not find replication_slot_name: " << replication_slot_name
                               << " in cdcsdk_replication_slots_to_stream_map_.";
        return STATUS(
            NotFound, "Could not find CDC stream", req->ShortDebugString(),
            MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      stream_id = cdcsdk_replication_slots_to_stream_map_.at(replication_slot_name);
    }

    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(
        NotFound, "Could not find CDC stream", req->ShortDebugString(),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForRead();

  CDCStreamInfoPB* stream_info = resp->mutable_stream();

  stream_info->set_stream_id(stream->id());
  std::string id_type_option_value(cdc::kTableId);

  for (auto option : stream_lock->options()) {
    if (option.has_key() && option.key() == cdc::kIdType) id_type_option_value = option.value();
  }

  if (id_type_option_value == cdc::kNamespaceId) {
    stream_info->set_namespace_id(stream_lock->namespace_id());
  }

  for (auto& table_id : stream_lock->table_id()) {
    stream_info->add_table_id(table_id);
  }

  if (stream_lock->pb.unqualified_table_id_size() > 0) {
    // Only applicable for CDCSDK streams.
    for (auto& table_id : stream_lock->unqualified_table_id()) {
      stream_info->add_unqualified_table_id(table_id);
    }
  }

  stream_info->mutable_options()->CopyFrom(stream_lock->options());
  stream_info->set_transactional(stream_lock->transactional());

  if (stream_lock->pb.has_state()) {
    auto state_option = stream_info->add_options();
    state_option->set_key(cdc::kStreamState);
    state_option->set_value(SysCDCStreamEntryPB::State_Name(stream_lock->pb.state()));
  }

  if (stream_lock->pb.has_cdcsdk_ysql_replication_slot_name()) {
    stream_info->set_cdcsdk_ysql_replication_slot_name(
        stream_lock->pb.cdcsdk_ysql_replication_slot_name());
  }

  if (stream_lock->pb.has_cdcsdk_ysql_replication_slot_plugin_name()) {
    stream_info->set_cdcsdk_ysql_replication_slot_plugin_name(
        stream_lock->pb.cdcsdk_ysql_replication_slot_plugin_name());
  }

  if (stream_lock->pb.has_cdcsdk_stream_metadata()) {
    auto cdcsdk_stream_metadata = stream_lock->pb.cdcsdk_stream_metadata();
    if (cdcsdk_stream_metadata.has_snapshot_time()) {
      stream_info->set_cdcsdk_consistent_snapshot_time(cdcsdk_stream_metadata.snapshot_time());
    }
    if (cdcsdk_stream_metadata.has_consistent_snapshot_option()) {
      stream_info->set_cdcsdk_consistent_snapshot_option(
          cdcsdk_stream_metadata.consistent_snapshot_option());
    }
  }

  if (stream_lock->pb.has_stream_creation_time()) {
    stream_info->set_stream_creation_time(stream_lock->pb.stream_creation_time());
  }

  if (FLAGS_cdcsdk_enable_dynamic_tables_disable_option &&
      stream_lock->pb.has_cdcsdk_disable_dynamic_table_addition()) {
    stream_info->set_cdcsdk_disable_dynamic_table_addition(
        stream_lock->pb.cdcsdk_disable_dynamic_table_addition());
  }

  // We need to ensure that we do not check the lsn type option when we are
  // dealing with a gRPC stream. Adding a check to only access the lsn type values
  // when we have a slot name present which essentially indicates that we are in
  // the context of logical replication.
  if (stream_lock->pb.has_cdcsdk_ysql_replication_slot_name()) {
    auto cdc_stream_info_options = stream_info->mutable_cdc_stream_info_options();

    auto replication_slot_lsn_type = ReplicationSlotLsnType::ReplicationSlotLsnType_SEQUENCE;
    auto replication_slot_ordering_mode =
        ReplicationSlotOrderingMode::ReplicationSlotOrderingMode_TRANSACTION;

    if (FLAGS_ysql_yb_allow_replication_slot_lsn_types &&
        stream_lock->pb.has_cdcsdk_ysql_replication_slot_lsn_type()) {
      replication_slot_lsn_type = stream_lock->pb.cdcsdk_ysql_replication_slot_lsn_type();
    } else {
      VLOG(2) << "No cdcsdk_ysql_replication_slot_lsn_type found for stream: " << stream->id()
              << " and slot " << stream_lock->pb.cdcsdk_ysql_replication_slot_name()
              << " with flag value: " << FLAGS_ysql_yb_allow_replication_slot_lsn_types
              << ". Keeping default value of 'SEQUENCE'.";
    }

    if (FLAGS_ysql_yb_allow_replication_slot_ordering_modes &&
        stream_lock->pb.has_cdcsdk_ysql_replication_slot_ordering_mode()) {
      replication_slot_ordering_mode = stream_lock->pb.cdcsdk_ysql_replication_slot_ordering_mode();
    } else {
      VLOG(2) << "No cdcsdk_ysql_replication_slot_ordering_mode found for stream: " << stream->id()
              << " and slot " << stream_lock->pb.cdcsdk_ysql_replication_slot_name()
              << " with flag value: " << FLAGS_ysql_yb_allow_replication_slot_ordering_modes
              << ". Keeping default value of 'TRANSACTION'.";
    }

    cdc_stream_info_options->set_cdcsdk_ysql_replication_slot_lsn_type(replication_slot_lsn_type);
    cdc_stream_info_options->set_cdcsdk_ysql_replication_slot_ordering_mode(
        replication_slot_ordering_mode);
  }

  auto replica_identity_map = stream_lock->pb.replica_identity_map();
  stream_info->mutable_replica_identity_map()->swap(replica_identity_map);

  stream_info->set_allow_tables_without_primary_key(
      stream_lock->pb.has_allow_tables_without_primary_key() &&
      stream_lock->pb.allow_tables_without_primary_key());

  stream_info->set_detect_publication_changes_implicitly(
      stream_lock->pb.has_detect_publication_changes_implicitly() &&
      stream_lock->pb.detect_publication_changes_implicitly());

  return Status::OK();
}

Status CatalogManager::GetCDCDBStreamInfo(
    const GetCDCDBStreamInfoRequestPB* req, GetCDCDBStreamInfoResponsePB* resp) {
  if (!req->has_db_stream_id()) {
    return STATUS(
        InvalidArgument, "CDC DB Stream ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(
        cdc_stream_map_, VERIFY_RESULT(xrepl::StreamId::FromString(req->db_stream_id())));
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(
        NotFound, "Could not find CDC stream", req->ShortDebugString(),
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

  if (stream_lock->pb.unqualified_table_id_size() > 0) {
    for (const auto& unqualified_table_id : stream_lock->unqualified_table_id()) {
      const auto unqualified_table_info = resp->add_unqualified_table_info();
      unqualified_table_info->set_stream_id(req->db_stream_id());
      unqualified_table_info->set_table_id(unqualified_table_id);
    }
  }

  return Status::OK();
}

Status CatalogManager::ListCDCStreams(
    const ListCDCStreamsRequestPB* req, ListCDCStreamsResponsePB* resp) {
  bool filter_table = req->has_table_id();
  TableId table_id;
  if (filter_table) {
    table_id = req->table_id();
    auto stripped_table_id = xcluster::StripSequencesDataAliasIfPresent(table_id);
    RETURN_NOT_OK(FindTableById(stripped_table_id));
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

    if (filter_table && entry.second->table_id().size() > 0 &&
        table_id != entry.second->table_id().Get(0)) {
      continue;  // Skip deleting/deleted streams and streams from other tables.
    }

    auto ltm = entry.second->LockForRead();

    if (ltm->is_deleting()) {
      continue;
    }

    for (const auto& option : ltm->options()) {
      if (option.key() == cdc::kIdType) {
        id_type_option_present = true;
        if (req->has_id_type()) {
          if (req->id_type() == IdTypePB::NAMESPACE_ID && option.value() != cdc::kNamespaceId) {
            skip_stream = true;
            break;
          }
          if (req->id_type() == IdTypePB::TABLE_ID && option.value() == cdc::kNamespaceId) {
            skip_stream = true;
            break;
          }
        }
      }
    }

    if ((!id_type_option_present && req->id_type() == IdTypePB::NAMESPACE_ID) || skip_stream)
      continue;

    CDCStreamInfoPB* stream = resp->add_streams();
    stream->set_stream_id(entry.second->id());
    for (const auto& table_id : ltm->table_id()) {
      stream->add_table_id(table_id);
    }

    if (ltm->pb.unqualified_table_id_size() > 0) {
      // Only applicable for CDCSDK streams.
      for (const auto& table_id : ltm->unqualified_table_id()) {
        stream->add_unqualified_table_id(table_id);
      }
    }

    stream->mutable_options()->CopyFrom(ltm->options());
    // Also add an option for the current state.
    if (ltm->pb.has_state()) {
      auto state_option = stream->add_options();
      state_option->set_key(cdc::kStreamState);
      state_option->set_value(master::SysCDCStreamEntryPB::State_Name(ltm->pb.state()));
    }

    if (ltm->pb.has_namespace_id()) {
      stream->set_namespace_id(ltm->pb.namespace_id());
    }

    if (ltm->pb.has_cdcsdk_ysql_replication_slot_name()) {
      stream->set_cdcsdk_ysql_replication_slot_name(
        ltm->pb.cdcsdk_ysql_replication_slot_name());
    }

    if (ltm->pb.has_cdcsdk_ysql_replication_slot_plugin_name()) {
      stream->set_cdcsdk_ysql_replication_slot_plugin_name(
          ltm->pb.cdcsdk_ysql_replication_slot_plugin_name());
    }

    if (FLAGS_ysql_yb_allow_replication_slot_lsn_types) {
      if (ltm->pb.has_cdcsdk_ysql_replication_slot_lsn_type()) {
        auto cdc_stream_info_options = stream->mutable_cdc_stream_info_options();
        cdc_stream_info_options->set_cdcsdk_ysql_replication_slot_lsn_type(
            ltm->pb.cdcsdk_ysql_replication_slot_lsn_type());
      }
    }

    if (FLAGS_ysql_yb_allow_replication_slot_ordering_modes) {
      if (ltm->pb.has_cdcsdk_ysql_replication_slot_ordering_mode()) {
        auto cdc_stream_info_options = stream->mutable_cdc_stream_info_options();
        cdc_stream_info_options->set_cdcsdk_ysql_replication_slot_ordering_mode(
            ltm->pb.cdcsdk_ysql_replication_slot_ordering_mode());
      }
    }

    if (ltm->pb.has_cdcsdk_stream_metadata()) {
      auto cdcsdk_stream_metadata = ltm->pb.cdcsdk_stream_metadata();
      if (cdcsdk_stream_metadata.has_snapshot_time()) {
        stream->set_cdcsdk_consistent_snapshot_time(cdcsdk_stream_metadata.snapshot_time());
      }
      if (cdcsdk_stream_metadata.has_consistent_snapshot_option()) {
        stream->set_cdcsdk_consistent_snapshot_option(
            cdcsdk_stream_metadata.consistent_snapshot_option());
      }
    }

    if (ltm->pb.has_stream_creation_time()) {
      stream->set_stream_creation_time(ltm->pb.stream_creation_time());
    }

    if (FLAGS_cdcsdk_enable_dynamic_tables_disable_option &&
        ltm->pb.has_cdcsdk_disable_dynamic_table_addition()) {
      stream->set_cdcsdk_disable_dynamic_table_addition(
          ltm->pb.cdcsdk_disable_dynamic_table_addition());
    }

    auto replica_identity_map = ltm->pb.replica_identity_map();
    stream->mutable_replica_identity_map()->swap(replica_identity_map);

    stream->set_allow_tables_without_primary_key(
        ltm->pb.has_allow_tables_without_primary_key() &&
        ltm->pb.allow_tables_without_primary_key());

    stream->set_detect_publication_changes_implicitly(
        ltm->pb.has_detect_publication_changes_implicitly() &&
        ltm->pb.detect_publication_changes_implicitly());
  }
  return Status::OK();
}

Status CatalogManager::IsObjectPartOfXRepl(
    const IsObjectPartOfXReplRequestPB* req, IsObjectPartOfXReplResponsePB* resp) {
  auto table_info = GetTableInfo(req->table_id());
  SCHECK(table_info, NotFound, "Table with id $0 does not exist", req->table_id());
  SharedLock lock(mutex_);
  resp->set_is_object_part_of_xrepl(
      xcluster_manager_->IsTableReplicated(table_info->id()) ||
      IsTablePartOfCDCSDK(table_info->id()));
  return Status::OK();
}

Status CatalogManager::UpdateCDCStreams(
    const std::vector<xrepl::StreamId>& stream_ids,
    const std::vector<yb::master::SysCDCStreamEntryPB>& update_entries) {
  RSTATUS_DCHECK(stream_ids.size() > 0, InvalidArgument, "No stream ID provided.");
  RSTATUS_DCHECK(
      stream_ids.size() == update_entries.size(), InvalidArgument,
      "Mismatched number of stream IDs and update entries provided.");

  // Map StreamId to (CDCStreamInfo, SysCDCStreamEntryPB). StreamId is sorted in
  // increasing order in the map.
  std::map<xrepl::StreamId, std::pair<CDCStreamInfoPtr, yb::master::SysCDCStreamEntryPB>>
      id_to_update_infos;
  {
    SharedLock lock(mutex_);
    for (size_t i = 0; i < stream_ids.size(); i++) {
      auto stream_id = stream_ids[i];
      auto entry = update_entries[i];
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
      if (stream == nullptr) {
        return STATUS(
            NotFound, "Could not find CDC stream", stream_id.ToString(),
            MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      id_to_update_infos[stream_id] = {stream, entry};
    }
  }

  // Acquire CDCStreamInfo::WriteLock in increasing order of xrepl::StreamId to avoid deadlock.
  std::vector<CDCStreamInfo::WriteLock> stream_locks;
  std::vector<CDCStreamInfoPtr> streams_to_update;
  stream_locks.reserve(stream_ids.size());
  streams_to_update.reserve(stream_ids.size());
  for (const auto& [stream_id, update_info] : id_to_update_infos) {
    auto& [stream, entry] = update_info;

    stream_locks.emplace_back(stream->LockForWrite());
    auto& stream_lock = stream_locks.back();
    if (stream_lock->is_deleting()) {
      return STATUS(
          NotFound, "CDC stream has been deleted", stream->id(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    auto& pb = stream_lock.mutable_data()->pb;
    pb.CopyFrom(entry);

    for (auto it = pb.mutable_options()->begin(); it != pb.mutable_options()->end(); ++it) {
      if (it->key() == cdc::kStreamState) {
        // State should be set only via the dedicated field.
        // This can happen because CDCStreamInfoPB stores the state in the options map whereas
        // SysCDCStreamEntryPB stores state as a separate field.
        // TODO(xrepl): Add a dedicated state field to CDCStreamInfoPB.
        LOG(WARNING) << "Ignoring cdc state option " << it->value() << " for stream " << stream_id;
        pb.mutable_options()->erase(it);
      }
    }
    streams_to_update.push_back(stream);
  }

  // First persist changes in sys catalog, then commit changes in the order of lock acquiring.
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), streams_to_update));
  for (auto& stream_lock : stream_locks) {
    stream_lock.Commit();
  }

  return Status::OK();
}

Status CatalogManager::UpdateCDCStream(
    const UpdateCDCStreamRequestPB* req, UpdateCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "UpdateCDCStream from " << RequestorString(rpc) << ": " << req->DebugString();

  SCHECK(
      !FLAGS_TEST_xcluster_fail_setup_stream_update, IllegalState,
      "Test flag to fail setup stream update is set");

  std::vector<xrepl::StreamId> stream_ids;
  std::vector<yb::master::SysCDCStreamEntryPB> update_entries;
  stream_ids.reserve(req->streams_size() > 0 ? req->streams_size() : 1);
  update_entries.reserve(req->streams_size() > 0 ? req->streams_size() : 1);

  if (req->streams_size() == 0) {
    // Support backwards compatibility for single stream update.
    if (!req->has_stream_id()) {
      return STATUS(
          InvalidArgument, "Stream ID must be provided", req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    if (!req->has_entry()) {
      return STATUS(
          InvalidArgument, "CDC Stream Entry must be provided", req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    stream_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id())));
    update_entries.push_back(req->entry());
  } else {
    // Process batch update.
    for (const auto& stream : req->streams()) {
      stream_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(stream.stream_id())));
      update_entries.push_back(stream.entry());
    }
  }

  RETURN_NOT_OK(UpdateCDCStreams(stream_ids, update_entries));
  return Status::OK();
}

// Query if Bootstrapping is required for a CDC stream (e.g. Are we missing logs).
Status CatalogManager::IsBootstrapRequired(
    const IsBootstrapRequiredRequestPB* req,
    IsBootstrapRequiredResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "IsBootstrapRequired from " << RequestorString(rpc) << ": " << req->DebugString();
  RSTATUS_DCHECK(req->table_ids_size() > 0, InvalidArgument, "Table ID required");
  RSTATUS_DCHECK(
      req->stream_ids_size() == 0 || req->stream_ids_size() == req->table_ids_size(),
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
  auto task_completed = std::make_shared<bool>(false);  // Protected by data_lock.
  auto finished_tasks = std::make_shared<size_t>(0);    // Protected by data_lock.
  size_t total_tasks = req->table_ids_size();

  for (int t = 0; t < req->table_ids_size(); t++) {
    auto table_id = req->table_ids(t);
    auto stream_id = streams_given ? VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_ids(t)))
                                   : xrepl::StreamId::Nil();

    // TODO: Submit the task to a thread pool.
    // Capture everything by value to increase their refcounts.
    scoped_refptr<Thread> async_task;
    RETURN_NOT_OK(Thread::Create(
        "xrepl_catalog_manager", "is_bootstrap_required",
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
    return SetupError(
        resp->mutable_error(),
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

Status CatalogManager::IsTableBootstrapRequired(
    const TableId& table_id,
    const xrepl::StreamId& stream_id,
    CoarseTimePoint deadline,
    bool* const bootstrap_required) {
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(table_id));
  RSTATUS_DCHECK(table != nullptr, NotFound, "Table ID not found: " + table_id);

  // Make a batch call for IsBootstrapRequired on every relevant TServer.
  std::map<std::shared_ptr<cdc::CDCServiceProxy>, cdc::IsBootstrapRequiredRequestPB>
      proxy_to_request;
  for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
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
    if (stream_id) {
      tablet_req.set_stream_id(stream_id.ToString());
    }
    auto& cdc_service = proxy_request.first;

    RETURN_NOT_OK(cdc_service->IsBootstrapRequired(tablet_req, &tablet_resp, &rpc));
    if (tablet_resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(tablet_resp.error().status()));
    } else if (tablet_resp.has_bootstrap_required() && tablet_resp.bootstrap_required()) {
      *bootstrap_required = true;
      break;
    }
  }

  return Status::OK();
}

Status CatalogManager::UpdateCDCProducerOnTabletSplit(
    const TableId& producer_table_id, const SplitTabletIds& split_tablet_ids) {
  std::vector<CDCStreamInfoPtr> streams;
  std::unordered_set<xrepl::StreamId> cdcsdk_stream_ids;
  std::vector<cdc::CDCStateTableEntry> entries;
  for (const auto stream_type : {cdc::XCLUSTER, cdc::CDCSDK}) {
    if (stream_type == cdc::CDCSDK) {
      const auto table_info = GetTableInfo(producer_table_id);
      if (table_info) {
        // Skip adding children tablet entries in cdc state if the table is an index or a mat view.
        // These tables, if present in CDC stream, are anyway going to be removed by a bg thread.
        // This check ensures even if there is a race condition where a tablet of a non-eligible
        // table splits and concurrently we are removing such tables from stream, the child tables
        // do not get added.
        {
          if (!IsTableEligibleForCDCSDKStream(
                  table_info, table_info->LockForRead(), /*check_schema=*/false,
                  /*allow_tables_without_primary_key=*/false)) {
            LOG(INFO) << "Skipping adding children tablets to cdc state for table "
                      << producer_table_id << " as it is not meant to part of a CDC stream";
            continue;
          }
        }
      }
    }

    {
      SharedLock lock(mutex_);
      streams = GetXReplStreamsForTable(producer_table_id, stream_type);
    }

    if (stream_type == cdc::CDCSDK) {
      WarnIfTableNotPresentInAllCDCSDKStreams(producer_table_id);
    }

    TEST_SYNC_POINT("UpdateCDCProducerOnTabletSplit::FindStreamsForAddingChildEntriesComplete");

    for (const auto& stream : streams) {
      if (stream_type == cdc::CDCSDK) {
        cdcsdk_stream_ids.insert(stream->StreamId());
      }

      auto last_active_time = GetCurrentTimeMicros();

      std::optional<cdc::CDCStateTableEntry> parent_entry_opt;
      if (stream_type == cdc::CDCSDK) {
        parent_entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
            {split_tablet_ids.source, stream->StreamId()}, cdc::CDCStateTableEntrySelector()
                                                               .IncludeCheckpoint()
                                                               .IncludeActiveTime()
                                                               .IncludeCDCSDKSafeTime()));
        DCHECK(parent_entry_opt);
      }

      // In the case of a Consistent Snapshot Stream, set the active_time of the children tablets
      // to the corresponding value in the parent tablet.
      // This will allow to establish that a child tablet is of interest to a stream
      // iff the parent tablet is also of interest to the stream.
      // Thus, retention barriers, inherited from the parent tablet, can be released
      // on the children tablets also if not of interest to the stream
      if (stream->IsConsistentSnapshotStream()) {
        LOG_WITH_FUNC(INFO) << "Copy active time from parent to child tablets"
                            << " Tablets involved: " << split_tablet_ids.ToString()
                            << " Consistent Snapshot StreamId: " << stream->StreamId();
        DCHECK(parent_entry_opt->active_time);
        if (parent_entry_opt && parent_entry_opt->active_time) {
            last_active_time = *parent_entry_opt->active_time;
        } else {
            LOG_WITH_FUNC(WARNING)
                << Format("Did not find $0 value in the cdc state table",
                          parent_entry_opt ? "active_time" : "row")
                << " for parent tablet: " << split_tablet_ids.source
                << " and stream: " << stream->StreamId();
        }
      }

      // Insert children entries into cdc_state now. In case of logical replication set the opid to
      // parent entry's opid. The split will be detected in the immediate next GetChanges call on
      // this tablet and we will transition to the children tablets. In other cases, set the opid to
      // 0.0 and the timestamp to NULL. When we process the parent's SPLIT_OP in GetChanges, we will
      // update the opid to the SPLIT_OP so that the children pollers continue from the next
      // records. When we process the first GetChanges for the children, then their timestamp value
      // will be set. We use this information to know that the children has been polled for. Once
      // both children have been polled for, then we can delete the parent tablet via the bg task
      // DoProcessXClusterParentTabletDeletion.
      for (const auto& child_tablet_id : split_tablet_ids.children) {
        cdc::CDCStateTableEntry entry(child_tablet_id, stream->StreamId());
        if (!stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
            DCHECK(parent_entry_opt->checkpoint);
            entry.checkpoint = *parent_entry_opt->checkpoint;
        } else {
            entry.checkpoint = OpId().Min();
        }

        if (stream_type == cdc::CDCSDK) {
          entry.active_time = last_active_time;
          DCHECK(parent_entry_opt->cdc_sdk_safe_time);
          if (parent_entry_opt && parent_entry_opt->cdc_sdk_safe_time) {
            entry.cdc_sdk_safe_time = *parent_entry_opt->cdc_sdk_safe_time;
          } else {
            LOG_WITH_FUNC(WARNING) << Format(
                                          "Did not find $0 value in the cdc state table",
                                          parent_entry_opt ? "cdc_sdk_safe_time" : "row")
                                   << " for parent tablet: " << split_tablet_ids.source
                                   << " and stream: " << stream->StreamId();
            entry.cdc_sdk_safe_time = last_active_time;
          }
        }

        entries.push_back(std::move(entry));
      }
    }
  }

  RETURN_NOT_OK(cdc_state_table_->InsertEntries(entries));

  TEST_SYNC_POINT("UpdateCDCProducerOnTabletSplit::AddChildEntriesComplete");

  TEST_SYNC_POINT("UpdateCDCProducerOnTabletSplit::ReVerifyStreamForAddingChildEntries");

  // Re-fetch all CDCSDK streams for the table and confirm the above inserted entries belong to one
  // of those streams. If not, update them and set the checkpoint to max. This is to handle race
  // condition where the table being removed from the stream splits simultaneously.
  if (!entries.empty() && !cdcsdk_stream_ids.empty()) {
    RETURN_NOT_OK(
        ReVerifyChildrenEntriesOnTabletSplit(producer_table_id, entries, cdcsdk_stream_ids));
  }

  return Status::OK();
}

Status CatalogManager::ReVerifyChildrenEntriesOnTabletSplit(
    const TableId& producer_table_id, const std::vector<cdc::CDCStateTableEntry>& entries,
    const std::unordered_set<xrepl::StreamId>& cdcsdk_stream_ids) {
  std::vector<CDCStreamInfoPtr> streams;
  {
    SharedLock lock(mutex_);
    streams = GetXReplStreamsForTable(producer_table_id, cdc::CDCSDK);
  }

  std::unordered_set<xrepl::StreamId> refetched_cdcsdk_stream_ids;
  for (const auto& stream : streams) {
    refetched_cdcsdk_stream_ids.insert(stream->StreamId());
  }

  std::vector<cdc::CDCStateTableKey> entries_to_delete;
  for (const auto& entry : entries) {
    auto stream_id = entry.key.stream_id;
    // Update the entries whose streams were not received on re-fetch.
    if (cdcsdk_stream_ids.contains(stream_id) && !refetched_cdcsdk_stream_ids.contains(stream_id)) {
      entries_to_delete.emplace_back(entry.key);
    }
  }

  if (!entries_to_delete.empty()) {
    LOG(INFO) << "Deleting the following state table entries as their table "
                 "is being/has been removed from the stream - "
              << AsString(entries_to_delete);
    RETURN_NOT_OK(cdc_state_table_->DeleteEntries(entries_to_delete));
  }

  return Status::OK();
}

Status CatalogManager::ChangeXClusterRole(
    const ChangeXClusterRoleRequestPB* req,
    ChangeXClusterRoleResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing ChangeXClusterRole request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();
  return Status::OK();
}

Status CatalogManager::BootstrapProducer(
    const master::BootstrapProducerRequestPB* req, master::BootstrapProducerResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing BootstrapProducer request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  SCHECK_EQ(req->db_type(), YQL_DATABASE_PGSQL, InvalidArgument, "Invalid database type");
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, namespace_name);
  SCHECK_GT(req->table_name_size(), 0, InvalidArgument, "No tables specified");
  SCHECK_EQ(
      req->pg_schema_name_size(), req->table_name_size(), InvalidArgument,
      "Number of tables and number of pg schemas must match");

  NamespaceIdentifierPB ns_id;
  ns_id.set_database_type(req->db_type());
  ns_id.set_name(req->namespace_name());
  auto ns = VERIFY_RESULT(FindNamespace(ns_id));
  // This is only called in non-DB-scoped xCluster, so is_automatic_ddl_mode is false (this will
  // skip getting sequences data and materialized views).
  auto all_tables = VERIFY_RESULT(
      GetTablesEligibleForXClusterReplication(*this, ns->id(), /*automatic_ddl_mode=*/false));

  cdc::BootstrapProducerRequestPB bootstrap_req;
  master::TSDescriptorPtr ts = nullptr;
  for (int i = 0; i < req->table_name_size(); i++) {
    auto& pg_schema_name = req->pg_schema_name(i);

    auto table_designator = std::find_if(
        all_tables.begin(), all_tables.end(),
        [&table_name = req->table_name(i),
         &pg_schema_name = req->pg_schema_name(i)](const TableDesignator& table_designator) {
          return table_designator.name() == table_name &&
                 table_designator.pgschema_name() == pg_schema_name;
        });
    SCHECK(
        table_designator != all_tables.end(), NotFound, Format("Table $0.$1$2 not found"),
        req->namespace_name(), (pg_schema_name.empty() ? "" : pg_schema_name + "."),
        req->table_name(i));

    bootstrap_req.add_table_ids(table_designator->id);
    resp->add_table_ids(table_designator->id);

    // Pick a valid tserver to bootstrap from.
    if (!ts) {
      auto table_info = VERIFY_RESULT(FindTableById(table_designator->id));
      ts = VERIFY_RESULT(VERIFY_RESULT(table_info->GetTablets()).front()->GetLeader());
    }
  }
  SCHECK(ts, IllegalState, "No valid tserver found to bootstrap from");

  std::shared_ptr<cdc::CDCServiceProxy> proxy;
  RETURN_NOT_OK(ts->GetProxy(&proxy));

  cdc::BootstrapProducerResponsePB bootstrap_resp;
  rpc::RpcController bootstrap_rpc;
  bootstrap_rpc.set_deadline(rpc->GetClientDeadline());

  RETURN_NOT_OK(proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &bootstrap_rpc));
  if (bootstrap_resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(bootstrap_resp.error().status()));
  }

  resp->mutable_bootstrap_ids()->Swap(bootstrap_resp.mutable_cdc_bootstrap_ids());
  if (bootstrap_resp.has_bootstrap_time()) {
    resp->set_bootstrap_time(bootstrap_resp.bootstrap_time());
  }

  return Status::OK();
}

Status CatalogManager::SetUniverseReplicationInfoEnabled(
    const xcluster::ReplicationGroupId& replication_group_id, bool is_enabled) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, replication_group_id);
    if (universe == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC producer universe", replication_group_id,
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Update the Master's Universe Config with the new state.
  {
    auto l = universe->LockForWrite();
    if (l->pb.state() != SysUniverseReplicationEntryPB::DISABLED &&
        l->pb.state() != SysUniverseReplicationEntryPB::ACTIVE) {
      return STATUS(
          InvalidArgument,
          Format(
              "Universe Replication in invalid state: $0. Retry or Delete.",
              SysUniverseReplicationEntryPB::State_Name(l->pb.state())),
          replication_group_id,
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
    if (is_enabled) {
      l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
    } else {
      // DISABLE.
      l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DISABLED);
    }
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), universe),
        "updating universe replication info in sys-catalog"));
    l.Commit();
  }
  return Status::OK();
}

Status CatalogManager::GetUniverseReplication(
    const GetUniverseReplicationRequestPB* req, GetUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUniverseReplication from " << RequestorString(rpc) << ": " << req->DebugString();

  if (!req->has_replication_group_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(
        universe_replication_map_, xcluster::ReplicationGroupId(req->replication_group_id()));
    if (universe == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC producer universe", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  resp->mutable_entry()->CopyFrom(universe->LockForRead()->pb);
  return Status::OK();
}

Status CatalogManager::UpdateConsumerOnProducerSplit(
    const UpdateConsumerOnProducerSplitRequestPB* req,
    UpdateConsumerOnProducerSplitResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "UpdateConsumerOnProducerSplit from " << RequestorString(rpc) << ": "
            << req->DebugString();

  if (!req->has_replication_group_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_stream_id()) {
    return STATUS(
        InvalidArgument, "Stream ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_producer_split_tablet_info()) {
    return STATUS(
        InvalidArgument, "Producer split tablet info must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  auto consumer_table_id = req->consumer_table_id();
  // If the consumer table ID is not provided (only for upgrade cases) get it from the stream entry.
  if (consumer_table_id.empty()) {
    consumer_table_id = VERIFY_RESULT(GetConsumerTableIdForStreamId(
        *this, xcluster::ReplicationGroupId(req->replication_group_id()),
        VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()))));
  }

  auto locked_config_and_ranges =
      VERIFY_RESULT(LockClusterConfigAndGetTableKeyRanges(*this, consumer_table_id));
  auto& cluster_config = locked_config_and_ranges.cluster_config;
  auto& l = locked_config_and_ranges.write_lock;
  auto& consumer_tablet_keys = locked_config_and_ranges.table_key_ranges;

  auto replication_group_map =
      l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto producer_entry = FindOrNull(*replication_group_map, req->replication_group_id());
  if (!producer_entry) {
    return STATUS_FORMAT(
        NotFound, "Unable to find the producer entry for universe $0", req->replication_group_id());
  }
  auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), req->stream_id());
  if (!stream_entry) {
    return STATUS_FORMAT(
        NotFound, "Unable to find the stream entry for universe $0, stream $1",
        req->replication_group_id(), req->stream_id());
  }
  SCHECK_EQ(
      stream_entry->consumer_table_id(), consumer_table_id, InvalidArgument,
      "Consumer table ID provided in request does not match the stream entry");

  const auto& split_info = req->producer_split_tablet_info();
  const auto split_children = GetSplitChildTabletIds(split_info);
  const auto split_keys = GetSplitPartitionKeys(split_info);

  SCHECK_EQ(
      split_children.size() - 1, split_keys.size(), IllegalState,
      "Unexpected number of partition keys");
  SplitTabletIds split_tablet_id {
    .source = split_info.tablet_id(),
    .children = split_children
  };

  bool found_source = false, found_all_split_children = false;
  RETURN_NOT_OK(UpdateTabletMappingOnProducerSplit(
      consumer_tablet_keys, split_tablet_id, split_keys, &found_source, &found_all_split_children,
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
    LOG(WARNING) << "Unable to find matching source tablet "
                 << split_info.tablet_id() << " for universe "
                 << req->replication_group_id() << " stream " << req->stream_id();

    return Status::OK();
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers (and new
  // pollers are created for the new tablets).
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "Updating cluster config in sys-catalog"));

  xcluster_manager_->SyncConsumerReplicationStatusMap(
      xcluster::ReplicationGroupId(req->replication_group_id()), *replication_group_map);
  l.Commit();

  xcluster_manager_->CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

// Related function: PlayChangeMetadataRequest() in tablet_bootstrap.cc.
Status CatalogManager::UpdateConsumerOnProducerMetadata(
    const UpdateConsumerOnProducerMetadataRequestPB* req,
    UpdateConsumerOnProducerMetadataResponsePB* resp, rpc::RpcContext* rpc) {
  LOG_WITH_FUNC(INFO) << " from " << RequestorString(rpc) << ": " << req->DebugString();

  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_xcluster_skip_schema_compatibility_checks_on_alter))) {
    resp->set_should_wait(false);
    return Status::OK();
  }

  const xcluster::ReplicationGroupId replication_group_id(req->replication_group_id());
  const auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));

  return DoUpdateConsumerOnProducerMetadata(
      replication_group_id, stream_id, req->producer_schema_version(),
      req->consumer_schema_version(), req->colocation_id(),
      /*check_min_consumer_schema_version=*/false, resp);
}

Status CatalogManager::DoUpdateConsumerOnProducerMetadata(
    const xcluster::ReplicationGroupId& replication_group_id, const xrepl::StreamId& stream_id,
    SchemaVersion producer_schema_version, SchemaVersion consumer_schema_version,
    ColocationId colocation_id, bool check_min_consumer_schema_version,
    UpdateConsumerOnProducerMetadataResponsePB* resp) {
  // Get corresponding local data for this stream.
  TableId consumer_table_id = VERIFY_RESULT(
      xcluster_manager_->GetConsumerTableIdForStreamId(replication_group_id, stream_id));

  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);

    // The destination table should be found or created by now.
    table = tables_->FindTableOrNull(consumer_table_id);
  }
  SCHECK(table, NotFound, Format("Missing table id $0", consumer_table_id));

  // Use the stream ID to find ClusterConfig entry
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto replication_group_map =
      l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto producer_entry = FindOrNull(*replication_group_map, replication_group_id.ToString());
  SCHECK(producer_entry, NotFound, Format("Missing replication group $0", replication_group_id));
  auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id.ToString());
  SCHECK(
      stream_entry, NotFound,
      Format("Missing replication group $0, stream $1", replication_group_id, stream_id));

  cdc::SchemaVersionsPB* schema_versions_pb = nullptr;
  bool schema_versions_updated = false;

  // TODO (#16557): Support remove_table_id() for colocated tables / tablegroups.
  if (IsColocationParentTableId(consumer_table_id) && colocation_id != kColocationIdNotSet) {
    auto map = stream_entry->mutable_colocated_schema_versions();
    schema_versions_pb = FindOrNull(*map, colocation_id);
    if (nullptr == schema_versions_pb) {
      // If the colocation_id itself does not exist, it needs to be recorded in clusterconfig.
      // This is to handle the case where source-target schema version mapping is 0:0.
      schema_versions_updated = true;
      schema_versions_pb = &((*map)[colocation_id]);
    }
  } else {
    schema_versions_pb = stream_entry->mutable_schema_versions();
  }

  SchemaVersion current_producer_schema_version =
      schema_versions_pb->current_producer_schema_version();
  SchemaVersion current_consumer_schema_version =
      schema_versions_pb->current_consumer_schema_version();
  auto* old_producer_schema_versions = schema_versions_pb->mutable_old_producer_schema_versions();
  auto* old_consumer_schema_versions = schema_versions_pb->mutable_old_consumer_schema_versions();
  std::map<SchemaVersion, SchemaVersion> old_schema_versions_map;
  for (int i = 0; i < old_producer_schema_versions->size(); ++i) {
    old_schema_versions_map[old_producer_schema_versions->Get(i)] =
        old_consumer_schema_versions->Get(i);
  }

  if (check_min_consumer_schema_version) {
    // Verify that the consumer schema version is greater than all values in the mapping
    // (otherwise it could have been gc-ed).
    auto min_consumer_schema_version = current_consumer_schema_version;
    for (const auto& old_consumer_schema_version : *old_consumer_schema_versions) {
      min_consumer_schema_version =
          std::min(min_consumer_schema_version, old_consumer_schema_version);
    }
    SCHECK_EC_FORMAT(
        consumer_schema_version >= min_consumer_schema_version, InvalidArgument,
        MasterError(MasterErrorPB::XCLUSTER_CONSUMER_SCHEMA_VERSION_TOO_OLD),
        "Received too old consumer schema version for replication group $0, consumer table $1. "
        "Min consumer schema version in map: $2, received consumer schema version: $3",
        replication_group_id, consumer_table_id, min_consumer_schema_version,
        consumer_schema_version);
  }

  if (producer_schema_version == current_producer_schema_version ||
      old_schema_versions_map.contains(producer_schema_version)) {
    // If we have already seen this producer schema version, then verify that the consumer schema
    // version matches what we saw from other tablets or we received a new one.
    // If we get an older schema version from the consumer, that's an indication that it
    // has not yet performed the ALTER and caught up to the latest schema version so fail the
    // request until it catches up to the latest schema version.
    auto prev_consumer_schema_version = FindWithDefault(
        old_schema_versions_map, producer_schema_version, current_consumer_schema_version);
    SCHECK_GE(
        consumer_schema_version, prev_consumer_schema_version, InternalError,
        Format(
            "Received older consumer schema version for replication group $1, consumer table $2",
            replication_group_id, consumer_table_id));
  } else if (producer_schema_version > current_producer_schema_version) {
    // Incoming producer version is greater than anything we've seen before, update stored versions.
    old_schema_versions_map[current_producer_schema_version] = current_consumer_schema_version;
    current_producer_schema_version = producer_schema_version;
    current_consumer_schema_version = consumer_schema_version;
    schema_versions_updated = true;
  } else {
    // This is an older producer schema version that we haven't seen before, need to keep track of
    // it to handle old rows.
    old_schema_versions_map[producer_schema_version] = consumer_schema_version;
    schema_versions_updated = true;
  }

  // Clean up the oldest schema versions if we've reached our max limit.
  auto max_num_schemas =
      FLAGS_xcluster_store_older_schema_versions ? FLAGS_xcluster_max_old_schema_versions : 1;
  while (old_schema_versions_map.size() > max_num_schemas) {
    LOG(INFO) << Format(
        "Removing oldest schema version (producer version: $0, consumer version: $1) for "
        "replication group $2, consumer table $3",
        old_schema_versions_map.begin()->first, old_schema_versions_map.begin()->second,
        replication_group_id, consumer_table_id);
    old_schema_versions_map.erase(old_schema_versions_map.begin());
    schema_versions_updated = true;
  }

  // Update the schema versions in the stream entry.
  schema_versions_pb->set_current_producer_schema_version(current_producer_schema_version);
  schema_versions_pb->set_current_consumer_schema_version(current_consumer_schema_version);
  if (schema_versions_updated) {
    old_producer_schema_versions->Clear();
    old_consumer_schema_versions->Clear();
    for (const auto& [producer_schema_version, consumer_schema_version] : old_schema_versions_map) {
      old_producer_schema_versions->Add(producer_schema_version);
      old_consumer_schema_versions->Add(consumer_schema_version);
    }
  }

  // Set the values for the response.
  auto resp_schema_versions = resp->mutable_schema_versions();
  resp_schema_versions->set_current_producer_schema_version(current_producer_schema_version);
  resp_schema_versions->set_current_consumer_schema_version(current_consumer_schema_version);
  resp_schema_versions->mutable_old_producer_schema_versions()->Add(
      old_producer_schema_versions->begin(), old_producer_schema_versions->end());
  resp_schema_versions->mutable_old_consumer_schema_versions()->Add(
      old_consumer_schema_versions->begin(), old_consumer_schema_versions->end());

  SchemaVersion last_old_producer_schema_version =
      old_producer_schema_versions->empty() ? 0 : *old_producer_schema_versions->rbegin();
  SchemaVersion last_old_consumer_schema_version =
      old_consumer_schema_versions->empty() ? 0 : *old_consumer_schema_versions->rbegin();

  if (schema_versions_updated) {
    // Bump the ClusterConfig version so we'll broadcast new schema versions.
    l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
    RETURN_NOT_OK(CheckStatus(sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
        "Updating cluster config in sys-catalog"));
    l.Commit();
  } else {
    // Make sure to release this lock, especially since we grab mutex_ again later.
    l.Unlock();
  }

  LOG(INFO) << Format(
      "Updated the schema versions for table $0 with stream id $1, colocation id $2."
      "Current producer schema version:$3, current consumer schema version:$4, "
      "last old producer schema version:$5, last old consumer schema version:$6, "
      "replication group:$7",
      consumer_table_id, stream_id, colocation_id, current_producer_schema_version,
      current_consumer_schema_version, last_old_producer_schema_version,
      last_old_consumer_schema_version, replication_group_id);
  return Status::OK();
}

Status CatalogManager::InsertHistoricalColocatedSchemaPacking(
    const xcluster::ReplicationGroupId& replication_group_id, const TablegroupId& tablegroup_id,
    const ColocationId colocation_id,
    const std::function<Status(UniverseReplicationInfo&)>& add_historical_schema_fn) {
  LockGuard lock(mutex_);
  // First check if this table has been created yet. If so, then we don't need to add historical
  // schemas and can just add schemas in the regular way (via InsertPackedSchemaForXClusterTarget).
  auto table_res = GetColocatedTableIdUnlocked(tablegroup_id, colocation_id);
  SCHECK(!table_res.ok(), AlreadyPresent, "Table is already created, not adding historical schema");
  SCHECK(
      table_res.status().IsNotFound(), IllegalState,
      "Unexpected error finding colocated table: ", table_res.status());

  // Get the replication group.
  auto universe = FindPtrOrNull(universe_replication_map_, replication_group_id);
  SCHECK(universe, NotFound, "Universe not found: ", replication_group_id);

  return add_historical_schema_fn(*universe);
}

Status CatalogManager::WaitForReplicationDrain(
    const WaitForReplicationDrainRequestPB* req,
    WaitForReplicationDrainResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "WaitForReplicationDrain from " << RequestorString(rpc) << ": "
            << req->DebugString();
  if (req->stream_ids_size() == 0) {
    return STATUS(
        InvalidArgument, "No stream ID provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  MicrosecondsInt64 target_time =
      req->has_target_time() ? req->target_time() : GetCurrentTimeMicros();
  if (!req->target_time()) {
    LOG(INFO) << "WaitForReplicationDrain: target_time unspecified. Default to " << target_time;
  }

  // Find all streams to check for replication drain.
  std::unordered_set<xrepl::StreamId> filter_stream_ids;
  for (auto& stream_id : req->stream_ids()) {
    filter_stream_ids.emplace(VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
  }

  std::unordered_set<xrepl::StreamId> found_stream_ids;
  std::vector<CDCStreamInfoPtr> streams;
  {
    std::vector<CDCStreamInfoPtr> all_streams;
    GetAllCDCStreams(&all_streams);
    for (const auto& stream : all_streams) {
      if (!filter_stream_ids.contains(stream->StreamId())) {
        continue;
      }
      streams.push_back(stream);
      found_stream_ids.insert(stream->StreamId());
    }
  }

  // Verify that all specified stream_ids are found.
  std::ostringstream not_found_streams;
  for (const auto& stream_id : filter_stream_ids) {
    if (!found_stream_ids.contains(stream_id)) {
      not_found_streams << stream_id << ",";
    }
  }
  if (!not_found_streams.str().empty()) {
    string stream_ids = not_found_streams.str();
    stream_ids.pop_back();  // Remove the last comma.
    return STATUS(
        InvalidArgument, Format("Streams not found: $0", stream_ids), req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Keep track of the drained (stream_id, tablet_id) tuples.
  std::unordered_set<StreamTabletIdPair, StreamTabletIdHash> drained_stream_tablet_ids;

  // Calculate deadline and interval for each CallReplicationDrain call to tservers.
  CoarseTimePoint deadline = rpc->GetClientDeadline();
  if (deadline == CoarseTimePoint::max()) {
    deadline = CoarseMonoClock::now() + MonoDelta::FromMilliseconds(FLAGS_master_rpc_timeout_ms);
  }
  auto timeout =
      MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_wait_replication_drain_retry_timeout_ms));

  while (true) {
    // 1. Construct the request to be sent to each tserver. Meanwhile, collect all tuples that
    //    are not marked as drained in previous iterations.
    std::unordered_set<StreamTabletIdPair, StreamTabletIdHash> undrained_stream_tablet_ids;
    std::unordered_map<std::shared_ptr<cdc::CDCServiceProxy>, cdc::CheckReplicationDrainRequestPB>
        proxy_to_request;
    for (const auto& stream : streams) {
      for (const auto& table_id : stream->table_id()) {
        auto stripped_table_id = xcluster::StripSequencesDataAliasIfPresent(table_id);
        auto table_info = VERIFY_RESULT(FindTableById(stripped_table_id));
        RSTATUS_DCHECK(table_info != nullptr, NotFound, "Table ID not found: " + table_id);

        for (const auto& tablet : VERIFY_RESULT(table_info->GetTablets())) {
          // (1) If tuple is marked as drained in a previous iteration, skip it.
          // (2) Otherwise, check if it is drained in the current iteration.
          if (drained_stream_tablet_ids.contains({stream->StreamId(), tablet->id()})) {
            continue;
          }
          undrained_stream_tablet_ids.insert({stream->StreamId(), tablet->id()});

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
          auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_info.stream_id()));
          undrained_stream_tablet_ids.erase({stream_id, stream_info.tablet_id()});
          drained_stream_tablet_ids.insert({stream_id, stream_info.tablet_id()});
        }
      }
    }

    // 3. Check if all current undrained tuples are marked as drained, or it is too close
    //    to deadline. If so, prepare the response and terminate the loop.
    if (undrained_stream_tablet_ids.empty() || deadline - CoarseMonoClock::Now() <= timeout * 2) {
      std::ostringstream output_stream;
      output_stream << "WaitForReplicationDrain from " << RequestorString(rpc) << " finished.";
      if (!undrained_stream_tablet_ids.empty()) {
        output_stream << " Found undrained streams:";
      }

      for (const auto& [stream_id, table_id] : undrained_stream_tablet_ids) {
        output_stream << "\n\tStream: " << stream_id << ", Tablet: " << table_id;
        auto undrained_stream_info = resp->add_undrained_stream_info();
        undrained_stream_info->set_stream_id(stream_id.ToString());
        undrained_stream_info->set_tablet_id(table_id);
      }
      LOG(INFO) << output_stream.str();
      break;
    }
    SleepFor(timeout);
  }

  return Status::OK();
}

PgReplicaIdentity GetReplicaIdentityFromRecordType(std::string record_type_name) {
  cdc::CDCRecordType record_type = cdc::CDCRecordType::CHANGE;
  CDCRecordType_Parse(record_type_name, &record_type);
  switch (record_type) {
    case cdc::CDCRecordType::ALL: FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::PG_FULL: return PgReplicaIdentity::FULL;
    case cdc::CDCRecordType::PG_DEFAULT: return PgReplicaIdentity::DEFAULT;
    case cdc::CDCRecordType::PG_NOTHING: return PgReplicaIdentity::NOTHING;
    case cdc::CDCRecordType::PG_CHANGE_OLD_NEW: FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::FULL_ROW_NEW_IMAGE: FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES:
      LOG(WARNING) << "The record type of the older stream does not have a corresponding replica "
                      "identity. Going forward with replica identity CHANGE.";
      FALLTHROUGH_INTENDED;
    case cdc::CDCRecordType::CHANGE: return PgReplicaIdentity::CHANGE;
    // This case should never be reached.
    default: return PgReplicaIdentity::CHANGE;
  }
}

Status CatalogManager::YsqlBackfillReplicationSlotNameToCDCSDKStream(
    const YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB* req,
    YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing YsqlBackfillReplicationSlotNameToCDCSDKStream request from "
            << RequestorString(rpc) << ": " << req->ShortDebugString();

  if (!FLAGS_ysql_yb_enable_replication_commands ||
      !FLAGS_ysql_yb_enable_replica_identity ||
      !FLAGS_enable_backfilling_cdc_stream_with_replication_slot) {
    RETURN_INVALID_REQUEST_STATUS("Backfilling replication slot name is disabled");
  }

  if (!req->has_stream_id() || !req->has_cdcsdk_ysql_replication_slot_name()) {
    RETURN_INVALID_REQUEST_STATUS(
        "Both CDC Stream ID and Replication slot name must be provided");
  }

  RETURN_NOT_OK(ReplicationSlotValidateName(req->cdcsdk_ysql_replication_slot_name()));

  auto replication_slot_name = ReplicationSlotName(req->cdcsdk_ysql_replication_slot_name());
  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  if (stream == nullptr || stream->LockForRead()->is_deleting()) {
    return STATUS(
        NotFound, "Could not find CDC stream", MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto namespace_id = stream->LockForRead()->namespace_id();
  auto ns = VERIFY_RESULT(FindNamespaceById(namespace_id));

  if (ns->database_type() != YQLDatabase::YQL_DATABASE_PGSQL) {
    RETURN_INVALID_REQUEST_STATUS(
        "Only CDCSDK streams created on PGSQL namespaces can have a replication slot name");
  }

  if (!stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
    RETURN_INVALID_REQUEST_STATUS(
        "Cannot update the replication slot name of a CDCSDK stream");
  }

  LOG_WITH_FUNC(INFO) << "Valid request. Updating the replication slot name";
  {
    LockGuard lock(mutex_);

    if (cdcsdk_replication_slots_to_stream_map_.contains(replication_slot_name)) {
      return STATUS(
          AlreadyPresent, "A CDC stream with the replication slot name already exists",
          MasterError(MasterErrorPB::OBJECT_ALREADY_PRESENT));
    }

    auto stream_lock = stream->LockForWrite();
    auto& pb = stream_lock.mutable_data()->pb;

    pb.set_cdcsdk_ysql_replication_slot_name(req->cdcsdk_ysql_replication_slot_name());
    cdcsdk_replication_slots_to_stream_map_.insert_or_assign(replication_slot_name, stream_id);

    PgReplicaIdentity replica_identity;
    bool has_record_type =  false;
    for (const auto& option : pb.options()) {
      if (option.key() == cdc::kRecordType) {
        // Check if record type is a valid replica identity, if not assign replica
        // identity CHANGE.
        replica_identity = GetReplicaIdentityFromRecordType(option.value());
        has_record_type = true;
        break;
      }
    }
    // This should never happen.
    RSTATUS_DCHECK(
        has_record_type, NotFound, Format("Option record_type not present in stream $0"),
        stream_id);
    for(auto table_id : pb.table_id()) {
       pb.mutable_replica_identity_map()->insert({table_id, replica_identity});
    }

    // TODO(#22249): Set the plugin name for streams upgraded from older clusters.

    stream_lock.Commit();
  }

  return Status::OK();
}

Status CatalogManager::DisableDynamicTableAdditionOnCDCSDKStream(
    const DisableDynamicTableAdditionOnCDCSDKStreamRequestPB* req,
    DisableDynamicTableAdditionOnCDCSDKStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DisableDynamicTableAdditionOnCDCSDKStream request from "
            << RequestorString(rpc) << ": " << req->ShortDebugString();

  if (!req->has_stream_id()) {
    RETURN_INVALID_REQUEST_STATUS("CDC Stream ID must be provided");
  }

  if (!FLAGS_cdcsdk_enable_dynamic_tables_disable_option) {
    RETURN_INVALID_REQUEST_STATUS(
        "Disabling addition of dynamic tables to CDC stream is disallowed in the middle of an "
        "upgrade. Finalize the upgrade and try again");
  }

  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  RETURN_NOT_OK(ValidateStreamForTableRemoval(stream));

  if (stream->IsDynamicTableAdditionDisabled()) {
    return STATUS(AlreadyPresent, "Dynamic table addition already disabled on the CDC stream");
  }

  // Disable dynamic table addition by setting the stream metadata field to true.
  {
    auto stream_lock = stream->LockForWrite();
    auto& pb = stream_lock.mutable_data()->pb;

    pb.set_cdcsdk_disable_dynamic_table_addition(true);

    RETURN_ACTION_NOT_OK(
        sys_catalog_->Upsert(leader_ready_term(), stream), "Updating CDC stream in system catalog");

    stream_lock.Commit();
  }

  LOG_WITH_FUNC(INFO) << "Successfully disabled dynamic table addition on CDC stream: "
                      << stream_id;

  return Status::OK();
}

Status CatalogManager::RemoveUserTableFromCDCSDKStream(
    const RemoveUserTableFromCDCSDKStreamRequestPB* req,
    RemoveUserTableFromCDCSDKStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing RemoveUserTableFromCDCSDKStream request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  if (!req->has_stream_id() || !req->has_table_id()) {
    RETURN_INVALID_REQUEST_STATUS("Both CDC Stream ID and table ID must be provided");
  }

  if (!FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup) {
    RETURN_INVALID_REQUEST_STATUS(
        "Removal of user table from CDC stream is disallowed in the middle of an "
        "upgrade. Finalize the upgrade and try again");
  }

  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));
  auto table_id = req->table_id();

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  RETURN_NOT_OK(ValidateStreamForTableRemoval(stream));

  auto stream_ns_id = stream->LockForRead()->namespace_id();

  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);
    table = tables_->FindTableOrNull(table_id);
  }

  RETURN_NOT_OK(ValidateTableForRemovalFromCDCSDKStream(table, /* check_for_ineligibility */ true));

  auto table_ns_id = table->LockForRead()->namespace_id();
  if (table_ns_id != stream_ns_id) {
    RETURN_INVALID_REQUEST_STATUS("Stream and Table are not under the same namespace");
  }

  // Add to the 'cdcsdk_unprocessed_unqualified_tables_to_streams_' map which will be further
  // processed by the catalog manager bg thread.
  RETURN_NOT_OK(AddTableForRemovalFromCDCSDKStream({table_id}, stream));

  LOG_WITH_FUNC(INFO) << "Successfully added table " << table_id
                      << " to unqualified list for CDC stream: " << stream_id;

  return Status::OK();
}

Status CatalogManager::ValidateAndSyncCDCStateEntriesForCDCSDKStream(
    const ValidateAndSyncCDCStateEntriesForCDCSDKStreamRequestPB* req,
    ValidateAndSyncCDCStateEntriesForCDCSDKStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing ValidateAndSyncCDCStateEntriesForCDCSDKStream request from "
            << RequestorString(rpc) << ": " << req->ShortDebugString();

  if (!req->has_stream_id()) {
    RETURN_INVALID_REQUEST_STATUS("CDC Stream ID must be provided");
  }

  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));
  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  RETURN_NOT_OK(ValidateStreamForTableRemoval(stream));

  std::unordered_set<TableId> tables_in_stream_metadata;
  {
    auto stream_lock = stream->LockForRead();
    tables_in_stream_metadata.reserve(stream_lock->table_id().size());
    for (const auto& table_id : stream_lock->table_id()) {
      tables_in_stream_metadata.insert(table_id);
    }
  }

  auto deleted_state_table_entries =
      VERIFY_RESULT(SyncCDCStateTableEntries(stream_id, tables_in_stream_metadata));

  for (const auto& key : deleted_state_table_entries) {
    if (key.colocated_table_id.empty()) {
      resp->add_deleted_tablet_entries(key.tablet_id);
    }
  }

  LOG_WITH_FUNC(INFO)
      << "Successfully validated and synced cdc state table entries for CDC stream: " << stream_id;

  return Status::OK();
}

Status CatalogManager::RemoveTablesFromCDCSDKStream(
    const RemoveTablesFromCDCSDKStreamRequestPB* req, RemoveTablesFromCDCSDKStreamResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing RemoveTablesFromCDCSDKStream request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  if (!req->has_stream_id()) {
    RETURN_INVALID_REQUEST_STATUS(
        "Stream ID is requirred for removing tables from CDCSDK stream");
  }

  auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req->stream_id()));

  const auto& table_ids = req->table_ids();
  if (table_ids.empty()) {
    RETURN_INVALID_REQUEST_STATUS("No Table ID provided for removal from CDCSDK stream");
  }

  CDCStreamInfoPtr stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, stream_id);
  }

  RETURN_NOT_OK(ValidateStreamForTableRemoval(stream));

  std::unordered_set<TableId> valid_tables_for_removal;
  Status status;
  for (const auto& table_id : table_ids) {
    scoped_refptr<TableInfo> table;
    {
      SharedLock lock(mutex_);
      table = tables_->FindTableOrNull(table_id);
    }

    status = ValidateTableForRemovalFromCDCSDKStream(table, /* check_for_ineligibility */ true);
    if (!status.ok()) {
      // No need to return the non-ok status to the caller (Update Peers and Metrics), since it will
      // be retried if the state table entry is found again in next iteration.
      LOG(WARNING) << "Could not remove table: " << table_id << " from stream: " << req->stream_id()
                   << " : " << status.ToString();
      continue;
    }

    valid_tables_for_removal.insert(table_id);
  }

  // Add to the 'cdcsdk_unprocessed_unqualified_tables_to_streams_' map which will be further
  // processed by the catalog manager bg thread.
  RETURN_NOT_OK(AddTableForRemovalFromCDCSDKStream(valid_tables_for_removal, stream));

  if (!valid_tables_for_removal.empty()) {
    LOG_WITH_FUNC(INFO) << "Successfully added table " << AsString(valid_tables_for_removal)
                        << " to unqualified list for CDC stream: " << stream_id;
  }

  return Status::OK();
}

std::vector<SysUniverseReplicationEntryPB>
CatalogManager::GetAllXClusterUniverseReplicationInfos() {
  SharedLock lock(mutex_);
  std::vector<SysUniverseReplicationEntryPB> result;
  for (const auto& [_, universe_info] : universe_replication_map_) {
    auto l = universe_info->LockForRead();
    result.push_back(l->pb);
  }

  return result;
}

// Validate that the given replication slot name is valid.
// This function is a duplicate of the ReplicationSlotValidateName function from
// src/postgres/src/backend/replication/slot.c
Status CatalogManager::ReplicationSlotValidateName(const std::string& replication_slot_name) {
  if (replication_slot_name.empty()) {
    RETURN_INVALID_REQUEST_STATUS("Replication slot name cannot be empty");
  }

  // The 64 comes from the NAMEDATALEN constant in YSQL.
  if (replication_slot_name.size() >= 64) {
    RETURN_INVALID_REQUEST_STATUS("Replication slot name length must be < 64");
  }

  for (auto c : replication_slot_name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_'))) {
      RETURN_INVALID_REQUEST_STATUS(
          "Replication slot names may only contain lower case letters, numbers, and the underscore "
          "character.");
    }
  }

  return Status::OK();
}

Status CatalogManager::TEST_CDCSDKFailCreateStreamRequestIfNeeded(const std::string& sync_point) {
  bool fail_create_cdc_stream_request = false;
  TEST_SYNC_POINT_CALLBACK(sync_point, &fail_create_cdc_stream_request);
  if (fail_create_cdc_stream_request) {
    return STATUS_FORMAT(Aborted, "Test failure for sync point $0.", sync_point);
  }
  return Status::OK();
}

bool CatalogManager::IsTablePartOfXRepl(const TableId& table_id) const {
  return xcluster_manager_->IsTableReplicated(table_id) || IsTablePartOfCDCSDK(table_id);
}

bool CatalogManager::IsTablePartOfCDCSDK(
    const TableId& table_id, bool require_replication_slot) const {
  DCHECK(xrepl_maps_loaded_);
  auto* stream_ids = FindOrNull(cdcsdk_tables_to_stream_map_, table_id);
  if (stream_ids) {
    for (const auto& stream_id : *stream_ids) {
      auto stream_info = FindPtrOrNull(cdc_stream_map_, stream_id);
      if (stream_info) {
        auto s = stream_info->LockForRead();
        if (!s->is_deleting() &&
            (!require_replication_slot || s->pb.has_cdcsdk_ysql_replication_slot_name())) {
          VLOG(1) << "Found an active CDCSDK stream: " << stream_id << " for table: " << table_id
                  << (require_replication_slot
                          ? " with replication slot: " + s->pb.cdcsdk_ysql_replication_slot_name()
                          : "");
          return true;
        }
      }
    }
  }

  return false;
}

std::unordered_set<xrepl::StreamId> CatalogManager::GetCDCSDKStreamsForTable(
    const TableId& table_id) const {
  DCHECK(xrepl_maps_loaded_);
  auto stream_ids = FindOrNull(cdcsdk_tables_to_stream_map_, table_id);
  if (!stream_ids) {
    return {};
  }
  return *stream_ids;
}

std::unordered_set<xrepl::StreamId> CatalogManager::GetAllCDCSDKStreamsForNamespace(
    const NamespaceId& ns_id) const {
  std::unordered_set<xrepl::StreamId> stream_ids;
  for (const auto& entry : cdc_stream_map_) {
    auto ltm = entry.second->LockForRead();

    if (!ltm->started_deleting() && !ltm->namespace_id().empty() && ltm->namespace_id() == ns_id) {
      stream_ids.insert(entry.first);
    }
  }
  return stream_ids;
}

Result<HybridTime> CatalogManager::GetMinRestartTimeAcrossSlots(
    const std::unordered_set<xrepl::StreamId>& stream_ids) {
  if (stream_ids.empty()) {
    return HybridTime::kInvalid;
  }

  // All these streams are on the same tablet (hence same DB). We can only have either logical
  // replication or gRPC replication on any DB. If the first stream present in cdc_stream_map_
  // among the ones passed, does't contain replication slot name it means that this DB has gRPC
  // streams.
  for (const auto stream_id : stream_ids) {
    if (cdc_stream_map_.contains(stream_id)) {
      if (cdc_stream_map_[stream_id]->GetCdcsdkYsqlReplicationSlotName().empty()) {
        return HybridTime::kInvalid;
      }
      break;
    }
  }

  HybridTime min_restart_time_across_all_slots;
  for (const auto& stream_id : stream_ids) {
    auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
        {kCDCSDKSlotEntryTabletId, stream_id},
        cdc::CDCStateTableEntrySelector().IncludeRecordIdCommitTime()));
    RSTATUS_DCHECK(entry_opt, NotFound, "Slot entry not found for stream_id : {}", stream_id);
    RSTATUS_DCHECK(
        entry_opt->record_id_commit_time.has_value(), NotFound,
        "Restart time not found for slot with stream_id : {}", stream_id);
    min_restart_time_across_all_slots =
        std::min(min_restart_time_across_all_slots, HybridTime(*entry_opt->record_id_commit_time));
  }

  return min_restart_time_across_all_slots;
}

Result<std::unordered_map<xrepl::StreamId, std::optional<HybridTime>>>
CatalogManager::GetCDCSDKStreamCreationTimeMap(
    const std::unordered_set<xrepl::StreamId>& stream_ids) {
  std::unordered_map<xrepl::StreamId, std::optional<HybridTime>> stream_creation_time_map;
  for (const auto stream_id : stream_ids) {
    auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
    RSTATUS_DCHECK(
        stream, NotFound, "Entry for stream: {} not found in cdc_stream_map_", stream_id);

    auto stream_lock_pb = stream->LockForRead()->pb;
    if (stream_lock_pb.has_stream_creation_time()) {
      stream_creation_time_map.emplace(stream_id, stream_lock_pb.stream_creation_time());
    } else {
      stream_creation_time_map.emplace(stream_id, std::nullopt);
    }
  }
  return stream_creation_time_map;
}

bool CatalogManager::IsCDCSDKTabletExpiredOrNotOfInterest(
    HybridTime last_active_time, std::optional<HybridTime> stream_creation_time) {
  if (last_active_time.AddMilliseconds(FLAGS_cdc_intent_retention_ms) < Clock()->Now()) {
    return true;
  }

  auto not_of_interest_limit_secs =
      GetAtomicFlag(&FLAGS_cdcsdk_tablet_not_of_interest_timeout_secs) + 2;
  if (!stream_creation_time.has_value() || last_active_time != *stream_creation_time ||
      last_active_time.AddSeconds(not_of_interest_limit_secs) > Clock()->Now()) {
    return false;
  }
  return true;
}

void CatalogManager::RunXReplBgTasks(const LeaderEpoch& epoch) {
  TEST_SYNC_POINT("RunXReplBgTasks::Start");

  if (!FLAGS_TEST_cdcsdk_disable_deleted_stream_cleanup) {
    WARN_NOT_OK(CleanUpDeletedXReplStreams(epoch), "Failed Cleaning Deleted XRepl Streams");
  }

  // Clean up Failed Universes on the Consumer.
  WARN_NOT_OK(ClearFailedUniverse(epoch), "Failed Clearing Failed Universe");

  // Clean up Failed Replication Bootstrap on the Consumer.
  WARN_NOT_OK(ClearFailedReplicationBootstrap(), "Failed Clearing Failed Replication Bootstrap");

  if (!FLAGS_TEST_cdcsdk_disable_drop_table_cleanup) {
    WARN_NOT_OK(CleanUpCDCSDKStreamsMetadata(epoch), "Failed Cleanup CDCSDK Streams Metadata");
  }

  // Restart xCluster and CDCSDK parent tablet deletion bg task.
  StartXReplParentTabletDeletionTaskIfStopped();
}

Status CatalogManager::ClearFailedUniverse(const LeaderEpoch& epoch) {
  // Delete a single failed universe from universes_to_clear_.
  if (PREDICT_FALSE(FLAGS_disable_universe_gc)) {
    return Status::OK();
  }

  xcluster::ReplicationGroupId replication_group_id;
  {
    LockGuard lock(mutex_);

    if (universes_to_clear_.empty()) {
      return Status::OK();
    }
    // Get the first universe.  Only try once to avoid failure loops.
    replication_group_id = universes_to_clear_.front();
    universes_to_clear_.pop_front();
  }

  GetUniverseReplicationRequestPB universe_req;
  GetUniverseReplicationResponsePB universe_resp;
  universe_req.set_replication_group_id(replication_group_id.ToString());

  RETURN_NOT_OK(GetUniverseReplication(&universe_req, &universe_resp, /* RpcContext */ nullptr));

  DeleteUniverseReplicationRequestPB req;
  DeleteUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_ignore_errors(true);

  RETURN_NOT_OK(
      xcluster_manager_->DeleteUniverseReplication(&req, &resp, /* RpcContext */ nullptr, epoch));

  return Status::OK();
}

Status CatalogManager::DoClearFailedReplicationBootstrap(
    const CleanupFailedReplicationBootstrapInfo& info) {
  const auto& [
    state,
    xcluster_rpc_task,
    bootstrap_ids,
    old_snapshot_id,
    new_snapshot_id,
    namespace_map,
    type_map,
    tables_data,
    epoch
  ] = info;

  Status s = Status::OK();
  switch (state) {
    case SysUniverseReplicationBootstrapEntryPB_State_SETUP_REPLICATION:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_RESTORE_SNAPSHOT:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_TRANSFER_SNAPSHOT:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_CREATE_CONSUMER_SNAPSHOT: {
      if (!new_snapshot_id.IsNil()) {
        auto deadline = CoarseMonoClock::Now() + 30s;
        s = master_->snapshot_coordinator().Delete(new_snapshot_id, leader_ready_term(), deadline);
        if (!s.ok()) {
          LOG(WARNING) << Format("Failed to delete snapshot on consumer on status: $0", s);
        }
      }
    }
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_IMPORT_SNAPSHOT:
      DeleteNewSnapshotObjects(namespace_map, type_map, tables_data, epoch);
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_CREATE_PRODUCER_SNAPSHOT: {
      if (!old_snapshot_id.IsNil()) {
        DeleteSnapshotResponsePB resp;
        s = xcluster_rpc_task->client()->DeleteSnapshot(old_snapshot_id, &resp);
        if (!s.ok()) {
          LOG(WARNING) << Format(
              "Failed to send delete snapshot request to producer on status: $0", s);
        }
        if (resp.has_error()) {
          LOG(WARNING) << Format(
              "Failed to delete snapshot on producer with error: $0", resp.error());
        }
      }
    }
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_BOOTSTRAP_PRODUCER: {
      DeleteCDCStreamResponsePB resp;
      s = xcluster_rpc_task->client()->DeleteCDCStream(
          bootstrap_ids, /*force_delete=*/ true, /*ignore_errors=*/false, &resp);
      if (!s.ok()) {
        LOG(WARNING) << Format(
            "Failed to send delete CDC streams request to producer on status: $0", s);
      }
      if (resp.has_error()) {
        LOG(WARNING) << Format(
            "Failed to delete CDC streams on producer with error: $0", resp.error());
      }
    }
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_INITIALIZING:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_DONE:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_FAILED:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_DELETED:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_DELETED_ERROR:
      FALLTHROUGH_INTENDED;
    case SysUniverseReplicationBootstrapEntryPB_State_DELETING:
      break;
  }
  return s;
}

Status CatalogManager::ClearFailedReplicationBootstrap() {
  xcluster::ReplicationGroupId replication_id;
  {
    LockGuard lock(mutex_);

    if (replication_bootstraps_to_clear_.empty()) {
      return Status::OK();
    }
    // Get the first bootstrap.  Only try once to avoid failure loops.
    replication_id = replication_bootstraps_to_clear_.front();
    replication_bootstraps_to_clear_.pop_front();
  }

  // First get the universe.
  scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    bootstrap_info = FindPtrOrNull(universe_replication_bootstrap_map_, replication_id);
    if (bootstrap_info == nullptr) {
      auto error_msg =
          Format("UniverseReplicationBootstrap not found: $0", replication_id.ToString());
      LOG(WARNING) << error_msg;
      return STATUS(NotFound, error_msg);
    }
  }

  // Retrieve information required to cleanup replication bootstrap.
  CleanupFailedReplicationBootstrapInfo info;

  {
    auto l = bootstrap_info->LockForRead();
    info.state = l->failed_on();
    info.epoch = l->epoch();
    info.old_snapshot_id = l->old_snapshot_id();
    info.new_snapshot_id = l->new_snapshot_id();
    info.xcluster_rpc_task = VERIFY_RESULT(
        bootstrap_info->GetOrCreateXClusterRpcTasks(l->pb.producer_master_addresses()));

    for (const auto& entry : l->pb.table_bootstrap_ids()) {
      info.bootstrap_ids.emplace_back(VERIFY_RESULT(xrepl::StreamIdFromString(entry.second)));
    }

    l->set_into_namespace_map(&info.namespace_map);
    l->set_into_tables_data(&info.tables_data);
    l->set_into_ud_type_map(&info.type_map);
  }

  // Set sys catalog state to be DELETING.
  {
    auto l = bootstrap_info->LockForWrite();
    l.mutable_data()->pb.set_state(SysUniverseReplicationBootstrapEntryPB::DELETING);
    Status s = sys_catalog_->Upsert(leader_ready_term(), bootstrap_info);
    RETURN_NOT_OK(
        CheckLeaderStatus(s, "Updating delete universe replication info into sys-catalog"));
    TRACE("Wrote universe replication bootstrap info to sys-catalog");
    l.Commit();
  }

  // Start cleanup.
  auto l = bootstrap_info->LockForWrite();
  l.mutable_data()->pb.set_state(SysUniverseReplicationBootstrapEntryPB::DELETED);

  // Cleanup any objects created during the bootstrap process.
  WARN_NOT_OK(
      DoClearFailedReplicationBootstrap(info),
      "Failed to delete newly created objects in replication bootstrap");

  // Try to delete from sys catalog.
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Delete(leader_ready_term(), bootstrap_info),
      Format("updating sys-catalog, replication_group_id: $0", bootstrap_info->id()));

  // Remove it from the map.
  LockGuard lock(mutex_);
  if (universe_replication_bootstrap_map_.erase(bootstrap_info->ReplicationGroupId()) < 1) {
    LOG(WARNING) << "Failed to remove replication info from map: replication_group_id: "
                 << bootstrap_info->id();
  }

  TRACE("Wrote universe replication bootstrap info to sys-catalog");
  l.Commit();

  return Status::OK();
}

void CatalogManager::StartXReplParentTabletDeletionTaskIfStopped() {
  if (GetAtomicFlag(&FLAGS_cdc_parent_tablet_deletion_task_retry_secs) <= 0) {
    // Task is disabled.
    return;
  }
  const bool is_already_running = xrepl_parent_tablet_deletion_task_running_.exchange(true);
  if (!is_already_running) {
    ScheduleXReplParentTabletDeletionTask();
  }
}

void CatalogManager::ScheduleXReplParentTabletDeletionTask() {
  int wait_time = GetAtomicFlag(&FLAGS_cdc_parent_tablet_deletion_task_retry_secs);
  if (wait_time <= 0) {
    // Task has been disabled.
    xrepl_parent_tablet_deletion_task_running_ = false;
    return;
  }

  // Submit to run async in diff thread pool, since this involves accessing cdc_state.
  xrepl_parent_tablet_deletion_task_->Schedule(
      [this](const Status& status) {
        Status s = background_tasks_thread_pool_->SubmitFunc(
            [this] { ProcessXReplParentTabletDeletionPeriodically(); });
        if (!s.IsOk()) {
          // Failed to submit task to the thread pool. Mark that the task is now no longer running.
          LOG(WARNING) << "Failed to schedule: ProcessXReplParentTabletDeletionPeriodically";
          xrepl_parent_tablet_deletion_task_running_ = false;
        }
      },
      wait_time * 1s);
}

void CatalogManager::ProcessXReplParentTabletDeletionPeriodically() {
  if (!CheckIsLeaderAndReady().IsOk()) {
    xrepl_parent_tablet_deletion_task_running_ = false;
    return;
  }
  WARN_NOT_OK(DoProcessCDCSDKTabletDeletion(), "Failed to run DoProcessCDCSdkTabletDeletion.");
  WARN_NOT_OK(
      xcluster_manager_->DoProcessHiddenTablets(),
      "Failed to run xCluster DoProcessHiddenTablets.");

  // Schedule the next iteration of the task.
  ScheduleXReplParentTabletDeletionTask();
}

Status CatalogManager::DoProcessCDCSDKTabletDeletion() {
  std::unordered_map<TabletId, HiddenReplicationTabletInfo> hidden_tablets;
  {
    SharedLock lock(mutex_);
    if (retained_by_cdcsdk_.empty()) {
      return Status::OK();
    }
    hidden_tablets = retained_by_cdcsdk_;
  }

  std::unordered_set<TabletId> tablets_to_delete;
  std::vector<cdc::CDCStateTableKey> entries_to_delete;

  for (auto& [tablet_id, hidden_tablet] : hidden_tablets) {
    // If our parent tablet is still around, need to process that one first.
    const auto& parent_tablet_id = hidden_tablet.parent_tablet_id_;
    if (!parent_tablet_id.empty() && hidden_tablets.contains(parent_tablet_id)) {
      continue;
    }

    // For each hidden tablet, get all the streams on it and related info about the streams.
    std::unordered_set<xrepl::StreamId> stream_ids;
    HybridTime min_restart_time_across_slots;
    std::unordered_map<xrepl::StreamId, std::optional<HybridTime>> stream_creation_time_map;
    {
      SharedLock lock(mutex_);
      stream_ids = GetCDCSDKStreamsForTable(hidden_tablet.table_id_);
      min_restart_time_across_slots = VERIFY_RESULT(GetMinRestartTimeAcrossSlots(stream_ids));
      if (min_restart_time_across_slots.is_valid()) {
        stream_creation_time_map = VERIFY_RESULT(GetCDCSDKStreamCreationTimeMap(stream_ids));
      }
    }

    size_t count_tablet_streams_to_delete = 0;
    size_t count_streams_already_deleted = 0;

    for (const auto& stream_id : stream_ids) {
      // Case 1: If the entry for the tablet does not exist in cdc state table, then we can go ahead
      // with deletion of the tablet. This can happen when:
      //   a. The stream was deleted, and hence the entry was deleted.
      //   b. The entry was deleted as per the Case 3 & 4.
      auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
          {tablet_id, stream_id}, cdc::CDCStateTableEntrySelector()
                                      .IncludeCheckpoint()
                                      .IncludeLastReplicationTime()
                                      .IncludeActiveTime()));

      // This means we already deleted the entry for this stream in a previous iteration.
      if (!entry_opt) {
        VLOG(2) << "Did not find an entry corresponding to the tablet: " << tablet_id
                << ", and stream: " << stream_id << ", in the cdc_state table";
        ++count_streams_already_deleted;
        continue;
      }

      // Handling cases exclusive to scenario when the tablet is retained due to table splitting
      if (hidden_tablet.hide_reason_ == HiddenReplicationTabletInfo::HideReason::kTabletSplit) {
        // Case 2: We check if there is any stream where the CDCSDK client has started streaming
        // from the hidden tablet, if not we can delete the tablet. There are two ways to verify
        // that the client has not started streaming:
        // 1. The checkpoint is -1.-1 (which is the case when a stream is bootstrapped)
        // 2. The checkpoint is 0.0 and 'CdcLastReplicationTime' is Null (when the tablet was a
        // result of a tablet split, and was added to the cdc_state table when the tablet split is
        // initiated.)
        if (entry_opt->checkpoint) {
          auto& checkpoint = *entry_opt->checkpoint;

          if (checkpoint == OpId::Invalid() ||
              (checkpoint == OpId::Min() && !entry_opt->last_replication_time)) {
            VLOG(2) << "The stream: " << stream_id << ", is not active for tablet: " << tablet_id;
            count_tablet_streams_to_delete++;
            continue;
          }
        }

        // Case 3: This means there was an active stream for the source tablet. In which case if we
        // see that all children tablet entries have started streaming, we can delete the parent
        // tablet.
        bool found_all_children = true;
        for (auto& child_tablet_id : hidden_tablet.split_tablets_) {
          auto entry_opt = VERIFY_RESULT(cdc_state_table_->TryFetchEntry(
              {child_tablet_id, stream_id},
              cdc::CDCStateTableEntrySelector().IncludeLastReplicationTime()));

          // Check CdcLastReplicationTime to ensure that there has been a poll for this tablet, or
          // if the split has been reported.
          if (!entry_opt || !entry_opt->last_replication_time) {
            VLOG(2) << "The stream: " << stream_id
                    << ", has not started polling for the child tablet: " << child_tablet_id
                    << ". Hence we will not delete the hidden parent tablet: " << tablet_id;
            found_all_children = false;
            break;
          }
        }
        if (found_all_children) {
          LOG(INFO) << "Deleting tablet " << tablet_id << " from stream " << stream_id
                    << ". Reason: Consumer finished processing parent tablet after split.";

          // Also delete the parent tablet from cdc_state for all completed streams.
          entries_to_delete.emplace_back(tablet_id, stream_id);
          count_tablet_streams_to_delete++;
          continue;
        }
      }

      // Case 4: This is the case where the tablet is not being polled by the replication slot
      // corresponding to the stream id. We can delete the hidden tablet if:
      //   a. The min_restart_time_across_slots is greater than the hide time of the hidden tablet.
      //   b. If the tablet has expired or become not of interest. This is because if a tablet
      //   has expired or become not of interest, all its barriers will be lifted, hence making it
      //   unconsumable for CDC.
      // Also delete the parent tablet entry from cdc_state table.
      DCHECK(entry_opt->active_time);
      auto last_active_time = HybridTime::FromMicros(*entry_opt->active_time);
      if (min_restart_time_across_slots.is_valid() &&
          (hidden_tablet.hide_time_ < min_restart_time_across_slots ||
           IsCDCSDKTabletExpiredOrNotOfInterest(
               last_active_time, stream_creation_time_map.at(stream_id)))) {
        VLOG(2) << "Hidden tablet " << tablet_id
                << " no longer required by any CDC stream. restart time across all slots: "
                << min_restart_time_across_slots
                << ", tablet hide time: " << hidden_tablet.hide_time_
                << ", tablet expired or became not of interest: "
                << IsCDCSDKTabletExpiredOrNotOfInterest(
                       last_active_time, stream_creation_time_map.at(stream_id));
        entries_to_delete.emplace_back(tablet_id, stream_id);
        count_tablet_streams_to_delete++;
      }
    }

    if (count_tablet_streams_to_delete + count_streams_already_deleted == stream_ids.size()) {
      tablets_to_delete.insert(tablet_id);
    }
  }

  auto s = cdc_state_table_->DeleteEntries(entries_to_delete);
  if (!s.ok()) {
    LOG(WARNING) << "Unable to flush operations to delete cdc streams: " << s;
    return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
  }

  // Delete tablets from retained_by_cdcsdk_, CleanupHiddenTablets will do the
  // actual tablet deletion.
  {
    LockGuard lock(mutex_);
    for (const auto& tablet_id : tablets_to_delete) {
      retained_by_cdcsdk_.erase(tablet_id);
    }
  }

  return Status::OK();
}

std::shared_ptr<cdc::CDCServiceProxy> CatalogManager::GetCDCServiceProxy(RemoteTabletServer* ts) {
  auto ybclient = master_->cdc_state_client_future().get();
  auto hostport = HostPortFromPB(ts->DesiredHostPort(ybclient->cloud_info()));
  DCHECK(!hostport.host().empty());

  auto cdc_service = std::make_shared<cdc::CDCServiceProxy>(&ybclient->proxy_cache(), hostport);

  return cdc_service;
}

void CatalogManager::SetCDCServiceEnabled() {
  master_->EnableCDCService();
  cdc_enabled_.store(true, std::memory_order_release);
}

Result<scoped_refptr<TableInfo>> CatalogManager::GetTableById(const TableId& table_id) const {
  return FindTableById(table_id);
}

Status CatalogManager::FillHeartbeatResponseCDC(
    const SysClusterConfigEntryPB& cluster_config,
    const TSHeartbeatRequestPB& req,
    TSHeartbeatResponsePB* resp) {
  if (cdc_enabled_.load(std::memory_order_acquire)) {
    resp->set_xcluster_enabled_on_producer(true);
  }

  if (cluster_config.has_consumer_registry()) {
    if (req.cluster_config_version() < cluster_config.version()) {
      const auto& consumer_registry = cluster_config.consumer_registry();
      resp->set_cluster_config_version(cluster_config.version());
      *resp->mutable_consumer_registry() = consumer_registry;
    }
  }

  RETURN_NOT_OK(xcluster_manager_->FillHeartbeatResponse(req, resp));

  return Status::OK();
}

bool CatalogManager::CDCSDKShouldRetainHiddenTablet(const TabletId& tablet_id) {
  SharedLock read_lock(mutex_);
  return retained_by_cdcsdk_.contains(tablet_id);
}

Result<HybridTime> CatalogManager::GetTabletHideTime(const TabletId& tablet_id) const {
  SharedLock read_lock(mutex_);
  auto hidden_tablet_info = FindOrNull(retained_by_cdcsdk_, tablet_id);
  SCHECK_FORMAT(
      hidden_tablet_info, NotFound, "Tablet_id $0 not found in retained_by_cdcsdk_", tablet_id);
  return hidden_tablet_info->hide_time_;
}

Status CatalogManager::BumpVersionAndStoreClusterConfig(
    ClusterConfigInfo* cluster_config, ClusterConfigInfo::WriteLock* l) {
  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config),
      "updating cluster config in sys-catalog"));
  l->Commit();

  xcluster_manager_->CreateXClusterSafeTimeTableAndStartService();
  return Status::OK();
}

std::unordered_set<xrepl::StreamId> CatalogManager::GetAllXReplStreamIds() const {
  SharedLock l(mutex_);
  std::unordered_set<xrepl::StreamId> result;
  for (const auto& [stream_id, _] : cdc_stream_map_) {
    result.insert(stream_id);
  }

  return result;
}

scoped_refptr<UniverseReplicationInfo> CatalogManager::GetUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id) {
  SharedLock lock(mutex_);
  TRACE("Acquired catalog manager lock");
  return FindPtrOrNull(universe_replication_map_, replication_group_id);
}

std::vector<scoped_refptr<UniverseReplicationInfo>> CatalogManager::GetAllUniverseReplications()
    const {
  std::vector<scoped_refptr<UniverseReplicationInfo>> result;
  SharedLock lock(mutex_);
  for (const auto& [_, universe] : universe_replication_map_) {
    result.emplace_back(universe);
  }
  return result;
}

void CatalogManager::MarkUniverseForCleanup(
    const xcluster::ReplicationGroupId& replication_group_id) {
  LockGuard lock(mutex_);
  universes_to_clear_.push_back(replication_group_id);
}

Status CatalogManager::CreateCdcStateTableIfNotFound(const LeaderEpoch& epoch) {
  RETURN_NOT_OK(CreateTableIfNotFound(
      cdc::CDCStateTable::GetNamespaceName(), cdc::CDCStateTable::GetTableName(),
      &cdc::CDCStateTable::GenerateCreateCdcStateTableRequest, epoch));

  TRACE("Created CDC state table");

  // Mark the cluster as CDC enabled now that we have triggered the CDC state table creation.
  SetCDCServiceEnabled();

  return Status::OK();
}

Result<scoped_refptr<CDCStreamInfo>> CatalogManager::InitNewXReplStream() {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  auto stream_id = GenerateNewXreplStreamId();
  auto stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
  stream->mutable_metadata()->StartMutation();

  cdc_stream_map_[stream_id] = stream;

  return stream;
}

void CatalogManager::ReleaseAbandonedXReplStream(const xrepl::StreamId& stream_id) {
  LockGuard lock(mutex_);
  TRACE("Acquired catalog manager lock");

  cdc_stream_map_.erase(stream_id);
  RecoverXreplStreamId(stream_id);
}

void CatalogManager::CDCSDKPopulateDeleteRetainerInfoForTableDrop(
    const TableInfo& table_info, TabletDeleteRetainerInfo& delete_retainer) const {
  if (IsTablePartOfCDCSDK(table_info.id(), /*require_replication_slot=*/true)) {
    LOG(INFO) << "Retaining dropped table " << table_info.id()
              << " since it has active CDCSDK logical replication streams";
    delete_retainer.active_cdcsdk = true;
  }
}

void CatalogManager::CDCSDKPopulateDeleteRetainerInfoForTabletDrop(
    const TabletInfo& tablet_info, TabletDeleteRetainerInfo& delete_retainer) const {
  // For CDCSDK , the only time we try to delete a single tablet that is part of an
  // active stream is during tablet splitting, where we need to keep the parent tablet around until
  // we have replicated its SPLIT_OP record.
  {
    auto tablet_lock = tablet_info.LockForRead();
    if (tablet_lock->pb.split_tablet_ids_size() < 2) {
      return;
    }
  }

  if (IsTablePartOfCDCSDK(tablet_info.table()->id())) {
    LOG(INFO) << "Retaining dropped tablet " << tablet_info.id()
              << " since it has active CDCSDK streams";
    delete_retainer.active_cdcsdk = true;
  }
}

Status CatalogManager::RemoveTabletEntriesInCDCState(
    const xrepl::StreamId& stream_id, const std::unordered_set<TableId>& tables_in_stream_metadata,
    const TableInfoPtr& table_to_be_removed) {
  bool is_colocated_table = table_to_be_removed->IsSecondaryTable();
  auto tablets = VERIFY_RESULT(table_to_be_removed->GetTabletsIncludeInactive());
  if (tablets.empty()) {
    return Status::OK();
  }

  std::vector<cdc::CDCStateTableKey> entries_to_delete;
  if (is_colocated_table) {
    DCHECK_EQ(tablets.size(), 1);
    for (const auto& tablet : tablets) {
      if (!tablet) {
        continue;
      }

      bool should_delete_streaming_entry = true;
      for (const auto& table_id : tablet->GetTableIds()) {
        if (tables_in_stream_metadata.contains(table_id)) {
          should_delete_streaming_entry = false;
          break;
        }
      }

      if (should_delete_streaming_entry) {
        entries_to_delete.emplace_back(tablet->tablet_id(), stream_id);
        LOG_WITH_FUNC(INFO) << "Deleting cdc state table entry (tablet,stream) - "
                            << tablet->tablet_id() << ", " << stream_id;
      }

      // Deleting snapshot entries for colocated tables (containing the colocated table id).
      entries_to_delete.emplace_back(tablet->tablet_id(), stream_id, table_to_be_removed->id());
      LOG_WITH_FUNC(INFO) << "Deleting cdc state table entry (tablet, stream, table) - "
                          << tablet->tablet_id() << ", " << stream_id << ", "
                          << table_to_be_removed->id();
    }
  } else {
    for (const auto& tablet : tablets) {
      if (!tablet) {
        continue;
      }
      entries_to_delete.emplace_back(tablet->tablet_id(), stream_id);
      LOG_WITH_FUNC(INFO) << "Deleting cdc state table entry (tablet,stream) - "
                          << tablet->tablet_id() << ", " << stream_id;
    }
  }

  if (!entries_to_delete.empty()) {
    LOG_WITH_FUNC(INFO) << "Deleting " << entries_to_delete.size()
                        << " cdc state entries for CDC stream: " << stream_id;
    RETURN_NOT_OK_PREPEND(
        cdc_state_table_->DeleteEntries(entries_to_delete),
        "Error deleting cdc state entries from cdc_state table");
  }

  return Status::OK();
}

Result<std::vector<cdc::CDCStateTableKey>> CatalogManager::SyncCDCStateTableEntries(
    const xrepl::StreamId& stream_id,
    const std::unordered_set<TableId>& tables_in_stream_metadata) {
  // Scan all the rows of state table and get the TabletInfo for each of them.
  Status iteration_status;
  auto all_entry_keys =
      VERIFY_RESULT(cdc_state_table_->GetTableRange({} /* just key columns */, &iteration_status));
  std::vector<cdc::CDCStateTableKey> entries_to_delete;
  // Get all the tablet, stream pairs from cdc_state for the given stream.
  std::vector<TabletId> cdc_state_tablet_entries;
  for (const auto& entry_result : all_entry_keys) {
    RETURN_NOT_OK(entry_result);
    const auto& entry = *entry_result;

    if (entry.key.stream_id == stream_id) {
      if (entry.key.colocated_table_id.empty()) {
        cdc_state_tablet_entries.push_back(entry.key.tablet_id);
      } else {
        // If the colocated table is not present in the stream metadata, we can delete the entry.
        if (!tables_in_stream_metadata.contains(entry.key.colocated_table_id)) {
          entries_to_delete.emplace_back(entry.key);
          LOG_WITH_FUNC(INFO) << "Deleting cdc state table entry (tablet, stream, table) - "
                              << entry.key.tablet_id << ", " << entry.key.stream_id << ", "
                              << entry.key.colocated_table_id;
        }
      }
    }
  }
  RETURN_NOT_OK(iteration_status);

  // Get the tablet info for state table entries of the stream.
  auto tablet_infos = GetTabletInfos(cdc_state_tablet_entries);

  for (const auto& tablet_info : tablet_infos) {
    // If the TabletInfo is not found for tablet_id of a particular state table entry, it means the
    // physical tablet has been deleted. Ignore this entry.
    if (!tablet_info) {
      continue;
    }

    bool should_delete_entry = true;
    // The state table entry can only be deleted if it belongs to none of the tables present in
    // stream metadata.
    for (const auto& table_id : tablet_info->GetTableIds()) {
      if (tables_in_stream_metadata.contains(table_id)) {
        should_delete_entry = false;
        break;
      }
    }

    if (should_delete_entry) {
      entries_to_delete.emplace_back(tablet_info->tablet_id(), stream_id);
      LOG_WITH_FUNC(INFO) << "Deleting cdc state table entry (tablet,stream) - "
                          << tablet_info->tablet_id() << ", " << stream_id;
    }
  }

  if (!entries_to_delete.empty()) {
    LOG_WITH_FUNC(INFO) << "Deleting " << entries_to_delete.size()
                        << " cdc state entries for CDC stream: " << stream_id;
    RETURN_NOT_OK_PREPEND(
        cdc_state_table_->DeleteEntries(entries_to_delete),
        "Error deleting cdc state entries from cdc_state table");
  }

  return entries_to_delete;
}

Status CatalogManager::RemoveTableFromCDCStreamMetadataAndMaps(
    const CDCStreamInfoPtr stream, const TableId table_id, const LeaderEpoch& epoch) {
  // Remove the table from the CDC stream metadata & cdcsdk_tables_to_stream_map_ and persist
  // the updated metadata.
  {
    auto ltm = stream->LockForWrite();
    bool need_to_update_stream = false;

    auto table_id_iter = std::find(ltm->table_id().begin(), ltm->table_id().end(), table_id);
    if (table_id_iter != ltm->table_id().end()) {
      need_to_update_stream = true;
      ltm.mutable_data()->pb.mutable_table_id()->erase(table_id_iter);
    }

    if (need_to_update_stream) {
      LOG_WITH_FUNC(INFO) << "Removing table " << table_id
                          << " from qualified table list of CDC stream " << stream->id();
      RETURN_ACTION_NOT_OK(
          sys_catalog_->Upsert(epoch, stream), "Updating CDC streams in system catalog");
    }

    ltm.Commit();

    if (need_to_update_stream) {
      {
        LockGuard lock(mutex_);
        cdcsdk_tables_to_stream_map_[table_id].erase(stream->StreamId());
      }
    }
  }

  return Status::OK();
}

void CatalogManager::WarnIfTableNotPresentInAllCDCSDKStreams(const TableId& table_id) {
  std::unordered_set<xrepl::StreamId> cdc_stream_ids_for_table;
  std::unordered_set<xrepl::StreamId> all_cdc_stream_ids_in_namespace;
  std::vector<xrepl::StreamId> cdc_stream_ids_for_table_not_in_metadata;
  {
    SharedLock lock(mutex_);
    cdc_stream_ids_for_table = GetCDCSDKStreamsForTable(table_id);

    const auto table_info = GetTableInfoUnlocked(table_id);
    if (table_info && !table_info->namespace_id().empty()) {
      all_cdc_stream_ids_in_namespace = GetAllCDCSDKStreamsForNamespace(table_info->namespace_id());
    }
  }

  for (const auto& stream_id : all_cdc_stream_ids_in_namespace) {
    if (!cdc_stream_ids_for_table.contains(stream_id)) {
      cdc_stream_ids_for_table_not_in_metadata.push_back(stream_id);
    }
  }

  if (!cdc_stream_ids_for_table_not_in_metadata.empty()) {
    LOG_WITH_FUNC(INFO) << "Streams " << AsString(cdc_stream_ids_for_table_not_in_metadata)
                        << " DO NOT contain the table " << table_id << " in their metadata.";
  }
}

void CatalogManager::InsertNewUniverseReplication(UniverseReplicationInfo& replication_group) {
  LockGuard lock(mutex_);
  universe_replication_map_[replication_group.ReplicationGroupId()] =
      scoped_refptr<UniverseReplicationInfo>(&replication_group);
}

scoped_refptr<UniverseReplicationBootstrapInfo> CatalogManager::GetUniverseReplicationBootstrap(
    const xcluster::ReplicationGroupId& replication_group_id) {
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);

  return FindPtrOrNull(universe_replication_bootstrap_map_, replication_group_id);
}

void CatalogManager::InsertNewUniverseReplicationInfoBootstrapInfo(
    UniverseReplicationBootstrapInfo& bootstrap_info) {
  LockGuard lock(mutex_);
  universe_replication_bootstrap_map_[bootstrap_info.ReplicationGroupId()] =
      scoped_refptr<UniverseReplicationBootstrapInfo>(&bootstrap_info);
}

void CatalogManager::MarkReplicationBootstrapForCleanup(
    const xcluster::ReplicationGroupId& replication_group_id) {
  LockGuard lock(mutex_);
  replication_bootstraps_to_clear_.push_back(replication_group_id);
}

void CatalogManager::RemoveUniverseReplicationFromMap(
    const xcluster::ReplicationGroupId& replication_group_id) {
  LockGuard lock(mutex_);
  if (universe_replication_map_.erase(replication_group_id) < 1) {
    LOG(DFATAL) << "Replication group " << replication_group_id
                 << " was already deleted from in-mem map";
  }
}

bool CatalogManager::CDCSDKAllowTableRewrite(
    const TableId& table_id, bool is_truncate_request) const {
  // Allow rewrites on tables when:
  // - the request is truncate and FLAGS_enable_truncate_cdcsdk_table is enabled, or
  // - the FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table is enabled and the table is part of a
  // CDC logical replication stream.
  // TODO(#29877): Remove FLAGS_enable_truncate_cdcsdk_table once the
  // TEST_enable_table_rewrite_for_cdcsdk_table flag becomes a normal flag and default to true.
  return (is_truncate_request && FLAGS_enable_truncate_cdcsdk_table) ||
         (FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table &&
          IsTablePartOfCDCSDK(table_id, /*require_replication_slot=*/true));
}

Status CatalogManager::CDCSDKValidateCreateTableRequest(const CreateTableRequestPB& req) {
  // Fail rewrites on tables and nonconcurrent index backfills, that are part of CDC, except when
  // CDCSDKAllowTableRewrite() returns true.
  SharedLock lock(mutex_);
  const auto table_id = req.old_rewrite_table_id();
  if (table_id.empty() || !IsTablePartOfCDCSDK(table_id) ||
      CDCSDKAllowTableRewrite(table_id, req.is_truncate())) {
    return Status::OK();
  }

  return STATUS(
      NotSupported,
      "Table rewrite is disallowed with CDC gRPC streams, and with logical replication streams "
      "when FLAGS_TEST_enable_table_rewrite_for_cdcsdk_table is false.");
}

}  // namespace master
}  // namespace yb
