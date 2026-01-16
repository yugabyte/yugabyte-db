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

#include "yb/tserver/stateful_services/pg_auto_analyze_service.h"

#include <algorithm>
#include <chrono>
#include <ranges>

#include "yb/bfql/gen_opcodes.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/schema.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/ysql_utils.h"

#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"

#include "yb/server/server_common_flags.h"
#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/pg_util.h"
#include "yb/util/status.h"

#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

#include "yb/common/json_util.h"
#include "yb/common/jsonb.h"


DEFINE_RUNTIME_uint32(ysql_cluster_level_mutation_persist_interval_ms, 10000,
                      "Interval at which the reported node level table mutation counts are "
                      "persisted to the underlying YCQL table by the central auto analyze service");
DEFINE_RUNTIME_uint32(ysql_cluster_level_mutation_persist_rpc_timeout_ms, 10000,
                      "Timeout for rpcs involved in persisting mutations in the auto-analyze "
                      "table.");
DEFINE_RUNTIME_uint32(ysql_auto_analyze_threshold, 50,
                      "The minimum number of mutations needed to trigger an ANALYZE on a table.");
DEFINE_RUNTIME_double(ysql_auto_analyze_scale_factor, 0.1,
                      "A fraction of the table size to add to ysql_auto_analyze_threshold when "
                      "deciding whether to trigger an ANALYZE.");
DEFINE_RUNTIME_uint32(ysql_auto_analyze_batch_size, 10,
                      "The max number of tables the auto analyze service tries to analyze in a "
                      "single ANALYZE statement.");
DEFINE_test_flag(int32, simulate_analyze_deleted_table_secs, 0,
                 "Delay triggering analyze to create a scenairo where we need to fall back to "
                 "analyze each table separately because a table is deleted.");
DEFINE_test_flag(bool, sort_auto_analyze_target_table_ids, false,
                 "Sort the analyze target tables' ids to generate deterministic ANALYZE statements "
                 "for testing purpose.");

DECLARE_bool(ysql_enable_auto_analyze_service);
DEFINE_RUNTIME_double(ysql_auto_analyze_cooldown_per_table_scale_factor, 2,
                      "The per-table cooldown factor for the auto analyze service. "
                      "The auto analyze service will not trigger an ANALYZE on a table if "
                      "the last ANALYZE was less than "
                      "ysql_auto_analyze_cooldown_per_table_scale_factor * (t1 - t2) seconds "
                      "ago, where t1 and t2 are the timestamps of the last two ANALYZEs.");
DEFINE_RUNTIME_uint32(ysql_auto_analyze_max_cooldown_per_table,
                      60 * 60 * 24 * 1000 /* 24 hours */,
                      "The maximum cooldown time for the auto analyze service in milliseconds."
                      "The cooldown between analyzes will grow by "
                      "ysql_auto_analyze_cooldown_per_table_scale_factor for each consecutive "
                      "analyze until it passes this threshold, at which point "
                      "the cooldown will be clamped to ysql_auto_analyze_max_cooldown_per_table "
                      "and stay at that value forever. For example, if "
                      "ysql_auto_analyze_min_cooldown_per_table is 1000ms, "
                      "ysql_auto_analyze_max_cooldown_per_table is 10000ms, "
                      "and ysql_auto_analyze_cooldown_per_table_scale_factor is 2, "
                      "then the cooldowns will be 1s, 2s, 4s, 8s, 10s, 10s, 10s, ...");
DEFINE_RUNTIME_uint32(ysql_auto_analyze_min_cooldown_per_table,
                       10000,
                       "The minimum cooldown time in milliseconds for the auto analyze service "
                       "to trigger an ANALYZE on a table again after it has been analyzed.");
DEFINE_test_flag(uint64, ysql_auto_analyze_max_history_entries, 5,
                 "The maximum number of analyze history entries to keep for each table.");

using namespace std::chrono_literals;
using std::chrono::system_clock;

namespace yb {

namespace stateful_service {

rapidjson::Value AutoAnalyzeInfo::AnalyzeEvent::ToRapidJson(
    rapidjson::Document::AllocatorType& alloc) const {
  rapidjson::Value value(rapidjson::kObjectType);
  auto timestamp_us =
      std::chrono::duration_cast<std::chrono::microseconds>(timestamp.time_since_epoch());
  uint64_t timestamp_us_value = timestamp_us.count();
  uint64_t cooldown_us_value = cooldown.count();
  value.AddMember("timestamp", rapidjson::Value(timestamp_us_value), alloc);
  value.AddMember("cooldown", rapidjson::Value(cooldown_us_value), alloc);
  return value;
}

std::string AutoAnalyzeInfo::AnalyzeEvent::ToString() const {
  return Format(
      "{{timestamp: $0, cooldown: $1}}", timestamp.time_since_epoch().count(), cooldown.count());
}

std::string AutoAnalyzeInfo::ToString() const {
  return Format(
      "{mutations: $0, analyze_history: $1}", mutations, CollectionToString(analyze_history));
}

rapidjson::Document AutoAnalyzeInfo::ToRapidJson() const {
  rapidjson::Document doc;
  auto& alloc = doc.GetAllocator();
  doc.SetObject();
  rapidjson::Value history_value(rapidjson::kArrayType);
  for (const auto& event : analyze_history) {
    history_value.PushBack(event.ToRapidJson(alloc), alloc);
  }
  doc.AddMember(PgAutoAnalyzeService::kAnalyzeHistoryKey, history_value, alloc);
  return doc;
}

PgAutoAnalyzeService::PgAutoAnalyzeService(
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future,
    ConnectToPostgresFunc connect_to_pg_func)
    : StatefulRpcServiceBase(StatefulServiceKind::PG_AUTO_ANALYZE, metric_entity, client_future),
      client_future_(client_future), connect_to_pg_func_(connect_to_pg_func),
      refresh_name_cache_(false) {}

void PgAutoAnalyzeService::Activate() { LOG(INFO) << ServiceName() << " activated"; }

void PgAutoAnalyzeService::Deactivate() { LOG(INFO) << ServiceName() << " de-activated"; }

Status PgAutoAnalyzeService::FlushMutationsToServiceTable() {
  const auto& table_id_to_mutations_maps = pg_cluster_level_mutation_counter_.GetAndClear();
  if (table_id_to_mutations_maps.empty()) {
    VLOG(5) << "No more mutations";
    return Status::OK();
  }

  auto session = VERIFY_RESULT(GetYBSession(
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  auto* table = VERIFY_RESULT(GetServiceTable());

  // Increment mutation counters for tables
  const auto& schema = table->schema();
  auto mutations_col_id = schema.ColumnId(schema.FindColumn(master::kPgAutoAnalyzeMutations));

  VLOG(2) << "Apply mutations: " << AsString(table_id_to_mutations_maps);

  std::vector<client::YBOperationPtr> ops;
  for (const auto& [table_id, mutation_count] : table_id_to_mutations_maps) {
    // Add count if entry already exists
    const auto add_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* const update_req = add_op->mutable_request();
    QLAddStringHashValue(update_req, table_id);
    update_req->mutable_column_refs()->add_ids(mutations_col_id);
    QLColumnValuePB *col_pb = update_req->add_column_values();
    col_pb->set_column_id(mutations_col_id);
    QLBCallPB* bfcall_expr_pb = col_pb->mutable_expr()->mutable_bfcall();
    bfcall_expr_pb->set_opcode(std::to_underlying(bfql::BFOpcode::OPCODE_AddI64I64_80));
    QLExpressionPB* operand1 = bfcall_expr_pb->add_operands();
    QLExpressionPB* operand2 = bfcall_expr_pb->add_operands();
    operand1->set_column_id(mutations_col_id);
    operand2->mutable_value()->set_int64_value(mutation_count);
    update_req->mutable_if_expr()->mutable_condition()->set_op(::yb::QLOperator::QL_OP_EXISTS);
    VLOG(4) << "Increment table mutations - " << update_req->ShortDebugString();
    ops.push_back(std::move(add_op));

    // Insert the count if entry does not exist
    const auto insert_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const insert_req = insert_op->mutable_request();
    QLAddStringHashValue(insert_req, table_id);
    table->AddInt64ColumnValue(insert_req, master::kPgAutoAnalyzeMutations, mutation_count);
    insert_req->mutable_if_expr()->mutable_condition()->set_op(::yb::QLOperator::QL_OP_NOT_EXISTS);
    VLOG(4) << "Insert table entry if does not exist - " << insert_req->ShortDebugString();
    ops.push_back(std::move(insert_op));
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(ops), "Failed to aggregate mutations into auto analyze table");

  // TODO(auto-analyze, #19475): For mutations that surely weren't applied to the underlying table,
  // re-add to pg_cluster_level_mutation_counter_.
  return Status::OK();
}

uint32 PgAutoAnalyzeService::PeriodicTaskIntervalMs() const {
  return GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_interval_ms);
}

// TriggerAnalyze has the following steps:
// (1) Read from the underlying YCQL table to get all pairs of (table id, mutation count).
// (2) Get table id to YBTableName mapping using a ListTables rpc to yb-master.
// (3) For all tables, check their current reltuples in the table_tuple_count_ cache.
//     If absent, fetch it via a catalog read using a PG connection to the corresponding database.
// (4) Categorize tables based on the database to save on the number of PG connections
//     that are needed to trigger ANALYZE.
// (5) Connect to each database and run ANALYZE sequentially.
// (6) For successful ANALYZEs or for tables that don't exist, subtract the mutations used to
//     decide an ANALYZE from the mutations in the YCQL table.
Status PgAutoAnalyzeService::TriggerAnalyze() {
  if (IsYsqlMajorVersionUpgradeInProgress()) {
    YB_LOG_EVERY_N_SECS(INFO, 1800) << "Skipping auto analyze during YSQL major version upgrade";
    return Status::OK();
  }

  VLOG_WITH_FUNC(3);

  const auto analyze_timestamp = system_clock::now();

  auto table_id_to_info_maps = VERIFY_RESULT(ReadTableMutations());
  if (table_id_to_info_maps.empty()) {
    return Status::OK();
  }
  VLOG(1) << "table_id_to_info_maps: " << ToString(table_id_to_info_maps);

  RETURN_NOT_OK(GetTablePGSchemaAndName(table_id_to_info_maps));

  std::unordered_set<NamespaceId> deleted_databases;
  RETURN_NOT_OK(FetchUnknownReltuples(table_id_to_info_maps, deleted_databases));

  auto params = VERIFY_RESULT(GetAutoAnalyzeParams(table_id_to_info_maps, deleted_databases));

  auto namespace_id_to_analyze_target_tables
      = VERIFY_RESULT(DetermineTablesForAnalyze(table_id_to_info_maps, analyze_timestamp));

  auto [analyzed_tables, deleted_tables]
      = VERIFY_RESULT(DoAnalyzeOnCandidateTables(namespace_id_to_analyze_target_tables,
                                                 deleted_databases));

  RETURN_NOT_OK(UpdateTableMutationsAfterAnalyze(analyzed_tables, table_id_to_info_maps));

  table_id_to_info_maps = VERIFY_RESULT(UpdateAnalyzeHistory(
      analyzed_tables, std::move(table_id_to_info_maps), analyze_timestamp, params));

  RETURN_NOT_OK(FlushAnalyzeHistory(
      analyzed_tables, table_id_to_info_maps, analyze_timestamp));

  RETURN_NOT_OK(CleanUpDeletedTablesFromServiceTable(table_id_to_info_maps, deleted_tables,
                                                     deleted_databases));

  return Status::OK();
}

Result<std::vector<AutoAnalyzeInfo::AnalyzeEvent>> PgAutoAnalyzeService::ParseHistoryFromJsonb(
    const QLValuePB& value) {
  if (value.value_case() != QLValuePB::kJsonbValue) {
    return std::vector<AutoAnalyzeInfo::AnalyzeEvent>();
  }

  common::Jsonb jsonb;
  RETURN_NOT_OK(jsonb.FromQLValue(value));
  rapidjson::Document doc;
  RETURN_NOT_OK(jsonb.ToRapidJson(&doc));

  std::vector<AutoAnalyzeInfo::AnalyzeEvent> history;
  if (doc.HasMember(PgAutoAnalyzeService::kAnalyzeHistoryKey) &&
      doc[PgAutoAnalyzeService::kAnalyzeHistoryKey].IsArray()) {
    const auto& history_array = doc[PgAutoAnalyzeService::kAnalyzeHistoryKey];
    history.reserve(history_array.Size());
    for (const auto& event : history_array.GetArray()) {
      if (event.IsObject() && event.HasMember("timestamp") && event["timestamp"].IsInt64()) {
        AutoAnalyzeInfo::AnalyzeEvent analyze_event;
        analyze_event.timestamp = std::chrono::system_clock::time_point(
            std::chrono::microseconds(event["timestamp"].GetInt64()));
        if (event.HasMember("cooldown") && event["cooldown"].IsInt64()) {
          analyze_event.cooldown = std::chrono::microseconds(event["cooldown"].GetInt64());
        } else {
          analyze_event.cooldown = std::chrono::microseconds(0);
        }
        history.push_back(std::move(analyze_event));
      }
    }
  }
  std::sort(history.begin(), history.end(), [](const auto& a, const auto& b) {
    return a.timestamp < b.timestamp;
  });
  return history;
}

Result<AutoAnalyzeInfoMap> PgAutoAnalyzeService::ReadTableMutations() {
  VLOG_WITH_FUNC(3);
  AutoAnalyzeInfoMap table_id_to_info_maps;
  // Read from the underlying YCQL table to get all pairs of (table id, mutation count).
  auto session = VERIFY_RESULT(
      GetYBSession(GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  auto* table = VERIFY_RESULT(GetServiceTable());

  const client::YBqlReadOpPtr read_op = table->NewReadOp();
  auto* const read_req = read_op->mutable_request();
  table->AddColumns(
      {yb::master::kPgAutoAnalyzeTableId, yb::master::kPgAutoAnalyzeMutations,
       yb::master::kPgAutoAnalyzeLastAnalyzeInfo},
      read_req);
  VLOG(4) << "Read table mutations - " << read_req->ShortDebugString();

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(read_op), "Failed to read from auto analyze table");

  auto rowblock = ql::RowsResult(read_op.get()).GetRowBlock();
  auto& row_schema = rowblock->schema();
  auto table_id_idx = row_schema.find_column(master::kPgAutoAnalyzeTableId);
  auto mutations_idx = row_schema.find_column(master::kPgAutoAnalyzeMutations);
  auto analyze_history_idx = row_schema.find_column(master::kPgAutoAnalyzeLastAnalyzeInfo);
  for (const auto& row : rowblock->rows()) {
    TableId table_id = row.column(table_id_idx).string_value();
    int64_t mutations = row.column(mutations_idx).int64_value();
    AutoAnalyzeInfo info(mutations);

    auto parse_result = ParseHistoryFromJsonb(row.column(analyze_history_idx).value());
    if (!parse_result.ok()) {
      LOG(WARNING) << "Failed to parse analyze history for table " << table_id;
      info.analyze_history = {};
    } else {
      info.analyze_history = std::move(*parse_result);
    }

    VLOG(5) << "Table "
            << (table_id_to_name_.contains(table_id)
                    ? Format("$0[$1]", table_id_to_name_.find(table_id)->second, table_id)
                    : table_id)
            << " info: " << ToString(info);

    table_id_to_info_maps[table_id] = std::move(info);
  }

  return table_id_to_info_maps;
}

// TODO(auto-analyze, #22946): maybe do some optimizations to speed up getting PG schema name and
//                             relation name.
// Get tables' PG schema name and relation name.
Status PgAutoAnalyzeService::GetTablePGSchemaAndName(
    const AutoAnalyzeInfoMap& table_id_to_info_maps) {
  VLOG_WITH_FUNC(3);

  // Check if we have all mutated tables' names in cache.
  // If not, then we need to issue a ListTables RPC to retrieve tables' name.
  if (!refresh_name_cache_ &&
      std::all_of(
          table_id_to_info_maps.begin(), table_id_to_info_maps.end(),
          [this](auto& tableid_mutation_pair) {
            const auto& [table_id, info] = tableid_mutation_pair;
            auto result = this->table_id_to_name_.contains(table_id);
            VLOG_IF(1, !result)
                << "GetTablePGSchemaAndName: Refresh because missing: "
                << table_id;
            if (result) {
              result = info.mutations ==
                  (pg_class_id_mutations_.contains(table_id) ?
                   pg_class_id_mutations_.at(table_id).mutations : info.mutations);
              VLOG_IF(1, !result)
                  << "GetTablePGSchemaAndName: Refresh because pg_class modified "
                  << table_id;
            }
            return result;
          })) {
    VLOG(4) << "name cache has all mutated tables' name";
    return Status::OK();
  }

  VLOG_IF_WITH_FUNC(1, refresh_name_cache_) << "Refresh because of refresh_name_cache_";

  refresh_name_cache_ = false;
  // We don't have all mutated tables' name in cache, so we need to rebuild it.
  // We fetch the entire table list even if we need the info for just one table,
  // so it's simpler to clear the in-mem list and recreate it. This also helps GC old entries.
  table_id_to_name_.clear();
  namespace_id_to_name_.clear();
  pg_class_id_mutations_.clear();
  auto all_table_names
      = VERIFY_RESULT(client_future_.get()->ListTables("" /* filter */, false /* exclude_ysql */,
                                                       "" /* ysql_db_filter */,
                                                       client::SkipHidden::kTrue));
  for (auto& table_name : all_table_names) {
    if (table_id_to_info_maps.contains(table_name.table_id())) {
      table_id_to_name_[table_name.table_id()] = table_name;
      if (table_name.table_name() == "pg_class") {
        pg_class_id_mutations_.emplace(
            table_name.table_id(),
            table_id_to_info_maps.contains(table_name.table_id()) ?
            table_id_to_info_maps.at(table_name.table_id()) : AutoAnalyzeInfo(0));
      }
      if (!namespace_id_to_name_.contains(table_name.namespace_id())) {
        namespace_id_to_name_[table_name.namespace_id()] = table_name.namespace_name();
      }
    }
  }
  VLOG(1) << "Built table name cache: " << ToString(table_id_to_name_)
          << " and database name cache " << ToString(namespace_id_to_name_);

  return Status::OK();
}

// TODO(auto-analyze, #22938): fetch reltuples without using PG connections.
// For each table we don't know its number of tuples, we need to fetch its reltuples from
// pg_class catalog within the same database as this table.
Status PgAutoAnalyzeService::FetchUnknownReltuples(
    const AutoAnalyzeInfoMap& table_id_to_info_maps,
    std::unordered_set<NamespaceId>& deleted_databases) {
  VLOG_WITH_FUNC(3);
  std::unordered_map<NamespaceId, std::vector<std::pair<TableId, PgOid>>>
      namespace_id_to_tables_with_unknown_reltuples;
  // Clean up dead entries from table_tuple_count_.
  std::erase_if(table_tuple_count_, [&table_id_to_info_maps](const auto& kv) {
    return !table_id_to_info_maps.contains(kv.first);
  });
  // Gather tables with unknown reltuples.
  for (const auto& [table_id, mutations] : table_id_to_info_maps) {
    if (!table_id_to_name_.contains(table_id))
      continue;
    auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
    auto table_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
    if (!table_tuple_count_.contains(table_id)) {
      namespace_id_to_tables_with_unknown_reltuples[namespace_id].push_back(
          std::make_pair(table_id, table_oid));
    }
  }
  VLOG(1) << "namespace_id_to_tables_with_unknown_reltuples: "
          << ToString(namespace_id_to_tables_with_unknown_reltuples);
  for (const auto& [namespace_id, tables] : namespace_id_to_tables_with_unknown_reltuples) {
    // If the database is deleted. We need to clean up table entries belonging to
    // this database from the YCQL service table.
    // If the database is renamed, we need to refresh name cache.
    // In either case, we need to let auto analyze proceed to later steps.
    // In other cases, return the error status.
    bool is_deleted_or_renamed = false;
    auto conn_result = EstablishDBConnection(namespace_id, deleted_databases,
                                             &is_deleted_or_renamed);
    if (is_deleted_or_renamed) {
      VLOG(1) << "DB deleted or renamed " << namespace_id << ", skipping";
      continue;
    }
    if (!conn_result)
      return conn_result.status();
    auto& conn = *conn_result;
    for (const auto& [table_id, table_oid] : tables) {
      // In YB, after a table rewrite operation, table id is based on relfilenode instead of
      // table oid. Most of relations' initial relfilenode is equal to its oid, so try querying
      // reltuples using relfilenode first.
      // In case querying reltuples using relfilnode doesn't return any result, we need to query
      // using oid instead. Mapped catalogs have zero in their pg_class.relfilenode entries.
      auto is_fetched = VERIFY_RESULT(DoFetchReltuples(conn, table_id, table_oid, true));
      if (!is_fetched) {
        VERIFY_RESULT(DoFetchReltuples(conn, table_id, table_oid, false));
      }
    }
  }

  return Status::OK();
}

// ANALYZE is triggered for tables crossing their analyze thresholds.
Result<PgAutoAnalyzeService::NamespaceTablesMap> PgAutoAnalyzeService::DetermineTablesForAnalyze(
    const AutoAnalyzeInfoMap& table_id_to_info_maps,
    const std::chrono::system_clock::time_point& now) {
  VLOG_WITH_FUNC(3);
  NamespaceTablesMap namespace_id_to_analyze_target_tables;
  for (const auto& [table_id, table_info] : table_id_to_info_maps) {
    auto it = table_tuple_count_.find(table_id);
    if (it == table_tuple_count_.end()) {
      VLOG(1) << "Table not in table_tuple_count_, so skipping: " << table_id;
      continue;
    }
    if (!table_id_to_name_.contains(table_id)) {
      VLOG(1) << "Table not in table_id_to_name_, so skipping: " << table_id;
      continue;
    }
    double analyze_threshold = FLAGS_ysql_auto_analyze_threshold +
        FLAGS_ysql_auto_analyze_scale_factor * it->second;
    VLOG(2) << "table_id: " << table_id << ", analyze_threshold: " << analyze_threshold;

    std::chrono::microseconds cooldown{0};
    std::chrono::system_clock::time_point last_analyze_timestamp{};

    if (!table_info.analyze_history.empty()) {
      const auto& last = table_info.analyze_history.back();
      cooldown = last.cooldown;
      last_analyze_timestamp = last.timestamp;
    }

    const auto since_last_analyze = now - last_analyze_timestamp;

    VLOG(5) << "Current time: " << ToString(now)
            << "; last analyze time: " << ToString(last_analyze_timestamp)
            << "; cooldown from last analyze: " << ToString(cooldown)
            << "; time since last analyze: " << ToString(since_last_analyze);

    if (table_info.mutations >= analyze_threshold && since_last_analyze >= cooldown) {
      VLOG(2) << "Table with id " << table_id << " has " << table_info.mutations << " mutations "
              << " and reaches its analyze threshold " << analyze_threshold;
      auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
      namespace_id_to_analyze_target_tables[namespace_id].push_back(table_id);
    }
  }

  if (PREDICT_FALSE(FLAGS_TEST_sort_auto_analyze_target_table_ids)) {
    for(auto& [namespace_id, tables_to_analyze] : namespace_id_to_analyze_target_tables) {
      sort(tables_to_analyze.begin(), tables_to_analyze.end());
    }
  }

  VLOG(1) << "namespace_id_to_analyze_target_tables: "
          << AsString(namespace_id_to_analyze_target_tables);
  return namespace_id_to_analyze_target_tables;
}

// Trigger ANALYZE on tables database by database.
Result<std::pair<std::vector<TableId>, std::vector<TableId>>>
    PgAutoAnalyzeService::DoAnalyzeOnCandidateTables(
        const NamespaceTablesMap& namespace_id_to_analyze_target_tables,
        std::unordered_set<NamespaceId>& deleted_databases) {
  VLOG_WITH_FUNC(3);

  if (PREDICT_FALSE(FLAGS_TEST_simulate_analyze_deleted_table_secs > 0)) {
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_simulate_analyze_deleted_table_secs));
  }
  std::vector<TableId> analyzed_tables;
  std::vector<TableId> deleted_tables;
  for(const auto& [namespace_id, tables_to_analyze] : namespace_id_to_analyze_target_tables) {
    // If a connection setup fails, check if the database is renamed or deleted.
    // If the database is deleted. We need to clean up table entries belonging to
    // this database from the YCQL service table.
    // If the database is renamed, we need to refresh name cache so that tables in the renamed
    // database can be analyzed in the next iteration of TriggerAnalyze.
    const auto& dbname = namespace_id_to_name_[namespace_id];
    LOG(INFO) << "Trigger ANALYZE for tables within database: " << dbname;
    bool is_deleted_or_renamed = false;
    auto conn_result = EstablishDBConnection(namespace_id, deleted_databases,
                                             &is_deleted_or_renamed);
    // If a connection setup fails due to a deleted or renamed database,
    // then continue doing ANALYZEs on tables in other databases.
    if (is_deleted_or_renamed) {
      VLOG_WITH_FUNC(1) << "Deleted or renamed " << dbname << "/" << namespace_id << ", skipping";
      continue;
    }

    if (!conn_result) {
      VLOG_WITH_FUNC(1) << "Conn failed: " << conn_result.status();
      return conn_result.status();
    }
    auto& conn = *conn_result;

    auto disabled = VERIFY_RESULT(conn.FetchRow<std::string>("SHOW yb_disable_auto_analyze"));
    if (disabled == "on") {
      YB_LOG_EVERY_N_SECS(INFO, 30) << "Auto analyze is disabled on database " << dbname;
      continue;
    }

    auto s = conn.Execute("SET yb_use_internal_auto_analyze_service_conn=true");
    RETURN_NOT_OK(s);

    // Construct ANALYZE statement and RUN ANALYZE.
    // Try to analyze all tables in batches to minimize the number of catalog version increments.
    // More catalog version increments lead to a higher number of PG cache refreshes on all PG
    // backends which introduces a large overhead.
    // Once the incremental catalog cache refresh (#24498) is implemented, we can remove the
    // requirement to batch multiple tables in the one ANALYZE statement.
    // If an error occurs in a batched ANALYZE, then fall back to analyze each table separately.
    const std::string analyze_query = "ANALYZE ";
    std::vector<TableId> batched_tables;
    for (auto& table_id : tables_to_analyze) {
      batched_tables.push_back(table_id);
      // FLAGS_ysql_auto_analyze_batch_size == 0 has the effect of batching all tables
      // in one single ANALYZE statement.
      if (batched_tables.size() == FLAGS_ysql_auto_analyze_batch_size
          || table_id == tables_to_analyze.back()) {
        auto table_names = TableNamesForAnalyzeCmd(batched_tables);
        VLOG(1) << "In YSQL database: " << dbname
                <<  ", run ANALYZE statement for tables in batch: "
                << analyze_query << table_names;
        auto s = conn.Execute(analyze_query + table_names);
        if (s.ok()) {
          analyzed_tables.insert(analyzed_tables.end(), batched_tables.begin(),
                                 batched_tables.end());
        } else {
          VLOG(1) << "Fall back to analyze each table separately due to " << s;
          for (auto& table_id : batched_tables) {
            // Each time run ANALYZE for one table instead of runnning one ANALYZE for batch tables
            // to deal with the scenario where a table we are going to analyze is deleted by
            // a user before or during its ANALYZE is kicked off.
            auto table_name = TableNamesForAnalyzeCmd({ table_id });
            VLOG(1) << "In YSQL database: " << dbname <<  ", run ANALYZE statement: "
                    << analyze_query << table_name;
            auto s = conn.Execute(analyze_query + table_name);
            // Gracefully handle the error status to allow other ANALYZE statements to proceed.
            // A table might be deleted before or during its ANALYZE. Treat the deleted table
            // as analyzed to clean up its mutation count.
            // A table might be renamed. Clear our table name cache so that the renamed table
            // can be analyzed in the next iteration of TriggerAnalyze.
            if (!s.ok()) {
              const auto& str = s.ToString();
              if (str.find("does not exist") == std::string::npos) {
                // Don't directly return status if an error status is generated from running
                // analyze. Allow subsequent ANALYZEs to run.
                LOG(WARNING) << "In YSQL database: " << dbname <<  ", failed ANALYZE statement: "
                             << analyze_query << table_name << " with error: " << str;
              } else {
                // Check if the table is deleted or renamed.
                auto renamed = VERIFY_RESULT(conn.FetchRow<bool>(
                                  Format("SELECT EXISTS(SELECT 1 FROM pg_class WHERE oid = '$0')",
                                         VERIFY_RESULT(GetPgsqlTableOid(table_id)))));
                if (renamed) {
                  VLOG(1) << "Table " << table_name << " was renamed";
                  // Need to refresh name cache because the cached table name is outdated.
                  refresh_name_cache_ = true;
                } else {
                  // TODO: Fix this, else branch doesn't imply that the table was deleted.
                  VLOG(1) << "Table " << table_name << " was deleted";
                  // Need to remove deleted table entries from the YCQL service table.
                  deleted_tables.push_back(table_id);
                }
              }
            } else {
              analyzed_tables.push_back(table_id);
            }
          }
        }
        batched_tables.clear();
      }
    }
  }

  return make_pair(analyzed_tables, deleted_tables);
}

// Update the table mutations by subtracting the mutations count we fetched
// if ANALYZE succeeded or failed with "does not exist error".
// Do substraction instead of directly updating the mutation counts to zero because
// updating mutation counts to zero might cause us to lose some mutations collected
// during triggering ANALYZE.
// TODO(auto-analyze, #22883): Clean up entries from auto analyze YCQL table if
// mutations is 0 for a table for a long time to free up memory.
Status PgAutoAnalyzeService::UpdateTableMutationsAfterAnalyze(
    const std::vector<TableId>& tables,
    const AutoAnalyzeInfoMap& table_id_to_info_maps) {
  VLOG_WITH_FUNC(2) << "tables: " << AsString(tables);
  auto session = VERIFY_RESULT(GetYBSession(
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  auto* table = VERIFY_RESULT(GetServiceTable());
  const auto& schema = table->schema();
  auto mutations_col_id = schema.ColumnId(schema.FindColumn(master::kPgAutoAnalyzeMutations));

  std::vector<client::YBOperationPtr> ops;
  for (auto& table_id : tables) {
    const auto update_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* const update_req = update_op->mutable_request();
    QLAddStringHashValue(update_req, table_id);
    update_req->mutable_column_refs()->add_ids(mutations_col_id);
    QLColumnValuePB *col_pb = update_req->add_column_values();
    col_pb->set_column_id(mutations_col_id);
    QLBCallPB* bfcall_expr_pb = col_pb->mutable_expr()->mutable_bfcall();
    bfcall_expr_pb->set_opcode(std::to_underlying(bfql::BFOpcode::OPCODE_SubI64I64_85));
    QLExpressionPB* operand1 = bfcall_expr_pb->add_operands();
    QLExpressionPB* operand2 = bfcall_expr_pb->add_operands();
    operand1->set_column_id(mutations_col_id);
    auto it = table_id_to_info_maps.find(table_id);
    operand2->mutable_value()->set_int64_value(
        it == table_id_to_info_maps.end() ? 0 : it->second.mutations);
    ops.push_back(std::move(update_op));
    auto* const condition = update_req->mutable_if_expr()->mutable_condition();
    condition->set_op(QL_OP_EXISTS);

    // Erase the table we analyzed from table row count cache.
    table_tuple_count_.erase(table_id);
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(ops), "Failed to clean up mutations from auto analyze table");

  return Status::OK();
}

// Remove deleted table entries from the YCQL service table.
Status PgAutoAnalyzeService::CleanUpDeletedTablesFromServiceTable(
    const AutoAnalyzeInfoMap& table_id_to_info_maps,
    const std::vector<TableId>& deleted_tables,
    const std::unordered_set<NamespaceId>& deleted_databases) {
  VLOG_WITH_FUNC(3);

  std::vector<TableId> tables_of_deleted_databases;
  std::vector<TableId> tables_absent_in_name_cache;
  for (const auto& [table_id, mutations] : table_id_to_info_maps) {
    // table_id_to_name_ is a subset of table_id_to_info_maps
    if (!table_id_to_name_.contains(table_id)) {
      // Table with table_id doesn't exist, so remove its entry from the service table.
      tables_absent_in_name_cache.push_back(table_id);
    } else if (deleted_databases.contains(VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id)))) {
      // Tables of deleted databases, so remove its entry from the service table.
      tables_of_deleted_databases.push_back(table_id);
    }
  }

  VLOG_IF_WITH_FUNC(2, !deleted_tables.empty())
      << "Tables were deleted directly: " << AsString(deleted_tables);
  VLOG_IF_WITH_FUNC(2, !deleted_databases.empty())
      << "Databases were deleted: " << AsString(deleted_databases);
  VLOG_IF_WITH_FUNC(2, !tables_of_deleted_databases.empty())
      << "Tables were deleted due to the deleted databases: "
      << AsString(tables_of_deleted_databases);
  VLOG_IF_WITH_FUNC(2, !tables_absent_in_name_cache.empty())
      << "Tables that are absent in the name cache: " << AsString(tables_absent_in_name_cache);

  auto* table = VERIFY_RESULT(GetServiceTable());
  std::vector<client::YBOperationPtr> ops;
  for (auto& table_id : deleted_tables) {
    const auto delete_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    auto* const delete_req = delete_op->mutable_request();
    QLAddStringHashValue(delete_req, table_id);
    ops.push_back(delete_op);
  }
  for (auto& table_id : tables_of_deleted_databases) {
    const auto delete_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    auto* const delete_req = delete_op->mutable_request();
    QLAddStringHashValue(delete_req, table_id);
    ops.push_back(delete_op);
  }
  for (auto& table_id : tables_absent_in_name_cache) {
    const auto delete_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_DELETE);
    auto* const delete_req = delete_op->mutable_request();
    QLAddStringHashValue(delete_req, table_id);
    ops.push_back(delete_op);
  }

  auto session = VERIFY_RESULT(GetYBSession(
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(session->TEST_ApplyAndFlush(ops),
      "Failed to clean up deleted entries from auto analyze table");

  return Status::OK();
}

Result<pgwrapper::PGConn> PgAutoAnalyzeService::EstablishDBConnection(
    const NamespaceId& namespace_id, std::unordered_set<NamespaceId>& deleted_databases,
    bool* is_deleted_or_renamed) {
  // Connect to PG database.
  const auto& dbname = namespace_id_to_name_[namespace_id];
  auto conn_result = connect_to_pg_func_(dbname, std::nullopt);
  // If connection setup fails,  continue
  // doing ANALYZEs on tables in other databases.
  if (!conn_result) {
    // Check if the nonexistent database is renamed or deleted.
    bool namespace_exists =
        VERIFY_RESULT(client_future_.get()->NamespaceIdExists(namespace_id, YQL_DATABASE_PGSQL));
    if (!namespace_exists) { // deleted
      // The database is deleted. Need to clean up table entries belonging to
      // this database from the YCQL service table.
      VLOG(4) << "Database " << dbname << " was deleted";
      deleted_databases.insert(namespace_id);
      *is_deleted_or_renamed = true;
    } else {
      // If the database is renamed, we need to refresh name cache so that tables in the renamed
      // database can be analyzed in the next iteration of TriggerAnalyze.
      master::GetNamespaceInfoResponsePB resp;
      RETURN_NOT_OK(client_future_.get()->GetNamespaceInfo(namespace_id, &resp));
      if (resp.namespace_().name() != dbname) {  // renamed
        VLOG(4) << "Database " << dbname << " was renamed to " << resp.namespace_().name();
        refresh_name_cache_ = true;
        *is_deleted_or_renamed = true;
      }
    }
  }

  return conn_result;
}

// Return true if reltuples is fetched.
Result<bool> PgAutoAnalyzeService::DoFetchReltuples(pgwrapper::PGConn& conn, TableId table_id,
                                                    PgOid oid, bool use_relfilenode) {
  auto res =
    VERIFY_RESULT(conn.Fetch(Format("SELECT reltuples FROM pg_class WHERE $0 = $1",
                                    use_relfilenode ? "relfilenode" : "oid", oid)));
  if (PQntuples(res.get()) > 0) {
    float reltuples = VERIFY_RESULT(pgwrapper::GetValue<float>(res.get(), 0, 0));
    table_tuple_count_[table_id] = reltuples == -1 ? 0 : reltuples;
    VLOG(1) << "Table with id " << table_id << " has " << table_tuple_count_[table_id]
            << " reltuples";
    return true;
  }

  return false;
}

// Construct tables' names list.
std::string PgAutoAnalyzeService::TableNamesForAnalyzeCmd(const std::vector<TableId>& table_ids) {
  std::string table_names = "";
  for (auto& table_id : table_ids) {
    if (table_names != "")
      table_names += ", ";
    auto table_name =
        Format("\"$0\".\"$1\"",
               table_id_to_name_[table_id].has_pgschema_name() ?
               table_id_to_name_[table_id].pgschema_name() : "pg_catalog",
               table_id_to_name_[table_id].table_name());
    table_names += table_name;
  }

  return table_names;
}

Result<bool> PgAutoAnalyzeService::RunPeriodicTask() {
  if (FLAGS_ysql_enable_auto_analyze_service) {
    // Update the underlying YCQL service table that tracks cluster-wide mutations
    // for all YSQL tables.
    RETURN_NOT_OK(FlushMutationsToServiceTable());

    // Trigger ANALYZE for tables whose mutation counts have crossed their thresholds.
    RETURN_NOT_OK(TriggerAnalyze());
  }

  // Return true to re-trigger this periodic task after PeriodicTaskIntervalMs.
  return true;
}

Status PgAutoAnalyzeService::IncreaseMutationCountersImpl(
    const IncreaseMutationCountersRequestPB& req, IncreaseMutationCountersResponsePB* resp,
    rpc::RpcContext& rpc) {
  VLOG_WITH_FUNC(3) << "req=" << req.ShortDebugString();

  pg_cluster_level_mutation_counter_.IncreaseBatch(
      req.table_mutation_counts() | std::views::transform(
          [](const auto& entry) {
            return std::make_pair(std::cref(entry.table_id()), entry.mutation_count());
          }));

  return Status::OK();
}

//------------------------------------------------------------------------------
// PgAutoAnalyzeService helpers
//------------------------------------------------------------------------------
Result<AutoAnalyzeInfoMap> PgAutoAnalyzeService::UpdateAnalyzeHistory(
    const std::vector<TableId>& analyzed_tables, AutoAnalyzeInfoMap&& table_id_to_info_maps,
    const std::chrono::system_clock::time_point& now, const AutoAnalyzeParams& params) {
  for (const auto& table_id : analyzed_tables) {
    auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
    auto it = table_id_to_info_maps.find(table_id);
    if (it == table_id_to_info_maps.end()) {
      VLOG(5) << "Table " << table_id << " not found in mutations map";
      continue;
    }
    auto& table_info = it->second;
    std::chrono::microseconds cooldown;
    if (table_info.analyze_history.empty()) {
      std::chrono::milliseconds default_cooldown{params.GetMinCooldownPerTable(namespace_id)};
      cooldown = std::chrono::duration_cast<std::chrono::microseconds>(default_cooldown);
      VLOG(5) << "No analyze history found for table " << table_id << ", setting cooldown to "
              << ToString(cooldown);
    } else {
      auto prev_cooldown = table_info.analyze_history.back().cooldown;
      cooldown = std::chrono::duration_cast<std::chrono::microseconds>(
          prev_cooldown * params.GetCooldownScaleFactor(namespace_id));
    }

    auto max_cooldown = params.GetMaxCooldownPerTable(namespace_id);
    if (cooldown > max_cooldown) {
      cooldown = max_cooldown;
    }

    VLOG(5) << "Adding analyze event at " << ToString(now) << " for table " << table_id
            << " with cooldown " << ToString(cooldown);
    table_info.analyze_history.push_back(AutoAnalyzeInfo::AnalyzeEvent{now, cooldown});

    // Keep only the N most recent analyze history entries.
    if (table_info.analyze_history.size() > FLAGS_TEST_ysql_auto_analyze_max_history_entries) {
      table_info.analyze_history.erase(
          table_info.analyze_history.begin(),
          table_info.analyze_history.end() - FLAGS_TEST_ysql_auto_analyze_max_history_entries);
    }
  }

  return std::move(table_id_to_info_maps);
}

/*
 * Updates the analyze history for each table listed in the auto-analyze CQL table.
 * The history is a JSONB value with a "history" array of integers, where each integer is a
 * timestamp in microseconds.
 * Example input: {"history":[2512767964307,2512768509620]}
 * Example output: {"history":[2512767964307,2512768509620,2512768509621]}
 */
Status PgAutoAnalyzeService::FlushAnalyzeHistory(
    const std::vector<TableId>& tables, const AutoAnalyzeInfoMap& table_id_to_mutations_maps,
    const std::chrono::system_clock::time_point& now) {
  const auto rpc_timeout =
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms;

  auto session = VERIFY_RESULT(GetYBSession(rpc_timeout));
  auto* table = VERIFY_RESULT(GetServiceTable());

  const auto& schema = table->schema();
  const ColumnId history_column_id(
      schema.ColumnId(schema.FindColumn(master::kPgAutoAnalyzeLastAnalyzeInfo)));

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(tables.size());

  for (const auto& table_id : tables) {
    auto& mutations = table_id_to_mutations_maps.at(table_id);
    auto history_doc = mutations.ToRapidJson();

    // 3. Convert to Jsonb
    common::Jsonb jsonb;
    std::string json_string;
    RETURN_NOT_OK(jsonb.FromRapidJson(history_doc));
    if (VLOG_IS_ON(5)) {
      std::string json_string;
      RETURN_NOT_OK(jsonb.ToJsonString(&json_string));
      VLOG(5) << "Writing history for table " << table_id << ": " << json_string;
    }

    // 4. Build UPDATE ... WHERE table_id = ? IF EXISTS;
    auto update_op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* req = update_op->mutable_request();

    QLAddStringHashValue(req, table_id);  // PK
    req->mutable_column_refs()->add_ids(history_column_id);

    auto* col = req->add_column_values();
    col->set_column_id(history_column_id);
    col->mutable_expr()->mutable_value()->set_jsonb_value(jsonb.MoveSerializedJsonb());

    req->mutable_if_expr()->mutable_condition()->set_op(QL_OP_EXISTS);

    ops.push_back(std::move(update_op));
  }

  RETURN_NOT_OK_PREPEND(session->TEST_ApplyAndFlush(ops), "Failed to update auto-analyze history");
  return Status::OK();
}
Result<AutoAnalyzeParams> PgAutoAnalyzeService::GetAutoAnalyzeParams(
    const AutoAnalyzeInfoMap& table_id_to_info_maps,
    std::unordered_set<NamespaceId>& deleted_databases) {
  std::unordered_set<NamespaceId> namespaces_to_fetch;
  for (const auto& [table_id, mutations] : table_id_to_info_maps) {
    auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
    namespaces_to_fetch.insert(namespace_id);
  }

  AutoAnalyzeParams params;
  for (const auto& namespace_id : namespaces_to_fetch) {
    // For now, all DBs have the same params.
    params.SetDbParams(
        namespace_id,
        DbAutoAnalyzeParams{
            FLAGS_ysql_auto_analyze_cooldown_per_table_scale_factor,
            std::chrono::milliseconds(FLAGS_ysql_auto_analyze_max_cooldown_per_table),
            std::chrono::milliseconds(FLAGS_ysql_auto_analyze_min_cooldown_per_table)});
  }

  return params;
}

}  // namespace stateful_service
}  // namespace yb
