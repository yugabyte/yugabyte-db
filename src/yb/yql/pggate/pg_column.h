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
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/common/ql_datatype.h"

namespace yb {
namespace pggate {

class PgColumn {
 public:
  // Constructor & Destructor.
  PgColumn(std::reference_wrapper<const Schema> schema, size_t index);

  // Bindings for write requests.
  LWPgsqlExpressionPB *AllocPrimaryBindPB(LWPgsqlWriteRequestPB *write_req);
  LWPgsqlExpressionPB *AllocBindPB(LWPgsqlWriteRequestPB *write_req);

  // Bindings for read requests.
  LWPgsqlExpressionPB *AllocPrimaryBindPB(LWPgsqlReadRequestPB *write_req);
  LWPgsqlExpressionPB *AllocBindPB(LWPgsqlReadRequestPB *read_req);

  // Bindings for read requests.
  LWPgsqlExpressionPB *AllocBindConditionExprPB(LWPgsqlReadRequestPB *read_req);

  // Assign values for write requests.
  LWPgsqlExpressionPB *AllocAssignPB(LWPgsqlWriteRequestPB *write_req);

  void ResetBindPB();

  // Access functions.
  const ColumnSchema& desc() const;

  const LWPgsqlExpressionPB *bind_pb() const {
    return bind_pb_;
  }

  LWPgsqlExpressionPB *bind_pb() {
    return bind_pb_;
  }

  LWPgsqlExpressionPB *assign_pb() {
    return assign_pb_;
  }

  const std::string& attr_name() const;

  int attr_num() const;

  int id() const;

  InternalType internal_type() const;

  bool read_requested() const {
    return read_requested_;
  }

  void set_read_requested(const bool value) {
    read_requested_ = value;
  }

  bool write_requested() const {
    return write_requested_;
  }

  void set_write_requested(const bool value) {
    write_requested_ = value;
  }

  bool is_partition() const;
  bool is_primary() const;
  bool is_virtual_column() const;

  void set_pg_type_info(int typid, int typmod, int collid) {
    pg_typid_ = typid;
    pg_typmod_ = typmod;
    pg_collid_ = collid;
  }

  bool has_pg_type_info() const {
    return pg_typid_ != 0;
  }

  int pg_typid() const {
    return pg_typid_;
  }

  int pg_typmod() const {
    return pg_typmod_;
  }

  int pg_collid() const {
    return pg_collid_;
  }

 private:
  const Schema& schema_;
  const size_t index_;

  // Protobuf code.
  // Input binds. For now these are just literal values of the columns.
  // - In DocDB API, for primary columns, their associated values in protobuf expression list must
  //   strictly follow the order that was specified by CREATE TABLE statement while Postgres DML
  //   statements will not follow this order. Therefore, we reserve the spaces in protobuf
  //   structures for associated expressions of the primary columns in the specified order.
  // - During DML execution, the reserved expression spaces will be filled with actual values.
  // - The data-member "primary_exprs" is to map column id with the reserved expression spaces.
  LWPgsqlExpressionPB *bind_pb_ = nullptr;
  LWPgsqlExpressionPB *bind_condition_expr_pb_ = nullptr;

  // Protobuf for new-values of a column in the tuple.
  LWPgsqlExpressionPB *assign_pb_ = nullptr;

  // Wether or not this column must be read from DB for the SQL request.
  bool read_requested_ = false;

  // Wether or not this column will be written for the request.
  bool write_requested_ = false;

  int pg_typid_ = 0;

  int pg_typmod_ = -1;

  int pg_collid_ = 0;
};

}  // namespace pggate
}  // namespace yb
