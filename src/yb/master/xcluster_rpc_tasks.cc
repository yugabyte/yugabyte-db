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

#include "yb/master/xcluster_rpc_tasks.h"

#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"

#include "yb/cdc/xcluster_util.h"

#include "yb/gutil/callback.h"

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_client.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/logging.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"

DEFINE_RUNTIME_int32(list_snapshot_backoff_increment_ms, 1000,
    "Number of milliseconds added to the delay between retries of fetching state of a snapshot. "
    "This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(list_snapshot_backoff_increment_ms, stable);
TAG_FLAG(list_snapshot_backoff_increment_ms, advanced);

DEFINE_RUNTIME_int32(list_snapshot_max_backoff_sec, 10,
    "Maximum number of seconds to delay between retries of fetching state of a snpashot. This "
    "delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(list_snapshot_max_backoff_sec, stable);
TAG_FLAG(list_snapshot_max_backoff_sec, advanced);

DEFINE_test_flag(
    bool, xcluster_fail_to_send_create_snapshot_request, false,
    "Fail to send a CreateSnapshot request to the producer.");

DEFINE_test_flag(
    bool, xcluster_fail_create_producer_snapshot, false,
    "In the SetupReplicationWithBootstrap flow, fail to create snapshot on producer.");

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_string(certs_dir);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_for_client_dir);
DECLARE_string(certs_for_cdc_dir);

namespace yb {
namespace master {
using yb::master::SysSnapshotEntryPB_State;

Result<std::shared_ptr<XClusterRpcTasks>> XClusterRpcTasks::CreateWithMasterAddrs(
    const xcluster::ReplicationGroupId& replication_group_id, const std::string& master_addrs) {
  // NOTE: This is currently an expensive call (5+ sec). Encountered during Task #10611.
  auto xcluster_rpc_tasks = std::make_shared<XClusterRpcTasks>();
  std::string dir;

  if (FLAGS_use_node_to_node_encryption) {
    rpc::MessengerBuilder messenger_builder("cdc-rpc-tasks");
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      dir = JoinPathSegments(
          FLAGS_certs_for_cdc_dir,
          xcluster::GetOriginalReplicationGroupId(replication_group_id).ToString());
    }
    xcluster_rpc_tasks->secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        dir, "", "", server::SecureContextType::kInternal, &messenger_builder));
    xcluster_rpc_tasks->messenger_ = VERIFY_RESULT(messenger_builder.Build());
  }

  LOG(INFO) << __func__ << " before";
  xcluster_rpc_tasks->yb_client_ =
      VERIFY_RESULT(yb::client::YBClientBuilder()
                        .add_master_server_addr(master_addrs)
                        .default_admin_operation_timeout(
                            MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
                        .Build(xcluster_rpc_tasks->messenger_.get()));
  LOG(INFO) << __func__ << " after";

  return xcluster_rpc_tasks;
}

XClusterRpcTasks::~XClusterRpcTasks() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Result<client::YBClient*> XClusterRpcTasks::UpdateMasters(const std::string& master_addrs) {
  RETURN_NOT_OK(yb_client_->SetMasterAddresses(master_addrs));
  return yb_client_.get();
}

Result<google::protobuf::RepeatedPtrField<TabletLocationsPB>> XClusterRpcTasks::GetTableLocations(
    const std::string& table_id) {
  google::protobuf::RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(yb_client_->GetTabletsFromTableId(table_id, 0 /* max tablets */, &tablets));
  return tablets;
}

Status XClusterRpcTasks::BootstrapProducer(
    const NamespaceIdentifierPB& producer_namespace, const std::vector<client::YBTableName>& tables,
    BootstrapProducerCallback callback) {
  SCHECK(!tables.empty(), InvalidArgument, "Empty tables");
  const auto& db_type = producer_namespace.database_type();
  const auto& namespace_name = producer_namespace.name();
  auto has_schema_name = (db_type == YQL_DATABASE_PGSQL);
  std::vector<std::string> table_names;
  table_names.reserve(tables.size());

  std::vector<std::string> schema_names;
  schema_names.reserve(has_schema_name ? tables.size() : 0);

  for (auto& table : tables) {
    SCHECK_EQ(
        table.namespace_type(), db_type, IllegalState,
        Format(
            "Table db type $0 does not match given db type $1", table.namespace_type(), db_type));
    SCHECK_EQ(
        table.namespace_name(), namespace_name, IllegalState,
        Format(
            "Table namespace $0 does not match given namespace $1", table.namespace_name(),
            namespace_name));

    table_names.emplace_back(std::move(table.table_name()));
    if (has_schema_name) {
      schema_names.emplace_back(std::move(table.pgschema_name()));
    }
  }

  std::promise<Result<TableBootstrapIdsMap>> promise;
  RETURN_NOT_OK(yb_client_->BootstrapProducer(
      db_type, namespace_name, schema_names, table_names,
      [this, callback](client::BootstrapProducerResult bootstrap_result) {
        callback.Run(HandleBootstrapProducerResponse(std::move(bootstrap_result)));
      }));

  return Status::OK();
}

Result<TableBootstrapIdsMap> XClusterRpcTasks::HandleBootstrapProducerResponse(
    client::BootstrapProducerResult bootstrap_result) {
  auto [table_ids, bootstrap_ids, _] = VERIFY_RESULT(std::move(bootstrap_result));
  SCHECK_EQ(
      table_ids.size(), bootstrap_ids.size(), IllegalState,
      Format("Received $0 table ids and $1 bootstrap ids", table_ids.size(), bootstrap_ids.size()));

  TableBootstrapIdsMap table_bootstrap_ids;
  for (size_t i = 0; i < table_ids.size(); ++i) {
    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_ids.at(i)));
    table_bootstrap_ids.insert_or_assign(table_ids[i], stream_id);
  }

  return table_bootstrap_ids;
}

Result<SnapshotInfoPB> XClusterRpcTasks::CreateSnapshot(
    const std::vector<client::YBTableName>& tables, TxnSnapshotId* snapshot_id) {
  if (PREDICT_FALSE(FLAGS_TEST_xcluster_fail_to_send_create_snapshot_request)) {
    return STATUS(Aborted, "Test failure");
  }

  std::promise<Result<SnapshotInfoPB>> promise;
  RETURN_NOT_OK(yb_client_->CreateSnapshot(
      tables, [this, &promise, &snapshot_id](Result<TxnSnapshotId> snapshot_result) {
        if (!snapshot_result.ok()) {
          promise.set_value(ResultToStatus(snapshot_result));
          return;
        }
        *snapshot_id = std::move(*snapshot_result);
        promise.set_value(CreateSnapshotCallback(*snapshot_id));
      }));

  return promise.get_future().get();
}

Result<SnapshotInfoPB> XClusterRpcTasks::CreateSnapshotCallback(const TxnSnapshotId& snapshot_id) {
  const auto delay_increment =
      MonoDelta::FromMilliseconds(FLAGS_list_snapshot_backoff_increment_ms);
  const auto max_delay_time = MonoDelta::FromSeconds(FLAGS_list_snapshot_max_backoff_sec);
  auto delay_time = delay_increment;
  uint32_t attempts = 1;
  auto start_time = CoarseMonoClock::Now();

  SnapshotInfoPB result;
  while (true) {
    auto snapshots =
        VERIFY_RESULT(yb_client_->ListSnapshots(snapshot_id, /* prepare_for_backup = */ true));
    SCHECK_EQ(
        snapshots.size(), 1, IllegalState,
        Format("Received $0 snapshots when expecting 1", snapshots.size()));

    auto& snapshot = *snapshots.begin();
    auto id = TryFullyDecodeTxnSnapshotId(snapshot.id());
    SCHECK_EQ(
        id, snapshot_id, IllegalState,
        Format("Received snapshot $0 when expecting $1", id, snapshot_id));

    auto state = snapshot.entry().state();
    if (state == SysSnapshotEntryPB_State_CREATING) {
      auto total_time = std::to_string((CoarseMonoClock::Now() - start_time).count()) + "ms";
      YB_LOG_EVERY_N(INFO, 5) << Format(
          "Snapshot $0 is still running. Attempts: $1, Total Time: $2. Sleeping for $3ms and "
          "retrying...",
          snapshot_id, attempts, total_time, delay_time);

      // Delay before retrying so that we don't accidentally DDoS producer.
      // Time increases linearly by delay_increment up to max_delay.
      SleepFor(delay_time);
      delay_time = std::min(max_delay_time, delay_time + delay_increment);
      attempts++;
      continue;
    }

    SCHECK_EQ(
        state, SysSnapshotEntryPB_State_COMPLETE, IllegalState,
        Format("Snapshot failed on state: $0", state));
    result = std::move(snapshot);
    break;
  }

  if (PREDICT_FALSE(FLAGS_TEST_xcluster_fail_create_producer_snapshot)) {
    return STATUS(Aborted, "Test failure");
  }
  return result;
}

}  // namespace master
}  // namespace yb
