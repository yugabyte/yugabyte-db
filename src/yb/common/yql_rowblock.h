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
// A YQL row. The columns' datatypes are kept in the row schema and the null states are stored in
// a bitmap (specialized vector<bool>). Each column is identified by its column index in the row
// schema, not by the column id.
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

  // Get and set a column value as/using YQLValue.
  // Note: for more efficient access, consider using the accessors below.
  YQLValue column(size_t col_idx) const;
  void set_column(size_t col_idx, const YQLValue& v);
  void set_column(size_t col_idx, YQLValue&& v);

  // Is the column null?
  inline bool IsNull(size_t col_idx) const {
    return is_nulls_[col_idx];
  }

  // Set the column to null or not.
  void SetNull(const size_t col_idx, const bool is_null) {
    is_nulls_[col_idx] = is_null;
  }

  //----------------------------------- get value methods -----------------------------------
  // Get the row column value. CHECK failure will result if the value stored is not of the
  // expected datatype or the value is null.
  template<typename type_t>
  type_t value(const size_t col_idx, const DataType expected_type, const type_t value) const {
    return YQLValueCore::value(column_type(col_idx), expected_type, IsNull(col_idx), value);
  }

  int8_t int8_value(size_t col_idx) const;
  int16_t int16_value(size_t col_idx) const;
  int32_t int32_value(size_t col_idx) const;
  int64_t int64_value(size_t col_idx) const;
  float float_value(size_t col_idx) const;
  double double_value(size_t col_idx) const;
  std::string string_value(size_t col_idx) const;
  bool bool_value(size_t col_idx) const;
  Timestamp timestamp_value(size_t col_idx) const;

  //----------------------------------- set value methods -----------------------------------
  // Set the row column value. CHECK failure will result if the value stored is not of the
  // expected datatype.
  template<typename type_t>
  void set_value(
      const size_t col_idx, const DataType expected_type, const type_t other, type_t* value) {
    YQLValueCore::set_value(column_type(col_idx), expected_type, other, value);
    SetNull(col_idx, false);
  }

  void set_int8_value(size_t col_idx, int8_t v);
  void set_int16_value(size_t col_idx, int16_t v);
  void set_int32_value(size_t col_idx, int32_t v);
  void set_int64_value(size_t col_idx, int64_t v);
  void set_float_value(size_t col_idx, float v);
  void set_double_value(size_t col_idx, double v);
  void set_string_value(size_t col_idx, const std::string& v);
  void set_bool_value(size_t col_idx, bool v);
  void set_timestamp_value(size_t col_idx, const Timestamp& v);

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
  YQLValueCore* values_;
  std::vector<bool> is_nulls_;
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

// Map for easy lookup of column values of a row by the column id.
using YQLValueMap = std::unordered_map<ColumnId, YQLValue>;

// Evaluate a boolean condition for the given row.
CHECKED_STATUS EvaluateCondition(
    const YQLConditionPB& condition, const YQLValueMap& row, const Schema& schema, bool* result);

} // namespace yb

#endif // YB_COMMON_YQL_ROWBLOCK_H
