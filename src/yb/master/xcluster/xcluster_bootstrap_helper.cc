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

#include "yb/master/xcluster/xcluster_bootstrap_helper.h"

#include "yb/client/yb_table_name.h"
#include "yb/gutil/bind.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/snapshot_transfer_manager.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/xcluster/xcluster_universe_replication_setup_helper.h"
#include "yb/util/backoff_waiter.h"

using namespace std::literals;

DEFINE_test_flag(bool, xcluster_fail_restore_consumer_snapshot, false,
    "In the SetupReplicationWithBootstrap flow, test failure to restore snapshot on consumer.");

// assumes the existence of a local variable bootstrap_info
#define MARK_BOOTSTRAP_FAILED_NOT_OK(s) \
  do { \
    auto&& _s = (s); \
    if (PREDICT_FALSE(!_s.ok())) { \
      MarkReplicationBootstrapFailed(bootstrap_info, _s); \
      return; \
    } \
  } while (false)

#define VERIFY_RESULT_MARK_BOOTSTRAP_FAILED(expr) \
  RESULT_CHECKER_HELPER(expr, MARK_BOOTSTRAP_FAILED_NOT_OK(ResultToStatus(__result)))

namespace yb::master {

SetupUniverseReplicationWithBootstrapHelper::SetupUniverseReplicationWithBootstrapHelper(
    Master& master, CatalogManager& catalog_manager, const LeaderEpoch& epoch)
    : master_(master),
      catalog_manager_(catalog_manager),
      sys_catalog_(*catalog_manager.sys_catalog()),
      xcluster_manager_(*catalog_manager.GetXClusterManagerImpl()),
      epoch_(epoch) {}

SetupUniverseReplicationWithBootstrapHelper::~SetupUniverseReplicationWithBootstrapHelper() {}

Status SetupUniverseReplicationWithBootstrapHelper::SetupWithBootstrap(
    Master& master, CatalogManager& catalog_manager,
    const SetupNamespaceReplicationWithBootstrapRequestPB* req,
    SetupNamespaceReplicationWithBootstrapResponsePB* resp, const LeaderEpoch& epoch) {
  scoped_refptr<SetupUniverseReplicationWithBootstrapHelper> helper =
      new SetupUniverseReplicationWithBootstrapHelper(master, catalog_manager, epoch);
  return helper->SetupWithBootstrap(req, resp);
}

/*
 * SetupNamespaceReplicationWithBootstrap is setup in 5 stages.
 * 1. Validates user input & connect to producer.
 * 2. Calls BootstrapProducer with all user tables in namespace.
 * 3. Create snapshot on producer and import onto consumer.
 * 4. Download snapshots from producer and restore on consumer.
 * 5. SetupUniverseReplication.
 */
Status SetupUniverseReplicationWithBootstrapHelper::SetupWithBootstrap(
    const SetupNamespaceReplicationWithBootstrapRequestPB* req,
    SetupNamespaceReplicationWithBootstrapResponsePB* resp) {
  // PHASE 1: Validating user input.
  RETURN_NOT_OK(ValidateReplicationBootstrapRequest(req));

  // Create entry in sys catalog.
  auto replication_id = xcluster::ReplicationGroupId(req->replication_id());
  auto transactional = req->has_transactional() ? req->transactional() : false;
  auto bootstrap_info = VERIFY_RESULT(CreateUniverseReplicationBootstrapInfoForProducer(
      replication_id, req->producer_master_addresses(), transactional));

  // Connect to producer.
  auto xcluster_rpc_result =
      bootstrap_info->GetOrCreateXClusterRpcTasks(req->producer_master_addresses());
  if (!xcluster_rpc_result.ok()) {
    auto s = ResultToStatus(xcluster_rpc_result);
    MarkReplicationBootstrapFailed(bootstrap_info, s);
    return s;
  }
  auto xcluster_rpc_tasks = std::move(*xcluster_rpc_result);

  // Get user tables in producer namespace.
  auto tables_result = xcluster_rpc_tasks->client()->ListUserTables(req->producer_namespace());
  if (!tables_result.ok()) {
    auto s = ResultToStatus(tables_result);
    MarkReplicationBootstrapFailed(bootstrap_info, s);
    return s;
  }
  auto tables = std::move(*tables_result);

  // Bootstrap producer.
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::BOOTSTRAP_PRODUCER);
  auto s = xcluster_rpc_tasks->BootstrapProducer(
      req->producer_namespace(), tables,
      Bind(
          &SetupUniverseReplicationWithBootstrapHelper::DoReplicationBootstrap,
          scoped_refptr<SetupUniverseReplicationWithBootstrapHelper>(this), replication_id,
          tables));
  if (!s.ok()) {
    MarkReplicationBootstrapFailed(bootstrap_info, s);
    return s;
  }

  return Status::OK();
}
Result<scoped_refptr<UniverseReplicationBootstrapInfo>>
SetupUniverseReplicationWithBootstrapHelper::CreateUniverseReplicationBootstrapInfoForProducer(
    const xcluster::ReplicationGroupId& replication_group_id,
    const google::protobuf::RepeatedPtrField<HostPortPB>& master_addresses, bool transactional) {
  if (catalog_manager_.GetUniverseReplicationBootstrap(replication_group_id) != nullptr) {
    return STATUS(
        InvalidArgument, Format("Bootstrap already present: $0", replication_group_id),
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  // Create an entry in the system catalog DocDB for this new universe replication.
  scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info =
      new UniverseReplicationBootstrapInfo(replication_group_id);
  bootstrap_info->mutable_metadata()->StartMutation();

  SysUniverseReplicationBootstrapEntryPB* metadata =
      &bootstrap_info->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_replication_group_id(replication_group_id.ToString());
  metadata->mutable_producer_master_addresses()->CopyFrom(master_addresses);
  metadata->set_state(SysUniverseReplicationBootstrapEntryPB::INITIALIZING);
  metadata->set_transactional(transactional);
  metadata->set_leader_term(epoch_.leader_term);
  metadata->set_pitr_count(epoch_.pitr_count);

  RETURN_NOT_OK(CheckLeaderStatus(
      sys_catalog_.Upsert(epoch_, bootstrap_info),
      "inserting universe replication bootstrap info into sys-catalog"));

  // Commit the in-memory state now that it's added to the persistent catalog.
  bootstrap_info->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Setup universe replication bootstrap from producer " << bootstrap_info->ToString();

  catalog_manager_.InsertNewUniverseReplicationInfoBootstrapInfo(*bootstrap_info);

  return bootstrap_info;
}

void SetupUniverseReplicationWithBootstrapHelper::MarkReplicationBootstrapFailed(
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info, const Status& failure_status) {
  auto l = bootstrap_info->LockForWrite();
  MarkReplicationBootstrapFailed(failure_status, &l, bootstrap_info);
}

void SetupUniverseReplicationWithBootstrapHelper::MarkReplicationBootstrapFailed(
    const Status& failure_status,
    CowWriteLock<PersistentUniverseReplicationBootstrapInfo>* bootstrap_info_lock,
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info) {
  auto& l = *bootstrap_info_lock;
  auto state = l->pb.state();
  if (state == SysUniverseReplicationBootstrapEntryPB::DELETED) {
    l.mutable_data()->pb.set_state(SysUniverseReplicationBootstrapEntryPB::DELETED_ERROR);
  } else {
    l.mutable_data()->pb.set_state(SysUniverseReplicationBootstrapEntryPB::FAILED);
    l.mutable_data()->pb.set_failed_on(state);
  }

  LOG(WARNING) << Format(
      "Replication bootstrap $0 failed: $1", bootstrap_info->ToString(), failure_status.ToString());

  bootstrap_info->SetReplicationBootstrapErrorStatus(failure_status);

  // Update sys_catalog.
  const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);

  l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
}

void SetupUniverseReplicationWithBootstrapHelper::SetReplicationBootstrapState(
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info,
    const SysUniverseReplicationBootstrapEntryPB::State& state) {
  auto l = bootstrap_info->LockForWrite();
  l.mutable_data()->set_state(state);

  // Update sys_catalog.
  const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);
  l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
}

Status SetupUniverseReplicationWithBootstrapHelper::ValidateReplicationBootstrapRequest(
    const SetupNamespaceReplicationWithBootstrapRequestPB* req) {
  SCHECK(
      !req->replication_id().empty(), InvalidArgument, "Replication ID must be provided",
      req->ShortDebugString());

  SCHECK(
      req->producer_master_addresses_size() > 0, InvalidArgument,
      "Producer master address must be provided", req->ShortDebugString());

  {
    auto l = catalog_manager_.ClusterConfig()->LockForRead();
    SCHECK(
        l->pb.cluster_uuid() != req->replication_id(), InvalidArgument,
        "Replication name cannot be the target universe UUID", req->ShortDebugString());
  }

  RETURN_NOT_OK_PREPEND(
      SetupUniverseReplicationHelper::ValidateMasterAddressesBelongToDifferentCluster(
          master_, req->producer_master_addresses()),
      req->ShortDebugString());

  auto universe =
      catalog_manager_.GetUniverseReplication(xcluster::ReplicationGroupId(req->replication_id()));
  SCHECK(
      universe == nullptr, InvalidArgument,
      Format("Can't bootstrap replication that already exists"));

  return Status::OK();
}

void SetupUniverseReplicationWithBootstrapHelper::DoReplicationBootstrap(
    const xcluster::ReplicationGroupId& replication_id,
    const std::vector<client::YBTableName>& tables,
    Result<TableBootstrapIdsMap> bootstrap_producer_result) {
  // First get the universe.
  auto bootstrap_info = catalog_manager_.GetUniverseReplicationBootstrap(replication_id);
  if (bootstrap_info == nullptr) {
    LOG(ERROR) << "UniverseReplicationBootstrap not found: " << replication_id;
    return;
  }

  // Verify the result from BootstrapProducer & update values in PB if successful.
  auto table_bootstrap_ids =
      VERIFY_RESULT_MARK_BOOTSTRAP_FAILED(std::move(bootstrap_producer_result));
  {
    auto l = bootstrap_info->LockForWrite();
    auto map = l.mutable_data()->pb.mutable_table_bootstrap_ids();
    for (const auto& [table_id, bootstrap_id] : table_bootstrap_ids) {
      (*map)[table_id] = bootstrap_id.ToString();
    }

    // Update sys_catalog.
    const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);
    l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
  }

  // Create producer snapshot.
  auto snapshot = VERIFY_RESULT_MARK_BOOTSTRAP_FAILED(
      DoReplicationBootstrapCreateSnapshot(tables, bootstrap_info));

  // Import snapshot and create consumer snapshot.
  auto tables_meta = VERIFY_RESULT_MARK_BOOTSTRAP_FAILED(
      DoReplicationBootstrapImportSnapshot(snapshot, bootstrap_info));

  // Transfer and restore snapshot.
  MARK_BOOTSTRAP_FAILED_NOT_OK(
      DoReplicationBootstrapTransferAndRestoreSnapshot(tables_meta, bootstrap_info));

  // Call SetupUniverseReplication
  SetupUniverseReplicationRequestPB replication_req;
  SetupUniverseReplicationResponsePB replication_resp;
  {
    auto l = bootstrap_info->LockForRead();
    replication_req.set_replication_group_id(l->pb.replication_group_id());
    replication_req.set_transactional(l->pb.transactional());
    replication_req.mutable_producer_master_addresses()->CopyFrom(
        l->pb.producer_master_addresses());
    for (const auto& [table_id, bootstrap_id] : table_bootstrap_ids) {
      replication_req.add_producer_table_ids(table_id);
      replication_req.add_producer_bootstrap_ids(bootstrap_id.ToString());
    }
  }

  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::SETUP_REPLICATION);
  MARK_BOOTSTRAP_FAILED_NOT_OK(SetupUniverseReplicationHelper::Setup(
      master_, catalog_manager_, &replication_req, &replication_resp, epoch_));

  LOG(INFO) << Format(
      "Successfully completed replication bootstrap for $0", replication_id.ToString());
  SetReplicationBootstrapState(bootstrap_info, SysUniverseReplicationBootstrapEntryPB::DONE);
}

Result<SnapshotInfoPB>
SetupUniverseReplicationWithBootstrapHelper::DoReplicationBootstrapCreateSnapshot(
    const std::vector<client::YBTableName>& tables,
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info) {
  LOG(INFO) << Format(
      "SetupReplicationWithBootstrap: create producer snapshot for replication $0",
      bootstrap_info->id());
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::CREATE_PRODUCER_SNAPSHOT);

  auto xcluster_rpc_tasks = VERIFY_RESULT(bootstrap_info->GetOrCreateXClusterRpcTasks(
      bootstrap_info->LockForRead()->pb.producer_master_addresses()));

  TxnSnapshotId old_snapshot_id = TxnSnapshotId::Nil();

  // Send create request and wait for completion.
  auto snapshot_result = xcluster_rpc_tasks->CreateSnapshot(tables, &old_snapshot_id);

  // If the producer failed to complete the snapshot, we still want to store the snapshot_id for
  // cleanup purposes.
  if (!old_snapshot_id.IsNil()) {
    auto l = bootstrap_info->LockForWrite();
    l.mutable_data()->set_old_snapshot_id(old_snapshot_id);

    // Update sys_catalog.
    const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);
    l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
  }

  return snapshot_result;
}

Result<std::vector<TableMetaPB>>
SetupUniverseReplicationWithBootstrapHelper::DoReplicationBootstrapImportSnapshot(
    const SnapshotInfoPB& snapshot,
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info) {
  ///////////////////////////
  // ImportSnapshotMeta
  ///////////////////////////
  LOG(INFO) << Format(
      "SetupReplicationWithBootstrap: import snapshot for replication $0", bootstrap_info->id());
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::IMPORT_SNAPSHOT);

  NamespaceMap namespace_map;
  UDTypeMap type_map;
  ExternalTableSnapshotDataMap tables_data;

  // ImportSnapshotMeta timeout should be a function of the table size.
  auto deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(10 + 1 * tables_data.size());
  auto epoch = bootstrap_info->LockForRead()->epoch();
  RETURN_NOT_OK(catalog_manager_.DoImportSnapshotMeta(
      snapshot, epoch, std::nullopt /* clone_target_namespace_name */, &namespace_map, &type_map,
      &tables_data, deadline));

  // Update sys catalog with new information.
  {
    auto l = bootstrap_info->LockForWrite();
    l.mutable_data()->set_new_snapshot_objects(namespace_map, type_map, tables_data);

    // Update sys_catalog.
    const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);
    l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
  }

  ///////////////////////////
  // CreateConsumerSnapshot
  ///////////////////////////
  LOG(INFO) << Format(
      "SetupReplicationWithBootstrap: create consumer snapshot for replication $0",
      bootstrap_info->id());
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::CREATE_CONSUMER_SNAPSHOT);

  CreateSnapshotRequestPB snapshot_req;
  CreateSnapshotResponsePB snapshot_resp;

  std::vector<TableMetaPB> tables_meta;
  for (const auto& [table_id, table_data] : tables_data) {
    if (table_data.table_meta) {
      tables_meta.push_back(std::move(*table_data.table_meta));
    }
  }

  for (const auto& table_meta : tables_meta) {
    SCHECK(
        ImportSnapshotMetaResponsePB_TableType_IsValid(table_meta.table_type()), InternalError,
        Format("Found unknown table type: $0", table_meta.table_type()));

    const auto& new_table_id = table_meta.table_ids().new_id();
    RETURN_NOT_OK(catalog_manager_.WaitForCreateTableToFinish(new_table_id, deadline));

    snapshot_req.mutable_tables()->Add()->set_table_id(new_table_id);
  }

  snapshot_req.set_add_indexes(false);
  snapshot_req.set_imported(true);
  RETURN_NOT_OK(
      catalog_manager_.CreateTransactionAwareSnapshot(snapshot_req, &snapshot_resp, deadline));

  // Update sys catalog with new information.
  {
    auto l = bootstrap_info->LockForWrite();
    l.mutable_data()->set_new_snapshot_id(TryFullyDecodeTxnSnapshotId(snapshot_resp.snapshot_id()));

    // Update sys_catalog.
    const Status s = sys_catalog_.Upsert(epoch_, bootstrap_info);
    l.CommitOrWarn(s, "updating universe replication bootstrap info in sys-catalog");
  }

  return std::vector<TableMetaPB>(tables_meta.begin(), tables_meta.end());
}

Status
SetupUniverseReplicationWithBootstrapHelper::DoReplicationBootstrapTransferAndRestoreSnapshot(
    const std::vector<TableMetaPB>& tables_meta,
    scoped_refptr<UniverseReplicationBootstrapInfo> bootstrap_info) {
  // Retrieve required data from PB.
  TxnSnapshotId old_snapshot_id = TxnSnapshotId::Nil();
  TxnSnapshotId new_snapshot_id = TxnSnapshotId::Nil();
  google::protobuf::RepeatedPtrField<HostPortPB> producer_masters;
  auto epoch = bootstrap_info->epoch();
  {
    auto l = bootstrap_info->LockForRead();
    old_snapshot_id = l->old_snapshot_id();
    new_snapshot_id = l->new_snapshot_id();
    producer_masters.CopyFrom(l->pb.producer_master_addresses());
  }

  auto xcluster_rpc_tasks =
      VERIFY_RESULT(bootstrap_info->GetOrCreateXClusterRpcTasks(producer_masters));

  // Transfer snapshot.
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::TRANSFER_SNAPSHOT);
  auto snapshot_transfer_manager = std::make_shared<SnapshotTransferManager>(
      &master_, &catalog_manager_, xcluster_rpc_tasks->client());
  RETURN_NOT_OK_PREPEND(
      snapshot_transfer_manager->TransferSnapshot(
          old_snapshot_id, new_snapshot_id, tables_meta, epoch),
      Format("Failed to transfer snapshot $0 from producer", old_snapshot_id.ToString()));

  // Restore snapshot.
  SetReplicationBootstrapState(
      bootstrap_info, SysUniverseReplicationBootstrapEntryPB::RESTORE_SNAPSHOT);
  auto restoration_id = VERIFY_RESULT(master_.snapshot_coordinator().Restore(
      new_snapshot_id, HybridTime(), epoch.leader_term));

  if (PREDICT_FALSE(FLAGS_TEST_xcluster_fail_restore_consumer_snapshot)) {
    return STATUS(Aborted, "Test failure");
  }

  // Wait for restoration to complete.
  return WaitFor(
      [this, &new_snapshot_id, &restoration_id]() -> Result<bool> {
        ListSnapshotRestorationsResponsePB resp;
        RETURN_NOT_OK(master_.snapshot_coordinator().ListRestorations(
            restoration_id, new_snapshot_id, &resp));

        SCHECK_EQ(
            resp.restorations_size(), 1, IllegalState,
            Format("Expected 1 restoration, got $0", resp.restorations_size()));
        const auto& restoration = *resp.restorations().begin();
        const auto& state = restoration.entry().state();
        return state == SysSnapshotEntryPB::RESTORED;
      },
      MonoDelta::kMax, "Waiting for restoration to finish", 100ms);
}

}  // namespace yb::master
