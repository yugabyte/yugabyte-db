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

#include "yb/common/ql_protocol_util.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/util/result.h"
#include "yb/util/status_log.h"

namespace yb {

QLValuePB* QLPrepareColumn(QLWriteRequestPB* req, int column_id) {
  auto column_value = req->add_column_values();
  column_value->set_column_id(column_id);
  return column_value->mutable_expr()->mutable_value();
}

QLValuePB* QLPrepareCondition(QLConditionPB* condition, int column_id, QLOperator op) {
  condition->add_operands()->set_column_id(column_id);
  condition->set_op(op);
  return condition->add_operands()->mutable_value();
}

#define QL_PROTOCOL_TYPE_DEFINITIONS_IMPL(name, lname, type) \
void PP_CAT3(QLAdd, name, ColumnValue)( \
    QLWriteRequestPB* req, int column_id, type value) { \
  QLPrepareColumn(req, column_id)->PP_CAT3(set_, lname, _value)(value); \
} \
\
void PP_CAT3(QLSet, name, Expression)(QLExpressionPB* expr, type value) { \
  expr->mutable_value()->PP_CAT3(set_, lname, _value)(value); \
} \
\
void PP_CAT3(QLSet, name, Condition)( \
    QLConditionPB* condition, int column_id, QLOperator op, type value) { \
  QLPrepareCondition(condition, column_id, op)->PP_CAT3(set_, lname, _value)(value); \
} \
\
void PP_CAT3(QLAdd, name, Condition)( \
    QLConditionPB* condition, int column_id, QLOperator op, type value) { \
  PP_CAT3(QLSet, name, Condition)( \
    condition->add_operands()->mutable_condition(), column_id, op, value); \
} \

#define QL_PROTOCOL_TYPE_DEFINITIONS(i, data, entry) QL_PROTOCOL_TYPE_DEFINITIONS_IMPL entry

BOOST_PP_SEQ_FOR_EACH(QL_PROTOCOL_TYPE_DEFINITIONS, ~, QL_PROTOCOL_TYPES);

void QLAddNullColumnValue(QLWriteRequestPB* req, int column_id) {
  QLPrepareColumn(req, column_id);
}

void QLAddColumns(const Schema& schema, const std::vector<ColumnId>& columns,
                  QLReadRequestPB* req) {
  if (columns.empty()) {
    QLAddColumns(schema, schema.column_ids(), req);
    return;
  }
  req->clear_selected_exprs();
  req->mutable_column_refs()->Clear();
  QLRSRowDescPB* rsrow_desc = req->mutable_rsrow_desc();
  rsrow_desc->Clear();
  for (const auto& id : columns) {
    auto column = schema.column_by_id(id);
    CHECK_OK(column);
    req->add_selected_exprs()->set_column_id(id);
    req->mutable_column_refs()->add_ids(id);

    QLRSColDescPB* rscol_desc = rsrow_desc->add_rscol_descs();
    rscol_desc->set_name(column->name());
    column->type()->ToQLTypePB(rscol_desc->mutable_ql_type());
  }
}

std::unique_ptr<QLRowBlock> CreateRowBlock(QLClient client, const Schema& schema, Slice data) {
  auto rowblock = std::make_unique<QLRowBlock>(schema);
  if (!data.empty()) {
    // TODO: a better way to handle errors here?
    CHECK_OK(rowblock->Deserialize(client, &data));
  }
  return rowblock;
}

bool RequireReadForExpressions(const QLWriteRequestPB& request) {
  // A QLWriteOperation requires a read if it contains an IF clause or an UPDATE assignment that
  // involves an expresion with a column reference. If the IF clause contains a condition that
  // involves a column reference, the column will be included in "column_refs". However, we cannot
  // rely on non-empty "column_ref" alone to decide if a read is required because "IF EXISTS" and
  // "IF NOT EXISTS" do not involve a column reference explicitly.
  return request.has_if_expr() ||
         (request.has_column_refs() &&
             (!request.column_refs().ids().empty() || !request.column_refs().static_ids().empty()));
}

// If range key portion is missing and there are no targeted columns this is a range operation
// (e.g. range delete) -- it affects all rows within a hash key that match the where clause.
// Note: If target columns are given this could just be e.g. a delete targeting a static column
// which can also omit the range portion -- Analyzer will check these restrictions.
bool IsRangeOperation(const QLWriteRequestPB& request, const Schema& schema) {
  return request.range_column_values().size() < schema.num_range_key_columns() &&
         request.column_values().empty();
}

bool RequireRead(const QLWriteRequestPB& request, const Schema& schema) {
  // In case of a user supplied timestamp, we need a read (and hence appropriate locks for read
  // modify write) but it is at the docdb level on a per key basis instead of a QL read of the
  // latest row.
  bool has_user_timestamp = request.has_user_timestamp_usec();

  // We need to read the rows in the given range to find out which rows to write to.
  bool is_range_operation = IsRangeOperation(request, schema);

  return RequireReadForExpressions(request) || has_user_timestamp || is_range_operation;
}

Result<int32_t> CQLDecodeLength(Slice* data) {
  RETURN_NOT_ENOUGH(data, sizeof(int32_t));
  const auto len = static_cast<int32_t>(NetworkByteOrder::Load32(data->data()));
  data->remove_prefix(sizeof(int32_t));
  return len;
}

} // namespace yb
