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
//

#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"

#include "yb/client/table_info.h"
#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common_net.pb.h"
#include "yb/common/xcluster_util.h"

#include "yb/gutil/bind.h"

#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_util.h"
#include "yb/master/master.h"
#include "yb/master/xcluster_consumer_registry_service.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/xcluster/master_xcluster_util.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/status.h"

DEFINE_RUNTIME_bool(check_bootstrap_required, false,
    "Is it necessary to check whether bootstrap is required for Universe Replication.");

DEFINE_test_flag(bool, allow_ycql_transactional_xcluster, false,
    "Determines if xCluster transactional replication on YCQL tables is allowed.");

DEFINE_test_flag(
    bool, fail_universe_replication_merge, false,
    "Causes MergeUniverseReplication to fail with an error.");

DEFINE_test_flag(bool, exit_unfinished_merging, false,
    "Whether to exit part way through the merging universe process.");

DECLARE_bool(enable_xcluster_auto_flag_validation);

#define RETURN_ACTION_NOT_OK(expr, action) \
  RETURN_NOT_OK_PREPEND((expr), Format("An error occurred while $0", action))

namespace yb::master {

namespace {

Status ValidateTableListForDbScopedReplication(
    UniverseReplicationInfo& universe, const std::vector<NamespaceId>& namespace_ids,
    const std::set<TableId>& replicated_tables, const CatalogManager& catalog_manager) {
  std::set<TableId> validated_tables;

  for (const auto& namespace_id : namespace_ids) {
    auto table_infos =
        VERIFY_RESULT(GetTablesEligibleForXClusterReplication(catalog_manager, namespace_id));

    std::vector<TableId> missing_tables;

    for (const auto& table_info : table_infos) {
      const auto& table_id = table_info->id();
      if (replicated_tables.contains(table_id)) {
        validated_tables.insert(table_id);
      } else {
        missing_tables.push_back(table_id);
      }
    }

    SCHECK_FORMAT(
        missing_tables.empty(), IllegalState,
        "Namespace $0 has additional tables that were not added to xCluster DB Scoped replication "
        "group $1: $2",
        namespace_id, universe.id(), yb::ToString(missing_tables));
  }

  auto diff = STLSetSymmetricDifference(replicated_tables, validated_tables);
  SCHECK_FORMAT(
      diff.empty(), IllegalState,
      "xCluster DB Scoped replication group $0 contains tables $1 that do not belong to replicated "
      "namespaces $2",
      universe.id(), yb::ToString(diff), yb::ToString(namespace_ids));

  return Status::OK();
}

// Check if the local AutoFlags config is compatible with the source universe and returns the source
// universe AutoFlags config version if they are compatible.
// If they are not compatible, returns a bad status.
// If the source universe is running an older version which does not support AutoFlags compatiblity
// check, returns an invalid AutoFlags config version.
Result<uint32> GetAutoFlagConfigVersionIfCompatible(
    UniverseReplicationInfo& replication_info, const AutoFlagsConfigPB& local_config) {
  const auto& replication_group_id = replication_info.ReplicationGroupId();

  VLOG_WITH_FUNC(2) << "Validating AutoFlags config for replication group: " << replication_group_id
                    << " with target config version: " << local_config.config_version();

  auto validate_result = VERIFY_RESULT(ValidateAutoFlagsConfig(replication_info, local_config));

  if (!validate_result) {
    VLOG_WITH_FUNC(2)
        << "Source universe of replication group " << replication_group_id
        << " is running a version that does not support the AutoFlags compatibility check yet";
    return kInvalidAutoFlagsConfigVersion;
  }

  auto& [is_valid, source_version] = *validate_result;

  SCHECK(
      is_valid, IllegalState,
      "AutoFlags between the universes are not compatible. Upgrade the target universe to a "
      "version higher than or equal to the source universe");

  return source_version;
}

}  // namespace

SetupUniverseReplicationHelper::SetupUniverseReplicationHelper(
    Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch)
    : master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(*catalog_manager.sys_catalog()),
      xcluster_manager_(*catalog_manager.GetXClusterManagerImpl()),
      epoch_(epoch) {}

SetupUniverseReplicationHelper::~SetupUniverseReplicationHelper() {}

Status SetupUniverseReplicationHelper::Setup(
    Master& master, CatalogManager& catalog_manager, const SetupUniverseReplicationRequestPB* req,
    SetupUniverseReplicationResponsePB* resp, const LeaderEpoch& epoch) {
  auto helper = scoped_refptr<SetupUniverseReplicationHelper>(
      new SetupUniverseReplicationHelper(master, catalog_manager, epoch));
  return helper->SetupUniverseReplication(req, resp);
}

void SetupUniverseReplicationHelper::MarkUniverseReplicationFailed(
    scoped_refptr<UniverseReplicationInfo> universe, const Status& failure_status) {
  auto l = universe->LockForWrite();
  MarkUniverseReplicationFailed(failure_status, &l, universe);
}

void SetupUniverseReplicationHelper::MarkUniverseReplicationFailed(
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
  const Status s = sys_catalog_.Upsert(epoch_, universe);

  l.CommitOrWarn(s, "updating universe replication info in sys-catalog");
}

/*
 * UniverseReplication is setup in 4 stages within the Catalog Manager
 * 1. SetupUniverseReplication: Validates user input & requests Producer schema.
 * 2. GetTableSchemaCallback:   Validates Schema compatibility & requests Producer xCluster init.
 * 3. AddStreamToUniverseAndInitConsumer:  Setup RPC connections for xCluster Streaming
 * 4. InitXClusterConsumer:          Initializes the Consumer settings to begin tailing data
 */
Status SetupUniverseReplicationHelper::SetupUniverseReplication(
    const SetupUniverseReplicationRequestPB* req, SetupUniverseReplicationResponsePB* resp) {
  // Sanity checking section.
  if (!req->has_replication_group_id()) {
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
    auto l = catalog_manager_.ClusterConfig()->LockForRead();
    if (l->pb.cluster_uuid() == req->replication_group_id()) {
      return STATUS(
          InvalidArgument, "The request UUID and cluster UUID are identical.",
          req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  RETURN_NOT_OK_PREPEND(
      ValidateMasterAddressesBelongToDifferentCluster(master_, req->producer_master_addresses()),
      req->ShortDebugString());

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
      table_id_to_bootstrap_id.insert_or_assign(
          req->producer_table_ids(i),
          VERIFY_RESULT(xrepl::StreamId::FromString(req->producer_bootstrap_ids(i))));
    }
  }

  SCHECK(
      req->producer_namespaces().empty() || req->transactional(), InvalidArgument,
      "Transactional flag must be set for Db scoped replication groups");

  std::vector<NamespaceId> producer_namespace_ids, consumer_namespace_ids;
  for (const auto& producer_ns_id : req->producer_namespaces()) {
    SCHECK(!producer_ns_id.id().empty(), InvalidArgument, "Invalid Namespace Id");
    SCHECK(!producer_ns_id.name().empty(), InvalidArgument, "Invalid Namespace name");
    SCHECK_EQ(
        producer_ns_id.database_type(), YQLDatabase::YQL_DATABASE_PGSQL, InvalidArgument,
        "Invalid Namespace database_type");

    producer_namespace_ids.push_back(producer_ns_id.id());

    NamespaceIdentifierPB consumer_ns_id;
    consumer_ns_id.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    consumer_ns_id.set_name(producer_ns_id.name());
    auto ns_info = VERIFY_RESULT(catalog_manager_.FindNamespace(consumer_ns_id));
    consumer_namespace_ids.push_back(ns_info->id());
  }

  // We should set the universe uuid even if we fail with AlreadyPresent error.
  {
    auto universe_uuid = catalog_manager_.GetUniverseUuidIfExists();
    if (universe_uuid) {
      resp->set_universe_uuid(universe_uuid->ToString());
    }
  }

  auto ri = VERIFY_RESULT(CreateUniverseReplicationInfo(
      xcluster::ReplicationGroupId(req->replication_group_id()), req->producer_master_addresses(),
      producer_namespace_ids, consumer_namespace_ids, req->producer_table_ids(),
      setup_info.transactional));

  // Initialize the xCluster Stream by querying the Producer server for RPC sanity checks.
  auto result = ri->GetOrCreateXClusterRpcTasks(req->producer_master_addresses());
  if (!result.ok()) {
    MarkUniverseReplicationFailed(ri, ResultToStatus(result));
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, result.status());
  }
  std::shared_ptr<XClusterRpcTasks> xcluster_rpc = *result;

  // For each table, run an async RPC task to verify a sufficient Producer:Consumer schema match.
  for (int i = 0; i < req->producer_table_ids_size(); i++) {
    scoped_refptr<SetupUniverseReplicationHelper> shared_this = this;

    // SETUP CONTINUES after this async call.
    Status s;
    if (IsColocatedDbParentTableId(req->producer_table_ids(i))) {
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = xcluster_rpc->client()->GetColocatedTabletSchemaByParentTableId(
          req->producer_table_ids(i), tables_info,
          Bind(
              &SetupUniverseReplicationHelper::GetColocatedTabletSchemaCallback, shared_this,
              ri->ReplicationGroupId(), tables_info, setup_info));
    } else if (IsTablegroupParentTableId(req->producer_table_ids(i))) {
      auto tablegroup_id = GetTablegroupIdFromParentTableId(req->producer_table_ids(i));
      auto tables_info = std::make_shared<std::vector<client::YBTableInfo>>();
      s = xcluster_rpc->client()->GetTablegroupSchemaById(
          tablegroup_id, tables_info,
          Bind(
              &SetupUniverseReplicationHelper::GetTablegroupSchemaCallback, shared_this,
              ri->ReplicationGroupId(), tables_info, tablegroup_id, setup_info));
    } else {
      auto table_info = std::make_shared<client::YBTableInfo>();
      s = xcluster_rpc->client()->GetTableSchemaById(
          req->producer_table_ids(i), table_info,
          Bind(
              &SetupUniverseReplicationHelper::GetTableSchemaCallback, shared_this,
              ri->ReplicationGroupId(), table_info, setup_info));
    }

    if (!s.ok()) {
      MarkUniverseReplicationFailed(ri, s);
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
  }

  LOG(INFO) << "Started schema validation for universe replication " << ri->ToString();
  return Status::OK();
}

Status SetupUniverseReplicationHelper::ValidateMasterAddressesBelongToDifferentCluster(
    Master& master, const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses) {
  std::vector<ServerEntryPB> cluster_master_addresses;
  RETURN_NOT_OK(master.ListMasters(&cluster_master_addresses));
  std::unordered_set<HostPort, HostPortHash> cluster_master_hps;

  for (const auto& cluster_elem : cluster_master_addresses) {
    if (cluster_elem.has_registration()) {
      auto p_rpc_addresses = cluster_elem.registration().private_rpc_addresses();
      for (const auto& p_rpc_elem : p_rpc_addresses) {
        cluster_master_hps.insert(HostPort::FromPB(p_rpc_elem));
      }

      auto broadcast_addresses = cluster_elem.registration().broadcast_addresses();
      for (const auto& bc_elem : broadcast_addresses) {
        cluster_master_hps.insert(HostPort::FromPB(bc_elem));
      }
    }

    for (const auto& master_address : master_addresses) {
      auto master_hp = HostPort::FromPB(master_address);
      SCHECK(
          !cluster_master_hps.contains(master_hp), InvalidArgument,
          "Master address $0 belongs to the target universe", master_hp);
    }
  }
  return Status::OK();
}

void SetupUniverseReplicationHelper::GetTableSchemaCallback(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::shared_ptr<client::YBTableInfo>& producer_info,
    const SetupReplicationInfo& setup_info, const Status& s) {
  // First get the universe.
  auto universe = catalog_manager_.GetUniverseReplication(replication_group_id);
  if (universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << replication_group_id;
    return;
  }

  std::string action = "getting schema for table";
  auto status = s;
  if (status.ok()) {
    action = "validating table schema and creating xCluster stream";
    status = ValidateTableAndCreateStreams(universe, producer_info, setup_info);
  }

  if (!status.ok()) {
    LOG(ERROR) << "Error " << action << ". Universe: " << replication_group_id
               << ", Table: " << producer_info->table_id << ": " << status;
    MarkUniverseReplicationFailed(universe, status);
  }
}

void SetupUniverseReplicationHelper::GetTablegroupSchemaCallback(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const TablegroupId& producer_tablegroup_id, const SetupReplicationInfo& setup_info,
    const Status& s) {
  // First get the universe.
  auto universe = catalog_manager_.GetUniverseReplication(replication_group_id);
  if (universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << replication_group_id;
    return;
  }

  auto status =
      GetTablegroupSchemaCallbackInternal(universe, *infos, producer_tablegroup_id, setup_info, s);
  if (!status.ok()) {
    std::ostringstream oss;
    for (size_t i = 0; i < infos->size(); ++i) {
      oss << ((i == 0) ? "" : ", ") << (*infos)[i].table_id;
    }
    LOG(ERROR) << "Error processing for tables: [ " << oss.str()
               << " ] for xCluster replication group " << replication_group_id << ": " << status;
    MarkUniverseReplicationFailed(universe, status);
  }
}

Status SetupUniverseReplicationHelper::GetTablegroupSchemaCallbackInternal(
    scoped_refptr<UniverseReplicationInfo>& universe, const std::vector<client::YBTableInfo>& infos,
    const TablegroupId& producer_tablegroup_id, const SetupReplicationInfo& setup_info,
    const Status& s) {
  RETURN_NOT_OK(s);

  SCHECK(!infos.empty(), IllegalState, Format("Tablegroup $0 is empty", producer_tablegroup_id));

  // validated_consumer_tables contains the table IDs corresponding to that
  // from the producer tables.
  std::unordered_set<TableId> validated_consumer_tables;
  ColocationSchemaVersions colocated_schema_versions;
  colocated_schema_versions.reserve(infos.size());
  for (const auto& info : infos) {
    // Validate each of the member table in the tablegroup.
    GetTableSchemaResponsePB resp;
    RETURN_NOT_OK(ValidateTableSchemaForXCluster(info, setup_info, &resp));

    colocated_schema_versions.emplace_back(
        resp.schema().colocated_table_id().colocation_id(), info.schema.version(), resp.version());
    validated_consumer_tables.insert(resp.identifier().table_id());
  }

  // Get the consumer tablegroup ID. Since this call is expensive (one needs to reverse lookup
  // the tablegroup ID from table ID), we only do this call once and do validation afterward.
  TablegroupId consumer_tablegroup_id;
  // Starting Colocation GA, colocated databases create implicit underlying tablegroups.
  bool colocated_database;
  RETURN_NOT_OK(catalog_manager_.GetTableGroupAndColocationInfo(
      *validated_consumer_tables.begin(), consumer_tablegroup_id, colocated_database));

  // tables_in_consumer_tablegroup are the tables listed within the consumer_tablegroup_id.
  // We need validated_consumer_tables and tables_in_consumer_tablegroup to be identical.
  std::unordered_set<TableId> tables_in_consumer_tablegroup;
  {
    GetTablegroupSchemaRequestPB req;
    GetTablegroupSchemaResponsePB resp;
    req.mutable_tablegroup()->set_id(consumer_tablegroup_id);
    auto status = catalog_manager_.GetTablegroupSchema(&req, &resp);
    if (status.ok() && resp.has_error()) {
      status = StatusFromPB(resp.error().status());
    }
    RETURN_NOT_OK_PREPEND(
        status,
        Format("Error when getting consumer tablegroup schema: $0", consumer_tablegroup_id));

    for (const auto& info : resp.get_table_schema_response_pbs()) {
      tables_in_consumer_tablegroup.insert(info.identifier().table_id());
    }
  }

  if (validated_consumer_tables != tables_in_consumer_tablegroup) {
    return STATUS(
        IllegalState,
        Format(
            "Mismatch between tables associated with producer tablegroup $0 and "
            "tables in consumer tablegroup $1: ($2) vs ($3).",
            producer_tablegroup_id, consumer_tablegroup_id, AsString(validated_consumer_tables),
            AsString(tables_in_consumer_tablegroup)));
  }

  RETURN_NOT_OK_PREPEND(
      IsBootstrapRequiredOnProducer(
          universe, producer_tablegroup_id, setup_info.table_bootstrap_ids),
      Format(
          "Found error while checking if bootstrap is required for table $0",
          producer_tablegroup_id));

  TableId producer_parent_table_id;
  TableId consumer_parent_table_id;
  if (colocated_database) {
    producer_parent_table_id = GetColocationParentTableId(producer_tablegroup_id);
    consumer_parent_table_id = GetColocationParentTableId(consumer_tablegroup_id);
  } else {
    producer_parent_table_id = GetTablegroupParentTableId(producer_tablegroup_id);
    consumer_parent_table_id = GetTablegroupParentTableId(consumer_tablegroup_id);
  }

  SCHECK(
      !xcluster_manager_.IsTableReplicationConsumer(consumer_parent_table_id), IllegalState,
      "N:1 replication topology not supported");

  RETURN_NOT_OK(AddValidatedTableAndCreateStreams(
      universe, setup_info.table_bootstrap_ids, producer_parent_table_id, consumer_parent_table_id,
      colocated_schema_versions));
  return Status::OK();
}

void SetupUniverseReplicationHelper::GetColocatedTabletSchemaCallback(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::shared_ptr<std::vector<client::YBTableInfo>>& infos,
    const SetupReplicationInfo& setup_info, const Status& s) {
  // First get the universe.
  auto universe = catalog_manager_.GetUniverseReplication(replication_group_id);
  if (universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << replication_group_id;
    return;
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
    Status table_status = ValidateTableSchemaForXCluster(info, setup_info, &resp);
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
    auto message = Format(
        "Found incorrect number of producer colocated parent table ids. "
        "Expected 1, but found: $0",
        AsString(producer_parent_table_ids));
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument, message));
    LOG(ERROR) << message;
    return;
  }
  if (consumer_parent_table_ids.size() != 1) {
    auto message = Format(
        "Found incorrect number of consumer colocated parent table ids. "
        "Expected 1, but found: $0",
        AsString(consumer_parent_table_ids));
    MarkUniverseReplicationFailed(universe, STATUS(InvalidArgument, message));
    LOG(ERROR) << message;
    return;
  }

  if (xcluster_manager_.IsTableReplicationConsumer(*consumer_parent_table_ids.begin())) {
    std::string message = "N:1 replication topology not supported";
    MarkUniverseReplicationFailed(universe, STATUS(IllegalState, message));
    LOG(ERROR) << message;
    return;
  }

  Status status = IsBootstrapRequiredOnProducer(
      universe, *producer_parent_table_ids.begin(), setup_info.table_bootstrap_ids);
  if (!status.ok()) {
    MarkUniverseReplicationFailed(universe, status);
    LOG(ERROR) << "Found error while checking if bootstrap is required for table "
               << *producer_parent_table_ids.begin() << ": " << status;
  }

  status = AddValidatedTableAndCreateStreams(
      universe, setup_info.table_bootstrap_ids, *producer_parent_table_ids.begin(),
      *consumer_parent_table_ids.begin(), colocated_schema_versions);

  if (!status.ok()) {
    LOG(ERROR) << "Found error while adding validated table to system catalog: "
               << *producer_parent_table_ids.begin() << ": " << status;
    return;
  }
}

Status SetupUniverseReplicationHelper::ValidateTableAndCreateStreams(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::shared_ptr<client::YBTableInfo>& producer_info,
    const SetupReplicationInfo& setup_info) {
  auto l = universe->LockForWrite();
  if (producer_info->table_name.namespace_name() == master::kSystemNamespaceName) {
    auto status = STATUS(IllegalState, "Cannot replicate system tables.");
    MarkUniverseReplicationFailed(status, &l, universe);
    return status;
  }
  RETURN_ACTION_NOT_OK(
      sys_catalog_.Upsert(epoch_, universe), "updating system tables in universe replication");
  l.Commit();

  GetTableSchemaResponsePB consumer_schema;
  RETURN_NOT_OK(ValidateTableSchemaForXCluster(*producer_info, setup_info, &consumer_schema));

  // If Bootstrap Id is passed in then it must be provided for all tables.
  const auto& producer_bootstrap_ids = setup_info.table_bootstrap_ids;
  SCHECK(
      producer_bootstrap_ids.empty() || producer_bootstrap_ids.contains(producer_info->table_id),
      NotFound,
      Format("Bootstrap id not found for table $0", producer_info->table_name.ToString()));

  RETURN_NOT_OK(
      IsBootstrapRequiredOnProducer(universe, producer_info->table_id, producer_bootstrap_ids));

  SchemaVersion producer_schema_version = producer_info->schema.version();
  SchemaVersion consumer_schema_version = consumer_schema.version();
  ColocationSchemaVersions colocated_schema_versions;
  RETURN_NOT_OK(AddValidatedTableToUniverseReplication(
      universe, producer_info->table_id, consumer_schema.identifier().table_id(),
      producer_schema_version, consumer_schema_version, colocated_schema_versions));

  return CreateStreamsIfReplicationValidated(universe, producer_bootstrap_ids);
}

Status SetupUniverseReplicationHelper::ValidateTableSchemaForXCluster(
    const client::YBTableInfo& info, const SetupReplicationInfo& setup_info,
    GetTableSchemaResponsePB* resp) {
  bool is_ysql_table = info.table_type == client::YBTableType::PGSQL_TABLE_TYPE;
  if (setup_info.transactional && !GetAtomicFlag(&FLAGS_TEST_allow_ycql_transactional_xcluster) &&
      !is_ysql_table) {
    return STATUS_FORMAT(
        NotSupported, "Transactional replication is not supported for non-YSQL tables: $0",
        info.table_name.ToString());
  }

  // Get corresponding table schema on local universe.
  GetTableSchemaRequestPB req;

  auto* table = req.mutable_table();
  table->set_table_name(info.table_name.table_name());
  table->mutable_namespace_()->set_name(info.table_name.namespace_name());
  table->mutable_namespace_()->set_database_type(
      GetDatabaseTypeForTable(client::ClientToPBTableType(info.table_type)));

  // Since YSQL tables are not present in table map, we first need to list tables to get the table
  // ID and then get table schema.
  // Remove this once table maps are fixed for YSQL.
  ListTablesRequestPB list_req;
  ListTablesResponsePB list_resp;

  list_req.set_name_filter(info.table_name.table_name());
  Status status = catalog_manager_.ListTables(&list_req, &list_resp);
  SCHECK(
      status.ok() && !list_resp.has_error(), NotFound,
      Format("Error while listing table: $0", status.ToString()));

  const auto& source_schema = client::internal::GetSchema(info.schema);
  for (const auto& t : list_resp.tables()) {
    // Check that table name and namespace both match.
    if (t.name() != info.table_name.table_name() ||
        t.namespace_().name() != info.table_name.namespace_name()) {
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
    status = catalog_manager_.GetTableSchema(&req, resp);
    SCHECK(
        status.ok() && !resp->has_error(), NotFound,
        Format("Error while getting table schema: $0", status.ToString()));

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
      return STATUS_FORMAT(
          NotSupported, "Replication is not supported for materialized view: $0",
          info.table_name.ToString());
    }

    Schema consumer_schema;
    auto result = SchemaFromPB(resp->schema(), &consumer_schema);

    // We now have a table match. Validate the schema.
    SCHECK(
        result.ok() && consumer_schema.EquivalentForDataCopy(source_schema), IllegalState,
        Format(
            "Source and target schemas don't match: "
            "Source: $0, Target: $1, Source schema: $2, Target schema: $3",
            info.table_id, resp->identifier().table_id(), info.schema.ToString(),
            resp->schema().DebugString()));
    break;
  }

  SCHECK(
      table->has_table_id(), NotFound,
      Format(
          "Could not find matching table for $0$1", info.table_name.ToString(),
          (is_ysql_table ? " pgschema_name: " + source_schema.SchemaName() : "")));

  // Still need to make map of table id to resp table id (to add to validated map)
  // For colocated tables, only add the parent table since we only added the parent table to the
  // original pb (we use the number of tables in the pb to determine when validation is done).
  if (info.colocated) {
    // We require that colocated tables have the same colocation ID.
    //
    // Backward compatibility: tables created prior to #7378 use YSQL table OID as a colocation ID.
    auto source_clc_id = info.schema.has_colocation_id()
                             ? info.schema.colocation_id()
                             : CHECK_RESULT(GetPgsqlTableOid(info.table_id));
    auto target_clc_id = (resp->schema().has_colocated_table_id() &&
                          resp->schema().colocated_table_id().has_colocation_id())
                             ? resp->schema().colocated_table_id().colocation_id()
                             : CHECK_RESULT(GetPgsqlTableOid(resp->identifier().table_id()));
    SCHECK(
        source_clc_id == target_clc_id, IllegalState,
        Format(
            "Source and target colocation IDs don't match for colocated table: "
            "Source: $0, Target: $1, Source colocation ID: $2, Target colocation ID: $3",
            info.table_id, resp->identifier().table_id(), source_clc_id, target_clc_id));
  }

  SCHECK(
      !xcluster_manager_.IsTableReplicationConsumer(table->table_id()), IllegalState,
      "N:1 replication topology not supported");

  return Status::OK();
}

Status SetupUniverseReplicationHelper::IsBootstrapRequiredOnProducer(
    scoped_refptr<UniverseReplicationInfo> universe, const TableId& producer_table,
    const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids) {
  if (!FLAGS_check_bootstrap_required) {
    return Status::OK();
  }
  auto master_addresses = universe->LockForRead()->pb.producer_master_addresses();
  boost::optional<xrepl::StreamId> bootstrap_id;
  if (table_bootstrap_ids.count(producer_table) > 0) {
    bootstrap_id = table_bootstrap_ids.at(producer_table);
  }

  auto xcluster_rpc = VERIFY_RESULT(universe->GetOrCreateXClusterRpcTasks(master_addresses));
  if (VERIFY_RESULT(xcluster_rpc->client()->IsBootstrapRequired({producer_table}, bootstrap_id))) {
    return STATUS(
        IllegalState,
        Format(
            "Error Missing Data in Logs. Bootstrap is required for producer $0", universe->id()));
  }
  return Status::OK();
}

Status SetupUniverseReplicationHelper::AddValidatedTableToUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe, const TableId& producer_table,
    const TableId& consumer_table, const SchemaVersion& producer_schema_version,
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
      sys_catalog_.Upsert(epoch_, universe), "updating universe replication info in sys-catalog");
  l.Commit();

  return Status::OK();
}

Status SetupUniverseReplicationHelper::AddValidatedTableAndCreateStreams(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids,
    const TableId& producer_table, const TableId& consumer_table,
    const ColocationSchemaVersions& colocated_schema_versions) {
  RETURN_NOT_OK(AddValidatedTableToUniverseReplication(
      universe, producer_table, consumer_table, cdc::kInvalidSchemaVersion,
      cdc::kInvalidSchemaVersion, colocated_schema_versions));
  return CreateStreamsIfReplicationValidated(universe, table_bootstrap_ids);
}

Status SetupUniverseReplicationHelper::CreateStreamsIfReplicationValidated(
    scoped_refptr<UniverseReplicationInfo> universe,
    const std::unordered_map<TableId, xrepl::StreamId>& table_bootstrap_ids) {
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
  auto res = universe->GetOrCreateXClusterRpcTasks(master_addresses);
  if (!res.ok()) {
    MarkUniverseReplicationFailed(res.status(), &l, universe);
    return STATUS(
        InternalError, Format(
                           "Error while setting up client for producer $0: $1", universe->id(),
                           res.status().ToString()));
  }
  std::shared_ptr<XClusterRpcTasks> xcluster_rpc = *res;

  // Now, all tables are validated.
  std::vector<TableId> validated_tables;
  auto& tbl_iter = mutable_pb->tables();
  validated_tables.insert(validated_tables.begin(), tbl_iter.begin(), tbl_iter.end());

  mutable_pb->set_state(SysUniverseReplicationEntryPB::VALIDATED);
  // Update sys_catalog.
  RETURN_ACTION_NOT_OK(
      sys_catalog_.Upsert(epoch_, universe), "updating universe replication info in sys-catalog");
  l.Commit();

  // Create xCluster stream for each validated table, after persisting the replication state change.
  if (!validated_tables.empty()) {
    // Keep track of the bootstrap_id, table_id, and options of streams to update after
    // the last GetStreamCallback finishes. Will be updated by multiple async
    // GetStreamCallback.
    auto stream_update_infos = std::make_shared<StreamUpdateInfos>();
    stream_update_infos->reserve(validated_tables.size());
    auto update_infos_lock = std::make_shared<std::mutex>();

    for (const auto& table : validated_tables) {
      scoped_refptr<SetupUniverseReplicationHelper> shared_this = this;

      auto producer_bootstrap_id = FindOrNull(table_bootstrap_ids, table);
      if (producer_bootstrap_id && *producer_bootstrap_id) {
        auto table_id = std::make_shared<TableId>();
        auto stream_options = std::make_shared<std::unordered_map<std::string, std::string>>();
        xcluster_rpc->client()->GetCDCStream(
            *producer_bootstrap_id, table_id, stream_options,
            std::bind(
                &SetupUniverseReplicationHelper::GetStreamCallback, shared_this,
                *producer_bootstrap_id, table_id, stream_options, universe->ReplicationGroupId(),
                table, xcluster_rpc, std::placeholders::_1, stream_update_infos,
                update_infos_lock));
      } else {
        // Streams are used as soon as they are created so set state to active.
        client::XClusterClient(*xcluster_rpc->client())
            .CreateXClusterStreamAsync(
                table, /*active=*/true, transactional,
                std::bind(
                    &SetupUniverseReplicationHelper::AddStreamToUniverseAndInitConsumer,
                    shared_this, universe->ReplicationGroupId(), table, std::placeholders::_1,
                    nullptr /* on_success_cb */));
      }
    }
  }
  return Status::OK();
}

void SetupUniverseReplicationHelper::GetStreamCallback(
    const xrepl::StreamId& bootstrap_id, std::shared_ptr<TableId> table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options,
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table,
    std::shared_ptr<XClusterRpcTasks> xcluster_rpc, const Status& s,
    std::shared_ptr<StreamUpdateInfos> stream_update_infos,
    std::shared_ptr<std::mutex> update_infos_lock) {
  if (!s.ok()) {
    LOG(ERROR) << "Unable to find bootstrap id " << bootstrap_id;
    AddStreamToUniverseAndInitConsumer(replication_group_id, table, s);
    return;
  }

  if (*table_id != table) {
    const Status invalid_bootstrap_id_status = STATUS_FORMAT(
        InvalidArgument, "Invalid bootstrap id for table $0. Bootstrap id $1 belongs to table $2",
        table, bootstrap_id, *table_id);
    LOG(ERROR) << invalid_bootstrap_id_status;
    AddStreamToUniverseAndInitConsumer(replication_group_id, table, invalid_bootstrap_id_status);
    return;
  }

  auto original_universe = catalog_manager_.GetUniverseReplication(replication_group_id);

  if (original_universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << replication_group_id;
    return;
  }

  cdc::StreamModeTransactional transactional(original_universe->LockForRead()->pb.transactional());

  // todo check options
  {
    std::lock_guard lock(*update_infos_lock);
    stream_update_infos->push_back({bootstrap_id, *table_id, *options});
  }

  const auto update_xrepl_stream_func = [&]() -> Status {
    // Extra callback on universe setup success - update the producer to let it know that
    // the bootstrapping is complete. This callback will only be called once among all
    // the GetStreamCallback calls, and we update all streams in batch at once.

    std::vector<xrepl::StreamId> update_bootstrap_ids;
    std::vector<SysCDCStreamEntryPB> update_entries;
    {
      std::lock_guard lock(*update_infos_lock);

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
    }

    RETURN_NOT_OK_PREPEND(
        xcluster_rpc->client()->UpdateCDCStream(update_bootstrap_ids, update_entries),
        "Unable to update xrepl stream options on source universe");

    {
      std::lock_guard lock(*update_infos_lock);
      stream_update_infos->clear();
    }
    return Status::OK();
  };

  AddStreamToUniverseAndInitConsumer(
      replication_group_id, table, bootstrap_id, update_xrepl_stream_func);
}

void SetupUniverseReplicationHelper::AddStreamToUniverseAndInitConsumer(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
    const Result<xrepl::StreamId>& stream_id, std::function<Status()> on_success_cb) {
  auto universe = catalog_manager_.GetUniverseReplication(replication_group_id);
  if (universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << replication_group_id;
    return;
  }

  Status s;
  if (!stream_id.ok()) {
    s = std::move(stream_id).status();
  } else {
    s = AddStreamToUniverseAndInitConsumerInternal(
        universe, table_id, *stream_id, std::move(on_success_cb));
  }

  if (!s.ok()) {
    MarkUniverseReplicationFailed(universe, s);
  }
}

Status SetupUniverseReplicationHelper::AddStreamToUniverseAndInitConsumerInternal(
    scoped_refptr<UniverseReplicationInfo> universe, const TableId& table_id,
    const xrepl::StreamId& stream_id, std::function<Status()> on_success_cb) {
  bool merge_alter = false;
  bool validated_all_tables = false;
  std::vector<XClusterConsumerStreamInfo> consumer_info;
  {
    auto l = universe->LockForWrite();
    if (l->is_deleted_or_failed()) {
      // Nothing to do if universe is being deleted.
      return Status::OK();
    }

    auto map = l.mutable_data()->pb.mutable_table_streams();
    (*map)[table_id] = stream_id.ToString();

    // This functions as a barrier: waiting for the last RPC call from GetTableSchemaCallback.
    if (l.mutable_data()->pb.table_streams_size() == l->pb.tables_size()) {
      // All tables successfully validated! Register xCluster consumers & start replication.
      validated_all_tables = true;
      LOG(INFO) << "Registering xCluster consumers for universe " << universe->id();

      consumer_info.reserve(l->pb.tables_size());
      std::set<TableId> consumer_table_ids;
      for (const auto& [producer_table_id, consumer_table_id] : l->pb.validated_tables()) {
        consumer_table_ids.insert(consumer_table_id);

        XClusterConsumerStreamInfo info;
        info.producer_table_id = producer_table_id;
        info.consumer_table_id = consumer_table_id;
        info.stream_id = VERIFY_RESULT(xrepl::StreamId::FromString((*map)[producer_table_id]));
        consumer_info.push_back(info);
      }

      if (l->IsDbScoped()) {
        std::vector<NamespaceId> consumer_namespace_ids;
        for (const auto& ns_info : l->pb.db_scoped_info().namespace_infos()) {
          consumer_namespace_ids.push_back(ns_info.consumer_namespace_id());
        }
        RETURN_NOT_OK(ValidateTableListForDbScopedReplication(
            *universe, consumer_namespace_ids, consumer_table_ids, catalog_manager_));
      }

      std::vector<HostPort> hp;
      HostPortsFromPBs(l->pb.producer_master_addresses(), &hp);
      auto xcluster_rpc_tasks =
          VERIFY_RESULT(universe->GetOrCreateXClusterRpcTasks(l->pb.producer_master_addresses()));
      RETURN_NOT_OK(InitXClusterConsumer(
          consumer_info, HostPort::ToCommaSeparatedString(hp), *universe.get(),
          xcluster_rpc_tasks));

      if (xcluster::IsAlterReplicationGroupId(universe->ReplicationGroupId())) {
        // Don't enable ALTER universes, merge them into the main universe instead.
        // on_success_cb will be invoked in MergeUniverseReplication.
        merge_alter = true;
      } else {
        l.mutable_data()->pb.set_state(SysUniverseReplicationEntryPB::ACTIVE);
        if (on_success_cb) {
          // Before updating, run any callbacks on success.
          RETURN_NOT_OK(on_success_cb());
        }
      }
    }

    // Update sys_catalog with new producer table id info.
    RETURN_NOT_OK(sys_catalog_.Upsert(epoch_, universe));

    l.Commit();
  }

  if (!validated_all_tables) {
    return Status::OK();
  }

  auto final_id = xcluster::GetOriginalReplicationGroupId(universe->ReplicationGroupId());
  // If this is an 'alter', merge back into primary command now that setup is a success.
  if (merge_alter) {
    RETURN_NOT_OK(MergeUniverseReplication(universe, final_id, std::move(on_success_cb)));
  }
  // Update the in-memory cache of consumer tables.
  for (const auto& info : consumer_info) {
    xcluster_manager_.RecordTableConsumerStream(info.consumer_table_id, final_id, info.stream_id);
  }

  return Status::OK();
}

Status SetupUniverseReplicationHelper::InitXClusterConsumer(
    const std::vector<XClusterConsumerStreamInfo>& consumer_info, const std::string& master_addrs,
    UniverseReplicationInfo& replication_info,
    std::shared_ptr<XClusterRpcTasks> xcluster_rpc_tasks) {
  auto universe_l = replication_info.LockForRead();
  auto schema_version_mappings = universe_l->pb.schema_version_mappings();

  // Get the tablets in the consumer table.
  cdc::ProducerEntryPB producer_entry;

  if (FLAGS_enable_xcluster_auto_flag_validation) {
    auto compatible_auto_flag_config_version = VERIFY_RESULT(
        GetAutoFlagConfigVersionIfCompatible(replication_info, master_.GetAutoFlagsConfig()));
    producer_entry.set_compatible_auto_flag_config_version(compatible_auto_flag_config_version);
    producer_entry.set_validated_auto_flags_config_version(compatible_auto_flag_config_version);
  }

  auto cluster_config = catalog_manager_.ClusterConfig();
  auto l = cluster_config->LockForWrite();
  auto* consumer_registry = l.mutable_data()->pb.mutable_consumer_registry();
  auto transactional = universe_l->pb.transactional();
  if (!xcluster::IsAlterReplicationGroupId(replication_info.ReplicationGroupId())) {
    if (universe_l->IsDbScoped()) {
      DCHECK(transactional);
    }
  }

  for (const auto& stream_info : consumer_info) {
    auto consumer_tablet_keys =
        VERIFY_RESULT(catalog_manager_.GetTableKeyRanges(stream_info.consumer_table_id));
    auto schema_version =
        VERIFY_RESULT(catalog_manager_.GetTableSchemaVersion(stream_info.consumer_table_id));

    cdc::StreamEntryPB stream_entry;
    // Get producer tablets and map them to the consumer tablets
    RETURN_NOT_OK(InitXClusterStream(
        stream_info.producer_table_id, stream_info.consumer_table_id, consumer_tablet_keys,
        &stream_entry, xcluster_rpc_tasks));
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

    // Mark this stream as special if it is for the ddl_queue table.
    auto table_info = catalog_manager_.GetTableInfo(stream_info.consumer_table_id);
    stream_entry.set_is_ddl_queue_table(
        table_info->GetTableType() == PGSQL_TABLE_TYPE &&
        table_info->name() == xcluster::kDDLQueueTableName &&
        table_info->pgschema_name() == xcluster::kDDLQueuePgSchemaName);

    (*producer_entry.mutable_stream_map())[stream_info.stream_id.ToString()] =
        std::move(stream_entry);
  }

  // Log the Network topology of the Producer Cluster
  auto master_addrs_list = StringSplit(master_addrs, ',');
  producer_entry.mutable_master_addrs()->Reserve(narrow_cast<int>(master_addrs_list.size()));
  for (const auto& addr : master_addrs_list) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, 0));
    HostPortToPB(hp, producer_entry.add_master_addrs());
  }

  auto* replication_group_map = consumer_registry->mutable_producer_map();
  SCHECK_EQ(
      replication_group_map->count(replication_info.id()), 0, InvalidArgument,
      "Already created a consumer for this universe");

  // TServers will use the ClusterConfig to create xCluster Consumers for applicable local tablets.
  (*replication_group_map)[replication_info.id()] = std::move(producer_entry);

  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(CheckStatus(
      sys_catalog_.Upsert(epoch_, cluster_config.get()), "updating cluster config in sys-catalog"));

  xcluster_manager_.SyncConsumerReplicationStatusMap(
      replication_info.ReplicationGroupId(), *replication_group_map);
  l.Commit();

  xcluster_manager_.CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Status SetupUniverseReplicationHelper::MergeUniverseReplication(
    scoped_refptr<UniverseReplicationInfo> universe, xcluster::ReplicationGroupId original_id,
    std::function<Status()> on_success_cb) {
  // Merge back into primary command now that setup is a success.
  LOG(INFO) << "Merging xCluster ReplicationGroup: " << universe->id() << " into " << original_id;

  SCHECK(
      !FLAGS_TEST_fail_universe_replication_merge, IllegalState,
      "TEST_fail_universe_replication_merge");

  auto original_universe = catalog_manager_.GetUniverseReplication(original_id);
  if (original_universe == nullptr) {
    LOG(ERROR) << "Universe not found: " << original_id;
    return Status::OK();
  }

  {
    auto cluster_config = catalog_manager_.ClusterConfig();
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
    auto& alter_pb = alter_lock.mutable_data()->pb;
    auto& original_pb = original_lock.mutable_data()->pb;

    auto* alter_tables = alter_pb.mutable_tables();
    original_pb.mutable_tables()->MergeFrom(*alter_tables);
    alter_tables->Clear();
    auto* alter_table_streams = alter_pb.mutable_table_streams();
    original_pb.mutable_table_streams()->insert(
        alter_table_streams->begin(), alter_table_streams->end());
    alter_table_streams->clear();
    auto* alter_validated_tables = alter_pb.mutable_validated_tables();
    original_pb.mutable_validated_tables()->insert(
        alter_validated_tables->begin(), alter_validated_tables->end());
    alter_validated_tables->clear();
    if (alter_lock.mutable_data()->IsDbScoped()) {
      auto* alter_namespace_info = alter_pb.mutable_db_scoped_info()->mutable_namespace_infos();
      original_pb.mutable_db_scoped_info()->mutable_namespace_infos()->MergeFrom(
          *alter_namespace_info);
      alter_namespace_info->Clear();
    }

    alter_pb.set_state(SysUniverseReplicationEntryPB::DELETED);

    if (PREDICT_FALSE(FLAGS_TEST_exit_unfinished_merging)) {
      return Status::OK();
    }

    if (on_success_cb) {
      RETURN_NOT_OK(on_success_cb());
    }

    {
      // Need all three updates to be atomic.
      auto s = CheckStatus(
          sys_catalog_.Upsert(
              epoch_, original_universe.get(), universe.get(), cluster_config.get()),
          "Updating universe replication entries and cluster config in sys-catalog");
    }

    xcluster_manager_.SyncConsumerReplicationStatusMap(
        original_universe->ReplicationGroupId(), *pm);
    xcluster_manager_.SyncConsumerReplicationStatusMap(universe->ReplicationGroupId(), *pm);

    alter_lock.Commit();
    cl.Commit();
    original_lock.Commit();
  }

  // Add alter temp universe to GC.
  catalog_manager_.MarkUniverseForCleanup(universe->ReplicationGroupId());

  LOG(INFO) << "Done with Merging " << universe->id() << " into " << original_universe->id();

  xcluster_manager_.CreateXClusterSafeTimeTableAndStartService();

  return Status::OK();
}

Result<scoped_refptr<UniverseReplicationInfo>>
SetupUniverseReplicationHelper::CreateUniverseReplicationInfo(
    const xcluster::ReplicationGroupId& replication_group_id,
    const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses,
    const std::vector<NamespaceId>& producer_namespace_ids,
    const std::vector<NamespaceId>& consumer_namespace_ids,
    const google::protobuf::RepeatedPtrField<std::string>& table_ids, bool transactional) {
  SCHECK_EQ(
      producer_namespace_ids.size(), consumer_namespace_ids.size(), InvalidArgument,
      "We should have the namespaceIds from both producer and consumer");

  SCHECK(
      catalog_manager_.GetUniverseReplication(replication_group_id) == nullptr, AlreadyPresent,
      "Replication group $0 already present", replication_group_id.ToString());

  for (const auto& universe : catalog_manager_.GetAllUniverseReplications()) {
    for (const auto& consumer_namespace_id : consumer_namespace_ids) {
      SCHECK_FORMAT(
          !IncludesConsumerNamespace(*universe, consumer_namespace_id), AlreadyPresent,
          "Namespace $0 already included in replication group $1", consumer_namespace_id,
          universe->ReplicationGroupId());
    }
  }

  // Create an entry in the system catalog DocDB for this new universe replication.
  scoped_refptr<UniverseReplicationInfo> ri = new UniverseReplicationInfo(replication_group_id);
  ri->mutable_metadata()->StartMutation();
  SysUniverseReplicationEntryPB* metadata = &ri->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_replication_group_id(replication_group_id.ToString());
  metadata->mutable_producer_master_addresses()->CopyFrom(master_addresses);

  if (!producer_namespace_ids.empty()) {
    auto* db_scoped_info = metadata->mutable_db_scoped_info();
    for (size_t i = 0; i < producer_namespace_ids.size(); i++) {
      auto* ns_info = db_scoped_info->mutable_namespace_infos()->Add();
      ns_info->set_producer_namespace_id(producer_namespace_ids[i]);
      ns_info->set_consumer_namespace_id(consumer_namespace_ids[i]);
    }
  }
  metadata->mutable_tables()->CopyFrom(table_ids);
  metadata->set_state(SysUniverseReplicationEntryPB::INITIALIZING);
  metadata->set_transactional(transactional);

  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_.Upsert(epoch_, ri), "inserting universe replication info into sys-catalog"));

  // Commit the in-memory state now that it's added to the persistent catalog.
  ri->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Setup universe replication from producer " << ri->ToString();

  catalog_manager_.InsertNewUniverseReplication(*ri);

  // Make sure the AutoFlags are compatible.
  // This is done after the replication info is persisted since it performs RPC calls to source
  // universe and we can crash during this call.
  // TODO: When new master starts it can retry this step or mark the replication group as failed.
  if (FLAGS_enable_xcluster_auto_flag_validation) {
    const auto auto_flags_config = master_.GetAutoFlagsConfig();
    auto status = ResultToStatus(GetAutoFlagConfigVersionIfCompatible(*ri, auto_flags_config));

    if (!status.ok()) {
      MarkUniverseReplicationFailed(ri, status);
      return status.CloneAndAddErrorCode(MasterError(MasterErrorPB::INVALID_REQUEST));
    }

    auto l = ri->LockForWrite();
    l.mutable_data()->pb.set_validated_local_auto_flags_config_version(
        auto_flags_config.config_version());

    RETURN_NOT_OK(CheckLeaderStatus(
        sys_catalog_.Upsert(epoch_, ri), "inserting universe replication info into sys-catalog"));

    l.Commit();
  }
  return ri;
}

}  // namespace yb::master
