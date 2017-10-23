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

#include "yb/ql/ptree/pt_type.h"
#include "yb/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTFloat::PTFloat(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kFloatValue, DataType::FLOAT, true>(memctx, loc),
      precision_(precision) {
}

PTFloat::~PTFloat() {
}

PTDouble::PTDouble(MemoryContext *memctx, YBLocation::SharedPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE, true>(memctx, loc),
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
  ql_type_ = QLType::CreateTypeMap(keys_type->ql_type(), values_type->ql_type());
}

PTMap::~PTMap() {
}

CHECKED_STATUS PTMap::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(keys_type_->Analyze(sem_context));
  RETURN_NOT_OK(values_type_->Analyze(sem_context));
  ql_type_ = QLType::CreateTypeMap(keys_type_->ql_type(), values_type_->ql_type());

  // Both key and value types cannot be collection.
  if (keys_type_->ql_type()->IsCollection() || keys_type_->ql_type()->IsUserDefined() ||
      values_type_->ql_type()->IsCollection() || values_type_->ql_type()->IsUserDefined()) {
    return sem_context->Error(this,
                              "Collection type parameters cannot be (un-frozen) collections "
                              "or UDTs",  ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Data types of map keys must be valid primary key types since they are encoded as keys in DocDB
  if (!keys_type_->IsApplicableForPrimaryKey()) {
    return sem_context->Error(this,
                              "Invalid datatype for map key or set element, "
                              "must be valid primary key type",
                              ErrorCode::INVALID_TABLE_DEFINITION);
  }

  return Status::OK();
}

PTSet::PTSet(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kSetValue, DataType::SET, false>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeSet(elems_type->ql_type());
}

PTSet::~PTSet() {
}

CHECKED_STATUS PTSet::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(elems_type_->Analyze(sem_context));
  ql_type_ = QLType::CreateTypeSet(elems_type_->ql_type());

  // Elems type cannot be collection.
  if (elems_type_->ql_type()->IsCollection() || elems_type_->ql_type()->IsUserDefined()) {
    return sem_context->Error(this,
                              "Collection type parameters cannot be (un-frozen) collections "
                              "or UDTs",  ErrorCode::INVALID_TABLE_DEFINITION);
  }

  // Data types of set elems must be valid primary key types since they are encoded as keys in DocDB
  if (!elems_type_->IsApplicableForPrimaryKey()) {
    return sem_context->Error(this,
                              "Invalid datatype for map key or set element, "
                              "must be valid primary key type",
                              ErrorCode::INVALID_TABLE_DEFINITION);
  }

  return Status::OK();
}

PTList::PTList(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kListValue, DataType::LIST, false>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeList(elems_type->ql_type());
}

PTList::~PTList() {
}

CHECKED_STATUS PTList::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(elems_type_->Analyze(sem_context));
  ql_type_ = QLType::CreateTypeList(elems_type_->ql_type());

  // Elems type cannot be collection.
  if (elems_type_->ql_type()->IsCollection() || elems_type_->ql_type()->IsUserDefined()) {
    return sem_context->Error(this,
                              "Collection type parameters cannot be (un-frozen) collections "
                              "or UDTs", ErrorCode::INVALID_TABLE_DEFINITION);
  }

  return Status::OK();
}

PTUserDefinedType::PTUserDefinedType(MemoryContext *memctx,
                                     YBLocation::SharedPtr loc,
                                     const PTQualifiedName::SharedPtr& name)
    : PTPrimitiveType<InternalType::kMapValue, DataType::USER_DEFINED_TYPE, false>(memctx, loc),
      name_(name) {
}

PTUserDefinedType::~PTUserDefinedType() {
}

CHECKED_STATUS PTUserDefinedType::Analyze(SemContext *sem_context) {

  RETURN_NOT_OK(name_->AnalyzeName(sem_context, OBJECT_TYPE));
  auto ybname = name_->ToTableName();
  ql_type_ = sem_context->GetUDType(ybname.namespace_name(), ybname.table_name());
  if (ql_type_ == nullptr) {
    return sem_context->Error(this, "Could not find user defined type", ErrorCode::TYPE_NOT_FOUND);
  }

  return Status::OK();
}

PTFrozen::PTFrozen(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kFrozenValue, DataType::FROZEN, true>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeFrozen(elems_type->ql_type());
}

PTFrozen::~PTFrozen() {
}

CHECKED_STATUS PTFrozen::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(elems_type_->Analyze(sem_context));
  ql_type_ = QLType::CreateTypeFrozen(elems_type_->ql_type());

  if (!elems_type_->ql_type()->IsCollection() && !elems_type_->ql_type()->IsUserDefined()) {
    return sem_context->Error(this, "Can only freeze collections or user defined types",
                              ErrorCode::INVALID_TABLE_DEFINITION);
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
