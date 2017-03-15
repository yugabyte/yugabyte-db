//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_type.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTFloat::PTFloat(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kFloatValue, DataType::FLOAT>(memctx, loc),
      precision_(precision) {
}

PTFloat::~PTFloat() {
}

PTDouble::PTDouble(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE>(memctx, loc),
      precision_(precision) {
}

PTDouble::~PTDouble() {
}

//--------------------------------------------------------------------------------------------------

PTCharBaseType::PTCharBaseType(MemoryContext *memctx,
                               YBLocation::SharedPtr loc,
                               int32_t max_length)
    : PTSimpleType<InternalType::kStringValue, DataType::STRING>(memctx, loc),
      max_length_(max_length) {
}

PTCharBaseType::~PTCharBaseType() {
}

PTChar::PTChar(MemoryContext *memctx, YBLocation::SharedPtr loc, int32_t max_length)
    : PTCharBaseType(memctx, loc, max_length) {
}

PTChar::~PTChar() {
}

PTVarchar::PTVarchar(MemoryContext *memctx, YBLocation::SharedPtr loc, int32_t max_length)
    : PTCharBaseType(memctx, loc, max_length) {
}

PTVarchar::~PTVarchar() {
}

PTTimestamp::PTTimestamp(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTSimpleType<InternalType::kTimestampValue, DataType::TIMESTAMP>(memctx, loc) {
}

PTTimestamp::~PTTimestamp() {
}

PTInet::PTInet(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTPrimitiveType<InternalType::kInetaddressValue, DataType::INET>(memctx, loc) {
}

PTInet::~PTInet() {
}

PTMap::PTMap(MemoryContext *memctx, YBLocation::SharedPtr loc,
    YQLType keys_type, YQLType values_type)
    : PTPrimitiveType<InternalType::kMapValue, DataType::MAP>(memctx, loc) {
  type_params_.push_back(keys_type);
  type_params_.push_back(values_type);
}

PTMap::~PTMap() {
}


PTSet::PTSet(MemoryContext *memctx, YBLocation::SharedPtr loc, YQLType elems_type)
    : PTPrimitiveType<InternalType::kSetValue, DataType::SET>(memctx, loc) {
  type_params_.push_back(elems_type);
}

PTSet::~PTSet() {
}


PTList::PTList(MemoryContext *memctx, YBLocation::SharedPtr loc, YQLType elems_type)
    : PTPrimitiveType<InternalType::kListValue, DataType::LIST>(memctx, loc) {
  type_params_.push_back(elems_type);
}

PTList::~PTList() {
}

}  // namespace sql
}  // namespace yb
