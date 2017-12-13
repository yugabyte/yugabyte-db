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

#include "yb/client/table_handle.h"

#include "yb/master/master.pb.h"

#include "yb/ql/util/statement_result.h"

using namespace std::literals;

namespace yb {
namespace client {

Status TableHandle::Create(const YBTableName& table_name,
                           int num_tablets,
                           YBClient* client,
                           YBSchemaBuilder* builder,
                           int num_replicas) {
  YBSchema schema;
  RETURN_NOT_OK(builder->Build(&schema));
  return Create(table_name, num_tablets, schema, client, num_replicas);
}

Status TableHandle::Create(const YBTableName& table_name,
                           int num_tablets,
                           const YBSchema& schema,
                           YBClient* client,
                           int num_replicas) {
  std::unique_ptr <YBTableCreator> table_creator(client->NewTableCreator());
  RETURN_NOT_OK(table_creator->table_name(table_name)
      .schema(&schema)
      .num_replicas(num_replicas)
      .num_tablets(num_tablets)
      .Create());

  return Open(table_name, client);
}

Status TableHandle::Open(const YBTableName& table_name, YBClient* client) {
  RETURN_NOT_OK(client->OpenTable(table_name, &table_));

  auto schema = table_->schema();
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    yb::ColumnId col_id = yb::ColumnId(schema.ColumnId(i));
    column_ids_.emplace(schema.Column(i).name(), col_id);
    column_types_.emplace(col_id, schema.Column(i).type());
  }

  return Status::OK();
}

namespace {

template<class T>
auto SetupRequest(const T& op, const YBSchema& schema) {
  auto* req = op->mutable_request();
  req->set_client(YQL_CLIENT_CQL);
  req->set_request_id(0);
  req->set_query_id(reinterpret_cast<int64_t>(op.get()));
  req->set_schema_version(schema.version());
  return req;
}

} // namespace

std::shared_ptr<YBqlWriteOp> TableHandle::NewWriteOp(QLWriteRequestPB::QLStmtType type) const {
  auto op = std::make_shared<YBqlWriteOp>(table_);
  auto* req = SetupRequest(op, table_->schema());
  req->set_type(type);
  return op;
}

std::shared_ptr<YBqlReadOp> TableHandle::NewReadOp() const {
  std::shared_ptr<YBqlReadOp> op(table_->NewQLRead());
  SetupRequest(op, table_->schema());
  return op;
}

void TableHandle::AddInt32ColumnValue(
    QLWriteRequestPB* req, const string& column_name, int32_t value) const {
  auto column_value = req->add_column_values();
  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_int32_value(value);
}

void TableHandle::AddInt64ColumnValue(
    QLWriteRequestPB* req, const string& column_name, int64_t value) const {
  auto column_value = req->add_column_values();
  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_int64_value(value);
}

void TableHandle::AddStringColumnValue(
    QLWriteRequestPB* req, const string& column_name, const string& value) const {
  auto column_value = req->add_column_values();
  column_value->set_column_id(ColumnId(column_name));
  column_value->mutable_expr()->mutable_value()->set_string_value(value);
}

void TableHandle::AddInt32RangeValue(QLWriteRequestPB* req, int32_t value) const {
  SetInt32Expression(req->add_range_column_values(), value);
}

void TableHandle::AddInt64RangeValue(QLWriteRequestPB* req, int64_t value) const {
  SetInt64Expression(req->add_range_column_values(), value);
}

void TableHandle::AddStringRangeValue(QLWriteRequestPB* req, const std::string& value) const {
  SetStringExpression(req->add_range_column_values(), value);
}

void TableHandle::SetInt32Expression(QLExpressionPB* expr, int32_t value) const {
  expr->mutable_value()->set_int32_value(value);
}

void TableHandle::SetInt64Expression(QLExpressionPB* expr, int64_t value) const {
  expr->mutable_value()->set_int64_value(value);
}

void TableHandle::SetStringExpression(QLExpressionPB* expr, const string& value) const {
  expr->mutable_value()->set_string_value(value);
}

void TableHandle::SetColumn(QLColumnValuePB* column_value, const string& column_name) const {
  column_value->set_column_id(ColumnId(column_name));
}

void TableHandle::SetInt32Condition(
    QLConditionPB* const condition, const string& column_name, const QLOperator op,
    const int32_t value) const {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto* const val = condition->add_operands()->mutable_value();
  val->set_int32_value(value);
}

void TableHandle::SetStringCondition(
    QLConditionPB* const condition, const string& column_name, const QLOperator op,
    const string& value) const {
  condition->add_operands()->set_column_id(ColumnId(column_name));
  condition->set_op(op);
  auto* const val = condition->add_operands()->mutable_value();
  val->set_string_value(value);
}

void TableHandle::AddInt32Condition(
    QLConditionPB* const condition, const string& column_name, const QLOperator op,
    const int32_t value) const {
  SetInt32Condition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddStringCondition(
    QLConditionPB* const condition, const string& column_name, const QLOperator op,
    const string& value) const {
  SetStringCondition(condition->add_operands()->mutable_condition(), column_name, op, value);
}

void TableHandle::AddCondition(QLConditionPB* const condition, const QLOperator op) const {
  condition->add_operands()->mutable_condition()->set_op(op);
}

void TableHandle::AddColumns(const std::vector <std::string>& columns, QLReadRequestPB* req) const {
  QLRSRowDescPB* rsrow_desc = req->mutable_rsrow_desc();
  for (const auto column : columns) {
    auto id = ColumnId(column);
    req->add_selected_exprs()->set_column_id(id);
    req->mutable_column_refs()->add_ids(id);

    QLRSColDescPB* rscol_desc = rsrow_desc->add_rscol_descs();
    rscol_desc->set_name(column);
    ColumnType(column)->ToQLTypePB(rscol_desc->mutable_ql_type());
  }
}

TableIterator::TableIterator() : table_(nullptr) {}

TableIterator::TableIterator(
    const TableHandle* table,
    YBConsistencyLevel consistency,
    const std::vector<std::string>& columns,
    const TableFilter& filter)
    : table_(table) {
  auto client = (*table)->client();
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  CHECK_OK(client->GetTablets(table->name(), 0, &tablets));
  ops_.reserve(tablets.size());

  auto session = client->NewSession();
  session->SetTimeout(60s);

  for (const auto& tablet : tablets) {
    auto op = table->NewReadOp();
    auto req = op->mutable_request();
    op->set_yb_consistency_level(consistency);

    const auto& key_start = tablet.partition().partition_key_start();
    if (!key_start.empty()) {
      req->set_hash_code(PartitionSchema::DecodeMultiColumnHashValue(key_start));
    }

    if (filter) {
      filter(*table_, req->mutable_where_expr()->mutable_condition());
    }
    table_->AddColumns(columns, req);

    CHECK_OK(session->Apply(op));
    ops_.push_back(std::move(op));
  }

  CHECK_OK(session->Flush());

  for(const auto& op : ops_) {
    CHECK_EQ(QLResponsePB::YQL_STATUS_OK, op->response().status());
  }

  ops_index_ = 0;
  Move();
}

bool TableIterator::Equals(const TableIterator& rhs) const {
  return table_ == rhs.table_;
}

TableIterator& TableIterator::operator++() {
  ++row_index_;
  Move();
  return *this;
}

const QLRow& TableIterator::operator*() const {
  return current_block_->rows()[row_index_];
}

void TableIterator::Move() {
  while (!current_block_ || row_index_ == current_block_->rows().size()) {
    if (current_block_) {
      ++ops_index_;
    }
    if (ops_index_ == ops_.size()) {
      table_ = nullptr;
      return;
    }
    auto next_block = ops_[ops_index_]->MakeRowBlock();
    CHECK_OK(next_block);
    current_block_ = std::move(*next_block);
    row_index_ = 0;
  }
}

void FilterBetween::operator()(const TableHandle& table, QLConditionPB* condition) const {
  condition->set_op(QL_OP_AND);
  table.AddInt32Condition(
      condition, column_, lower_inclusive_ ? QL_OP_GREATER_THAN_EQUAL : QL_OP_GREATER_THAN,
      lower_bound_);
  table.AddInt32Condition(
      condition, column_, upper_inclusive_ ? QL_OP_LESS_THAN_EQUAL : QL_OP_LESS_THAN, upper_bound_);
}

void FilterGreater::operator()(const TableHandle& table, QLConditionPB* condition) const {
  table.SetInt32Condition(
      condition, column_, inclusive_ ? QL_OP_GREATER_THAN_EQUAL : QL_OP_GREATER_THAN, bound_);
}

void FilterLess::operator()(const TableHandle& table, QLConditionPB* condition) const {
  table.SetInt32Condition(
      condition, column_, inclusive_ ? QL_OP_LESS_THAN_EQUAL : QL_OP_LESS_THAN, bound_);
}

} // namespace client
} // namespace yb
