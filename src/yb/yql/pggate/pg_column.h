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

#ifndef YB_YQL_PGGATE_PG_COLUMN_H_
#define YB_YQL_PGGATE_PG_COLUMN_H_

#include "yb/yql/pggate/pg_coldesc.h"
#include "yb/yql/pggate/pg_expr.h"

namespace yb {
namespace pggate {

class PgColumn {
 public:
  PgColumn();

  // Bindings.
  PgsqlExpressionPB *AllocPrimaryBindPB(PgsqlWriteRequestPB *write_req);
  PgsqlExpressionPB *AllocBindPB(PgsqlWriteRequestPB *write_req);

  PgsqlExpressionPB *AllocPartitionBindPB(PgsqlReadRequestPB *read_req);
  PgsqlExpressionPB *AllocBindPB(PgsqlReadRequestPB *read_req);

  // Access functions.
  ColumnDesc *desc() {
    return &desc_;
  }

  const ColumnDesc *desc() const {
    return &desc_;
  }

  PgsqlExpressionPB *bind_pb() {
    return bind_pb_;
  }

  int attr_num() const {
    return desc_.attr_num();
  }

  int id() const {
    return desc_.id();
  }

  InternalType internal_type() const {
    return desc_.internal_type();
  }

  bool read_requested() const {
    return read_requested_;
  }

  bool set_read_requested(bool value) {
    return read_requested_ = value;
  }

 private:
  ColumnDesc desc_;

  // Protobuf code.
  // Input binds. For now these are just literal values of the columns.
  // - In Kudu API, for primary columns, their associated values in protobuf expression list must
  //   strictly follow the order that was specified by CREATE TABLE statement while Postgres DML
  //   statements will not follow this order. Therefore, we reserve the spaces in protobuf
  //   structures for associated expressions of the primary columns in the specified order.
  // - During DML execution, the reserved expression spaces will be filled with actual values.
  // - The data-member "primary_exprs" is to map column id with the reserved expression spaces.
  PgsqlExpressionPB *bind_pb_ = nullptr;

  // Wether or not this column must be read from DB for the SQL request.
  bool read_requested_ = false;
};

}  // namespace pggate
}  // namespace yb

#endif  // YB_YQL_PGGATE_PG_COLUMN_H_
