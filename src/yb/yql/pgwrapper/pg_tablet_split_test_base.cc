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
#include "yb/docdb/doc_key.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

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

Status PgTabletSplitTestBase::SplitSingleTablet(const TableId& table_id) {
  auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
  auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  if (tablets.size() != 1) {
    return STATUS_FORMAT(InternalError, "Expected single tablet, found $0.", tablets.size());
  }
  auto tablet_id = tablets.at(0)->tablet_id();

  return master->catalog_manager().SplitTablet(tablet_id, master::ManualSplit::kTrue);
}

Status PgTabletSplitTestBase::InvokeSplitTabletRpc(const std::string& tablet_id) {
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  master::SplitTabletResponsePB resp;

  auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
  RETURN_NOT_OK(master->catalog_manager_impl().SplitTablet(&req, &resp, nullptr));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Status PgTabletSplitTestBase::InvokeSplitTabletRpcAndWaitForSplitCompleted(
    tablet::TabletPeerPtr peer) {
  SCHECK_NOTNULL(peer.get());
  RETURN_NOT_OK(InvokeSplitTabletRpc(peer->tablet_id()));
  return WaitFor([&]() -> Result<bool> {
    const auto leaders =
        ListTableActiveTabletLeadersPeers(cluster_.get(), peer->tablet_metadata()->table_id());
    return leaders.size() == 2;
  }, 15s * kTimeMultiplier, "Wait for split completion.");
}

Status PgTabletSplitTestBase::DisableCompaction(std::vector<tablet::TabletPeerPtr>* peers) {
  for (auto& peer : *peers) {
    RETURN_NOT_OK(peer->tablet()->doc_db().regular->SetOptions({
        {"level0_file_num_compaction_trigger", std::to_string(std::numeric_limits<int32>::max())}
    }));
  }
  return Status::OK();
}

size_t PgTabletSplitTestBase::NumTabletServers() {
  return 1;
}

} // namespace pgwrapper
} // namespace yb
