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

#ifndef YB_YQL_PGGATE_PG_DML_WRITE_H_
#define YB_YQL_PGGATE_PG_DML_WRITE_H_

#include "yb/yql/pggate/pg_dml.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DML WRITE - Insert, Update, Delete.
//--------------------------------------------------------------------------------------------------

class PgDmlWrite : public PgDml {
 public:
  // Abstract class without constructors.
  virtual ~PgDmlWrite();

  // Prepare write operations.
  CHECKED_STATUS Prepare();

  // Execute.
  CHECKED_STATUS Exec();

  // Setup internal structures for binding values.
  void PrepareBinds();

 protected:
  // Constructor.
  PgDmlWrite(PgSession::SharedPtr pg_session,
             const char *database_name,
             const char *schema_name,
             const char *table_name,
             StmtOp stmt_op);

  // Allocate write request.
  virtual void AllocWriteRequest() = 0;

  // Allocate column expression.
  PgsqlExpressionPB *AllocColumnExprPB(int attr_num) override;

  // Protobuf code.
  std::shared_ptr<client::YBPgsqlWriteOp> write_op_;
  PgsqlWriteRequestPB *write_req_ = nullptr;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DML_WRITE_H_
