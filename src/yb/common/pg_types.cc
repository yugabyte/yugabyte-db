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

#include "yb/common/pg_types.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace yb {

PgObjectId::PgObjectId(const TableId& table_id) {
  auto res = GetPgsqlDatabaseOidByTableId(table_id);
  if (res.ok()) {
    database_oid = res.get();
  }
  res = GetPgsqlTableOid(table_id);
  if (res.ok()) {
    object_oid = res.get();
  } else {
    // Reset the previously set database_oid.
    database_oid = kPgInvalidOid;
  }
}

// TODO (dmitry) : Reimplement by using std::string_view (#11904) to avoid string creation
PgObjectId::PgObjectId(const Slice& table_id)
    : PgObjectId(table_id.ToBuffer()) {
}

std::string PgObjectId::ToString() const {
  return YB_STRUCT_TO_STRING(database_oid, object_oid);
}

std::string YsqlFullTableName::ToString() const {
  return YB_STRUCT_TO_STRING(namespace_name, schema_name, table_name);
}

}  // namespace yb
