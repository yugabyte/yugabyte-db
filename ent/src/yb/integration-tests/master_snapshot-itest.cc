// Copyright (c) YugaByte, Inc.

#include <gtest/gtest.h>

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/rpc/messenger.h"

#include "yb/master/master.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/master-test-util.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

DECLARE_bool(enable_remote_bootstrap);

namespace yb {

using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;
using std::tuple;
using std::set;
using std::vector;

using strings::Substitute;
using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using itest::TabletServerMap;
using yb::tablet::TabletPeer;
using yb::tablet::enterprise::Tablet;
using yb::tserver::MiniTabletServer;
using yb::master::kNumSystemNamespaces;
using yb::master::MiniMaster;
using yb::master::MasterServiceProxy;
using yb::master::MasterBackupServiceProxy;
using yb::master::TableInfo;
using yb::master::TabletInfo;

using yb::master::SysSnapshotEntryPB;
using yb::master::CreateNamespaceRequestPB;
using yb::master::CreateNamespaceResponsePB;
using yb::master::DeleteNamespaceRequestPB;
using yb::master::DeleteNamespaceResponsePB;
using yb::master::ListNamespacesRequestPB;
using yb::master::ListNamespacesResponsePB;
using yb::master::TableIdentifierPB;
using yb::master::CreateTableRequestPB;
using yb::master::CreateTableResponsePB;
using yb::master::IsCreateTableDoneRequestPB;
using yb::master::IsCreateTableDoneResponsePB;
using yb::master::DeleteTableRequestPB;
using yb::master::DeleteTableResponsePB;
using yb::master::ListTablesRequestPB;
using yb::master::ListTablesResponsePB;
using yb::master::CreateSnapshotRequestPB;
using yb::master::CreateSnapshotResponsePB;
using yb::master::IsSnapshotOpDoneRequestPB;
using yb::master::IsSnapshotOpDoneResponsePB;
using yb::master::ListSnapshotsRequestPB;
using yb::master::ListSnapshotsResponsePB;
using yb::master::RestoreSnapshotRequestPB;
using yb::master::RestoreSnapshotResponsePB;

class MiniClusterMasterTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MiniClusterMasterTest() {}

  void SetUp() override {
    // Workaround KUDU-941: without this, it's likely that while shutting
    // down tablets, they'll get resuscitated by their existing leaders.
    FLAGS_enable_remote_bootstrap = false;

    YBMiniClusterTestBase::SetUp();

    // Set an RPC timeout for the controllers.
    controller_ = make_shared<RpcController>();
    controller_->set_timeout(MonoDelta::FromSeconds(10));

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(MessengerBuilder("test-msgr").set_num_reactors(1).Build().MoveTo(&messenger_));
    proxy_.reset(new MasterServiceProxy(messenger_, cluster_->mini_master()->bound_rpc_addr()));
    proxy_backup_.reset(new MasterBackupServiceProxy(
        messenger_, cluster_->mini_master()->bound_rpc_addr()));
    ASSERT_OK(CreateTabletServerMap(proxy_.get(), messenger_, &ts_map_));

    mini_master_ = cluster_->mini_master();
  }

  void DoTearDown() override {
    cluster_->Shutdown();
    ts_map_.clear();
  }

  RpcController* ResetAndGetController() {
    controller_->Reset();
    return controller_.get();
  }

  void DoListAllNamespaces(ListNamespacesResponsePB* resp) {
    ListNamespacesRequestPB req;

    ASSERT_OK(proxy_->ListNamespaces(req, resp, ResetAndGetController()));
    SCOPED_TRACE(resp->DebugString());
    ASSERT_FALSE(resp->has_error());
  }

  Status CreateNamespace(const NamespaceName& ns_name,
                         CreateNamespaceResponsePB* resp) {
    CreateNamespaceRequestPB req;
    req.set_name(ns_name);

    RETURN_NOT_OK(proxy_->CreateNamespace(req, resp, ResetAndGetController()));
    if (resp->has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp->error().status()));
    }
    return Status::OK();
  }

  void CheckNamespacesExistance(const set<tuple<NamespaceName, NamespaceId>>& namespace_info,
                                const ListNamespacesResponsePB& namespaces) {
    for (auto info : namespace_info) {
      bool found = false;
      for (int i = 0; i < namespaces.namespaces_size(); ++i) {
        if (std::get<0>(info) == namespaces.namespaces(i).name() &&
            std::get<1>(info) == namespaces.namespaces(i).id()) {
            found = true;
        }
      }

      ASSERT_TRUE(found) << Substitute(
          "Couldn't find namespace $0 with id $1", std::get<0>(info), std::get<1>(info));
    }
  }

  Status CreateTable(const TableName& table_name,
                     const Schema& schema,
                     const NamespaceName& namespace_name,
                     string* table_id = nullptr) {
    CreateTableRequestPB request;
    CreateTableResponsePB resp;

    request.set_table_type(TableType::YQL_TABLE_TYPE);
    request.set_name(table_name);
    request.set_num_tablets(3);
    RETURN_NOT_OK(SchemaToPB(schema, request.mutable_schema()));

    if (!namespace_name.empty()) {
      request.mutable_namespace_()->set_name(namespace_name);
    }

    request.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(2);

    // Dereferencing as the RPCs require const ref for request. Keeping request param as pointer
    // though, as that helps with readability and standardization.
    RETURN_NOT_OK(proxy_->CreateTable(request, &resp, ResetAndGetController()));
    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }

    if (!resp.has_table_id()) {
      return STATUS(InternalError, "No table_id value in CreateTableResponsePB");
    }

    LOG(INFO) << "Created table " << namespace_name << '.' << table_name <<
        " with id " << resp.table_id();
    if (table_id) {
      *table_id = resp.table_id();
    }
    return Status::OK();
  }

  void DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp) {
    ASSERT_OK(proxy_->ListTables(req, resp, ResetAndGetController()));
    SCOPED_TRACE(resp->DebugString());
    ASSERT_FALSE(resp->has_error());
  }

  void DoListAllTables(ListTablesResponsePB* resp,
                       const NamespaceName& namespace_name = "") {
    ListTablesRequestPB req;

    if (!namespace_name.empty()) {
      req.mutable_namespace_()->set_name(namespace_name);
    }

    DoListTables(req, resp);
  }

  Status DeleteTable(const TableName& table_name,
                     const NamespaceName& namespace_name  = "",
                     TableId* table_id = nullptr) {
    DeleteTableRequestPB req;
    DeleteTableResponsePB resp;
    req.mutable_table()->set_table_name(table_name);

    if (!namespace_name.empty()) {
      req.mutable_table()->mutable_namespace_()->set_name(namespace_name);
    }

    RETURN_NOT_OK(proxy_->DeleteTable(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    if (table_id) {
      *table_id = resp.table_id();
    }

    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }
    return Status::OK();
  }

  void CheckTablesExistance(const set<tuple<TableName, NamespaceName, NamespaceId>>& table_info,
                            const ListTablesResponsePB& tables) {
    for (auto info : table_info) {
      bool found = false;
      for (int i = 0; i < tables.tables_size(); ++i) {
        if (std::get<0>(info) == tables.tables(i).name() &&
            std::get<1>(info) == tables.tables(i).namespace_().name() &&
            std::get<2>(info) == tables.tables(i).namespace_().id()) {
            found = true;
        }
      }

      ASSERT_TRUE(found) << Substitute(
          "Couldn't find table $0.$1", std::get<1>(info), std::get<0>(info));
    }
  }

  void CheckAllSnapshots(
      const std::set<std::tuple<SnapshotId, SysSnapshotEntryPB::State>>& snapshot_info,
      SnapshotId cur_id = "") {
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

    if (cur_id.empty()) {
      ASSERT_FALSE(list_resp.has_current_snapshot_id());
    } else {
      ASSERT_TRUE(list_resp.has_current_snapshot_id());
      ASSERT_EQ(list_resp.current_snapshot_id(), cur_id);
      LOG(INFO) << "Current snapshot: " << list_resp.current_snapshot_id();
    }

    for (int i = 0; i < list_resp.snapshots_size(); ++i) {
      LOG(INFO) << "Snapshot " << i << ": " << list_resp.snapshots(i).DebugString();

      auto search_key = std::make_tuple(
          list_resp.snapshots(i).id(), list_resp.snapshots(i).entry().state());
      ASSERT_TRUE(snapshot_info.find(search_key) != snapshot_info.end())
          << strings::Substitute("Couldn't find snapshot id $0 in state $1",
              list_resp.snapshots(i).id(), list_resp.snapshots(i).entry().state());
    }
  }

  template <typename THandler>
  void WaitTillComplete(const string& handler_name, THandler handler) {
    // Do a set of iterations to check that the operation is done.
    // Sleep if the operation is not done by the moment.
    // Increase sleep time on each iteration in accordance with the formula:
    //   X[0] = 100 ms
    //   X[i+1] = X[i] * 1.5    // +50%
    //   Sleep(100 ms + X[i] ms)
    // So, sleep time:
    //   Iteration  0: 0.20 sec
    //   Iteration  1: 0.25 sec
    //   Iteration  2: 0.32 sec
    //   ...
    //   Iteration  9: 3.94 sec
    //   Iteration 10: 5.87 sec
    //   Iteration 11: 8.75 sec
    //   Sum: 27 seconds (in 12 iterations)
    static const int kHalfMinWaitMs = 100;
    static const int kMaxNumRetries = 12;
    int wait_ms = kHalfMinWaitMs;
    bool complete = false;

    for (int num_retries = kMaxNumRetries; num_retries > 0; --num_retries) {
      complete = handler();
      if (complete) {
        LOG(INFO) << handler_name << ": DONE";
        break;
      }

      LOG(INFO) << handler_name << ": not done - sleep " << (kHalfMinWaitMs + wait_ms) << " ms";
      SleepFor(MonoDelta::FromMilliseconds(kHalfMinWaitMs + wait_ms));
      wait_ms = wait_ms * 3 / 2; // +50%
    }

    // Test fails if operation was not successfully completed.
    ASSERT_TRUE(complete);
  }

  template <typename TReq, typename TResp, typename TProxy>
  auto ProxyCallLambda(
      const TReq* req, TResp* resp, TProxy* proxy,
      Status (TProxy::*call)(const TReq&, TResp*, rpc::RpcController* controller)) {
    return [=]() -> bool {
      EXPECT_OK((proxy->*call)(*req, resp, ResetAndGetController()));
      EXPECT_FALSE(resp->has_error());
      EXPECT_TRUE(resp->has_done());
      return resp->done();
    };
  }

  void CreateNamespaceAndTable(const NamespaceName& ns_name, const TableName& table_name) {
    // Create a new namespace.
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(ns_name, &resp));
    NamespaceId ns_id = resp.id();

    ListNamespacesResponsePB namespaces;
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    // Note: kNumSystemNamespaces plus "testns" (ns_name).
    ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespacesExistance({ make_tuple(ns_name, ns_id) }, namespaces);

    // Create a table with the defined new namespace.
    const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
    string table_id;
    ASSERT_OK(CreateTable(table_name, kTableSchema, ns_name, &table_id));

    IsCreateTableDoneRequestPB is_create_req;
    IsCreateTableDoneResponsePB is_create_resp;
    is_create_req.mutable_table()->set_table_name(table_name);
    is_create_req.mutable_table()->mutable_namespace_()->set_name(ns_name);

    WaitTillComplete(
        "IsCreateTableDone", ProxyCallLambda(
            &is_create_req, &is_create_resp, proxy_.get(), &MasterServiceProxy::IsCreateTableDone));

    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(1 + mini_master_->master()->NumSystemTables(), tables.tables_size());
    CheckTablesExistance({ make_tuple(table_name, ns_name, ns_id) }, tables);
  }

  void DestroyNamespaceAndTable(const NamespaceName& ns_name, const TableName& table_name) {
    // Delete the table in the namespace 'ns_name'.
    ASSERT_OK(DeleteTable(table_name, ns_name));

    // List tables, should show only system tables.
    ListTablesResponsePB tables;
    ASSERT_NO_FATALS(DoListAllTables(&tables));
    ASSERT_EQ(mini_master_->master()->NumSystemTables(), tables.tables_size());

    // Delete the namespace (by NAME).
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    ListNamespacesResponsePB namespaces;
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(kNumSystemNamespaces, namespaces.namespaces_size());
  }

  void WaitForSnapshotOpDone(const string& op_name, const string& snapshot_id) {
    IsSnapshotOpDoneRequestPB is_snapshot_done_req;
    IsSnapshotOpDoneResponsePB is_snapshot_done_resp;
    is_snapshot_done_req.set_snapshot_id(snapshot_id);

    WaitTillComplete(
        op_name, ProxyCallLambda(
            &is_snapshot_done_req, &is_snapshot_done_resp,
            proxy_backup_.get(), &MasterBackupServiceProxy::IsSnapshotOpDone));
  }

  string CreateSnapshot(const NamespaceName& ns_name, const TableName& table_name) {
    CreateSnapshotRequestPB req;
    CreateSnapshotResponsePB resp;
    TableIdentifierPB* const table = req.mutable_tables()->Add();
    table->set_table_name(table_name);
    table->mutable_namespace_()->set_name(ns_name);

    // Check the request.
    EXPECT_OK(proxy_backup_->CreateSnapshot(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    EXPECT_FALSE(resp.has_error());
    EXPECT_TRUE(resp.has_snapshot_id());
    LOG(INFO) << "Started snapshot creation: ID=" << resp.snapshot_id();
    const string snapshot_id = resp.snapshot_id();

    CheckAllSnapshots(
        {
            std::make_tuple(snapshot_id, SysSnapshotEntryPB::CREATING)
        }, snapshot_id);

    // Check the snapshot creation is complete.
    WaitForSnapshotOpDone("IsCreateSnapshotDone", snapshot_id);

    CheckAllSnapshots(
        {
            std::make_tuple(snapshot_id, SysSnapshotEntryPB::COMPLETE)
        });

    return snapshot_id;
  }

 protected:
  shared_ptr<Messenger> messenger_;
  unique_ptr<MasterServiceProxy> proxy_;
  unique_ptr<MasterBackupServiceProxy> proxy_backup_;
  shared_ptr<RpcController> controller_;
  TabletServerMap ts_map_;
  MiniMaster* mini_master_;
};

TEST_F(MiniClusterMasterTest, TestCreateSnapshot) {
  const NamespaceName kNamespaceName = "testns";
  const TableName kTableName = "testtb";
  CreateNamespaceAndTable(kNamespaceName, kTableName);

  // Check tablet folders before the snapshot creation.
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
    vector<scoped_refptr<TabletPeer> > ts_tablet_peers;
    ts->server()->tablet_manager()->GetTabletPeers(&ts_tablet_peers);

    // Iterate through all available tablets (on this TabletServer).
    // There is only one table here (testtb).
    for (scoped_refptr<TabletPeer>& tablet_peer : ts_tablet_peers) {
      FsManager* const fs = tablet_peer->tablet_metadata()->fs_manager();
      const string rocksdb_dir = tablet_peer->tablet_metadata()->rocksdb_dir();
      const string top_snapshots_dir = Tablet::SnapshotsDirName(rocksdb_dir);

      ASSERT_TRUE(fs->Exists(rocksdb_dir));
      ASSERT_FALSE(fs->Exists(top_snapshots_dir));
    }
  }

  CheckAllSnapshots({});

  // Check CreateSnapshot().
  const string snapshot_id = CreateSnapshot(kNamespaceName, kTableName);

  // Check snapshot files existence.
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
    vector<scoped_refptr<TabletPeer> > ts_tablet_peers;
    ts->server()->tablet_manager()->GetTabletPeers(&ts_tablet_peers);

    // Iterate through all available tablets (on this TabletServer), because there is
    // only one table here (testtb). And snapshot was created for this table.
    for (scoped_refptr<TabletPeer>& tablet_peer : ts_tablet_peers) {
      FsManager* const fs = tablet_peer->tablet_metadata()->fs_manager();
      const string rocksdb_dir = tablet_peer->tablet_metadata()->rocksdb_dir();
      const string top_snapshots_dir = Tablet::SnapshotsDirName(rocksdb_dir);
      const string tablet_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);

      LOG(INFO) << "Checking tablet snapshot folder: " << tablet_dir;
      ASSERT_TRUE(fs->Exists(rocksdb_dir));
      ASSERT_TRUE(fs->Exists(top_snapshots_dir));
      ASSERT_TRUE(fs->Exists(tablet_dir));
      // Check existence of snapshot files:
      ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "CURRENT")));
      ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "MANIFEST-000001")));
    }
  }

  LOG(INFO) << "TestCreateSnapshot finished. Deleting test table & namespace.";
  DestroyNamespaceAndTable(kNamespaceName, kTableName);
}

TEST_F(MiniClusterMasterTest, TestRestoreSnapshot) {
  const NamespaceName kNamespaceName = "testns";
  const TableName kTableName = "testtb";
  CreateNamespaceAndTable(kNamespaceName, kTableName);

  CheckAllSnapshots({});

  // Check CreateSnapshot().
  const string snapshot_id = CreateSnapshot(kNamespaceName, kTableName);

  // Check RestoreSnapshot().
  {
    RestoreSnapshotRequestPB req;
    RestoreSnapshotResponsePB resp;
    req.set_snapshot_id(snapshot_id);

    // Check the request.
    ASSERT_OK(proxy_backup_->RestoreSnapshot(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    LOG(INFO) << "Started snapshot restoring: ID=" << snapshot_id;
  }

  CheckAllSnapshots(
      {
          std::make_tuple(snapshot_id, SysSnapshotEntryPB::RESTORING)
      }, snapshot_id);

  // Check the snapshot restoring is complete.
  WaitForSnapshotOpDone("IsRestoreSnapshotDone", snapshot_id);

  CheckAllSnapshots(
      {
          std::make_tuple(snapshot_id, SysSnapshotEntryPB::COMPLETE)
      });

  // TODO: Check restored data.

  LOG(INFO) << "TestRestoreSnapshot finished. Deleting test table & namespace.";
  DestroyNamespaceAndTable(kNamespaceName, kTableName);
}

} // namespace yb
