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

#include "yb/master/master-test_base.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/catalog_manager.h"
#include "yb/util/string_case.h"

namespace yb {
using std::string;

const auto kTestEchoServiceKind = StatefulServiceKind::TEST_ECHO;

namespace master {
class StatefulServiceTest : public MasterTestBase {};

TEST_F(StatefulServiceTest, TestCreateStatefulService) {
  CatalogManager& catalog_manager = mini_master_->catalog_manager_impl();

  auto epoch = catalog_manager.GetLeaderEpochInternal();
  ASSERT_OK(catalog_manager.CreateTestEchoService(epoch));

  const auto table_name_prefix = ToLowerCase(StatefulServiceKind_Name(kTestEchoServiceKind));
  auto tables = catalog_manager.GetTables(GetTablesMode::kAll);
  TableInfoPtr service_table;
  for (auto& table : tables) {
    if (table->name().find(table_name_prefix) != string::npos) {
      service_table = table;
    }
  }

  ASSERT_EQ(service_table->GetHostedStatefulServices().size(), 1);
  ASSERT_EQ(service_table->GetHostedStatefulServices()[0], kTestEchoServiceKind);

  // Validate the tablet is created with the correct service kind.
  auto tablets = ASSERT_RESULT(service_table->GetTablets());
  ASSERT_EQ(tablets.size(), 1);

  // Validate the tablet cannot be split.
  ASSERT_NOK(catalog_manager.tablet_split_manager()->ValidateSplitCandidateTable(service_table));
}
}  // namespace master
}  // namespace yb
