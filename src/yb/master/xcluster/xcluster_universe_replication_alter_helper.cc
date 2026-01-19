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

#include "yb/master/xcluster/xcluster_universe_replication_alter_helper.h"

#include "yb/common/wire_protocol.h"
#include "yb/common/xcluster_util.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_util.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"
#include "yb/util/backoff_waiter.h"

DEFINE_RUNTIME_uint32(xcluster_alter_universe_replication_setup_timeout_ms, 10 * 60 * 1000,
    "Maximum time in milliseconds to wait for setup replication to complete when altering "
    "universe replication.");

namespace yb::master {

AlterUniverseReplicationHelper::AlterUniverseReplicationHelper(
    Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch)
    : catalog_manager_(catalog_manager),
      sys_catalog_(*catalog_manager.sys_catalog()),
      xcluster_manager_(*catalog_manager.GetXClusterManagerImpl()),
      epoch_(epoch) {}

AlterUniverseReplicationHelper::~AlterUniverseReplicationHelper() {}

Status AlterUniverseReplicationHelper::Alter(
    Master& master, CatalogManager& catalog_manager, const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp, const LeaderEpoch& epoch) {
  AlterUniverseReplicationHelper helper(master, catalog_manager, epoch);
  return helper.AlterUniverseReplication(req, resp);
}

Status AlterUniverseReplicationHelper::AddTablesToReplication(
    Master& master, CatalogManager& catalog_manager, const AddTablesToReplicationData& data,
    const LeaderEpoch& epoch) {
  auto original_ri = catalog_manager.GetUniverseReplication(data.replication_group_id);
  SCHECK_FORMAT(
      original_ri, NotFound, "Could not find xCluster replication group $0",
      data.replication_group_id);
  AlterUniverseReplicationHelper helper(master, catalog_manager, epoch);
  return helper.AddTablesToReplication(original_ri, data);
}

Status AlterUniverseReplicationHelper::AlterUniverseReplication(
    const AlterUniverseReplicationRequestPB* req, AlterUniverseReplicationResponsePB* resp) {
  SCHECK(
      !req->has_deprecated_new_replication_group_id(), InvalidArgument,
      "Use of field deprecated_new_replication_group_id is not supported");

  auto replication_group_id = xcluster::ReplicationGroupId(req->replication_group_id());
  auto original_ri = catalog_manager_.GetUniverseReplication(replication_group_id);
  SCHECK_EC_FORMAT(
      original_ri, NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
      "Could not find xCluster replication group $0", replication_group_id);

  // Currently, config options are mutually exclusive to simplify transactionality.
  int config_count = (req->producer_master_addresses_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_remove_size() > 0 ? 1 : 0) +
                     (req->producer_table_ids_to_add_size() > 0 ? 1 : 0) +
                     (!req->producer_namespace_id_to_remove().empty() ? 1 : 0);
  SCHECK_EC_FORMAT(
      config_count == 1, InvalidArgument, MasterError(MasterErrorPB::INVALID_REQUEST),
      "Only 1 Alter operation per request currently supported: $0", req->ShortDebugString());

  if (req->producer_master_addresses_size() > 0) {
    return UpdateProducerAddress(original_ri, req);
  }

  if (req->has_producer_namespace_id_to_remove()) {
    return RemoveNamespaceFromReplicationGroup(
        original_ri, req->producer_namespace_id_to_remove(), catalog_manager_, epoch_);
  }

  if (req->producer_table_ids_to_remove_size() > 0) {
    std::vector<TableId> table_ids(
        req->producer_table_ids_to_remove().begin(), req->producer_table_ids_to_remove().end());
    return master::RemoveTablesFromReplicationGroup(
        original_ri, table_ids, catalog_manager_, epoch_);
  }

  if (req->producer_table_ids_to_add_size() > 0) {
    std::vector<xrepl::StreamId> bootstrap_ids;
    for (const auto& bootstrap_id : req->producer_bootstrap_ids_to_add()) {
      bootstrap_ids.push_back(VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_id)));
    }
    AddTablesToReplicationData data{
        .replication_group_id = replication_group_id,
        .source_namespace_to_add = req->producer_namespace_to_add(),
        .source_table_ids_to_add = std::vector(
            req->producer_table_ids_to_add().begin(), req->producer_table_ids_to_add().end()),
        .source_bootstrap_ids_to_add = std::move(bootstrap_ids),
    };
    RETURN_NOT_OK(AddTablesToReplication(original_ri, data));
    xcluster_manager_.CreateXClusterSafeTimeTableAndStartService();
    return Status::OK();
  }

  return Status::OK();
}

Status AlterUniverseReplicationHelper::UpdateProducerAddress(
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req) {
  CHECK_GT(req->producer_master_addresses_size(), 0);

  // TODO: Verify the input. Setup an RPC Task, ListTables, ensure same.

  {
    // 1a. Persistent Config: Update the Universe Config for Master.
    auto l = universe->LockForWrite();
    l.mutable_data()->pb.mutable_producer_master_addresses()->CopyFrom(
        req->producer_master_addresses());

    // 1b. Persistent Config: Update the Consumer Registry (updates TServers)
    auto cluster_config = catalog_manager_.ClusterConfig();
    auto cl = cluster_config->LockForWrite();
    auto replication_group_map =
        cl.mutable_data()->pb.mutable_consumer_registry()->mutable_producer_map();
    auto it = replication_group_map->find(req->replication_group_id());
    if (it == replication_group_map->end()) {
      LOG(WARNING) << "Valid Producer Universe not in Consumer Registry: "
                   << req->replication_group_id();
      return STATUS(
          NotFound, "Could not find xCluster ReplicationGroup", req->ShortDebugString(),
          MasterError(MasterErrorPB::OBJECT_NOT_FOUND));
    }
    it->second.mutable_master_addrs()->CopyFrom(req->producer_master_addresses());
    cl.mutable_data()->pb.set_version(cl.mutable_data()->pb.version() + 1);

    {
      RETURN_NOT_OK(CheckStatus(
          sys_catalog_.Upsert(epoch_, universe.get(), cluster_config.get()),
          "Updating universe replication info and cluster config in sys-catalog"));
    }
    l.Commit();
    cl.Commit();
  }

  // 2. Memory Update: Change xcluster_rpc_tasks (Master cache)
  {
    auto result = universe->GetOrCreateXClusterRpcTasks(req->producer_master_addresses());
    if (!result.ok()) {
      return result.status();
    }
  }

  return Status::OK();
}

Status AlterUniverseReplicationHelper::AddTablesToReplication(
    scoped_refptr<UniverseReplicationInfo> universe,
    const AddTablesToReplicationData& add_table_data) {
  SCHECK(!add_table_data.source_table_ids_to_add.empty(), InvalidArgument, "No tables specified");

  if (universe->IsDbScoped()) {
    // We either add the entire namespace at once, or one table at a time as they get created.
    if (add_table_data.HasSourceNamespaceToAdd()) {
      SCHECK(
          !add_table_data.source_namespace_to_add.id().empty(), InvalidArgument,
          "Invalid Namespace Id");
      SCHECK(
          !add_table_data.source_namespace_to_add.name().empty(), InvalidArgument,
          "Invalid Namespace name");
      SCHECK_EQ(
          add_table_data.source_namespace_to_add.database_type(), YQLDatabase::YQL_DATABASE_PGSQL,
          InvalidArgument, "Invalid Namespace database_type");
    } else {
      SCHECK_EQ(
          add_table_data.source_table_ids_to_add.size(), 1, InvalidArgument,
          "When adding more than table to a DB scoped replication the namespace info must also be "
          "provided");
    }
  } else {
    SCHECK(
        !add_table_data.HasSourceNamespaceToAdd(), InvalidArgument,
        "Cannot add namespaces to non DB scoped replication");
  }

  xcluster::ReplicationGroupId alter_replication_group_id(xcluster::GetAlterReplicationGroupId(
      xcluster::ReplicationGroupId(add_table_data.replication_group_id)));

  // If user passed in bootstrap ids, check that there is a bootstrap id for every table.
  SCHECK(
      add_table_data.source_bootstrap_ids_to_add.size() == 0 ||
          add_table_data.source_table_ids_to_add.size() ==
              add_table_data.source_bootstrap_ids_to_add.size(),
      InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
      add_table_data.ToString());

  // Also verify target_table_ids.
  SCHECK(
      !add_table_data.target_table_id || (add_table_data.source_table_ids_to_add.size() == 1 &&
                                          !add_table_data.HasSourceNamespaceToAdd()),
      InvalidArgument, "Target table id can only be specified when adding a single table");

  // Verify no 'alter' command running.
  auto alter_ri = catalog_manager_.GetUniverseReplication(alter_replication_group_id);

  {
    if (alter_ri != nullptr) {
      LOG(INFO) << "Found " << alter_replication_group_id << "... Removing";
      if (alter_ri->LockForRead()->is_deleted_or_failed()) {
        // Delete previous Alter if it's completed but failed.
        master::DeleteUniverseReplicationRequestPB delete_req;
        delete_req.set_replication_group_id(alter_ri->id());
        master::DeleteUniverseReplicationResponsePB delete_resp;
        RETURN_NOT_OK(xcluster_manager_.DeleteUniverseReplication(
            &delete_req, &delete_resp, /*rpc=*/nullptr, epoch_));
        if (delete_resp.has_error()) {
          return StatusFromPB(delete_resp.error().status());
        }
      } else {
        return STATUS(
            InvalidArgument, "Alter for xCluster ReplicationGroup is already running",
            add_table_data.ToString(), MasterError(MasterErrorPB::INVALID_REQUEST));
      }
    }
  }

  // Map each table id to its corresponding bootstrap id.
  std::unordered_map<TableId, xrepl::StreamId> table_id_to_bootstrap_id;
  if (!add_table_data.source_bootstrap_ids_to_add.empty()) {
    for (size_t i = 0; i < add_table_data.source_table_ids_to_add.size(); i++) {
      table_id_to_bootstrap_id.emplace(
          add_table_data.source_table_ids_to_add[i], add_table_data.source_bootstrap_ids_to_add[i]);
    }

    // Ensure that table ids are unique. We need to do this here even though
    // the same check is performed by SetupUniverseReplication because
    // duplicate table ids can cause a bootstrap id entry in table_id_to_bootstrap_id
    // to be overwritten.
    if (table_id_to_bootstrap_id.size() != add_table_data.source_table_ids_to_add.size()) {
      return STATUS(
          InvalidArgument, "When providing bootstrap ids, the list of tables must be unique",
          add_table_data.ToString(), MasterError(MasterErrorPB::INVALID_REQUEST));
    }
  }

  // Only add new tables.  Ignore tables that are currently being replicated.
  std::unordered_set<std::string> new_tables(
      add_table_data.source_table_ids_to_add.begin(), add_table_data.source_table_ids_to_add.end());
  XClusterSetupUniverseReplicationData setup_data;
  {
    auto original_universe_l = universe->LockForRead();
    auto& original_universe_pb = original_universe_l->pb;

    for (const auto& table_id : original_universe_pb.tables()) {
      new_tables.erase(table_id);
    }
    SCHECK(
        !new_tables.empty(), InvalidArgument,
        "xCluster ReplicationGroup already contains all requested tables",
        add_table_data.ToString());

    // 1. Create an ALTER table request that mirrors the original 'setup_replication'.
    setup_data.replication_group_id = alter_replication_group_id;
    setup_data.source_masters.CopyFrom(original_universe_pb.producer_master_addresses());
    setup_data.transactional = original_universe_pb.transactional();
    setup_data.automatic_ddl_mode = original_universe_pb.db_scoped_info().automatic_ddl_mode();
  }

  if (add_table_data.HasSourceNamespaceToAdd()) {
    setup_data.source_namespace_ids.push_back(add_table_data.source_namespace_to_add.id());
    // Also fetch the corresponding target namespace id.
    NamespaceIdentifierPB target_ns_id;
    target_ns_id.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    target_ns_id.set_name(add_table_data.source_namespace_to_add.name());
    auto ns_info = VERIFY_RESULT(catalog_manager_.FindNamespace(target_ns_id));
    setup_data.target_namespace_ids.push_back(ns_info->id());
  }

  for (const auto& table_id : new_tables) {
    setup_data.source_table_ids.push_back(table_id);

    // Add bootstrap id to request if it exists.
    auto bootstrap_id = FindOrNull(table_id_to_bootstrap_id, table_id);
    if (bootstrap_id) {
      setup_data.stream_ids.push_back(*bootstrap_id);
    }

    // Add target table id to request if it exists.
    if (add_table_data.target_table_id) {
      setup_data.target_table_ids.push_back(*add_table_data.target_table_id);
    }
  }

  return RunSetupUniverseReplication(std::move(setup_data));
}

Status AlterUniverseReplicationHelper::RunSetupUniverseReplication(
    const XClusterSetupUniverseReplicationData& setup_data) {
  // Run the 'setup_replication' pipeline on the ALTER replication group.
  auto timeout =
      MonoDelta::FromMilliseconds(FLAGS_xcluster_alter_universe_replication_setup_timeout_ms);
  // TODO: This wait should be async.
  return LoggedWaitFor(
      [&]() -> Result<bool> {
        auto s = xcluster_manager_.SetupUniverseReplication(
            XClusterSetupUniverseReplicationData(setup_data), epoch_);
        if (!s.ok()) {
          if (s.IsTryAgain()) {
            // There currently exists a task running for this replication group (eg from a different
            // database). We can wait and try again.
            YB_LOG_EVERY_N_SECS(INFO, 10)
                << __func__ << " Found existing setup task for replication group "
                << setup_data.replication_group_id << ". Waiting and trying again. " << s;
            return false;
          }
          return s;
        }
        return true;
      },
      timeout, Format("Waiting for setup replication of $0", setup_data.replication_group_id));
}

}  // namespace yb::master
