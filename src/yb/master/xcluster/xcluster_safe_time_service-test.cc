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

#include "yb/util/test_util.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"

using std::string;

namespace yb {
namespace master {

using OK = Status::OK;

class XClusterSafeTimeServiceMocked : public XClusterSafeTimeService {
 public:
  XClusterSafeTimeServiceMocked()
      : XClusterSafeTimeService(nullptr, nullptr, nullptr), create_table_if_not_found_(false) {}

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

  std::map<ProducerTabletInfo, HybridTime> table_entries_;
  std::map<ProducerTabletInfo, NamespaceId> consumer_registry_;
  XClusterNamespaceToSafeTimeMap safe_time_map_;
  std::vector<ProducerTabletInfo> entries_to_delete_;
  bool create_table_if_not_found_;
};

TEST(XClusterSafeTimeServiceTest, ComputeSafeTime) {
  using ProducerTabletInfo = XClusterSafeTimeService::ProducerTabletInfo;

  const auto replication_group_id = xcluster::ReplicationGroupId("c1");
  const NamespaceId db1 = "db1";
  const NamespaceId db2 = "db2";
  const ProducerTabletInfo t1 = {replication_group_id, "t1"};
  const ProducerTabletInfo t2 = {replication_group_id, "t2"};
  const ProducerTabletInfo t3 = {replication_group_id, "t3"};
  const ProducerTabletInfo t4 = {replication_group_id, "t4"};
  std::map<ProducerTabletInfo, NamespaceId> default_consumer_registry;
  default_consumer_registry[t1] = db1;
  default_consumer_registry[t2] = db2;
  default_consumer_registry[t3] = db2;
  const auto ht1 = HybridTime(HybridTime::kInitial.ToUint64() + 1);
  const auto ht2 = HybridTime(ht1.ToUint64() + 1);
  const auto ht3 = HybridTime(ht2.ToUint64() + 1);
  const auto ht_invalid = HybridTime::kInvalid;
  int64_t dummy_leader_term = 1;

  // Empty config
  {
    XClusterSafeTimeServiceMocked safe_time_service;

    // Empty consumer registry
    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 0);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);
    ASSERT_FALSE(safe_time_service.create_table_if_not_found_);

    // No table data
    safe_time_service.consumer_registry_ = default_consumer_registry;
    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
    ASSERT_EQ(safe_time_service.safe_time_map_.size(), 2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db1], ht2);
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht_invalid);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 0);

    // Catchup the tablet
    safe_time_service.table_entries_[t3] = ht2;
    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
    ASSERT_EQ(safe_time_service.safe_time_map_[db2], ht2);

    // Tablet going backwards due to stale YCQL write
    safe_time_service.table_entries_[t3] = ht1;
    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    ASSERT_OK(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    auto further_computation_needed =
        ASSERT_RESULT(safe_time_service.ComputeSafeTime(dummy_leader_term));
    ASSERT_TRUE(further_computation_needed);

    // Clear consumer registry
    safe_time_service.consumer_registry_.clear();
    further_computation_needed =
        ASSERT_RESULT(safe_time_service.ComputeSafeTime(dummy_leader_term));
    ASSERT_FALSE(safe_time_service.create_table_if_not_found_);
    ASSERT_EQ(safe_time_service.entries_to_delete_.size(), 3);
    ASSERT_FALSE(further_computation_needed);

    // Reenable replication
    safe_time_service.consumer_registry_ = default_consumer_registry;
    further_computation_needed =
        ASSERT_RESULT(safe_time_service.ComputeSafeTime(dummy_leader_term));
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

    auto result = safe_time_service.ComputeSafeTime(dummy_leader_term);
    ASSERT_NOK(result);
    ASSERT_STR_CONTAINS(result.ToString(), "System tables cannot be replicated");
  }
}

}  // namespace master
}  // namespace yb
