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

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb {
namespace pggate {

static const YBCPgTypeAttrs kYBCTestTypeAttrs = { 0 };

YBCStatus YBCTestCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                      DataType yb_type, bool is_hash, bool is_range) {
  int pg_type = 0;
  switch (yb_type) {
  case DataType::BOOL:
    pg_type = BOOLOID;
    break;
  case DataType::INT8:
    pg_type = CHAROID;
    break;
  case DataType::INT16:
    pg_type = INT2OID;
    break;
  case DataType::INT32:
    pg_type = INT4OID;
    break;
  case DataType::INT64:
    pg_type = INT8OID;
    break;
  case DataType::FLOAT:
    pg_type = FLOAT4OID;
    break;
  case DataType::DOUBLE:
    pg_type = FLOAT8OID;
    break;
  case DataType::STRING:
    pg_type = TEXTOID;
    break;
  default:
    break;
  }
  return YBCPgCreateTableAddColumn(handle, attr_name, attr_num, YBCPgFindTypeEntity(pg_type),
      is_hash, is_range, false /* is_desc */, false /* is_nulls_first */);
}

//--------------------------------------------------------------------------------------------------

YBCStatus YBCTestNewColumnRef(YBCPgStatement stmt, int attr_num, DataType yb_type,
                              YBCPgExpr *expr_handle) {
  int pg_type = 0;
  switch (yb_type) {
  case DataType::BOOL:
    pg_type = BOOLOID;
    break;
  case DataType::INT8:
    pg_type = CHAROID;
    break;
  case DataType::INT16:
    pg_type = INT2OID;
    break;
  case DataType::INT32:
    pg_type = INT4OID;
    break;
  case DataType::INT64:
    pg_type = INT8OID;
    break;
  case DataType::FLOAT:
    pg_type = FLOAT4OID;
    break;
  case DataType::DOUBLE:
    pg_type = FLOAT8OID;
    break;
  case DataType::STRING:
    pg_type = TEXTOID;
    break;
  default:
    break;
  }
  return YBCPgNewColumnRef(stmt, attr_num, YBCPgFindTypeEntity(pg_type),
                           false /* collate_is_valid_non_c */,
                           &kYBCTestTypeAttrs, expr_handle);
}

//--------------------------------------------------------------------------------------------------

YBCStatus YBCTestNewConstantBool(YBCPgStatement stmt, bool value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(BOOLOID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantInt1(YBCPgStatement stmt, int8_t value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(CHAROID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantInt2(YBCPgStatement stmt, int16_t value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(INT2OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantInt4(YBCPgStatement stmt, int32_t value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(INT4OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantInt8(YBCPgStatement stmt, int64_t value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(INT8OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantInt8Op(YBCPgStatement stmt, int64_t value, bool is_null,
                                 YBCPgExpr *expr_handle, bool is_gt) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(INT8OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstantOp(stmt, type_entity, false, nullptr, datum, is_null, expr_handle, is_gt);
}

YBCStatus YBCTestNewConstantFloat4(YBCPgStatement stmt, float value, bool is_null,
                                   YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(FLOAT4OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantFloat8(YBCPgStatement stmt, double value, bool is_null,
                                   YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(FLOAT8OID);
  Datum datum = type_entity->yb_to_datum(&value, 0, nullptr);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

YBCStatus YBCTestNewConstantText(YBCPgStatement stmt, const char *value, bool is_null,
                                 YBCPgExpr *expr_handle) {
  const YBCPgTypeEntity *type_entity = YBCPgFindTypeEntity(TEXTOID);
  Datum datum = type_entity->yb_to_datum(value, strlen(value), &kYBCTestTypeAttrs);
  return YBCPgNewConstant(stmt, type_entity, false, nullptr, datum, is_null, expr_handle);
}

} // namespace pggate
} // namespace yb
