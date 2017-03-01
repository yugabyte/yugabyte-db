// Copyright (c) YugaByte, Inc.
//
// This file contains the classes that represent a YQL row and a row block.

#ifndef YB_COMMON_YQL_ROWBLOCK_H
#define YB_COMMON_YQL_ROWBLOCK_H

#include <memory>

#include "yb/common/yql_value.h"
#include "yb/common/schema.h"

namespace yb {

//------------------------------------------ YQL row ----------------------------------------
// A YQL row. It uses YQLValueWithPB to store the column values.
class YQLRow {
 public:
  explicit YQLRow(const std::shared_ptr<const Schema>& schema);
  explicit YQLRow(const YQLRow& row);
  explicit YQLRow(YQLRow&& row);
  ~YQLRow();

  // Row columns' schema
  const Schema& schema() const { return *schema_.get(); }

  // Column count
  size_t column_count() const { return schema_->num_columns(); }

  // Column's datatype
  DataType column_type(const size_t col_idx) const {
    return schema_->column(col_idx).type_info()->type();
  }

  // Get a mutable/non-mutable column value.
  const YQLValue& column(const size_t col_idx) const { return values_.at(col_idx); }
  YQLValue* mutable_column(const size_t col_idx) { return &values_.at(col_idx); }

  YQLRow& operator=(const YQLRow& other);
  YQLRow& operator=(YQLRow&& other);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

 private:
  friend class YQLRowBlock;

  //----------------------------- serializer / deserializer ---------------------------------
  // Note: YQLRow's serialize / deserialize methods are private because we expect YQL rows
  // to be serialized / deserialized as part of a row block. See YQLRowBlock.
  void Serialize(YQLClient client, faststring* buffer) const;
  CHECKED_STATUS Deserialize(YQLClient client, Slice* data);

  std::shared_ptr<const Schema> schema_;
  std::vector<YQLValueWithPB> values_;
};

//--------------------------------------- YQL row block --------------------------------------
// A block of YQL rows. The rows can be extended. The rows are stored in an ordered vector so
// it is sortable.
class YQLRowBlock {
 public:
  // Create a row block for a table with the given schema and the selected column ids.
  YQLRowBlock(const Schema& schema, const std::vector<ColumnId>& column_ids);

  // Create a row block for the given schema.
  explicit YQLRowBlock(const Schema& schema);

  virtual ~YQLRowBlock();

  // Row columns' schema
  const Schema& schema() const { return *schema_.get(); }

  // Row count
  size_t row_count() const { return rows_.size(); }

  // The rows
  std::vector<YQLRow>& rows() { return rows_; }

  // Return the row by index
  YQLRow& row(size_t idx) { return rows_.at(idx); }

  // Extend row block by 1 emtpy row and return the new row.
  YQLRow& Extend();

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

  //----------------------------- serializer / deserializer ---------------------------------
  void Serialize(YQLClient client, faststring* buffer) const;
  CHECKED_STATUS Deserialize(YQLClient client, Slice* data);


 private:
  // Schema of the selected columns. (Note: this schema has no key column definitions)
  std::shared_ptr<Schema> schema_;
  // Rows in this block.
  std::vector<YQLRow> rows_;
};

// Map for easy lookup of column values of a row by the column id. This map is used in tserver
// for saving the column values of a selected row to evaluate the WHERE and IF clauses. Since
// we use the clauses in protobuf to evaluate, we will maintain the column values in YQLValuePB
// also to avoid conversion to and from YQLValueWithPB.
using YQLValueMap = std::unordered_map<ColumnId, YQLValuePB>;

// Evaluate a boolean condition for the given row.
CHECKED_STATUS EvaluateCondition(
    const YQLConditionPB& condition, const YQLValueMap& row, const Schema& schema, bool* result);

} // namespace yb

#endif // YB_COMMON_YQL_ROWBLOCK_H
