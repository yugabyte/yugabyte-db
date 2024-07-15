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

#include <gtest/gtest.h>

#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/async_snapshot_tasks.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tools/yb-admin_util.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/cast.h"
#include "yb/util/pb_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DECLARE_bool(enable_ysql);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint32(default_snapshot_retention_hours);
DECLARE_bool(TEST_tablet_verify_flushed_frontier_after_modifying);
DECLARE_bool(TEST_treat_hours_as_milliseconds_for_snapshot_expiry);

namespace yb {

using std::shared_ptr;
using std::unique_ptr;
using std::set;
using std::vector;
using std::string;

using google::protobuf::RepeatedPtrField;

using client::YBTableName;
using master::MasterBackupProxy;
using master::SysRowEntry;
using master::SysRowEntryType;
using master::BackupRowEntryPB;
using master::TableInfo;
using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using tablet::TabletPeer;
using tserver::MiniTabletServer;

using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::IdPairPB;
using master::ImportSnapshotMetaRequestPB;
using master::ImportSnapshotMetaResponsePB;
using master::ImportSnapshotMetaResponsePB_TableMetaPB;
using master::IsCreateTableDoneRequestPB;
using master::IsCreateTableDoneResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::ListSnapshotRestorationsRequestPB;
using master::ListSnapshotRestorationsResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::SnapshotInfoPB;
using master::SysNamespaceEntryPB;
using master::SysTablesEntryPB;
using master::SysSnapshotEntryPB;
using master::TableIdentifierPB;

const YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "snapshot_test_table");

class SnapshotTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 5;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_verify_flushed_frontier_after_modifying) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = false;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    messenger_ = ASSERT_RESULT(
        MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    rpc::ProxyCache proxy_cache(messenger_.get());
    proxy_ddl_.reset(new master::MasterDdlProxy(
        &proxy_cache, cluster_->mini_master()->bound_rpc_addr()));
    proxy_backup_.reset(new MasterBackupProxy(
        &proxy_cache, cluster_->mini_master()->bound_rpc_addr()));

    // Connect to the cluster.
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  void DoTearDown() override {
    Result<bool> exist = client_->TableExists(kTableName);
    ASSERT_OK(exist);

    if (exist.get()) {
      ASSERT_OK(client_->DeleteTable(kTableName));
    }

    client_.reset();

    messenger_->Shutdown();

    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    YBMiniClusterTestBase::DoTearDown();
  }

  RpcController* ResetAndGetController() {
    controller_.Reset();
    controller_.set_timeout(10s);
    return &controller_;
  }

  void CheckAllSnapshots(
      const std::map<TxnSnapshotId, SysSnapshotEntryPB::State>& snapshot_info) {
    ListSnapshotsRequestPB list_req;
    ListSnapshotsResponsePB list_resp;

    LOG(INFO) << "Requested available snapshots.";
    const Status s = proxy_backup_->ListSnapshots(
        list_req, &list_resp, ResetAndGetController());

    ASSERT_TRUE(s.ok());
    SCOPED_TRACE(list_resp.DebugString());
    ASSERT_FALSE(list_resp.has_error());

    LOG(INFO) << "Number of snapshots: " << list_resp.snapshots_size();
    ASSERT_EQ(list_resp.snapshots_size(), snapshot_info.size());

    // Current snapshot is available for non-transaction aware snapshots only.
    ASSERT_FALSE(list_resp.has_current_snapshot_id());

    for (int i = 0; i < list_resp.snapshots_size(); ++i) {
      LOG(INFO) << "Snapshot " << i << ": " << list_resp.snapshots(i).DebugString();
      auto id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(list_resp.snapshots(i).id()));

      auto it = snapshot_info.find(id);
      ASSERT_NE(it, snapshot_info.end()) << "Unknown snapshot: " << id;
      ASSERT_EQ(list_resp.snapshots(i).entry().state(), it->second);
    }
  }

  template <typename THandler>
  Status WaitTillComplete(const string& handler_name, THandler handler, MonoDelta timeout = 30s) {
    return LoggedWaitFor(handler, timeout, handler_name, 100ms, 1.5);
  }

  Status WaitForSnapshotOpDone(
      const string& op_name, const TxnSnapshotId& snapshot_id, MonoDelta timeout = 30s) {
    return WaitTillComplete(
        op_name,
        [this, &snapshot_id, &op_name]() -> Result<bool> {
          ListSnapshotsRequestPB list_req;
          ListSnapshotsResponsePB list_resp;
          list_req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());

          Status s = proxy_backup_->ListSnapshots(
              list_req, &list_resp, ResetAndGetController());
          if (s.ok() && list_resp.has_error()) {
            s = StatusFromPB(list_resp.error().status());
          }
          if (op_name == "IsSnapshotDeleted" && !s.ok() && s.IsNotFound()) {
            return true;
          }
          RETURN_NOT_OK(s);
          SCHECK_FORMAT(list_resp.snapshots_size() == 1, IllegalState,
              "Wrong number of snapshots: ", list_resp.snapshots_size());
          if (op_name == "IsSnapshotDeleted") {
            return list_resp.snapshots(0).entry().state() == SysSnapshotEntryPB::DELETED;
          }
          return list_resp.snapshots(0).entry().state() == SysSnapshotEntryPB::COMPLETE;
        }, timeout);
  }

  Status WaitForSnapshotRestorationDone(const TxnSnapshotRestorationId& restoration_id) {
    return WaitTillComplete(
        "IsRestorationDone",
        [this, &restoration_id]() -> Result<bool> {
          SCHECK(restoration_id, InvalidArgument, "Invalid restoration id");
          ListSnapshotRestorationsRequestPB list_req;
          ListSnapshotRestorationsResponsePB list_resp;
          list_req.set_restoration_id(restoration_id.data(), restoration_id.size());

          RETURN_NOT_OK(proxy_backup_->ListSnapshotRestorations(
              list_req, &list_resp, ResetAndGetController()));
          if (list_resp.has_status()) {
            auto status = StatusFromPB(list_resp.status());
            // If master is not yet ready, just wait and try another one.
            if (status.IsServiceUnavailable()) {
              return false;
            }
            RETURN_NOT_OK(status);
          }
          SCHECK_FORMAT(list_resp.restorations_size() == 1, IllegalState,
              "Wrong number of restorations: ", list_resp.restorations_size());
          return list_resp.restorations(0).entry().state() == SysSnapshotEntryPB::RESTORED;
        });
  }

  Status WaitForCreateTableDone(const YBTableName& table_name) {
    return WaitTillComplete(
        "IsCreateTableDone",
        [this, &table_name]() -> Result<bool> {
          IsCreateTableDoneRequestPB req;
          IsCreateTableDoneResponsePB resp;
          table_name.SetIntoTableIdentifierPB(req.mutable_table());

          RETURN_NOT_OK(proxy_ddl_->IsCreateTableDone(req, &resp, ResetAndGetController()));
          SCHECK(!resp.has_error(), IllegalState, "Expected response without error");
          SCHECK(resp.has_done(), IllegalState, "Response must have 'done'");
          return resp.done();
        });
  }

  TxnSnapshotId CreateSnapshot(int32_t retention_duration_hours = 0) {
    CreateSnapshotRequestPB req;
    CreateSnapshotResponsePB resp;
    req.set_transaction_aware(true);
    TableIdentifierPB* const table = req.mutable_tables()->Add();
    table->set_table_name(kTableName.table_name());
    table->mutable_namespace_()->set_name(kTableName.namespace_name());
    if (retention_duration_hours > 0) {
      req.set_retention_duration_hours(retention_duration_hours);
    }

    // Check the request.
    EXPECT_OK(proxy_backup_->CreateSnapshot(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    EXPECT_FALSE(resp.has_error());
    EXPECT_TRUE(resp.has_snapshot_id());
    const auto snapshot_id = EXPECT_RESULT(FullyDecodeTxnSnapshotId(resp.snapshot_id()));

    LOG(INFO) << "Started snapshot creation: ID=" << snapshot_id;

    // Check the snapshot creation is complete.
    EXPECT_OK(WaitForSnapshotOpDone("IsCreateSnapshotDone", snapshot_id));

    CheckAllSnapshots(
        {
            { snapshot_id, SysSnapshotEntryPB::COMPLETE }
        });

    return snapshot_id;
  }

  Status DeleteSnapshot(const TxnSnapshotId& snapshot_id) {
    master::DeleteSnapshotResponsePB resp;
    auto s = client_->DeleteSnapshot(snapshot_id, &resp);
    if (s.ok() && resp.has_error()) {
      s = StatusFromPB(resp.error().status());
    }
    return s;
  }

  Result<TxnSnapshotRestorationId> RestoreSnapshot(const TxnSnapshotId& snapshot_id) {
    RestoreSnapshotRequestPB req;
    RestoreSnapshotResponsePB resp;
    req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());
    RETURN_NOT_OK(proxy_backup_->RestoreSnapshot(req, &resp, ResetAndGetController()));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return TryFullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
  }

  Status DeleteSnapshotAndWait(const TxnSnapshotId& snapshot_id) {
    RETURN_NOT_OK(DeleteSnapshot(snapshot_id));
    LOG(INFO) << "Started snapshot deletion " << snapshot_id;
    return WaitForSnapshotOpDone("IsSnapshotDeleted", snapshot_id);
  }

  void VerifySnapshotFiles(const TxnSnapshotId& snapshot_id) {
    std::unordered_map<TabletId, OpId> last_tablet_op;

    size_t max_tablets = 0;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
      auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();
      max_tablets = std::max(max_tablets, ts_tablet_peers.size());
      for (const auto& tablet_peer : ts_tablet_peers) {
        EXPECT_OK(tablet_peer->WaitUntilConsensusRunning(15s));
        last_tablet_op[tablet_peer->tablet_id()].MakeAtLeast(
            CHECK_RESULT(tablet_peer->GetConsensus())->GetLastReceivedOpId());
      }
    }

    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      MiniTabletServer* ts = cluster_->mini_tablet_server(i);
      auto predicate = [max_tablets, ts]() {
        return ts->server()->tablet_manager()->GetTabletPeers().size() >= max_tablets;
      };
      ASSERT_OK(WaitFor(predicate, 15s, "Wait for peers to be up"));
    }

    // Check snapshot files existence.
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
      auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();
      SCOPED_TRACE(Format("TServer: $0", i));

      // Iterate through all available tablets (on this TabletServer), because there is
      // only one table here (testtb). And snapshot was created for this table.
      for (const auto& tablet_peer : ts_tablet_peers) {
        SCOPED_TRACE(Format("Tablet: $0", tablet_peer->tablet_id()));
        auto last_op_id = last_tablet_op[tablet_peer->tablet_id()];
        ASSERT_OK(WaitFor([tablet_peer, last_op_id]() {
            EXPECT_OK(tablet_peer->WaitUntilConsensusRunning(15s));
            return CHECK_RESULT(tablet_peer->GetConsensus())->GetLastCommittedOpId() >= last_op_id;
          },
          15s,
          "Wait for op id commit"
        ));
        FsManager* const fs = tablet_peer->tablet_metadata()->fs_manager();
        const auto rocksdb_dir = tablet_peer->tablet_metadata()->rocksdb_dir();
        const auto top_snapshots_dir = tablet_peer->tablet_metadata()->snapshots_dir();
        const auto snapshot_dir = JoinPathSegments(top_snapshots_dir, snapshot_id.ToString());

        LOG(INFO) << "Checking tablet snapshot folder: " << snapshot_dir;
        ASSERT_TRUE(fs->Exists(rocksdb_dir));
        ASSERT_TRUE(fs->Exists(top_snapshots_dir));
        ASSERT_TRUE(fs->Exists(snapshot_dir));
        // Check existence of snapshot files:
        auto list = ASSERT_RESULT(fs->ListDir(snapshot_dir));
        ASSERT_TRUE(std::find(list.begin(), list.end(), "CURRENT") != list.end());
        bool has_manifest = false;
        for (const auto& file : list) {
          SCOPED_TRACE("File: " + file);
          if (file.find("MANIFEST-") == 0) {
            has_manifest = true;
          }
          if (file.find(".sst") != std::string::npos) {
            auto snapshot_path = JoinPathSegments(snapshot_dir, file);
            auto rocksdb_path = JoinPathSegments(rocksdb_dir, file);
            auto snapshot_inode = ASSERT_RESULT(fs->env()->GetFileINode(snapshot_path));
            auto rocksdb_inode = ASSERT_RESULT(fs->env()->GetFileINode(rocksdb_path));
            ASSERT_EQ(snapshot_inode, rocksdb_inode);
            LOG(INFO) << "Snapshot: " << snapshot_path << " vs " << rocksdb_path
                      << ", inode: " << snapshot_inode << " vs " << rocksdb_inode;
          }
        }
        ASSERT_TRUE(has_manifest);
      }
    }
  }

  TestWorkload CreateDefaultWorkload() {
    TestWorkload workload(cluster_.get());
    workload.set_table_name(kTableName);
    workload.set_sequential_write(true);
    workload.set_insert_failures_allowed(false);
    workload.set_num_write_threads(1);
    workload.set_write_batch_size(10);
    return workload;
  }

  TestWorkload SetupWorkload() {
    auto workload = CreateDefaultWorkload();
    workload.Setup();
    return workload;
  }

  Status TableHidden(const TableId& table_id) {
    LOG(INFO) << "Verifying table is hidden";
    // Check that the table is hidden on the master.
    auto master_leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto table = VERIFY_RESULT(master_leader->catalog_manager_impl().FindTableById(table_id));
    LOG(INFO) << "Table info " << table->id();
    if (!table->AreAllTabletsHidden()) {
      return STATUS_FORMAT(IllegalState, "Tablets of table $0 not hidden on master", table_id);
    }
    if (!table->LockForRead()->is_hidden()) {
      return STATUS_FORMAT(IllegalState, "Table $0 not hidden on master", table_id);
    }

    // Check that the table is hidden on the tservers.
    for (auto ts : cluster_->mini_tablet_servers()) {
      auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();

      // Iterate through all available tablets (on this TabletServer).
      for (std::shared_ptr<TabletPeer>& tablet_peer : ts_tablet_peers) {
        if (tablet_peer->tablet_metadata()->table_id() == table_id &&
            !tablet_peer->tablet_metadata()->hidden()) {
          return STATUS_FORMAT(
              IllegalState, "Tablet $0 of table $1 not hidden on tserver",
              tablet_peer->tablet_id(), table_id);
        }
      }
    }
    return Status::OK();
  }

  Result<bool> IsTableDropped(const TableId& table_id) {
    LOG(INFO) << "Verifying table is dropped";
    // Check that the table is dropped on the master.
    auto master_leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto table = VERIFY_RESULT(master_leader->catalog_manager_impl().FindTableById(table_id));
    LOG(INFO) << "Table info " << table->id();
    if (!table->AreAllTabletsDeleted()) {
      return false;
    }
    if (!table->LockForRead()->is_deleted()) {
      return false;
    }

    // Check that the table is deleted on the tservers.
    for (auto ts : cluster_->mini_tablet_servers()) {
      auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();
      for (const auto& peer : ts_tablet_peers) {
        if (peer->tablet_metadata()->table_id() == table_id) {
          LOG(INFO) << "Tablet peer " << peer->tablet_id() << " is not yet deleted";
          return false;
        }
      }
    }
    return true;
  }

  Status SnapshotCoversTablets(
      const TxnSnapshotId& snapshot_id, const TableId& table_id) {
    auto master_leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto table = VERIFY_RESULT(master_leader->catalog_manager_impl().GetTableById(table_id));
    auto tablets = table->GetTablets(master::IncludeInactive::kTrue);
    auto* coordinator = down_cast<master::MasterSnapshotCoordinator*>(
        &master_leader->catalog_manager_impl().snapshot_coordinator());
    for (const auto& tablet : tablets) {
      if (!coordinator->TEST_IsTabletCoveredBySnapshot(tablet->id(), snapshot_id)) {
        return STATUS_FORMAT(
            IllegalState, "Covering snapshot for tablet $0 not found", tablet->id());
      }
    }
    return Status::OK();
  }

 protected:
  std::unique_ptr<Messenger> messenger_;
  unique_ptr<MasterBackupProxy> proxy_backup_;
  unique_ptr<master::MasterDdlProxy> proxy_ddl_;
  RpcController controller_;
  std::unique_ptr<client::YBClient> client_;
};

TEST_F(SnapshotTest, CreateSnapshot) {
  SetupWorkload(); // Used to create table

  // Check tablet folders before the snapshot creation.
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
    auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();

    // Iterate through all available tablets (on this TabletServer).
    // There is only one table here (testtb).
    for (std::shared_ptr<TabletPeer>& tablet_peer : ts_tablet_peers) {
      FsManager* const fs = tablet_peer->tablet_metadata()->fs_manager();
      const string rocksdb_dir = tablet_peer->tablet_metadata()->rocksdb_dir();
      const string top_snapshots_dir = tablet_peer->tablet_metadata()->snapshots_dir();

      ASSERT_TRUE(fs->Exists(rocksdb_dir));
      ASSERT_TRUE(fs->Exists(top_snapshots_dir));
    }
  }

  CheckAllSnapshots({});

  // Check CreateSnapshot().
  const auto snapshot_id = CreateSnapshot();

  ASSERT_NO_FATALS(VerifySnapshotFiles(snapshot_id));

  ASSERT_OK(cluster_->RestartSync());
}

// Tests that a snapshot hides a table that is dropped subsequently.
// Until the snapshot is deleted, the table is hidden and once the
// snapshot gets deleted, the table is subsequently deleted.
TEST_F(SnapshotTest, HideTablesCoveredBySnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 500;
  auto workload = SetupWorkload(); // Used to create table

  // Get the table id.
  auto master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto table = master_leader->catalog_manager_impl().GetTableInfoFromNamespaceNameAndTableName(
      workload.table_name().namespace_type(), workload.table_name().namespace_name(),
      workload.table_name().table_name());

  // Create Snapshot.
  const auto snapshot_id = CreateSnapshot();

  // Verify the snapshot covers tablets on the master.
  ASSERT_OK(SnapshotCoversTablets(snapshot_id, table->id()));
  // Even after restart the cover should be present.
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(SnapshotCoversTablets(snapshot_id, table->id()));

  // Drop table.
  ASSERT_OK(client_->DeleteTable(workload.table_name(), true));

  // Verify table is hidden on the masters as well as tservers.
  ASSERT_OK(TableHidden(table->id()));
  // Even after restart the table should remain hidden.
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(TableHidden(table->id()));
  // Delete the snapshot and verify that the table gets eventually deleted.
  ASSERT_OK(DeleteSnapshotAndWait(snapshot_id));
  // The cover should be removed.
  ASSERT_NOK(SnapshotCoversTablets(snapshot_id, table->id()));
  ASSERT_OK(WaitFor(
      std::bind(&SnapshotTest::IsTableDropped, this, table->id()), 120s, "IsTableDropped"));
}

// Tests that snapshot TTL field is set in snapshots.
TEST_F(SnapshotTest, SnapshotTtlBasic) {
  SetupWorkload();
  auto snapshot_id = CreateSnapshot(5 /* retention_duration_hours */);
  auto expiry_equals_cb = [this](const TxnSnapshotId& snapshot_id, int32_t expiry_hrs) -> Status {
    ListSnapshotsRequestPB list_req;
    ListSnapshotsResponsePB list_resp;
    list_req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());

    RETURN_NOT_OK(proxy_backup_->ListSnapshots(
        list_req, &list_resp, ResetAndGetController()));
    if (list_resp.snapshots_size() != 1) {
      return STATUS(IllegalState, "Expect only one snapshot of a given id");
    }
    if (list_resp.snapshots(0).entry().retention_duration_hours() != expiry_hrs) {
      return STATUS(IllegalState, "Ttl in snapshot metadata is not equal to set expiry");
    }
    return Status::OK();
  };
  ASSERT_OK(expiry_equals_cb(snapshot_id, 5));
  ASSERT_OK(DeleteSnapshotAndWait(snapshot_id));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_default_snapshot_retention_hours) = 12;
  // Default value controlled by gflag should be set.
  snapshot_id = CreateSnapshot();
  ASSERT_OK(expiry_equals_cb(snapshot_id, FLAGS_default_snapshot_retention_hours));
}

// Tests that snapshot TTL is honoured and that snapshots get expired.
TEST_F(SnapshotTest, SnapshotTtl) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 500;
  // To speed up tests.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_treat_hours_as_milliseconds_for_snapshot_expiry) = true;
  SetupWorkload();
  // This will actually be treated as 1000 ms instead of 1000 hours due to the flag.
  const auto snapshot_id = CreateSnapshot(1000 /* retention_duration_hours */);
  // Give three cycles to the snapshot coordinator to cleanup the snapshot.
  ASSERT_OK(WaitForSnapshotOpDone(
      "IsSnapshotDeleted", snapshot_id,
      MonoDelta::FromMilliseconds(FLAGS_snapshot_coordinator_poll_interval_ms * 3)));
}

TEST_F(SnapshotTest, SnapshotTtlWithRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 500;
  // To speed up tests.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_treat_hours_as_milliseconds_for_snapshot_expiry) = true;
  SetupWorkload();
  // This will actually be treated as 5000 ms instead of 5000 hours due to the flag.
  const auto snapshot_id = CreateSnapshot(5000 /* retention_duration_hours */);
  // Restart.
  ASSERT_OK(cluster_->RestartSync());
  // Give four cycles to the snapshot coordinator to cleanup the snapshot.
  ASSERT_OK(WaitForSnapshotOpDone(
      "IsSnapshotDeleted", snapshot_id,
      MonoDelta::FromMilliseconds(FLAGS_snapshot_coordinator_poll_interval_ms * 4)));
}

// Tests that deleted objects are eventually cleaned up even if the client
// fails to invoke delete snapshot.
TEST_F(SnapshotTest, EventuallyDeleteTablesCoveredBySnapshot) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 500;
  // To speed up tests.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_treat_hours_as_milliseconds_for_snapshot_expiry) = true;

  auto workload = SetupWorkload(); // Used to create table

  // Get the table id.
  auto master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto table = master_leader->catalog_manager_impl().GetTableInfoFromNamespaceNameAndTableName(
      workload.table_name().namespace_type(), workload.table_name().namespace_name(),
      workload.table_name().table_name());

  // This will actually be treated as 1000 ms instead of 1000 hours due to the flag.
  CreateSnapshot(1000 /* retention_duration_hours */);

  // Drop table.
  ASSERT_OK(client_->DeleteTable(workload.table_name(), true));
  ASSERT_OK(WaitFor(
      std::bind(&SnapshotTest::IsTableDropped, this, table->id()), 120s, "IsTableDropped"));
}

TEST_F(SnapshotTest, RestoreSnapshot) {
  TestWorkload workload = SetupWorkload();
  workload.Start();

  workload.WaitInserted(100);

  CheckAllSnapshots({});

  int64_t min_inserted = workload.rows_inserted();
  // Check CreateSnapshot().
  const auto snapshot_id = CreateSnapshot();
  int64_t max_inserted = workload.rows_inserted();

  workload.WaitInserted(max_inserted + 100);

  workload.StopAndJoin();

  // Check RestoreSnapshot().
  TxnSnapshotRestorationId restoration_id = TxnSnapshotRestorationId::Nil();
  {
    RestoreSnapshotRequestPB req;
    RestoreSnapshotResponsePB resp;
    req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());

    // Check the request.
    ASSERT_OK(proxy_backup_->RestoreSnapshot(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_TRUE(resp.has_restoration_id());
    restoration_id = TryFullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
    LOG(INFO) << "Started snapshot restoring: ID=" << snapshot_id
              << " Restoration ID=" << restoration_id;
  }

  // Check the snapshot restoring is complete.
  ASSERT_OK(WaitForSnapshotRestorationDone(restoration_id));

  CheckAllSnapshots(
      {
          { snapshot_id, SysSnapshotEntryPB::COMPLETE }
      });

  client::TableHandle table;
  ASSERT_OK(table.Open(kTableName, client_.get()));
  int64_t inserted_before_min = 0;
  for (const auto& row : client::TableRange(table)) {
    auto key = row.column(0).int32_value();
    ASSERT_LE(key, max_inserted);
    ASSERT_GE(key, 1);
    if (key <= min_inserted) {
      ++inserted_before_min;
    }
  }
  ASSERT_EQ(inserted_before_min, min_inserted);
}

TEST_F(SnapshotTest, SnapshotRemoteBootstrap) {
  auto* const ts0 = cluster_->mini_tablet_server(0);

  // Shutdown one node, so remote bootstrap will be required after its start.
  ts0->Shutdown();
  auto se = ScopeExit([ts0] {
    // Restart the node in the end, because we need to perform table deletion, etc.
    LOG(INFO) << "Restarting the stopped tserver";
    ASSERT_OK(ts0->RestartStoppedServer());
    ASSERT_OK(ts0->WaitStarted());
  });

  TxnSnapshotId snapshot_id = TxnSnapshotId::Nil();
  {
    LOG(INFO) << "Setting up workload";
    TestWorkload workload = SetupWorkload();
    workload.Start();
    auto se = ScopeExit([&workload] {
      LOG(INFO) << "Stopping workload";
      workload.StopAndJoin();
    });
    LOG(INFO) << "Waiting for data to be inserted";
    workload.WaitInserted(1000);

    LOG(INFO) << "Creating snapshot";
    snapshot_id = CreateSnapshot();

    LOG(INFO) << "Wait to make sure that we would need remote bootstrap";
    std::this_thread::sleep_for(std::chrono::seconds(FLAGS_log_min_seconds_to_retain) * 1.1);

    // Workload will stop here at the latest.
  }

  // Flushing tablets for all tablet servers except for the one that we stopped.
  for (size_t i = 1; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->FlushTablets());
  }

  ASSERT_OK(cluster_->CleanTabletLogs());
  const MonoDelta kTimeout = 20s * kTimeMultiplier;
  ASSERT_OK(ts0->Start());
  // Get the map of tserverdetails and the set of all_tablet_ids in order to use them
  // for ts0 to catch up with the rest of replicas after remote bootstrapping
  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto client = ASSERT_RESULT(cluster_->CreateClient());
  master::MasterClusterProxy master_proxy(&client->proxy_cache(), leader_master->bound_rpc_addr());
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, &client->proxy_cache()));
  std::set<TabletId> all_tablet_ids = itest::GetClusterTabletIds(cluster_.get());
  // wait for all replicas of ts0 to be bootstrapped and catch up
  for (auto tablet_id : all_tablet_ids) {
    ASSERT_OK(WaitForAllPeersToCatchup(tablet_id, TServerDetailsVector(ts_map), kTimeout));
  }
  ASSERT_NO_FATALS(VerifySnapshotFiles(snapshot_id));
}

TEST_F(SnapshotTest, ImportSnapshotMeta) {
  TestWorkload workload = SetupWorkload();
  workload.Start();
  workload.WaitInserted(100);

  CheckAllSnapshots({});

  Result<bool> result_exist = client_->TableExists(kTableName);
  ASSERT_OK(result_exist);
  ASSERT_TRUE(result_exist.get());

  // Check CreateSnapshot().
  const auto snapshot_id = CreateSnapshot();

  workload.StopAndJoin();

  // Check the snapshot creating is complete.
  ASSERT_OK(WaitForSnapshotOpDone("IsCreateSnapshotDone", snapshot_id));

  CheckAllSnapshots(
      {
          { snapshot_id, SysSnapshotEntryPB::COMPLETE }
      });

  ListSnapshotsRequestPB list_req;
  ListSnapshotsResponsePB list_resp;
  list_req.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());
  list_req.set_prepare_for_backup(true);
  ASSERT_OK(proxy_backup_->ListSnapshots(list_req, &list_resp, ResetAndGetController()));
  LOG(INFO) << "Requested available snapshots.";
  SCOPED_TRACE(list_resp.DebugString());
  ASSERT_FALSE(list_resp.has_error());

  ASSERT_EQ(list_resp.snapshots_size(), 1);
  const SnapshotInfoPB& snapshot = list_resp.snapshots(0);

  // Get snapshot items names.
  int old_table_num_tablets = 0;
  string old_table_name, old_namespace_name;

  for (const BackupRowEntryPB& backup_entry : snapshot.backup_entries()) {
    const SysRowEntry& entry = backup_entry.entry();
    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: { // Get NAMESPACE name.
        SysNamespaceEntryPB meta;
        const string& data = entry.data();
        ASSERT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));
        ASSERT_TRUE(old_namespace_name.empty()); // One namespace allowed.
        old_namespace_name = meta.name();
        break;
      }
      case SysRowEntryType::TABLE: { // Recreate TABLE.
        SysTablesEntryPB meta;
        const string& data = entry.data();
        ASSERT_OK(pb_util::ParseFromArray(&meta, to_uchar_ptr(data.data()), data.size()));
        ASSERT_TRUE(old_table_name.empty()); // One table allowed.
        old_table_name = meta.name();
        break;
      }
      case SysRowEntryType::TABLET:
        old_table_num_tablets += 1;
        break;
      default:
        ASSERT_OK(STATUS_SUBSTITUTE(
            IllegalState, "Unexpected snapshot entry type $0", entry.type()));
    }
  }

  LOG(INFO) << "Deleting table & namespace: " << kTableName.ToString();
  ASSERT_OK(DeleteSnapshotAndWait(snapshot_id));
  ASSERT_OK(client_->DeleteTable(kTableName));
  ASSERT_OK(client_->DeleteNamespace(kTableName.namespace_name()));

  result_exist = client_->TableExists(kTableName);
  ASSERT_OK(result_exist);
  ASSERT_FALSE(result_exist.get());

  result_exist = client_->NamespaceExists(kTableName.namespace_name());
  ASSERT_OK(result_exist);
  ASSERT_FALSE(result_exist.get());

  // Check ImportSnapshotMeta().
  {
    ImportSnapshotMetaRequestPB req;
    ImportSnapshotMetaResponsePB resp;
    *req.mutable_snapshot() = snapshot;

    // Check the request.
    ASSERT_OK(proxy_backup_->ImportSnapshotMeta(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    LOG(INFO) << "Imported snapshot: ID=" << snapshot_id << ". ID map:";

    const RepeatedPtrField<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta =
        resp.tables_meta();

    for (int i = 0; i < tables_meta.size(); ++i) {
      const ImportSnapshotMetaResponsePB_TableMetaPB& table_meta = tables_meta.Get(i);

      const IdPairPB& ns_pair = table_meta.namespace_ids();
      LOG(INFO) << "Keyspace: " << ns_pair.old_id() << " -> " << ns_pair.new_id();
      ASSERT_NE(ns_pair.old_id(), ns_pair.new_id());

      const string new_namespace_name = cluster_->mini_master()->catalog_manager().
          GetNamespaceName(ns_pair.new_id());
      ASSERT_EQ(old_namespace_name, new_namespace_name);

      const IdPairPB& table_pair = table_meta.table_ids();
      LOG(INFO) << "Table: " << table_pair.old_id() << " -> " << table_pair.new_id();
      ASSERT_NE(table_pair.old_id(), table_pair.new_id());
      scoped_refptr<TableInfo> info = cluster_->mini_master()->catalog_manager().
          GetTableInfo(table_pair.new_id());
      ASSERT_EQ(old_table_name, info->name());
      auto tablets = info->GetTablets();
      ASSERT_EQ(old_table_num_tablets, tablets.size());

      const RepeatedPtrField<IdPairPB>& tablets_map = table_meta.tablets_ids();
      for (int j = 0; j < tablets_map.size(); ++j) {
        const IdPairPB& pair = tablets_map.Get(j);
        LOG(INFO) << "Tablet " << j << ": " << pair.old_id() << " -> " << pair.new_id();
        ASSERT_NE(pair.old_id(), pair.new_id());
      }
    }
  }

  // Check imported table creating is complete.
  ASSERT_OK(WaitForCreateTableDone(kTableName));

  result_exist = client_->TableExists(kTableName);
  ASSERT_OK(result_exist);
  ASSERT_TRUE(result_exist.get());

  result_exist = client_->NamespaceExists(kTableName.namespace_name());
  ASSERT_OK(result_exist);
  ASSERT_TRUE(result_exist.get());

  LOG(INFO) << "Test ImportSnapshotMeta finished.";
}

class RestoreAndDeleteValidationTest : public SnapshotTest {
 public:
  TxnSnapshotId CreateTableAndSnapshotDuringWrites(int insertions = 100) {
    auto workload = CreateDefaultWorkload();
    workload.Setup();
    workload.Start();
    workload.WaitInserted(insertions);
    const auto snapshot_id = CreateSnapshot();
    int64_t max_inserted = workload.rows_inserted();
    workload.WaitInserted(max_inserted + insertions);
    workload.StopAndJoin();
    return snapshot_id;
  }
};

TEST_F(RestoreAndDeleteValidationTest, DeleteDuringRestore) {
  auto snapshot_id = CreateTableAndSnapshotDuringWrites();
  // Pause before sending tablet restore ops to tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_issuing_tserver_snapshot_requests) = true;
  TxnSnapshotRestorationId restoration_id = ASSERT_RESULT(RestoreSnapshot(snapshot_id));
  auto s = DeleteSnapshot(snapshot_id);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_issuing_tserver_snapshot_requests) = false;
  ASSERT_TRUE(s.IsInvalidArgument())
      << "Expected invalid argument from deleting snapshot, instead got: " << s;
  ASSERT_STR_CONTAINS(s.ToString(), Format("restoration $0 is in progress", restoration_id));
  ASSERT_OK(WaitForSnapshotRestorationDone(restoration_id));
}

TEST_F(RestoreAndDeleteValidationTest, DeleteAfterRestore) {
  auto snapshot_id = CreateTableAndSnapshotDuringWrites();
  TxnSnapshotRestorationId restoration_id = ASSERT_RESULT(RestoreSnapshot(snapshot_id));
  ASSERT_OK(WaitForSnapshotRestorationDone(restoration_id));
  // Should be able to delete the snapshot now that the restore is complete.
  ASSERT_OK(DeleteSnapshot(snapshot_id));
}

TEST_F(RestoreAndDeleteValidationTest, RestoreDuringDelete) {
  auto snapshot_id = CreateTableAndSnapshotDuringWrites();
  // Pause before sending tablet deletion ops to tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_issuing_tserver_snapshot_requests) = true;
  ASSERT_OK(DeleteSnapshot(snapshot_id));
  auto result = RestoreSnapshot(snapshot_id);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_issuing_tserver_snapshot_requests) = false;
  ASSERT_TRUE(!result.ok() && result.status().IsIllegalState())
      << "Expected illegal state from restoring deleted snapshot, instead got: "
      << (!result.ok() ? result.status() : Status::OK());
  ASSERT_STR_CONTAINS(result.status().ToString(), "The snapshot has started deleting");
}

TEST_F(RestoreAndDeleteValidationTest, RestoreAfterDelete) {
  auto snapshot_id = CreateTableAndSnapshotDuringWrites();
  ASSERT_OK(DeleteSnapshotAndWait(snapshot_id));
  auto result = RestoreSnapshot(snapshot_id);
  ASSERT_TRUE(!result.ok() && result.status().IsIllegalState())
      << "Expected illegal state from restoring deleted snapshot, instead got: "
      << (!result.ok() ? result.status() : Status::OK());
}

} // namespace yb
