// Copyright (c) YugaByte, Inc.

#include "yb/client/client.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

class PlacementInfoTest : public YBTest {
 public:
  PlacementInfoTest() {}

  ~PlacementInfoTest() {}

  const int kNumTservers = 3;

 protected:
  void SetUp() override {
    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = 1;
    opts.num_tablet_servers = kNumTservers;

    // Start tservers with different placement information.
    std::vector<tserver::TabletServerOptions> tserver_opts;
    for (int i = 0; i < kNumTservers; i++) {
      tserver::TabletServerOptions opts;
      opts.placement_cloud = "aws";
      opts.placement_region = PlacementRegion(i);
      opts.placement_zone = PlacementZone(i);
      tserver_opts.push_back(opts);
    }

    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start(tserver_opts));
    for (int i = 0; i < cluster_->num_tablet_servers(); i++) {
      std::string ts_uuid = cluster_->mini_tablet_server(i)->server()->fs_manager()->uuid();
      ts_uuid_to_index_.emplace(ts_uuid, i);
    }

    YBClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(&builder, &client_));
    rpc::MessengerBuilder bld("Client");
    ASSERT_OK(bld.Build(&client_messenger_));
    proxy_.reset(new master::MasterServiceProxy(client_messenger_,
                                                cluster_->leader_mini_master()->bound_rpc_addr()));
  }

  std::string PlacementRegion(int ts_index) {
    return strings::Substitute("region$0", ts_index);
  }

  std::string PlacementZone(int ts_index) {
    return strings::Substitute("zone$0", ts_index);
  }

  void TearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
      cluster_.reset();
    }

    YBTest::TearDown();
  }

  std::unique_ptr<MiniCluster> cluster_;
  std::shared_ptr<YBClient> client_;
  std::unique_ptr<master::MasterServiceProxy> proxy_;
  std::shared_ptr<rpc::Messenger> client_messenger_;
  std::map<std::string, int> ts_uuid_to_index_;
};

TEST_F(PlacementInfoTest, TestTabletLocations) {
  // Create the table.
  YBSchema schema;
  YBSchemaBuilder b;
  b.AddColumn("key")->Type(INT32)->NotNull()->PrimaryKey();
  b.AddColumn("int_val")->Type(INT32)->NotNull();
  CHECK_OK(b.Build(&schema));
  gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
  YBTableName table_name("test_tablet_locations");
  table_name.set_namespace_name(yb::master::kDefaultNamespaceName);
  CHECK_OK(table_creator->table_name(table_name)
      .schema(&schema)
      .wait(true)
      .num_tablets(1)
      .num_replicas(kNumTservers)
      .Create());


  // Retrieve tablets.
  rpc::RpcController controller;
  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;
  table_name.SetIntoTableIdentifierPB(req.mutable_table());
  ASSERT_OK(proxy_->GetTableLocations(req, &resp, &controller));

  // Verify tablet information.
  ASSERT_EQ(1, resp.tablet_locations_size());
  auto tablet_locations = resp.tablet_locations(0);
  ASSERT_EQ(kNumTservers, tablet_locations.replicas_size());
  for (int i = 0; i < tablet_locations.replicas_size(); i++) {
    // Find the ts by uuid.
    auto cloud_info = tablet_locations.replicas(i).ts_info().cloud_info();
    auto ts_uuid = tablet_locations.replicas(i).ts_info().permanent_uuid();
    auto it = ts_uuid_to_index_.find(ts_uuid);
    int ts_index = (*it).second;
    ASSERT_TRUE(it != ts_uuid_to_index_.end());

    // Remove it from the map to ensure we look through all ts uuids.
    ts_uuid_to_index_.erase(it);

    ASSERT_EQ("aws", cloud_info.placement_cloud());
    ASSERT_EQ(PlacementRegion(ts_index), cloud_info.placement_region());
    ASSERT_EQ(PlacementZone(ts_index), cloud_info.placement_zone());
  }
  ASSERT_EQ(0, ts_uuid_to_index_.size());
}

} // namespace client
} // namespace yb
