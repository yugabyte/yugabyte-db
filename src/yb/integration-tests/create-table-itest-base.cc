// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/integration-tests/create-table-itest-base.h"

using std::string;
using std::vector;

namespace yb {

Status CreateTableITestBase::CreateTableWithPlacement(
    const master::ReplicationInfoPB& replication_info, const string& table_suffix,
    const YBTableType table_type) {
  auto db_type = master::GetDatabaseTypeForTable(client::ClientToPBTableType(table_type));
  RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(), db_type));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(yb::GetSimpleTestSchema()));
  if (table_type != YBTableType::REDIS_TABLE_TYPE) {
    table_creator->schema(&client_schema);
  }
  return table_creator
      ->table_name(YBTableName(
          db_type,
          kTableName.namespace_name(),
          Substitute("$0:$1", kTableName.table_name(), table_suffix)))
      .replication_info(replication_info)
      .table_type(table_type)
      .wait(true)
      .Create();
}

Result<bool> CreateTableITestBase::VerifyTServerTablets(
    int idx, int num_tablets, int num_leaders, const std::string& table_name, bool verify_leaders) {
  auto tablets = VERIFY_RESULT(cluster_->GetTablets(cluster_->tablet_server(idx)));

  int leader_count = 0, tablet_count = 0;
  for (const auto& tablet : tablets) {
    if (tablet.table_name() != table_name) {
      continue;
    }
    if (tablet.state() != tablet::RaftGroupStatePB::RUNNING) {
      return false;
    }
    tablet_count++;
    if (tablet.is_leader()) {
      leader_count++;
    }
  }
  LOG(INFO) << "For table " << table_name << ", on tserver " << idx << " number of leaders "
            << leader_count << " number of tablets " << tablet_count;
  if ((verify_leaders && leader_count != num_leaders) || tablet_count != num_tablets) {
    return false;
  }
  return true;
}

void CreateTableITestBase::PreparePlacementInfo(
    const std::unordered_map<string, int>& zone_to_replica_count,
    int num_replicas,
    master::PlacementInfoPB* placement_info) {
  placement_info->set_num_replicas(num_replicas);
  for (const auto& zone_and_count : zone_to_replica_count) {
    auto* pb = placement_info->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud("c");
    pb->mutable_cloud_info()->set_placement_region("r");
    pb->mutable_cloud_info()->set_placement_zone(zone_and_count.first);
    pb->set_min_num_replicas(zone_and_count.second);
  }
}

void CreateTableITestBase::AddTServerInZone(const string& zone) {
  vector<std::string> flags = {
    "--placement_cloud=c",
    "--placement_region=r",
    "--placement_zone=" + zone
  };
  ASSERT_OK(cluster_->AddTabletServer(true, flags));
}

}  // namespace yb
