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
// This file contains the classes that represent a QL row and a row block.

#ifndef YB_COMMON_QL_ROWBLOCK_H
#define YB_COMMON_QL_ROWBLOCK_H

#include <memory>

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

namespace yb {

//------------------------------------------ QL row ----------------------------------------
// A QL row. It uses QLValueWithPB to store the column values.
class QLRow {
 public:
  explicit QLRow(const std::shared_ptr<const Schema>& schema);
  QLRow(const QLRow& row);
  QLRow(QLRow&& row);
  ~QLRow();

  // Row columns' schema
  const Schema& schema() const { return *schema_.get(); }

  // Column count
  size_t column_count() const { return schema_->num_columns(); }

  // Column's datatype
  const std::shared_ptr<QLType>& column_type(const size_t col_idx) const {
    return schema_->column(col_idx).type();
  }

  // Get a mutable/non-mutable column value.
  const QLValue& column(const size_t col_idx) const { return values_.at(col_idx); }
  QLValue* mutable_column(const size_t col_idx) { return &values_.at(col_idx); }

  QLRow& operator=(const QLRow& other);
  QLRow& operator=(QLRow&& other);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

 private:
  friend class QLRowBlock;

  //----------------------------- serializer / deserializer ---------------------------------
  // Note: QLRow's serialize / deserialize methods are private because we expect QL rows
  // to be serialized / deserialized as part of a row block. See QLRowBlock.
  void Serialize(QLClient client, faststring* buffer) const;
  CHECKED_STATUS Deserialize(QLClient client, Slice* data);

  std::shared_ptr<const Schema> schema_;
  std::vector<QLValueWithPB> values_;
};

//--------------------------------------- QL row block --------------------------------------
// A block of QL rows. The rows can be extended. The rows are stored in an ordered vector so
// it is sortable.
class QLRowBlock {
 public:
  // Create a row block for a table with the given schema and the selected column ids.
  QLRowBlock(const Schema& schema, const std::vector<ColumnId>& column_ids);

  // Create a row block for the given schema.
  explicit QLRowBlock(const Schema& schema);

  virtual ~QLRowBlock();

  // Row columns' schema
  const Schema& schema() const { return *schema_.get(); }

  // Row count
  size_t row_count() const { return rows_.size(); }

  // The rows
  std::vector<QLRow>& rows() { return rows_; }

  // Return the row by index
  QLRow& row(size_t idx) { return rows_.at(idx); }

  // Extend row block by 1 emtpy row and return the new row.
  QLRow& Extend();

  // Add a row to the rowblock.
  CHECKED_STATUS AddRow(const QLRow& row);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

  //----------------------------- serializer / deserializer ---------------------------------
  void Serialize(QLClient client, faststring* buffer) const;
  CHECKED_STATUS Deserialize(QLClient client, Slice* data);

  //-------------------------- utility functions for rows data ------------------------------
  // Return row count.
  static CHECKED_STATUS GetRowCount(QLClient client, const std::string& data, size_t* count);

  // Append rows data. Caller should ensure the column schemas are the same.
  static CHECKED_STATUS AppendRowsData(QLClient client, const std::string& src, std::string* dst);

 private:
  // Schema of the selected columns. (Note: this schema has no key column definitions)
  std::shared_ptr<Schema> schema_;
  // Rows in this block.
  std::vector<QLRow> rows_;
};

// Map for easy lookup of column values of a row by the column id. This map is used in tserver
// for saving the column values of a selected row to evaluate the WHERE and IF clauses. Since
// we use the clauses in protobuf to evaluate, we will maintain the column values in QLValuePB
// also to avoid conversion to and from QLValueWithPB.

struct QLTableColumn {
 public:
  QLValuePB value;
  int64_t ttl_seconds;
  int64_t write_time;
};

using QLTableRow = std::unordered_map<ColumnId, QLTableColumn>;
using QLValueMap = std::unordered_map<ColumnId, QLValuePB>;

// Evaluate a boolean condition for the given row.
CHECKED_STATUS EvaluateCondition(const QLConditionPB& condition,
                                 const QLTableRow& row,
                                 bool* result);

} // namespace yb

#endif // YB_COMMON_QL_ROWBLOCK_H
