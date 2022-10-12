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

#include <iostream>

#include <boost/algorithm/string.hpp>

#include <rapidjson/document.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"

#include "yb/common/entity_ids.h"
#include "yb/common/json_util.h"
#include "yb/common/ql_type_util.h"
#include "yb/common/wire_protocol.h"

#include "yb/encryption/encryption_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/strings/split.h"

#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_encryption.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_util.h"

#include "yb/tools/yb-admin_cli.h"
#include "yb/tools/yb-admin_client.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tools/yb-admin_util.h"
#include "yb/util/cast.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/timestamp.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/test_util.h"

DEFINE_test_flag(int32, metadata_file_format_version, 0,
                 "Used in 'export_snapshot' metadata file format (0 means using latest format).");

DECLARE_bool(use_client_to_server_encryption);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb {
namespace tools {
namespace enterprise {

using namespace std::literals;

using std::cout;
using std::endl;
using std::pair;
using std::string;
using std::vector;
using std::unordered_map;

using google::protobuf::RepeatedPtrField;

using client::YBTableName;
using pb_util::ParseFromSlice;
using rpc::RpcController;

using master::ChangeEncryptionInfoRequestPB;
using master::ChangeEncryptionInfoResponsePB;
using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::DeleteSnapshotRequestPB;
using master::DeleteSnapshotResponsePB;
using master::IdPairPB;
using master::ImportSnapshotMetaRequestPB;
using master::ImportSnapshotMetaResponsePB;
using master::ImportSnapshotMetaResponsePB_TableMetaPB;
using master::ListSnapshotRestorationsRequestPB;
using master::ListSnapshotRestorationsResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::ListTablesRequestPB;
using master::ListTablesResponsePB;
using master::ListTabletServersResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::SnapshotInfoPB;
using master::SysNamespaceEntryPB;
using master::SysRowEntry;
using master::SysRowEntryType;
using master::BackupRowEntryPB;
using master::SysTablesEntryPB;
using master::SysSnapshotEntryPB;
using master::SysUDTypeEntryPB;

PB_ENUM_FORMATTERS(yb::master::SysSnapshotEntryPB::State);

namespace {

template <class Allocator>
Result<rapidjson::Value> SnapshotScheduleInfoToJson(
    const master::SnapshotScheduleInfoPB& schedule, Allocator* allocator) {
  rapidjson::Value json_schedule(rapidjson::kObjectType);
  AddStringField(
      "id", VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id())).ToString(), &json_schedule,
      allocator);

  const auto& filter = schedule.options().filter();
  string filter_output;
  // The user input should only have 1 entry, at namespace level.
  if (filter.tables().tables_size() == 1) {
    const auto& table_id = filter.tables().tables(0);
    if (table_id.has_namespace_()) {
      string database_type;
      if (table_id.namespace_().database_type() == YQL_DATABASE_PGSQL) {
        database_type = "ysql";
      } else if (table_id.namespace_().database_type() == YQL_DATABASE_CQL) {
        database_type = "ycql";
      }
      if (!database_type.empty()) {
        filter_output = Format("$0.$1", database_type, table_id.namespace_().name());
      }
    }
  }
  // If the user input was non standard, just display the whole debug PB.
  if (filter_output.empty()) {
    filter_output = filter.ShortDebugString();
    DCHECK(false) << "Non standard filter " << filter_output;
  }
  rapidjson::Value options(rapidjson::kObjectType);
  AddStringField("filter", filter_output, &options, allocator);
  auto interval_min = schedule.options().interval_sec() / MonoTime::kSecondsPerMinute;
  AddStringField("interval", Format("$0 min", interval_min), &options, allocator);
  auto retention_min = schedule.options().retention_duration_sec() / MonoTime::kSecondsPerMinute;
  AddStringField("retention", Format("$0 min", retention_min), &options, allocator);
  auto delete_time = HybridTime::FromPB(schedule.options().delete_time());
  if (delete_time) {
    AddStringField("delete_time", HybridTimeToString(delete_time), &options, allocator);
  }

  json_schedule.AddMember("options", options, *allocator);
  rapidjson::Value json_snapshots(rapidjson::kArrayType);
  for (const auto& snapshot : schedule.snapshots()) {
    rapidjson::Value json_snapshot(rapidjson::kObjectType);
    AddStringField(
        "id", VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id())).ToString(), &json_snapshot,
        allocator);
    auto snapshot_ht = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
    AddStringField("snapshot_time", HybridTimeToString(snapshot_ht), &json_snapshot, allocator);
    auto previous_snapshot_ht =
        HybridTime::FromPB(snapshot.entry().previous_snapshot_hybrid_time());
    if (previous_snapshot_ht) {
      AddStringField(
          "previous_snapshot_time", HybridTimeToString(previous_snapshot_ht), &json_snapshot,
          allocator);
    }
    json_snapshots.PushBack(json_snapshot, *allocator);
  }
  json_schedule.AddMember("snapshots", json_snapshots, *allocator);
  return json_schedule;
}

}  // namespace

Result<ListSnapshotsResponsePB> ClusterAdminClient::ListSnapshots(const ListSnapshotsFlags& flags) {
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListSnapshotsRequestPB req;
    req.set_list_deleted_snapshots(flags.Test(ListSnapshotsFlag::SHOW_DELETED));
    auto* flags_pb = req.mutable_detail_options();
    // Explicitly set all boolean fields as the defaults of this proto are inconsistent.
    if (flags.Test(ListSnapshotsFlag::SHOW_DETAILS)) {
      flags_pb->set_show_namespace_details(true);
      flags_pb->set_show_udtype_details(true);
      flags_pb->set_show_table_details(true);
      flags_pb->set_show_tablet_details(false);
    } else {
      flags_pb->set_show_namespace_details(false);
      flags_pb->set_show_udtype_details(false);
      flags_pb->set_show_table_details(false);
      flags_pb->set_show_tablet_details(false);
    }
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));
  return resp;
}

Status ClusterAdminClient::CreateSnapshot(
    const vector<YBTableName>& tables,
    const bool add_indexes,
    const int flush_timeout_secs) {
  if (flush_timeout_secs > 0) {
    const auto status = FlushTables(tables, add_indexes, flush_timeout_secs, false);
    if (status.IsTimedOut()) {
      cout << status.ToString(false) << " (ignored)" << endl;
    } else if (!status.ok() && !status.IsNotFound()) {
      return status;
    }
  }

  CreateSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    CreateSnapshotRequestPB req;
    for (const YBTableName& table_name : tables) {
      table_name.SetIntoTableIdentifierPB(req.add_tables());
    }

    req.set_add_indexes(add_indexes);
    req.set_add_ud_types(true); // No-op for YSQL.
    req.set_transaction_aware(true);
    return master_backup_proxy_->CreateSnapshot(req, &resp, rpc);
  }));

  cout << "Started snapshot creation: " << SnapshotIdToString(resp.snapshot_id()) << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateNamespaceSnapshot(const TypedNamespaceName& ns) {
  ListTablesResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListTablesRequestPB req;

    req.mutable_namespace_()->set_name(ns.name);
    req.mutable_namespace_()->set_database_type(ns.db_type);
    req.set_exclude_system_tables(true);
    req.add_relation_type_filter(master::USER_TABLE_RELATION);
    req.add_relation_type_filter(master::INDEX_TABLE_RELATION);
    req.add_relation_type_filter(master::MATVIEW_TABLE_RELATION);
    return master_ddl_proxy_->ListTables(req, &resp, rpc);
  }));

  if (resp.tables_size() == 0) {
    return STATUS_FORMAT(InvalidArgument, "No tables found in namespace: $0", ns.name);
  }

  vector<YBTableName> tables(resp.tables_size());
  for (int i = 0; i < resp.tables_size(); ++i) {
    const auto& table = resp.tables(i);
    tables[i].set_table_id(table.id());
    tables[i].set_namespace_id(table.namespace_().id());
    tables[i].set_pgschema_name(table.pgschema_name());

    RSTATUS_DCHECK(table.relation_type() == master::USER_TABLE_RELATION ||
            table.relation_type() == master::INDEX_TABLE_RELATION ||
            table.relation_type() == master::MATVIEW_TABLE_RELATION, InternalError,
            Format("Invalid relation type: $0", table.relation_type()));
    RSTATUS_DCHECK_EQ(table.namespace_().name(), ns.name, InternalError,
               Format("Invalid namespace name: $0", table.namespace_().name()));
    RSTATUS_DCHECK_EQ(table.namespace_().database_type(), ns.db_type, InternalError,
               Format("Invalid namespace type: $0",
                      YQLDatabase_Name(table.namespace_().database_type())));
  }

  return CreateSnapshot(tables, /* add_indexes */ false);
}

Result<ListSnapshotRestorationsResponsePB> ClusterAdminClient::ListSnapshotRestorations(
    const TxnSnapshotRestorationId& restoration_id) {
  master::ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::ListSnapshotRestorationsRequestPB req;
    if (restoration_id) {
      req.set_restoration_id(restoration_id.data(), restoration_id.size());
    }
    return master_backup_proxy_->ListSnapshotRestorations(req, &resp, rpc);
  }));
  return resp;
}

Result<rapidjson::Document> ClusterAdminClient::CreateSnapshotSchedule(
    const client::YBTableName& keyspace, MonoDelta interval, MonoDelta retention) {
  master::CreateSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::CreateSnapshotScheduleRequestPB req;

    auto& options = *req.mutable_options();
    auto& filter_tables = *options.mutable_filter()->mutable_tables()->mutable_tables();
    keyspace.SetIntoTableIdentifierPB(filter_tables.Add());

    options.set_interval_sec(interval.ToSeconds());
    options.set_retention_duration_sec(retention.ToSeconds());
    return master_backup_proxy_->CreateSnapshotSchedule(req, &resp, rpc);
  }));

  rapidjson::Document document;
  document.SetObject();

  AddStringField(
      "schedule_id",
      VERIFY_RESULT(FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id())).ToString(),
      &document, &document.GetAllocator());
  return document;
}

Result<rapidjson::Document> ClusterAdminClient::ListSnapshotSchedules(
    const SnapshotScheduleId& schedule_id) {
  master::ListSnapshotSchedulesResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [this, &resp, &schedule_id](RpcController* rpc) {
    master::ListSnapshotSchedulesRequestPB req;
    if (schedule_id) {
      req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
    }
    return master_backup_proxy_->ListSnapshotSchedules(req, &resp, rpc);
  }));

  rapidjson::Document result;
  result.SetObject();
  rapidjson::Value json_schedules(rapidjson::kArrayType);
  for (const auto& schedule : resp.schedules()) {
    json_schedules.PushBack(
        VERIFY_RESULT(SnapshotScheduleInfoToJson(schedule, &result.GetAllocator())),
        result.GetAllocator());
  }
  result.AddMember("schedules", json_schedules, result.GetAllocator());
  return result;
}

Result<rapidjson::Document> ClusterAdminClient::DeleteSnapshotSchedule(
    const SnapshotScheduleId& schedule_id) {
  master::DeleteSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [this, &resp, &schedule_id](RpcController* rpc) {
    master::DeleteSnapshotScheduleRequestPB req;
    req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());

    return master_backup_proxy_->DeleteSnapshotSchedule(req, &resp, rpc);
  }));

  rapidjson::Document document;
  document.SetObject();
  AddStringField("schedule_id", schedule_id.ToString(), &document, &document.GetAllocator());
  return document;
}

bool SnapshotSuitableForRestoreAt(const SysSnapshotEntryPB& entry, HybridTime restore_at) {
  return (entry.state() == master::SysSnapshotEntryPB::COMPLETE ||
          entry.state() == master::SysSnapshotEntryPB::CREATING) &&
         HybridTime::FromPB(entry.snapshot_hybrid_time()) >= restore_at &&
         HybridTime::FromPB(entry.previous_snapshot_hybrid_time()) < restore_at;
}

Result<TxnSnapshotId> ClusterAdminClient::SuitableSnapshotId(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at, CoarseTimePoint deadline) {
  for (;;) {
    auto last_snapshot_time = HybridTime::kMin;
    {
      RpcController rpc;
      rpc.set_deadline(deadline);
      master::ListSnapshotSchedulesRequestPB req;
      master::ListSnapshotSchedulesResponsePB resp;
      if (schedule_id) {
        req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
      }

      RETURN_NOT_OK_PREPEND(master_backup_proxy_->ListSnapshotSchedules(req, &resp, &rpc),
                            "Failed to list snapshot schedules");

      if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
      }

      if (resp.schedules().size() < 1) {
        return STATUS_FORMAT(InvalidArgument, "Unknown schedule: $0", schedule_id);
      }

      for (const auto& snapshot : resp.schedules()[0].snapshots()) {
        auto snapshot_hybrid_time = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
        last_snapshot_time = std::max(last_snapshot_time, snapshot_hybrid_time);
        if (SnapshotSuitableForRestoreAt(snapshot.entry(), restore_at)) {
          return VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
        }
      }
      if (last_snapshot_time > restore_at) {
        return STATUS_FORMAT(
            IllegalState, "Cannot restore at $0, last snapshot: $1, snapshots: $2",
            restore_at, last_snapshot_time, resp.schedules()[0].snapshots());
      }
    }
    RpcController rpc;
    rpc.set_deadline(deadline);
    master::CreateSnapshotRequestPB req;
    master::CreateSnapshotResponsePB resp;
    req.set_schedule_id(schedule_id.data(), schedule_id.size());
    RETURN_NOT_OK_PREPEND(master_backup_proxy_->CreateSnapshot(req, &resp, &rpc),
                          "Failed to create snapshot");
    if (resp.has_error()) {
      auto status = StatusFromPB(resp.error().status());
      if (master::MasterError(status) == master::MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION) {
        std::this_thread::sleep_until(std::min(deadline, CoarseMonoClock::now() + 1s));
        continue;
      }
      return status;
    }
    return FullyDecodeTxnSnapshotId(resp.snapshot_id());
  }
}

Status ClusterAdminClient::DisableTabletSplitsDuringRestore(CoarseTimePoint deadline) {
  // TODO(Sanket): Eventually all of this logic needs to be moved
  // to the master and exposed as APIs for the clients to consume.
  const std::string feature_name = "PITR";
  const auto splitting_disabled_until =
      CoarseMonoClock::Now() + MonoDelta::FromSeconds(kPitrSplitDisableDurationSecs);
  // Disable splitting and then wait for all pending splits to complete before
  // starting restoration.
  VERIFY_RESULT_PREPEND(
      DisableTabletSplitsInternal(kPitrSplitDisableDurationSecs * 1000, feature_name),
      "Failed to disable tablet split before restore.");

  while (CoarseMonoClock::Now() < std::min(splitting_disabled_until, deadline)) {
    // Wait for existing split operations to complete.
    const auto resp = VERIFY_RESULT_PREPEND(
        IsTabletSplittingCompleteInternal(true /* wait_for_parent_deletion */),
        "Tablet splitting did not complete. Cannot restore.");
    if (resp.is_tablet_splitting_complete()) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(kPitrSplitDisableCheckFreqMs));
  }

  if (CoarseMonoClock::now() >= deadline) {
    return STATUS(TimedOut, "Timed out waiting for tablet splitting to complete.");
  }

  // Return if we have used almost all of our time in waiting for splitting to complete,
  // since we can't guarantee that another split does not start.
  if (CoarseMonoClock::now() + MonoDelta::FromSeconds(3) >= splitting_disabled_until) {
    return STATUS(TimedOut, "Not enough time after disabling splitting to disable ",
                            "splitting again.");
  }

  // Disable for kPitrSplitDisableDurationSecs again so the restore has the full amount of time with
  // splitting disables. This overwrites the previous value since the feature_name is the same so
  // overall the time is still kPitrSplitDisableDurationSecs.
  VERIFY_RESULT_PREPEND(
      DisableTabletSplitsInternal(kPitrSplitDisableDurationSecs * 1000, feature_name),
      "Failed to disable tablet split before restore.");

  return Status::OK();
}

Result<rapidjson::Document> ClusterAdminClient::RestoreSnapshotScheduleDeprecated(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at) {
  auto deadline = CoarseMonoClock::now() + timeout_;

  // Disable splitting for the entire run of restore.
  RETURN_NOT_OK(DisableTabletSplitsDuringRestore(deadline));

  // Get the suitable snapshot to restore from.
  auto snapshot_id = VERIFY_RESULT(SuitableSnapshotId(schedule_id, restore_at, deadline));

  for (;;) {
    RpcController rpc;
    rpc.set_deadline(deadline);
    master::ListSnapshotsRequestPB req;
    req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
    master::ListSnapshotsResponsePB resp;
    RETURN_NOT_OK_PREPEND(master_backup_proxy_->ListSnapshots(req, &resp, &rpc),
                          "Failed to list snapshots");
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    if (resp.snapshots().size() != 1) {
      return STATUS_FORMAT(
          IllegalState, "Wrong number of snapshots received $0", resp.snapshots().size());
    }
    if (resp.snapshots()[0].entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
      if (SnapshotSuitableForRestoreAt(resp.snapshots()[0].entry(), restore_at)) {
        break;
      }
      return STATUS_FORMAT(
          IllegalState, "Snapshot is not suitable for restore at $0", restore_at);
    }
    auto now = CoarseMonoClock::now();
    if (now >= deadline) {
      return STATUS_FORMAT(
          TimedOut, "Timed out to complete a snapshot $0", snapshot_id);
    }
    std::this_thread::sleep_until(std::min(deadline, now + 100ms));
  }

  RpcController rpc;
  rpc.set_deadline(deadline);
  RestoreSnapshotRequestPB req;
  RestoreSnapshotResponsePB resp;
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.set_restore_ht(restore_at.ToUint64());
  RETURN_NOT_OK_PREPEND(master_backup_proxy_->RestoreSnapshot(req, &resp, &rpc),
                        "Failed to restore snapshot");

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  auto restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()));

  rapidjson::Document document;
  document.SetObject();

  AddStringField("snapshot_id", snapshot_id.ToString(), &document, &document.GetAllocator());
  AddStringField("restoration_id", restoration_id.ToString(), &document, &document.GetAllocator());

  return document;
}

Result<rapidjson::Document> ClusterAdminClient::RestoreSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at) {
  auto deadline = CoarseMonoClock::now() + timeout_;

  RpcController rpc;
  rpc.set_deadline(deadline);
  master::RestoreSnapshotScheduleRequestPB req;
  master::RestoreSnapshotScheduleResponsePB resp;
  req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
  req.set_restore_ht(restore_at.ToUint64());

  Status s = master_backup_proxy_->RestoreSnapshotSchedule(req, &resp, &rpc);
  if (!s.ok()) {
    if (s.IsRemoteError() &&
        rpc.error_response()->code() == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD) {
      cout << "WARNING: fallback to RestoreSnapshotScheduleDeprecated." << endl;
      return RestoreSnapshotScheduleDeprecated(schedule_id, restore_at);
    }
    RETURN_NOT_OK_PREPEND(s, Format("Failed to restore snapshot from schedule: $0",
        schedule_id.ToString()));
  }

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(resp.snapshot_id()));
  auto restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()));

  rapidjson::Document document;
  document.SetObject();

  AddStringField("snapshot_id", snapshot_id.ToString(), &document, &document.GetAllocator());
  AddStringField("restoration_id", restoration_id.ToString(), &document, &document.GetAllocator());

  return document;
}

Result<rapidjson::Document> ClusterAdminClient::EditSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, std::optional<MonoDelta> new_interval,
    std::optional<MonoDelta> new_retention) {

  master::EditSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::EditSnapshotScheduleRequestPB req;
    req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
    if (new_interval) {
      req.set_interval_sec(new_interval->ToSeconds());
    }
    if (new_retention) {
      req.set_retention_duration_sec(new_retention->ToSeconds());
    }
    return master_backup_proxy_->EditSnapshotSchedule(req, &resp, rpc);
  }));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value json_schedule =
      VERIFY_RESULT(SnapshotScheduleInfoToJson(resp.schedule(), &document.GetAllocator()));
  document.AddMember("schedule", json_schedule, document.GetAllocator());
  return document;
}

Status ClusterAdminClient::RestoreSnapshot(const string& snapshot_id,
                                           HybridTime timestamp) {
  RestoreSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    RestoreSnapshotRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));
    if (timestamp) {
      req.set_restore_ht(timestamp.ToUint64());
    }
    return master_backup_proxy_->RestoreSnapshot(req, &resp, rpc);
  }));

  cout << "Started restoring snapshot: " << snapshot_id << endl
       << "Restoration id: " << FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()) << endl;
  if (timestamp) {
    cout << "Restore at: " << timestamp << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::DeleteSnapshot(const std::string& snapshot_id) {
  DeleteSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    DeleteSnapshotRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));
    return master_backup_proxy_->DeleteSnapshot(req, &resp, rpc);
  }));

  cout << "Deleted snapshot: " << snapshot_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateSnapshotMetaFile(const string& snapshot_id,
                                                  const string& file_name) {
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListSnapshotsRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));

    // Format 0 - latest format (== Format 2 at the moment).
    // Format -1 - old format (no 'namespace_name' in the Table entry).
    // Format 1 - old format.
    // Format 2 - new format.
    if (FLAGS_TEST_metadata_file_format_version == 0 ||
        FLAGS_TEST_metadata_file_format_version >= 2) {
      req.set_prepare_for_backup(true);
    }
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));

  if (resp.snapshots_size() > 1) {
    LOG(WARNING) << "Requested snapshot metadata for snapshot '" << snapshot_id << "', but got "
                 << resp.snapshots_size() << " snapshots in the response";
  }

  SnapshotInfoPB* snapshot = nullptr;
  for (SnapshotInfoPB& snapshot_entry : *resp.mutable_snapshots()) {
    if (SnapshotIdToString(snapshot_entry.id()) == snapshot_id) {
      snapshot = &snapshot_entry;
      break;
    }
  }
  if (!snapshot) {
    return STATUS_FORMAT(
        InternalError, "Response contained $0 entries but no entry for snapshot '$1'",
        resp.snapshots_size(), snapshot_id);
  }

  if (FLAGS_TEST_metadata_file_format_version == -1) {
    // Remove 'namespace_name' from SysTablesEntryPB.
    SysSnapshotEntryPB& sys_entry = *snapshot->mutable_entry();
    for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      if (entry.type() == SysRowEntryType::TABLE) {
        auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
        meta.clear_namespace_name();
        entry.set_data(meta.SerializeAsString());
      }
    }
  }

  cout << "Exporting snapshot " << snapshot_id << " ("
       << snapshot->entry().state() << ") to file " << file_name << endl;

  // Serialize snapshot protobuf to given path.
  RETURN_NOT_OK(pb_util::WritePBContainerToPath(
      Env::Default(), file_name, *snapshot, pb_util::OVERWRITE, pb_util::SYNC));

  cout << "Snapshot metadata was saved into file: " << file_name << endl;
  return Status::OK();
}

string ClusterAdminClient::GetDBTypeName(const SysNamespaceEntryPB& pb) {
  const YQLDatabase db_type = GetDatabaseType(pb);
  switch (db_type) {
    case YQL_DATABASE_UNKNOWN: return "UNKNOWN DB TYPE";
    case YQL_DATABASE_CQL: return "YCQL keyspace";
    case YQL_DATABASE_PGSQL: return "YSQL database";
    case YQL_DATABASE_REDIS: return "YEDIS namespace";
  }
  FATAL_INVALID_ENUM_VALUE(YQLDatabase, db_type);
}

Status ClusterAdminClient::UpdateUDTypes(
    QLTypePB* pb_type, bool* update_meta, const NSNameToNameMap& ns_name_to_name) {
  return IterateAndDoForUDT(
      pb_type,
      [update_meta, &ns_name_to_name](QLTypePB::UDTypeInfo* udtype_info) -> Status {
        auto ns_it = ns_name_to_name.find(udtype_info->keyspace_name());
        if (ns_it == ns_name_to_name.end()) {
          return STATUS_FORMAT(
              InvalidArgument, "No metadata for keyspace '$0' referenced in UDType '$1'",
              udtype_info->keyspace_name(), udtype_info->name());
        }

        if (udtype_info->keyspace_name() != ns_it->second) {
          udtype_info->set_keyspace_name(ns_it->second);
          *DCHECK_NOTNULL(update_meta) = true;
        }
        return Status::OK();
      });
}

Status ClusterAdminClient::ImportSnapshotMetaFile(const string& file_name,
                                                  const TypedNamespaceName& keyspace,
                                                  const vector<YBTableName>& tables) {
  cout << "Read snapshot meta file " << file_name << endl;

  ImportSnapshotMetaRequestPB req;

  SnapshotInfoPB* const snapshot_info = req.mutable_snapshot();

  // Read snapshot protobuf from given path.
  RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(Env::Default(), file_name, snapshot_info));

  if (!snapshot_info->has_format_version()) {
    SCHECK_EQ(
        0, snapshot_info->backup_entries_size(), InvalidArgument,
        Format("Metadata file in Format 1 has backup entries from Format 2: $0",
        snapshot_info->backup_entries_size()));

    // Repack PB data loaded in the old format.
    // Init BackupSnapshotPB based on loaded SnapshotInfoPB.
    SysSnapshotEntryPB& sys_entry = *snapshot_info->mutable_entry();
    snapshot_info->mutable_backup_entries()->Reserve(sys_entry.entries_size());
    for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      snapshot_info->add_backup_entries()->mutable_entry()->Swap(&entry);
    }

    sys_entry.clear_entries();
    snapshot_info->set_format_version(2);
  }

  cout << "Importing snapshot " << SnapshotIdToString(snapshot_info->id())
       << " (" << snapshot_info->entry().state() << ")" << endl;

  // Map: Old namespace ID -> [Old name, New name] pair.
  typedef pair<NamespaceName, NamespaceName> NSNamePair;
  typedef unordered_map<NamespaceId, NSNamePair> NSIdToNameMap;
  NSIdToNameMap ns_id_to_name;
  NSNameToNameMap ns_name_to_name;

  size_t table_index = 0;
  bool was_table_renamed = false;
  for (BackupRowEntryPB& backup_entry : *snapshot_info->mutable_backup_entries()) {
    SysRowEntry& entry = *backup_entry.mutable_entry();
    const YBTableName table_name = table_index < tables.size()
        ? tables[table_index] : YBTableName();

    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: {
        auto meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
        const string db_type = GetDBTypeName(meta);
        cout << "Target imported " << db_type << " name: "
             << (keyspace.name.empty() ? meta.name() : keyspace.name) << endl
             << db_type << " being imported: " << meta.name() << endl;

        if (!keyspace.name.empty() && keyspace.name != meta.name()) {
          ns_id_to_name[entry.id()] = NSNamePair(meta.name(), keyspace.name);
          ns_name_to_name[meta.name()] = keyspace.name;

          meta.set_name(keyspace.name);
          entry.set_data(meta.SerializeAsString());
        } else {
          ns_id_to_name[entry.id()] = NSNamePair(meta.name(), meta.name());
          ns_name_to_name[meta.name()] = meta.name();
        }
        break;
      }
      case SysRowEntryType::UDTYPE: {
        // Note: UDT renaming is not supported.
        auto meta = VERIFY_RESULT(ParseFromSlice<SysUDTypeEntryPB>(entry.data()));
        const auto ns_it = ns_id_to_name.find(meta.namespace_id());
        cout << "Target imported user-defined type name: "
             << (ns_it == ns_id_to_name.end() ? "[" + meta.namespace_id() + "]" :
                 ns_it->second.second) << "." << meta.name() << endl
             << "User-defined type being imported: "
             << (ns_it == ns_id_to_name.end() ? "[" + meta.namespace_id() + "]" :
                 ns_it->second.first) << "." << meta.name() << endl;

        bool update_meta = false;
        // Update QLTypePB::UDTypeInfo::keyspace_name in the UDT params.
        for (int i = 0; i < meta.field_names_size(); ++i) {
          RETURN_NOT_OK(UpdateUDTypes(meta.mutable_field_types(i), &update_meta, ns_name_to_name));
        }

        if (update_meta) {
          entry.set_data(meta.SerializeAsString());
        }
        break;
      }
      case SysRowEntryType::TABLE: {
        if (was_table_renamed && table_name.empty()) {
          // Renaming is allowed for all tables OR for no one table.
          return STATUS_FORMAT(InvalidArgument,
                               "There is no name for table (including indexes) number: $0",
                               table_index);
        }

        auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
        const auto colocated_prefix = meta.colocated() ? "colocated " : "";

        if (meta.indexed_table_id().empty()) {
          cout << "Table type: " << colocated_prefix << "table" << endl;
        } else {
          cout << "Table type: " << colocated_prefix << "index (attaching to the old table id "
               << meta.indexed_table_id() << ")" << endl;
        }

        if (!table_name.empty()) {
          DCHECK(table_name.has_namespace());
          DCHECK(table_name.has_table());
          cout << "Target imported " << colocated_prefix << "table name: "
               << table_name.ToString() << endl;
        } else if (!keyspace.name.empty()) {
          cout << "Target imported " << colocated_prefix << "table name: "
               << keyspace.name << "." << meta.name() << endl;
        }

        // Print old table name before the table renaming in the code below.
        cout << (meta.colocated() ? "Colocated t" : "T") << "able being imported: "
             << (meta.namespace_name().empty() ?
                 "[" + meta.namespace_id() + "]" : meta.namespace_name())
             << "." << meta.name() << endl;

        bool update_meta = false;
        if (!table_name.empty() && table_name.table_name() != meta.name()) {
          meta.set_name(table_name.table_name());
          update_meta = true;
          was_table_renamed = true;
        }
        if (!keyspace.name.empty() && keyspace.name != meta.namespace_name()) {
          meta.set_namespace_name(keyspace.name);
          update_meta = true;
        }

        if (meta.name().empty()) {
          return STATUS(IllegalState, "Could not find table name from snapshot metadata");
        }

        // Update QLTypePB::UDTypeInfo::keyspace_name in all UDT params in the table Schema.
        SchemaPB* const schema = meta.mutable_schema();
        // Recursively update ids in used user-defined types.
        for (int i = 0; i < schema->columns_size(); ++i) {
          QLTypePB* const pb_type = schema->mutable_columns(i)->mutable_type();
          RETURN_NOT_OK(UpdateUDTypes(pb_type, &update_meta, ns_name_to_name));
        }

        // Update the table name if needed.
        if (update_meta) {
          entry.set_data(meta.SerializeAsString());
        }

        ++table_index;
        break;
      }
      default:
        break;
    }
  }

  // RPC timeout is a function of the number of tables that needs to be imported.
  ImportSnapshotMetaResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    return master_backup_proxy_->ImportSnapshotMeta(req, &resp, rpc);
  }, timeout_ * std::max(static_cast<size_t>(1), table_index)));

  const int kObjectColumnWidth = 16;
  const auto pad_object_type = [](const string& s) {
    return RightPadToWidth(s, kObjectColumnWidth);
  };

  cout << "Successfully applied snapshot." << endl
       << pad_object_type("Object") << kColumnSep
       << RightPadToUuidWidth("Old ID") << kColumnSep
       << RightPadToUuidWidth("New ID") << endl;

  const RepeatedPtrField<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta =
      resp.tables_meta();
  CreateSnapshotRequestPB snapshot_req;
  CreateSnapshotResponsePB snapshot_resp;

  for (int i = 0; i < tables_meta.size(); ++i) {
    const ImportSnapshotMetaResponsePB_TableMetaPB& table_meta = tables_meta.Get(i);
    const string& new_table_id = table_meta.table_ids().new_id();

    cout << pad_object_type("Keyspace") << kColumnSep
         << table_meta.namespace_ids().old_id() << kColumnSep
         << table_meta.namespace_ids().new_id() << endl;

    if (!ImportSnapshotMetaResponsePB_TableType_IsValid(table_meta.table_type())) {
      return STATUS_FORMAT(InternalError, "Found unknown table type: ", table_meta.table_type());
    }

    const string table_type =
        AllCapsToCamelCase(ImportSnapshotMetaResponsePB_TableType_Name(table_meta.table_type()));
    cout << pad_object_type(table_type) << kColumnSep
         << table_meta.table_ids().old_id() << kColumnSep
         << new_table_id << endl;

    const RepeatedPtrField<IdPairPB>& udts_map = table_meta.ud_types_ids();
    for (int j = 0; j < udts_map.size(); ++j) {
      const IdPairPB& pair = udts_map.Get(j);
      cout << pad_object_type("UDType") << kColumnSep
           << pair.old_id() << kColumnSep
           << pair.new_id() << endl;
    }

    const RepeatedPtrField<IdPairPB>& tablets_map = table_meta.tablets_ids();
    for (int j = 0; j < tablets_map.size(); ++j) {
      const IdPairPB& pair = tablets_map.Get(j);
      cout << pad_object_type(Format("Tablet $0", j)) << kColumnSep
           << pair.old_id() << kColumnSep
           << pair.new_id() << endl;
    }

    RETURN_NOT_OK(yb_client_->WaitForCreateTableToFinish(
        new_table_id,
        CoarseMonoClock::Now() + MonoDelta::FromSeconds(FLAGS_yb_client_admin_operation_timeout_sec)
    ));

    snapshot_req.mutable_tables()->Add()->set_table_id(new_table_id);
  }

  // All indexes already are in the request. Do not add them twice.
  snapshot_req.set_add_indexes(false);
  snapshot_req.set_transaction_aware(true);
  snapshot_req.set_imported(true);
  // Create new snapshot.
  RETURN_NOT_OK(RequestMasterLeader(&snapshot_resp, [&](RpcController* rpc) {
    return master_backup_proxy_->CreateSnapshot(snapshot_req, &snapshot_resp, rpc);
  }));

  cout << pad_object_type("Snapshot") << kColumnSep
       << SnapshotIdToString(snapshot_info->id()) << kColumnSep
       << SnapshotIdToString(snapshot_resp.snapshot_id()) << endl;

  return Status::OK();
}

Status ClusterAdminClient::ListReplicaTypeCounts(const YBTableName& table_name) {
  vector<string> tablet_ids, ranges;
  RETURN_NOT_OK(yb_client_->GetTablets(table_name, 0, &tablet_ids, &ranges));
  master::GetTabletLocationsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::GetTabletLocationsRequestPB req;
    for (const auto& tablet_id : tablet_ids) {
      req.add_tablet_ids(tablet_id);
    }
    return master_client_proxy_->GetTabletLocations(req, &resp, rpc);
  }));

  struct ReplicaCounts {
    int live_count;
    int read_only_count;
    string placement_uuid;
  };
  std::map<TabletServerId, ReplicaCounts> replica_map;

  std::cout << "Tserver ID\t\tPlacement ID\t\tLive count\t\tRead only count\n";

  for (int tablet_idx = 0; tablet_idx < resp.tablet_locations_size(); tablet_idx++) {
    const master::TabletLocationsPB& locs = resp.tablet_locations(tablet_idx);
    for (int replica_idx = 0; replica_idx < locs.replicas_size(); replica_idx++) {
      const auto& replica = locs.replicas(replica_idx);
      const string& ts_uuid = replica.ts_info().permanent_uuid();
      const string& placement_uuid =
          replica.ts_info().has_placement_uuid() ? replica.ts_info().placement_uuid() : "";
      bool is_replica_read_only =
          replica.member_type() == consensus::PeerMemberType::PRE_OBSERVER ||
          replica.member_type() == consensus::PeerMemberType::OBSERVER;
      int live_count = is_replica_read_only ? 0 : 1;
      int read_only_count = 1 - live_count;
      if (replica_map.count(ts_uuid) == 0) {
        replica_map[ts_uuid].live_count = live_count;
        replica_map[ts_uuid].read_only_count = read_only_count;
        replica_map[ts_uuid].placement_uuid = placement_uuid;
      } else {
        ReplicaCounts* counts = &replica_map[ts_uuid];
        counts->live_count += live_count;
        counts->read_only_count += read_only_count;
      }
    }
  }

  for (auto const& tserver : replica_map) {
    std::cout << tserver.first << "\t\t" << tserver.second.placement_uuid << "\t\t"
              << tserver.second.live_count << "\t\t" << tserver.second.read_only_count << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::SetPreferredZones(const std::vector<string>& preferred_zones) {
  rpc::RpcController rpc;
  master::SetPreferredZonesRequestPB req;
  master::SetPreferredZonesResponsePB resp;
  rpc.set_timeout(timeout_);

  std::set<string> zones;
  std::set<int> visited_priorities;

  for (const string& zone : preferred_zones) {
    if (zones.find(zone) != zones.end()) {
      return STATUS_SUBSTITUTE(
          InvalidArgument, "Invalid argument for preferred zone $0, values should not repeat",
          zone);
    }

    std::vector<std::string> zone_priority_split = strings::Split(zone, ":", strings::AllowEmpty());
    if (zone_priority_split.size() == 0 || zone_priority_split.size() > 2) {
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Invalid argument for preferred zone $0, should have format cloud.region.zone[:priority]",
          zone);
    }

    std::vector<string> tokens = strings::Split(zone_priority_split[0], ".", strings::AllowEmpty());
    if (tokens.size() != 3) {
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Invalid argument for preferred zone $0, should have format cloud.region.zone:[priority]",
          zone);
    }

    uint priority = 1;

    if (zone_priority_split.size() == 2) {
      auto result = CheckedStoi(zone_priority_split[1]);
      if (!result.ok() || result.get() < 1) {
        return STATUS_SUBSTITUTE(
            InvalidArgument,
            "Invalid argument for preferred zone $0, priority should be non-zero positive integer",
            zone);
      }
      priority = static_cast<uint>(result.get());
    }

    // Max priority if each zone has a unique priority value can only be the size of the input array
    if (priority > preferred_zones.size()) {
      return STATUS(
          InvalidArgument,
          "Priority value cannot be more than the number of zones in the preferred list since each "
          "priority should be associated with at least one zone from the list");
    }

    CloudInfoPB* cloud_info = nullptr;
    visited_priorities.insert(priority);

    while (req.multi_preferred_zones_size() < static_cast<int>(priority)) {
      req.add_multi_preferred_zones();
    }

    auto current_list = req.mutable_multi_preferred_zones(priority - 1);
    cloud_info = current_list->add_zones();

    cloud_info->set_placement_cloud(tokens[0]);
    cloud_info->set_placement_region(tokens[1]);
    cloud_info->set_placement_zone(tokens[2]);

    zones.emplace(zone);

    if (priority == 1) {
      // Handle old clusters which can only handle a single priority. New clusters will ignore this
      // member as multi_preferred_zones is already set.
      cloud_info = req.add_preferred_zones();
      cloud_info->set_placement_cloud(tokens[0]);
      cloud_info->set_placement_region(tokens[1]);
      cloud_info->set_placement_zone(tokens[2]);
    }
  }

  int size = static_cast<int>(visited_priorities.size());
  if (size > 0 && (*(visited_priorities.rbegin()) != size)) {
    return STATUS_SUBSTITUTE(
        InvalidArgument, "Invalid argument, each priority should have at least one zone");
  }

  RETURN_NOT_OK(master_cluster_proxy_->SetPreferredZones(req, &resp, &rpc));

  if (resp.has_error()) {
    return STATUS(ServiceUnavailable, resp.error().status().message());
  }

  return Status::OK();
}

Status ClusterAdminClient::RotateUniverseKey(const std::string& key_path) {
  return SendEncryptionRequest(key_path, true);
}

Status ClusterAdminClient::DisableEncryption() {
  return SendEncryptionRequest("", false);
}

Status ClusterAdminClient::SendEncryptionRequest(
    const std::string& key_path, bool enable_encryption) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  // Get the cluster config from the master leader.
  master::ChangeEncryptionInfoRequestPB encryption_info_req;
  master::ChangeEncryptionInfoResponsePB encryption_info_resp;
  encryption_info_req.set_encryption_enabled(enable_encryption);
  if (key_path != "") {
    encryption_info_req.set_key_path(key_path);
  }
  RETURN_NOT_OK_PREPEND(master_encryption_proxy_->
      ChangeEncryptionInfo(encryption_info_req, &encryption_info_resp, &rpc),
                        "MasterServiceImpl::ChangeEncryptionInfo call fails.")

  if (encryption_info_resp.has_error()) {
    return StatusFromPB(encryption_info_resp.error().status());
  }
  return Status::OK();
}

Status ClusterAdminClient::IsEncryptionEnabled() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::IsEncryptionEnabledRequestPB req;
  master::IsEncryptionEnabledResponsePB resp;
  RETURN_NOT_OK_PREPEND(master_encryption_proxy_->
      IsEncryptionEnabled(req, &resp, &rpc),
      "MasterServiceImpl::IsEncryptionEnabled call fails.");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "Encryption status: " << (resp.encryption_enabled() ?
      Format("ENABLED with key id $0", resp.key_id()) : "DISABLED" ) << std::endl;
  return Status::OK();
}

Status ClusterAdminClient::AddUniverseKeyToAllMasters(
    const std::string& key_id, const std::string& universe_key) {

  RETURN_NOT_OK(encryption::EncryptionParams::IsValidKeySize(
      universe_key.size() - encryption::EncryptionParams::kBlockSize));

  master::AddUniverseKeysRequestPB req;
  master::AddUniverseKeysResponsePB resp;
  auto* universe_keys = req.mutable_universe_keys();
  (*universe_keys->mutable_map())[key_id] = universe_key;

  for (auto hp : VERIFY_RESULT(HostPort::ParseStrings(master_addr_list_, 7100))) {
    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    master::MasterEncryptionProxy proxy(proxy_cache_.get(), hp);
    RETURN_NOT_OK_PREPEND(proxy.AddUniverseKeys(req, &resp, &rpc),
                          Format("MasterServiceImpl::AddUniverseKeys call fails on host $0.",
                                 hp.ToString()));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    std::cout << Format("Successfully added key to the node $0.\n", hp.ToString());
  }

  return Status::OK();
}

Status ClusterAdminClient::AllMastersHaveUniverseKeyInMemory(const std::string& key_id) {
  master::HasUniverseKeyInMemoryRequestPB req;
  master::HasUniverseKeyInMemoryResponsePB resp;
  req.set_version_id(key_id);

  for (auto hp : VERIFY_RESULT(HostPort::ParseStrings(master_addr_list_, 7100))) {
    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    master::MasterEncryptionProxy proxy(proxy_cache_.get(), hp);
    RETURN_NOT_OK_PREPEND(proxy.HasUniverseKeyInMemory(req, &resp, &rpc),
                          "MasterServiceImpl::ChangeEncryptionInfo call fails.");

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    if (!resp.has_key()) {
      return STATUS_FORMAT(TryAgain, "Node $0 does not have universe key in memory", hp);
    }

    std::cout << Format("Node $0 has universe key in memory: $1\n", hp.ToString(), resp.has_key());
  }

  return Status::OK();
}

Status ClusterAdminClient::RotateUniverseKeyInMemory(const std::string& key_id) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::ChangeEncryptionInfoRequestPB req;
  master::ChangeEncryptionInfoResponsePB resp;
  req.set_encryption_enabled(true);
  req.set_in_memory(true);
  req.set_version_id(key_id);
  RETURN_NOT_OK_PREPEND(master_encryption_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
                        "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "Rotated universe key in memory\n";
  return Status::OK();
}

Status ClusterAdminClient::DisableEncryptionInMemory() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::ChangeEncryptionInfoRequestPB req;
  master::ChangeEncryptionInfoResponsePB resp;
  req.set_encryption_enabled(false);
  RETURN_NOT_OK_PREPEND(master_encryption_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
                        "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "Encryption disabled\n";
  return Status::OK();
}

Status ClusterAdminClient::WriteUniverseKeyToFile(
    const std::string& key_id, const std::string& file_name) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::GetUniverseKeyRegistryRequestPB req;
  master::GetUniverseKeyRegistryResponsePB resp;
  RETURN_NOT_OK_PREPEND(master_encryption_proxy_->GetUniverseKeyRegistry(req, &resp, &rpc),
                        "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  auto universe_keys = resp.universe_keys();
  const auto& it = universe_keys.map().find(key_id);
  if (it == universe_keys.map().end()) {
    return STATUS_FORMAT(NotFound, "Could not find key with id $0", key_id);
  }

  RETURN_NOT_OK(WriteStringToFile(Env::Default(), Slice(it->second), file_name));

  std::cout << "Finished writing to file\n";
  return Status::OK();
}

Status ClusterAdminClient::CreateCDCSDKDBStream(
  const TypedNamespaceName& ns, const std::string& checkpoint_type) {
  HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS());
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(proxy_cache_.get(), ts_addr);

  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;

  req.set_namespace_name(ns.name);
  req.set_record_type(cdc::CDCRecordType::CHANGE);
  req.set_record_format(cdc::CDCRecordFormat::PROTO);
  req.set_source_type(cdc::CDCRequestSource::CDCSDK);
  if (checkpoint_type == yb::ToString("EXPLICIT")) {
    req.set_checkpoint_type(cdc::CDCCheckpointType::EXPLICIT);
  } else {
    req.set_checkpoint_type(cdc::CDCCheckpointType::IMPLICIT);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error creating stream: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Stream ID: " << resp.db_stream_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateCDCStream(const TableId& table_id) {
  master::CreateCDCStreamRequestPB req;
  master::CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);
  req.mutable_options()->Reserve(3);

  auto record_type_option = req.add_options();
  record_type_option->set_key(cdc::kRecordType);
  record_type_option->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  auto record_format_option = req.add_options();
  record_format_option->set_key(cdc::kRecordFormat);
  record_format_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::JSON));

  auto source_type_option = req.add_options();
  source_type_option->set_key(cdc::kSourceType);
  source_type_option->set_value(CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->CreateCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error creating stream: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Stream ID: " << resp.stream_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteCDCSDKDBStream(const std::string& db_stream_id) {
  master::DeleteCDCStreamRequestPB req;
  master::DeleteCDCStreamResponsePB resp;
  req.add_stream_id(db_stream_id);

  RpcController rpc;
  rpc.set_timeout(timeout_);
      RETURN_NOT_OK(master_replication_proxy_->DeleteCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error deleting stream: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "Successfully deleted Change Data Stream ID: " << db_stream_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteCDCStream(const std::string& stream_id, bool force_delete) {
  master::DeleteCDCStreamRequestPB req;
  master::DeleteCDCStreamResponsePB resp;
  req.add_stream_id(stream_id);
  req.set_force_delete(force_delete);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->DeleteCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error deleting stream: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "Successfully deleted CDC Stream ID: " << stream_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListCDCStreams(const TableId& table_id) {
  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;
  req.set_id_type(yb::master::IdTypePB::TABLE_ID);
  if (!table_id.empty()) {
    req.set_table_id(table_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting CDC stream list: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Streams: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::ListCDCSDKStreams(const std::string& namespace_name) {
  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;
  req.set_id_type(yb::master::IdTypePB::NAMESPACE_ID);

  if (!namespace_name.empty()) {
    cout << "Filtering out DB streams for the namespace: " << namespace_name << "\n\n";
    master::GetNamespaceInfoResponsePB namespace_info_resp;
    RETURN_NOT_OK(yb_client_->GetNamespaceInfo("", namespace_name, YQL_DATABASE_PGSQL,
                                               &namespace_info_resp));
    req.set_namespace_id(namespace_info_resp.namespace_().id());
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting CDC stream list: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Streams: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::GetCDCDBStreamInfo(const std::string& db_stream_id) {
  master::GetCDCDBStreamInfoRequestPB req;
  master::GetCDCDBStreamInfoResponsePB resp;
  req.set_db_stream_id(db_stream_id);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetCDCDBStreamInfo(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting info corresponding to CDC db stream : " <<
    resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC DB Stream Info: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::WaitForSetupUniverseReplicationToFinish(const string& producer_uuid) {
  master::IsSetupUniverseReplicationDoneRequestPB req;
  req.set_producer_id(producer_uuid);
  for (;;) {
    master::IsSetupUniverseReplicationDoneResponsePB resp;
    RpcController rpc;
    rpc.set_timeout(timeout_);
    Status s = master_replication_proxy_->IsSetupUniverseReplicationDone(req, &resp, &rpc);

    if (!s.ok() || resp.has_error()) {
        LOG(WARNING) << "Encountered error while waiting for setup_universe_replication to complete"
                     << " : " << (!s.ok() ? s.ToString() : resp.error().status().message());
    }
    if (resp.has_done() && resp.done()) {
      return StatusFromPB(resp.replication_error());
    }

    // Still processing, wait and then loop again.
    std::this_thread::sleep_for(100ms);
  }
}

Status ClusterAdminClient::SetupUniverseReplication(
    const string& producer_uuid, const vector<string>& producer_addresses,
    const vector<TableId>& tables,
    const vector<string>& producer_bootstrap_ids) {
  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_uuid);

  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
  for (const auto& addr : producer_addresses) {
    // HostPort::FromString() expects a default port.
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
    HostPortToPB(hp, req.add_producer_master_addresses());
  }

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(tables.size()));
  for (const auto& table : tables) {
    req.add_producer_table_ids(table);
  }

  for (const auto& producer_bootstrap_id : producer_bootstrap_ids) {
    req.add_producer_bootstrap_ids(producer_bootstrap_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  auto setup_result_status = master_replication_proxy_->SetupUniverseReplication(req, &resp, &rpc);

  setup_result_status = WaitForSetupUniverseReplicationToFinish(producer_uuid);

  if (resp.has_error()) {
    cout << "Error setting up universe replication: " << resp.error().status().message() << endl;
    Status status_from_error = StatusFromPB(resp.error().status());

    return status_from_error;
  }

    // Clean up config files if setup fails to complete.
  if (!setup_result_status.ok()) {
    cout << "Error waiting for universe replication setup to complete: "
         << setup_result_status.message().ToBuffer() << endl;
    return setup_result_status;
  }

  cout << "Replication setup successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteUniverseReplication(const std::string& producer_id,
                                                     bool ignore_errors) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_id);
  req.set_ignore_errors(ignore_errors);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->DeleteUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error deleting universe replication: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  if (resp.warnings().size() > 0) {
    cout << "Encountered the following warnings while running delete_universe_replication:" << endl;
    for (const auto& warning : resp.warnings()) {
      cout << " - " << warning.message() << endl;
    }
  }

  cout << "Replication deleted successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::AlterUniverseReplication(const std::string& producer_uuid,
    const std::vector<std::string>& producer_addresses,
    const std::vector<TableId>& add_tables,
    const std::vector<TableId>& remove_tables,
    const std::vector<std::string>& producer_bootstrap_ids_to_add,
    const std::string& new_producer_universe_id,
    bool remove_table_ignore_errors) {
  master::AlterUniverseReplicationRequestPB req;
  master::AlterUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_uuid);
  req.set_remove_table_ignore_errors(remove_table_ignore_errors);

  if (!producer_addresses.empty()) {
    req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
    for (const auto& addr : producer_addresses) {
      // HostPort::FromString() expects a default port.
      auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
      HostPortToPB(hp, req.add_producer_master_addresses());
    }
  }

  if (!add_tables.empty()) {
    req.mutable_producer_table_ids_to_add()->Reserve(narrow_cast<int>(add_tables.size()));
    for (const auto& table : add_tables) {
      req.add_producer_table_ids_to_add(table);
    }

    if (!producer_bootstrap_ids_to_add.empty()) {
      // There msut be a bootstrap id for every table id.
      if (producer_bootstrap_ids_to_add.size() != add_tables.size()) {
        cout << "The number of bootstrap ids must equal the number of table ids. "
             << "Use separate alter commands if only some tables are being bootstrapped." << endl;
        return STATUS(InternalError, "Invalid number of bootstrap ids");
      }

      req.mutable_producer_bootstrap_ids_to_add()->Reserve(
          narrow_cast<int>(producer_bootstrap_ids_to_add.size()));
      for (const auto& bootstrap_id : producer_bootstrap_ids_to_add) {
        req.add_producer_bootstrap_ids_to_add(bootstrap_id);
      }
    }
  }

  if (!remove_tables.empty()) {
    req.mutable_producer_table_ids_to_remove()->Reserve(narrow_cast<int>(remove_tables.size()));
    for (const auto& table : remove_tables) {
      req.add_producer_table_ids_to_remove(table);
    }
  }

  if (!new_producer_universe_id.empty()) {
    req.set_new_producer_universe_id(new_producer_universe_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->AlterUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error altering universe replication: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  if (!add_tables.empty()) {
    // If we are adding tables, then wait for the altered producer to be deleted (this happens once
    // it is merged with the original).
    RETURN_NOT_OK(WaitForSetupUniverseReplicationToFinish(producer_uuid + ".ALTER"));
  }

  cout << "Replication altered successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::SetUniverseReplicationEnabled(const std::string& producer_id,
                                                                 bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;
  req.set_producer_id(producer_id);
  req.set_is_enabled(is_enabled);
  const string toggle = (is_enabled ? "enabl" : "disabl");

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->SetUniverseReplicationEnabled(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error " << toggle << "ing "
         << "universe replication: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "Replication " << toggle << "ed successfully" << endl;
  return Status::OK();
}

Result<HostPort> ClusterAdminClient::GetFirstRpcAddressForTS() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (server.has_registration() &&
        !server.registration().common().private_rpc_addresses().empty()) {
      return HostPortFromPB(server.registration().common().private_rpc_addresses(0));
    }
  }

  return STATUS(NotFound, "Didn't find a server registered with the Master");
}

Status ClusterAdminClient::BootstrapProducer(const vector<TableId>& table_ids) {

  HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS());
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(proxy_cache_.get(), ts_addr);

  cdc::BootstrapProducerRequestPB bootstrap_req;
  cdc::BootstrapProducerResponsePB bootstrap_resp;
  for (const auto& table_id : table_ids) {
    bootstrap_req.add_table_ids(table_id);
  }
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(std::max(timeout_.ToSeconds(), 120.0)));
  RETURN_NOT_OK(cdc_proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &rpc));

  if (bootstrap_resp.has_error()) {
    cout << "Error bootstrapping consumer: " << bootstrap_resp.error().status().message() << endl;
    return StatusFromPB(bootstrap_resp.error().status());
  }

  if (implicit_cast<size_t>(bootstrap_resp.cdc_bootstrap_ids().size()) != table_ids.size()) {
    cout << "Received invalid number of bootstrap ids: " << bootstrap_resp.ShortDebugString();
    return STATUS(InternalError, "Invalid number of bootstrap ids");
  }

  int i = 0;
  for (const auto& bootstrap_id : bootstrap_resp.cdc_bootstrap_ids()) {
    cout << "table id: " << table_ids[i++] << ", CDC bootstrap id: " << bootstrap_id << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::WaitForReplicationDrain(const std::vector<CDCStreamId> &stream_ids,
                                                   const string& target_time) {
  master::WaitForReplicationDrainRequestPB req;
  master::WaitForReplicationDrainResponsePB resp;
  for (const auto& stream_id : stream_ids) {
    req.add_stream_ids(stream_id);
  }
  // If target_time is not provided, it will be set to current time in the master API.
  if (!target_time.empty()) {
    auto result = HybridTime::ParseHybridTime(target_time);
    if (!result.ok()) {
      return STATUS(InvalidArgument, "Error parsing target_time: " + result.ToString());
    }
    req.set_target_time(result->GetPhysicalValueMicros());
  }
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->WaitForReplicationDrain(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error waiting for replication drain: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  std::unordered_map<CDCStreamId, std::vector<TabletId>> undrained_streams;
  for (const auto& stream_info : resp.undrained_stream_info()) {
    undrained_streams[stream_info.stream_id()].push_back(stream_info.tablet_id());
  }
  if (!undrained_streams.empty()) {
    cout << "Found undrained replications:" << endl;
    for (const auto& stream_to_tablets : undrained_streams) {
      cout << "- Under Stream " << stream_to_tablets.first << ":" << endl;
      for (const auto& tablet_id : stream_to_tablets.second) {
        cout << "  - Tablet: " << tablet_id << endl;
      }
    }
  } else {
    cout << "All replications are caught-up." << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::SetupNSUniverseReplication(
    const std::string& producer_uuid,
    const std::vector<std::string>& producer_addresses,
    const TypedNamespaceName& producer_namespace) {
  switch (producer_namespace.db_type) {
    case YQL_DATABASE_CQL:
      break;
    case YQL_DATABASE_PGSQL:
      return STATUS(InvalidArgument,
          "YSQL not currently supported for namespace-level replication setup");
    default:
      return STATUS(InvalidArgument, "Unsupported namespace type");
  }

  master::SetupNSUniverseReplicationRequestPB req;
  master::SetupNSUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_uuid);
  req.set_producer_ns_name(producer_namespace.name);
  req.set_producer_ns_type(producer_namespace.db_type);

  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
  for (const auto& addr : producer_addresses) {
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
    HostPortToPB(hp, req.add_producer_master_addresses());
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->SetupNSUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error setting up namespace-level universe replication: "
         << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "Namespace-level replication setup successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetReplicationInfo(
    const std::string& universe_uuid) {

  master::GetReplicationStatusRequestPB req;
  master::GetReplicationStatusResponsePB resp;

  if (!universe_uuid.empty()) {
    req.set_universe_id(universe_uuid);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetReplicationStatus(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting replication status: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << resp.DebugString();
  return Status::OK();
}

Result<rapidjson::Document> ClusterAdminClient::GetXClusterEstimatedDataLoss() {
  master::GetXClusterEstimatedDataLossRequestPB req;
  master::GetXClusterEstimatedDataLossResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetXClusterEstimatedDataLoss(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting xCluster estimated data loss values: " << resp.error().status().message()
         << endl;
    return StatusFromPB(resp.error().status());
  }

  rapidjson::Document document;
  document.SetArray();
  for (const auto& data_loss : resp.namespace_data_loss()) {
    rapidjson::Value json_entry(rapidjson::kObjectType);
    AddStringField("namespace_id", data_loss.namespace_id(), &json_entry, &document.GetAllocator());

    // Use 1 second granularity.
    int64_t data_loss_s = MonoDelta::FromMicroseconds(data_loss.data_loss_us()).ToSeconds();
    AddStringField(
        "data_loss_sec", std::to_string(data_loss_s), &json_entry, &document.GetAllocator());
    document.PushBack(json_entry, document.GetAllocator());
  }

  return document;
}

Result<rapidjson::Document> ClusterAdminClient::GetXClusterSafeTime() {
  master::GetXClusterSafeTimeRequestPB req;
  master::GetXClusterSafeTimeResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetXClusterSafeTime(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting xCluster safe time values: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  rapidjson::Document document;
  document.SetArray();
  for (const auto& safe_time : resp.namespace_safe_times()) {
    rapidjson::Value json_entry(rapidjson::kObjectType);
    AddStringField("namespace_id", safe_time.namespace_id(), &json_entry, &document.GetAllocator());
    const auto& st = HybridTime::FromPB(safe_time.safe_time_ht());
    AddStringField("safe_time", HybridTimeToString(st), &json_entry, &document.GetAllocator());
    AddStringField(
        "safe_time_epoch", std::to_string(st.GetPhysicalValueMicros()), &json_entry,
        &document.GetAllocator());
    document.PushBack(json_entry, document.GetAllocator());
  }

  return document;
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
