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

#include "yb/master/cdcsdk_manager.h"

#include <list>

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

#include "yb/util/flags/flag_tags.h"

DEFINE_test_flag(bool, cdcsdk_skip_processing_dynamic_table_addition, false,
    "Skip finding unprocessed tables for cdcsdk streams");

DEFINE_test_flag(bool, cdcsdk_skip_processing_unqualified_tables, false,
    "Skip the bg task that finds and removes unprocessed unqualified tables from "
    "cdcsdk streams.");

DECLARE_bool(cdcsdk_enable_dynamic_table_addition_with_table_cleanup);

namespace yb::master {

using TableStreamIdsMap = std::unordered_map<TableId, std::list<CDCStreamInfoPtr>>;

CdcsdkManager::CdcsdkManager(
    Master& master, CatalogManager& catalog_manager, SysCatalogTable& sys_catalog)
    : catalog_manager_(catalog_manager) {}

CdcsdkManager::~CdcsdkManager() = default;

void CdcsdkManager::RunBgTasks(const LeaderEpoch& epoch) {
  if (!FLAGS_TEST_cdcsdk_skip_processing_dynamic_table_addition) {
    // Find if there have been any new tables added to any namespace with an active cdcsdk
    // stream.
    TableStreamIdsMap table_unprocessed_streams_map;
    // In case of master leader restart of leadership changes, we will scan all streams for
    // unprocessed tables, but from the second iteration onwards we will only consider the
    // 'cdcsdk_unprocessed_tables' field of CDCStreamInfo object stored in the cdc_state_map.
    Status s = catalog_manager_.FindCDCSDKStreamsForAddedTables(&table_unprocessed_streams_map);

    if (s.ok() && !table_unprocessed_streams_map.empty()) {
      s = catalog_manager_.ProcessNewTablesForCDCSDKStreams(table_unprocessed_streams_map, epoch);
    }
    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10)
          << "Encountered failure while trying to add unprocessed tables to cdc_state table: "
          << s.ToString();
    }
  } else {
    LOG(INFO) << "Skipping processing of dynamic table addition due to "
                 "cdcsdk_skip_processing_dynamic_table_addition being true";
  }

  if (FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup) {
    // Find if there are any non eligible tables (indexes, mat views) present in cdcsdk
    // stream that are not associated with a replication slot.
    TableStreamIdsMap non_user_tables_to_streams_map;
    // In case of master leader restart or leadership changes, we would have scanned all
    // streams (without replication slot) in ACTIVE/DELETING METADATA state for non eligible
    // tables and marked such tables for removal in
    // namespace_to_cdcsdk_non_eligible_table_map_.
    Status s =
        catalog_manager_.FindCDCSDKStreamsForNonEligibleTables(&non_user_tables_to_streams_map);

    if (s.ok() && !non_user_tables_to_streams_map.empty()) {
      s = catalog_manager_.ProcessTablesToBeRemovedFromCDCSDKStreams(
          non_user_tables_to_streams_map, /* non_eligible_table_cleanup */ true, epoch);
    }
    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10) << "Encountered failure while trying to remove non eligible "
                                     "tables from cdc_state table: "
                                  << s.ToString();
    }
  }

  if (FLAGS_cdcsdk_enable_dynamic_table_addition_with_table_cleanup &&
      !FLAGS_TEST_cdcsdk_skip_processing_unqualified_tables) {
    TableStreamIdsMap tables_to_be_removed_streams_map;
    Status s = catalog_manager_.FindCDCSDKStreamsForUnprocessedUnqualifiedTables(
        &tables_to_be_removed_streams_map);

    if (s.ok() && !tables_to_be_removed_streams_map.empty()) {
      s = catalog_manager_.ProcessTablesToBeRemovedFromCDCSDKStreams(
          tables_to_be_removed_streams_map, /* non_eligible_table_cleanup */ false, epoch);
    }

    if (!s.ok()) {
      YB_LOG_EVERY_N(WARNING, 10) << "Encountered failure while trying to remove unqualified "
                                     "tables from stream metadata & updating cdc_state table: "
                                  << s.ToString();
    }
  }
}

}  // namespace yb::master
