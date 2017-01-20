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
    : PTPrimitiveType<yb::DataType::FLOAT, client::YBColumnSchema::FLOAT>(memctx, loc),
      precision_(precision) {
}

PTFloat::~PTFloat() {
}

PTDouble::PTDouble(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTPrimitiveType<yb::DataType::DOUBLE, client::YBColumnSchema::DOUBLE>(memctx, loc),
      precision_(precision) {
}

PTDouble::~PTDouble() {
}

//--------------------------------------------------------------------------------------------------

PTCharBaseType::PTCharBaseType(MemoryContext *memctx,
                               YBLocation::SharedPtr loc,
                               int32_t max_length)
    : PTPrimitiveType<yb::DataType::STRING, client::YBColumnSchema::STRING>(memctx, loc),
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
    : PTPrimitiveType<yb::DataType::TIMESTAMP, client::YBColumnSchema::TIMESTAMP>(memctx, loc) {
}

PTTimestamp::~PTTimestamp() {
}

}  // namespace sql
}  // namespace yb
