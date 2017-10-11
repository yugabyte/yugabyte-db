// Copyright (c) YugaByte, Inc.

#include "../../src/yb/master/catalog_manager-test_base.h"

namespace yb {
namespace master {
namespace enterprise {

using std::shared_ptr;
using std::make_shared;

class TestLoadBalancerEnterprise : public TestLoadBalancerBase<ClusterLoadBalancerMocked> {
 public:
  TestLoadBalancerEnterprise(ClusterLoadBalancerMocked* cb, const string& table_id) :
      TestLoadBalancerBase<ClusterLoadBalancerMocked>(cb, table_id) {}

  void TestAlgorithm() {
    TestLoadBalancerBase<ClusterLoadBalancerMocked>::TestAlgorithm();

    shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
    shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "b");
    shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "c");

    TSDescriptorVector ts_descs = {ts0, ts1, ts2};

    PrepareTestState(ts_descs);
    PrepareAffinitizedLeaders("a" /* affinitized zones */);
    TestAlreadyBalancedAffinitizedLeaders();

    PrepareTestState(ts_descs);
    PrepareAffinitizedLeaders("a");
    TestBalancingOneAffinitizedLeader();

    PrepareTestState(ts_descs);
    PrepareAffinitizedLeaders("bc");
    TestBalancingTwoAffinitizedLeaders();
  }

  void TestAlreadyBalancedAffinitizedLeaders() {
    LOG(INFO) << "Starting TestAlreadyBalancedAffinitizedLeaders";
    // Move all leaders to ts0.
    for (const auto tablet : tablets_) {
      MoveTabletLeader(tablet.get(), ts_descs_[0]);
    }
    LOG(INFO) << "Leader distribution: 4 0 0";

    AnalyzeTablets();
    string placeholder;
    // Only the affinitized zone contains tablet leaders, should be no movement.
    ASSERT_FALSE(HandleLeaderMoves(&placeholder, &placeholder, &placeholder));
    LOG(INFO) << "Finishing TestAlreadyBalancedAffinitizedLeaders";
  }

  void TestBalancingOneAffinitizedLeader() {
    LOG(INFO) << "Starting TestBalancingOneAffinitizedLeader";
    int i = 0;
    for (const auto tablet : tablets_) {
      MoveTabletLeader(tablet.get(), ts_descs_[(i % 2) + 1]);
      i++;
    }
    LOG(INFO) << "Leader distribution: 0 2 2";

    AnalyzeTablets();

    std::map<string, int> from_count;
    std::unordered_set<string> tablets_moved;

    string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2;
    ts_0 = ts_descs_[0]->permanent_uuid();
    ts_1 = ts_descs_[1]->permanent_uuid();
    ts_2 = ts_descs_[2]->permanent_uuid();

    from_count[ts_0] = 0;
    from_count[ts_1] = 0;
    from_count[ts_2] = 0;

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(to_ts, ts_0);
    tablets_moved.insert(tablet_id);
    from_count[from_ts]++;

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(to_ts, ts_0);
    tablets_moved.insert(tablet_id);
    from_count[from_ts]++;

    LOG(INFO) << "Leader distribution: 2 1 1";
    // Make sure one tablet moved from each ts1 and ts2.
    ASSERT_EQ(1, from_count[ts_1]);
    ASSERT_EQ(1, from_count[ts_2]);

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(to_ts, ts_0);
    tablets_moved.insert(tablet_id);
    from_count[from_ts]++;

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(to_ts, ts_0);
    tablets_moved.insert(tablet_id);
    from_count[from_ts]++;

    LOG(INFO) << "Leader distribution: 4 0 0";
    // Make sure two tablets moved from each ts1 and ts2.
    ASSERT_EQ(2, from_count[ts_1]);
    ASSERT_EQ(2, from_count[ts_2]);

    // Make sure all the tablets moved are distinct.
    ASSERT_EQ(4, tablets_moved.size());

    ASSERT_FALSE(HandleLeaderMoves(&placeholder, &placeholder, &placeholder));
    LOG(INFO) << "Finishing TestBalancingOneAffinitizedLeader";
  }

  void TestBalancingTwoAffinitizedLeaders() {
    LOG(INFO) << "Starting TestBalancingTwoAffinitizedLeaders";

    for (const auto tablet : tablets_) {
      MoveTabletLeader(tablet.get(), ts_descs_[0]);
    }
    LOG(INFO) << "Leader distribution: 4 0 0";

    AnalyzeTablets();

    std::map<string, int> to_count;
    std::unordered_set<string> tablets_moved;

    string placeholder, tablet_id, from_ts, to_ts, ts_0, ts_1, ts_2;
    ts_0 = ts_descs_[0]->permanent_uuid();
    ts_1 = ts_descs_[1]->permanent_uuid();
    ts_2 = ts_descs_[2]->permanent_uuid();

    to_count[ts_0] = 0;
    to_count[ts_1] = 0;
    to_count[ts_2] = 0;


    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(from_ts, ts_0);
    tablets_moved.insert(tablet_id);
    to_count[to_ts]++;

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(from_ts, ts_0);
    tablets_moved.insert(tablet_id);
    to_count[to_ts]++;

    LOG(INFO) << "Leader distribution: 2 1 1";
    // Make sure one tablet moved to each ts1 and ts2.
    ASSERT_EQ(1, to_count[ts_1]);
    ASSERT_EQ(1, to_count[ts_2]);

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(from_ts, ts_0);
    tablets_moved.insert(tablet_id);
    to_count[to_ts]++;

    ASSERT_TRUE(HandleLeaderMoves(&tablet_id, &from_ts, &to_ts));
    ASSERT_EQ(from_ts, ts_0);
    tablets_moved.insert(tablet_id);
    to_count[to_ts]++;

    LOG(INFO) << "Leader distribution: 0 2 2";
    // Make sure two tablets moved to each ts1 and ts2.
    ASSERT_EQ(2, to_count[ts_1]);
    ASSERT_EQ(2, to_count[ts_2]);

    // Make sure all the tablets moved are distinct.
    ASSERT_EQ(4, tablets_moved.size());

    ASSERT_FALSE(HandleLeaderMoves(&placeholder, &placeholder, &placeholder));
    LOG(INFO) << "Finishing TestBalancingTwoAffinitizedLeaders";
  }

  void PrepareAffinitizedLeaders(const string& zones) {
    for (char zone : zones) {
      CloudInfoPB ci;
      ci.set_placement_cloud("aws");
      ci.set_placement_region("us-west-1");
      ci.set_placement_zone(string(1, zone));
      affinitized_zones_.insert(ci);
    }
  }
};

TEST(TestLoadBalancerEnterprise, TestLoadBalancerAlgorithm) {
  const TableId table_id = CURRENT_TEST_NAME();
  auto cb = make_shared<yb::master::enterprise::ClusterLoadBalancerMocked>();
  auto lb = make_shared<TestLoadBalancerEnterprise>(cb.get(), table_id);
  lb->TestAlgorithm();
}

} // namespace enterprise
} // namespace master
} // namespace yb
