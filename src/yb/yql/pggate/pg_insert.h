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

#ifndef YB_YQL_PGGATE_PG_INSERT_H_
#define YB_YQL_PGGATE_PG_INSERT_H_

#include "yb/yql/pggate/pg_statement.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// INSERT
//--------------------------------------------------------------------------------------------------

class PgInsert : public PgStatement {
 public:
  // Public types.
  typedef std::shared_ptr<PgInsert> SharedPtr;
  typedef std::shared_ptr<const PgInsert> SharedPtrConst;

  typedef std::unique_ptr<PgInsert> UniPtr;
  typedef std::unique_ptr<const PgInsert> UniPtrConst;

  // Constructors.
  PgInsert(PgSession::SharedPtr pg_session,
           const char *database_name,
           const char *schema_name,
           const char *table_name);
  virtual ~PgInsert();
  CHECKED_STATUS Prepare();

  // Set numeric types.
  CHECKED_STATUS SetColumnInt2(int attnum, int16_t value);
  CHECKED_STATUS SetColumnInt4(int attnum, int32_t value);
  CHECKED_STATUS SetColumnInt8(int attnum, int64_t value);

  CHECKED_STATUS SetColumnFloat4(int attnum, float value);
  CHECKED_STATUS SetColumnFloat8(int attnum, double value);

  // Set string types.
  CHECKED_STATUS SetColumnText(int attnum, const char *att_value, int att_bytes);

  // Set serialized-to-string types.
  CHECKED_STATUS SetColumnSerializedData(int attnum, const char *att_value, int att_bytes);

  // Execute.
  CHECKED_STATUS Exec();

 private:
  // Allocate column protobuf.
  PgsqlExpressionPB *AllocColumnPB(int attr_num);

  // Data members.
  client::YBTableName table_name_;
  std::shared_ptr<client::YBTable> table_;
  // TODO(neil) It would make a lot more sense if we index by attr_num instead of ID.
  std::vector<ColumnDesc> col_descs_;
  int key_col_count_;
  int partition_col_count_;

  // Protobuf code.
  vector<PgsqlExpressionPB *> col_values_;
  std::shared_ptr<client::YBPgsqlWriteOp> op_;
  PgsqlWriteRequestPB *req_ = nullptr;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_INSERT_H_
