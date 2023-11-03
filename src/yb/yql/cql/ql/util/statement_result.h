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

#pragma once

#include "yb/client/client_fwd.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_fwd.h"
#include "yb/common/value.pb.h"

#include "yb/gutil/callback_forward.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/util/ref_cnt_buffer.h"

namespace yb {
namespace ql {

// This module is included by a few outside classes, so we cannot include ptree header files here.
// Use forward declaration.
class PTDmlStmt;
class PTListNode;

//------------------------------------------------------------------------------------------------
// Result of preparing a statement. Only DML statement will return a prepared result that describes
// the schemas of the bind variables used and, for SELECT statement, the schemas of the columns
// selected.
class PreparedResult {
 public:
  // Public types.
  typedef std::unique_ptr<PreparedResult> UniPtr;
  typedef std::unique_ptr<const PreparedResult> UniPtrConst;

  // Constructors.
  explicit PreparedResult(const PTDmlStmt& stmt);
  explicit PreparedResult(const PTListNode& stmt);
  virtual ~PreparedResult();

  // Accessors.
  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<client::YBTableName>& bind_table_names() const { return bind_table_names_; }
  const std::vector<ColumnSchema>& bind_variable_schemas() const { return bind_variable_schemas_; }
  const std::vector<int64_t>& hash_col_indices() const { return hash_col_indices_; }
  const std::vector<ColumnSchema>& column_schemas() const { return *column_schemas_; }

 private:
  const client::YBTableName table_name_;
  std::vector<client::YBTableName> bind_table_names_;
  std::vector<ColumnSchema> bind_variable_schemas_;
  std::vector<int64_t> hash_col_indices_;
  std::shared_ptr<std::vector<ColumnSchema>> column_schemas_;
};

//------------------------------------------------------------------------------------------------
// Result of executing a statement. Different possible types of results are listed below.
class ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<ExecutedResult> SharedPtr;
  typedef std::shared_ptr<const ExecutedResult> SharedPtrConst;

  // Constructors.
  ExecutedResult() { }
  virtual ~ExecutedResult() { }

  // Execution result types.
  enum class Type {
    SET_KEYSPACE  = 1,
    ROWS          = 2,
    SCHEMA_CHANGE = 3
  };

  virtual Type type() const = 0;
};

// Callback to be called after a statement is executed. When execution fails, a not-ok status is
// passed. When it succeeds, an ok status and the execution result are passed. When there is no
// result (i.e. void), a nullptr is passed.
typedef Callback<void(const Status&, const ExecutedResult::SharedPtr&)> StatementExecutedCallback;

//------------------------------------------------------------------------------------------------
// Result of "USE <keyspace>" statement.
class SetKeyspaceResult : public ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<SetKeyspaceResult> SharedPtr;
  typedef std::shared_ptr<const SetKeyspaceResult> SharedPtrConst;

  // Constructors.
  explicit SetKeyspaceResult(const std::string& keyspace) : keyspace_(keyspace) { }
  virtual ~SetKeyspaceResult() override { };

  // Result type.
  Type type() const override { return Type::SET_KEYSPACE; }

  // Accessor function for keyspace.
  const std::string& keyspace() const { return keyspace_; }

 private:
  const std::string keyspace_;
};

//------------------------------------------------------------------------------------------------
// Result of rows returned from executing a DML statement.
class RowsResult : public ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<RowsResult> SharedPtr;
  typedef std::shared_ptr<const RowsResult> SharedPtrConst;

  // Constructors.
  explicit RowsResult(const PTDmlStmt *tnode); // construct empty rows result for the statement.
  explicit RowsResult(client::YBqlOp *op, const PTDmlStmt *tnode = nullptr);
  RowsResult(const client::YBTableName& table_name,
             const std::shared_ptr<std::vector<ColumnSchema>>& column_schemas,
             const RefCntSlice& rows_data);
  virtual ~RowsResult() override;

  // Result type.
  Type type() const override { return Type::ROWS; }

  // Accessor functions.
  const client::YBTableName& table_name() const { return table_name_; }
  const std::vector<ColumnSchema>& column_schemas() const { return *column_schemas_; }
  void set_column_schema(int col_index, const std::shared_ptr<QLType>& type);
  Slice rows_data() const { return rows_data_.AsSlice(); }
  void set_rows_data(const RefCntSlice& value) { rows_data_ = value; }
  const std::string& paging_state() const { return paging_state_; }
  QLClient client() const { return client_; }

  Status Append(RowsResult&& other);

  void SetPagingState(client::YBqlOp *op);
  void SetPagingState(const QLPagingStatePB& paging_state);
  void SetPagingState(RowsResult&& other);
  // Override the schema version in the paging state. This is needed to support sending the schema
  // version of the main table in the response while keeping the rest of the state from the index.
  void OverrideSchemaVersionInPagingState(uint32_t schema_version);
  void ClearPagingState();
  bool has_paging_state() { return !paging_state_.empty(); }

  // Parse the rows data and return it as a row block. It is the caller's responsibility to free
  // the row block after use.
  std::unique_ptr<qlexpr::QLRowBlock> GetRowBlock() const;

 private:
  const client::YBTableName table_name_;
  std::shared_ptr<std::vector<ColumnSchema>> column_schemas_;
  const QLClient client_;
  RefCntSlice rows_data_;
  std::string paging_state_;
};

//------------------------------------------------------------------------------------------------
// Result of a schema object being changed as a result of executing a DDL statement.
class SchemaChangeResult : public ExecutedResult {
 public:
  // Public types.
  typedef std::shared_ptr<SchemaChangeResult> SharedPtr;
  typedef std::shared_ptr<const SchemaChangeResult> SharedPtrConst;

  // Constructors.
  SchemaChangeResult(
      const std::string& change_type, const std::string& object_type,
      const std::string& keyspace_name, const std::string& object_name = "");
  virtual ~SchemaChangeResult() override;

  // Result type.
  Type type() const override { return Type::SCHEMA_CHANGE; }

  // Accessor functions.
  const std::string& change_type() const { return change_type_; }
  const std::string& object_type() const { return object_type_; }
  const std::string& keyspace_name() const { return keyspace_name_; }
  const std::string& object_name() const { return object_name_; }

 private:
  const std::string change_type_;
  const std::string object_type_;
  const std::string keyspace_name_;
  const std::string object_name_;
};

} // namespace ql
} // namespace yb
