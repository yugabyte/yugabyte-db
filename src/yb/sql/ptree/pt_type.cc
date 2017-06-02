//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTFloat::PTFloat(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kFloatValue, DataType::FLOAT, false>(memctx, loc),
      precision_(precision) {
}

PTFloat::~PTFloat() {
}

PTDouble::PTDouble(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE, false>(memctx, loc),
      precision_(precision) {
}

PTDouble::~PTDouble() {
}

//--------------------------------------------------------------------------------------------------

PTCounter::PTCounter(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTSimpleType<InternalType::kInt64Value, DataType::INT64, false>(memctx, loc) {
}

PTCounter::~PTCounter() {
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

//--------------------------------------------------------------------------------------------------

PTMap::PTMap(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTBaseType::SharedPtr& keys_type,
             const PTBaseType::SharedPtr& values_type)
    : PTPrimitiveType<InternalType::kMapValue, DataType::MAP, false>(memctx, loc),
      keys_type_(keys_type),
      values_type_(values_type) {
  yql_type_ = YQLType::CreateTypeMap(keys_type->yql_type()->main(),
                                     values_type->yql_type()->main());
}

PTMap::~PTMap() {
}

CHECKED_STATUS PTMap::IsValid(SemContext *sem_context) {
  // Both key and value types cannot be collection.
  if (keys_type_->yql_type()->IsParametric() || values_type_->yql_type()->IsParametric()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_DEFINITION,
                              "Nested Collections Are Not Supported Yet");
  }

  // Data types of map keys must be valid primary key types since they are encoded as keys in DocDB
  if (!keys_type_->IsApplicableForPrimaryKey()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_DEFINITION,
        "Invalid datatype for map key or set element, must be valid primary key type");
  }

  return Status::OK();
}

PTSet::PTSet(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kSetValue, DataType::SET, false>(memctx, loc),
      elems_type_(elems_type) {
  yql_type_ = YQLType::CreateTypeSet(elems_type->yql_type()->main());
}

PTSet::~PTSet() {
}

CHECKED_STATUS PTSet::IsValid(SemContext *sem_context) {
  // Both key and value types cannot be collection.
  if (elems_type_->yql_type()->IsParametric()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_DEFINITION,
                              "Nested Collections Are Not Supported Yet");
  }

  // Data types of set elems must be valid primary key types since they are encoded as keys in DocDB
  if (!elems_type_->IsApplicableForPrimaryKey()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_DEFINITION,
        "Invalid datatype for map key or set element, must be valid primary key type");
  }

  return Status::OK();
}

PTList::PTList(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kListValue, DataType::LIST, false>(memctx, loc),
      elems_type_(elems_type) {
  yql_type_ = YQLType::CreateTypeList(elems_type->yql_type()->main());
}

PTList::~PTList() {
}

CHECKED_STATUS PTList::IsValid(SemContext *sem_context) {
  // Both key and value types cannot be collection.
  if (elems_type_->yql_type()->IsParametric()) {
    return sem_context->Error(loc(), ErrorCode::INVALID_TABLE_DEFINITION,
                              "Nested Collections Are Not Supported Yet");
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
