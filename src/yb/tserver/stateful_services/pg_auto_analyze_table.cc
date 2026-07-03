// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/tserver/stateful_services/pg_auto_analyze_table.h"

#include <utility>
#include <vector>

#include "yb/bfql/gen_opcodes.h"

#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_protocol_util.h"

#include "yb/master/master_defaults.h"

#include "yb/util/status.h"

namespace yb::stateful_service {
namespace {

int32_t MutationsColumnId(const client::TableHandle& table) {
  const auto& schema = table.schema();
  return schema.ColumnId(schema.FindColumn(master::kPgAutoAnalyzeMutations));
}

template <class RequestPB>
void AddTableIdHashKey(RequestPB* req, const TableId& table_id) {
  QLAddStringHashValue(req, table_id);
}

template <class ConditionPB>
void AddExistsAndMutationsCondition(
    const client::TableHandle& table, ConditionPB* condition, QLOperator op, int64_t mutations) {
  condition->set_op(QL_OP_AND);
  table.AddCondition(condition, QL_OP_EXISTS);
  table.AddInt64Condition(condition, master::kPgAutoAnalyzeMutations, op, mutations);
}

client::YBqlWriteOpPtr NewSetMutationsOp(
    const client::TableHandle& table, const TableId& table_id, int64_t mutations) {
  auto update_op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  auto* const update_req = update_op->mutable_request();
  const auto mutations_col_id = MutationsColumnId(table);

  AddTableIdHashKey(update_req, table_id);
  update_req->mutable_column_refs()->add_ids(mutations_col_id);
  auto* col_pb = update_req->add_column_values();
  col_pb->set_column_id(mutations_col_id);
  col_pb->mutable_expr()->mutable_value()->set_int64_value(mutations);
  return update_op;
}

client::YBqlWriteOpPtr NewSubtractMutationsOp(
    const client::TableHandle& table, const PgAutoAnalyzeMutationSnapshot& snapshot) {
  auto update_op = table.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  auto* const update_req = update_op->mutable_request();
  const auto mutations_col_id = MutationsColumnId(table);

  AddTableIdHashKey(update_req, snapshot.table_id);
  update_req->mutable_column_refs()->add_ids(mutations_col_id);
  auto* col_pb = update_req->add_column_values();
  col_pb->set_column_id(mutations_col_id);
  auto* bfcall_expr_pb = col_pb->mutable_expr()->mutable_bfcall();
  bfcall_expr_pb->set_opcode(std::to_underlying(bfql::BFOpcode::OPCODE_SubI64I64_85));
  auto* operand1 = bfcall_expr_pb->add_operands();
  auto* operand2 = bfcall_expr_pb->add_operands();
  operand1->set_column_id(mutations_col_id);
  operand2->mutable_value()->set_int64_value(snapshot.mutations);
  return update_op;
}

}  // namespace

Status ResetPgAutoAnalyzeMutationCounts(
    const client::TableHandle& table, client::YBSession& session,
    std::span<const TableId> table_ids) {
  if (table_ids.empty()) {
    return Status::OK();
  }

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(table_ids.size());
  for (const auto& table_id : table_ids) {
    auto update_op = NewSetMutationsOp(table, table_id, 0);
    update_op->mutable_request()->mutable_if_expr()->mutable_condition()->set_op(QL_OP_EXISTS);
    ops.push_back(std::move(update_op));
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session.TEST_ApplyAndFlush(ops), "Failed to reset mutations in auto analyze table");
  return Status::OK();
}

Status SubtractPgAutoAnalyzeMutationCounts(
    const client::TableHandle& table, client::YBSession& session,
    std::span<const PgAutoAnalyzeMutationSnapshot> snapshots) {
  if (snapshots.empty()) {
    return Status::OK();
  }

  std::vector<client::YBOperationPtr> ops;
  ops.reserve(snapshots.size() * 2);
  for (const auto& snapshot : snapshots) {
    // If a manual ANALYZE reset raced with this auto-analyze pass, the current count can be lower
    // than the snapshot we originally analyzed. Clamp to zero instead of subtracting below zero.
    auto reset_op = NewSetMutationsOp(table, snapshot.table_id, 0);
    AddExistsAndMutationsCondition(
        table, reset_op->mutable_request()->mutable_if_expr()->mutable_condition(),
        QL_OP_LESS_THAN, snapshot.mutations);
    ops.push_back(std::move(reset_op));

    auto subtract_op = NewSubtractMutationsOp(table, snapshot);
    AddExistsAndMutationsCondition(
        table, subtract_op->mutable_request()->mutable_if_expr()->mutable_condition(),
        QL_OP_GREATER_THAN_EQUAL, snapshot.mutations);
    ops.push_back(std::move(subtract_op));
  }

  // TODO(async_flush): https://github.com/yugabyte/yugabyte-db/issues/12173
  RETURN_NOT_OK_PREPEND(
      session.TEST_ApplyAndFlush(ops), "Failed to clean up mutations from auto analyze table");
  return Status::OK();
}

}  // namespace yb::stateful_service
