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

#include "yb/bfql/gen_opcodes.h"

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/session.h"
#include "yb/client/schema.h"
#include "yb/client/yb_op.h"

#include "yb/master/master_defaults.h"

#include "yb/tserver/pg_mutation_counter.h"

#include "yb/util/atomic.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"

DEFINE_RUNTIME_uint32(ysql_cluster_level_mutation_persist_interval_ms, 10000,
                      "Interval at which the reported node level table mutation counts are "
                      "persisted to the underlying YCQL table by the central auto analyze service");
DEFINE_RUNTIME_uint32(ysql_cluster_level_mutation_persist_rpc_timeout_ms, 10000,
                      "Timeout for rpcs involved in persisting mutations in the auto-analyze "
                      "table.");

using namespace std::chrono_literals;

namespace yb {

namespace stateful_service {
PgAutoAnalyzeService::PgAutoAnalyzeService(
    const scoped_refptr<MetricEntity>& metric_entity,
    const std::shared_future<client::YBClient*>& client_future)
    : StatefulRpcServiceBase(StatefulServiceKind::PG_AUTO_ANALYZE, metric_entity, client_future) {}

void PgAutoAnalyzeService::Activate() { LOG(INFO) << ServiceName() << " activated"; }

void PgAutoAnalyzeService::Deactivate() { LOG(INFO) << ServiceName() << " de-activated"; }

Status PgAutoAnalyzeService::UpdateMutationsSinceLastAnalyze() {
  const auto& table_mutation_counts = pg_cluster_level_mutation_counter_.GetAndClear();
  if (table_mutation_counts.empty()) {
    VLOG(5) << "No more mutations";
    return Status::OK();
  }

  auto session = VERIFY_RESULT(GetYBSession(
      GetAtomicFlag(&FLAGS_ysql_cluster_level_mutation_persist_rpc_timeout_ms) * 1ms));
  auto* table = VERIFY_RESULT(GetServiceTable());

  // Increment mutation counters for tables
  const client::YBSchema& schema = table->schema();
  auto mutations_col_id = schema.ColumnId(schema.FindColumn(master::kPgAutoAnalyzeMutations));

  VLOG(2) << "Apply mutations: " << AsString(table_mutation_counts);

  std::vector<client::YBOperationPtr> ops;
  for (const auto& [table_id, mutation_count] : table_mutation_counts) {
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

Result<bool> PgAutoAnalyzeService::RunPeriodicTask() {
  // Update the underlying YCQL table that tracks cluster-wide mutations since the last
  // ANALYZE for all Pg tables.
  WARN_NOT_OK(UpdateMutationsSinceLastAnalyze(), "Failed to update mutations");

  // TODO(auto-analyze): Trigger ANALYZE for tables whose mutation counts have crossed the required
  // thresholds.
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
