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

#include "yb/yql/pgwrapper/pg_tablet_split_test_base.h"

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/dockv/doc_key.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/string_case.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/util/tsan_util.h"

using namespace std::literals;

namespace yb {
namespace pgwrapper {

namespace {

constexpr std::chrono::duration<int64> kRpcTimeout = std::chrono::seconds(60) * kTimeMultiplier;

const std::string empty_partition_key;

bool IsTabletInCollection(const master::TabletInfoPtr& tablet, const master::TabletInfos& tablets) {
  return tablets.end() != std::find_if(
      tablets.begin(), tablets.end(),
      [&tablet](const master::TabletInfoPtr& p) { return p->tablet_id() == tablet->tablet_id(); });
}

} // namespace

Result<master::TabletInfoPtr> SelectFirstTabletPolicy::operator()(
    const PartitionKeyTabletMap& tablets) {
  if (tablets.empty()) {
    return nullptr;
  }
  return tablets.begin()->second;
}

Result<master::TabletInfoPtr> SelectLastTabletPolicy::operator()(
    const PartitionKeyTabletMap& tablets) {
  if (tablets.empty()) {
    return nullptr;
  }
  return tablets.rbegin()->second;
}

Result<master::TabletInfoPtr> SelectMiddleTabletPolicy::operator()(
    const PartitionKeyTabletMap& tablets) {
  if (tablets.empty()) {
    return nullptr;
  }
  const size_t middle_pos = (tablets.size() - 1) / 2;
  auto it = tablets.begin();
  std::advance(it, middle_pos);
  return it->second;
}

TabletSelector::TabletSelector(
    size_t max_tablet_selections, SelectTabletCallback selector_policy,
    VerifyTabletsCallback tablet_verifier)
  : max_selections(max_tablet_selections), selections_count(0)
  , policy(std::move(selector_policy))
  , verifier(std::move(tablet_verifier)) {
}

Result<master::TabletInfoPtr> TabletSelector::operator()(const PartitionKeyTabletMap& tablets) {
  if (selections_count++ >= max_selections) {
    return nullptr;
  }
  RETURN_NOT_OK(verifier(tablets));
  return policy(tablets);
}


PgTabletSplitTestBase::PgTabletSplitTestBase() = default;
PgTabletSplitTestBase::~PgTabletSplitTestBase() = default;

void PgTabletSplitTestBase::SetUp() {
  PgMiniTestBase::SetUp();
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
}

Result<TabletId> PgTabletSplitTestBase::GetOnlyTabletId(const TableId& table_id) {
  const auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  SCHECK_EQ(
      tablets.size(), 1, InternalError,
      Format("Expected single tablet, found $0.", tablets.size()));
  return tablets.front()->tablet_id();
}

Status PgTabletSplitTestBase::SplitTablet(const TabletId& tablet_id) {
  auto epoch = VERIFY_RESULT(catalog_manager())->GetLeaderEpochInternal();
  return VERIFY_RESULT(catalog_manager())
      ->SplitTablet(tablet_id, master::ManualSplit::kTrue, epoch);
}

Status PgTabletSplitTestBase::SplitSingleTablet(const TableId& table_id) {
  return SplitTablet(VERIFY_RESULT(GetOnlyTabletId(table_id)));
}

Status PgTabletSplitTestBase::SplitSingleTabletAndWaitForActiveChildTablets(
    const TableId& table_id) {
  RETURN_NOT_OK(SplitSingleTablet(table_id));
  return WaitForSplitCompletion(table_id, /* expected_active_leaders = */ 2);
}

Status PgTabletSplitTestBase::InvokeSplitTabletRpc(const std::string& tablet_id) {
  auto master_admin_proxy = master::MasterAdminProxy(
      proxy_cache_.get(), VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->bound_rpc_addr());

  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);

  rpc::RpcController controller;
  controller.set_timeout(kRpcTimeout);
  master::SplitTabletResponsePB resp;
  RETURN_NOT_OK(master_admin_proxy.SplitTablet(req, &resp, &controller));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Status PgTabletSplitTestBase::InvokeSplitTabletRpcAndWaitForSplitCompleted(
    const std::string& tablet_id) {
  const auto catalog_mgr = VERIFY_RESULT(catalog_manager());
  const auto tablet = VERIFY_RESULT(catalog_mgr->GetTabletInfo(tablet_id));

  // Get current number of tablets for the table.
  const auto table = catalog_mgr->GetTableInfo(tablet->table()->id());

  return DoInvokeSplitTabletRpcAndWaitForCompletion(table, tablet);
}

Status PgTabletSplitTestBase::InvokeSplitsAndWaitForCompletion(
    const TableId& table_id, SelectTabletCallback select_tablet) {
  // Get initial tables.
  const auto catalog_mgr = VERIFY_RESULT(catalog_manager());
  const auto table = catalog_mgr->GetTableInfo(table_id);

  // Loop while a tablet can be picked.
  master::TabletInfoPtr parent;
  while (true) {
    const auto tablets = GetTabletsByPartitionKey(table);
    const auto tablet  = VERIFY_RESULT(select_tablet(tablets));
    if (!tablet) {
      break;
    }

    // Wait for parent tablet is deleted. This may be required by a series of splits.
    if (parent) {
      RETURN_NOT_OK(itest::WaitForTabletIsDeletedOrHidden(
          catalog_mgr, parent->tablet_id(), MonoDelta::FromSeconds(5) * kTimeMultiplier));
    }
    parent = tablet;

    // Invoke split tablet RPC and wait for the split is done.
    RETURN_NOT_OK(DoInvokeSplitTabletRpcAndWaitForCompletion(table, tablet));
  }

  return Status::OK();
}

Status PgTabletSplitTestBase::DisableCompaction(std::vector<tablet::TabletPeerPtr>* peers) {
  for (auto& peer : *peers) {
    RETURN_NOT_OK(peer->tablet()->regular_db()->SetOptions({
        {"level0_file_num_compaction_trigger", std::to_string(std::numeric_limits<int32>::max())}
    }));
  }
  return Status::OK();
}

Status PgTabletSplitTestBase::WaitForSplitCompletion(
    const TableId& table_id, const size_t expected_active_leaders) {
  return WaitFor(
      [cluster = cluster_.get(), &table_id, expected_active_leaders]() -> Result<bool> {
        return ListTableActiveTabletLeadersPeers(cluster, table_id).size() ==
               expected_active_leaders;
      },
      15s * kTimeMultiplier, "Wait for split completion.");
}

size_t PgTabletSplitTestBase::NumTabletServers() {
  return 1;
}

Status PgTabletSplitTestBase::DoInvokeSplitTabletRpcAndWaitForCompletion(
    const master::TableInfoPtr& table, const master::TabletInfoPtr& tablet) {
  // Keep current tablets.
  const auto tablets = table->GetTablets();

  // Sanity check that tablet belongs to the table.
  if (!IsTabletInCollection(tablet, tablets)) {
    return STATUS(InvalidArgument, "The tablet does not belong to table's tablets list.");
  }

  // Send split RPC.
  RETURN_NOT_OK(InvokeSplitTabletRpc(tablet->tablet_id()));

  // Wait for new tablets are added.
  RETURN_NOT_OK(WaitForTableActiveTabletLeadersPeers(
      cluster_.get(), table->id(), tablets.size() + 1));

  // Wait until split is replicated across all tablet servers.
    RETURN_NOT_OK(WaitAllReplicasReady(
        cluster_.get(), table->id(), MonoDelta::FromSeconds(20) * kTimeMultiplier));

  // Select new tablets ids
  const auto all_tablets = table->GetTablets();
  std::vector<TabletId> new_tablet_ids;
  new_tablet_ids.reserve(all_tablets.size());
  for (const auto& t : all_tablets) {
    if (!IsTabletInCollection(t, tablets)) {
      new_tablet_ids.push_back(t->tablet_id());
    }
  }

  // Wait for new peers are fully compacted.
  return WaitForPeersPostSplitCompacted(cluster_.get(), new_tablet_ids);
}

PartitionKeyTabletMap GetTabletsByPartitionKey(const master::TableInfoPtr& table) {
  // Get tablets and keep in sorted order, we assume partition_key cannot be changed
  // as we are holding a std::string_view to partition_key_start.
  PartitionKeyTabletMap tablets;
  for (auto& t : table->GetTablets()) {
    const auto& partition = t->LockForRead()->pb.partition();
    const auto& partition_key =
        partition.has_partition_key_start() ? partition.partition_key_start()
                                            : empty_partition_key;
    tablets.emplace(partition_key, t);
  }
  return tablets;
}

} // namespace pgwrapper
} // namespace yb
