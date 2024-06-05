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

#include <gtest/gtest.h>

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/restore_sys_catalog_state.h"

#include "yb/util/oid_generator.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace master {

void CheckMatch(
    const TableId& table_id, const SysTablesEntryPB& table_entry, RestoreSysCatalogState* state,
    bool match) {
  auto result = ASSERT_RESULT(state->TEST_MatchTable(table_id, table_entry));
  ASSERT_EQ(result, match);
}

TEST(RestoreSysCatalogStateTest, Filter) {
  const NamespaceId kNamespaceId = GenerateObjectId();
  const NamespaceId kWrongNamespaceId = GenerateObjectId();
  const std::string kNamespaceName = "namespace";
  const std::string kWrongNamespaceName = "wrong namespace";
  const TableId kTableId = GenerateObjectId();
  const TableId kWrongTableId = GenerateObjectId();
  const std::string kTableName = "table";
  const std::string kWrongTableName = "wrong table";

  auto restoration = SnapshotScheduleRestoration {
      .snapshot_id = TxnSnapshotId::GenerateRandom(),
      .restore_at = HybridTime(),
      .restoration_id = TxnSnapshotRestorationId::GenerateRandom(),
      .op_id = OpId(),
      .write_time = {},
      .term = 0,
      .db_oid = std::nullopt,
      .schedules = {},
      .non_system_obsolete_tablets = {},
      .non_system_obsolete_tables = {},
      .non_system_objects_to_restore = {},
      .existing_system_tables = {},
      .restoring_system_tables = {},
      .parent_to_child_tables = {},
      .non_system_tablets_to_restore = {},
  };
  RestoreSysCatalogState state(&restoration);
  SysNamespaceEntryPB namespace_entry;
  namespace_entry.set_name(kNamespaceName);
  namespace_entry.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
  state.TEST_AddNamespace(kNamespaceId, namespace_entry);

  SysTablesEntryPB table_entry;
  table_entry.set_name(kTableName);
  table_entry.set_namespace_id(kNamespaceId);
  table_entry.set_namespace_name(kNamespaceName);

  restoration.schedules.emplace_back(SnapshotScheduleId::Nil(), SnapshotScheduleFilterPB());
  auto& table_identifier = *restoration.schedules[0].second.mutable_tables()->add_tables();

  for (bool use_table_id : {true, false}) {
    SCOPED_TRACE(Format("use_table_id: $0", use_table_id));
    for (bool good_table : {true, false}) {
      SCOPED_TRACE(Format("good_table: $0", good_table));
      table_identifier.Clear();
      if (use_table_id) {
        table_identifier.set_table_id(good_table ? kTableId : kWrongTableId);
        CheckMatch(kTableId, table_entry, &state, good_table);
      } else {
        table_identifier.set_table_name(good_table ? kTableName : kWrongTableName);
        for (bool use_namespace_id : {true, false}) {
          SCOPED_TRACE(Format("use_namespace_id: $0", use_namespace_id));
          for (bool good_namespace : {true, false}) {
            SCOPED_TRACE(Format("good_namespace: $0", good_namespace));
            auto& ns = *table_identifier.mutable_namespace_();
            ns.Clear();
            if (use_namespace_id) {
              ns.set_id(good_namespace ? kNamespaceId : kWrongNamespaceId);
              CheckMatch(kTableId, table_entry, &state, good_table && good_namespace);
            } else {
              ns.set_name(good_namespace ? kNamespaceName : kWrongNamespaceName);
              CheckMatch(kTableId, table_entry, &state, good_table && good_namespace);
              ns.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
              CheckMatch(kTableId, table_entry, &state, good_table && good_namespace);
              ns.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
              CheckMatch(kTableId, table_entry, &state, false);
            }
          }
        }
      }
    }
  }
}

}  // namespace master
}  // namespace yb
