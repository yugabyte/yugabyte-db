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

#include <string>
#include <vector>

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/backup.proxy.h"
#include "yb/tserver/tserver_admin.proxy.h"

#include "yb/util/backoff_waiter.h"

namespace yb {
namespace integration_tests {

using client::internal::GetSchema;
using rpc::RpcController;
using std::string;
using std::vector;
using tablet::CloneTabletRequestPB;
using tserver::CloneTabletResponsePB;
using tserver::TabletSnapshotOpRequestPB;
using tserver::TabletSnapshotOpResponsePB;

class CloneTabletExternalItest : public YBTableTestBase {
 protected:
  bool use_external_mini_cluster() override { return true; }

  bool use_yb_admin_client() override { return true; }

  bool enable_ysql() override { return false; }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->replication_factor = 1;
  }

  size_t num_tablet_servers() override {
    return 1;
  }

  size_t num_masters() override {
    return 1;
  }

  int num_tablets() override {
    return 1;
  }

  Status CreateTabletSnapshot(const TabletId& tablet_id, const TxnSnapshotId& snapshot_id) {
    RpcController rpc;
    TabletSnapshotOpRequestPB req;
    TabletSnapshotOpResponsePB resp;
    req.set_operation(TabletSnapshotOpRequestPB::CREATE_ON_TABLET);
    req.set_dest_uuid(external_mini_cluster()->tablet_server(0)->uuid());
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    req.add_tablet_id(tablet_id);
    auto backup_proxy =
        external_mini_cluster()->GetTServerProxy<tserver::TabletServerBackupServiceProxy>(0);
    RETURN_NOT_OK(backup_proxy.TabletSnapshotOp(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status CloneTablet(const TabletId& tablet_id, uint32_t clone_request_seq_no) {
    SchemaPB schema_pb;
    auto schema = GetSchema(schema_);
    schema.InitColumnIdsByDefault();
    SchemaToPB(schema, &schema_pb);

    RpcController rpc;
    tablet::CloneTabletRequestPB req;
    tserver::CloneTabletResponsePB resp;

    req.set_dest_uuid(external_mini_cluster()->tablet_server(0)->uuid());
    req.set_tablet_id(tablet_id);
    req.set_target_tablet_id(kTargetTabletId);
    req.set_source_snapshot_id(kSourceSnapshotId.data(), kSourceSnapshotId.size());
    req.set_target_snapshot_id(kTargetSnapshotId.data(), kTargetSnapshotId.size());
    req.set_target_table_id(kTargetTableId);
    req.set_target_namespace_name(kTargetNamespaceName);
    req.set_clone_request_seq_no(clone_request_seq_no);
    req.set_target_pg_table_id("target_pg_table_id");
    *req.mutable_target_schema() = schema_pb;
    auto admin_proxy =
        external_mini_cluster()->GetTServerProxy<tserver::TabletServerAdminServiceProxy>(0);
    RETURN_NOT_OK(admin_proxy.CloneTablet(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Status RestoreTablet(
      const TabletId& tablet_id, const TxnSnapshotId& snapshot_id,
      const HybridTime restore_time) {
    RETURN_NOT_OK(WaitFor([&]() {
      return external_mini_cluster()->GetTabletLeaderIndex(
          tablet_id, true /* require_lease */).ok();
    }, 5s, "Wait for tablet to have a leader."));

    RpcController rpc;
    tserver::TabletSnapshotOpRequestPB req;
    tserver::TabletSnapshotOpResponsePB resp;
    req.set_dest_uuid(external_mini_cluster()->tablet_server(0)->uuid());
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    req.add_tablet_id(tablet_id);
    req.set_snapshot_hybrid_time(restore_time.ToUint64());
    req.set_operation(TabletSnapshotOpRequestPB::RESTORE_ON_TABLET);
    auto backup_proxy =
        external_mini_cluster()->GetTServerProxy<tserver::TabletServerBackupServiceProxy>(0);
    RETURN_NOT_OK(backup_proxy.TabletSnapshotOp(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Result<TabletId> GetSourceTabletId() {
    auto tablets = VERIFY_RESULT(
        external_mini_cluster()->ListTablets(external_mini_cluster()->tablet_server(0)));
    if (tablets.status_and_schema_size() != 1) {
      return STATUS_FORMAT(
          IllegalState, "Expected 1 tablet, got $0", tablets.status_and_schema_size());
    }
    return tablets.status_and_schema(0).tablet_status().tablet_id();
  }

  const TxnSnapshotId kSourceSnapshotId = TxnSnapshotId::GenerateRandom();
  const TxnSnapshotId kTargetSnapshotId = TxnSnapshotId::GenerateRandom();
  const string kTargetTableId = "target_table_id";
  const string kTargetTabletId = "target_tablet_id";
  const string kTargetNamespaceName = "target_namespace_name";
};

class CloneTabletExternalCrashItest:
    public CloneTabletExternalItest,
    public testing::WithParamInterface<std::string> {};

TEST_P(CloneTabletExternalCrashItest, CrashDuringApply) {
  ASSERT_OK(external_mini_cluster()->SetFlagOnTServers(GetParam(), "true"));

  PutKeyValue("key1", "value1");

  auto restore_time = HybridTime::FromMicros(ASSERT_RESULT(WallClock()->Now()).time_point);
  auto source_tablet_id = ASSERT_RESULT(GetSourceTabletId());
  ASSERT_OK(CreateTabletSnapshot(source_tablet_id, kSourceSnapshotId));

  auto* ts = external_mini_cluster()->tablet_server(0);

  // Cloning the tablet should cause the tserver to crash during apply.
  const auto clone_request_seq_no = 100;
  ASSERT_NOK(CloneTablet(source_tablet_id, clone_request_seq_no));
  ASSERT_OK(external_mini_cluster()->WaitForTSToCrash(ts, 30s));

  // Restart the tserver and wait for all tablets to start running.
  ASSERT_OK(ts->Restart(
      ExternalMiniClusterOptions::kDefaultStartCqlProxy,
      {{GetParam(), "false"}}));

  auto apply_complete_lw = LogWaiter(ts, Format(
      "Clone operation for tablet $0 with seq_no $1 has been applied", source_tablet_id,
      clone_request_seq_no));
  ASSERT_OK(apply_complete_lw.WaitFor(10s));

  ASSERT_OK(RestoreTablet(kTargetTabletId, kTargetSnapshotId, restore_time));
}

INSTANTIATE_TEST_CASE_P(
    CloneTabletExternalItest,
    CloneTabletExternalCrashItest,
    ::testing::Values(
        /* Crash after the target tablet has snapshots but is not marked as TABLET_DATA_READY.
           Note that the partially created tablet will not be registered as part of bootstrap and
           will eventually be deleted (since it is in the TABLET_DATA_INIT_STARTED state), but the
           clone op needs to be able to clean it up to retry the apply. */
        // TODO(asrivastava): Re-enable this once the TransitionInProgressDeleter bug is fixed
        // "TEST_crash_before_clone_target_marked_ready",
        /* Crash before marking a clone as completed but after creating the target tablet and
           marking it as TABLET_DATA_READY. We will re-apply the clone op as a no-op except for
           setting the seq_no on the source tablet. */
        "TEST_crash_before_mark_clone_attempted"));

} // namespace integration_tests
} // namespace yb
