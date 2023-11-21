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

#include "yb/cdc/cdc_service.h"

#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/colocated_util.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_wire_protocol.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/bind_helpers.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"
#include "yb/master/sys_catalog-internal.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/master/ysql_tablegroup_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug-util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/thread.h"
#include "yb/util/trace.h"

#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

using std::string;
using namespace std::literals;
using std::vector;

DEFINE_RUNTIME_int32(cdc_wal_retention_time_secs, 4 * 3600,
    "WAL retention time in seconds to be used for tables for which a CDC stream was created.");

DEFINE_RUNTIME_int32(cdc_state_table_num_tablets, 0,
    "Number of tablets to use when creating the CDC state table. "
    "0 to use the same default num tablets as for regular tables.");

DEFINE_RUNTIME_bool(check_bootstrap_required, false,
    "Is it necessary to check whether bootstrap is required for Universe Replication.");

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

DEFINE_RUNTIME_int32(ns_replication_sync_retry_secs, 5,
    "Frequency at which the bg task will try to sync with producer and add tables to "
    "the current NS-level replication, when there are non-replicated consumer tables.");

DEFINE_RUNTIME_int32(ns_replication_sync_backoff_secs, 60,
    "Frequency of the add table task for a NS-level replication, when there are no "
    "non-replicated consumer tables.");

DEFINE_RUNTIME_int32(ns_replication_sync_error_backoff_secs, 300,
    "Frequency of the add table task for a NS-level replication, when there are too "
    "many consecutive errors happening for the replication.");

DEFINE_RUNTIME_bool(disable_universe_gc, false, "Whether to run the GC on universes or not.");

DEFINE_RUNTIME_int32(cdc_parent_tablet_deletion_task_retry_secs, 30,
    "Frequency at which the background task will verify parent tablets retained for xCluster or "
    "CDCSDK replication and determine if they can be cleaned up.");

DEFINE_test_flag(bool, hang_wait_replication_drain, false,
    "Used in tests to temporarily block WaitForReplicationDrain.");

DEFINE_test_flag(bool, fail_setup_system_universe_replication, false,
    "Cause the setup of system universe replication to fail.");

DEFINE_test_flag(bool, exit_unfinished_deleting, false,
    "Whether to exit part way through the deleting universe process.");

DEFINE_test_flag(bool, exit_unfinished_merging, false,
    "Whether to exit part way through the merging universe process.");

DECLARE_bool(xcluster_wait_on_ddl_alter);
DECLARE_int32(master_rpc_timeout_ms);

DEFINE_test_flag(bool, xcluster_fail_table_create_during_bootstrap, false,
    "Fail the table or index creation during xcluster bootstrap stage.");

DECLARE_bool(TEST_enable_replicate_transaction_status_table);

#define RETURN_ACTION_NOT_OK(expr, action) \
  RETURN_NOT_OK_PREPEND((expr), Format("An error occurred while $0", action))

namespace yb {
using client::internal::RemoteTabletServer;

namespace master {

////////////////////////////////////////////////////////////
// CDC Stream Loader
////////////////////////////////////////////////////////////

class CDCStreamLoader : public Visitor<PersistentCDCStreamInfo> {
 public:
  explicit CDCStreamLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  void AddDefaultValuesIfMissing(const SysCDCStreamEntryPB& metadata, CDCStreamInfo::WriteLock* l) {
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
      table = catalog_manager_->tables_->FindTableOrNull(metadata.table_id(0));
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
      catalog_manager_->xcluster_producer_tables_to_stream_map_[metadata.table_id(0)].insert(
          stream->id());
    }
    if (ns) {
      for (const auto& table_id : metadata.table_id()) {
        catalog_manager_->cdcsdk_tables_to_stream_map_[table_id].insert(stream->id());
      }
    }

    l.Commit();

    // For CDCSDK Streams, we scan all the tables in the namespace, and compare it with all the
    // tables associated with the stream.
    if (metadata.state() == SysCDCStreamEntryPB::ACTIVE && ns &&
        ns->state() == SysNamespaceEntryPB::RUNNING) {
      catalog_manager_->FindAllTablesMissingInCDCSDKStream(
          stream_id, metadata.table_id(), metadata.namespace_id());
    }

    LOG(INFO) << "Loaded metadata for CDC stream " << stream->ToString() << ": "
              << metadata.ShortDebugString();

    return Status::OK();
  }

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(CDCStreamLoader);
};

void CatalogManager::ClearXReplState() {
  // Clear CDC stream map.
  cdc_stream_map_.clear();
  xcluster_producer_tables_to_stream_map_.clear();

  // Clear CDCSDK stream map.
  cdcsdk_tables_to_stream_map_.clear();

  // Clear universe replication map.
  universe_replication_map_.clear();
  xcluster_consumer_tables_to_stream_map_.clear();
}

Status CatalogManager::LoadXReplStream() {
  LOG_WITH_FUNC(INFO) << "Loading CDC streams into memory.";
  auto cdc_stream_loader = std::make_unique<CDCStreamLoader>(this);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(cdc_stream_loader.get()),
      "Failed while visiting CDC streams in sys catalog");

  // Load retained_by_xcluster_ and retained_by_cdcsdk_ only after loading all CDC streams to reduce
  // loops.
  LoadCDCRetainedTabletsSet();

  return Status::OK();
}

void CatalogManager::LoadCDCRetainedTabletsSet() {
  for (const auto& hidden_tablet : hidden_tablets_) {
    // Only keep track of hidden tablets that have been split and that are part of a CDC stream.
    if (hidden_tablet->old_pb().split_tablet_ids_size() > 0) {
      bool is_table_cdc_producer = IsTableXClusterProducer(*hidden_tablet->table());
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
      if (l->is_deleted_or_failed() || l->pb.state() == SysUniverseReplicationEntryPB::DELETING ||
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

Status CatalogManager::CreateCdcStateTableIfNeeded(rpc::RpcContext* rpc) {
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
  schema_builder.AddColumn(master::kCdcData)
      ->Type(QLType::CreateTypeMap(DataType::STRING, DataType::STRING));
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

template <class CDCStreamInfoPointer>
std::string JoinStreamsCSVLine(std::vector<CDCStreamInfoPointer> cdc_streams) {
  std::vector<CDCStreamId> cdc_stream_ids;
  for (const auto& cdc_stream : cdc_streams) {
    cdc_stream_ids.push_back(cdc_stream->id());
  }
  return JoinCSVLine(cdc_stream_ids);
}

Status ReturnErrorOrAddWarning(
    const Status& s, bool ignore_errors, DeleteUniverseReplicationResponsePB* resp) {
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
}  // namespace

// TODO(Hari): #16971 Make this function handle all tables.
Status CatalogManager::DeleteXReplStatesForIndexTables(const vector<TableId>& table_ids) {
  return RemoveTableFromXcluster(table_ids);
}

Status CatalogManager::DeleteCDCStreamsForTables(const std::unordered_set<TableId>& table_ids) {
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

Status CatalogManager::DeleteCDCStreamsMetadataForTables(
    const std::unordered_set<TableId>& table_ids) {
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
  LockGuard lock(cdcsdk_unprocessed_table_mutex_);
  VLOG(1) << "Added table: " << table_id << ", under namesapce: " << ns_id
          << ", to namespace_to_cdcsdk_unprocessed_table_map_ to be processed by CDC streams";
  namespace_to_cdcsdk_unprocessed_table_map_[ns_id].insert(table_id);

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

Status CatalogManager::CreateCDCStream(
    const CreateCDCStreamRequestPB* req, CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
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
      return STATUS(
          InternalError, "Unable to change the WAL retention time for table", req->table_id(),
          MasterError(MasterErrorPB::INTERNAL_ERROR));
    }

    Status status = BackfillMetadataForCDC(table, rpc);
    if (!status.ok()) {
      return STATUS(
          InternalError, "Unable to backfill pgschema_name and/or pg_type_oid", req->table_id(),
          MasterError(MasterErrorPB::INTERNAL_ERROR));
    }
  }

  if (!req->has_db_stream_id()) {
    RETURN_NOT_OK(CreateNewCDCStream(*req, id_type_option_value, resp, rpc));
  } else {
    // Update and add table_id.
    RETURN_NOT_OK(AddTableIdToCDCStream(*req));
  }

  // Now that the stream is set up, mark the entire cluster as a cdc enabled.
  SetCDCServiceEnabled();

  return Status::OK();
}

Status CatalogManager::CreateNewCDCStream(
    const CreateCDCStreamRequestPB& req, const std::string& id_type_option_value,
    CreateCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  scoped_refptr<CDCStreamInfo> stream;
  {
    TRACE("Acquired catalog manager lock");
    LockGuard lock(mutex_);
    // Construct the CDC stream if the producer wasn't bootstrapped.
    CDCStreamId stream_id;
    stream_id = GenerateIdUnlocked(SysRowEntryType::CDC_STREAM);

    stream = make_scoped_refptr<CDCStreamInfo>(stream_id);
    stream->mutable_metadata()->StartMutation();
    auto* metadata = &stream->mutable_metadata()->mutable_dirty()->pb;
    bool create_namespace = id_type_option_value == cdc::kNamespaceId;
    if (create_namespace) {
      metadata->set_namespace_id(req.table_id());
    } else {
      metadata->add_table_id(req.table_id());
    }

    metadata->set_transactional(req.transactional());

    metadata->mutable_options()->CopyFrom(req.options());
    metadata->set_state(
        req.has_initial_state() ? req.initial_state() : SysCDCStreamEntryPB::ACTIVE);

    // Add the stream to the in-memory map.
    cdc_stream_map_[stream->id()] = stream;
    if (!create_namespace) {
      xcluster_producer_tables_to_stream_map_[req.table_id()].insert(stream->id());
    }
    resp->set_stream_id(stream->id());
  }
  TRACE("Inserted new CDC stream into CatalogManager maps");

  // Update the on-disk system catalog.
  RETURN_NOT_OK(CheckLeaderStatusAndSetupError(
      sys_catalog_->Upsert(leader_ready_term(), stream), "inserting CDC stream into sys-catalog",
      resp));
  TRACE("Wrote CDC stream to sys-catalog");

  // Commit the in-memory state.
  stream->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Created CDC stream " << stream->ToString();

  RETURN_NOT_OK(CreateCdcStateTableIfNeeded(rpc));
  TRACE("Created CDC state table");

  if (!PREDICT_FALSE(FLAGS_TEST_disable_cdc_state_insert_on_setup) &&
      (!req.has_initial_state() || (req.initial_state() == master::SysCDCStreamEntryPB::ACTIVE)) &&
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
    scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(req.table_id()));
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

  return Status::OK();
}

Status CatalogManager::AddTableIdToCDCStream(const CreateCDCStreamRequestPB& req) {
  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req.db_stream_id());
  }

  if (stream == nullptr) {
    return STATUS(
        NotFound, "Could not find CDC stream", req.ShortDebugString(),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }

  auto stream_lock = stream->LockForWrite();
  auto* metadata = &stream_lock.mutable_data()->pb;
  if (stream_lock->is_deleting()) {
    return STATUS(
        NotFound, "CDC stream has been deleted", req.ShortDebugString(),
        MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
  }
  metadata->add_table_id(req.table_id());

  // Transactional mode cannot be changed once set.
  DCHECK(!req.has_transactional());

  if (req.has_initial_state()) {
    metadata->set_state(req.initial_state());
  }

  // Also need to persist changes in sys catalog.
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), stream));
  stream_lock.Commit();
  TRACE("Updated CDC stream in sys-catalog");
  // Add the stream to the in-memory map.
  {
    LockGuard lock(mutex_);
    cdcsdk_tables_to_stream_map_[req.table_id()].insert(stream->id());
  }

  return Status::OK();
}

Status CatalogManager::DeleteCDCStream(
    const DeleteCDCStreamRequestPB* req, DeleteCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteCDCStream request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  if (req->stream_id_size() < 1) {
    return STATUS(
        InvalidArgument, "No CDC Stream ID given", req->ShortDebugString(),
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
            return STATUS(
                NotSupported,
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
    string missing_streams = JoinElementsIterator(
        resp->not_found_stream_ids().begin(), resp->not_found_stream_ids().end(), ",");
    return STATUS(
        NotFound,
        Format(
            "Did not find all requested CDC streams. Missing streams: [$0]. Request: $1",
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

    auto const ns_iter = namespace_to_unprocessed_table_map.find(stream_info->namespace_id());
    if (ns_iter == namespace_to_unprocessed_table_map.end()) {
      continue;
    }

    auto ltm = stream_info->LockForRead();
    if (ltm->pb.state() == SysCDCStreamEntryPB::ACTIVE) {
      for (const auto& unprocessed_table_id : ns_iter->second) {
        auto table = tables_->FindTableOrNull(unprocessed_table_id);
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
            RemoveTableFromCDCSDKUnprocessedMap(unprocessed_table_id, stream_info->namespace_id());
            VLOG(1)
                << "Table: " << unprocessed_table_id
                << ", will not be added to CDCSDK stream, since it does not have a primary key ";
            has_pk = false;
            break;
          }
        }
        if (!has_pk) {
          continue;
        }

        if (std::find(ltm->table_id().begin(), ltm->table_id().end(), unprocessed_table_id) ==
            ltm->table_id().end()) {
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
    const CDCStreamId& stream_id, const google::protobuf::RepeatedPtrField<std::string>& table_ids,
    const NamespaceId& ns_id) {
  std::unordered_set<TableId> stream_table_ids;
  // Store all table_ids associated with the stram in 'stream_table_ids'.
  for (const auto& table_id : table_ids) {
    stream_table_ids.insert(table_id);
  }

  // Get all the tables associated with the namespace.
  // If we find any table present only in the namespace, but not in the namespace, we add the table
  // id to 'cdcsdk_unprocessed_tables'.
  for (const auto& table_info : tables_->GetAllTables()) {
    {
      auto ltm = table_info->LockForRead();
      if (!ltm->visible_to_client()) {
        continue;
      }
      if (ltm->namespace_id() != ns_id) {
        continue;
      }

      bool has_pk = true;
      for (const auto& col : ltm->schema().columns()) {
        if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
          // ybrowid column is added for tables that don't have user-specified primary key.
          VLOG(1) << "Table: " << table_info->id()
                  << ", will not be added to CDCSDK stream, since it does not have a primary key";
          has_pk = false;
          break;
        }
      }
      if (!has_pk) {
        continue;
      }
    }

    if (IsMatviewTable(*table_info)) {
      continue;
    }
    if (!IsUserTableUnlocked(*table_info)) {
      continue;
    }

    if (!stream_table_ids.contains(table_info->id())) {
      LOG(INFO) << "Found unprocessed table: " << table_info->id()
                << ", for stream: " << stream_id;
      LockGuard lock(cdcsdk_unprocessed_table_mutex_);
      namespace_to_cdcsdk_unprocessed_table_map_[table_info->namespace_id()].insert(
          table_info->id());
    }
  }
}

/*
 * Processing for relevant tables that have been added after the creation of a stream
 * This involves
 *   1) Enabling the WAL retention for the tablets of the table
 *   2) INSERTING records for the tablets of this table and each stream for which
 *      this table is relevant into the cdc_state table
 */
Status CatalogManager::ProcessNewTablesForCDCSDKStreams(
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

    // Set the WAL retention for this new table
    // Make asynchronous ALTER TABLE requests to do this, just as was done during stream creation
    AlterTableRequestPB alter_table_req;
    alter_table_req.mutable_table()->set_table_id(table_id);
    alter_table_req.set_wal_retention_secs(FLAGS_cdc_wal_retention_time_secs);
    AlterTableResponsePB alter_table_resp;
    s = this->AlterTable(&alter_table_req, &alter_table_resp, nullptr);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to change the WAL retention time for table " << table_id;
      continue;
    }

    // INSERT the required cdc_state table entries
    NamespaceId namespace_id;
    bool stream_pending = false;
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
        stream_pending = true;
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
        stream_pending = true;
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

void CatalogManager::RemoveTableFromCDCSDKUnprocessedMap(
    const TableId& table_id, const NamespaceId& ns_id) {
  LockGuard lock(cdcsdk_unprocessed_table_mutex_);
  auto iter = namespace_to_cdcsdk_unprocessed_table_map_.find(ns_id);
  if (iter == namespace_to_cdcsdk_unprocessed_table_map_.end()) {
    return;
  }
  iter->second.erase(table_id);
  if (iter->second.empty()) {
    namespace_to_cdcsdk_unprocessed_table_map_.erase(ns_id);
  }
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
    const scoped_refptr<CDCStreamInfo> stream, std::set<TabletId>* tablets_with_streams,
    std::set<TableId>* dropped_tables) {
  for (const auto& table_id : stream->table_id()) {
    TabletInfos tablets;
    scoped_refptr<TableInfo> table;
    {
      TRACE("Acquired catalog manager lock");
      SharedLock lock(mutex_);
      table = tables_->FindTableOrNull(table_id);
    }
    // GetTablets locks lock_ in shared mode.
    if (table) {
      tablets = table->GetTablets(IncludeInactive::kTrue);
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

  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::StartStep1");
  // for efficient filtering of cdc_state table entries to only the list received in streams.
  std::unordered_set<CDCStreamId> stream_ids_metadata_to_be_cleaned_up;
  for(const auto& stream : streams) {
    stream_ids_metadata_to_be_cleaned_up.insert(stream->id());
  }
  // Step-1: Get entries from cdc_state table.
  std::vector<std::pair<TabletId, CDCStreamId>> cdc_state_entries;
  std::shared_ptr<client::YBSession> session = ybclient->NewSession();
  std::shared_ptr<yb::client::TableHandle> cdc_state_table = VERIFY_RESULT(GetCDCStateTable());
  client::TableIteratorOptions options;
  Status failure_status;
  options.error_handler = [&failure_status](const Status& status) {
    LOG(WARNING) << "Scan of table failed: " << status;
    failure_status = status;
  };
  options.columns = std::vector<std::string>{master::kCdcTabletId, master::kCdcStreamId};
  for (const auto& row : client::TableRange(*cdc_state_table, options)) {
    auto stream_id = row.column(master::kCdcStreamIdIdx).string_value();
    auto tablet_id = row.column(master::kCdcTabletIdIdx).string_value();
    // only add those entries that belong to the received list of streams.
    if(stream_ids_metadata_to_be_cleaned_up.contains(stream_id)) {
      cdc_state_entries.emplace_back(std::make_pair(tablet_id, stream_id));
    }
  }
  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::CompletedStep1");
  TEST_SYNC_POINT("CleanUpCDCStreamMetadata::StartStep2");
  // Step-2: Get list of tablets to keep for each stream.
  // Map of valid tablets to keep for each stream.
  std::unordered_map<CDCStreamId, std::set<TabletId>> tablets_to_keep_per_stream;
  // Map to identify the list of dropped tables for the stream.
  StreamTablesMap drop_stream_table_list;
  for (const auto& stream : streams) {
    const auto& stream_id = stream->id();
    // Get the set of all tablets not associated with the table dropped. Tablets belonging to this
    // set will not be deleted from cdc_state.
    // The second set consists of all the tables that were associated with the stream, but dropped.
    GetValidTabletsAndDroppedTablesForStream(
        stream, &tablets_to_keep_per_stream[stream_id], &drop_stream_table_list[stream_id]);
  }

  std::vector<client::YBOperationPtr> ops;
  for (const auto&[tablet_id, stream_id] : cdc_state_entries) {
    const auto tablets = FindOrNull(tablets_to_keep_per_stream, stream_id);
    RSTATUS_DCHECK(tablets, IllegalState,
      "No entry found in tablets_to_keep_per_stream map for the stream");

    if (!tablets->contains(tablet_id)) {
      // Tablet is no longer part of this stream so delete it.
      LOG(INFO) << "Deleting cdc_state table entry for stream " << stream_id
                << " for tablet " << tablet_id;
      const auto delete_op = cdc_state_table->NewDeleteOp();
      auto* const delete_req = delete_op->mutable_request();
      QLAddStringHashValue(delete_req, tablet_id);
      QLAddStringRangeValue(delete_req, stream_id);
      ops.push_back(std::move(delete_op));
    }
  }
  RETURN_NOT_OK(failure_status);
  if (ops.empty()) {
    return Status::OK();
  }

  Status s = session->TEST_ApplyAndFlush(ops);
  if (!s.ok()) {
    LOG(ERROR) << "Unable to flush operations to delete cdc streams: " << s;
    return s.CloneAndPrepend("Error deleting cdc stream rows from cdc_state table");
  }

  // Cleanup the streams from system catalog and from internal maps.
  return CleanUpCDCMetadataFromSystemCatalog(drop_stream_table_list);
}

Status CatalogManager::RemoveStreamFromXClusterProducerConfig(
    const std::vector<CDCStreamInfo*>& streams) {
  auto xcluster_config = XClusterConfig();
  auto l = xcluster_config->LockForWrite();
  auto* data = l.mutable_data();
  auto paused_producer_stream_ids =
      data->pb.mutable_xcluster_producer_registry()->mutable_paused_producer_stream_ids();
  for (const auto& stream : streams) {
    paused_producer_stream_ids->erase(stream->id());
  }
  data->pb.set_version(data->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), xcluster_config.get()),
      "updating xcluster config in sys-catalog"));
  l.Commit();
  return Status::OK();
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
        cdc_table->AddStringColumnValue(update_req, master::kCdcCheckpoint, OpId::Max().ToString());
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
                   << e.second->request().range_column_values(0).value().string_value() << ": "
                   << e.second->response().status();
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

  // Remove the stream ID from the cluster config CDC stream replication enabled/disabled map.
  RETURN_NOT_OK(RemoveStreamFromXClusterProducerConfig(streams_to_delete));

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

Status CatalogManager::GetCDCStream(
    const GetCDCStreamRequestPB* req, GetCDCStreamResponsePB* resp, rpc::RpcContext* rpc) {
  LOG(INFO) << "GetCDCStream from " << RequestorString(rpc) << ": " << req->DebugString();

  if (!req->has_stream_id()) {
    return STATUS(
        InvalidArgument, "CDC Stream ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->stream_id());
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

  stream_info->mutable_options()->CopyFrom(stream_lock->options());
  stream_info->set_transactional(stream_lock->transactional());

  if (stream_lock->pb.has_state()) {
    auto state_option = stream_info->add_options();
    state_option->set_key(cdc::kStreamState);
    state_option->set_value(SysCDCStreamEntryPB::State_Name(stream_lock->pb.state()));
  }

  return Status::OK();
}

Status CatalogManager::GetCDCDBStreamInfo(
    const GetCDCDBStreamInfoRequestPB* req, GetCDCDBStreamInfoResponsePB* resp) {
  if (!req->has_db_stream_id()) {
    return STATUS(
        InvalidArgument, "CDC DB Stream ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<CDCStreamInfo> stream;
  {
    SharedLock lock(mutex_);
    stream = FindPtrOrNull(cdc_stream_map_, req->db_stream_id());
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

  return Status::OK();
}

Status CatalogManager::ListCDCStreams(
    const ListCDCStreamsRequestPB* req, ListCDCStreamsResponsePB* resp) {
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

    if (filter_table && entry.second->table_id().size() > 0 &&
        table->id() != entry.second->table_id().Get(0)) {
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
    stream->mutable_options()->CopyFrom(ltm->options());
    // Also add an option for the current state.
    if (ltm->pb.has_state()) {
      auto state_option = stream->add_options();
      state_option->set_key(cdc::kStreamState);
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
  RSTATUS_DCHECK(
      stream_ids.size() == update_entries.size(), InvalidArgument,
      "Mismatched number of stream IDs and update entries provided.");

  // Map CDCStreamId to (CDCStreamInfo, SysCDCStreamEntryPB). CDCStreamId is sorted in
  // increasing order in the map.
  std::map<CDCStreamId, std::pair<scoped_refptr<CDCStreamInfo>, yb::master::SysCDCStreamEntryPB>>
      id_to_update_infos;
  {
    SharedLock lock(mutex_);
    for (size_t i = 0; i < stream_ids.size(); i++) {
      auto stream_id = stream_ids[i];
      auto entry = update_entries[i];
      auto stream = FindPtrOrNull(cdc_stream_map_, stream_id);
      if (stream == nullptr) {
        return STATUS(
            NotFound, "Could not find CDC stream", stream_id,
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

  std::vector<CDCStreamId> stream_ids;
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
    auto stream_id = streams_given ? req->stream_ids(t) : "";

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
          std::lock_guard<std::mutex> lock(*data_lock);
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

Result<scoped_refptr<UniverseReplicationInfo>>
CatalogManager::CreateUniverseReplicationInfoForProducer(
    const std::string& producer_id,
    const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
    const google::protobuf::RepeatedPtrField<std::string>& table_ids,
    bool transactional) {
  scoped_refptr<UniverseReplicationInfo> ri;
  {
    TRACE("Acquired catalog manager lock");
    SharedLock lock(mutex_);

    if (FindPtrOrNull(universe_replication_map_, producer_id) != nullptr) {
      return STATUS(
          InvalidArgument, "Producer already present", producer_id,
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  // Create an entry in the system catalog DocDB for this new universe replication.
  ri = new UniverseReplicationInfo(producer_id);
  ri->mutable_metadata()->StartMutation();
  SysUniverseReplicationEntryPB* metadata = &ri->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_producer_id(producer_id);
  metadata->mutable_producer_master_addresses()->CopyFrom(master_addresses);
  metadata->mutable_tables()->CopyFrom(table_ids);
  metadata->set_state(SysUniverseReplicationEntryPB::INITIALIZING);
  metadata->set_transactional(transactional);

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
 * 4. InitXClusterConsumer:          Initializes the Consumer settings to begin tailing data
 */
Status CatalogManager::SetupUniverseReplication(
    const SetupUniverseReplicationRequestPB* req,
    SetupUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupUniverseReplication from " << RequestorString(rpc) << ": "
            << req->DebugString();

  // Sanity checking section.
  if (!req->has_producer_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_master_addresses_size() <= 0) {
    return STATUS(
        InvalidArgument, "Producer master address must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->producer_bootstrap_ids().size() > 0 &&
      req->producer_bootstrap_ids().size() != req->producer_table_ids().size()) {
    return STATUS(
        InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    auto l = ClusterConfig()->LockForRead();
    if (l->pb.cluster_uuid() == req->producer_id()) {
      return STATUS(
          InvalidArgument, "The request UUID and cluster UUID are identical.",
          req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  {
    auto request_master_addresses = req->producer_master_addresses();
    std::vector<ServerEntryPB> cluster_master_addresses;
    RETURN_NOT_OK(master_->ListMasters(&cluster_master_addresses));
    for (const auto& req_elem : request_master_addresses) {
      for (const auto& cluster_elem : cluster_master_addresses) {
        if (cluster_elem.has_registration()) {
          auto p_rpc_addresses = cluster_elem.registration().private_rpc_addresses();
          for (const auto& p_rpc_elem : p_rpc_addresses) {
            if (req_elem.host() == p_rpc_elem.host() && req_elem.port() == p_rpc_elem.port()) {
              return STATUS(
                  InvalidArgument,
                  "Duplicate between request master addresses and private RPC addresses",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }

          auto broadcast_addresses = cluster_elem.registration().broadcast_addresses();
          for (const auto& bc_elem : broadcast_addresses) {
            if (req_elem.host() == bc_elem.host() && req_elem.port() == bc_elem.port()) {
              return STATUS(
                  InvalidArgument,
                  "Duplicate between request master addresses and broadcast addresses",
                  req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
            }
          }
        }
      }
    }
  }

  SetupReplicationInfo setup_info;
  setup_info.transactional = req->transactional();
  auto& table_id_to_bootstrap_id = setup_info.table_bootstrap_ids;

  if (!req->producer_bootstrap_ids().empty()) {
    if (req->producer_table_ids().size() != req->producer_bootstrap_ids_size()) {
      return STATUS(
          InvalidArgument, "Bootstrap ids must be provided for all tables", req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    table_id_to_bootstrap_id.reserve(req->producer_table_ids().size());
    for (int i = 0; i < req->producer_table_ids().size(); i++) {
      table_id_to_bootstrap_id[req->producer_table_ids(i)] = req->producer_bootstrap_ids(i);
    }
  }

  auto ri = VERIFY_RESULT(CreateUniverseReplicationInfoForProducer(
      req->producer_id(), req->producer_master_addresses(), req->producer_table_ids(),
      setup_info.transactional));

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
          Bind(
              &CatalogManager::GetColocatedTabletSchemaCallback, Unretained(this), ri->id(),
              tables_info, setup_info));
    } else if (IsTablegroupParentTableId(req->producer_table_ids(i))) {
      auto tablegroup_id = GetTablegroupIdFromParentTableId(req->producer_table_ids(i));
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = cdc_rpc->client()->GetTablegroupSchemaById(
          tablegroup_id, tables_info,
          Bind(
              &CatalogManager::GetTablegroupSchemaCallback, Unretained(this), ri->id(), tables_info,
              tablegroup_id, setup_info));
    } else {
      auto table_info = std::make_shared<client::YBTableInfo>();
      s = cdc_rpc->client()->GetTableSchemaById(
          req->producer_table_ids(i), table_info,
          Bind(
              &CatalogManager::GetTableSchemaCallback, Unretained(this), ri->id(), table_info,
              setup_info));
    }

    if (!s.ok()) {
      MarkUniverseReplicationFailed(ri, s);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
  }

  LOG(INFO) << "Started schema validation for universe replication " << ri->ToString();
  return Status::OK();
}

void CatalogManager::MarkUniverseReplicationFailed(
    scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status) {
  auto l = universe->LockForWrite();
  MarkUniverseReplicationFailed(failure_status, &l, universe);
}

void CatalogManager::MarkUniverseReplicationFailed(
    const Status& failure_status, CowWriteLock<PersistentUniverseReplicationInfo>* universe_lock,
    scoped_refptr<UniverseReplicationInfo> universe) {
  auto& l = *universe_lock;
  if (l->pb.state() == SysUniverseReplicationEntryPB::DELETED) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::DELETED_ERROR);
  } else {
    l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::FAILED);
  }

  LOG(WARNING) << "Universe replication " << universe->ToString()
               << " failed: " << failure_status.ToString();

  universe->SetSetupUniverseReplicationErrorStatus(failure_status);

  // Update sys_catalog.
  const Status s = sys_catalog_->Upsert(leader_ready_term(), universe);

  l.CommitOrWarn(s, "updating universe replication info in sys-catalog");
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
    return STATUS(
        IllegalState,
        Format(
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
    } else if (tablet_resp.has_bootstrap_required() && tablet_resp.bootstrap_required()) {
      *bootstrap_required = true;
      break;
    }
  }

  return Status::OK();
}

Status CatalogManager::AddValidatedTableToUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe,
    const TableId& producer_table,
    const TableId& consumer_table,
    const SchemaVersion& producer_schema_version,
    const SchemaVersion& consumer_schema_version,
    const ColocationSchemaVersions& colocated_schema_versions) {
  auto l = universe->LockForWrite();

  auto map = l.mutable_data()->pb.mutable_validated_tables();
  (*map)[producer_table] = consumer_table;

  SchemaVersionMappingEntryPB entry;
  if (IsColocationParentTableId(consumer_table)) {
    for (const auto& [colocation_id, producer_schema_version, consumer_schema_version] :
        colocated_schema_versions) {
      auto colocated_entry = entry.add_colocated_schema_versions();
      auto colocation_mapping = colocated_entry->mutable_schema_version_mapping();
      colocated_entry->set_colocation_id(colocation_id);
      colocation_mapping->set_producer_schema_version(producer_schema_version);
      colocation_mapping->set_consumer_schema_version(consumer_schema_version);
    }
  } else {
    auto mapping = entry.mutable_schema_version_mapping();
    mapping->set_producer_schema_version(producer_schema_version);
    mapping->set_consumer_schema_version(consumer_schema_version);
  }


  auto schema_versions_map = l.mutable_data()->pb.mutable_schema_version_mappings();
  (*schema_versions_map)[producer_table] = std::move(entry);

  // TODO: end of config validation should be where SetupUniverseReplication exits back to user
  LOG(INFO) << "UpdateItem in AddValidatedTable";

  // Update sys_catalog.
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Upsert(leader_ready_term(), universe),
      "updating universe replication info in sys-catalog");
  l.Commit();

  return Status::OK();
}

Status CatalogManager::CreateCdcStreamsIfReplicationValidated(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids) {
  auto l = universe->LockForWrite();
  if (l->is_deleted_or_failed()) {
    // Nothing to do since universe is being deleted.
    return STATUS(Aborted, "Universe is being deleted");
  }

  auto* mutable_pb = &l.mutable_data()->pb;

  if (mutable_pb->state() != SysUniverseReplicationEntryPB::INITIALIZING) {
    VLOG_WITH_FUNC(2) << "Universe replication is in invalid state " << l->pb.state();

    // Replication stream has already been validated, or is in FAILED state which cannot be
    // recovered.
    return Status::OK();
  }

  if (mutable_pb->validated_tables_size() != mutable_pb->tables_size()) {
    // Replication stream is not yet ready. All the tables have to be validated.
    return Status::OK();
  }

  auto master_addresses = mutable_pb->producer_master_addresses();
  cdc::StreamModeTransactional transactional(mutable_pb->transactional());
  auto res = universe->GetOrCreateCDCRpcTasks(master_addresses);
  if (!res.ok()) {
    MarkUniverseReplicationFailed(res.status(), &l, universe);
    return STATUS(
        InternalError,
        Format(
            "Error while setting up client for producer $0: $1", universe->id(),
            res.status().ToString()));
  }
  std::shared_ptr<CDCRpcTasks> cdc_rpc = *res;

  // Now, all tables are validated.
  vector<TableId> validated_tables;
  auto& tbl_iter = mutable_pb->tables();
  validated_tables.insert(validated_tables.begin(), tbl_iter.begin(), tbl_iter.end());

  mutable_pb->set_state(SysUniverseReplicationEntryPB::VALIDATED);
  // Update sys_catalog.
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Upsert(leader_ready_term(), universe),
      "updating universe replication info in sys-catalog");
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
        cdc_rpc->client()->GetCDCStream(
            producer_bootstrap_id, table_id, stream_options,
            std::bind(
                &CatalogManager::GetCDCStreamCallback, this, producer_bootstrap_id, table_id,
                stream_options, universe->id(), table, cdc_rpc, std::placeholders::_1,
                stream_update_infos, update_infos_lock));
      } else {
        cdc_rpc->client()->CreateCDCStream(
            table, options, transactional,
            std::bind(
                &CatalogManager::AddCDCStreamToUniverseAndInitConsumer, this, universe->id(), table,
                std::placeholders::_1, nullptr /* on_success_cb */));
      }
    }
  }
  return Status::OK();
}

Status CatalogManager::AddValidatedTableAndCreateCdcStreams(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::unordered_map<TableId, std::string>& table_bootstrap_ids,
    const TableId& producer_table,
    const TableId& consumer_table,
    const ColocationSchemaVersions& colocated_schema_versions) {
  RETURN_NOT_OK(AddValidatedTableToUniverseReplication(universe, producer_table, consumer_table,
                                                       cdc::kInvalidSchemaVersion,
                                                       cdc::kInvalidSchemaVersion,
                                                       colocated_schema_versions));
  return CreateCdcStreamsIfReplicationValidated(universe, table_bootstrap_ids);
}

void CatalogManager::GetTableSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<client::YBTableInfo>& producer_info,
    const SetupReplicationInfo& setup_info,
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

  string action = "getting schema for table";
  auto status = s;
  if (status.ok()) {
    action = "validating table schema and creating CDC stream";
    status = ValidateTableAndCreateCdcStreams(universe, producer_info, setup_info);
  }

  if (!status.ok()) {
    LOG(ERROR) << "Error " << action << ". Universe: " << universe_id
               << ", Table: " << producer_info->table_id << ": " << status;
    MarkUniverseReplicationFailed(universe, status);
  }
}

Status CatalogManager::ValidateTableAndCreateCdcStreams(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::shared_ptr<client::YBTableInfo>& producer_info,
      const SetupReplicationInfo& setup_info) {
  auto l = universe->LockForWrite();
  if (producer_info->table_name.namespace_name() == master::kSystemNamespaceName &&
      PREDICT_FALSE(FLAGS_TEST_fail_setup_system_universe_replication)) {
    auto status = STATUS(IllegalState, "Cannot replicate system tables.");
    MarkUniverseReplicationFailed(status, &l, universe);
    return status;
  }
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Upsert(leader_ready_term(), universe),
      "updating system tables in universe replication");
  l.Commit();

  GetTableSchemaResponsePB consumer_schema;
  RETURN_NOT_OK(ValidateTableSchema(producer_info, setup_info, &consumer_schema));

  // If Bootstrap Id is passed in then it must be provided for all tables.
  const auto& producer_bootstrap_ids = setup_info.table_bootstrap_ids;
  SCHECK(
      producer_bootstrap_ids.empty() || producer_bootstrap_ids.contains(producer_info->table_id),
      NotFound,
      Format("Bootstrap id not found for table $0", producer_info->table_name.ToString()));

  if (producer_info->table_name.namespace_name() != master::kSystemNamespaceName ||
      producer_bootstrap_ids.contains(producer_info->table_id)) {
    RETURN_NOT_OK(
        IsBootstrapRequiredOnProducer(universe, producer_info->table_id, producer_bootstrap_ids));
  }

  SchemaVersion producer_schema_version = producer_info->schema.version();
  SchemaVersion consumer_schema_version = consumer_schema.version();
  ColocationSchemaVersions colocated_schema_versions;
  RETURN_NOT_OK(AddValidatedTableToUniverseReplication(
      universe, producer_info->table_id, consumer_schema.identifier().table_id(),
      producer_schema_version, consumer_schema_version, colocated_schema_versions));

  return CreateCdcStreamsIfReplicationValidated(universe, producer_bootstrap_ids);
}

void CatalogManager::GetTablegroupSchemaCallback(
    const std::string& universe_id,
    const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const TablegroupId& producer_tablegroup_id,
    const SetupReplicationInfo& setup_info,
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
  ColocationSchemaVersions colocated_schema_versions;
  colocated_schema_versions.reserve(infos->size());
  for (const auto& info : *infos) {
    // Validate each of the member table in the tablegroup.
    GetTableSchemaResponsePB resp;
    Status table_status = ValidateTableSchema(
        std::make_shared<client::YBTableInfo>(info), setup_info, &resp);

    if (!table_status.ok()) {
      MarkUniverseReplicationFailed(universe, table_status);
      LOG(ERROR) << "Found error while validating table schema for table " << info.table_id << ": "
                 << table_status;
      return;
    }

    colocated_schema_versions.emplace_back(resp.schema().colocated_table_id().colocation_id(),
                                           info.schema.version(),
                                           resp.version());
    validated_consumer_tables.insert(resp.identifier().table_id());
  }

  // Get the consumer tablegroup ID. Since this call is expensive (one needs to reverse lookup
  // the tablegroup ID from table ID), we only do this call once and do validation afterward.
  TablegroupId consumer_tablegroup_id;
  // Starting Colocation GA, colocated databases create implicit underlying tablegroups.
  bool colocated_database;
  {
    SharedLock lock(mutex_);
    const auto* tablegroup = tablegroup_manager_->FindByTable(*validated_consumer_tables.begin());
    if (!tablegroup) {
      std::string message = Format(
          "No consumer tablegroup found for producer tablegroup: $0", producer_tablegroup_id);
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }
    consumer_tablegroup_id = tablegroup->id();

    scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, tablegroup->database_id());
    if (ns == nullptr) {
      std::string message =
          Format("Could not find namespace by namespace id $0", tablegroup->database_id());
      MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
      LOG(ERROR) << message;
      return;
    }
    colocated_database = ns->colocated();
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
      std::string message =
          Format("Error when getting consumer tablegroup schema: $0", consumer_tablegroup_id);
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
    for (auto it = validated_consumer_tables.begin(); it != validated_consumer_tables.end(); it++) {
      validated_tables_oss << (it == validated_consumer_tables.begin() ? "" : ",") << *it;
    }
    std::ostringstream consumer_tables_oss;
    for (auto it = tables_in_consumer_tablegroup.begin(); it != tables_in_consumer_tablegroup.end();
         it++) {
      consumer_tables_oss << (it == tables_in_consumer_tablegroup.begin() ? "" : ",") << *it;
    }

    std::string message = Format(
        "Mismatch between tables associated with producer tablegroup $0 and "
        "tables in consumer tablegroup $1: ($2) vs ($3).",
        producer_tablegroup_id, consumer_tablegroup_id, validated_tables_oss.str(),
        consumer_tables_oss.str());
    MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
    LOG(ERROR) << message;
    return;
  }

  Status status = IsBootstrapRequiredOnProducer(
      universe, producer_tablegroup_id, setup_info.table_bootstrap_ids);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table "
               << producer_tablegroup_id << ": " << status;
  }

  TableId producer_parent_table_id;
  TableId consumer_parent_table_id;
  if (colocated_database) {
    producer_parent_table_id = GetColocationParentTableId(producer_tablegroup_id);
    consumer_parent_table_id = GetColocationParentTableId(consumer_tablegroup_id);
  } else {
    producer_parent_table_id = GetTablegroupParentTableId(producer_tablegroup_id);
    consumer_parent_table_id = GetTablegroupParentTableId(consumer_tablegroup_id);
  }

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
      setup_info.table_bootstrap_ids,
      producer_parent_table_id,
      consumer_parent_table_id,
      colocated_schema_versions);
  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << producer_tablegroup_id << ": " << status;
    return;
  }
}

void CatalogManager::GetColocatedTabletSchemaCallback(
    const std::string& universe_id, const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const SetupReplicationInfo& setup_info,
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

  // Validate table schemas.
  std::unordered_set<TableId> producer_parent_table_ids;
  std::unordered_set<TableId> consumer_parent_table_ids;
  ColocationSchemaVersions colocated_schema_versions;
  colocated_schema_versions.reserve(infos->size());
  for (const auto& info : *infos) {
    // Verify that we have a colocated table.
    if (!info.colocated) {
      MarkUniverseReplicationFailed(
          universe,
          STATUS(InvalidArgument, Format("Received non-colocated table: $0", info.table_id)));
      LOG(ERROR) << "Received non-colocated table: " << info.table_id;
      return;
    }
    // Validate each table, and get the parent colocated table id for the consumer.
    GetTableSchemaResponsePB resp;
    Status table_status = ValidateTableSchema(
        std::make_shared<client::YBTableInfo>(info), setup_info, &resp);
    if (!table_status.ok()) {
      MarkUniverseReplicationFailed(universe, table_status);
      LOG(ERROR) << "Found error while validating table schema for table " << info.table_id << ": "
                 << table_status;
      return;
    }
    // Store the parent table ids.
    producer_parent_table_ids.insert(GetColocatedDbParentTableId(info.table_name.namespace_id()));
    consumer_parent_table_ids.insert(
        GetColocatedDbParentTableId(resp.identifier().namespace_().id()));
    colocated_schema_versions.emplace_back(
        resp.schema().colocated_table_id().colocation_id(), info.schema.version(), resp.version());
  }

  // Verify that we only found one producer and one consumer colocated parent table id.
  if (producer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = producer_parent_table_ids.begin(); it != producer_parent_table_ids.end(); ++it) {
      oss << ((it == producer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(
        universe, STATUS(
                      InvalidArgument,
                      Format(
                          "Found incorrect number of producer colocated parent table ids. "
                          "Expected 1, but found: [ $0 ]",
                          oss.str())));
    LOG(ERROR) << "Found incorrect number of producer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
  }
  if (consumer_parent_table_ids.size() != 1) {
    std::ostringstream oss;
    for (auto it = consumer_parent_table_ids.begin(); it != consumer_parent_table_ids.end(); ++it) {
      oss << ((it == consumer_parent_table_ids.begin()) ? "" : ", ") << *it;
    }
    MarkUniverseReplicationFailed(
        universe, STATUS(
                      InvalidArgument,
                      Format(
                          "Found incorrect number of consumer colocated parent table ids. "
                          "Expected 1, but found: [ $0 ]",
                          oss.str())));
    LOG(ERROR) << "Found incorrect number of consumer colocated parent table ids. "
               << "Expected 1, but found: [ " << oss.str() << " ]";
    return;
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

  Status status = IsBootstrapRequiredOnProducer(
      universe, *producer_parent_table_ids.begin(), setup_info.table_bootstrap_ids);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table "
               << *producer_parent_table_ids.begin() << ": " << status;
  }

  status = AddValidatedTableAndCreateCdcStreams(
      universe,
      setup_info.table_bootstrap_ids,
      *producer_parent_table_ids.begin(),
      *consumer_parent_table_ids.begin(),
      colocated_schema_versions);

  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << *producer_parent_table_ids.begin() << ": " << status;
    return;
  }
}

void CatalogManager::GetCDCStreamCallback(
    const CDCStreamId& bootstrap_id,
    std::shared_ptr<TableId>
        table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>>
        options,
    const std::string& universe_id,
    const TableId& table,
    std::shared_ptr<CDCRpcTasks>
        cdc_rpc,
    const Status& s,
    std::shared_ptr<StreamUpdateInfos>
        stream_update_infos,
    std::shared_ptr<std::mutex>
        update_infos_lock) {
  if (!s.ok()) {
    LOG(ERROR) << "Unable to find bootstrap id " << bootstrap_id;
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, s);
    return;
  }

  if (*table_id != table) {
    const Status invalid_bootstrap_id_status = STATUS_FORMAT(
        InvalidArgument, "Invalid bootstrap id for table $0. Bootstrap id $1 belongs to table $2",
        table, bootstrap_id, *table_id);
    LOG(ERROR) << invalid_bootstrap_id_status;
    AddCDCStreamToUniverseAndInitConsumer(universe_id, table, invalid_bootstrap_id_status);
    return;
  }

  scoped_refptr<UniverseReplicationInfo> original_universe;
  {
    SharedLock lock(mutex_);
    original_universe = FindPtrOrNull(
        universe_replication_map_, cdc::GetOriginalReplicationUniverseId(universe_id));
  }

  if (original_universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << universe_id;
    return;
  }

  cdc::StreamModeTransactional transactional(original_universe->LockForRead()->pb.transactional());

  // todo check options
  {
    std::lock_guard<std::mutex> lock(*update_infos_lock);
    stream_update_infos->push_back({bootstrap_id, *table_id, *options});
  }

  AddCDCStreamToUniverseAndInitConsumer(universe_id, table, bootstrap_id, [&]() {
    // Extra callback on universe setup success - update the producer to let it know that
    // the bootstrapping is complete. This callback will only be called once among all
    // the GetCDCStreamCallback calls, and we update all streams in batch at once.
    std::lock_guard<std::mutex> lock(*update_infos_lock);

    std::vector<CDCStreamId> update_bootstrap_ids;
    std::vector<SysCDCStreamEntryPB> update_entries;

    for (const auto& [update_bootstrap_id, update_table_id, update_options] :
         *stream_update_infos) {
      SysCDCStreamEntryPB new_entry;
      new_entry.add_table_id(update_table_id);
      new_entry.mutable_options()->Reserve(narrow_cast<int>(update_options.size()));
      for (const auto& [key, value] : update_options) {
        if (key == cdc::kStreamState) {
          // We will set state explicitly.
          continue;
        }
        auto new_option = new_entry.add_options();
        new_option->set_key(key);
        new_option->set_value(value);
      }
      new_entry.set_state(master::SysCDCStreamEntryPB::ACTIVE);
      new_entry.set_transactional(transactional);

      update_bootstrap_ids.push_back(update_bootstrap_id);
      update_entries.push_back(new_entry);
    }
    WARN_NOT_OK(
        cdc_rpc->client()->UpdateCDCStream(update_bootstrap_ids, update_entries),
        "Unable to update CDC stream options");
    stream_update_infos->clear();
  });
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
        Status s = InitXClusterConsumer(
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
    const TableId& consumer_table_id, const SplitTabletIds& split_tablet_ids) {
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

  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "Updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::UpdateCDCProducerOnTabletSplit(
    const TableId& producer_table_id, const SplitTabletIds& split_tablet_ids) {
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

Status CatalogManager::InitXClusterConsumer(
    const std::vector<CDCConsumerStreamInfo>& consumer_info,
    const std::string& master_addrs,
    const std::string& producer_universe_uuid,
    std::shared_ptr<CDCRpcTasks> cdc_rpc_tasks) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);
    universe = FindPtrOrNull(universe_replication_map_, producer_universe_uuid);
    if (universe == nullptr) {
      return STATUS(NotFound, "Could not find CDC producer universe",
                    producer_universe_uuid, MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }
  auto universe_l = universe->LockForRead();
  auto schema_version_mappings = universe_l->pb.schema_version_mappings();

  // Get the tablets in the consumer table.
  cdc::ProducerEntryPB producer_entry;
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto* consumer_registry = l.mutable_data()->pb.mutable_consumer_registry();
  std::string transaction_status_table_id;
  auto transaction_status_table_result = GetGlobalTransactionStatusTable();
  WARN_NOT_OK(transaction_status_table_result, "Could not open transaction status table");
  if (transaction_status_table_result) {
    transaction_status_table_id = (*transaction_status_table_result)->id();
  }
  auto transactional = universe_l->pb.transactional();
  if (!cdc::IsAlterReplicationUniverseId(universe->id())) {
    consumer_registry->set_transactional(transactional);
  }
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
    auto* schema_versions = stream_entry.mutable_schema_versions();
    auto mapping = FindOrNull(schema_version_mappings, stream_info.producer_table_id);
    SCHECK(mapping, NotFound, Format("No schema mapping for $0", stream_info.producer_table_id));
    if (IsColocationParentTableId(stream_info.consumer_table_id)) {
      // Get all the child tables and add their mappings
      auto& colocated_schema_versions_pb = *stream_entry.mutable_colocated_schema_versions();
      for (const auto& colocated_entry : mapping->colocated_schema_versions()) {
        auto colocation_id = colocated_entry.colocation_id();
        colocated_schema_versions_pb[colocation_id].set_current_producer_schema_version(
            colocated_entry.schema_version_mapping().producer_schema_version());
        colocated_schema_versions_pb[colocation_id].set_current_consumer_schema_version(
            colocated_entry.schema_version_mapping().consumer_schema_version());
      }
    } else {
      schema_versions->set_current_producer_schema_version(
        mapping->schema_version_mapping().producer_schema_version());
      schema_versions->set_current_consumer_schema_version(
        mapping->schema_version_mapping().consumer_schema_version());
    }

    (*producer_entry.mutable_stream_map())[stream_info.stream_id] = std::move(stream_entry);
    if (stream_info.consumer_table_id == transaction_status_table_id) {
      RSTATUS_DCHECK(
          transactional && FLAGS_TEST_enable_replicate_transaction_status_table, InvalidArgument,
          "Transaction status table replication is not allowed.");
      // The replication group includes the transaction status table, enable consistent
      // transactions.
      consumer_registry->set_enable_replicate_transaction_status_table(true);
    }
  }

  // Log the Network topology of the Producer Cluster
  auto master_addrs_list = StringSplit(master_addrs, ',');
  producer_entry.mutable_master_addrs()->Reserve(narrow_cast<int>(master_addrs_list.size()));
  for (const auto& addr : master_addrs_list) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, 0));
    HostPortToPB(hp, producer_entry.add_master_addrs());
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

void CatalogManager::MergeUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe, std::string original_id) {
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
    auto* consumer_registry = cl.mutable_data()->pb.mutable_consumer_registry();
    auto pm = consumer_registry->mutable_producer_map();
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
      auto s = w->Mutate(
          QLWriteRequestPB::QL_STMT_UPDATE,
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

Status CatalogManager::DeleteUniverseReplication(
    const std::string& producer_id, bool ignore_errors, DeleteUniverseReplicationResponsePB* resp) {
  scoped_refptr<UniverseReplicationInfo> ri;
  {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock");

    ri = FindPtrOrNull(universe_replication_map_, producer_id);
    if (ri == nullptr) {
      return STATUS(
          NotFound, "Universe replication info does not exist", producer_id,
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
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
      auto s = cdc_rpc->client()->DeleteCDCStream(
          streams,
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
          s = STATUS(
              NotFound, "Could not find the following streams: [" + missing_streams.str() + "].");
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
  RETURN_NOT_OK(
      ReturnErrorOrAddWarning(DeleteUniverseReplicationUnlocked(ri), ignore_errors, resp));
  l.Commit();
  LOG(INFO) << "Processed delete universe replication of " << ri->ToString();

  // Run the safe time task as it may need to perform cleanups of it own
  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplication(
    const DeleteUniverseReplicationRequestPB* req,
    DeleteUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUniverseReplication request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  if (!req->has_producer_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID required", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  RETURN_NOT_OK(DeleteUniverseReplication(req->producer_id(), req->ignore_errors(), resp));
  LOG(INFO) << "Successfully completed DeleteUniverseReplication request from "
            << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::DeleteUniverseReplicationUnlocked(
    scoped_refptr<UniverseReplicationInfo> universe) {
  // Assumes that caller has locked universe.
  RETURN_ACTION_NOT_OK(
      sys_catalog_->Delete(leader_ready_term(), universe),
      Format("updating sys-catalog, universe_id: $0", universe->id()));

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

Status CatalogManager::ChangeXClusterRole(
    const ChangeXClusterRoleRequestPB* req,
    ChangeXClusterRoleResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing ChangeXClusterRole request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  auto new_role = req->role();
  // Get the current role from the cluster config
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto consumer_registry = l.mutable_data()->pb.mutable_consumer_registry();
  auto current_role = consumer_registry->role();
  if (current_role == new_role) {
    return STATUS(
        InvalidArgument, "New role must be different than existing role", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (new_role == cdc::XClusterRole::STANDBY) {
    if (!consumer_registry->transactional()) {
      return STATUS(
          InvalidArgument,
          "This replication group does not support xCluster roles. "
          "Recreate the group with the transactional flag to enable STANDBY mode",
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

  LOG(INFO) << "Successfully completed ChangeXClusterRole request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::BootstrapProducer(
    const BootstrapProducerRequestPB* req,
    BootstrapProducerResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing BootstrapProducer request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  const bool pg_database_type = req->db_type() == YQL_DATABASE_PGSQL;
  SCHECK(
      pg_database_type || req->db_type() == YQL_DATABASE_CQL, InvalidArgument,
      "Invalid database type");
  SCHECK(
      req->has_namespace_name() && !req->namespace_name().empty(), InvalidArgument,
      "No namespace specified");
  SCHECK_GT(req->table_name_size(), 0, InvalidArgument, "No tables specified");
  if (pg_database_type) {
    SCHECK_EQ(
        req->pg_schema_name_size(), req->table_name_size(), InvalidArgument,
        "Number of tables and number of pg schemas must match");
  } else {
    SCHECK_EQ(
        req->pg_schema_name_size(), 0, InvalidArgument,
        "Pg Schema does not apply to CQL databases");
  }

  cdc::BootstrapProducerRequestPB bootstrap_req;
  master::TSDescriptor* ts = nullptr;
  for (int i = 0; i < req->table_name_size(); i++) {
    string pg_schema_name = pg_database_type ? req->pg_schema_name(i) : "";
    auto table_info = GetTableInfoFromNamespaceNameAndTableName(
        req->db_type(), req->namespace_name(), req->table_name(i), pg_schema_name);
    SCHECK(
        table_info, NotFound, Format("Table $0.$1$2 not found"), req->namespace_name(),
        (pg_schema_name.empty() ? "" : pg_schema_name + "."), req->table_name(i));

    bootstrap_req.add_table_ids(table_info->id());
    resp->add_table_ids(table_info->id());

    // Pick a valid tserver to bootstrap from.
    if (!ts) {
      ts = VERIFY_RESULT(table_info->GetTablets().front()->GetLeader());
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
    const std::string& producer_id, bool is_enabled) {
  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, producer_id);
    if (universe == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC producer universe", producer_id,
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
          producer_id,
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

Status CatalogManager::SetConsumerRegistryEnabled(
    const std::string& producer_id, bool is_enabled, ClusterConfigInfo::WriteLock* l) {
  // Modify the Consumer Registry, which will fan out this info to all TServers on heartbeat.
  {
    auto producer_map = l->mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    {
      auto it = producer_map->find(producer_id);
      if (it == producer_map->end()) {
        LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << producer_id;
        return STATUS(
            NotFound, "Could not find CDC producer universe", producer_id,
            MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
      }
      (*it).second.set_disable_stream(!is_enabled);
    }
  }
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
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  if (!req->has_is_enabled()) {
    return STATUS(
        InvalidArgument, "Must explicitly set whether to enable", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  const auto is_enabled = req->is_enabled();
  // When updating the cluster config, make sure that the change to the user replication and
  // system replication commit atomically by using the same lock.
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  RETURN_NOT_OK(SetConsumerRegistryEnabled(req->producer_id(), is_enabled, &l));
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
      "updating cluster config in sys-catalog"));
  l.Commit();

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::PauseResumeXClusterProducerStreams(
    const PauseResumeXClusterProducerStreamsRequestPB* req,
    PauseResumeXClusterProducerStreamsResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing PauseXCluster request from " << RequestorString(rpc) << ".";
  SCHECK(req->has_is_paused(), InvalidArgument, "is_paused must be set in the request");
  bool paused = req->is_paused();
  string action = paused ? "Pausing" : "Resuming";
  if (req->stream_ids_size() == 0) {
    LOG(INFO) << action << " replication for all XCluster streams.";
  }

  auto xcluster_config = XClusterConfig();
  auto l = xcluster_config->LockForWrite();
  {
    SharedLock lock(mutex_);
    auto paused_producer_stream_ids = l.mutable_data()
                                          ->pb.mutable_xcluster_producer_registry()
                                          ->mutable_paused_producer_stream_ids();
    // If an empty stream_ids list is given, then pause replication for all streams. Presence in
    // paused_producer_stream_ids indicates that a stream is paused.
    if (req->stream_ids().empty()) {
      if (paused) {
        for (auto& stream : cdc_stream_map_) {
          // If the stream id is not already in paused_producer_stream_ids, then insert it into
          // paused_producer_stream_ids to pause it.
          if (!paused_producer_stream_ids->count(stream.first)) {
            paused_producer_stream_ids->insert({stream.first, true});
          }
        }
      } else {
        // Clear paused_producer_stream_ids to resume replication for all streams.
        paused_producer_stream_ids->clear();
      }
    } else {
      // Pause or resume the user-provided list of streams.
      for (const auto& stream_id : req->stream_ids()) {
        bool contains_stream_id = paused_producer_stream_ids->count(stream_id);
        bool stream_exists = cdc_stream_map_.contains(stream_id);
        SCHECK(stream_exists, NotFound, "XCluster Stream: $0 does not exists", stream_id);
        if (paused && !contains_stream_id) {
          // Insert stream id to pause replication on that stream.
          paused_producer_stream_ids->insert({stream_id, true});
        } else if (!paused && contains_stream_id) {
          // Erase stream id to resume replication on that stream.
          paused_producer_stream_ids->erase(stream_id);
        }
      }
    }
  }
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), xcluster_config.get()),
      "updating xcluster config in sys-catalog"));
  l.Commit();
  return Status::OK();
}

Status CatalogManager::AlterUniverseReplication(
    const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterUniverseReplication request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  // Sanity Checking Cluster State and Input.
  if (!req->has_producer_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Verify that there is an existing Universe config
  scoped_refptr<UniverseReplicationInfo> original_ri;
  {
    SharedLock lock(mutex_);

    original_ri = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (original_ri == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC producer universe", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
  }

  // Currently, config options are mutually exclusive to simplify transactionality.
  int config_count = (req->producer_master_addresses_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_remove_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_add_size() > 0 ? 1 : 0) +
                     (req->has_new_producer_universe_id() ? 1 : 0);
  if (config_count != 1) {
    return STATUS(
        InvalidArgument, "Only 1 Alter operation per request currently supported",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  Status s;
  if (req->producer_master_addresses_size() > 0) {
    s = UpdateProducerAddress(original_ri, req);
  } else if (req->producer_table_ids_to_remove_size() > 0) {
    s = RemoveTablesFromReplication(original_ri, req);
  } else if (req->producer_table_ids_to_add_size() > 0) {
    RETURN_NOT_OK(AddTablesToReplication(original_ri, req, resp, rpc));
  } else if (req->has_new_producer_universe_id()) {
    s = RenameUniverseReplication(original_ri, req);
  }

  if (!s.ok()) {
    return SetupError(resp->mutable_error(), s);
  }

  CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status CatalogManager::UpdateProducerAddress(
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req) {
  CHECK_GT(req->producer_master_addresses_size(), 0);

  // TODO: Verify the input. Setup an RPC Task, ListTables, ensure same.

  {
    // 1a. Persistent Config: Update the Universe Config for Master.
    auto l = universe->LockForWrite();
    l.mutable_data()->pb.mutable_producer_master_addresses()->CopyFrom(
        req->producer_master_addresses());

    // 1b. Persistent Config: Update the Consumer Registry (updates TServers)
    auto cluster_config = ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto producer_map = cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = producer_map->find(req->producer_id());
    if (it == producer_map->end()) {
      LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: " << req->producer_id();
      return STATUS(
          NotFound, "Could not find CDC producer universe", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    it->second.mutable_master_addrs()->CopyFrom(req->producer_master_addresses());
    cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);

    {
      // Need both these updates to be atomic.
      auto w = sys_catalog_->NewWriter(leader_ready_term());
      RETURN_NOT_OK(
          w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE, universe.get(), cluster_config.get()));
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->SyncWrite(w.get()),
          "Updating universe replication info and cluster config in sys-catalog"));
    }
    l.Commit();
    cl.Commit();
  }

  // 2. Memory Update: Change cdc_rpc_tasks (Master cache)
  {
    auto result = universe->GetOrCreateCDCRpcTasks(req->producer_master_addresses());
    if (!result.ok()) {
      return result.status();
    }
  }

  return Status::OK();
}

Status CatalogManager::RemoveTablesFromReplication(
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req) {
  CHECK_GT(req->producer_table_ids_to_remove_size() , 0);

  auto it = req->producer_table_ids_to_remove();
  std::set<string> table_ids_to_remove(it.begin(), it.end());
  std::set<string> consumer_table_ids_to_remove;
  // Filter out any tables that aren't in the existing replication config.
  {
    auto l = universe->LockForRead();
    auto tbl_iter = l->pb.tables();
    std::set<string> existing_tables(tbl_iter.begin(), tbl_iter.end()), filtered_list;
    set_intersection(
        table_ids_to_remove.begin(), table_ids_to_remove.end(), existing_tables.begin(),
        existing_tables.end(), std::inserter(filtered_list, filtered_list.begin()));
    filtered_list.swap(table_ids_to_remove);
  }

  vector<CDCStreamId> streams_to_remove;

  {
    auto l = universe->LockForWrite();
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
        return STATUS(
            InvalidArgument, "Removal matched no entries.", req->ShortDebugString(),
            MasterError(MasterErrorPB::INVALID_REQUEST));
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
      auto result = universe->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses());
      if (!result.ok()) {
        LOG(ERROR) << "Unable to create cdc rpc task. CDC streams won't be deleted: " << result;
        producer_status = STATUS(
            InternalError, "Cannot create cdc rpc task.", req->ShortDebugString(),
            MasterError(MasterErrorPB::INTERNAL_ERROR));
      } else {
        producer_status = (*result)->client()->DeleteCDCStream(
            streams_to_remove, true /* force_delete */, req->remove_table_ignore_errors());
        if (!producer_status.ok()) {
          std::stringstream os;
          std::copy(
              streams_to_remove.begin(), streams_to_remove.end(),
              std::ostream_iterator<CDCStreamId>(os, ", "));
          LOG(ERROR) << "Unable to delete CDC streams: " << os.str()
                     << " on producer due to error: " << producer_status
                     << ". Try setting the ignore-errors option.";
        }
      }
    }

    // Currently, due to the sys_catalog write below, atomicity cannot be guaranteed for
    // both producer and consumer deletion, and the atomicity of producer is compromised.
    RETURN_NOT_OK(producer_status);

    {
      // Need both these updates to be atomic.
      auto w = sys_catalog_->NewWriter(leader_ready_term());
      auto s = w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE, universe.get(), cluster_config.get());
      if (s.ok()) {
        s = sys_catalog_->SyncWrite(w.get());
      }
      if (!s.ok()) {
        LOG(DFATAL) << "Updating universe replication info and cluster config in sys-catalog "
                       "failed. However, the deletion of streams on the producer has been issued."
                       " Please retry the command with the ignore-errors option to make sure that"
                       " streams are deleted properly on the consumer.";
        return s;
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

  return Status::OK();
}

Status CatalogManager::AddTablesToReplication(
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp, rpc::RpcContext* rpc) {
  CHECK_GT(req->producer_table_ids_to_add_size() , 0);

  string alter_producer_id = req->producer_id() + ".ALTER";

  // If user passed in bootstrap ids, check that there is a bootstrap id for every table.
  if (req->producer_bootstrap_ids_to_add().size() > 0 &&
      req->producer_table_ids_to_add().size() != req->producer_bootstrap_ids_to_add().size()) {
    return STATUS(
        InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
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
        return STATUS(
            InvalidArgument, "Alter for CDC producer currently running", req->ShortDebugString(),
            MasterError(MasterErrorPB::INVALID_REQUEST));
      }
    }
  }

  // Map each table id to its corresponding bootstrap id.
  std::unordered_map<TableId, std::string> table_id_to_bootstrap_id;
  if (req->producer_bootstrap_ids_to_add().size() > 0) {
    for (int i = 0; i < req->producer_table_ids_to_add().size(); i++) {
      table_id_to_bootstrap_id[req->producer_table_ids_to_add(i)] =
          req->producer_bootstrap_ids_to_add(i);
    }

    // Ensure that table ids are unique. We need to do this here even though
    // the same check is performed by SetupUniverseReplication because
    // duplicate table ids can cause a bootstrap id entry in table_id_to_bootstrap_id
    // to be overwritten.
    if (table_id_to_bootstrap_id.size() !=
        implicit_cast<size_t>(req->producer_table_ids_to_add().size())) {
      return STATUS(
          InvalidArgument,
          "When providing bootstrap ids, "
          "the list of tables must be unique",
          req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  // Only add new tables.  Ignore tables that are currently being replicated.
  auto tid_iter = req->producer_table_ids_to_add();
  std::unordered_set<string> new_tables(tid_iter.begin(), tid_iter.end());
  {
    auto l = universe->LockForRead();
    for (auto t : l->pb.tables()) {
      auto pos = new_tables.find(t);
      if (pos != new_tables.end()) {
        new_tables.erase(pos);
      }
    }
  }
  if (new_tables.empty()) {
    return STATUS(
        InvalidArgument, "CDC producer already contains all requested tables",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // 1. create an ALTER table request that mirrors the original 'setup_replication'.
  master::SetupUniverseReplicationRequestPB setup_req;
  master::SetupUniverseReplicationResponsePB setup_resp;
  setup_req.set_producer_id(alter_producer_id);
  setup_req.mutable_producer_master_addresses()->CopyFrom(
      universe->LockForRead()->pb.producer_master_addresses());
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

  return Status::OK();
}

Status CatalogManager::RenameUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req) {
  CHECK(req->has_new_producer_universe_id());

  const string old_universe_replication_id = universe->id();
  const string new_producer_universe_id = req->new_producer_universe_id();
  if (old_universe_replication_id == new_producer_universe_id) {
    return STATUS(
        InvalidArgument, "Old and new replication ids must be different", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  {
    LockGuard lock(mutex_);
    auto l = universe->LockForWrite();
    scoped_refptr<UniverseReplicationInfo> new_ri;

    // Assert that new_replication_name isn't already in use.
    if (FindPtrOrNull(universe_replication_map_, new_producer_universe_id) != nullptr) {
      return STATUS(
          InvalidArgument, "New replication id is already in use", req->ShortDebugString(),
          MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    // Since the producer_id is used as the key, we need to create a new UniverseReplicationInfo.
    new_ri = new UniverseReplicationInfo(new_producer_universe_id);
    new_ri->mutable_metadata()->StartMutation();
    SysUniverseReplicationEntryPB* metadata = &new_ri->mutable_metadata()->mutable_dirty()->pb;
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
      RETURN_NOT_OK(
          w->Mutate(QLWriteRequestPB::QL_STMT_UPDATE, new_ri.get(), cluster_config.get()));
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

  return Status::OK();
}

Status CatalogManager::GetUniverseReplication(
    const GetUniverseReplicationRequestPB* req,
    GetUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUniverseReplication from " << RequestorString(rpc) << ": " << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    SharedLock lock(mutex_);

    universe = FindPtrOrNull(universe_replication_map_, req->producer_id());
    if (universe == nullptr) {
      return STATUS(
          NotFound, "Could not find CDC producer universe", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
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
  LOG(INFO) << "IsSetupUniverseReplicationDone from " << RequestorString(rpc) << ": "
            << req->DebugString();

  if (!req->has_producer_id()) {
    return STATUS(
        InvalidArgument, "Producer universe ID must be provided", req->ShortDebugString(),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }
  const auto& producer_id = req->producer_id();
  bool is_alter_request = cdc::IsAlterReplicationUniverseId(producer_id);

  GetUniverseReplicationRequestPB universe_req;
  GetUniverseReplicationResponsePB universe_resp;
  universe_req.set_producer_id(producer_id);

  auto s = GetUniverseReplication(&universe_req, &universe_resp, /* RpcContext */ nullptr);
  // If the universe was deleted, we're done.  This is normal with ALTER tmp files.
  if (s.IsNotFound()) {
    resp->set_done(true);
    if (is_alter_request) {
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
  auto terminal_state = is_alter_request ? SysUniverseReplicationEntryPB::DELETED
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
      universe = FindPtrOrNull(universe_replication_map_, producer_id);
      if (universe == nullptr) {
        StatusToPB(
            STATUS(InternalError, "Could not find CDC producer universe after having failed."),
            resp->mutable_replication_error());
        return Status::OK();
      }
    }
    if (!universe->GetSetupUniverseReplicationErrorStatus().ok()) {
      StatusToPB(
          universe->GetSetupUniverseReplicationErrorStatus(), resp->mutable_replication_error());
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
  LOG(INFO) << "UpdateConsumerOnProducerSplit from " << RequestorString(rpc) << ": "
            << req->DebugString();

  if (!req->has_producer_id()) {
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
        NotFound, "Unable to find the stream entry for universe $0, stream $1", req->producer_id(),
        req->stream_id());
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
    LOG(WARNING) << "Unable to find matching source tablet "
                 << req->producer_split_tablet_info().tablet_id() << " for universe "
                 << req->producer_id() << " stream " << req->stream_id();

    return Status::OK();
  }

  // Also bump the cluster_config_ version so that changes are propagated to tservers (and new
  // pollers are created for the new tablets).
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
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
    auto iter = std::find_if(
        xcluster_consumer_tables_to_stream_map_.begin(),
        xcluster_consumer_tables_to_stream_map_.end(),
        [&u_id, &producer_stream_id](auto& id_map) {
          auto consumer_stream_id = id_map.second.find(u_id);
          return (
              consumer_stream_id != id_map.second.end() &&
              (*consumer_stream_id).second == producer_stream_id);
        });
    SCHECK(
        iter != xcluster_consumer_tables_to_stream_map_.end(), NotFound,
        Format("Unable to find the stream id $0", stream_id));
    consumer_table_id = iter->first;

    // The destination table should be found or created by now.
    table = tables_->FindTableOrNull(consumer_table_id);
  }
  SCHECK(table, NotFound, Format("Missing table id $0", consumer_table_id));

  // Use the stream ID to find ClusterConfig entry
  auto cluster_config = ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto producer_map = l.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
  auto producer_entry = FindOrNull(*producer_map, u_id);
  SCHECK(producer_entry, NotFound, Format("Missing universe $0", u_id));
  auto stream_entry = FindOrNull(*producer_entry->mutable_stream_map(), stream_id);
  SCHECK(stream_entry, NotFound, Format("Missing universe $0, stream $1", u_id, stream_id));
  auto schema_cached = stream_entry->mutable_producer_schema();
  // Clear out any cached schema version
  schema_cached->Clear();

  cdc::SchemaVersionsPB* schema_versions_pb = nullptr;

  // TODO (#16557): Support remove_table_id() for colocated tables / tablegroups.
  if (IsColocationParentTableId(consumer_table_id) && req->colocation_id() != kColocationIdNotSet) {
    auto map = stream_entry->mutable_colocated_schema_versions();
    schema_versions_pb = &((*map)[req->colocation_id()]);
  } else {
    schema_versions_pb = stream_entry->mutable_schema_versions();
  }

  bool schema_versions_updated = false;
  SchemaVersion current_producer_schema_version =
      schema_versions_pb->current_producer_schema_version();
  SchemaVersion current_consumer_schema_version =
      schema_versions_pb->current_consumer_schema_version();
  SchemaVersion old_producer_schema_version =
      schema_versions_pb->old_producer_schema_version();
  SchemaVersion old_consumer_schema_version =
      schema_versions_pb->old_consumer_schema_version();

  // Incoming producer version is greater than anything we've seen before, update our cache.
  if (req->producer_schema_version() > current_producer_schema_version) {
    old_producer_schema_version = current_producer_schema_version;
    old_consumer_schema_version = current_consumer_schema_version;
    current_producer_schema_version = req->producer_schema_version();
    current_consumer_schema_version = req->consumer_schema_version();
    schema_versions_updated = true;
  } else if (req->producer_schema_version() < current_producer_schema_version) {
    // We are seeing an older schema version that we need to keep track of to handle old rows.
    if (req->producer_schema_version() > old_producer_schema_version) {
      old_producer_schema_version = req->producer_schema_version();
      old_consumer_schema_version = req->consumer_schema_version();
      schema_versions_updated = true;
    } else {
      // If we have already seen this producer schema version in the past, we can ignore it OR
      // We recieved an update from a different tablet, so consumer schema version should match
      // or we received a new consumer schema version than what was cached locally.
      DCHECK(req->producer_schema_version() < old_producer_schema_version ||
             req->consumer_schema_version() >= old_consumer_schema_version);
    }
  } else {
    // If we have already seen this producer schema version, then verify that the consumer schema
    // version matches what we saw from other tablets or we received a new one.
    DCHECK(req->consumer_schema_version() >= current_consumer_schema_version);
  }

  schema_versions_pb->set_current_producer_schema_version(current_producer_schema_version);
  schema_versions_pb->set_current_consumer_schema_version(current_consumer_schema_version);
  schema_versions_pb->set_old_producer_schema_version(old_producer_schema_version);
  schema_versions_pb->set_old_consumer_schema_version(old_consumer_schema_version);

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

  // Set the values for the response.
  auto resp_schema_versions = resp->mutable_schema_versions();
  resp_schema_versions->set_current_producer_schema_version(current_producer_schema_version);
  resp_schema_versions->set_current_consumer_schema_version(current_consumer_schema_version);
  resp_schema_versions->set_old_producer_schema_version(old_producer_schema_version);
  resp_schema_versions->set_old_consumer_schema_version(old_consumer_schema_version);

  LOG(INFO) << Format(
      "Updated the schema versions for table $0 with stream id $1, colocation id $2."
      "Current producer schema version:$3, current consumer schema version:$4 "
      "old producer schema version:$5, old consumer schema version:$6, universe:$7",
      req->producer_id(), req->stream_id(), req->colocation_id(),
      current_producer_schema_version,
      current_consumer_schema_version,
      old_producer_schema_version,
      old_consumer_schema_version,
      u_id);

  // Clear replication errors related to schema mismatch.
  WARN_NOT_OK(ClearReplicationErrors(
      u_id, consumer_table_id, stream_id, {REPLICATION_SCHEMA_MISMATCH}),
      "Failed to store schema mismatch replication error");
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
    return STATUS(
        NotFound, Format("Could not locate universe $0", universe_id),
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
    return STATUS(
        NotFound, Format("Could not locate universe $0", universe_id),
        MasterError(MasterErrorPB::UNKNOWN_ERROR));
  }

  for (const auto& replication_error_code : replication_error_codes) {
    universe->ClearReplicationError(consumer_table_id, stream_id, replication_error_code);
  }

  return Status::OK();
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
  std::unordered_set<CDCStreamId> filter_stream_ids(
      req->stream_ids().begin(), req->stream_ids().end());
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
    if (undrained_stream_tablet_ids.empty() || deadline - CoarseMonoClock::Now() <= timeout * 2) {
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

Status CatalogManager::SetupNSUniverseReplication(
    const SetupNSUniverseReplicationRequestPB* req,
    SetupNSUniverseReplicationResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG(INFO) << "SetupNSUniverseReplication from " << RequestorString(rpc) << ": "
            << req->DebugString();

  SCHECK(
      req->has_producer_id() && !req->producer_id().empty(), InvalidArgument,
      "Producer universe ID must be provided");
  SCHECK(
      req->has_producer_ns_name() && !req->producer_ns_name().empty(), InvalidArgument,
      "Producer universe namespace name must be provided");
  SCHECK(
      req->has_producer_ns_type(), InvalidArgument,
      "Producer universe namespace type must be provided");
  SCHECK(
      req->producer_master_addresses_size() > 0, InvalidArgument,
      "Producer master address must be provided");

  std::string ns_name = req->producer_ns_name();
  YQLDatabase ns_type = req->producer_ns_type();
  switch (ns_type) {
    case YQLDatabase::YQL_DATABASE_CQL:
      break;
    case YQLDatabase::YQL_DATABASE_PGSQL:
      return STATUS(
          InvalidArgument, "YSQL not currently supported for namespace-level replication setup");
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
    auto cdc_rpc =
        VERIFY_RESULT(CDCRpcTasks::CreateWithMasterAddrs(req->producer_id(), producer_addrs));
    producer_tables = VERIFY_RESULT(XClusterFindProducerConsumerOverlap(
        cdc_rpc, &producer_namespace, &consumer_namespace, &num_non_matched_consumer_tables));

    // TODO: Remove this check after NS-level bootstrap is implemented.
    auto bootstrap_required =
        VERIFY_RESULT(cdc_rpc->client()->IsBootstrapRequired(producer_tables));
    SCHECK(
        !bootstrap_required, IllegalState,
        Format("Producer tables under namespace $0 require bootstrapping.", ns_name));
  }
  SCHECK(
      !producer_tables.empty(), NotFound,
      Format(
          "No producer tables under namespace $0 can be set up for replication. Please make "
          "sure that there are at least one pair of (producer, consumer) table with matching "
          "name and schema in order to initialize the namespace-level replication.",
          ns_name));

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
      return STATUS(
          NotFound, "Could not find universe after SetupUniverseReplication",
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
      metadata.next_add_table_task_time =
          CoarseMonoClock::Now() +
          MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_retry_secs));
    } else {
      // Delay the sync since there are currently no non-replicated consumer tables.
      metadata.next_add_table_task_time =
          CoarseMonoClock::Now() +
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
  LOG(INFO) << "GetReplicationStatus from " << RequestorString(rpc) << ": " << req->DebugString();

  // If the 'universe_id' is given, only populate the status for the streams in that universe.
  // Otherwise, populate all the status for all streams.
  if (!req->universe_id().empty()) {
    SharedLock lock(mutex_);
    auto universe = FindPtrOrNull(universe_replication_map_, req->universe_id());
    SCHECK(universe, InvalidArgument, Format("Could not find universe $0", req->universe_id()));
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
    const UniverseReplicationInfo& universe, GetReplicationStatusResponsePB* resp) const {
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

bool CatalogManager::IsTableXClusterProducer(const TableInfo& table_info) const {
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

bool CatalogManager::IsTableXClusterConsumer(const TableInfo& table_info) const {
  SharedLock lock(mutex_);
  return IsTableXClusterConsumerUnlocked(table_info);
}

bool CatalogManager::IsTableXClusterConsumerUnlocked(const TableInfo& table_info) const {
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

bool CatalogManager::IsXClusterEnabled(const TableInfo& table_info) const {
  SharedLock lock(mutex_);
  return IsXClusterEnabledUnlocked(table_info);
}

bool CatalogManager::IsXClusterEnabledUnlocked(const TableInfo& table_info) const {
  return IsTableXClusterProducer(table_info) || IsTableXClusterConsumerUnlocked(table_info);
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

Status CatalogManager::ValidateNewSchemaWithCdc(
    const TableInfo& table_info, const Schema& consumer_schema) const {
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
    SCHECK(producer_entry, NotFound, Format("Missing universe $0", universe_id));
    auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id);
    SCHECK(stream_entry, NotFound, Format("Missing stream $0:$1", universe_id, stream_id));

    // If we are halted on a Schema update as a Consumer...
    auto& producer_schema_pb = stream_entry->producer_schema();
    if (producer_schema_pb.has_pending_schema()) {
      // Compare our new schema to the Producer's pending schema.
      Schema producer_schema;
      RETURN_NOT_OK(SchemaFromPB(producer_schema_pb.pending_schema(), &producer_schema));

      // This new schema should allow us to consume data for the Producer's next schema.
      // If we instead diverge, we will be unable to consume any more of the Producer's data.
      bool can_apply = consumer_schema.EquivalentForDataCopy(producer_schema);
      SCHECK(
          can_apply, IllegalState,
          Format(
              "New Schema not compatible with XCluster Producer Schema:\n new={$0}\n producer={$1}",
              consumer_schema.ToString(), producer_schema.ToString()));
    }
  }

  return Status::OK();
}

Status CatalogManager::ResumeCdcAfterNewSchema(
    const TableInfo& table_info, SchemaVersion consumer_schema_version) {
  if (PREDICT_FALSE(!GetAtomicFlag(&FLAGS_xcluster_wait_on_ddl_alter))) {
    return Status::OK();
  }

  // Verify that this table is consuming a stream.
  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(table_info.id());
  if (stream_infos.empty()) {
    return Status::OK();
  }

  bool found_schema = false, resuming_replication = false;

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
                  << " is now data copy compatible with Producer: " << stream_id
                  << " @ schema version " << pending_version;
        // Clear meta we use to track progress on receiving all WAL entries with old schema.
        producer_schema_pb->set_validated_schema_version(
            std::max(producer_schema_pb->validated_schema_version(), pending_version));
        producer_schema_pb->set_last_compatible_consumer_schema_version(consumer_schema_version);
        producer_schema_pb->clear_pending_schema();
        // Bump the ClusterConfig version so we'll broadcast new schema version & resume operation.
        l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);

        WARN_NOT_OK(
            ClearReplicationErrors(u_id, table_info.id(), stream_id, {REPLICATION_SCHEMA_MISMATCH}),
            "Failed to store schema mismatch replication error");
      } else {
        LOG(INFO) << "Consumer schema not compatible for data copy of next Producer schema.";
      }
    }
  }

  if (resuming_replication) {
    RETURN_NOT_OK(CheckStatus(
        sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
        "updating cluster config after Schema for CDC"));
    l.Commit();
    LOG(INFO) << "Resuming Replication on " << table_info.id() << " after Consumer ALTER.";
  } else if (!found_schema) {
    LOG(INFO) << "No pending schema change from Producer.";
  }

  return Status::OK();
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
  WARN_NOT_OK(
      FindCDCStreamsMarkedForMetadataDeletion(
          &cdcsdk_streams, SysCDCStreamEntryPB::DELETING_METADATA),
      "Failed CDC Stream Metadata Deletion");
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
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_->Upsert(leader_ready_term(), cluster_config.get()),
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
        auto row = VERIFY_RESULT(cdc::FetchOptionalCdcStreamInfo(
            &cdc_state_table, session.get(), tablet_id, stream,
            {master::kCdcLastReplicationTime, master::kCdcCheckpoint}));

        // This means we already deleted the entry for this stream in a previous iteration.
        if (!row) {
          VLOG(2) << "Did not find an entry corresponding to the tablet: " << tablet_id
                  << ", and stream: " << stream << ", in the cdc_state table";
          streams_already_deleted.push_back(stream);
          continue;
        }
        if (request_source == cdc::XCLUSTER) {
          if (row->column(0).IsNull()) {
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
          auto checkpoint_result = OpId::FromString(row->column(1).string_value());
          if (checkpoint_result.ok()) {
            OpId checkpoint = *checkpoint_result;

            if (checkpoint == OpId::Invalid() ||
                (checkpoint == OpId::Min() && row->column(0).IsNull())) {
              VLOG(2) << "The stream: " << stream << ", is not active for tablet: " << tablet_id;
              streams_to_delete.push_back(stream);
              continue;
            }
          } else {
            LOG(WARNING) << "Read invalid op id " << row->column(1).string_value()
                         << " for tablet " << tablet_id << ": " << checkpoint_result.status()
                         << "from cdc_state table.";
          }
        }

        // This means there was an active stream for the source tablet. In which case if we see
        // that all children tablet entries have started streaming, we can delete the parent
        // tablet.
        bool found_all_children = true;
        for (auto& child_tablet : hidden_tablet.split_tablets_) {
          auto row = VERIFY_RESULT(cdc::FetchOptionalCdcStreamInfo(
              &cdc_state_table, session.get(), child_tablet, stream,
              {master::kCdcLastReplicationTime}));

          if (!row) {
            // This tablet stream still hasn't been found yet.
            found_all_children = false;
            break;
          }

          const auto& last_replicated_time = row->column(0);
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

void CatalogManager::SetCDCServiceEnabled() { cdc_enabled_.store(true, std::memory_order_release); }

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

void CatalogManager::XClusterAddTableToNSReplication(string universe_id, CoarseTimePoint deadline) {
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
          metadata.next_add_table_task_time =
              CoarseMonoClock::now() +
              MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_error_backoff_secs));
        } else {
          metadata.next_add_table_task_time =
              CoarseMonoClock::now() +
              MonoDelta::FromSeconds(GetAtomicFlag(&FLAGS_ns_replication_sync_retry_secs));
        }
      } else {
        metadata.num_accumulated_errors = 0;
        metadata.next_add_table_task_time =
            CoarseMonoClock::now() +
            MonoDelta::FromSeconds(
                has_non_replicated_consumer_table
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
    LOG_WITH_FUNC(WARNING) << "Error finding producer tables to add to universe " << universe->id()
                           << " : " << task_status;
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
    LOG_WITH_FUNC(WARNING) << "Error while waiting for AlterUniverseReplication on " << universe_id
                           << " to complete: " << task_status;
    return;
  }
  LOG_WITH_FUNC(INFO) << "Tables added to namespace-level replication " << universe->id() << " : "
                      << alter_req.ShortDebugString();
}

Status CatalogManager::XClusterNSReplicationSyncWithProducer(
    scoped_refptr<UniverseReplicationInfo> universe,
    std::vector<TableId>* producer_tables_to_add,
    bool* has_non_replicated_consumer_table) {
  auto l = universe->LockForRead();
  size_t num_non_matched_consumer_tables = 0;

  // 1. Find producer tables with a name-matching consumer table.
  auto cdc_rpc = VERIFY_RESULT(universe->GetOrCreateCDCRpcTasks(l->pb.producer_master_addresses()));
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
    auto bootstrap_required =
        VERIFY_RESULT(cdc_rpc->client()->IsBootstrapRequired(*producer_tables_to_add));
    if (bootstrap_required) {
      std::ostringstream ptable_stream;
      for (const auto& ptable : *producer_tables_to_add) {
        ptable_stream << ptable << ",";
      }
      std::string ptable_str = ptable_stream.str();
      ptable_str.pop_back();  // Remove the last comma.
      return STATUS(
          IllegalState,
          Format(
              "Producer tables [$0] require bootstrapping, which is not currently "
              "supported by the namespace-level replication setup.",
              ptable_str));
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
  SCHECK(
      !producer_tables.empty(), NotFound,
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
    SCHECK(
        list_resp.tables_size() > 0, NotFound,
        Format(
            "No consumer table found under namespace $0. Error: $1",
            consumer_namespace->ShortDebugString(), error_stream.str()));
    for (const auto& table : list_resp.tables()) {
      auto table_name = Format(
          "$0.$1.$2",
          table.namespace_().name(),
          table.pgschema_name(),  // Empty for YCQL tables.
          table.name());
      consumer_tables.insert(table_name);
    }
    consumer_namespace->set_id(list_resp.tables(0).namespace_().id());
  }

  // 3. Find producer tables with a name-matching consumer table.
  std::vector<TableId> overlap_tables;
  for (const auto& table : producer_tables) {
    auto table_name = Format(
        "$0.$1.$2",
        table.namespace_name(),
        table.pgschema_name(),  // Empty for YCQL tables.
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

Status CatalogManager::WaitForSetupUniverseReplicationToFinish(
    const string& producer_uuid, CoarseTimePoint deadline) {
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
    SCHECK(
        stream_map_iter != xcluster_consumer_tables_to_stream_map_.end(), NotFound,
        Format("Could not locate stream map for table id $0", consumer_table_id));

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
            LOG(WARNING) << Format(
                "Received replication status error code ($0) that does not "
                "correspond to an enum value in ReplicationErrorPb.");
            replication_errors.emplace_back(REPLICATION_UNKNOWN_ERROR, error_detail);
          }
        }

        WARN_NOT_OK(
            StoreReplicationErrorsUnlocked(
                producer_id, consumer_table_id, tablet_stream_id, replication_errors),
            Format(
                "Failed to store replication error for universe=$0 table=$1 stream=$2", producer_id,
                consumer_table_id, tablet_stream_id));
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::FillHeartbeatResponseCDC(
    const SysClusterConfigEntryPB& cluster_config,
    const TSHeartbeatRequestPB* req,
    TSHeartbeatResponsePB* resp) {
  if (cdc_enabled_.load(std::memory_order_acquire)) {
    resp->set_xcluster_enabled_on_producer(true);
  }
  if (cluster_config.has_consumer_registry()) {
    {
      auto l = xcluster_safe_time_info_.LockForRead();
      *resp->mutable_xcluster_namespace_to_safe_time() = l->pb.safe_time_map();
    }

    if (req->cluster_config_version() < cluster_config.version()) {
      const auto& consumer_registry = cluster_config.consumer_registry();
      resp->set_cluster_config_version(cluster_config.version());
      *resp->mutable_consumer_registry() = consumer_registry;
    }
  }
  if (req->has_xcluster_config_version() &&
      req->xcluster_config_version() < VERIFY_RESULT(GetXClusterConfigVersion())) {
    auto xcluster_config = XClusterConfig();
    auto l = xcluster_config->LockForRead();
    resp->set_xcluster_config_version(l->pb.version());
    *resp->mutable_xcluster_producer_registry() = l->pb.xcluster_producer_registry();
  }

  return Status::OK();
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

bool CatalogManager::RetainedByXRepl(const TabletId& tablet_id) {
  SharedLock read_lock(mutex_);
  return retained_by_xcluster_.contains(tablet_id) || retained_by_cdcsdk_.contains(tablet_id);
}

Status CatalogManager::BumpVersionAndStoreClusterConfig(
    ClusterConfigInfo* cluster_config, ClusterConfigInfo::WriteLock* l) {
  l->mutable_data()->pb.set_version(l->mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_->Upsert(leader_ready_term(), cluster_config),
      "updating cluster config in sys-catalog"));
  l->Commit();

  CreateXClusterSafeTimeTableAndStartService();
  return Status::OK();
}

Result<bool> CatalogManager::ScheduleBootstrapForXclusterIfNeeded(
    const TableInfoPtr& table, const SysTablesEntryPB& pb) {
  if (table->GetBootstrappingXClusterReplication()) {
    return true;
  }

  // TODO(Hari): #16469 Schedule the job as part of CreateTable.
  if (!VERIFY_RESULT(ShouldAddTableToXClusterReplication(*table, pb))) {
    return false;
  }

  // Submit a async task to bootstrap the table.
  if (!table->SetBootstrappingXClusterReplication(true)) {
    LOG(INFO) << "Adding index " << table->id() << " to xCluster replication";
    auto submit_status = background_tasks_thread_pool_->SubmitFunc([this, table]() {
      auto s = AddYsqlIndexToXClusterReplication(*table);
      if (!s.ok()) {
        LOG(WARNING) << "Failed to add index " << table->id() << " to xCluster replication: " << s;
        table->SetCreateTableErrorStatus(s);
      }
      table->SetBootstrappingXClusterReplication(false);
    });
    if (!submit_status.ok()) {
      table->SetBootstrappingXClusterReplication(false);
      LOG(WARNING) << "Failed to submit task to add index to xCluster replication: "
                   << submit_status;
      return submit_status;
    }
  }
  return true;
}

Result<bool> CatalogManager::ShouldAddTableToXClusterReplication(
    const TableInfo& table, const SysTablesEntryPB& pb) {
  // Only user created YSQL Indexes should be automatically added to xCluster replication.
  if (pb.colocated() || pb.table_type() != PGSQL_TABLE_TYPE || !IsIndex(pb) ||
      !IsUserCreatedTable(table)) {
    return false;
  }

  XClusterConsumerTableStreamInfoMap index_stream_infos =
      GetXClusterStreamInfoForConsumerTable(table.id());
  if (!index_stream_infos.empty()) {
    VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
            << yb::ToString(index_stream_infos);
    return false;
  }

  auto indexed_table = GetTableInfo(GetIndexedTableId(table.LockForRead()->pb));
  SCHECK(indexed_table, NotFound, "Indexed table not found");

  XClusterConsumerTableStreamInfoMap stream_infos =
      GetXClusterStreamInfoForConsumerTable(indexed_table->id());
  if (stream_infos.empty()) {
    return false;
  }

  if (stream_infos.size() > 1) {
    LOG(WARNING) << "Skip adding index " << table.ToString()
                 << " to xCluster replication as the base table" << indexed_table->ToString()
                 << " is part of multiple replication streams " << yb::ToString(stream_infos);
    return false;
  }

  const string& universe_id = stream_infos.begin()->first;
  auto cluster_config = ClusterConfig();
  {
    auto l = cluster_config->LockForRead();
    auto consumer_registry = l.data().pb.consumer_registry();
    // Only add if we are in a transactional replication with STANDBY mode.
    if (consumer_registry.role() != cdc::XClusterRole::STANDBY ||
        !consumer_registry.transactional()) {
      return false;
    }

    auto producer_entry = FindOrNull(consumer_registry.producer_map(), universe_id);
    if (producer_entry) {
      // Check if the table is already part of replication.
      // This is needed despite the check for GetXClusterStreamInfoForConsumerTable as the in-memory
      // list is not atomically updated.
      for (auto& stream_info : producer_entry->stream_map()) {
        if (stream_info.second.consumer_table_id() == table.id()) {
          VLOG(1) << "Index " << table.ToString() << " is already part of xcluster replication "
                  << stream_info.first;
          return false;
        }
      }
    }
  }

  scoped_refptr<UniverseReplicationInfo> universe;
  {
    TRACE("Acquired catalog manager lock");
    SharedLock lock(mutex_);
    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    if (universe == nullptr) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << universe_id
                   << " was not found";
      return false;
    }

    if (universe->LockForRead()->is_deleted_or_failed()) {
      LOG(WARNING) << "Skip adding index " << table.ToString()
                   << " to xCluster replication as the universe " << universe_id
                   << " is in a deleted or failed state";
      return false;
    }
  }

  return true;
}

Status CatalogManager::AddYsqlIndexToXClusterReplication(const TableInfo& index_info) {
  {
    TRACE("Locking table");
    auto l = index_info.LockForRead();
    RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l));
  }

  XClusterConsumerTableStreamInfoMap stream_ids =
      GetXClusterStreamInfoForConsumerTable(index_info.id());
  if (!stream_ids.empty()) {
    LOG(INFO) << "Index " << index_info.ToString() << " is already part of xcluster replication "
              << yb::ToString(stream_ids);
    return Status::OK();
  }

  auto bootstrap_time = VERIFY_RESULT(BootstrapAndAddIndexToXClusterReplication(index_info));

  VLOG_WITH_PREFIX(1) << "Waiting for xcluster safe time of namespace " << index_info.namespace_id()
                      << " to get past bootstrap_time " << bootstrap_time;
  RETURN_NOT_OK(WaitForAllXClusterConsumerTablesToCatchUpToSafeTime(
      index_info.namespace_id(), bootstrap_time));

  LOG(INFO) << "Index " << index_info.ToString()
            << " successfully added to xcluster universe replication";

  return Status::OK();
}

Result<HybridTime> CatalogManager::BootstrapAndAddIndexToXClusterReplication(
    const TableInfo& index_info) {
  const auto indexed_table_id = GetIndexedTableId(index_info.LockForRead()->pb);
  SCHECK(!indexed_table_id.empty(), IllegalState, "Indexed table id is empty");
  SCHECK(
      !FLAGS_TEST_xcluster_fail_table_create_during_bootstrap, IllegalState,
      "FLAGS_TEST_xcluster_fail_table_create_during_bootstrap");

  XClusterConsumerTableStreamInfoMap indexed_table_stream_ids =
      GetXClusterStreamInfoForConsumerTable(indexed_table_id);

  SCHECK_EQ(
      indexed_table_stream_ids.size(), 1, IllegalState,
      Format("Expected table $0 to be part of only one replication", indexed_table_id));

  const string& universe_id = indexed_table_stream_ids.begin()->first;

  scoped_refptr<UniverseReplicationInfo> universe;
  google::protobuf::RepeatedPtrField<HostPortPB> master_addresses;
  {
    TRACE("Acquired catalog manager lock");
    SharedLock lock(mutex_);
    universe = FindPtrOrNull(universe_replication_map_, universe_id);
    SCHECK(universe != nullptr, NotFound, Format("Universe $0 not found", universe_id));
    master_addresses = universe->LockForRead()->pb.producer_master_addresses();
  }

  auto cdc_rpc = VERIFY_RESULT(universe->GetOrCreateCDCRpcTasks(master_addresses));

  std::vector<string> producer_table_ids;
  std::vector<string> bootstrap_ids;
  HybridTime bootstrap_time;
  std::vector<PgSchemaName> pg_schema_names;
  if (!index_info.pgschema_name().empty()) {
    pg_schema_names.emplace_back(index_info.pgschema_name());
  }
  RETURN_NOT_OK(cdc_rpc->client()->BootstrapProducer(
      YQLDatabase::YQL_DATABASE_PGSQL, index_info.namespace_name(), pg_schema_names,
      {index_info.name()}, &producer_table_ids, &bootstrap_ids, &bootstrap_time));
  CHECK_EQ(producer_table_ids.size(), 1);
  CHECK_EQ(bootstrap_ids.size(), 1);
  SCHECK(!bootstrap_time.is_special(), IllegalState, "Failed to get a valid bootstrap time");

  auto& producer_table_id = producer_table_ids[0];
  auto& bootstrap_id = bootstrap_ids[0];

  LOG(INFO) << "Adding index " << index_info.ToString() << " to xcluster universe replication "
            << universe_id << " with bootstrap_id " << bootstrap_id << " and bootstrap_time "
            << bootstrap_time;
  AlterUniverseReplicationRequestPB alter_universe_req;
  AlterUniverseReplicationResponsePB alter_universe_resp;
  alter_universe_req.set_producer_id(universe_id);
  alter_universe_req.add_producer_table_ids_to_add(producer_table_id);
  alter_universe_req.add_producer_bootstrap_ids_to_add(bootstrap_id);
  RETURN_NOT_OK(
      AlterUniverseReplication(&alter_universe_req, &alter_universe_resp, nullptr /* rpc */));

  if (alter_universe_resp.has_error()) {
    return StatusFromPB(alter_universe_resp.error().status());
  }

  RETURN_NOT_OK(WaitForSetupUniverseReplicationToFinish(universe_id, CoarseTimePoint::max()));

  return bootstrap_time;
}

Status CatalogManager::WaitForAllXClusterConsumerTablesToCatchUpToSafeTime(
    const NamespaceId& namespace_id, const HybridTime& min_safe_time) {
  CHECK(min_safe_time.is_valid());
  // Force a refresh of the xCluster safe time map so that it accounts for all tables under
  // replication.
  auto initial_safe_time =
      VERIFY_RESULT(xcluster_safe_time_service_->RefreshAndGetXClusterNamespaceToSafeTimeMap());
  if (!initial_safe_time.contains(namespace_id)) {
    // Namespace is not part of any xCluster replication.
    return Status::OK();
  }

  auto initial_ht = initial_safe_time[namespace_id];
  initial_ht.MakeAtLeast(min_safe_time);

  // Wait for the xCluster safe time to advance beyond the initial value. This ensures all tables
  // under replication are part of the safe time computation.
  return WaitFor(
      [this, &namespace_id, &initial_ht]() -> Result<bool> {
        const auto ht = VERIFY_RESULT(GetXClusterSafeTime(namespace_id));
        if (ht > initial_ht) {
          return true;
        }
        YB_LOG_EVERY_N_SECS(WARNING, 10)
            << "Waiting for xCluster safe time" << ht << " to advance beyond " << initial_ht;
        return false;
      },
      MonoDelta::kMax, "Waiting for xCluster safe time to get past now", 100ms);
}

Status CatalogManager::RemoveTableFromXcluster(const vector<TabletId>& table_ids) {
  std::map<std::string, std::unordered_set<TableId>> universe_tables_map;
  {
    auto cluster_config = ClusterConfig();
    auto l = cluster_config->LockForRead();
    for (auto& table_id : table_ids) {
      SharedLock lock(mutex_);
      auto it = xcluster_consumer_tables_to_stream_map_.find(table_id);
      if (it == xcluster_consumer_tables_to_stream_map_.end()) {
        continue;
      }
      auto& stream_ids = it->second;

      auto stream_info = stream_ids.begin();
      const string& universe_id = stream_info->first;
      const CDCStreamId& stream_id = stream_info->second;
      // Fetch the stream entry so we can update the mappings.
      auto producer_map = l.data().pb.consumer_registry().producer_map();
      auto producer_entry = FindOrNull(producer_map, universe_id);
      // If we can't find the entries, then the stream has been deleted.
      if (!producer_entry) {
        LOG(WARNING) << "Unable to find the producer entry for universe " << universe_id;
        continue;
      }
      auto stream_entry = FindOrNull(producer_entry->stream_map(), stream_id);
      if (!stream_entry) {
        LOG(WARNING) << "Unable to find the producer entry for universe " << universe_id
                     << ", stream " << stream_id;
        continue;
      }
      universe_tables_map[universe_id].insert(stream_entry->producer_table_id());
    }
  }

  for (auto& [universe_id, producer_tables] : universe_tables_map) {
    LOG(INFO) << "Removing tables " << yb::ToString(table_ids) << " from xcluster replication "
              << universe_id;
    AlterUniverseReplicationRequestPB alter_universe_req;
    AlterUniverseReplicationResponsePB alter_universe_resp;
    alter_universe_req.set_producer_id(universe_id);
    alter_universe_req.set_remove_table_ignore_errors(true);
    for (auto& table_id : producer_tables) {
      alter_universe_req.add_producer_table_ids_to_remove(table_id);
    }
    RETURN_NOT_OK(
        AlterUniverseReplication(&alter_universe_req, &alter_universe_resp, nullptr /* rpc */));

    if (alter_universe_resp.has_error()) {
      return StatusFromPB(alter_universe_resp.error().status());
    }
  }

  for (auto& [universe_id, producer_tables] : universe_tables_map) {
    RETURN_NOT_OK(
        WaitForSetupUniverseReplicationToFinish(universe_id, CoarseMonoClock::TimePoint::max()));
  }
  return Status::OK();
}

}  // namespace master
}  // namespace yb
