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

#include "yb/common/wire_protocol.h"

#include "yb/master/master-test_base.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/string_case.h"

DECLARE_bool(TEST_echo_service_enabled);

using namespace std::literals;

namespace yb {
using std::string;

const auto kTestEchoServiceKind = StatefulServiceKind::TEST_ECHO;

namespace master {
class StatefulServiceTest : public MasterTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_echo_service_enabled) = true;
    MasterTestBase::SetUp();
  }
};

TEST_F(StatefulServiceTest, TestCreateStatefulService) {
  // The leader creates the echo service's table once the flag is on.
  const auto table_name_prefix = ToLowerCase(StatefulServiceKind_Name(kTestEchoServiceKind));
  TableId service_table_id;
  SysTablesEntryPB service_table;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto tables = VERIFY_RESULT(SysCatalogRowsOf<SysTablesEntryPB>(SysRowEntryType::TABLE));
        for (auto& [id, table] : tables) {
          if (table.name().find(table_name_prefix) != string::npos) {
            service_table_id = id;
            service_table = std::move(table);
            return true;
          }
        }
        return false;
      },
      30s, "the echo service's table"));

  ASSERT_EQ(service_table.hosted_stateful_services_size(), 1);
  ASSERT_EQ(service_table.hosted_stateful_services(0), kTestEchoServiceKind);

  // Validate the tablet is created with the correct service kind.
  std::vector<TabletId> tablets;
  for (const auto& [id, tablet] :
       ASSERT_RESULT(SysCatalogRowsOf<SysTabletsEntryPB>(SysRowEntryType::TABLET))) {
    if (tablet.table_id() == service_table_id) {
      tablets.push_back(id);
    }
  }
  ASSERT_EQ(tablets.size(), 1);

  // Validate the tablet cannot be split.
  SplitTabletRequestPB req;
  req.set_tablet_id(tablets[0]);
  SplitTabletResponsePB resp;
  ASSERT_OK(proxy_admin_->SplitTablet(req, &resp, ResetAndGetController()));
  ASSERT_TRUE(resp.has_error());
  ASSERT_STR_CONTAINS(
      resp.error().status().message(),
      "Tablet splitting is not supported on tables that host stateful services");
}
}  // namespace master
}  // namespace yb
