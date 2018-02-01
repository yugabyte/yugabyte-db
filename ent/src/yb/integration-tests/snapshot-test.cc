// Copyright (c) YugaByte, Inc.

#include <gtest/gtest.h>

#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/rpc/messenger.h"

#include "yb/master/master.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/master-test-util.h"

#include "yb/tablet/tablet.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);

namespace yb {

using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;
using std::tuple;
using std::set;
using std::vector;

using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::IsSnapshotOpDoneRequestPB;
using master::IsSnapshotOpDoneResponsePB;
using master::ListTablesRequestPB;
using master::ListTablesResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::MasterBackupServiceProxy;
using master::MasterServiceProxy;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::SysSnapshotEntryPB;
using master::TableIdentifierPB;
using rpc::Messenger;
using rpc::MessengerBuilder;
using rpc::RpcController;
using tablet::enterprise::Tablet;
using tablet::TabletPeer;
using tserver::MiniTabletServer;

const client::YBTableName kTableName("my_keyspace", "snapshot_test_table");

class SnapshotTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    flag_saver_.emplace();

    FLAGS_log_min_seconds_to_retain = 5;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(MessengerBuilder("test-msgr").set_num_reactors(1).Build().MoveTo(&messenger_));
    proxy_.reset(new MasterServiceProxy(messenger_, cluster_->mini_master()->bound_rpc_addr()));
    proxy_backup_.reset(new MasterBackupServiceProxy(
        messenger_, cluster_->mini_master()->bound_rpc_addr()));

    // Connect to the cluster.
    ASSERT_OK(cluster_->CreateClient(&client_));
  }

  void DoTearDown() override {
    bool exist = false;
    ASSERT_OK(client_->TableExists(kTableName, &exist));
    if (exist) {
      ASSERT_OK(client_->DeleteTable(kTableName));
    }
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    flag_saver_.reset();

    YBMiniClusterTestBase::DoTearDown();
  }

  RpcController* ResetAndGetController() {
    controller_.Reset();
    controller_.set_timeout(10s);
    return &controller_;
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
    ASSERT_OK(WaitFor(handler, 30s, handler_name, 100ms, 1.5));
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

  void WaitForSnapshotOpDone(const string& op_name, const string& snapshot_id) {
    IsSnapshotOpDoneRequestPB is_snapshot_done_req;
    IsSnapshotOpDoneResponsePB is_snapshot_done_resp;
    is_snapshot_done_req.set_snapshot_id(snapshot_id);

    WaitTillComplete(
        op_name, ProxyCallLambda(
            &is_snapshot_done_req, &is_snapshot_done_resp,
            proxy_backup_.get(), &MasterBackupServiceProxy::IsSnapshotOpDone));
  }

  string CreateSnapshot() {
    CreateSnapshotRequestPB req;
    CreateSnapshotResponsePB resp;
    TableIdentifierPB* const table = req.mutable_tables()->Add();
    table->set_table_name(kTableName.table_name());
    table->mutable_namespace_()->set_name(kTableName.namespace_name());

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

  void VerifySnapshotFiles(const std::string& snapshot_id) {
    std::unordered_map<TabletId, OpId> last_tablet_op;

    size_t max_tablets = 0;
    for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
      MiniTabletServer* const ts = cluster_->mini_tablet_server(i);
      auto ts_tablet_peers = ts->server()->tablet_manager()->GetTabletPeers();
      max_tablets = std::max(max_tablets, ts_tablet_peers.size());
      for (const auto& tablet_peer : ts_tablet_peers) {
        consensus::OpId op_id;
        ASSERT_OK(tablet_peer->consensus()->GetLastOpId(OpIdType::RECEIVED_OPID, &op_id));
        last_tablet_op[tablet_peer->tablet_id()].MakeAtLeast(OpId::FromPB(op_id));
      }
    }

    for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
      MiniTabletServer* ts = cluster_->mini_tablet_server(i);
      auto predicate = [max_tablets, ts]() {
        return ts->server()->tablet_manager()->GetTabletPeers().size() >= max_tablets;
      };
      ASSERT_OK(WaitFor(predicate, 15s, "Wait for peers to be up"));
    }

    // Check snapshot files existence.
    for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
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
            consensus::OpId pre_op_id;
            EXPECT_OK(tablet_peer->consensus()->GetLastOpId(OpIdType::COMMITTED_OPID, &pre_op_id));
            auto op_id = OpId::FromPB(pre_op_id);
            return op_id >= last_op_id;
          },
          15s,
          "Wait for op id commit"
        ));
        FsManager* const fs = tablet_peer->tablet_metadata()->fs_manager();
        const auto rocksdb_dir = tablet_peer->tablet_metadata()->rocksdb_dir();
        const auto top_snapshots_dir = Tablet::SnapshotsDirName(rocksdb_dir);
        const auto tablet_dir = JoinPathSegments(top_snapshots_dir, snapshot_id);

        LOG(INFO) << "Checking tablet snapshot folder: " << tablet_dir;
        ASSERT_TRUE(fs->Exists(rocksdb_dir));
        ASSERT_TRUE(fs->Exists(top_snapshots_dir));
        ASSERT_TRUE(fs->Exists(tablet_dir));
        // Check existence of snapshot files:
        auto list = ASSERT_RESULT(fs->ListDir(tablet_dir));
        ASSERT_TRUE(std::find(list.begin(), list.end(), "CURRENT") != list.end());
        bool has_manifest = false;
        for (const auto& file : list) {
          SCOPED_TRACE("File: " + file);
          if (file.find("MANIFEST-") == 0) {
            has_manifest = true;
          }
          if (file.find(".sst") != std::string::npos) {
            auto snapshot_path = JoinPathSegments(tablet_dir, file);
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

  TestWorkload SetupWorkload() {
    TestWorkload workload(cluster_.get());
    workload.set_table_name(kTableName);
    workload.set_sequential_write(true);
    workload.set_insert_failures_allowed(false);
    workload.set_num_write_threads(1);
    workload.set_write_batch_size(10);
    workload.Setup();
    return workload;
  }

 protected:
  shared_ptr<Messenger> messenger_;
  unique_ptr<MasterServiceProxy> proxy_;
  unique_ptr<MasterBackupServiceProxy> proxy_backup_;
  RpcController controller_;
  client::YBClientPtr client_;
  boost::optional<google::FlagSaver> flag_saver_;
};

TEST_F(SnapshotTest, CreateSnapshot) {
  SetupWorkload(); // Used to create table

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
  const string snapshot_id = CreateSnapshot();

  ASSERT_NO_FATALS(VerifySnapshotFiles(snapshot_id));

  ASSERT_OK(cluster_->RestartSync());
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
  auto ts0 = cluster_->mini_tablet_server(0);

  // Shutdown one node, so remote bootstrap will be required after its start.
  ts0->Shutdown();

  auto workload = SetupWorkload();
  workload.Start();
  workload.WaitInserted(1000);

  const auto snapshot_id = CreateSnapshot();

  // Wait to make sure that we would need remote bootstrap.
  std::this_thread::sleep_for(std::chrono::seconds(FLAGS_log_min_seconds_to_retain));

  workload.StopAndJoin();

  cluster_->FlushTablets();
  cluster_->CleanTabletLogs();

  ASSERT_OK(ts0->Start());
  ASSERT_NO_FATALS(VerifySnapshotFiles(snapshot_id));
}

} // namespace yb
