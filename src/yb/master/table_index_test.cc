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

#include "yb/master/table_index.h"

#include "gmock/gmock.h"
#include "yb/common/colocated_util.h"
#include "yb/master/master_util.h"
#include <gtest/gtest.h>

namespace tt = testing;

namespace yb {
namespace master {

TableInfoPtr CreateTable(const TableId& table_id, bool colocated = false) {
  auto table = scoped_refptr<TableInfo>(new TableInfo(table_id, colocated));
  if (colocated) {
    auto mut = table->mutable_metadata();
    mut->StartMutation();
    mut->mutable_dirty()->pb.set_colocated(true);
    mut->CommitMutation();
  }
  return table;
}

TableInfoPtr CreateParentTablegroupTable() {
  const TableId id = std::string(std::string(31, '0') + '1' + kColocatedDbParentTableIdSuffix);
  return CreateTable(id, true);
}

TEST(TableIndexTest, PointLookup) {
  const auto id = "t1";
  const auto missing_id = "t2";
  auto table = CreateTable(id);
  auto table_index = TableIndex();
  table_index.AddOrReplace(table);
  auto result = table_index.FindTableOrNull(id);
  EXPECT_THAT(table_index.FindTableOrNull(id), tt::Eq(table));
  EXPECT_THAT(table_index.FindTableOrNull(missing_id), tt::IsNull());
}

TEST(TableIndexTest, GetAllTables) {
  std::vector<TableInfoPtr> tables;
  std::vector<TableId> ids = {"t1", "t2", "t3"};
  auto table_index = TableIndex();
  for (const auto& id : ids) {
    auto table = CreateTable(id);
    tables.push_back(table);
    table_index.AddOrReplace(table);
  }
  EXPECT_THAT(table_index.GetAllTables(), tt::UnorderedElementsAreArray(tables));
}

TEST(TableIndexTest, GetPrimaryTables) {
  auto colocated_table = CreateTable("colocated", true);
  auto parent_table = CreateParentTablegroupTable();
  ASSERT_TRUE(parent_table->IsColocationParentTable());
  auto regular_table = CreateTable("regular");
  TableIndex tables;
  tables.AddOrReplace(colocated_table);
  tables.AddOrReplace(parent_table);
  tables.AddOrReplace(regular_table);
  EXPECT_THAT(tables.GetPrimaryTables(), tt::UnorderedElementsAre(parent_table, regular_table));
}

// Existing system behaviour relies on the table data structure in the catalog manager doing
// silent replacements of existing entries, at least for system tables. See GH issue
// https://github.com/yugabyte/yugabyte-db/issues/15216
TEST(TableIndexTest, ReplaceExistingEntry) {
  auto id = "t";
  auto regular_table = CreateTable(id, /* colocated */ false);
  auto colocated_table = CreateTable(id, /* colocated */ true);
  TableIndex tables;
  tables.AddOrReplace(regular_table);
  EXPECT_THAT(tables.FindTableOrNull(id), tt::Eq(regular_table));
  tables.AddOrReplace(colocated_table);
  EXPECT_THAT(tables.FindTableOrNull(id), tt::Eq(colocated_table));
}

}  // namespace master
}  // namespace yb
