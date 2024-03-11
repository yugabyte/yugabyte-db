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
//

#include <mutex>
#include "yb/common/hybrid_time.h"
#include "yb/util/test_util.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

using std::string;

namespace yb {
namespace master {

using OK = Status::OK;

class XClusterSafeTimeServiceMocked : public XClusterSafeTimeService {
 public:
  XClusterSafeTimeServiceMocked()
      : XClusterSafeTimeService(nullptr, nullptr, nullptr), create_table_if_not_found_(false) {
    std::lock_guard lock(mutex_);
    safe_time_table_ready_ = true;
  }

  ~XClusterSafeTimeServiceMocked() {}

  Result<std::map<ProducerTabletInfo, HybridTime>> GetSafeTimeFromTable() override
      REQUIRES(mutex_) {
    create_table_if_not_found_ = VERIFY_RESULT(CreateTableRequired());

    return table_entries_;
  }

  Status RefreshProducerTabletToNamespaceMap() REQUIRES(mutex_) override {
    if (producer_tablet_namespace_map_ != consumer_registry_) {
      producer_tablet_namespace_map_ = consumer_registry_;
    }

    return OK();
  }

  Result<XClusterNamespaceToSafeTimeMap> GetXClusterNamespaceToSafeTimeMap() override {
    return safe_time_map_;
  }

  Status SetXClusterSafeTime(
      const int64_t leader_term,
      const XClusterNamespaceToSafeTimeMap& new_safe_time_map_pb) override {
    safe_time_map_ = new_safe_time_map_pb;
    return OK();
  }

  Status CleanupEntriesFromTable(const std::vector<ProducerTabletInfo>& entries_to_delete) override
      REQUIRES(mutex_) {
    entries_to_delete_ = entries_to_delete;
    for (auto& entry : entries_to_delete_) {
      table_entries_.erase(entry);
    }
    return OK();
  }

  Result<HybridTime> GetLeaderSafeTimeFromCatalogManager() override { return leader_safe_time_; }

  void SetDdlQueueTablets(const std::vector<ProducerTabletInfo>& tablet_infos) {
    std::lock_guard lock(mutex_);
    ddl_queue_tablet_ids_.clear();
    for (const auto& tablet_info : tablet_infos) {
      ddl_queue_tablet_ids_.insert(tablet_info.tablet_id);
    }
  }

  std::map<ProducerTabletInfo, HybridTime> table_entries_;
  std::map<ProducerTabletInfo, NamespaceId> consumer_registry_;
  XClusterNamespaceToSafeTimeMap safe_time_map_;
  std::vector<ProducerTabletInfo> entries_to_delete_;
  HybridTime leader_safe_time_;
  bool create_table_if_not_found_;
};

class XClusterSafeTimeServiceTest : public YBTest {
 public:
  using ProducerTabletInfo = XClusterSafeTimeService::ProducerTabletInfo;

  const xcluster::ReplicationGroupId replication_group_id = xcluster::ReplicationGroupId("c1");
  const NamespaceId db1 = "db1";
  const NamespaceId db2 = "db2";
  const ProducerTabletInfo t1 = {replication_group_id, "t1"};
  const ProducerTabletInfo t2 = {replication_group_id, "t2"};
  const ProducerTabletInfo t3 = {replication_group_id, "t3"};
  const ProducerTabletInfo t4 = {replication_group_id, "t4"};
  const HybridTime ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
  const HybridTime ht2 = HybridTime(ht1.ToUint64() + 1);
  const HybridTime ht3 = HybridTime(ht2.ToUint64() + 1);
  const HybridTime ht_invalid = HybridTime::kInvalid;
  int64_t dummy_leader_term = 1;

  std::map<ProducerTabletInfo, NamespaceId> default_consumer_registry;

  XClusterSafeTimeServiceTest() {
    default_consumer_registry[t1] = db1;
    default_consumer_registry[t2] = db2;
    default_consumer_registry[t3] = db2;
  }

  Result<bool> ComputeSafeTime(XClusterSafeTimeServiceMocked& safe_time_service) {
    return safe_time_service.ComputeSafeTime(dummy_leader_term);
  }

  Result<HybridTime> GetXClusterSafeTimeWithNoFilter(
      XClusterSafeTimeServiceMocked& safe_time_service, const NamespaceId& namespace_id) {
    return safe_time_service.GetXClusterSafeTimeForNamespace(
        dummy_leader_term, namespace_id, XClusterSafeTimeFilter::NONE);
  }

  Result<HybridTime> GetXClusterSafeTimeFilterOutDdlQueue(
      XClusterSafeTimeServiceMocked& safe_time_service, const NamespaceId& namespace_id) {
    return safe_time_service.GetXClusterSafeTimeForNamespace(
        dummy_leader_term, namespace_id, XClusterSafeTimeFilter::DDL_QUEUE);
  }
};

TEST_F(XClusterSafeTimeServiceTest, ComputeSafeTime) {
  // Empty config
  {
    XClusterSafeTimeServiceMocked safe_time_service;

    // Empty consumer registry
    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 0);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
    ASSERT_FALSE(safe_time_service.create_table_if_not_found_);

    // No table data
    safe_time_service.consumer_registry_ = default_consumer_registry;
    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht_invalid);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht_invalid);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
    ASSERT_TRUE(safe_time_service.create_table_if_not_found_);
  }

  // All tablets caught up
  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    safe_time_service.table_entries_[t1] = ht1;
    safe_time_service.table_entries_[t2] = ht1;
    safe_time_service.table_entries_[t3] = ht1;

    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht1);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht1);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
  }

  // Missing tablet in db2
  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    safe_time_service.table_entries_[t1] = ht2;
    safe_time_service.table_entries_[t2] = ht2;

    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht_invalid);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);

    // Catchup the tablet
    safe_time_service.table_entries_[t3] = ht2;
    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);

    // Tablet going backwards due to stale YCQL write
    safe_time_service.table_entries_[t3] = ht1;
    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
  }

  // Lagging tablet in db2
  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    safe_time_service.table_entries_[t1] = ht2;
    safe_time_service.table_entries_[t2] = ht2;
    safe_time_service.table_entries_[t3] = ht1;

    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht1);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
  }

  // Extra tablet in db2
  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    safe_time_service.table_entries_[t1] = ht2;
    safe_time_service.table_entries_[t2] = ht2;
    safe_time_service.table_entries_[t3] = ht2;
    safe_time_service.table_entries_[t4] = ht3;

    ASSERT_OK(ComputeSafeTime(safe_time_service));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 1);
    ASSERT_EQ(safe_time_service.entries_to_delete_[0], t4);
  }

  // Stop replication
  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    safe_time_service.table_entries_[t1] = ht1;
    safe_time_service.table_entries_[t2] = ht1;
    safe_time_service.table_entries_[t3] = ht1;

    auto further_computation_needed = ASSERT_RESULT(ComputeSafeTime(safe_time_service));
    ASSERT_TRUE(further_computation_needed);

    // Clear consumer registry
    safe_time_service.consumer_registry_.clear();
    further_computation_needed = ASSERT_RESULT(ComputeSafeTime(safe_time_service));
    ASSERT_FALSE(safe_time_service.create_table_if_not_found_);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 3);
    ASSERT_FALSE(further_computation_needed);

    // Reenable replication
    safe_time_service.consumer_registry_ = default_consumer_registry;
    further_computation_needed = ASSERT_RESULT(ComputeSafeTime(safe_time_service));
    ASSERT_TRUE(safe_time_service.create_table_if_not_found_);
    ASSERT_TRUE(further_computation_needed);
  }

  {
    XClusterSafeTimeServiceMocked safe_time_service;
    safe_time_service.consumer_registry_ = default_consumer_registry;

    const ProducerTabletInfo t5 = {replication_group_id, "t5"};
    safe_time_service.table_entries_[t1] = ht2;
    safe_time_service.table_entries_[t2] = ht2;
    safe_time_service.table_entries_[t3] = ht2;
    safe_time_service.table_entries_[t5] = ht1;
    safe_time_service.consumer_registry_[t5] = kSystemNamespaceId;

    auto result = ComputeSafeTime(safe_time_service);
    ASSERT_NOK(result);
    ASSERT_STR_CONTAINS(result.ToString(), "System tables cannot be replicated");
  }
}

TEST_F(XClusterSafeTimeServiceTest, ComputeSafeTimeWithFilters) {
  XClusterSafeTimeServiceMocked safe_time_service;
  safe_time_service.consumer_registry_ = default_consumer_registry;
  safe_time_service.leader_safe_time_ = ht2;

  const ProducerTabletInfo t5 = {replication_group_id, "t5"};
  safe_time_service.consumer_registry_[t4] = db1;
  safe_time_service.consumer_registry_[t5] = db1;

  // Define t1 and t2 as ddl_queue tablets.
  safe_time_service.SetDdlQueueTablets({t1, t2});

  // db1
  safe_time_service.table_entries_[t1] = ht1;  // ddl_queue
  safe_time_service.table_entries_[t4] = ht2;
  safe_time_service.table_entries_[t5] = ht3;
  // db2
  safe_time_service.table_entries_[t2] = ht2;  // ddl_queue
  safe_time_service.table_entries_[t3] = ht_invalid;

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht1);
  ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht_invalid);
  ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);

  ASSERT_NOK(safe_time_service.GetXClusterSafeTimeForNamespace(
      dummy_leader_term + 1, db1, XClusterSafeTimeFilter::NONE));
  ASSERT_NOK(safe_time_service.GetXClusterSafeTimeForNamespace(
      dummy_leader_term - 1, db1, XClusterSafeTimeFilter::NONE));

  auto db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  auto db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  auto db2_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db2));
  auto db2_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db2));
  ASSERT_EQ(db1_none, ht1);
  ASSERT_EQ(db1_ddlqueue, ht2);
  ASSERT_EQ(db2_none, ht_invalid);
  ASSERT_EQ(db2_ddlqueue, ht_invalid);

  // Update safe times and ensure safe time advances.
  safe_time_service.table_entries_[t1] = ht3;  // ddl_queue
  safe_time_service.table_entries_[t3] = ht3;

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
  ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);

  db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  db2_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db2));
  db2_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db2));
  ASSERT_EQ(db1_none, ht2);
  ASSERT_EQ(db1_ddlqueue, ht2);
  ASSERT_EQ(db2_none, ht2);
  ASSERT_EQ(db2_ddlqueue, ht3);

  // Rollback safe times, ensure that computed safe time doesn't go backwards.
  safe_time_service.table_entries_[t1] = ht1;  // ddl_queue
  safe_time_service.table_entries_[t3] = ht1;

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
  ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);

  db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  db2_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db2));
  db2_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db2));
  ASSERT_EQ(db1_none, ht2);
  ASSERT_EQ(db1_ddlqueue, ht2);
  ASSERT_EQ(db2_none, ht2);
  ASSERT_EQ(db2_ddlqueue, ht3);
}

TEST_F(XClusterSafeTimeServiceTest, ComputeSafeTimeWithFiltersSingleTablet) {
  XClusterSafeTimeServiceMocked safe_time_service;
  safe_time_service.leader_safe_time_ = ht2;

  // Only start with one tablet, and mark is as a ddl_queue tablet
  safe_time_service.consumer_registry_[t1] = db1;
  safe_time_service.SetDdlQueueTablets({t1});

  safe_time_service.table_entries_[t1] = ht1;

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 1);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht1);
  ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);

  auto db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  auto db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  ASSERT_EQ(db1_none, ht1);
  ASSERT_EQ(db1_ddlqueue, ht2);  // leader safe time

  // Add a new tablet in.
  safe_time_service.consumer_registry_[t2] = db1;
  safe_time_service.table_entries_[t2] = ht3;

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 1);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht1);

  db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  ASSERT_EQ(db1_none, ht1);
  ASSERT_EQ(db1_ddlqueue, ht3);

  // Remove second tablet. Safe time should not regress.
  safe_time_service.consumer_registry_.erase(t2);
  safe_time_service.table_entries_.erase(t2);

  ASSERT_OK(ComputeSafeTime(safe_time_service));
  ASSERT_EQ(safe_time_service.safe_time_map_.size(), 1);
  ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht1);

  db1_none = ASSERT_RESULT(GetXClusterSafeTimeWithNoFilter(safe_time_service, db1));
  db1_ddlqueue = ASSERT_RESULT(GetXClusterSafeTimeFilterOutDdlQueue(safe_time_service, db1));
  ASSERT_EQ(db1_none, ht1);
  ASSERT_EQ(db1_ddlqueue, ht3);
}
}  // namespace master
}  // namespace yb
