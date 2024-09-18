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

#include "yb/tserver/pg_mutation_counter.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/pg_util.h"
#include "yb/util/status.h"

#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

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
DECLARE_bool(ysql_enable_auto_analyze_service);

using namespace std::chrono_literals;

namespace yb {

namespace stateful_service {
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
    bfcall_expr_pb->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_AddI64I64_80));
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

// TODO(auto-analyze, #22883): Ignore and clean up entries from auto analyze YCQL table if
// the databases the entries corresponds to have been deleted.
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
  VLOG_WITH_FUNC(2);

  auto table_id_to_mutations_maps = VERIFY_RESULT(ReadTableMutations());

  RETURN_NOT_OK(GetTablePGSchemaAndName(table_id_to_mutations_maps));

  RETURN_NOT_OK(FetchUnknownReltuples(table_id_to_mutations_maps));

  auto dbname_to_analyze_target_tables
      = VERIFY_RESULT(DetermineTablesForAnalyze(table_id_to_mutations_maps));

  auto analyzed_tables = VERIFY_RESULT(DoAnalyzeOnCandidateTables(dbname_to_analyze_target_tables));

  RETURN_NOT_OK(UpdateTableMutationsAfterAnalyze(analyzed_tables, table_id_to_mutations_maps));

  return Status::OK();
}

Result<std::unordered_map<TableId, int64_t>> PgAutoAnalyzeService::ReadTableMutations() {
  VLOG_WITH_FUNC(2);
  std::unordered_map<TableId, int64_t> table_id_to_mutations_maps;
  // Read from the underlying YCQL table to get all pairs of (table id, mutation count).
  auto session = VERIFY_RESULT(GetYBSession(
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  auto* table = VERIFY_RESULT(GetServiceTable());

  const client::YBqlReadOpPtr read_op = table->NewReadOp();
  auto* const read_req = read_op->mutable_request();
  table->AddColumns(
    {yb::master::kPgAutoAnalyzeTableId, yb::master::kPgAutoAnalyzeMutations}, read_req);
  VLOG(4) << "Read table mutations - " << read_req->ShortDebugString();

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(read_op), "Failed to read from auto analyze table");

  auto rowblock = ql::RowsResult(read_op.get()).GetRowBlock();
  auto& row_schema = rowblock->schema();
  auto table_id_idx = row_schema.find_column(master::kPgAutoAnalyzeTableId);
  auto mutations_idx = row_schema.find_column(master::kPgAutoAnalyzeMutations);
  for (const auto& row : rowblock->rows()) {
    TableId table_id = row.column(table_id_idx).string_value();
    int64_t mutations = row.column(mutations_idx).int64_value();
    table_id_to_mutations_maps[table_id] = mutations;
    VLOG(5) << "Table " << table_id << " has mutations: " << mutations;
  }

  return table_id_to_mutations_maps;
}

// TODO(auto-analyze, #22946): maybe do some optimizations to speed up getting PG schema name and
//                             relation name.
// Get tables' PG schema name and relation name.
Status PgAutoAnalyzeService::GetTablePGSchemaAndName(
    std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps) {
  VLOG_WITH_FUNC(2);
  // Check if we have all mutated tables' names in cache.
  // If not, then we need to issue a ListTables RPC to retrieve tables' name.
  if (!refresh_name_cache_
      && std::all_of(table_id_to_mutations_maps.begin(), table_id_to_mutations_maps.end(),
                     [this](auto& tableid_mutation_pair) {
                       return this->table_id_to_name_.contains(tableid_mutation_pair.first);
                     })) {
    VLOG(4) << "name cache has all mutated tables' name";
    return Status::OK();
  }
  refresh_name_cache_ = false;
  // We don't have all mutated tables' name in cache, so we need to rebuild it.
  // We fetch the entire table list even if we need the info for just one table,
  // so it's simpler to clear the in-mem list and recreate it. This also helps GC old entries.
  table_id_to_name_.clear();
  namespace_id_to_name_.clear();
  auto all_table_names
      = VERIFY_RESULT(client_future_.get()->ListTables("" /* filter */, false /* exclude_ysql */,
                                                       "" /* ysql_db_filter */,
                                                       true /* skip_hidden */));
  for (auto& table_name : all_table_names) {
    if (table_id_to_mutations_maps.contains(table_name.table_id())) {
      table_id_to_name_[table_name.table_id()] = table_name;
      if (!namespace_id_to_name_.contains(table_name.namespace_id())) {
        namespace_id_to_name_[table_name.namespace_id()] = table_name.namespace_name();
      }
    }
  }
  VLOG(5) << "Built table name cache: " << ToString(table_id_to_name_)
          << " and database name cache " << ToString(namespace_id_to_name_);

  return Status::OK();
}

// TODO(auto-analyze, #22938): fetch reltuples without using PG connections.
// For each table we don't know its number of tuples, we need to fetch its reltuples from
// pg_class catalog within the same database as this table.
Status PgAutoAnalyzeService::FetchUnknownReltuples(
    std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps) {
  VLOG_WITH_FUNC(2);
  std::unordered_map<NamespaceName, std::set<std::pair<TableId, PgOid>>>
      dbname_to_tables_with_unknown_reltuples;
  // Clean up dead entries from table_tuple_count_.
  std::erase_if(table_tuple_count_, [&table_id_to_mutations_maps](const auto& kv) {
    return !table_id_to_mutations_maps.contains(kv.first);
  });
  // Gather tables with unknown reltuples.
  for (const auto& [table_id, mutations] : table_id_to_mutations_maps) {
    auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
    auto table_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
    if (!table_tuple_count_.contains(table_id)) {
      dbname_to_tables_with_unknown_reltuples[namespace_id_to_name_[namespace_id]].insert(
          std::make_pair(table_id, table_oid));
    }
  }
  for (const auto& [dbname, tables] : dbname_to_tables_with_unknown_reltuples) {
    auto conn = VERIFY_RESULT(connect_to_pg_func_(dbname));
    for (const auto& [table_id, table_oid] : tables) {
      auto res =
        VERIFY_RESULT(conn.Fetch("SELECT reltuples FROM pg_class WHERE oid = "
                                  + std::to_string(table_oid)));
      table_tuple_count_[table_id] = VERIFY_RESULT(pgwrapper::GetValue<float>(res.get(), 0, 0));
      VLOG(4) << "Table with id " << table_id << " has " << table_tuple_count_[table_id]
              << " reltuples";
    }
  }

  return Status::OK();
}

// ANALYZE is triggered for tables crossing their analyze thresholds.
Result<std::unordered_map<NamespaceName, std::set<TableId>>>
    PgAutoAnalyzeService::DetermineTablesForAnalyze(
        std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps) {
  VLOG_WITH_FUNC(2);
  std::unordered_map<NamespaceName, std::set<TableId>>
      dbname_to_analyze_target_tables;
  for (auto& [table_id, mutations] : table_id_to_mutations_maps) {
    auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(table_id));
    double analyze_threshold = FLAGS_ysql_auto_analyze_threshold +
        FLAGS_ysql_auto_analyze_scale_factor * table_tuple_count_[table_id];
    if (mutations >= analyze_threshold) {
      VLOG(5) << "Table with id " << table_id << " has " << mutations << " mutations "
              << "and reaches its analyze threshold " << analyze_threshold;
      dbname_to_analyze_target_tables[namespace_id_to_name_[namespace_id]].insert(table_id);
    }
  }

  return dbname_to_analyze_target_tables;
}

// Trigger ANALYZE on tables database by database.
Result<std::vector<TableId>> PgAutoAnalyzeService::DoAnalyzeOnCandidateTables(
  std::unordered_map<NamespaceName, std::set<TableId>>& dbname_to_analyze_target_tables) {
  VLOG_WITH_FUNC(2);
  std::vector<TableId> analyzed_tables;
  for(const auto& [dbname, tables_to_analyze] : dbname_to_analyze_target_tables) {
    LOG(INFO) << "Trigger ANALYZE for tables within database: " << dbname;
    // Connect to PG database.
    auto conn_result = connect_to_pg_func_(dbname);
    // If connection setup fails,  continue
    // doing ANALYZEs on tables in other databases.
    if (!conn_result) {
      // Check if the nonexistent database is renamed or deleted.
      // If the database is renamed, we need to refresh name cache so that tables in the renamed
      // database can be analyzed in the next iteration of TriggerAnalyze.
      auto namespace_id = VERIFY_RESULT(GetNamespaceIdFromYsqlTableId(*tables_to_analyze.begin()));
      master::GetNamespaceInfoResponsePB resp;
      RETURN_NOT_OK(client_future_.get()->GetNamespaceInfo(namespace_id, "", YQL_DATABASE_PGSQL,
                                                           &resp));
      if (!resp.has_namespace_()) { // deleted
        // TODO(auto-analyze, #22883): a database is deleted. Clean up table entries belonging to
        // the database from the YCQL service table.
        continue;
      } else {
        if (resp.namespace_().name() != dbname) { // renamed
          VLOG(4) << "Database " << dbname << " was renamed to " << resp.namespace_().name();
          refresh_name_cache_ = true;
          continue;
        }
      }
      return conn_result.status();
    }
    auto& conn = *conn_result;
    // Construct ANALYZE statement and RUN ANALYZE.
    const std::string analyze_query = "ANALYZE ";
    for (auto& table_id : tables_to_analyze) {
      // Each time run ANALYZE for one table instead of runnning one ANALYZE for all tables in one
      // database to deal with the scenario where a table we are going to analyze is deleted by
      // a user before or during its ANALYZE is kicked off.
      auto table_name =
          Format("\"$0\".\"$1\"",
                 table_id_to_name_[table_id].has_pgschema_name() ?
                 table_id_to_name_[table_id].pgschema_name() : "pg_catalog",
                 table_id_to_name_[table_id].table_name());
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
          // Don't directly return status if an error status is generated from running analyze.
          // Allow subsequent ANALYZEs to run.
          LOG(WARNING) << "In YSQL database: " << dbname <<  ", failed ANALYZE statement: "
                       << analyze_query << table_name << " with error: " << str;
        } else {
          // Check if the table is deleted or renamed.
          auto renamed = VERIFY_RESULT(conn.FetchRow<bool>(
                            Format("SELECT EXISTS(SELECT 1 FROM pg_class WHERE oid = '$0')",
                                   VERIFY_RESULT(GetPgsqlTableOid(table_id)))));
          if (renamed) {
            VLOG(4) << "Table " << table_name << "was renamed";
            // Need to refresh name cache because the cached table name is outdated.
            refresh_name_cache_ = true;
          } else {
            // TODO(auto-analyze, #22883): remove deleted table entries from the YCQL service table.
            analyzed_tables.push_back(table_id);
          }
        }
      } else {
        analyzed_tables.push_back(table_id);
      }
    }
  }

  return analyzed_tables;
}

// Update the table mutations by subtracting the mutations count we fetched
// if ANALYZE succeeded or failed with "does not exist error".
// Do substraction instead of directly updating the mutation counts to zero because
// updating mutation counts to zero might cause us to lose some mutations collected
// during triggering ANALYZE.
// TODO(auto-analyze, #22883): Clean up entries from auto analyze YCQL table if
// mutations is 0 for a table for a long time to free up memory.
Status PgAutoAnalyzeService::UpdateTableMutationsAfterAnalyze(
    std::vector<TableId>& tables,
    std::unordered_map<TableId, int64_t>& table_id_to_mutations_maps) {
  VLOG_WITH_FUNC(2);
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
    bfcall_expr_pb->set_opcode(to_underlying(bfql::BFOpcode::OPCODE_SubI64I64_85));
    QLExpressionPB* operand1 = bfcall_expr_pb->add_operands();
    QLExpressionPB* operand2 = bfcall_expr_pb->add_operands();
    operand1->set_column_id(mutations_col_id);
    operand2->mutable_value()->set_int64_value(table_id_to_mutations_maps[table_id]);
    ops.push_back(std::move(update_op));

    // Erase the table we analyzed from table row count cache.
    table_tuple_count_.erase(table_id);
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session->TEST_ApplyAndFlush(ops), "Failed to clean up mutations from auto analyze table");

  return Status::OK();
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
    const IncreaseMutationCountersRequestPB& req, IncreaseMutationCountersResponsePB* resp) {
  VLOG_WITH_FUNC(3) << "req=" << req.ShortDebugString();

  for (const auto& elem : req.table_mutation_counts()) {
    pg_cluster_level_mutation_counter_.Increase(elem.table_id(), elem.mutation_count());
  }

  return Status::OK();
}

}  // namespace stateful_service
}  // namespace yb
