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

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using yb::tablet::Tablet;
using yb::tablet::TabletPeer;

DECLARE_int32(TEST_slowdown_alter_table_rpcs_ms);

namespace yb {
namespace pgwrapper {

class AlterTableWithConcurrentTxnTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    // This flag is set inside CatalogManager::SendAlterTableRequest() and
    // AsyncAlterTable::HandleResponse(). This flag will slow down async
    // alter table rpc's send request and response handler. This will result
    // in a heartbeat delay on the TServer and thus trigger ProcessTabletReport().
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_slowdown_alter_table_rpcs_ms) = 5000; // 5 seconds
    PgMiniTestBase::SetUp();
  }

 protected:
  void TriggerTServerLeaderChange() {
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      tserver::TabletServer* ts = cluster_->mini_tablet_server(i)->server();
      ts->maintenance_manager()->Shutdown();
      tserver::TSTabletManager* tm = ts->tablet_manager();
      auto peers = tm->GetTabletPeers();
      for (const std::shared_ptr<TabletPeer>& peer : peers) {
        Tablet* tablet = peer->tablet();
        if (peer->LeaderTerm()) {
          tm->ApplyChange(tablet->tablet_id(), nullptr);
        }
      }
    }
  }
};

TEST_F(AlterTableWithConcurrentTxnTest, TServerLeaderChange) {
  auto resource_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(resource_conn.Execute("CREATE TABLE p (a INT PRIMARY KEY)"));
  ASSERT_OK(resource_conn.Execute("INSERT INTO p VALUES (1)"));

  auto txn_conn = ASSERT_RESULT(Connect());
  auto ddl_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.Fetch("SELECT * FROM p"));
  ASSERT_OK(txn_conn.Execute("BEGIN"));
  ASSERT_OK(txn_conn.Execute("INSERT INTO p VALUES (2)"));

  ASSERT_OK(ddl_conn.Execute("ALTER TABLE p ADD COLUMN b TEXT"));
  ASSERT_NO_FATALS(TriggerTServerLeaderChange());

  ASSERT_NOK(txn_conn.Execute("COMMIT"));

  auto value = ASSERT_RESULT(txn_conn.FetchRow<PGUint64>(Format("SELECT COUNT(*) FROM $0", "p")));
  ASSERT_EQ(value, 1);
}

} // namespace pgwrapper
} // namespace yb
