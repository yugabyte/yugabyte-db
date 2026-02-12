// Copyright (c) YugabyteDB, Inc.
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

#include <regex>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client.h"
#include "yb/common/constants.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/json_util.h"
#include "yb/common/pg_types.h"
#include "yb/master/master_replication.pb.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_output_client.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_RUNTIME_int32(xcluster_ddl_queue_max_retries_per_ddl, 5,
    "Maximum number of retries per DDL before we pause processing of the ddl_queue table.");

DEFINE_RUNTIME_uint32(xcluster_ddl_queue_statement_timeout_ms, 0,
    "Statement timeout to use for executing DDLs from the ddl_queue table. 0 means no timeout.");

// Random default value to avoid collisions.
DEFINE_RUNTIME_int64(xcluster_ddl_queue_advisory_lock_key, 8674896558949688690,
    "Advisory lock key to use for DDL queue handler sessions. This ensures only one handler "
    "processes the DDL queue at a time. A value of 0 means no advisory lock will be used.");

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

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_fail_before_incremental_safe_time_bump, false,
    "Whether the ddl_queue handler should skip updating the xCluster safe time per DDL.");

DEFINE_test_flag(bool, xcluster_ddl_queue_handler_fail_ddl, false,
    "Whether the ddl_queue handler should fail the ddl command that it executes.");

DECLARE_bool(ysql_yb_enable_advisory_locks);

#define VALIDATE_MEMBER(doc, member_name, expected_type) \
  SCHECK( \
      (doc).HasMember(member_name), NotFound, \
      Format("JSON parse error: '$0' member not found.", member_name)); \
  SCHECK( \
      (doc)[member_name].Is##expected_type(), InvalidArgument, \
      Format("JSON parse error: '$0' member should be of type $1.", member_name, #expected_type))

#define HAS_MEMBER_OF_TYPE(doc, member_name, is_type) \
  ((doc).HasMember(member_name) && (doc)[member_name].is_type())

namespace yb::tserver {

namespace {

const std::string kDDLQueueFullTableName =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, xcluster::kDDLQueueTableName);
const std::string kReplicatedDDLsFullTableName =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, xcluster::kDDLReplicatedTableName);
const std::string kLocalVariableDDLEndTime =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_ddl_end_time");
const std::string kLocalVariableQueryId =
    Format("$0.$1", xcluster::kDDLQueuePgSchemaName, "ddl_queue_primary_key_query_id");

const int kDDLQueueJsonVersion = 1;
const char* kDDLJsonCommandTag = "command_tag";
const char* kDDLJsonQuery = "query";
const char* kDDLJsonVersion = "version";
const char* kDDLJsonSchema = "schema";
const char* kDDLJsonUser = "user";
const char* kDDLJsonNewRelMap = "new_rel_map";
const char* kDDLJsonRelName = "rel_name";
const char* kDDLJsonRelPgSchemaName = "rel_namespace";
const char* kDDLJsonRelFileOid = "relfile_oid";
const char* kDDLJsonColocationId = "colocation_id";
const char* kDDLJsonIsIndex = "is_index";
const char* kDDLJsonEnumLabelInfo = "enum_label_info";
const char* kDDLJsonTypeInfo = "type_info";
const char* kDDLJsonSequenceInfo = "sequence_info";
const char* kDDLJsonVariableMap = "variables";
const char* kDDLJsonManualReplication = "manual_replication";
const char* kDDLPrepStmtManualInsert = "manual_replication_insert";
const char* kDDLPrepStmtAlreadyProcessed = "already_processed_row";
const char* kDDLPrepStmtCommitTimesUpsert = "commit_times_insert";
const char* kDDLPrepStmtCommitTimesSelect = "commit_times_select";
const int kDDLReplicatedTableSpecialKey = 1;
const char* kSafeTimeBatchCommitTimes = "commit_times";
const char* kSafeTimeBatchApplySafeTime = "apply_safe_time";
const char* kSafeTimeBatchLastCommitTimeProcessed = "last_commit_time_processed";

const std::unordered_set<std::string> kSupportedCommandTags {
    // Relations
    "CREATE TABLE",
    "CREATE TABLE AS",
    "SELECT INTO",
    "CREATE INDEX",
    "CREATE MATERIALIZED VIEW",
    "CREATE TYPE",
    "CREATE SEQUENCE",
    "DROP TABLE",
    "DROP INDEX",
    "DROP MATERIALIZED VIEW",
    "DROP TYPE",
    "DROP SEQUENCE",
    "ALTER TABLE",
    "ALTER INDEX",
    "ALTER TYPE",
    "ALTER SEQUENCE",
    "TRUNCATE TABLE",
    "REFRESH MATERIALIZED VIEW",
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
    "ALTER DEFAULT PRIVILEGES",
    "ALTER DOMAIN",
    "ALTER FUNCTION",
    "ALTER MATERIALIZED VIEW",
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
    "DROP OWNED",
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
    "GRANT",
    "REVOKE",
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

// Parse the JSON string and return the XClusterDDLQueryInfo struct.
Result<XClusterDDLQueryInfo> GetDDLQueryInfo(
    const std::string& raw_json_data, int64 ddl_end_time, int64 query_id) {
  rapidjson::Document doc = VERIFY_RESULT(ParseSerializedJson(raw_json_data));
  XClusterDDLQueryInfo query_info;

  VALIDATE_MEMBER(doc, kDDLJsonVersion, Int);
  query_info.version = doc[kDDLJsonVersion].GetInt();
  SCHECK_EQ(query_info.version, kDDLQueueJsonVersion, InvalidArgument, "Invalid JSON version");

  query_info.ddl_end_time = ddl_end_time;
  query_info.query_id = query_id;

  VALIDATE_MEMBER(doc, kDDLJsonCommandTag, String);
  query_info.command_tag = doc[kDDLJsonCommandTag].GetString();
  VALIDATE_MEMBER(doc, kDDLJsonQuery, String);
  query_info.query = doc[kDDLJsonQuery].GetString();

  query_info.schema =
      HAS_MEMBER_OF_TYPE(doc, kDDLJsonSchema, IsString) ? doc[kDDLJsonSchema].GetString() : "";
  query_info.user =
      HAS_MEMBER_OF_TYPE(doc, kDDLJsonUser, IsString) ? doc[kDDLJsonUser].GetString() : "";

  query_info.is_manual_execution = HAS_MEMBER_OF_TYPE(doc, kDDLJsonManualReplication, IsBool);

  rapidjson::StringBuffer assignment_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(assignment_buffer);
  writer.StartObject();
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonEnumLabelInfo, IsArray)) {
    writer.Key(kDDLJsonEnumLabelInfo);
    doc[kDDLJsonEnumLabelInfo].Accept(writer);
  }
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonTypeInfo, IsArray)) {
    writer.Key(kDDLJsonTypeInfo);
    doc[kDDLJsonTypeInfo].Accept(writer);
  }
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonSequenceInfo, IsArray)) {
    writer.Key(kDDLJsonSequenceInfo);
    doc[kDDLJsonSequenceInfo].Accept(writer);
  }
  writer.EndObject();
  query_info.json_for_oid_assignment = assignment_buffer.GetString();

  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonNewRelMap, IsArray)) {
    for (const auto& rel : doc[kDDLJsonNewRelMap].GetArray()) {
      VALIDATE_MEMBER(rel, kDDLJsonRelFileOid, Uint);
      VALIDATE_MEMBER(rel, kDDLJsonRelName, String);
      XClusterDDLQueryInfo::RelationInfo rel_info;
      rel_info.relfile_oid = rel[kDDLJsonRelFileOid].GetUint();
      rel_info.relation_name = rel[kDDLJsonRelName].GetString();
      if (rel.HasMember(kDDLJsonRelPgSchemaName)) {
        VALIDATE_MEMBER(rel, kDDLJsonRelPgSchemaName, String);
        rel_info.relation_pgschema_name = rel[kDDLJsonRelPgSchemaName].GetString();
      } else {
        rel_info.relation_pgschema_name = query_info.schema;
      }
      rel_info.is_index =
          HAS_MEMBER_OF_TYPE(rel, kDDLJsonIsIndex, IsBool) ? rel[kDDLJsonIsIndex].GetBool() : false;
      rel_info.colocation_id = HAS_MEMBER_OF_TYPE(rel, kDDLJsonColocationId, IsUint)
                                   ? rel[kDDLJsonColocationId].GetUint()
                                   : kColocationIdNotSet;

      query_info.relation_map.push_back(std::move(rel_info));
    }
  }
  if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonVariableMap, IsObject)) {
    auto variables = doc[kDDLJsonVariableMap].GetObject();
    for (const auto& variable : variables) {
      auto name = variable.name.GetString();
      auto value = variable.value.GetString();
      query_info.variables[name] = value;
    }
  }

  return query_info;
}

}  // namespace

XClusterDDLQueueHandler::XClusterDDLQueueHandler(
    client::YBClient* local_client, const NamespaceName& namespace_name,
    const NamespaceId& source_namespace_id, const NamespaceId& target_namespace_id,
    const std::string& log_prefix, TserverXClusterContextIf& xcluster_context,
    ConnectToPostgresFunc connect_to_pg_func, UpdateSafeTimeFunc update_safe_time_func)
    : local_client_(local_client),
      namespace_name_(namespace_name),
      source_namespace_id_(source_namespace_id),
      target_namespace_id_(target_namespace_id),
      log_prefix_(log_prefix),
      xcluster_context_(xcluster_context),
      connect_to_pg_func_(std::move(connect_to_pg_func)),
      update_safe_time_func_(std::move(update_safe_time_func)) {}

XClusterDDLQueueHandler::~XClusterDDLQueueHandler() {}

void XClusterDDLQueueHandler::Shutdown() {
  if (pg_conn_ && FLAGS_ysql_yb_enable_advisory_locks &&
      FLAGS_xcluster_ddl_queue_advisory_lock_key != 0) {
    // Optimistically unlock the advisory lock so we don't have to wait for the connection to close.
    auto s = pg_conn_->Execute(Format("SELECT pg_advisory_unlock_all()"));
    // Alright if we fail here, log an error and wait for the connection to close normally.
    WARN_NOT_OK(s, "Encountered error unlocking advisory lock for xCluster DDL queue handler");
    pg_conn_.reset();
  }
}

Status XClusterDDLQueueHandler::ExecuteCommittedDDLs() {
  SCHECK(safe_time_batch_, InternalError, "Safe time batch is not initialized");
  if (!safe_time_batch_->IsComplete()) {
    // Either:
    // 1. No new records to process, so return normally and let the poller update the safe time.
    // 2. We have new records to process, but no safe time.
    //    We need to wait until we get a safe apply time before processing this batch.
    return Status::OK();
  }

  SCHECK(
      !FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start, InternalError,
      "Failing due to xcluster_ddl_queue_handler_fail_at_start");

  // Wait for all other pollers to have gotten to this safe time.
  const auto& apply_safe_time = safe_time_batch_->apply_safe_time;

  // TODO(#20928): Make these calls async.
  HybridTime safe_time_ht = VERIFY_RESULT(GetXClusterSafeTimeForNamespace());
  SCHECK(
      !safe_time_ht.is_special(), InternalError, "Found invalid safe time $0 for namespace $1",
      safe_time_ht, target_namespace_id_);
  SCHECK_GE(
      safe_time_ht, apply_safe_time, TryAgain,
      "Waiting for other pollers to catch up to safe time");

  HybridTime last_commit_time_processed = safe_time_batch_->last_commit_time_processed;
  // For each commit time in order, we read the ddl_queue table and process the entries at that
  // time. This ensures that we process all of the DDLs in commit order. We use the ddl_end_time to
  // break ties in the case of multiple DDLs in a single transaction.
  for (const auto& commit_time : safe_time_batch_->commit_times) {
    if (commit_time > apply_safe_time) {
      // Ignore commit times that are greater than the apply safe time. These remaining commit times
      // will be moved to the next batch.
      break;
    }

    // TODO(Transactional DDLs): Could detect these here and run them in a transaction.
    // TODO(#20928): Make these calls async.
    for (const auto& query_info : VERIFY_RESULT(GetQueriesToProcess(commit_time))) {
      if (query_info.is_manual_execution) {
        // Just add to the replicated_ddls table.
        RETURN_NOT_OK(ProcessManualExecutionQuery(query_info));
        continue;
      }

      VLOG_WITH_PREFIX(1) << "ExecuteCommittedDDLs: Processing entry " << query_info.ToString();

      SCHECK(
          kSupportedCommandTags.contains(query_info.command_tag), InvalidArgument,
          "Found unsupported command tag $0", query_info.command_tag);

      std::unordered_set<YsqlFullTableName> new_relations;
      auto se = ScopeExit([this, &new_relations]() {
        // Ensure that we always clear the xcluster_context.
        for (const auto& new_rel : new_relations) {
          xcluster_context_.ClearSourceTableInfoMappingForCreateTable(new_rel);
        }
      });

      RETURN_NOT_OK(ProcessNewRelations(query_info, new_relations, commit_time));
      RETURN_NOT_OK(ProcessDDLQuery(query_info));

      TEST_SYNC_POINT("XClusterDDLQueueHandler::DDLQueryProcessed");

      VLOG_WITH_PREFIX(2) << "ExecuteCommittedDDLs: Successfully processed entry "
                          << query_info.ToString();
    }
    last_commit_time_processed = commit_time;

    SCHECK(
        !FLAGS_TEST_xcluster_ddl_queue_handler_fail_before_incremental_safe_time_bump,
        InternalError,
        "Failing due to xcluster_ddl_queue_handler_fail_before_incremental_safe_time_bump");

    // After running all the DDLs for this commit time, we can update the safe time to this point.
    // Note that this needs to be propagated out to master + tservers, so there is some delay. This
    // means that currently we can have some transient holes where DDLs are visible before they
    // should be.
    // Eg. Table rewrite, we create the new table and backfill the data, but the safe time is still
    // behind. Until the safe time gets updated, we would read the new table, but it would appear
    // empty.
    // Eg. Drop column, while safe time is still behind, we would be using the new schema (without
    // the column) for reads at a time that the column should still exist.
    //
    // TODO(#27071) Add extra fencing to ensure that we don't read the new schema until the safe
    // time is caught up.
    VLOG_WITH_PREFIX(1) << "ExecuteCommittedDDLs: Bumping safe time to " << commit_time;
    update_safe_time_func_(commit_time);
    TEST_SYNC_POINT("XClusterDDLQueueHandler::DdlQueueSafeTimeBumped");
  }

  SCHECK(
      !FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end, InternalError,
      "Failing due to xcluster_ddl_queue_handler_fail_at_end");

  // Update/Clear the cached and persisted times for the next batch.
  // If we fail at any point before this, the next poller will first reprocess this batch before
  // calling GetChanges.
  RETURN_NOT_OK(UpdateSafeTimeBatchAfterProcessing(last_commit_time_processed));

  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessNewRelations(
    const XClusterDDLQueryInfo& query_info, std::unordered_set<YsqlFullTableName>& new_relations,
    const HybridTime& commit_time) {
  const auto source_db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(source_namespace_id_));
  // If there are new relations, need to update the context with the table name -> source
  // table id mapping. This will be passed to CreateTable and will be used in
  // add_table_to_xcluster_target task to find the matching source table.
  for (const auto& rel : query_info.relation_map) {
    // Only need to set the backfill time for indexes.
    const auto& backfill_time_opt = rel.is_index ? commit_time : HybridTime::kInvalid;

    RETURN_NOT_OK(xcluster_context_.SetSourceTableInfoMappingForCreateTable(
        {namespace_name_, rel.relation_pgschema_name, rel.relation_name},
        PgObjectId(source_db_oid, rel.relfile_oid), rel.colocation_id, backfill_time_opt));
    new_relations.insert({namespace_name_, query_info.schema, rel.relation_name});
  }

  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessDDLQuery(const XClusterDDLQueryInfo& query_info) {
  std::stringstream setup_query;
  setup_query << "SET ROLE NONE;";

  // Pass information needed to assign OIDs that need to be preserved across the universes.
  setup_query << Format(
      "SELECT pg_catalog.yb_xcluster_set_next_oid_assignments('$0');",
      std::regex_replace(query_info.json_for_oid_assignment, std::regex("'"), "''"));

  // Set session variables in order to pass the key to the replicated_ddls table.
  setup_query << Format("SET $0 TO $1;", kLocalVariableDDLEndTime, query_info.ddl_end_time);
  setup_query << Format("SET $0 TO $1;", kLocalVariableQueryId, query_info.query_id);

  // Set schema and role after setting the superuser extension variables.
  if (!query_info.schema.empty()) {
    setup_query << Format("SET SCHEMA '$0';", query_info.schema);
  }
  if (!query_info.user.empty()) {
    setup_query << Format("SET ROLE $0;", query_info.user);
  }

  // Set needed session variables as on source.
  for (const auto& [name, value] : query_info.variables) {
    auto escaped_value = std::regex_replace(value, std::regex("'"), "''");
    setup_query << Format("SET $0 = '$1';", name, escaped_value);
  }

  if (FLAGS_TEST_xcluster_ddl_queue_handler_fail_ddl) {
    setup_query << "SET yb_test_fail_next_ddl TO true;";
  }

  setup_query << Format(
      "SET statement_timeout TO $0;", FLAGS_xcluster_ddl_queue_statement_timeout_ms);

  RETURN_NOT_OK(RunAndLogQuery(setup_query.str()));
  RETURN_NOT_OK(ProcessFailedDDLQuery(RunAndLogQuery(query_info.query), query_info));
  RETURN_NOT_OK(
      // The SELECT here can't be last; otherwise, RunAndLogQuery complains that rows are returned.
      RunAndLogQuery(
          "SELECT pg_catalog.yb_xcluster_set_next_oid_assignments('{}');SET ROLE NONE;"));
  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessFailedDDLQuery(
    const Status& s, const XClusterDDLQueryInfo& query_info) {
  if (s.ok()) {
    num_fails_for_this_ddl_ = 0;
    last_failed_query_.reset();
    return Status::OK();
  }

  LOG_WITH_PREFIX(WARNING) << "Error when running DDL: " << s
                           << (FLAGS_TEST_xcluster_ddl_queue_handler_log_queries
                                   ? Format(". Query: $0", query_info.ToString())
                                   : "");

  DCHECK(!last_failed_query_ || last_failed_query_->MatchesQueryInfo(query_info));
  if (last_failed_query_ && last_failed_query_->MatchesQueryInfo(query_info)) {
    num_fails_for_this_ddl_++;
    if (num_fails_for_this_ddl_ >= FLAGS_xcluster_ddl_queue_max_retries_per_ddl) {
      LOG_WITH_PREFIX(WARNING) << "Failed to process DDL after " << num_fails_for_this_ddl_
                               << " retries. Pausing DDL replication.";
    }
  } else {
    last_failed_query_ = QueryIdentifier{query_info.ddl_end_time, query_info.query_id};
    num_fails_for_this_ddl_ = 1;
  }

  last_failed_status_ = s;
  return s;
}

Status XClusterDDLQueueHandler::CheckForFailedQuery() {
  if (num_fails_for_this_ddl_ >= FLAGS_xcluster_ddl_queue_max_retries_per_ddl) {
    return last_failed_status_.CloneAndPrepend(
        "DDL replication is paused due to repeated failures. Manual fix is required, followed by a "
        "leader stepdown of the target's ddl_queue tablet. ");
  }
  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessManualExecutionQuery(
    const XClusterDDLQueryInfo& query_info) {
  rapidjson::Document doc;
  doc.SetObject();
  doc.AddMember(
      rapidjson::StringRef(kDDLJsonQuery),
      rapidjson::Value(query_info.query.c_str(), doc.GetAllocator()), doc.GetAllocator());
  doc.AddMember(rapidjson::StringRef(kDDLJsonManualReplication), true, doc.GetAllocator());

  RETURN_NOT_OK(RunAndLogQuery(Format(
      "EXECUTE $0($1, $2, $4$3$4)", kDDLPrepStmtManualInsert, query_info.ddl_end_time,
      query_info.query_id, common::WriteRapidJsonToString(doc), "$manual_query$")));
  return Status::OK();
}

Status XClusterDDLQueueHandler::RunAndLogQuery(const std::string& query) {
  LOG_IF_WITH_PREFIX(INFO, FLAGS_TEST_xcluster_ddl_queue_handler_log_queries)
      << "XClusterDDLQueueHandler: Running query: " << query;
  return pg_conn_->Execute(query);
}

Result<bool> XClusterDDLQueueHandler::IsAlreadyProcessed(const XClusterDDLQueryInfo& query_info) {
  return pg_conn_->FetchRow<bool>(Format(
      "EXECUTE $0($1, $2)", kDDLPrepStmtAlreadyProcessed, query_info.ddl_end_time,
      query_info.query_id));
}

Status XClusterDDLQueueHandler::RunDdlQueueHandlerPrepareQueries(pgwrapper::PGConn* pg_conn) {
  std::stringstream query;
  // Read with tablet level consistency so that we see all the latest records.
  query << "SET yb_xcluster_consistency_level = tablet;";
  // Allow writes on target (read-only) cluster.
  query << "SET yb_non_ddl_txn_for_sys_tables_allowed = 1;";
  // Skip any data loads on the target since those records will be replicated (note that concurrent
  // index backfill uses a different flow). Skip sequence restart for TRUNCATE TABLE.
  query << "SET yb_xcluster_automatic_mode_target_ddl=true;";
  // Prepare replicated_ddls insert for manually replicated ddls.
  query << "PREPARE " << kDDLPrepStmtManualInsert << "(bigint, bigint, text) AS "
        << "INSERT INTO " << kReplicatedDDLsFullTableName << " VALUES ($1, $2, $3::jsonb);";
  // Prepare replicated_ddls select query.
  query << "PREPARE " << kDDLPrepStmtAlreadyProcessed << "(bigint, bigint) AS "
        << "SELECT EXISTS(SELECT 1 FROM " << kReplicatedDDLsFullTableName << " WHERE "
        << xcluster::kDDLQueueDDLEndTimeColumn << " = $1 AND " << xcluster::kDDLQueueQueryIdColumn
        << " = $2);";

  // Prepare commands for reading/upserting/deleting the special row in replicated_ddls.
  query << Format(
      "PREPARE $0 AS SELECT $1 FROM $2 WHERE $3 = $5 AND $4 = $5;", kDDLPrepStmtCommitTimesSelect,
      xcluster::kDDLQueueYbDataColumn, kReplicatedDDLsFullTableName,
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      kDDLReplicatedTableSpecialKey);
  query << Format(
      "PREPARE $0(text) AS INSERT INTO $1 VALUES ($2, $2, $$1::jsonb) "
      "ON CONFLICT ($3, $4) DO UPDATE SET $5 = EXCLUDED.$5;",  // Replace yb_data with new values.
      kDDLPrepStmtCommitTimesUpsert, kReplicatedDDLsFullTableName, kDDLReplicatedTableSpecialKey,
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn);

  return pg_conn->Execute(query.str());
}

Status XClusterDDLQueueHandler::InitPGConnection() {
  if (!FLAGS_TEST_xcluster_ddl_queue_handler_cache_connection) {
    pg_conn_.reset();
  }
  if (pg_conn_) {
    return Status::OK();
  }

  // Create pg connection if it doesn't exist.
  CoarseTimePoint deadline = CoarseMonoClock::Now() + local_client_->default_rpc_timeout();
  auto pg_conn = std::make_unique<pgwrapper::PGConn>(
      VERIFY_RESULT(connect_to_pg_func_(namespace_name_, deadline)));

  if (FLAGS_ysql_yb_enable_advisory_locks && FLAGS_xcluster_ddl_queue_advisory_lock_key != 0) {
    // Acquire advisory lock to ensure only one handler processes the DDL queue at a time.
    // Use non-blocking try_advisory_lock to fail fast if another handler is already running.
    auto lock_acquired = VERIFY_RESULT(pg_conn->FetchRow<bool>(
        Format("SELECT pg_try_advisory_lock($0)", FLAGS_xcluster_ddl_queue_advisory_lock_key)));
    SCHECK(lock_acquired, IllegalState, "Failed to acquire advisory lock for DDL queue handler.");
    TEST_SYNC_POINT("XClusterDDLQueueHandler::AdvisoryLockAcquired");
  }

  RETURN_NOT_OK(RunDdlQueueHandlerPrepareQueries(pg_conn.get()));

  pg_conn_ = std::move(pg_conn);
  return Status::OK();
}

Result<HybridTime> XClusterDDLQueueHandler::GetXClusterSafeTimeForNamespace() {
  return local_client_->GetXClusterSafeTimeForNamespace(
      target_namespace_id_, master::XClusterSafeTimeFilter::DDL_QUEUE);
}

// Fetch all DDL entries from ddl_queue at the specified commit_time that have not yet been
// processed and inserted into replicated_ddls. Since replicated_ddls is updated when a DDL is
// executed on the target, and this query is read at commit_time, some rows may still appear
// even if they have already been processed. Therefore, IsAlreadyProcessed must be called
// for each row to ensure correctness.
Result<std::vector<std::tuple<int64, int64, std::string>>>
XClusterDDLQueueHandler::GetRowsToProcess(const HybridTime& commit_time) {
  // Since applies can come out of order, need to read at the apply_safe_time and not latest.
  // Use yb_disable_catalog_version_check since we do not need to read from the latest catalog (the
  // extension tables should not change).
  RETURN_NOT_OK(pg_conn_->ExecuteFormat(
      "SET ROLE NONE; SET yb_disable_catalog_version_check = 1; SET yb_read_time TO '$0 ht'",
      commit_time.ToUint64()));
  auto rows = VERIFY_RESULT((pg_conn_->FetchRows<int64_t, int64_t, std::string>(Format(
      "/*+ MergeJoin(q r) */ "
      "SELECT q.$0, q.$1, q.$2 FROM $3 AS q "
      "WHERE NOT EXISTS ("
      "  SELECT 1 "
      "  FROM $4 AS r "
      "  WHERE r.$0 = q.$0 AND r.$1 = q.$1 "
      ") "
      "ORDER BY q.$0 ASC;",
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn, kDDLQueueFullTableName, kReplicatedDDLsFullTableName))));
  // DDLs are blocked when yb_read_time is non-zero, so reset.
  RETURN_NOT_OK(
      pg_conn_->Execute("SET yb_read_time = 0; SET yb_disable_catalog_version_check = 0"));

  VLOG_WITH_FUNC(2) << "Fetched '" << yb::AsString(rows) << "', apply_safe_time "
                    << commit_time.ToString();
  return rows;
}

Result<std::vector<XClusterDDLQueryInfo>> XClusterDDLQueueHandler::GetQueriesToProcess(
    const HybridTime& commit_time) {
  std::vector<XClusterDDLQueryInfo> query_infos;
  auto rows = VERIFY_RESULT(GetRowsToProcess(commit_time));
  for (const auto& [ddl_end_time, query_id, raw_json_data] : rows) {
    auto query_info = VERIFY_RESULT(GetDDLQueryInfo(raw_json_data, ddl_end_time, query_id));

    if (!VERIFY_RESULT(IsAlreadyProcessed(query_info))) {
      VLOG_WITH_FUNC(2) << "Query to be processed: " << query_info.ToString();
      query_infos.push_back(std::move(query_info));
    }
  }

  return query_infos;
}

Result<xcluster::SafeTimeBatch> XClusterDDLQueueHandler::FetchSafeTimeBatchFromReplicatedDdls(
    pgwrapper::PGConn* pg_conn) {
  auto rows = VERIFY_RESULT(
      pg_conn->FetchRows<std::string>(Format("EXECUTE $0", kDDLPrepStmtCommitTimesSelect)));
  SCHECK_LE(rows.size(), 1UL, InternalError, "Found more than one row in replicated_ddls");
  if (rows.empty()) {
    return xcluster::SafeTimeBatch();
  }
  const auto& row = rows.front();
  VLOG_WITH_FUNC(2) << "Fetched safe time batch row: " << row;
  auto doc = VERIFY_RESULT(ParseSerializedJson(row));

  xcluster::SafeTimeBatch safe_time_batch;

  auto convert_to_ht = [](uint64_t ht_int64, const char* field_name) -> Result<HybridTime> {
    HybridTime ht(ht_int64);
    SCHECK(
        !ht.is_special(), InternalError, "Found invalid $0: $1 in safe_time_batch", field_name, ht);
    return ht;
  };

  if (doc.HasMember(kSafeTimeBatchCommitTimes)) {
    VALIDATE_MEMBER(doc, kSafeTimeBatchCommitTimes, Array);
    for (const auto& commit_time : doc[kSafeTimeBatchCommitTimes].GetArray()) {
      safe_time_batch.commit_times.insert(
          VERIFY_RESULT(convert_to_ht(commit_time.GetUint64(), "commit_time")));
    }
  }

  auto get_ht_if_found = [&doc, &convert_to_ht](const char* field_name) -> Result<HybridTime> {
    if (!doc.HasMember(field_name)) {
      return HybridTime::kInvalid;
    }
    VALIDATE_MEMBER(doc, field_name, Int64);
    return convert_to_ht(doc[field_name].GetInt64(), field_name);
  };

  safe_time_batch.apply_safe_time = VERIFY_RESULT(get_ht_if_found(kSafeTimeBatchApplySafeTime));
  safe_time_batch.last_commit_time_processed =
      VERIFY_RESULT(get_ht_if_found(kSafeTimeBatchLastCommitTimeProcessed));

  return safe_time_batch;
}

Status XClusterDDLQueueHandler::ReloadSafeTimeBatchFromTableIfRequired() {
  if (safe_time_batch_) {
    return Status::OK();
  }
  safe_time_batch_ = VERIFY_RESULT(FetchSafeTimeBatchFromReplicatedDdls(pg_conn_.get()));
  return Status::OK();
}

Status XClusterDDLQueueHandler::PersistAndUpdateSafeTimeBatch(
    const std::set<HybridTime>& new_commit_times, const HybridTime& apply_safe_time) {
  if (new_commit_times.empty() && !apply_safe_time.is_valid()) {
    // We have nothing new to store or process so return.
    return Status::OK();
  }

  if (safe_time_batch_->commit_times.empty() && new_commit_times.empty()) {
    // We received an apply safe time, but no commit times to process, so can skip.
    return Status::OK();
  }

  SCHECK(safe_time_batch_, InternalError, "Safe time batch is not initialized");

  auto new_safe_time_batch = *safe_time_batch_;
  // Combine the new commit times with the existing commit times.
  new_safe_time_batch.commit_times.insert(new_commit_times.begin(), new_commit_times.end());
  new_safe_time_batch.apply_safe_time = apply_safe_time;
  return DoPersistAndUpdateSafeTimeBatch(std::move(new_safe_time_batch));
}

Status XClusterDDLQueueHandler::DoPersistAndUpdateSafeTimeBatch(
    xcluster::SafeTimeBatch new_safe_time_batch) {
  if (new_safe_time_batch == safe_time_batch_) {
    // Nothing to update.
    return Status::OK();
  }

  // Construct the json to insert into replicated_ddls.
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key(kSafeTimeBatchCommitTimes);
  writer.StartArray();
  for (const auto& commit_time : new_safe_time_batch.commit_times) {
    writer.Uint64(commit_time.ToUint64());
  }
  writer.EndArray();

  if (new_safe_time_batch.apply_safe_time.is_valid()) {
    writer.Key(kSafeTimeBatchApplySafeTime);
    writer.Int64(new_safe_time_batch.apply_safe_time.ToUint64());
  }
  if (new_safe_time_batch.last_commit_time_processed.is_valid()) {
    writer.Key(kSafeTimeBatchLastCommitTimeProcessed);
    writer.Int64(new_safe_time_batch.last_commit_time_processed.ToUint64());
  }
  writer.EndObject();

  const auto json_str = buffer.GetString();
  VLOG_WITH_PREFIX(2) << "Persisting safe time batch: " << json_str;

  RETURN_NOT_OK(ResetSafeTimeBatchOnFailure(
      pg_conn_->ExecuteFormat("EXECUTE $0('$1')", kDDLPrepStmtCommitTimesUpsert, json_str)));

  safe_time_batch_ = std::move(new_safe_time_batch);

  return Status::OK();
}

namespace {
Status VerifyCompleteSafeTimeBatch(
    const xcluster::SafeTimeBatch& safe_time_batch, const std::set<HybridTime>& new_commit_times,
    const HybridTime& apply_safe_time) {
  RSTATUS_DCHECK_EQ(
      safe_time_batch.apply_safe_time, apply_safe_time, InternalError,
      "Found different apply safe times for the same batch");
  // Also verify that new_commit_times is a subset of the commit times we have already have.
  for (const auto& commit_time : new_commit_times) {
    RSTATUS_DCHECK(
        safe_time_batch.commit_times.find(commit_time) != safe_time_batch.commit_times.end(),
        InternalError,
        Format(
            "Found commit time $0 that is not in the current batch $1", commit_time.ToString(),
            safe_time_batch.ToString()));
  }
  return Status::OK();
}
}  // namespace

Status XClusterDDLQueueHandler::UpdateSafeTimeBatchAfterProcessing(
    const HybridTime& last_commit_time_processed) {
  RSTATUS_DCHECK(safe_time_batch_, InternalError, "Safe time batch is uninitialized");

  auto new_safe_time_batch = *safe_time_batch_;
  new_safe_time_batch.apply_safe_time = HybridTime::kInvalid;
  new_safe_time_batch.last_commit_time_processed = last_commit_time_processed;

  // It's possible that we have commit times larger than the apply safe time, we only need to carry
  // those forward to the next batch.
  if (new_safe_time_batch.last_commit_time_processed.is_valid()) {
    std::erase_if(
        new_safe_time_batch.commit_times, [&new_safe_time_batch](const HybridTime& commit_time) {
          return commit_time <= new_safe_time_batch.last_commit_time_processed;
        });
  }

  // Update the commit times and clear the apply safe time.
  RETURN_NOT_OK(DoPersistAndUpdateSafeTimeBatch(std::move(new_safe_time_batch)));
  return Status::OK();
}

Status XClusterDDLQueueHandler::ResetSafeTimeBatchOnFailure(const Status& s) {
  // If we fail to run a query for the safe_time_batch, then we need to reset the cached values.
  if (!s.ok()) {
    safe_time_batch_.reset();
  }
  return s;
}

Status XClusterDDLQueueHandler::ProcessPendingBatchIfExists() {
  // We will check replicated_ddls (1,1) row if there is an existing full batch.
  // If we have a complete batch, we _must_ process it before calling GetChanges (since otherwise we
  //   could get a different apply_safe_time and lose DDLs).
  // If we have an incomplete batch, we will load it now, but skip processing it until we get a new
  //   apply_safe_time (from a future GetChanges call).
  RETURN_NOT_OK(CheckForFailedQuery());

  RETURN_NOT_OK(InitPGConnection());
  RETURN_NOT_OK(ReloadSafeTimeBatchFromTableIfRequired());
  return ExecuteCommittedDDLs();
}

Status XClusterDDLQueueHandler::ProcessGetChangesResponse(
    const XClusterOutputClientResponse& response) {
  DCHECK(response.status.ok());

  RETURN_NOT_OK(CheckForFailedQuery());
  RETURN_NOT_OK(InitPGConnection());

  // Load/update safe_time_batch_.
  RETURN_NOT_OK(ReloadSafeTimeBatchFromTableIfRequired());

  HybridTime apply_safe_time;
  if (response.get_changes_response->has_safe_hybrid_time()) {
    apply_safe_time = HybridTime(response.get_changes_response->safe_hybrid_time());
    SCHECK(
        !apply_safe_time.is_special(), InternalError,
        "Found invalid apply safe time $0 for namespace $1", apply_safe_time, target_namespace_id_);
  } else {
    apply_safe_time = HybridTime::kInvalid;
  }

  if (safe_time_batch_->IsComplete()) {
    // We failed to run this batch (eg xCluster safe time has not yet caught up), so we're
    // processing this batch again. Verify that it matches with the GetChanges response.
    RETURN_NOT_OK(VerifyCompleteSafeTimeBatch(
        *safe_time_batch_, response.ddl_queue_commit_times, apply_safe_time));
    // No need to update/persist any info in this case since it is the same.
  } else {
    // In the beginning/middle of processing a batch.
    RETURN_NOT_OK(PersistAndUpdateSafeTimeBatch(response.ddl_queue_commit_times, apply_safe_time));
  }

  // If the batch is complete then this will process all of its DDLs.
  return ExecuteCommittedDDLs();
}

Status XClusterDDLQueueHandler::UpdateSafeTimeForPause() {
  RETURN_NOT_OK(InitPGConnection());
  RETURN_NOT_OK(ReloadSafeTimeBatchFromTableIfRequired());

  auto max_commit_time = safe_time_batch_->last_commit_time_processed;

  for (const auto& commit_time : safe_time_batch_->commit_times) {
    if (safe_time_batch_->apply_safe_time.is_valid() &&
        commit_time > safe_time_batch_->apply_safe_time) {
      // Ignore commit times that are greater than the apply safe time.
      break;
    }

    auto queries = VERIFY_RESULT(GetQueriesToProcess(commit_time));
    if (!queries.empty()) {
      // Unprocessed DDL found.
      break;
    }
    if (!max_commit_time.is_valid() || commit_time > max_commit_time) {
      max_commit_time = commit_time;
    }
  }

  if (!max_commit_time.is_valid()) {
    VLOG_WITH_PREFIX(1) << "UpdateSafeTimeForPause: No DDLs were ever processed.";
    return Status::OK();
  }

  // During a failover we first pause the replication and make sure a accurate xCluster safe time is
  // computed.
  // It is possible that in cases where there is no in-flight DDLs, the apply safe time that we set
  // in the past was higher than the last DDL commit time. This is safe since only the max time is
  // propagated and persisted on the safe time table. We just need to ensure this time is higher
  // than any executed DDL commit times, since DDLs cannot be rolled back in case of failovers.
  VLOG_WITH_PREFIX(1) << "UpdateSafeTimeForPause: Bumping safe time to " << max_commit_time;
  update_safe_time_func_(max_commit_time);
  return Status::OK();
}

}  // namespace yb::tserver
