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

#include "yb/integration-tests/mini_cluster_utils.h"

#include "yb/client/yb_table_name.h"

#include "yb/common/common_util.h"
#include "yb/common/schema_pbutil.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master-test-util.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb {

size_t CountRunningTransactions(MiniCluster* cluster) {
  size_t result = 0;
  auto peers = ListTabletPeers(cluster, ListPeersFilter::kAll);
  for (const auto &peer : peers) {
    auto tablet = peer->shared_tablet();
    if (!tablet)
      continue;
    auto participant = tablet->transaction_participant();
    result += participant ? participant->TEST_GetNumRunningTransactions() : 0;
  }
  return result;
}

void AssertRunningTransactionsCountLessOrEqualTo(MiniCluster* cluster,
                                                 size_t max_remaining_txns_per_tablet) {
  MonoTime deadline = MonoTime::Now() + 15s * kTimeMultiplier;
  bool has_bad = false;
  for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
    auto server = cluster->mini_tablet_server(i)->server();
    std::vector<std::shared_ptr<tablet::TabletPeer>> tablets;
    auto status = Wait([server, &tablets] {
          tablets = server->tablet_manager()->GetTabletPeers();
          for (const auto& peer : tablets) {
            if (peer->shared_tablet() == nullptr) {
              return false;
            }
          }
          return true;
        }, deadline, "Wait until all peers have tablets");
    if (!status.ok()) {
      has_bad = true;
      for (const auto& peer : tablets) {
        if (peer->shared_tablet() == nullptr) {
          LOG(ERROR) << Format(
              "T $1 P $0: Tablet object is not created",
              server->permanent_uuid(), peer->tablet_id());
        }
      }
      continue;
    }
    for (const auto& peer : tablets) {
      // Keep a ref to guard against Shutdown races.
      auto tablet = peer->shared_tablet();
      if (!tablet) continue;
      auto participant = tablet->transaction_participant();
      if (participant) {
        auto status = Wait([participant, max_remaining_txns_per_tablet] {
              return participant->TEST_GetNumRunningTransactions() <= max_remaining_txns_per_tablet;
            },
            deadline,
            "Wait until no transactions are running");
        if (!status.ok()) {
          LOG(ERROR) << Format(
              "T $1 P $0: Transactions: $2",
              server->permanent_uuid(), peer->tablet_id(),
              participant->TEST_GetNumRunningTransactions());
          has_bad = true;
        }
      }
    }
  }
  ASSERT_FALSE(has_bad);
}

void AssertNoRunningTransactions(MiniCluster* cluster) {
  AssertRunningTransactionsCountLessOrEqualTo(cluster, 0);
}

void CreateTabletForTesting(MiniCluster* cluster,
                            const client::YBTableName& table_name,
                            const Schema& schema,
                            std::string* tablet_id,
                            std::string* table_id) {
  auto* mini_master = cluster->mini_master();
  auto epoch = mini_master->catalog_manager().GetLeaderEpochInternal();
  {
    master::CreateNamespaceRequestPB req;
    master::CreateNamespaceResponsePB resp;
    req.set_name(table_name.resolved_namespace_name());

    const Status s = mini_master->catalog_manager().CreateNamespace(
        &req, &resp, /* rpc::RpcContext* */ nullptr, epoch);
    ASSERT_TRUE(s.ok() || s.IsAlreadyPresent()) << " status=" << s.ToString();
  }
  {
    master::CreateTableRequestPB req;
    master::CreateTableResponsePB resp;

    req.set_name(table_name.table_name());
    req.mutable_namespace_()->set_name(table_name.resolved_namespace_name());

    SchemaToPB(schema, req.mutable_schema());
    ASSERT_OK(mini_master->catalog_manager().CreateTable(
        &req, &resp, /* rpc::RpcContext* */ nullptr, epoch));
  }

  int wait_time = 1000;
  bool is_table_created = false;
  for (int i = 0; i < 80; ++i) {
    master::IsCreateTableDoneRequestPB req;
    master::IsCreateTableDoneResponsePB resp;

    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    ASSERT_OK(mini_master->catalog_manager().IsCreateTableDone(&req, &resp));
    if (resp.done()) {
      is_table_created = true;
      break;
    }

    VLOG(1) << "Waiting for table '" << table_name.ToString() << "'to be created";

    SleepFor(MonoDelta::FromMicroseconds(wait_time));
    wait_time = std::min(wait_time * 5 / 4, 1000000);
  }
  ASSERT_TRUE(is_table_created);

  {
    master::GetTableSchemaRequestPB req;
    master::GetTableSchemaResponsePB resp;
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
    ASSERT_OK(mini_master->catalog_manager().GetTableSchema(&req, &resp));
    ASSERT_TRUE(resp.create_table_done());
    if (table_id != nullptr) {
      *table_id = resp.identifier().table_id();
    }
  }

  master::GetTableLocationsResponsePB resp;
  const auto num_tablets = GetInitialNumTabletsPerTable(
      table_name.namespace_type(), cluster->num_tablet_servers());
  ASSERT_OK(WaitForRunningTabletCount(
        mini_master, table_name, num_tablets, &resp));
  *tablet_id = resp.tablet_locations(0).tablet_id();
  LOG(INFO) << "Got tablet " << *tablet_id << " for table " << table_name.ToString();
}

} // namespace yb
