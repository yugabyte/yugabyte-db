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

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/json_util.h"
#include "yb/master/master_replication.pb.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_cache_connection, true,
    "Whether we should cache the ddl_queue handler's connection, or always recreate it.");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_log_queries, false,
    "Whether to log queries run by the XClusterDDLQueueHandler. "
    "Note that this could include sensitive information.");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_fail_at_end, false,
    "Whether the ddl_queue handler should fail at the end of processing (this will cause it "
    "to reprocess the current batch in a loop).");

#define VALIDATE_MEMBER(doc, member_name, expected_type) \
  SCHECK( \
      doc.HasMember(member_name), NotFound, \
      Format("JSON parse error: '$0' member not found.", member_name)); \
  SCHECK( \
      doc[member_name].Is##expected_type(), InvalidArgument, \
      Format("JSON parse error: '$0' member should be of type $1.", member_name, #expected_type))

#define HAS_MEMBER_OF_TYPE(doc, member_name, is_type) \
  (doc.HasMember(member_name) && doc[member_name].is_type())

#define LOG_QUERY(query) \
  LOG_IF(INFO, FLAGS_TEST_xcluster_ddl_queue_handler_log_queries) << "Running query: " << query;

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
const char* kDDLJsonSchema = "schema";
const char* kDDLJsonUser = "user";
const char* kDDLJsonManualReplication = "manual_replication";
const char* kDDLPrepStmtManualInsert = "manual_replication_insert";
const char* kDDLPrepStmtAlreadyProcessed = "already_processed_row";

const std::unordered_set<std::string> kSupportedCommandTags{"CREATE TABLE", "CREATE INDEX"};

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

    // Need to reverify replicated_ddls if this DDL has already been processed.
    if (VERIFY_RESULT(CheckIfAlreadyProcessed(start_time, query_id))) {
      continue;
    }

    if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonManualReplication, IsBool)) {
      // Just add to the replicated_ddls table.
      RETURN_NOT_OK(ProcessManualExecutionQuery({query, start_time, query_id}));
      continue;
    }

    const std::string& schema =
        HAS_MEMBER_OF_TYPE(doc, kDDLJsonSchema, IsString) ? doc[kDDLJsonSchema].GetString() : "";
    const std::string& user =
        HAS_MEMBER_OF_TYPE(doc, kDDLJsonUser, IsString) ? doc[kDDLJsonUser].GetString() : "";
    VLOG(1) << "ProcessDDLQueueTable: Processing entry "
            << YB_STRUCT_TO_STRING(start_time, query_id, command_tag, query, schema, user);

    SCHECK(
        kSupportedCommandTags.contains(command_tag), InvalidArgument,
        "Found unsupported command tag $0", command_tag);

    RETURN_NOT_OK(ProcessDDLQuery({query, start_time, query_id, schema, user}));
  }

  if (FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end) {
    return STATUS(InternalError, "Failing due to xcluster_ddl_queue_handler_fail_at_end");
  }
  applied_new_records_ = false;
  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessDDLQuery(const DDLQueryInfo& query_info) {
  std::stringstream setup_query;
  setup_query << "SET ROLE NONE;";

  // Set session variables in order to pass the key to the replicated_ddls table.
  setup_query << Format("SET $0 TO $1;", kLocalVariableStartTime, query_info.start_time);
  setup_query << Format("SET $0 TO $1;", kLocalVariableQueryId, query_info.query_id);

  // Set schema and role after setting the superuser extension variables.
  if (!query_info.schema.empty()) {
    setup_query << Format("SET SCHEMA '$0';", query_info.schema);
  }
  if (!query_info.user.empty()) {
    setup_query << Format("SET ROLE $0;", query_info.user);
  }

  RETURN_NOT_OK(RunAndLogQuery(setup_query.str()));
  RETURN_NOT_OK(RunAndLogQuery(query_info.query));
  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessManualExecutionQuery(const DDLQueryInfo& query_info) {
  rapidjson::Document doc;
  doc.SetObject();
  doc.AddMember(
      rapidjson::StringRef(kDDLJsonQuery),
      rapidjson::Value(query_info.query.c_str(), doc.GetAllocator()), doc.GetAllocator());
  doc.AddMember(rapidjson::StringRef(kDDLJsonManualReplication), true, doc.GetAllocator());

  RETURN_NOT_OK(RunAndLogQuery(Format(
      "EXECUTE $0($1, $2, '$3')", kDDLPrepStmtManualInsert, query_info.start_time,
      query_info.query_id, common::WriteRapidJsonToString(doc))));
  return Status::OK();
}

Status XClusterDDLQueueHandler::RunAndLogQuery(const std::string& query) {
  LOG_IF(INFO, FLAGS_TEST_xcluster_ddl_queue_handler_log_queries)
      << "XClusterDDLQueueHandler: Running query: " << query;
  return pg_conn_->Execute(query);
}

Result<bool> XClusterDDLQueueHandler::CheckIfAlreadyProcessed(int64 start_time, int64 query_id) {
  return pg_conn_->FetchRow<bool>(
      Format("EXECUTE $0($1, $2)", kDDLPrepStmtAlreadyProcessed, start_time, query_id));
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

  std::stringstream query;
  // Read with tablet level consistency so that we see all the latest records.
  query << "SET yb_xcluster_consistency_level = tablet;";
  // Allow writes on target (read-only) cluster.
  query << "SET yb_non_ddl_txn_for_sys_tables_allowed = 1;";
  // Prepare replicated_ddls insert for manually replicated ddls.
  query << "PREPARE " << kDDLPrepStmtManualInsert << "(bigint, bigint, text) AS "
        << "INSERT INTO " << xcluster::kDDLQueuePgSchemaName << "."
        << xcluster::kDDLReplicatedTableName << " VALUES ($1, $2, $3::jsonb);";
  // Prepare replicated_ddls select query.
  query << "PREPARE " << kDDLPrepStmtAlreadyProcessed << "(bigint, bigint) AS "
        << "SELECT EXISTS(SELECT 1 FROM " << xcluster::kDDLQueuePgSchemaName << "."
        << xcluster::kDDLReplicatedTableName << " WHERE " << xcluster::kDDLQueueStartTimeColumn
        << " = $1 AND " << xcluster::kDDLQueueQueryIdColumn << " = $2);";
  RETURN_NOT_OK(pg_conn_->Execute(query.str()));

  return Status::OK();
}

Result<HybridTime> XClusterDDLQueueHandler::GetXClusterSafeTimeForNamespace() {
  return local_client_->client->GetXClusterSafeTimeForNamespace(
      namespace_id_, master::XClusterSafeTimeFilter::DDL_QUEUE);
}

Result<std::vector<std::tuple<int64, int64, std::string>>>
XClusterDDLQueueHandler::GetRowsToProcess(const HybridTime& apply_safe_time) {
  // Since applies can come out of order, need to read at the apply_safe_time and not latest.
  RETURN_NOT_OK(pg_conn_->ExecuteFormat(
      "SET ROLE NONE; SET yb_read_time = $0", apply_safe_time.GetPhysicalValueMicros()));
  // Select all rows that are in ddl_queue but not in replicated_ddls.
  // Note that this is done at apply_safe_time and rows written to replicated_ddls are done at the
  // time the DDL is rerun, so this does not filter out all rows (see kDDLPrepStmtAlreadyProcessed).
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
