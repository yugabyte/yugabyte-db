//
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
//

#include "yb/master/master-test-util.h"

#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"

#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/mini_master.h"

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

    LOG(INFO) << "Waiting for " << expected_count << " tablets for table "
              << table_name.ToString() << ". So far we have " << resp->tablet_locations_size();

    SleepFor(MonoDelta::FromMicroseconds(wait_time));
    wait_time = std::min(wait_time * 5 / 4, 1000000);
  }

  // Unreachable.
  LOG(FATAL) << "Reached unreachable section";
  return STATUS(RuntimeError, "Unreachable statement"); // Suppress compiler warnings.
}

} // namespace master
} // namespace yb
