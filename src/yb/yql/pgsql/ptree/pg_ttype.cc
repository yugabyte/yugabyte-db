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
// Treenode definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_ttype.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
PgTBaseType::SharedPtr PgTBaseType::FromQLType(MemoryContext *memctx,
                                               const std::shared_ptr<QLType>& ql_type) {
  switch (ql_type->main()) {
    case DataType::INT8: return PgTTinyInt::MakeShared(memctx);
    case DataType::INT16: return PgTSmallInt::MakeShared(memctx);
    case DataType::INT32: return PgTInt::MakeShared(memctx);
    case DataType::INT64: return PgTBigInt::MakeShared(memctx);
    case DataType::STRING: return PgTVarchar::MakeShared(memctx);
    case DataType::BOOL: return PgTBoolean::MakeShared(memctx);
    case DataType::FLOAT: return PgTFloat::MakeShared(memctx);
    case DataType::DOUBLE: return PgTDouble::MakeShared(memctx);
    case DataType::TIMESTAMP: return PgTTimestamp::MakeShared(memctx);
    case DataType::DECIMAL: return PgTDecimal::MakeShared(memctx);
    case DataType::VARINT: return PgTVarInt::MakeShared(memctx);
    case DataType::UUID: return PgTUuid::MakeShared(memctx);
    case DataType::TIMEUUID: return PgTTimeUuid::MakeShared(memctx);

    case DataType::BINARY: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DataType::JSONB:
    case DataType::INET:
      // TODO: support conversion of complex type from ql_type to PT type.
      return nullptr;

    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::FROZEN:
      // Not supported these CQL type.
      return nullptr;

    case DataType::DATE: FALLTHROUGH_INTENDED;
    case DataType::TIME: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::NULL_VALUE_TYPE:
      FATAL_INVALID_ENUM_VALUE(DataType, ql_type->main());
  }
  FATAL_INVALID_ENUM_VALUE(DataType, ql_type->main());
}

//--------------------------------------------------------------------------------------------------

PgTFloat::PgTFloat(MemoryContext *memctx, PgTLocation::SharedPtr loc, int8_t precision)
    : PgTSimpleType<InternalType::kFloatValue, DataType::FLOAT, true>(memctx, loc),
      precision_(precision) {
}

PgTFloat::~PgTFloat() {
}

PgTDouble::PgTDouble(MemoryContext *memctx, PgTLocation::SharedPtr loc, int8_t precision)
    : PgTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE, true>(memctx, loc),
      precision_(precision) {
}

PgTDouble::~PgTDouble() {
}

//--------------------------------------------------------------------------------------------------

PgTCharBaseType::PgTCharBaseType(MemoryContext *memctx,
                               PgTLocation::SharedPtr loc,
                               int32_t max_length)
    : PgTSimpleType<InternalType::kStringValue, DataType::STRING>(memctx, loc),
      max_length_(max_length) {
}

PgTCharBaseType::~PgTCharBaseType() {
}

PgTChar::PgTChar(MemoryContext *memctx, PgTLocation::SharedPtr loc, int32_t max_length)
    : PgTCharBaseType(memctx, loc, max_length) {
}

PgTChar::~PgTChar() {
}

PgTVarchar::PgTVarchar(MemoryContext *memctx, PgTLocation::SharedPtr loc, int32_t max_length)
    : PgTCharBaseType(memctx, loc, max_length) {
}

PgTVarchar::~PgTVarchar() {
}

}  // namespace pgsql
}  // namespace yb
