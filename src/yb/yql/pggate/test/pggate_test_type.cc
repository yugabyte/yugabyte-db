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
//--------------------------------------------------------------------------------------------------

#include "yb/common/common.pb.h"
#include "yb/common/constants.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/pg_row.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/status_log.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_value.h"

using std::string;

namespace yb {
namespace pggate {

class PggateTestType : public PggateTest {};

Status AddColumnToBuilder(const std::string& column_name, int type_oid, SchemaBuilder* builder) {
  auto type_entity = YBCPgFindTypeEntity(type_oid);
  if (type_entity == nullptr) {
    return STATUS(NotFound, "Type entity not found");
  }
  return builder->AddColumn(column_name, ToLW(PersistentDataType(type_entity->yb_type)));
}

void SetAndCheckColumnValue(
    dockv::PgTableRow* row,
    const Schema& schema,
    const std::string& column_name,
    const QLValuePB& input) {
  auto column_index = ASSERT_RESULT(schema.ColumnIdByName(column_name));
  ASSERT_OK(row->SetValue(column_index, input));
  auto result = row->GetQLValuePB(column_index);
  ASSERT_EQ(QLValue(input), QLValue(result));
}

TEST_F(PggateTestType, TestPgTableRowTypeConversion) {
  ASSERT_OK(Init("TestPgTableRowTypeConversion"));
  SchemaBuilder builder;
  ASSERT_OK(AddColumnToBuilder("bool", BOOLOID, &builder));
  ASSERT_OK(AddColumnToBuilder("float4", FLOAT4OID, &builder));
  ASSERT_OK(AddColumnToBuilder("text", TEXTOID, &builder));

  const Schema schema = builder.Build();
  const dockv::ReaderProjection projection(schema);
  dockv::PgTableRow row(projection);

  QLValuePB ql_value;

  // BOOLOID
  ql_value.set_bool_value(true);
  SetAndCheckColumnValue(&row, schema, "bool", ql_value);

  // FLOAT4OID
  ql_value.set_float_value(3.0f);
  SetAndCheckColumnValue(&row, schema, "float4", ql_value);

  // TEXTOID
  ql_value.set_string_value("test_value");
  SetAndCheckColumnValue(&row, schema, "text", ql_value);
}

}  // namespace pggate
}  // namespace yb
