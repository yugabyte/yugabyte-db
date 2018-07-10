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

#ifndef YB_YQL_PGGATE_PG_DELETE_H_
#define YB_YQL_PGGATE_PG_DELETE_H_

#include "yb/yql/pggate/pg_dml_write.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// DELETE
//--------------------------------------------------------------------------------------------------

class PgDelete : public PgDmlWrite {
 public:
  // Public types.
  typedef std::shared_ptr<PgDelete> SharedPtr;
  typedef std::shared_ptr<const PgDelete> SharedPtrConst;

  typedef std::unique_ptr<PgDelete> UniPtr;
  typedef std::unique_ptr<const PgDelete> UniPtrConst;

  // Constructors.
  PgDelete(PgSession::SharedPtr pg_session,
           const char *database_name,
           const char *schema_name,
           const char *table_name);
  virtual ~PgDelete();

 private:
  virtual void AllocWriteRequest() override;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DELETE_H_
