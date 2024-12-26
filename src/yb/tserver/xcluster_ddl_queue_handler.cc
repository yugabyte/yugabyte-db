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
#include "yb/common/pg_types.h"
#include "yb/master/master_replication.pb.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/util/scope_exit.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_cache_connection, true,
    "Whether we should cache the ddl_queue handler's connection, or always recreate it.");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_log_queries, false,
    "Whether to log queries run by the XClusterDDLQueueHandler. "
    "Note that this could include sensitive information.");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_fail_at_end, false,
    "Whether the ddl_queue handler should fail at the end of processing (this will cause it "
    "to reprocess the current batch in a loop).");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_fail_at_start, false,
    "Whether the ddl_queue handler should fail at the start of processing (this will cause it "
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
const char* kDDLJsonNewRelMap = "new_rel_map";
const char* kDDLJsonRelFileOid = "relfile_oid";
const char* kDDLJsonRelName = "rel_name";
const char* kDDLJsonManualReplication = "manual_replication";
const char* kDDLPrepStmtManualInsert = "manual_replication_insert";
const char* kDDLPrepStmtAlreadyProcessed = "already_processed_row";

const std::unordered_set<std::string> kSupportedCommandTags {
    // Relations
    "CREATE TABLE",
    "CREATE INDEX",
    "DROP TABLE",
    "DROP INDEX",
    "ALTER TABLE",
    "ALTER INDEX",
    // Pass thru DDLs
    "CREATE ACCESS METHOD",
    "CREATE AGGREGATE",
    "CREATE CAST",
    "CREATE COLLATION",
    "CREATE DOMAIN",
    "CREATE EXTENSION",
    "CREATE FOREIGN DATA WRAPPER",
    "CREATE FOREIGN TABLE",
    "CREATE FUNCTION",
    "CREATE OPERATOR",
    "CREATE OPERATOR CLASS",
    "CREATE OPERATOR FAMILY",
    "CREATE POLICY",
    "CREATE PROCEDURE",
    "CREATE ROUTINE",
    "CREATE RULE",
    "CREATE SCHEMA",
    "CREATE SERVER",
    "CREATE STATISTICS",
    "CREATE TEXT SEARCH CONFIGURATION",
    "CREATE TEXT SEARCH DICTIONARY",
    "CREATE TEXT SEARCH PARSER",
    "CREATE TEXT SEARCH TEMPLATE",
    "CREATE TRIGGER",
    "CREATE USER MAPPING",
    "CREATE VIEW",
    "COMMENT",
    "ALTER AGGREGATE",
    "ALTER CAST",
    "ALTER COLLATION",
    "ALTER DOMAIN",
    "ALTER FUNCTION",
    "ALTER OPERATOR",
    "ALTER OPERATOR CLASS",
    "ALTER OPERATOR FAMILY",
    "ALTER POLICY",
    "ALTER PROCEDURE",
    "ALTER ROUTINE",
    "ALTER RULE",
    "ALTER SCHEMA",
    "ALTER STATISTICS",
    "ALTER TEXT SEARCH CONFIGURATION",
    "ALTER TEXT SEARCH DICTIONARY",
    "ALTER TEXT SEARCH PARSER",
    "ALTER TEXT SEARCH TEMPLATE",
    "ALTER TRIGGER",
    "ALTER VIEW",
    "DROP ACCESS METHOD",
    "DROP AGGREGATE",
    "DROP CAST",
    "DROP COLLATION",
    "DROP DOMAIN",
    "DROP EXTENSION",
    "DROP FOREIGN DATA WRAPPER",
    "DROP FOREIGN TABLE",
    "DROP FUNCTION",
    "DROP OPERATOR",
    "DROP OPERATOR CLASS",
    "DROP OPERATOR FAMILY",
    "DROP POLICY",
    "DROP PROCEDURE",
    "DROP ROUTINE",
    "DROP RULE",
    "DROP SCHEMA",
    "DROP SERVER",
    "DROP STATISTICS",
    "DROP TEXT SEARCH CONFIGURATION",
    "DROP TEXT SEARCH DICTIONARY",
    "DROP TEXT SEARCH PARSER",
    "DROP TEXT SEARCH TEMPLATE",
    "DROP TRIGGER",
    "DROP USER MAPPING",
    "DROP VIEW",
    "IMPORT FOREIGN SCHEMA",
    "SECURITY LABEL",
};

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
    client::YBClient* local_client, const NamespaceName& namespace_name,
    const NamespaceId& namespace_id, const std::string& log_prefix,
    TserverXClusterContextIf& xcluster_context, ConnectToPostgresFunc connect_to_pg_func)
    : local_client_(local_client),
      namespace_name_(namespace_name),
      namespace_id_(namespace_id),
      log_prefix_(log_prefix),
      xcluster_context_(xcluster_context),
      connect_to_pg_func_(std::move(connect_to_pg_func)) {}

XClusterDDLQueueHandler::~XClusterDDLQueueHandler() {}

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
    LOG_WITH_PREFIX(WARNING) << "Received invalid safe time " << target_safe_ht;
    return Status::OK();
  }

  SCHECK(
      !FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start, InternalError,
      "Failing due to xcluster_ddl_queue_handler_fail_at_start");

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
    const auto query_info = VERIFY_RESULT(GetDDLQueryInfo(doc, start_time, query_id));

    // Need to reverify replicated_ddls if this DDL has already been processed.
    if (VERIFY_RESULT(CheckIfAlreadyProcessed(query_info))) {
      continue;
    }

    if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonManualReplication, IsBool)) {
      // Just add to the replicated_ddls table.
      RETURN_NOT_OK(ProcessManualExecutionQuery(query_info));
      continue;
    }

    VLOG_WITH_PREFIX(1) << "ProcessDDLQueueTable: Processing entry " << query_info.ToString();

    SCHECK(
        kSupportedCommandTags.contains(query_info.command_tag), InvalidArgument,
        "Found unsupported command tag $0", query_info.command_tag);

    std::vector<YsqlFullTableName> new_relations;
    auto se = ScopeExit([this, &new_relations]() {
      // Ensure that we always clear the xcluster_context.
      for (const auto& new_rel : new_relations) {
        xcluster_context_.ClearSourceTableMappingForCreateTable(new_rel);
      }
    });

    RETURN_NOT_OK(ProcessNewRelations(doc, query_info.schema, new_relations));
    RETURN_NOT_OK(ProcessDDLQuery(query_info));

    VLOG_WITH_PREFIX(2) << "ProcessDDLQueueTable: Successfully processed entry "
                        << query_info.ToString();
  }

  if (FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end) {
    return STATUS(InternalError, "Failing due to xcluster_ddl_queue_handler_fail_at_end");
  }
  applied_new_records_ = false;
  return Status::OK();
}

Result<XClusterDDLQueueHandler::DDLQueryInfo> XClusterDDLQueueHandler::GetDDLQueryInfo(
    rapidjson::Document& doc, int64 start_time, int64 query_id) {
  DDLQueryInfo query_info;

  VALIDATE_MEMBER(doc, kDDLJsonVersion, Int);
  query_info.version = doc[kDDLJsonVersion].GetInt();
  SCHECK_EQ(query_info.version, kDDLQueueJsonVersion, InvalidArgument, "Invalid JSON version");

  query_info.start_time = start_time;
  query_info.query_id = query_id;

  VALIDATE_MEMBER(doc, kDDLJsonCommandTag, String);
  query_info.command_tag = doc[kDDLJsonCommandTag].GetString();
  VALIDATE_MEMBER(doc, kDDLJsonQuery, String);
  query_info.query = doc[kDDLJsonQuery].GetString();

  query_info.schema =
      HAS_MEMBER_OF_TYPE(doc, kDDLJsonSchema, IsString) ? doc[kDDLJsonSchema].GetString() : "";
  query_info.user =
      HAS_MEMBER_OF_TYPE(doc, kDDLJsonUser, IsString) ? doc[kDDLJsonUser].GetString() : "";

  return query_info;
}

Status XClusterDDLQueueHandler::ProcessNewRelations(
    rapidjson::Document& doc, const std::string& schema,
    std::vector<YsqlFullTableName>& new_relations) {
  const auto& new_rel_map = HAS_MEMBER_OF_TYPE(doc, kDDLJsonNewRelMap, IsArray)
                                ? std::optional(doc[kDDLJsonNewRelMap].GetArray())
                                : std::nullopt;
  if (new_rel_map) {
    // If there are new relations, need to update the context with the table name -> source
    // table id mapping. This will be passed to CreateTable and will be used in
    // add_table_to_xcluster_target task to find the matching source table.
    for (const auto& new_rel : *new_rel_map) {
      VALIDATE_MEMBER(new_rel, kDDLJsonRelFileOid, Int);
      VALIDATE_MEMBER(new_rel, kDDLJsonRelName, String);
      const auto db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_id_));
      const auto relfile_oid = new_rel[kDDLJsonRelFileOid].GetInt();
      const auto rel_name = new_rel[kDDLJsonRelName].GetString();
      RETURN_NOT_OK(xcluster_context_.SetSourceTableMappingForCreateTable(
          {namespace_name_, schema, rel_name}, PgObjectId(db_oid, relfile_oid)));
      new_relations.push_back({namespace_name_, schema, rel_name});
    }
  }
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

  setup_query << "SET yb_skip_data_insert_for_table_rewrite=true;";

  RETURN_NOT_OK(RunAndLogQuery(setup_query.str()));
  RETURN_NOT_OK(RunAndLogQuery(query_info.query));
  RETURN_NOT_OK(RunAndLogQuery("SET yb_skip_data_insert_for_table_rewrite=false"));
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
  LOG_IF_WITH_PREFIX(INFO, FLAGS_TEST_xcluster_ddl_queue_handler_log_queries)
      << "XClusterDDLQueueHandler: Running query: " << query;
  return pg_conn_->Execute(query);
}

Result<bool> XClusterDDLQueueHandler::CheckIfAlreadyProcessed(const DDLQueryInfo& query_info) {
  return pg_conn_->FetchRow<bool>(Format(
      "EXECUTE $0($1, $2)", kDDLPrepStmtAlreadyProcessed, query_info.start_time,
      query_info.query_id));
}

Status XClusterDDLQueueHandler::InitPGConnection() {
  if (pg_conn_ && FLAGS_TEST_xcluster_ddl_queue_handler_cache_connection) {
    return Status::OK();
  }
  // Create pg connection if it doesn't exist.
  // TODO(#20693) Create prepared statements as part of opening the connection.
  CoarseTimePoint deadline = CoarseMonoClock::Now() + local_client_->default_rpc_timeout();
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
  return local_client_->GetXClusterSafeTimeForNamespace(
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
