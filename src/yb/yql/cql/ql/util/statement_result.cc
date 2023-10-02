//--------------------------------------------------------------------------------------------------
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
//
// Different results of processing a statement.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/common/ql_protocol.messages.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/schema_pbutil.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/schema.h"
#include "yb/util/debug-util.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

using std::string;
using std::vector;
using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;

using client::YBOperation;
using client::YBqlOp;
using client::YBqlReadOp;
using client::YBqlWriteOp;
using client::YBTableName;

//------------------------------------------------------------------------------------------------
namespace {

// Get bind column schemas for DML.
void GetBindVariableSchemasFromDmlStmt(const PTDmlStmt& stmt,
                                       vector<ColumnSchema>* schemas,
                                       vector<YBTableName>* table_names = nullptr) {
  // Only add the bind variables if the table name is determined
  if (stmt.bind_table()) {
    schemas->reserve(schemas->size() + stmt.bind_variables().size());
    if (table_names != nullptr) {
      table_names->reserve(table_names->size() + stmt.bind_variables().size());
    }

    for (const PTBindVar *var : stmt.bind_variables()) {
      DCHECK_NOTNULL(var->name().get());
      schemas->emplace_back(var->name() ? string(var->name()->c_str()) : string(), var->ql_type());
      if (table_names != nullptr) {
        table_names->emplace_back(stmt.bind_table()->name());
      }
    }
  }
}

shared_ptr<vector<ColumnSchema>> GetColumnSchemasFromOp(const YBqlOp& op, const PTDmlStmt *tnode) {
  switch (op.type()) {
    case YBOperation::Type::QL_READ: {
      // For actual execution "tnode" is always not null.
      if (tnode != nullptr) {
        return tnode->selected_schemas();
      }

      return std::make_shared<vector<ColumnSchema>>(
          static_cast<const YBqlReadOp&>(op).MakeColumnSchemasFromRequest());
    }

    case YBOperation::Type::QL_WRITE: {
      shared_ptr<vector<ColumnSchema>> column_schemas = make_shared<vector<ColumnSchema>>();
      const auto& write_op = static_cast<const YBqlWriteOp&>(op);
      column_schemas->reserve(write_op.response().column_schemas().size());
      for (const auto& column_schema : write_op.response().column_schemas()) {
        column_schemas->emplace_back(ColumnSchemaFromPB(column_schema));
      }
      return column_schemas;
    }

    case YBOperation::Type::PGSQL_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::PGSQL_WRITE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_WRITE:
      break;
    // default: fallthrough
  }

  LOG(FATAL) << "Internal error: invalid or unknown QL operation: " << op.type();
  return nullptr;
}

QLClient GetClientFromOp(const YBqlOp& op) {
  switch (op.type()) {
    case YBOperation::Type::QL_READ:
      return static_cast<const YBqlReadOp&>(op).request().client();
    case YBOperation::Type::QL_WRITE:
      return static_cast<const YBqlWriteOp&>(op).request().client();
    case YBOperation::Type::PGSQL_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::PGSQL_WRITE: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_READ: FALLTHROUGH_INTENDED;
    case YBOperation::Type::REDIS_WRITE:
      break;
    // default: fallthrough
  }
  LOG(FATAL) << "Internal error: invalid or unknown QL operation: " << op.type();

  // Inactive code: It's only meant to avoid compilation warning.
  return QLClient();
}

} // namespace

//------------------------------------------------------------------------------------------------
PreparedResult::PreparedResult(const PTDmlStmt& stmt)
    : table_name_(stmt.bind_table() ? stmt.bind_table()->name() : YBTableName()),
      hash_col_indices_(stmt.hash_col_indices()),
      column_schemas_(stmt.selected_schemas()) {
  GetBindVariableSchemasFromDmlStmt(stmt, &bind_variable_schemas_);
  if (column_schemas_ == nullptr) {
    column_schemas_ = make_shared<vector<ColumnSchema>>();
  }
}

PreparedResult::PreparedResult(const PTListNode& stmt)
    : column_schemas_(make_shared<vector<ColumnSchema>>()) {
  for (TreeNode::SharedPtr tnode : stmt.node_list()) {
    switch (tnode->opcode()) {
      case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTDeleteStmt: {
        const auto& dml = static_cast<const PTDmlStmt&>(*tnode);
        GetBindVariableSchemasFromDmlStmt(dml, &bind_variable_schemas_, &bind_table_names_);
        if (hash_col_indices_.empty()) {
          hash_col_indices_ = dml.hash_col_indices();
        }
        break;
      }
      default:
        break;
    }
  }
}

PreparedResult::~PreparedResult() {
}

//------------------------------------------------------------------------------------------------
RowsResult::RowsResult(const PTDmlStmt *tnode)
    : table_name_(tnode->table()->name()),
      column_schemas_(tnode->selected_schemas()),
      client_(YQL_CLIENT_CQL),
      rows_data_(qlexpr::QLRowBlock::ZeroRowsData(YQL_CLIENT_CQL)) {
  if (column_schemas_ == nullptr) {
    column_schemas_ = make_shared<vector<ColumnSchema>>();
  }
}

RowsResult::RowsResult(YBqlOp *op, const PTDmlStmt *tnode)
    : table_name_(op->table()->name()),
      column_schemas_(GetColumnSchemasFromOp(*op, tnode)),
      client_(GetClientFromOp(*op)),
      rows_data_(op->rows_data()) {
  if (column_schemas_ == nullptr) {
    column_schemas_ = make_shared<vector<ColumnSchema>>();
  }
  SetPagingState(op);
}

RowsResult::RowsResult(const YBTableName& table_name,
                       const shared_ptr<vector<ColumnSchema>>& column_schemas,
                       const RefCntSlice& rows_data)
    : table_name_(table_name),
      column_schemas_(column_schemas),
      client_(QLClient::YQL_CLIENT_CQL),
      rows_data_(rows_data) {
}

RowsResult::~RowsResult() {
}

void RowsResult::set_column_schema(int col_index, const std::shared_ptr<QLType>& type) {
  (*column_schemas_)[col_index].set_type(type);
}

Status RowsResult::Append(RowsResult&& other) {
  column_schemas_ = std::move(other.column_schemas_);
  if (rows_data_.empty()) {
    rows_data_ = std::move(other.rows_data_);
  } else {
    RETURN_NOT_OK(qlexpr::QLRowBlock::AppendRowsData(other.client_, other.rows_data_, &rows_data_));
  }
  paging_state_ = std::move(other.paging_state_);
  return Status::OK();
}

void RowsResult::SetPagingState(YBqlOp *op) {
  // If there is a paging state in the response, fill in the table ID also and serialize the
  // paging state as bytes.
  if (op->response().has_paging_state()) {
    QLPagingStatePB *paging_state = op->mutable_response()->mutable_paging_state();
    paging_state->set_table_id(op->table()->id());
    paging_state->set_schema_version(op->table()->schema().version());
    SetPagingState(*paging_state);
  }
}

void RowsResult::SetPagingState(const QLPagingStatePB& paging_state) {
  paging_state_.clear();
  CHECK(paging_state.SerializeToString(&paging_state_));
}

void RowsResult::SetPagingState(RowsResult&& other) {
  paging_state_ = std::move(other.paging_state_);
}

void RowsResult::OverrideSchemaVersionInPagingState(uint32_t schema_version) {
  LOG_IF(DFATAL, paging_state_.empty()) << "PagingState is not available";
  QLPagingStatePB paging_state;
  paging_state.ParseFromString(paging_state_);
  paging_state.set_schema_version(schema_version);
  SetPagingState(paging_state);
}

void RowsResult::ClearPagingState() {
  VLOG(3) << "Clear paging state " << GetStackTrace();
  paging_state_.clear();
}

std::unique_ptr<qlexpr::QLRowBlock> RowsResult::GetRowBlock() const {
  return qlexpr::CreateRowBlock(client_, Schema(*column_schemas_), rows_data_.AsSlice());
}

//------------------------------------------------------------------------------------------------
SchemaChangeResult::SchemaChangeResult(
    const string& change_type, const string& object_type,
    const string& keyspace_name, const string& object_name)
    : change_type_(change_type), object_type_(object_type),
      keyspace_name_(keyspace_name), object_name_(object_name) {
}

SchemaChangeResult::~SchemaChangeResult() {
}


} // namespace ql
} // namespace yb
