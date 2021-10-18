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

#include "yb/tools/yb-admin_cli.h"
#include "yb/tools/yb-admin_client.h"

#include <iostream>

#include <boost/algorithm/string.hpp>

#include <rapidjson/document.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/client/client.h"
#include "yb/common/entity_ids.h"
#include "yb/common/json_util.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/strings/util.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"
#include "yb/rpc/messenger.h"
#include "yb/tools/yb-admin_util.h"
#include "yb/util/cast.h"
#include "yb/util/env.h"
#include "yb/util/flag_tags.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/physical_time.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/string_case.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/timestamp.h"
#include "yb/util/encryption_util.h"

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
using std::string;
using std::vector;

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
using master::IsCreateTableDoneRequestPB;
using master::IsCreateTableDoneResponsePB;
using master::ListSnapshotRestorationsRequestPB;
using master::ListSnapshotRestorationsResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::ListTablesRequestPB;
using master::ListTablesResponsePB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::SnapshotInfoPB;
using master::SysNamespaceEntryPB;
using master::SysRowEntry;
using master::BackupRowEntryPB;
using master::SysTablesEntryPB;
using master::SysSnapshotEntryPB;

PB_ENUM_FORMATTERS(yb::master::SysSnapshotEntryPB::State);

Status ClusterAdminClient::ListSnapshots(const ListSnapshotsFlags& flags) {
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListSnapshotsRequestPB req;
    req.set_list_deleted_snapshots(flags.Test(ListSnapshotsFlag::SHOW_DELETED));
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));

  rapidjson::Document document(rapidjson::kObjectType);
  bool json = flags.Test(ListSnapshotsFlag::JSON);

  if (resp.has_current_snapshot_id()) {
    if (json) {
      AddStringField("current_snapshot_id",
                     SnapshotIdToString(resp.current_snapshot_id()),
                     &document, &document.GetAllocator());
    } else {
      cout << "Current snapshot id: " << SnapshotIdToString(resp.current_snapshot_id()) << endl;
    }
  }

  rapidjson::Value json_snapshots(rapidjson::kArrayType);
  if (!json) {
    if (resp.snapshots_size()) {
      cout << RightPadToUuidWidth("Snapshot UUID") << kColumnSep << "State" << endl;
    } else {
      cout << "No snapshots" << endl;
    }
  }

  for (SnapshotInfoPB& snapshot : *resp.mutable_snapshots()) {
    rapidjson::Value json_snapshot(rapidjson::kObjectType);
    if (json) {
      AddStringField(
          "id", SnapshotIdToString(snapshot.id()), &json_snapshot, &document.GetAllocator());
      const auto& entry = snapshot.entry();
      AddStringField(
          "state", SysSnapshotEntryPB::State_Name(entry.state()), &json_snapshot,
          &document.GetAllocator());
      AddStringField(
          "snapshot_time", HybridTimeToString(HybridTime::FromPB(entry.snapshot_hybrid_time())),
          &json_snapshot, &document.GetAllocator());
      AddStringField(
          "previous_snapshot_time",
          HybridTimeToString(HybridTime::FromPB(entry.previous_snapshot_hybrid_time())),
          &json_snapshot, &document.GetAllocator());
    } else {
      cout << SnapshotIdToString(snapshot.id()) << kColumnSep << snapshot.entry().state() << endl;
    }

    // Not implemented in json mode.
    if (flags.Test(ListSnapshotsFlag::SHOW_DETAILS)) {
      for (SysRowEntry& entry : *snapshot.mutable_entry()->mutable_entries()) {
        string decoded_data;
        switch (entry.type()) {
          case SysRowEntry::NAMESPACE: {
            auto meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
            meta.clear_transaction();
            decoded_data = JsonWriter::ToJson(meta, JsonWriter::COMPACT);
            break;
          }
          case SysRowEntry::TABLE: {
            auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
            meta.clear_schema();
            meta.clear_partition_schema();
            meta.clear_index_info();
            meta.clear_indexes();
            meta.clear_transaction();
            decoded_data = JsonWriter::ToJson(meta, JsonWriter::COMPACT);
            break;
          }
          default:
            break;
        }

        if (!decoded_data.empty()) {
          entry.set_data("DATA");
          cout << kColumnSep << StringReplace(JsonWriter::ToJson(entry, JsonWriter::COMPACT),
                                              "\"DATA\"", decoded_data, false) << endl;
        }
      }
    }
    if (json) {
      json_snapshots.PushBack(json_snapshot, document.GetAllocator());
    }
  }

  ListSnapshotRestorationsResponsePB rest_resp;
  RETURN_NOT_OK(RequestMasterLeader(&rest_resp, [&](RpcController* rpc) {
    ListSnapshotRestorationsRequestPB rest_req;
    return master_backup_proxy_->ListSnapshotRestorations(rest_req, &rest_resp, rpc);
  }));

  if (json) {
    document.AddMember("snapshots", json_snapshots, document.GetAllocator());
    std::cout << common::PrettyWriteRapidJsonToString(document) << std::endl;
    return Status::OK();
  }

  if (rest_resp.restorations_size() == 0) {
    cout << "No snapshot restorations" << endl;
  } else if (flags.Test(ListSnapshotsFlag::NOT_SHOW_RESTORED)) {
    cout << "Not show fully RESTORED entries" << endl;
  }

  bool title_printed = false;
  for (const auto& restoration : rest_resp.restorations()) {
    if (!flags.Test(ListSnapshotsFlag::NOT_SHOW_RESTORED) ||
        restoration.entry().state() != SysSnapshotEntryPB::RESTORED) {
      if (!title_printed) {
        cout << RightPadToUuidWidth("Restoration UUID") << kColumnSep << "State" << endl;
        title_printed = true;
      }
      cout << TryFullyDecodeTxnSnapshotRestorationId(restoration.id()) << kColumnSep
           << restoration.entry().state() << endl;
    }
  }

  return Status::OK();
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
    return master_proxy_->ListTables(req, &resp, rpc);
  }));

  if (resp.tables_size() == 0) {
    return STATUS_FORMAT(InvalidArgument, "No tables found in namespace: $0", ns.name);
  }

  vector<YBTableName> tables(resp.tables_size());
  for (int i = 0; i < resp.tables_size(); ++i) {
    const auto& table = resp.tables(i);
    tables[i].set_table_id(table.id());
    tables[i].set_namespace_id(table.namespace_().id());

    RSTATUS_DCHECK(table.relation_type() == master::USER_TABLE_RELATION ||
            table.relation_type() == master::INDEX_TABLE_RELATION, InternalError,
            Format("Invalid relation type: $0", table.relation_type()));
    RSTATUS_DCHECK_EQ(table.namespace_().name(), ns.name, InternalError,
               Format("Invalid namespace name: $0", table.namespace_().name()));
    RSTATUS_DCHECK_EQ(table.namespace_().database_type(), ns.db_type, InternalError,
               Format("Invalid namespace type: $0",
                      YQLDatabase_Name(table.namespace_().database_type())));
  }

  return CreateSnapshot(tables, /* add_indexes */ false);
}

Result<rapidjson::Document> ClusterAdminClient::ListSnapshotRestorations(
    const TxnSnapshotRestorationId& restoration_id) {
  master::ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::ListSnapshotRestorationsRequestPB req;
    if (restoration_id) {
      req.set_restoration_id(restoration_id.data(), restoration_id.size());
    }
    return master_backup_proxy_->ListSnapshotRestorations(req, &resp, rpc);
  }));

  rapidjson::Document result;
  result.SetObject();
  rapidjson::Value json_restorations(rapidjson::kArrayType);
  for (const auto& restoration : resp.restorations()) {
    rapidjson::Value json_restoration(rapidjson::kObjectType);
    AddStringField("id",
                   VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(restoration.id())).ToString(),
                   &json_restoration, &result.GetAllocator());
    AddStringField(
        "snapshot_id",
        VERIFY_RESULT(FullyDecodeTxnSnapshotId(restoration.entry().snapshot_id())).ToString(),
        &json_restoration, &result.GetAllocator());
    AddStringField(
        "state",
        master::SysSnapshotEntryPB_State_Name(restoration.entry().state()),
        &json_restoration, &result.GetAllocator());
    json_restorations.PushBack(json_restoration, result.GetAllocator());
  }
  result.AddMember("restorations", json_restorations, result.GetAllocator());
  return result;
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
    rapidjson::Value json_schedule(rapidjson::kObjectType);
    AddStringField("id", VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id())).ToString(),
                   &json_schedule, &result.GetAllocator());

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
    AddStringField("filter", filter_output, &options, &result.GetAllocator());
    auto interval_min = schedule.options().interval_sec() / MonoTime::kSecondsPerMinute;
    AddStringField("interval",
                   Format("$0 min", interval_min),
                   &options, &result.GetAllocator());
    auto retention_min = schedule.options().retention_duration_sec() / MonoTime::kSecondsPerMinute;
    AddStringField("retention",
                   Format("$0 min", retention_min),
                   &options, &result.GetAllocator());
    auto delete_time = HybridTime::FromPB(schedule.options().delete_time());
    if (delete_time) {
      AddStringField("delete_time", HybridTimeToString(delete_time), &options,
                     &result.GetAllocator());
    }

    json_schedule.AddMember("options", options, result.GetAllocator());
    rapidjson::Value json_snapshots(rapidjson::kArrayType);
    for (const auto& snapshot : schedule.snapshots()) {
      rapidjson::Value json_snapshot(rapidjson::kObjectType);
      AddStringField("id", VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id())).ToString(),
                     &json_snapshot, &result.GetAllocator());
      auto snapshot_ht = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
      AddStringField("snapshot_time",
                     HybridTimeToString(snapshot_ht),
                     &json_snapshot, &result.GetAllocator());
      auto previous_snapshot_ht = HybridTime::FromPB(
          snapshot.entry().previous_snapshot_hybrid_time());
      if (previous_snapshot_ht) {
        AddStringField(
            "previous_snapshot_time",
            HybridTimeToString(previous_snapshot_ht),
            &json_snapshot, &result.GetAllocator());
      }
      json_snapshots.PushBack(json_snapshot, result.GetAllocator());
    }
    json_schedule.AddMember("snapshots", json_snapshots, result.GetAllocator());
    json_schedules.PushBack(json_schedule, result.GetAllocator());
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

Result<rapidjson::Document> ClusterAdminClient::RestoreSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at) {
  auto deadline = CoarseMonoClock::now() + timeout_;

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
    // Format 1 - old format.
    // Format 2 - new format.
    if (FLAGS_TEST_metadata_file_format_version != 1) {
      req.set_prepare_for_backup(true);
    }
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));

  const SnapshotInfoPB* snapshot = nullptr;
  for (const auto& snapshot_entry : resp.snapshots()) {
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
  if (resp.snapshots_size() > 1) {
    LOG(WARNING) << "Requested snapshot metadata for snapshot '" << snapshot_id << "', but got "
                 << resp.snapshots_size() << " snapshots in the response";
  }

  cout << "Exporting snapshot " << snapshot_id << " ("
       << snapshot->entry().state() << ") to file " << file_name << endl;

  // Serialize snapshot protobuf to given path.
  RETURN_NOT_OK(pb_util::WritePBContainerToPath(
      Env::Default(), file_name, *snapshot, pb_util::OVERWRITE, pb_util::SYNC));

  cout << "Snapshot metadata was saved into file: " << file_name << endl;
  return Status::OK();
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

  int table_index = 0;
  bool was_table_renamed = false;
  for (BackupRowEntryPB& backup_entry : *snapshot_info->mutable_backup_entries()) {
    SysRowEntry& entry = *backup_entry.mutable_entry();
    const YBTableName table_name = table_index < tables.size()
        ? tables[table_index] : YBTableName();

    switch (entry.type()) {
      case SysRowEntry::NAMESPACE: {
        auto meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));

        if (!keyspace.name.empty() && keyspace.name != meta.name()) {
          meta.set_name(keyspace.name);
          entry.set_data(meta.SerializeAsString());
        }
        break;
      }
      case SysRowEntry::TABLE: {
        if (was_table_renamed && table_name.empty()) {
          // Renaming is allowed for all tables OR for no one table.
          return STATUS_FORMAT(InvalidArgument,
                               "There is no name for table (including indexes) number: $0",
                               table_index);
        }

        auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));

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

        if (meta.namespace_name().empty()) {
          return STATUS(IllegalState, "Could not find keyspace name from snapshot metadata");
        }

        // Update the table name if needed.
        if (update_meta) {
          entry.set_data(meta.SerializeAsString());
        }

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

        cout << (meta.colocated() ? "Colocated t" : "T") << "able being imported: "
             << meta.namespace_name() << "." << meta.name() << endl;
        ++table_index;
        break;
      }
      default:
        break;
    }
  }

  ImportSnapshotMetaResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    return master_backup_proxy_->ImportSnapshotMeta(req, &resp, rpc);
  }));

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
    return master_proxy_->GetTabletLocations(req, &resp, rpc);
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
          replica.member_type() == consensus::RaftPeerPB::PRE_OBSERVER ||
          replica.member_type() == consensus::RaftPeerPB::OBSERVER;
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
  for (const string& zone : preferred_zones) {
    if (std::find(zones.begin(), zones.end(), zone) != zones.end()) {
      continue;
    }
    size_t last_pos = 0;
    size_t next_pos;
    std::vector<string> tokens;
    while ((next_pos = zone.find(".", last_pos)) != string::npos) {
      tokens.push_back(zone.substr(last_pos, next_pos - last_pos));
      last_pos = next_pos + 1;
    }
    tokens.push_back(zone.substr(last_pos, zone.size() - last_pos));
    if (tokens.size() != 3) {
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid argument for preferred zone $0, should "
          "have format cloud.region.zone", zone);
    }

    CloudInfoPB* cloud_info = req.add_preferred_zones();
    cloud_info->set_placement_cloud(tokens[0]);
    cloud_info->set_placement_region(tokens[1]);
    cloud_info->set_placement_zone(tokens[2]);

    zones.emplace(zone);
  }

  RETURN_NOT_OK(master_proxy_->SetPreferredZones(req, &resp, &rpc));

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
  RETURN_NOT_OK_PREPEND(master_proxy_->
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
  RETURN_NOT_OK_PREPEND(master_proxy_->
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

  RETURN_NOT_OK(EncryptionParams::IsValidKeySize(
      universe_key.size() - EncryptionParams::kBlockSize));

  master::AddUniverseKeysRequestPB req;
  master::AddUniverseKeysResponsePB resp;
  auto* universe_keys = req.mutable_universe_keys();
  (*universe_keys->mutable_map())[key_id] = universe_key;

  for (auto hp : VERIFY_RESULT(HostPort::ParseStrings(master_addr_list_, 7100))) {
    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    master::MasterServiceProxy proxy(proxy_cache_.get(), hp);
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
    master::MasterServiceProxy proxy(proxy_cache_.get(), hp);
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
  RETURN_NOT_OK_PREPEND(master_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
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
  RETURN_NOT_OK_PREPEND(master_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
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
  RETURN_NOT_OK_PREPEND(master_proxy_->GetUniverseKeyRegistry(req, &resp, &rpc),
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

Status ClusterAdminClient::CreateCDCStream(const TableId& table_id) {
  master::CreateCDCStreamRequestPB req;
  master::CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);
  req.mutable_options()->Reserve(2);

  auto record_type_option = req.add_options();
  record_type_option->set_key(cdc::kRecordType);
  record_type_option->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  auto record_format_option = req.add_options();
  record_format_option->set_key(cdc::kRecordFormat);
  record_format_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::JSON));

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->CreateCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error creating stream: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Stream ID: " << resp.stream_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteCDCStream(const std::string& stream_id) {
  master::DeleteCDCStreamRequestPB req;
  master::DeleteCDCStreamResponsePB resp;
  req.add_stream_id(stream_id);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->DeleteCDCStream(req, &resp, &rpc));

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
  if (!table_id.empty()) {
    req.set_table_id(table_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting CDC stream list: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  cout << "CDC Streams: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::WaitForSetupUniverseReplicationToFinish(const string& producer_uuid) {
  master::IsSetupUniverseReplicationDoneRequestPB req;
  req.set_producer_id(producer_uuid);
  for (;;) {
    master::IsSetupUniverseReplicationDoneResponsePB resp;
    RpcController rpc;
    rpc.set_timeout(timeout_);
    Status s = master_proxy_->IsSetupUniverseReplicationDone(req, &resp, &rpc);

    if (!s.ok() || resp.has_error()) {
        LOG(WARNING) << "Encountered error while waiting for setup_universe_replication to complete"
                     << " : " << (!s.ok() ? s.ToString() : resp.error().status().message());
    } else if (resp.has_done() && resp.done()) {
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

  req.mutable_producer_master_addresses()->Reserve(producer_addresses.size());
  for (const auto& addr : producer_addresses) {
    // HostPort::FromString() expects a default port.
    auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
    HostPortToPB(hp, req.add_producer_master_addresses());
  }

  req.mutable_producer_table_ids()->Reserve(tables.size());
  for (const auto& table : tables) {
    req.add_producer_table_ids(table);
  }

  for (const auto& producer_bootstrap_id : producer_bootstrap_ids) {
    req.add_producer_bootstrap_ids(producer_bootstrap_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  Status setup_result_status = master_proxy_->SetupUniverseReplication(req, &resp, &rpc);

  // Clean up config files if setup fails.
  if (!setup_result_status.ok()) {
    CleanupEnvironmentOnSetupUniverseReplicationFailure(producer_uuid, setup_result_status);
    return setup_result_status;
  }

  if (resp.has_error()) {
    cout << "Error setting up universe replication: " << resp.error().status().message() << endl;

    Status status_from_error = StatusFromPB(resp.error().status());
    CleanupEnvironmentOnSetupUniverseReplicationFailure(producer_uuid, status_from_error);

    return status_from_error;
  }

  setup_result_status = WaitForSetupUniverseReplicationToFinish(producer_uuid);

  // Clean up config files if setup fails to complete.
  if (!setup_result_status.ok()) {
    CleanupEnvironmentOnSetupUniverseReplicationFailure(producer_uuid, setup_result_status);
    return setup_result_status;
  }

  cout << "Replication setup successfully" << endl;
  return Status::OK();
}

// Helper function for deleting the universe if SetupUniverseReplicaion fails.
void ClusterAdminClient::CleanupEnvironmentOnSetupUniverseReplicationFailure(
  const std::string& producer_uuid, const Status& failure_status) {
  // We don't need to delete the universe if the call to SetupUniverseReplication
  // failed due to one of the sanity checks.
  if (failure_status.IsInvalidArgument()) {
    return;
  }

  cout << "Replication setup failed, cleaning up environment" << endl;

  Status delete_result_status = DeleteUniverseReplication(producer_uuid, false);
  if (!delete_result_status.ok()) {
    cout << "Could not clean up environment: " << delete_result_status.message() << endl;
  } else {
    cout << "Successfully cleaned up environment" << endl;
  }
}

Status ClusterAdminClient::DeleteUniverseReplication(const std::string& producer_id, bool force) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_id);
  req.set_force(force);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->DeleteUniverseReplication(req, &resp, &rpc));

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
    const std::vector<std::string>& producer_bootstrap_ids_to_add) {
  master::AlterUniverseReplicationRequestPB req;
  master::AlterUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_uuid);

  if (!producer_addresses.empty()) {
    req.mutable_producer_master_addresses()->Reserve(producer_addresses.size());
    for (const auto& addr : producer_addresses) {
      // HostPort::FromString() expects a default port.
      auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
      HostPortToPB(hp, req.add_producer_master_addresses());
    }
  }

  if (!add_tables.empty()) {
    req.mutable_producer_table_ids_to_add()->Reserve(add_tables.size());
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

      req.mutable_producer_bootstrap_ids_to_add()->Reserve(producer_bootstrap_ids_to_add.size());
      for (const auto& bootstrap_id : producer_bootstrap_ids_to_add) {
        req.add_producer_bootstrap_ids_to_add(bootstrap_id);
      }
    }
  }

  if (!remove_tables.empty()) {
    req.mutable_producer_table_ids_to_remove()->Reserve(remove_tables.size());
    for (const auto& table : remove_tables) {
      req.add_producer_table_ids_to_remove(table);
    }
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->AlterUniverseReplication(req, &resp, &rpc));

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

CHECKED_STATUS ClusterAdminClient::SetUniverseReplicationEnabled(const std::string& producer_id,
                                                                 bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;
  req.set_producer_id(producer_id);
  req.set_is_enabled(is_enabled);
  const string toggle = (is_enabled ? "enabl" : "disabl");

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_proxy_->SetUniverseReplicationEnabled(req, &resp, &rpc));

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

  if (bootstrap_resp.cdc_bootstrap_ids().size() != table_ids.size()) {
    cout << "Received invalid number of bootstrap ids: " << bootstrap_resp.ShortDebugString();
    return STATUS(InternalError, "Invalid number of bootstrap ids");
  }

  int i = 0;
  for (const auto& bootstrap_id : bootstrap_resp.cdc_bootstrap_ids()) {
    cout << "table id: " << table_ids[i++] << ", CDC bootstrap id: " << bootstrap_id << endl;
  }
  return Status::OK();
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
