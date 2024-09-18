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

#include "yb/common/xcluster_util.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_util.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_replication_group.h"

namespace yb::master {

AlterUniverseReplicationHelper::AlterUniverseReplicationHelper(
    Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch)
    : master_(master),
      catalog_manager_(catalog_manager),
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
    RETURN_NOT_OK(AddTablesToReplication(original_ri, req, resp));
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
    scoped_refptr<UniverseReplicationInfo> universe, const AlterUniverseReplicationRequestPB* req,
    AlterUniverseReplicationResponsePB* resp) {
  SCHECK_GT(req->producer_table_ids_to_add_size(), 0, InvalidArgument, "No tables specified");

  if (universe->IsDbScoped()) {
    // We either add the entire namespace at once, or one table at a time as they get created.
    if (req->has_producer_namespace_to_add()) {
      SCHECK(
          !req->producer_namespace_to_add().id().empty(), InvalidArgument, "Invalid Namespace Id");
      SCHECK(
          !req->producer_namespace_to_add().name().empty(), InvalidArgument,
          "Invalid Namespace name");
      SCHECK_EQ(
          req->producer_namespace_to_add().database_type(), YQLDatabase::YQL_DATABASE_PGSQL,
          InvalidArgument, "Invalid Namespace database_type");
    } else {
      SCHECK_EQ(
          req->producer_table_ids_to_add_size(), 1, InvalidArgument,
          "When adding more than table to a DB scoped replication the namespace info must also be "
          "provided");
    }
  } else {
    SCHECK(
        !req->has_producer_namespace_to_add(), InvalidArgument,
        "Cannot add namespaces to non DB scoped replication");
  }

  xcluster::ReplicationGroupId alter_replication_group_id(xcluster::GetAlterReplicationGroupId(
      xcluster::ReplicationGroupId(req->replication_group_id())));

  // If user passed in bootstrap ids, check that there is a bootstrap id for every table.
  SCHECK(
      req->producer_bootstrap_ids_to_add_size() == 0 ||
          req->producer_table_ids_to_add_size() == req->producer_bootstrap_ids_to_add().size(),
      InvalidArgument, "Number of bootstrap ids must be equal to number of tables",
      req->ShortDebugString());

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
        Status s = xcluster_manager_.DeleteUniverseReplication(
            &delete_req, &delete_resp, /*rpc=*/nullptr, epoch_);
        if (!s.ok()) {
          if (delete_resp.has_error()) {
            resp->mutable_error()->Swap(delete_resp.mutable_error());
            return s;
          }
          return SetupError(resp->mutable_error(), s);
        }
      } else {
        return STATUS(
            InvalidArgument, "Alter for xCluster ReplicationGroup is already running",
            req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
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
  std::unordered_set<std::string> new_tables(tid_iter.begin(), tid_iter.end());
  auto original_universe_l = universe->LockForRead();
  auto& original_universe_pb = original_universe_l->pb;

  for (const auto& table_id : original_universe_pb.tables()) {
    new_tables.erase(table_id);
  }
  if (new_tables.empty()) {
    return STATUS(
        InvalidArgument, "xCluster ReplicationGroup already contains all requested tables",
        req->ShortDebugString(), MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // 1. create an ALTER table request that mirrors the original 'setup_replication'.
  master::SetupUniverseReplicationRequestPB setup_req;
  master::SetupUniverseReplicationResponsePB setup_resp;
  setup_req.set_replication_group_id(alter_replication_group_id.ToString());
  setup_req.mutable_producer_master_addresses()->CopyFrom(
      original_universe_pb.producer_master_addresses());
  setup_req.set_transactional(original_universe_pb.transactional());

  if (req->has_producer_namespace_to_add()) {
    *setup_req.add_producer_namespaces() = req->producer_namespace_to_add();
  }

  for (const auto& table_id : new_tables) {
    setup_req.add_producer_table_ids(table_id);

    // Add bootstrap id to request if it exists.
    auto bootstrap_id = FindOrNull(table_id_to_bootstrap_id, table_id);
    if (bootstrap_id) {
      setup_req.add_producer_bootstrap_ids(*bootstrap_id);
    }
  }

  // 2. run the 'setup_replication' pipeline on the ALTER Table
  Status s =
      xcluster_manager_.SetupUniverseReplication(&setup_req, &setup_resp, /*rpc=*/nullptr, epoch_);
  if (!s.ok()) {
    if (setup_resp.has_error()) {
      resp->mutable_error()->Swap(setup_resp.mutable_error());
      return s;
    }
    return SetupError(resp->mutable_error(), s);
  }

  return Status::OK();
}

}  // namespace yb::master
