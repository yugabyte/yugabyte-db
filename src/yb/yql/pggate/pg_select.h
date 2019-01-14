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
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGGATE_PG_SELECT_H_
#define YB_YQL_PGGATE_PG_SELECT_H_

#include <list>

#include "yb/gutil/ref_counted.h"

#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// SELECT
//--------------------------------------------------------------------------------------------------

class PgSelect : public PgDml {
 public:
  // Public types.
  typedef scoped_refptr<PgSelect> ScopedRefPtr;

  // Constructors.
  PgSelect(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id);
  virtual ~PgSelect();

  virtual StmtOp stmt_op() const override { return StmtOp::STMT_SELECT; }

  // Prepare SELECT before execution.
  CHECKED_STATUS Prepare();

  // Setup internal structures for binding values during prepare.
  void PrepareColumns();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  // Allocate column protobuf.
  PgsqlExpressionPB *AllocColumnBindPB(PgColumn *col) override;

  // Allocate protobuf for target.
  PgsqlExpressionPB *AllocTargetPB() override;

  // Allocate column expression.
  PgsqlExpressionPB *AllocColumnAssignPB(PgColumn *col) override;

  // Delete allocated target for columns that have no bind-values.
  CHECKED_STATUS DeleteEmptyPrimaryBinds();

  // Protobuf instruction.
  std::shared_ptr<client::YBPgsqlReadOp> read_op_;
  PgsqlReadRequestPB *read_req_ = nullptr;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_SELECT_H_
