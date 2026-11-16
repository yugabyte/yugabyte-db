//
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
//

#include "yb/master/master-test-util.h"

#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"

#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/util/status.h"
#include "yb/util/stopwatch.h"


namespace yb {
namespace master {

Status WaitForRunningTabletCount(MiniMaster* mini_master,
                                 const client::YBTableName& table_name,
                                 int expected_count,
                                 GetTableLocationsResponsePB* resp) {
  int wait_time = 1000;

  SCOPED_LOG_TIMING(INFO, Format("waiting for tablet count of $0", expected_count));
  while (true) {
    GetTableLocationsRequestPB req;
    resp->Clear();
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    req.set_max_returned_locations(expected_count);
    RETURN_NOT_OK(mini_master->catalog_manager().GetTableLocations(&req, resp));
    if (resp->tablet_locations_size() >= expected_count) {
      bool is_stale = false;
      for (const TabletLocationsPB& loc : resp->tablet_locations()) {
        is_stale |= loc.stale();
      }

      if (!is_stale) {
        return Status::OK();
      }
    }
    std::vector<std::string> tablets;
    for (const auto& loc : resp->tablet_locations()) {
      tablets.push_back(loc.tablet_id());
    }
    std::sort(tablets.begin(), tablets.end());

    LOG(INFO) << "Waiting for " << expected_count << " tablets for table "
              << table_name.ToString() << ". So far we have " << resp->tablet_locations_size()
              << ", existing tablets: " << AsString(tablets);

    SleepFor(MonoDelta::FromMicroseconds(wait_time));
    wait_time = std::min(wait_time * 5 / 4, 1000000);
  }

  // Unreachable.
  LOG(FATAL) << "Reached unreachable section";
  return STATUS(RuntimeError, "Unreachable statement"); // Suppress compiler warnings.
}

std::unordered_map<TabletServerId, int64_t> ExpectedReplicaLoad(MiniMaster* mini_master) {
  std::unordered_map<TabletServerId, int64_t> result;
  for (const auto& table : mini_master->catalog_manager().GetTables(
           GetTablesMode::kAll, PrimaryTablesOnly::kTrue)) {
    auto tablets = table->GetTablets();
    if (!tablets.ok()) {
      continue;
    }
    for (const auto& tablet : *tablets) {
      if (tablet->LockForRead()->is_deleted()) {
        continue;
      }
      for (const auto& [ts_uuid, _] : *tablet->GetReplicaLocations()) {
        ++result[ts_uuid];
      }
    }
  }
  return result;
}

bool ReplicaLoadMatchesReplicaMaps(MiniMaster* mini_master) {
  auto expected = ExpectedReplicaLoad(mini_master);
  for (const auto& desc : mini_master->ts_manager().GetAllDescriptors()) {
    auto it = expected.find(desc->permanent_uuid());
    const int64_t want = it == expected.end() ? 0 : it->second;
    if (desc->replica_load() != want) {
      LOG(INFO) << desc->permanent_uuid() << " counts " << desc->replica_load() << " replicas, "
                << "maps hold " << want;
      return false;
    }
  }
  return true;
}

} // namespace master
} // namespace yb
