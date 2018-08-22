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
#include "yb/yql/pggate/util/pg_bind.h"

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
  PgSelect(PgSession::ScopedRefPtr pg_session,
           const char *database_name,
           const char *schema_name,
           const char *table_name);
  virtual ~PgSelect();
  CHECKED_STATUS Prepare();

  // Setup internal structures for binding values.
  void PrepareBinds();

  // Output bind for selected expressions.
  PgsqlExpressionPB *AllocSelectedExprPB();
  CHECKED_STATUS AddSelectedColumnPB(int attr_num);

  // Currently, we only bind to column values.
  CHECKED_STATUS BindExprInt2(int attnum, int16_t *attr_value);
  CHECKED_STATUS BindExprInt4(int attnum, int32_t *attr_value);
  CHECKED_STATUS BindExprInt8(int attnum, int64_t *attr_value);

  CHECKED_STATUS BindExprFloat4(int attnum, float *attr_value);
  CHECKED_STATUS BindExprFloat8(int attnum, double *attr_value);

  // Set string types.
  CHECKED_STATUS BindExprText(int attnum, char *att_value, int64_t *att_bytes);

  // Set serialized-to-string types.
  CHECKED_STATUS BindExprSerializedData(int attnum, char *att_value, int64_t *att_bytes);

  // Execute.
  CHECKED_STATUS Exec();

  // Fetch a row and advance cursor to the next row.
  CHECKED_STATUS Fetch(int64_t *row_count);

 private:
  // Allocate column protobuf.
  virtual PgsqlExpressionPB *AllocColumnExprPB(int attr_num) override;

  // Protobuf instruction.
  std::shared_ptr<client::YBPgsqlReadOp> read_op_;
  PgsqlReadRequestPB *read_req_ = nullptr;

  // Caching data.
  std::list<string> result_set_;
  vector<PgBind::SharedPtr> pg_binds_;

  // Cursor.
  int64_t total_row_count_ = 0;
  PgNetBuffer cursor_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_SELECT_H_
