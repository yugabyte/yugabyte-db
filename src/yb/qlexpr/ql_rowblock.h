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

#pragma once

#include <memory>
#include <vector>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"

#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status_fwd.h"

namespace yb {

class WriteBuffer;

}

namespace yb::qlexpr {

//------------------------------------------ QL row ----------------------------------------
// A QL row. It uses QLValue to store the column values.
class QLRow {
 public:
  explicit QLRow(const std::shared_ptr<const Schema>& schema);
  QLRow(const QLRow& row);
  QLRow(QLRow&& row);
  ~QLRow();

  // Row columns' schema
  const Schema& schema() const { return *schema_.get(); }

  // Column count
  size_t column_count() const;

  // Column's datatype
  const std::shared_ptr<QLType>& column_type(const size_t col_idx) const;

  // Get a mutable/non-mutable column value.
  const QLValue& column(const size_t col_idx) const;
  QLValue* mutable_column(const size_t col_idx);

  void SetColumn(size_t col_idx, QLValuePB value);

  QLRow& operator=(const QLRow& other);
  QLRow& operator=(QLRow&& other);

  void SetColumnValues(const std::vector<QLValue>& column_values);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

 private:
  friend class QLRowBlock;

  //----------------------------- serializer / deserializer ---------------------------------
  // Note: QLRow's serialize / deserialize methods are private because we expect QL rows
  // to be serialized / deserialized as part of a row block. See QLRowBlock.
  void Serialize(QLClient client, WriteBuffer* buffer) const;
  Status Deserialize(QLClient client, Slice* data);

  std::shared_ptr<const Schema> schema_;
  std::vector<QLValue> values_;
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

  const std::vector<QLRow>& rows() const { return rows_; }

  // Return the row by index
  QLRow& row(size_t idx) { return rows_[idx]; }

  // Extend row block by 1 emtpy row and return the new row.
  QLRow& Extend();

  // Optimization to reserve memory for up to this many rows.
  void Reserve(size_t size);

  // Add a row to the rowblock.
  Status AddRow(const QLRow& row);

  //------------------------------------ debug string ---------------------------------------
  // Return a string for debugging.
  std::string ToString() const;

  //----------------------------- serializer / deserializer ---------------------------------
  void Serialize(QLClient client, WriteBuffer* buffer) const;
  std::string SerializeToString() const;
  RefCntSlice SerializeToRefCntSlice() const;
  Status Deserialize(QLClient client, Slice* data);

  //-------------------------- utility functions for rows data ------------------------------
  // Return row count.
  static Result<size_t> GetRowCount(QLClient client, const std::string_view& data);

  // Append rows data. Caller should ensure the column schemas are the same.
  static Status AppendRowsData(QLClient client, const RefCntSlice& src, RefCntSlice* dst);

  // Return rows data of 0 (empty) rows.
  static RefCntBuffer ZeroRowsData(QLClient client);

 private:
  // Schema of the selected columns. (Note: this schema has no key column definitions)
  SchemaPtr schema_;
  // Rows in this block.
  std::vector<QLRow> rows_;
};

std::unique_ptr<QLRowBlock> CreateRowBlock(QLClient client, const Schema& schema, Slice data);

}  // namespace yb::qlexpr
