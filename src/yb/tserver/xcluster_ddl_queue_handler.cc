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
const char* kDDLJsonRelFileOid = "relfile_oid";
const char* kDDLJsonColocationId = "colocation_id";
const char* kDDLJsonIsIndex = "is_index";
const char* kDDLJsonEnumLabelInfo = "enum_label_info";
const char* kDDLJsonTypeInfo = "type_info";
const char* kDDLJsonSequenceInfo = "sequence_info";
const char* kDDLJsonManualReplication = "manual_replication";
const char* kDDLPrepStmtManualInsert = "manual_replication_insert";
const char* kDDLPrepStmtAlreadyProcessed = "already_processed_row";
const char* kDDLPrepStmtCommitTimesUpsert = "commit_times_insert";
const char* kDDLPrepStmtCommitTimesSelect = "commit_times_select";
const char* kDDLPrepStmtCommitTimesDelete = "commit_times_delete";
const int kDDLReplicatedTableSpecialKey = 1;

const std::unordered_set<std::string> kSupportedCommandTags {
    // Relations
    "CREATE TABLE",
    "CREATE INDEX",
    "CREATE TYPE",
    "CREATE SEQUENCE",
    "DROP TABLE",
    "DROP INDEX",
    "DROP TYPE",
    "DROP SEQUENCE",
    "ALTER TABLE",
    "ALTER INDEX",
    "ALTER TYPE",
    "ALTER SEQUENCE",
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

Status XClusterDDLQueueHandler::ExecuteCommittedDDLs() {
  SCHECK(safe_time_batch_, InternalError, "Safe time batch is not initialized");
  if (!safe_time_batch_->IsComplete()) {
    // Either:
    // 1. No new records to process, so return normally and let the poller update the safe time.
    // 2. We have new records to process, but no safe time.
    //    We need to wait until we get a safe apply time before processing this batch.
    return Status::OK();
  }

  // Wait for all other pollers to have gotten to this safe time.
  const auto& target_safe_ht = safe_time_batch_->apply_safe_time;
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
      safe_time_ht, target_namespace_id_);
  SCHECK_GE(
      safe_time_ht, target_safe_ht, TryAgain, "Waiting for other pollers to catch up to safe time");

  // For each commit time in order, we read the ddl_queue table and process the entries at that
  // time. This ensures that we process all of the DDLs in commit order. We use the ddl_end_time to
  // break ties in the case of multiple DDLs in a single transaction.
  for (const auto& commit_time : safe_time_batch_->commit_times) {
    // TODO(#20928): Make these calls async.
    const auto new_safe_time = HybridTime(commit_time);
    auto rows = VERIFY_RESULT(GetRowsToProcess(new_safe_time));
    // TODO(Transactional DDLs): Could detect these here and run them in a transaction.
    for (const auto& [ddl_end_time, query_id, raw_json_data] : rows) {
      rapidjson::Document doc = VERIFY_RESULT(ParseSerializedJson(raw_json_data));
      const auto query_info = VERIFY_RESULT(GetDDLQueryInfo(doc, ddl_end_time, query_id));

      // Need to reverify replicated_ddls if this DDL has already been processed.
      if (VERIFY_RESULT(CheckIfAlreadyProcessed(query_info))) {
        continue;
      }

      if (HAS_MEMBER_OF_TYPE(doc, kDDLJsonManualReplication, IsBool)) {
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

      RETURN_NOT_OK(ProcessNewRelations(doc, query_info.schema, new_relations, target_safe_ht));
      RETURN_NOT_OK(ProcessDDLQuery(query_info));

      VLOG_WITH_PREFIX(2) << "ExecuteCommittedDDLs: Successfully processed entry "
                          << query_info.ToString();
    }

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
    SCHECK_LE(
        new_safe_time, target_safe_ht, InternalError,
        "Found safe time for DDL that is greater than target safe time");
    VLOG_WITH_PREFIX(1) << "ExecuteCommittedDDLs: Bumping safe time to " << new_safe_time;
    update_safe_time_func_(commit_time);
    TEST_SYNC_POINT("XClusterDDLQueueHandler::DdlQueueSafeTimeBumped");
  }

  SCHECK(
      !FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_end, InternalError,
      "Failing due to xcluster_ddl_queue_handler_fail_at_end");

  // Clear the cached and persisted times for the next batch.
  // If we fail at any point before this, the next poller will first reprocess this batch before
  // calling GetChanges.
  RETURN_NOT_OK(ClearSafeTimeBatch());

  return Status::OK();
}

Result<XClusterDDLQueueHandler::DDLQueryInfo> XClusterDDLQueueHandler::GetDDLQueryInfo(
    rapidjson::Document& doc, int64 ddl_end_time, int64 query_id) {
  DDLQueryInfo query_info;

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
  return query_info;
}

Status XClusterDDLQueueHandler::ProcessNewRelations(
    rapidjson::Document& doc, const std::string& schema,
    std::unordered_set<YsqlFullTableName>& new_relations, const HybridTime& target_safe_ht) {
  const auto source_db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(source_namespace_id_));
  // If there are new relations, need to update the context with the table name -> source
  // table id mapping. This will be passed to CreateTable and will be used in
  // add_table_to_xcluster_target task to find the matching source table.
  const auto& rel_map = HAS_MEMBER_OF_TYPE(doc, kDDLJsonNewRelMap, IsArray)
                            ? std::optional(doc[kDDLJsonNewRelMap].GetArray())
                            : std::nullopt;
  if (rel_map) {
    for (const auto& rel : *rel_map) {
      VALIDATE_MEMBER(rel, kDDLJsonRelFileOid, Uint);
      VALIDATE_MEMBER(rel, kDDLJsonRelName, String);
      const auto relfile_oid = rel[kDDLJsonRelFileOid].GetUint();
      const auto rel_name = rel[kDDLJsonRelName].GetString();

      // Only need to set the backfill time for colocated indexes.
      const auto is_index =
          HAS_MEMBER_OF_TYPE(rel, kDDLJsonIsIndex, IsBool) ? rel[kDDLJsonIsIndex].GetBool() : false;
      const auto colocation_id = HAS_MEMBER_OF_TYPE(rel, kDDLJsonColocationId, IsUint)
                                     ? rel[kDDLJsonColocationId].GetUint()
                                     : kColocationIdNotSet;
      const auto& backfill_time_opt = (is_index && colocation_id != kColocationIdNotSet)
                                          ? target_safe_ht
                                          : HybridTime::kInvalid;

      RETURN_NOT_OK(xcluster_context_.SetSourceTableInfoMappingForCreateTable(
          {namespace_name_, schema, rel_name}, PgObjectId(source_db_oid, relfile_oid),
          colocation_id, backfill_time_opt));
      new_relations.insert({namespace_name_, schema, rel_name});
    }
  }

  return Status::OK();
}

Status XClusterDDLQueueHandler::ProcessDDLQuery(const DDLQueryInfo& query_info) {
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
    const Status& s, const DDLQueryInfo& query_info) {
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

Status XClusterDDLQueueHandler::ProcessManualExecutionQuery(const DDLQueryInfo& query_info) {
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

Result<bool> XClusterDDLQueueHandler::CheckIfAlreadyProcessed(const DDLQueryInfo& query_info) {
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
  // index backfill uses a different flow).
  query << "SET yb_skip_data_insert_for_xcluster_target=true;";
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
      "ON CONFLICT ($3, $4) DO UPDATE SET $5 = EXCLUDED.$5;",  // Replace yb_datawith new values.
      kDDLPrepStmtCommitTimesUpsert, kReplicatedDDLsFullTableName, kDDLReplicatedTableSpecialKey,
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn);
  query << Format(
      "PREPARE $0 AS DELETE FROM $1 WHERE $2 = $4 AND $3 = $4;", kDDLPrepStmtCommitTimesDelete,
      kReplicatedDDLsFullTableName, xcluster::kDDLQueueDDLEndTimeColumn,
      xcluster::kDDLQueueQueryIdColumn, kDDLReplicatedTableSpecialKey);

  return pg_conn->Execute(query.str());
}

Status XClusterDDLQueueHandler::InitPGConnection() {
  if (pg_conn_ && FLAGS_TEST_xcluster_ddl_queue_handler_cache_connection) {
    return Status::OK();
  }

  // Create pg connection if it doesn't exist.
  CoarseTimePoint deadline = CoarseMonoClock::Now() + local_client_->default_rpc_timeout();
  auto pg_conn = std::make_unique<pgwrapper::PGConn>(
      VERIFY_RESULT(connect_to_pg_func_(namespace_name_, deadline)));

  RETURN_NOT_OK(RunDdlQueueHandlerPrepareQueries(pg_conn.get()));

  pg_conn_ = std::move(pg_conn);
  return Status::OK();
}

Result<HybridTime> XClusterDDLQueueHandler::GetXClusterSafeTimeForNamespace() {
  return local_client_->GetXClusterSafeTimeForNamespace(
      target_namespace_id_, master::XClusterSafeTimeFilter::DDL_QUEUE);
}

Result<std::vector<std::tuple<int64, int64, std::string>>>
XClusterDDLQueueHandler::GetRowsToProcess(const HybridTime& apply_safe_time) {
  // Since applies can come out of order, need to read at the apply_safe_time and not latest.
  // Use yb_disable_catalog_version_check since we do not need to read from the latest catalog (the
  // extension tables should not change).
  RETURN_NOT_OK(pg_conn_->ExecuteFormat(
      "SET ROLE NONE; SET yb_disable_catalog_version_check = 1; SET yb_read_time = $0",
      apply_safe_time.GetPhysicalValueMicros()));
  // Select all rows that are in ddl_queue but not in replicated_ddls.
  // Note that this is done at apply_safe_time and rows written to replicated_ddls are done at the
  // time the DDL is rerun, so this does not filter out all rows (see kDDLPrepStmtAlreadyProcessed).
  auto rows = VERIFY_RESULT((pg_conn_->FetchRows<int64_t, int64_t, std::string>(Format(
      "SELECT $0, $1, $2 FROM $3 "
      "WHERE ($0, $1) NOT IN (SELECT $0, $1 FROM $4) "
      "ORDER BY $0 ASC",
      xcluster::kDDLQueueDDLEndTimeColumn, xcluster::kDDLQueueQueryIdColumn,
      xcluster::kDDLQueueYbDataColumn, kDDLQueueFullTableName, kReplicatedDDLsFullTableName))));
  // DDLs are blocked when yb_read_time is non-zero, so reset.
  RETURN_NOT_OK(
      pg_conn_->Execute("SET yb_read_time = 0; SET yb_disable_catalog_version_check = 0"));
  return rows;
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
  auto doc = VERIFY_RESULT(ParseSerializedJson(row));

  // Fetch the commit times.
  std::set<HybridTime> commit_times;
  if (doc.HasMember("commit_times")) {
    VALIDATE_MEMBER(doc, "commit_times", Array);
    const auto& commit_times_array = doc["commit_times"].GetArray();
    for (const auto& commit_time : commit_times_array) {
      commit_times.insert(HybridTime(commit_time.GetUint64()));
    }
  }

  xcluster::SafeTimeBatch safe_time_batch;
  safe_time_batch.commit_times = std::move(commit_times);
  if (doc.HasMember("apply_safe_time")) {
    VALIDATE_MEMBER(doc, "apply_safe_time", Int64);
    safe_time_batch.apply_safe_time = HybridTime(doc["apply_safe_time"].GetInt64());
  }
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
    const std::set<HybridTime>& new_commit_times, int64_t apply_safe_time) {
  if (new_commit_times.empty() && apply_safe_time == 0) {
    // We have nothing new to store or process so return.
    return Status::OK();
  }

  SCHECK(safe_time_batch_, InternalError, "Safe time batch is not initialized");
  if (safe_time_batch_->IsComplete()) {
    // We failed to run this batch (eg xCluster safe time has not yet caught up), so process this
    // batch again. This should be processed before we call GetChanges again, so validate that the
    // apply_safe_time is the same.
    RSTATUS_DCHECK_EQ(
        safe_time_batch_->apply_safe_time, HybridTime::FromPB(apply_safe_time), InternalError,
        "Found different apply safe times for the same batch");

    // Also verify that new_commit_times is a subset of the commit times we have already have.
    for (const auto& commit_time : new_commit_times) {
      RSTATUS_DCHECK(
          safe_time_batch_->commit_times.find(commit_time) != safe_time_batch_->commit_times.end(),
          InternalError,
          Format("Found commit time $0 that is not in the current batch", commit_time.ToString()));
    }
    // No need to read/update anything from replicated_ddls.
    return Status::OK();
  }

  // Combine the new commit times with the existing commit times.
  std::set<HybridTime> all_commit_times = safe_time_batch_->commit_times;
  all_commit_times.insert(new_commit_times.begin(), new_commit_times.end());

  if (all_commit_times.empty()) {
    // Nothing to process for this apply safe time.
    return Status::OK();
  }

  // Construct the json to insert into replicated_ddls.
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("commit_times");
  writer.StartArray();
  for (const auto& commit_time : all_commit_times) {
    writer.Uint64(commit_time.ToUint64());
  }
  writer.EndArray();

  // Add the apply safe time as well if we have it - used for poller failures during failover.
  // This also marks the end of a complete batch.
  if (apply_safe_time != 0) {
    writer.Key("apply_safe_time");
    writer.Int64(apply_safe_time);
  }
  writer.EndObject();

  RETURN_NOT_OK(ResetSafeTimeBatchOnFailure(pg_conn_->ExecuteFormat(
      "EXECUTE $0('$1')", kDDLPrepStmtCommitTimesUpsert, buffer.GetString())));

  // Cache this batch of commit times.
  safe_time_batch_->commit_times = std::move(all_commit_times);
  if (apply_safe_time != 0) {
    safe_time_batch_->apply_safe_time = HybridTime::FromPB(apply_safe_time);
  }

  return Status::OK();
}

Status XClusterDDLQueueHandler::ClearSafeTimeBatch() {
  RETURN_NOT_OK(ResetSafeTimeBatchOnFailure(
      pg_conn_->ExecuteFormat("EXECUTE $0", kDDLPrepStmtCommitTimesDelete)));
  // Don't fully reset the optional field, just clear the fields so that Complete() is false (ie we
  // are ready to process the next batch).
  safe_time_batch_ = xcluster::SafeTimeBatch();
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
  RETURN_NOT_OK(PersistAndUpdateSafeTimeBatch(
      response.ddl_queue_commit_times, response.get_changes_response->safe_hybrid_time()));

  return ExecuteCommittedDDLs();
}

}  // namespace yb::tserver
