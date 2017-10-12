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
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

DECLARE_bool(enable_remote_bootstrap);

namespace yb {

using std::make_shared;
using std::shared_ptr;
using std::tuple;
using std::set;
using std::vector;

using strings::Substitute;
using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using itest::TabletServerMap;
using yb::tablet::TabletPeer;
using yb::tablet::enterprise::kSnapshotsDirName;
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
using yb::master::IsCreateSnapshotDoneRequestPB;
using yb::master::IsCreateSnapshotDoneResponsePB;
using yb::master::ListSnapshotsRequestPB;
using yb::master::ListSnapshotsResponsePB;

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

    ASSERT_OK(MessengerBuilder("test-msgr")
              .set_num_reactors(1)
              .set_negotiation_threads(1)
              .Build(&messenger_));
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
    YBPartialRow split1(&schema);
    RETURN_NOT_OK(split1.SetInt32("key", 10));

    YBPartialRow split2(&schema);
    RETURN_NOT_OK(split2.SetInt32("key", 20));

    return CreateTableWithSplits(table_name, schema, { split1, split2 }, namespace_name, table_id);
  }

  Status CreateTableWithSplits(const TableName& table_name,
                               const Schema& schema,
                               const vector<YBPartialRow>& split_rows,
                               const NamespaceName& namespace_name,
                               string* table_id = nullptr) {
    CreateTableRequestPB req;
    RowOperationsPBEncoder encoder(req.mutable_split_rows());
    for (const YBPartialRow& row : split_rows) {
      encoder.Add(RowOperationsPB::SPLIT_ROW, row);
    }
    return DoCreateTable(table_name, schema, &req, namespace_name, table_id);
  }

  Status DoCreateTable(const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request,
                       const NamespaceName& namespace_name,
                       string* table_id = nullptr) {
    CreateTableResponsePB resp;

    request->set_table_type(TableType::YQL_TABLE_TYPE);
    request->set_name(table_name);
    RETURN_NOT_OK(SchemaToPB(schema, request->mutable_schema()));

    if (!namespace_name.empty()) {
      request->mutable_namespace_()->set_name(namespace_name);
    }

    request->mutable_replication_info()->mutable_live_replicas()->set_num_replicas(2);

    // Dereferencing as the RPCs require const ref for request. Keeping request param as pointer
    // though, as that helps with readability and standardization.
    RETURN_NOT_OK(proxy_->CreateTable(*request, &resp, ResetAndGetController()));
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

  void ListAllSnapshots(
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

 protected:
  shared_ptr<Messenger> messenger_;
  gscoped_ptr<MasterServiceProxy> proxy_;
  gscoped_ptr<MasterBackupServiceProxy> proxy_backup_;
  shared_ptr<RpcController> controller_;
  TabletServerMap ts_map_;
  MiniMaster* mini_master_;
};

TEST_F(MiniClusterMasterTest, TestCreateSnapshot) {
  // Create a new namespace.
  const NamespaceName other_ns_name = "testns";
  NamespaceId other_ns_id;
  ListNamespacesResponsePB namespaces;
  {
    CreateNamespaceResponsePB resp;
    ASSERT_OK(CreateNamespace(other_ns_name, &resp));
    other_ns_id = resp.id();
  }
  {
    ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
    ASSERT_EQ(2 + kNumSystemNamespaces, namespaces.namespaces_size());
    CheckNamespacesExistance({ make_tuple(other_ns_name, other_ns_id) }, namespaces);
  }

  // Create a table with the defined new namespace.
  const TableName kTableName = "testtb";
  const Schema kTableSchema({ ColumnSchema("key", INT32) }, 1);
  string table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, other_ns_name, &table_id));

  IsCreateTableDoneRequestPB is_create_req;
  IsCreateTableDoneResponsePB is_create_resp;

  is_create_req.mutable_table()->set_table_name(kTableName);
  is_create_req.mutable_table()->mutable_namespace_()->set_name(other_ns_name);

  // Do a set of iterations to check that the operation is done.
  // Sleep if the operation is not done by the moment.
  // Increase sleep time on each iteration in accordance with the formula:
  //   X[0] = 100 ms
  //   X[i+1] = X[i] * 1.5    // +50%
  //   Sleep(100 ms + X[i] ms)
  // So, sleep time:
  //   Iteration  0: 0,20 sec
  //   Iteration  1: 0,25 sec
  //   Iteration  2: 0,32 sec
  //   ...
  //   Iteration  9: 3,94 sec
  //   Iteration 10: 5,87 sec
  //   Iteration 11: 8,75 sec
  //   Sum: 27 seconds (in 12 iterations)
  int wait_ms = 100;
  static const int kMaxNumRetries = 12;

  for (int num_retries = kMaxNumRetries; num_retries > 0; --num_retries) {
    Status s = proxy_->IsCreateTableDone(is_create_req, &is_create_resp, ResetAndGetController());
    ASSERT_TRUE(s.ok());
    ASSERT_FALSE(is_create_resp.has_error());
    ASSERT_TRUE(is_create_resp.has_done());
    if (is_create_resp.done()) {
      LOG(INFO) << "IsCreateTableDone: DONE";
      break;
    }
    LOG(INFO) << "IsCreateTableDone: not done - sleep " << (100 + wait_ms) << " ms";
    SleepFor(MonoDelta::FromMilliseconds(100 + wait_ms));
    wait_ms = wait_ms * 3 / 2; // +50%
  }

  // Test fails if table was not created.
  ASSERT_TRUE(is_create_resp.done());

  ListTablesResponsePB tables;
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(1 + mini_master_->master()->NumSystemTables(), tables.tables_size());
  CheckTablesExistance({ make_tuple(kTableName, other_ns_name, other_ns_id) }, tables);

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
      const string snapshots_dir = JoinPathSegments(rocksdb_dir, kSnapshotsDirName);

      ASSERT_TRUE(fs->Exists(rocksdb_dir));
      ASSERT_FALSE(fs->Exists(snapshots_dir));
    }
  }

  ListAllSnapshots({});

  string snapshot_id;

  // Check CreateSnapshot().
  {
    CreateSnapshotRequestPB req;
    CreateSnapshotResponsePB resp;
    TableIdentifierPB* const table = req.mutable_tables()->Add();
    table->set_table_name(kTableName);
    table->mutable_namespace_()->set_name(other_ns_name);

    // Check the request.
    ASSERT_OK(proxy_backup_->CreateSnapshot(req, &resp, ResetAndGetController()));

    // Check the response.
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_TRUE(resp.has_snapshot_id());
    snapshot_id = resp.snapshot_id();
    LOG(INFO) << "Started snapshot creation: ID=" << snapshot_id;
  }

  ListAllSnapshots(
      {
          std::make_tuple(snapshot_id, SysSnapshotEntryPB::CREATING)
      }, snapshot_id);

  // Check the snapshot creation complete.
  {
    IsCreateSnapshotDoneRequestPB is_snapshot_done_req;
    IsCreateSnapshotDoneResponsePB is_snapshot_done_resp;

    is_snapshot_done_req.set_snapshot_id(snapshot_id);
    // Do a set of iterations to check that the operation is done.
    // Sleep if the operation is not done by the moment.
    // Increase sleep time on each iteration in accordance with the formula:
    //   X[0] = 100 ms
    //   X[i+1] = X[i] * 1.5    // +50%
    //   Sleep(100 ms + X[i] ms)
    // So, sleep time:
    //   Iteration  0: 0,20 sec
    //   Iteration  1: 0,25 sec
    //   Iteration  2: 0,32 sec
    //   ...
    //   Iteration  9: 3,94 sec
    //   Iteration 10: 5,87 sec
    //   Iteration 11: 8,75 sec
    //   Sum: 27 seconds (in 12 iterations)
    wait_ms = 100;

    for (int num_retries = kMaxNumRetries; num_retries > 0; --num_retries) {
      const Status s = proxy_backup_->IsCreateSnapshotDone(
          is_snapshot_done_req, &is_snapshot_done_resp, ResetAndGetController());

      ASSERT_TRUE(s.ok());
      ASSERT_FALSE(is_snapshot_done_resp.has_error());
      ASSERT_TRUE(is_snapshot_done_resp.has_done());
      if (is_snapshot_done_resp.done()) {
        LOG(INFO) << "IsCreateSnapshotDone: DONE";
        break;
      }
      LOG(INFO) << "IsCreateSnapshotDone: not done - sleep " << (100 + wait_ms) << " ms";
      SleepFor(MonoDelta::FromMilliseconds(100 + wait_ms));
      wait_ms = wait_ms * 3 / 2; // +50%
    }

    // Test fails if snapshot was not successfully completed.
    ASSERT_TRUE(is_snapshot_done_resp.done());
  }

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
      const string snapshots_dir = JoinPathSegments(rocksdb_dir, kSnapshotsDirName);
      const string tablet_dir = JoinPathSegments(snapshots_dir, snapshot_id);

      LOG(INFO) << "Checking tablet snapshot folder: " << tablet_dir;
      ASSERT_TRUE(fs->Exists(rocksdb_dir));
      ASSERT_TRUE(fs->Exists(snapshots_dir));
      ASSERT_TRUE(fs->Exists(tablet_dir));
      // Check existence of snapshot files:
      ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "CURRENT")));
      ASSERT_TRUE(fs->Exists(JoinPathSegments(tablet_dir, "MANIFEST-000001")));
    }
  }

  ListAllSnapshots(
      {
          std::make_tuple(snapshot_id, SysSnapshotEntryPB::COMPLETE)
      });

  LOG(INFO) << "CreateSnapshot finished. Deleting test table & namespace.";
  // Delete the table in the namespace 'testns'.
  ASSERT_OK(DeleteTable(kTableName, other_ns_name));

  // List tables, should show only system tables.
  ASSERT_NO_FATALS(DoListAllTables(&tables));
  ASSERT_EQ(mini_master_->master()->NumSystemTables(), tables.tables_size());

  // Delete the namespace (by NAME).
  {
    DeleteNamespaceRequestPB req;
    DeleteNamespaceResponsePB resp;
    req.mutable_namespace_()->set_name(other_ns_name);
    ASSERT_OK(proxy_->DeleteNamespace(req, &resp, ResetAndGetController()));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  ASSERT_NO_FATALS(DoListAllNamespaces(&namespaces));
  ASSERT_EQ(1 + kNumSystemNamespaces, namespaces.namespaces_size());
}

} // namespace yb
