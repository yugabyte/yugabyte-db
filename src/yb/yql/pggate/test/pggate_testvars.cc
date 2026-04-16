//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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

#include <initializer_list>

namespace yb::pggate {

/***************************************************************************************************
 * Conversion Functions.
 **************************************************************************************************/
/*
 * BOOL conversion.
 * Ignore the "bytes" data size.
 */
void YBCTestDatumToBool(Datum datum, void *void_data, int64 *bytes) {
  bool *data = reinterpret_cast<bool*>(void_data);
  *data = static_cast<bool>((datum & 0x000000ff) != 0);
}

Datum YBCTestBoolToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  const bool *data = reinterpret_cast<const bool*>(void_data);
  return (static_cast<Datum>(*data)) & 0x000000ff;
}

/*
 * CHAR conversion.
 * Ignore the "bytes" data size.
 */
void YBCTestDatumToChar(Datum datum, void *void_data, int64 *bytes) {
  char *data = reinterpret_cast<char*>(void_data);
  *data = static_cast<char>(datum & 0x000000ff);
}

Datum YBCTestCharToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  const char *data = reinterpret_cast<const char*>(void_data);
  return (static_cast<Datum>(*data)) & 0x000000ff;
}

/*
 * TEXT conversion.
 */
void YBCTestDatumToStr(Datum datum, void *void_data, int64 *bytes) {
  char **data = reinterpret_cast<char**>(void_data);
  *data = reinterpret_cast<char*>(datum);
  *bytes = strlen(*data);
}

Datum YBCTestStrToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  int64 len = type_attrs->typmod > 0 ? type_attrs->typmod : bytes + 1;
  char *str = static_cast<char *>(PggateTestAlloc(len));

  const char *data = reinterpret_cast<const char*>(void_data);
  strncpy(str, data, len - 1);
  str[len - 1] = 0;
  return reinterpret_cast<uint64_t>(str);
}

/*
 * INTEGERs conversion.
 */
void YBCTestDatumToInt16(Datum datum, void *void_data, int64 *bytes) {
  int16* data = reinterpret_cast<int16*>(void_data);
  *data = static_cast<int16>(datum & 0x0000ffff);
}

Datum YBCTestInt16ToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  const int16* data = reinterpret_cast<const int16*>(void_data);
  return (static_cast<Datum>(*data)) & 0x0000ffff;
}

void YBCTestDatumToInt32(Datum datum, void *void_data, int64 *bytes) {
  int32 *data = reinterpret_cast<int32*>(void_data);
  *data = static_cast<int32>(datum & 0xffffffff);
}

Datum YBCTestInt32ToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  const int32 *data = reinterpret_cast<const int32*>(void_data);
  return (static_cast<Datum>(*data)) & 0xffffffff;
}

void YBCTestDatumToInt64(Datum datum, void *void_data, int64 *bytes) {
  int64 *data = reinterpret_cast<int64*>(void_data);
  *data = static_cast<int64>(datum);
}

Datum YBCTestInt64ToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  const int64 *data = reinterpret_cast<const int64*>(void_data);
  return *reinterpret_cast<const Datum*>(data);
}

/*
 * FLOATs conversion.
 */
void YBCTestDatumToFloat4(Datum datum, void *void_data, int64 *bytes) {
  float *data = reinterpret_cast<float*>(void_data);
  *data = *reinterpret_cast<float *>(&datum);
}

Datum YBCTestFloat4ToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  return YBCTestInt32ToDatum(void_data, 0, nullptr);
}

void YBCTestDatumToFloat8(Datum datum, void *void_data, int64 *bytes) {
  double *data = reinterpret_cast<double*>(void_data);
  *data = *reinterpret_cast<double*>(&datum);
}

Datum YBCTestFloat8ToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  return YBCTestInt64ToDatum(void_data, 0, nullptr);
}

void YBCTestDatumToBinary(Datum datum, void *void_data, int64 *bytes) {
  CHECK(false) << "Not implemented yet";
}

Datum YBCTestBinaryToDatum(const void *void_data, int64 bytes, const YbcPgTypeAttrs *type_attrs) {
  CHECK(false) << "Not implemented yet";
  return 0;
}

/***************************************************************************************************
 * Conversion Table
 **************************************************************************************************/
constexpr std::initializer_list<YbcPgTypeEntity> kTypeEntityTable = {
  { BOOLOID, YB_YQL_DATA_TYPE_BOOL, true, 1, false,
    (YbcPgDatumToData)YBCTestDatumToBool,
    (YbcPgDatumFromData)YBCTestBoolToDatum },

  { INT2OID, YB_YQL_DATA_TYPE_INT16, true, 2, false,
    (YbcPgDatumToData)YBCTestDatumToInt16,
    (YbcPgDatumFromData)YBCTestInt16ToDatum },

  { INT4OID, YB_YQL_DATA_TYPE_INT32, true, 4, false,
    (YbcPgDatumToData)YBCTestDatumToInt32,
    (YbcPgDatumFromData)YBCTestInt32ToDatum },

  { INT8OID, YB_YQL_DATA_TYPE_INT64, true, 8, false,
    (YbcPgDatumToData)YBCTestDatumToInt64,
    (YbcPgDatumFromData)YBCTestInt64ToDatum },

  { TEXTOID, YB_YQL_DATA_TYPE_STRING, true, -1, false,
    (YbcPgDatumToData)YBCTestDatumToStr,
    (YbcPgDatumFromData)YBCTestStrToDatum },

  { OIDOID, YB_YQL_DATA_TYPE_INT32, true, 4, false,
    (YbcPgDatumToData)YBCTestDatumToInt32,
    (YbcPgDatumFromData)YBCTestInt32ToDatum },

  { FLOAT4OID, YB_YQL_DATA_TYPE_FLOAT, true, 8, false,
    (YbcPgDatumToData)YBCTestDatumToFloat4,
    (YbcPgDatumFromData)YBCTestFloat4ToDatum },

  { FLOAT8OID, YB_YQL_DATA_TYPE_DOUBLE, true, 8, false,
    (YbcPgDatumToData)YBCTestDatumToFloat8,
    (YbcPgDatumFromData)YBCTestFloat8ToDatum },

	{ BYTEAOID, YB_YQL_DATA_TYPE_BINARY, true, -1, false,
		(YbcPgDatumToData)YBCTestDatumToBinary,
		(YbcPgDatumFromData)YBCTestBinaryToDatum }
};

YbcPgTypeEntities YBCTestGetTypeTable() {
  return YbcPgTypeEntities{.data = &*kTypeEntityTable.begin(), .count = kTypeEntityTable.size()};
}

} // namespace yb::pggate
