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
const std::string kLocalVariableQueryId =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_query_id");

const int kDDLQueueJsonVersion = 1;
const char* kDDLJsonCommandTag = "command_tag";
const char* kDDLJsonQuery = "query";
const char* kDDLJsonVersion = "version";

const std::unordered_set<std::string> kSupportedCommandTags{"CREATE TABLE", "DROP TABLE"};

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

  if (response.get_changes_response->records_size() > 0) {
    applied_new_records_ = true;
  }

  if (!(response.get_changes_response->has_safe_hybrid_time() && applied_new_records_)) {
    return Status::OK();
  }

  // Wait for all other pollers to have gotten to this safe time.
  auto target_safe_ht = HybridTime::FromPB(response.get_changes_response->safe_hybrid_time());
  // We don't expect to get an invalid safe time, but it is possible in edge cases (see #21528).
  // Log an error and return for now, wait until a valid safe time does come in so we can continue.
  if (target_safe_ht.is_special()) {
    LOG(WARNING) << "Received invalid safe time " << target_safe_ht;
    return Status::OK();
  }

  // TODO(#20928): Make these calls async.
  HybridTime safe_time_ht = VERIFY_RESULT(GetXClusterSafeTimeForNamespace());
  SCHECK(
      !safe_time_ht.is_special(), InternalError, "Found invalid safe time $0 for namespace $1",
      safe_time_ht, namespace_id_);
  SCHECK_GE(
      safe_time_ht, target_safe_ht, TryAgain, "Waiting for other pollers to catch up to safe time");

  RETURN_NOT_OK(InitPGConnection());

  // TODO(#20928): Make these calls async.
  auto rows = VERIFY_RESULT(GetRowsToProcess(target_safe_ht));
  for (const auto& [start_time, query_id, raw_json_data] : rows) {
    rapidjson::Document doc = VERIFY_RESULT(ParseSerializedJson(raw_json_data));
    VALIDATE_MEMBER(doc, kDDLJsonVersion, Int);
    const auto& version = doc[kDDLJsonVersion].GetInt();
    SCHECK_EQ(version, kDDLQueueJsonVersion, InvalidArgument, "Invalid JSON version");

    VALIDATE_MEMBER(doc, kDDLJsonCommandTag, String);
    VALIDATE_MEMBER(doc, kDDLJsonQuery, String);
    const std::string& command_tag = doc[kDDLJsonCommandTag].GetString();
    const std::string& query = doc[kDDLJsonQuery].GetString();
    VLOG(1) << "ProcessDDLQueueTable: Processing entry "
            << YB_STRUCT_TO_STRING(start_time, query_id, command_tag, query);

    SCHECK(
        kSupportedCommandTags.contains(command_tag), InvalidArgument,
        "Found unsupported command tag $0", command_tag);

    RETURN_NOT_OK(ProcessDDLQuery(start_time, query_id, query));
  }

  applied_new_records_ = false;
  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessDDLQuery(
    int64 start_time, int64 query_id, const std::string& query) {
  // Set session variables in order to pass the key to the replicated_ddls table.
  RETURN_NOT_OK(pg_conn_->ExecuteFormat(
      "SET $0 TO $1; SET $2 TO $3", kLocalVariableStartTime, start_time, kLocalVariableQueryId,
      query_id));
  RETURN_NOT_OK(pg_conn_->Execute(query));
  return Status::OK();
}

Status XClusterDDLQueueHandler::InitPGConnection() {
  if (pg_conn_ && FLAGS_TEST_xcluster_ddl_queue_handler_cache_connection) {
    return Status::OK();
  }
  // Create pg connection if it doesn't exist.
  // TODO(#20693) Create prepared statements as part of opening the connection.
  CoarseTimePoint deadline = CoarseMonoClock::Now() + local_client_->client->default_rpc_timeout();
  pg_conn_ = std::make_unique<pgwrapper::PGConn>(
      VERIFY_RESULT(connect_to_pg_func_(namespace_name_, deadline)));

  // Read with tablet level consistency so that we see all the latest records.
  RETURN_NOT_OK(pg_conn_->Execute("SET yb_xcluster_consistency_level = tablet"));

  return Status::OK();
}

Result<HybridTime> XClusterDDLQueueHandler::GetXClusterSafeTimeForNamespace() {
  return local_client_->client->GetXClusterSafeTimeForNamespace(
      namespace_id_, master::XClusterSafeTimeFilter::DDL_QUEUE);
}

Result<std::vector<std::tuple<int64, int64, std::string>>>
XClusterDDLQueueHandler::GetRowsToProcess(const HybridTime& apply_safe_time) {
  // Since applies can come out of order, need to read at the apply_safe_time and not latest.
  RETURN_NOT_OK(
      pg_conn_->ExecuteFormat("SET yb_read_time = $0", apply_safe_time.GetPhysicalValueMicros()));
  // Select all rows that are in ddl_queue but not in replicated_ddls.
  auto rows = VERIFY_RESULT((pg_conn_->FetchRows<int64_t, int64_t, std::string>(Format(
      "SELECT $0, $1, $2 FROM $3 "
      "WHERE ($0, $1) NOT IN (SELECT $0, $1 FROM $4) "
      "ORDER BY $0 ASC",
      xcluster::kDDLQueueStartTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn, kDDLQueueFullTableName, kReplicatedDDLsFullTableName))));
  // DDLs are blocked when yb_read_time is non-zero, so reset.
  RETURN_NOT_OK(pg_conn_->Execute("SET yb_read_time = 0"));
  return rows;
}

}  // namespace yb::tserver
