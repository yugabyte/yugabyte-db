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

#include "yb/tserver/xcluster_ddl_queue_handler.h"

#include <rapidjson/error/en.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client.h"
#include "yb/common/hybrid_time.h"
#include "yb/master/master_replication.pb.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_cache_connection, true,
    "Whether we should cache the ddl_queue handler's connection, or always recreate it.");

#define VALIDATE_MEMBER(doc, member_name, expected_type) \
  SCHECK( \
      doc.HasMember(member_name), NotFound, \
      Format("JSON parse error: '$0' member not found.", member_name)); \
  SCHECK( \
      doc[member_name].Is##expected_type(), InvalidArgument, \
      Format("JSON parse error: '$0' member should be of type $1.", member_name, #expected_type))

#define HAS_MEMBER_OF_TYPE(doc, member_name, is_type) \
  (doc.HasMember(member_name) && doc[member_name].is_type())

namespace yb::tserver {

namespace {

const std::string kDDLQueueFullTableName =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, xcluster::kDDLQueueTableName);
const std::string kReplicatedDDLsFullTableName =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, xcluster::kDDLReplicatedTableName);
const std::string kLocalVariableStartTime =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_start_time");
const std::string kLocalVariableNodeId =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_node_id");
const std::string kLocalVariablePgBackendId =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_pg_backend_id");

const int kDDLQueueJsonVersion = 1;
const char* kDDLJsonCommandTag = "command_tag";
const char* kDDLJsonQuery = "query";
const char* kDDLJsonVersion = "version";
const char* kDDLJsonNewTableId = "new_table_id";
const char* kDDLJsonNewIndexIds = "new_index_ids";
const char* kDDLJsonDroppedIds = "dropped_ids";

const char* kDDLCommandCreateTable = "CREATE TABLE";
const char* kDDLCommandDropTable = "DROP TABLE";

Result<rapidjson::Document> ParseSerializedJson(const std::string& raw_json_data) {
  SCHECK(!raw_json_data.empty(), InvalidArgument, "Received empty json to parse.");

  // Serialized json string has an extra character at the front.
  const int64_t kCompleteJsonb = 1;
  SCHECK_EQ(raw_json_data[0], kCompleteJsonb, Corruption, "Could not parse serialized json.");

  rapidjson::Document doc;
  doc.Parse(raw_json_data.c_str() + 1);
  SCHECK(
      !doc.HasParseError(), Corruption,
      Format("Error parsing JSON: $0.", rapidjson::GetParseError_En(doc.GetParseError())));
  return doc;
}

}  // namespace

XClusterDDLQueueHandler::XClusterDDLQueueHandler(
    std::shared_ptr<XClusterClient> local_client, const NamespaceName& namespace_name,
    const NamespaceId& namespace_id, ConnectToPostgresFunc connect_to_pg_func)
    : local_client_(local_client),
      namespace_name_(namespace_name),
      namespace_id_(namespace_id),
      connect_to_pg_func_(std::move(connect_to_pg_func)) {}

Status XClusterDDLQueueHandler::ProcessDDLQueueTable(const XClusterOutputClientResponse& response) {
  DCHECK(response.status.ok());
  if (!response.get_changes_response->has_safe_hybrid_time()) {
    return Status::OK();
  }
  // TODO(#20929): Don't need to rescan tables if there haven't been any new records.

  // Wait for all other pollers to have gotten to this safe time.
  HybridTime target_safe_ht(response.get_changes_response->safe_hybrid_time());
  SCHECK(
      target_safe_ht.is_valid(), InvalidArgument, "Received invalid safe hybrid time $0",
      target_safe_ht);

  // TODO(#20928): Make these calls async.
  HybridTime safe_time_ht = VERIFY_RESULT(GetXClusterSafeTimeForNamespace());
  SCHECK(
      safe_time_ht.is_valid(), InternalError, "Found invalid safe time for namespace $0",
      namespace_id_);
  SCHECK_GE(
      safe_time_ht, target_safe_ht, TryAgain, "Waiting for other pollers to catch up to safe time");

  RETURN_NOT_OK(InitPGConnection(target_safe_ht));

  // TODO(#20928): Make these calls async.
  auto rows = VERIFY_RESULT(GetRowsToProcess());
  for (const auto& [start_time, node_id, pg_backend_pid, raw_json_data] : rows) {
    rapidjson::Document doc = VERIFY_RESULT(ParseSerializedJson(raw_json_data));
    VALIDATE_MEMBER(doc, kDDLJsonVersion, Int);
    const auto& version = doc[kDDLJsonVersion].GetInt();
    SCHECK_EQ(version, kDDLQueueJsonVersion, InvalidArgument, "Invalid JSON version");

    VALIDATE_MEMBER(doc, kDDLJsonCommandTag, String);
    VALIDATE_MEMBER(doc, kDDLJsonQuery, String);
    const std::string& command_tag = doc[kDDLJsonCommandTag].GetString();
    const std::string& query = doc[kDDLJsonQuery].GetString();
    VLOG(1) << "ProcessDDLQueueTable: Processing entry "
            << YB_STRUCT_TO_STRING(start_time, node_id, pg_backend_pid, command_tag, query);

    // Check if this DDL has already been processed.
    auto already_processed =
        VERIFY_RESULT(CheckIfAlreadyProcessed(start_time, node_id, pg_backend_pid));

    // Set session variables to store values into replicated_ddls table.
    RETURN_NOT_OK(StoreSessionVariables(start_time, node_id, pg_backend_pid));

    if (command_tag == kDDLCommandCreateTable) {
      RETURN_NOT_OK(HandleCreateTable(already_processed, query, doc));
    } else if (command_tag == kDDLCommandDropTable) {
      RETURN_NOT_OK(HandleDropTable(already_processed, query, doc));
    } else {
      return STATUS_FORMAT(InvalidArgument, "Found unsupported command tag $0", command_tag);
    }

    // Delete row from ddl_queue now that we are done processing it.
    RETURN_NOT_OK(RemoveDdlQueueEntry(start_time, node_id, pg_backend_pid));
  }

  return Status::OK();
}

Status XClusterDDLQueueHandler::HandleCreateTable(
    bool already_processed, const std::string& query, const rapidjson::Document& doc) {
  // First run the query, then add the new objects to replication.
  if (!already_processed) {
    RETURN_NOT_OK(pg_conn_->Execute(query));
  }

  // Add newly created tables (and unique indexes) to the replication group.
  VALIDATE_MEMBER(doc, kDDLJsonNewTableId, String);
  std::vector<std::string> new_table_ids = {doc[kDDLJsonNewTableId].GetString()};
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonNewIndexIds, IsArray)) {
    const auto& arr = doc[kDDLJsonNewIndexIds].GetArray();
    for (const auto& extra_index : arr) {
      new_table_ids.push_back(extra_index.GetString());
    }
  }
  RSTATUS_DCHECK(!new_table_ids.empty(), InternalError, "Found no new table ids in create table");

  return Status::OK();
}

Status XClusterDDLQueueHandler::HandleDropTable(
    bool already_processed, const std::string& query, const rapidjson::Document& doc) {
  if (already_processed) {
    return Status::OK();
  }

  std::vector<std::string> dropped_obj_ids;
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonDroppedIds, IsArray)) {
    const auto& arr = doc[kDDLJsonDroppedIds].GetArray();
    for (const auto& extra_index : arr) {
      dropped_obj_ids.push_back(extra_index.GetString());
    }
  }
  RSTATUS_DCHECK(
      !dropped_obj_ids.empty(), InternalError, "Found no dropped object ids in drop table");

  RETURN_NOT_OK(pg_conn_->Execute(query));

  return Status::OK();
}

Status XClusterDDLQueueHandler::InitPGConnection(const HybridTime& apply_safe_time) {
  // Since applies can come out of order, need to read at the apply_safe_time and not latest.
  if (pg_conn_ && FLAGS_TEST_xcluster_ddl_queue_handler_cache_connection) {
    return pg_conn_->ExecuteFormat("SET yb_read_time = $0", apply_safe_time.ToUint64());
  }
  // Create pg connection if it doesn't exist.
  // TODO(#20693) Create prepared statements as part of opening the connection.
  CoarseTimePoint deadline = CoarseMonoClock::Now() + local_client_->client->default_rpc_timeout();
  pg_conn_ = std::make_unique<pgwrapper::PGConn>(
      VERIFY_RESULT(connect_to_pg_func_(namespace_name_, deadline)));

  // Read with tablet level consistency so that we see all the latest records.
  RETURN_NOT_OK(pg_conn_->ExecuteFormat(
      "SET yb_xcluster_consistency_level = tablet; SET yb_read_time = $0",
      apply_safe_time.ToUint64()));

  return Status::OK();
}

Result<HybridTime> XClusterDDLQueueHandler::GetXClusterSafeTimeForNamespace() {
  return local_client_->client->GetXClusterSafeTimeForNamespace(
      namespace_id_, master::XClusterSafeTimeFilter::DDL_QUEUE);
}

Result<std::vector<std::tuple<int64, yb::Uuid, int, std::string>>>
XClusterDDLQueueHandler::GetRowsToProcess() {
  return pg_conn_->FetchRows<int64_t, Uuid, int32_t, std::string>(Format(
      "SELECT $0, $1, $2, $3 FROM $4 ORDER BY $0 ASC", xcluster::kDDLQueueStartTime,
      xcluster::kDDLQueueNodeId, xcluster::kDDLQueuePgBackendId, xcluster::kDDLQueueYbData,
      kDDLQueueFullTableName));
}

Result<bool> XClusterDDLQueueHandler::CheckIfAlreadyProcessed(
    int64 start_time, const yb::Uuid& node_id, int pg_backend_pid) {
  return pg_conn_->FetchRow<int64_t>(Format(
      "SELECT COUNT(*) FROM $0 WHERE $1 = $2 AND $3 = '$4' AND $5 = $6",
      kReplicatedDDLsFullTableName, xcluster::kDDLQueueStartTime, start_time,
      xcluster::kDDLQueueNodeId, node_id, xcluster::kDDLQueuePgBackendId, pg_backend_pid));
}

Status XClusterDDLQueueHandler::StoreSessionVariables(
    int64 start_time, const yb::Uuid& node_id, int pg_backend_pid) {
  return pg_conn_->ExecuteFormat(
      "SET $0 TO $1; SET $2 TO '$3'; SET $4 TO $5", kLocalVariableStartTime, start_time,
      kLocalVariableNodeId, node_id, kLocalVariablePgBackendId, pg_backend_pid);
}

Status XClusterDDLQueueHandler::RemoveDdlQueueEntry(
    int64 start_time, const yb::Uuid& node_id, int pg_backend_pid) {
  return pg_conn_->ExecuteFormat(
      "SET yb_non_ddl_txn_for_sys_tables_allowed TO true;"
      "DELETE FROM $0 "
      "WHERE $1 = $2 AND $2 = '$4' AND $5 = $6",
      kDDLQueueFullTableName, xcluster::kDDLQueueStartTime, start_time, xcluster::kDDLQueueNodeId,
      node_id, xcluster::kDDLQueuePgBackendId, pg_backend_pid);
}

}  // namespace yb::tserver
