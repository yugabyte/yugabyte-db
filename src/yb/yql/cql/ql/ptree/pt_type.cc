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

#include "yb/yql/cql/ql/ptree/pt_type.h"

#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
PTBaseType::SharedPtr PTBaseType::FromQLType(MemoryContext *memctx,
                                             const std::shared_ptr<QLType>& ql_type) {
  switch (ql_type->main()) {
    case DataType::INT8: return PTTinyInt::MakeShared(memctx);
    case DataType::INT16: return PTSmallInt::MakeShared(memctx);
    case DataType::INT32: return PTInt::MakeShared(memctx);
    case DataType::INT64: return PTBigInt::MakeShared(memctx);
    case DataType::STRING: return PTVarchar::MakeShared(memctx);
    case DataType::BOOL: return PTBoolean::MakeShared(memctx);
    case DataType::FLOAT: return PTFloat::MakeShared(memctx);
    case DataType::DOUBLE: return PTDouble::MakeShared(memctx);
    case DataType::BINARY: return PTBlob::MakeShared(memctx);
    case DataType::TIMESTAMP: return PTTimestamp::MakeShared(memctx);
    case DataType::DATE: return PTDate::MakeShared(memctx);
    case DataType::TIME: return PTTime::MakeShared(memctx);
    case DataType::DECIMAL: return PTDecimal::MakeShared(memctx);
    case DataType::VARINT: return PTVarInt::MakeShared(memctx);
    case DataType::INET: return PTInet::MakeShared(memctx);
    case DataType::JSONB: return PTJsonb::MakeShared(memctx);
    case DataType::UUID: return PTUuid::MakeShared(memctx);
    case DataType::TIMEUUID: return PTTimeUuid::MakeShared(memctx);

    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: FALLTHROUGH_INTENDED;
    case DataType::FROZEN:
      // TODO: support conversion of complex type from ql_type to PT type.
      return nullptr;

    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::TYPEARGS: FALLTHROUGH_INTENDED;
    case DataType::UINT8: FALLTHROUGH_INTENDED;
    case DataType::UINT16: FALLTHROUGH_INTENDED;
    case DataType::UINT32: FALLTHROUGH_INTENDED;
    case DataType::UINT64: FALLTHROUGH_INTENDED;
    case DataType::GIN_NULL: FALLTHROUGH_INTENDED;
    case DataType::UNKNOWN_DATA: FALLTHROUGH_INTENDED;
    case DataType::NULL_VALUE_TYPE:
      FATAL_INVALID_ENUM_VALUE(DataType, ql_type->main());
  }
  FATAL_INVALID_ENUM_VALUE(DataType, ql_type->main());
}

//--------------------------------------------------------------------------------------------------

PTFloat::PTFloat(MemoryContext *memctx, YBLocationPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kFloatValue, DataType::FLOAT, true>(memctx, loc),
      precision_(precision) {
}

PTFloat::~PTFloat() {
}

PTDouble::PTDouble(MemoryContext *memctx, YBLocationPtr loc, int8_t precision)
    : PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE, true>(memctx, loc),
      precision_(precision) {
}

PTDouble::~PTDouble() {
}

//--------------------------------------------------------------------------------------------------

PTCounter::PTCounter(MemoryContext *memctx, YBLocationPtr loc)
    : PTSimpleType<InternalType::kInt64Value, DataType::INT64, false>(memctx, loc) {
}

PTCounter::~PTCounter() {
}

//--------------------------------------------------------------------------------------------------

PTCharBaseType::PTCharBaseType(MemoryContext *memctx,
                               YBLocationPtr loc,
                               ssize_t max_length)
    : PTSimpleType<InternalType::kStringValue, DataType::STRING>(memctx, loc),
      max_length_(max_length) {
}

PTCharBaseType::~PTCharBaseType() {
}

PTChar::PTChar(MemoryContext *memctx, YBLocationPtr loc, int32_t max_length)
    : PTCharBaseType(memctx, loc, max_length) {
}

PTChar::~PTChar() {
}

PTVarchar::PTVarchar(MemoryContext *memctx, YBLocationPtr loc, int32_t max_length)
    : PTCharBaseType(memctx, loc, max_length) {
}

PTVarchar::~PTVarchar() {
}

//--------------------------------------------------------------------------------------------------

PTMap::PTMap(MemoryContext *memctx,
             YBLocationPtr loc,
             const PTBaseType::SharedPtr& keys_type,
             const PTBaseType::SharedPtr& values_type)
    : PTPrimitiveType<InternalType::kMapValue, DataType::MAP, false>(memctx, loc),
      keys_type_(keys_type),
      values_type_(values_type) {
  ql_type_ = QLType::CreateTypeMap(keys_type->ql_type(), values_type->ql_type());
}

PTMap::~PTMap() {
}

Status PTMap::Analyze(SemContext *sem_context) {
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
             YBLocationPtr loc,
             const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kSetValue, DataType::SET, false>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeSet(elems_type->ql_type());
}

PTSet::~PTSet() {
}

Status PTSet::Analyze(SemContext *sem_context) {
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
               YBLocationPtr loc,
               const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kListValue, DataType::LIST, false>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeList(elems_type->ql_type());
}

PTList::~PTList() {
}

Status PTList::Analyze(SemContext *sem_context) {
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
                                     YBLocationPtr loc,
                                     const PTQualifiedName::SharedPtr& name)
    : PTPrimitiveType<InternalType::kMapValue, DataType::USER_DEFINED_TYPE, false>(memctx, loc),
      name_(name) {
}

PTUserDefinedType::~PTUserDefinedType() {
}

Status PTUserDefinedType::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(name_->AnalyzeName(sem_context, ObjectType::TYPE));
  auto ybname = name_->ToTableName();
  ql_type_ = sem_context->GetUDType(ybname.namespace_name(), ybname.table_name());
  if (ql_type_ == nullptr) {
    return sem_context->Error(this, "Could not find user defined type", ErrorCode::TYPE_NOT_FOUND);
  }

  return Status::OK();
}

PTFrozen::PTFrozen(MemoryContext *memctx,
                   YBLocationPtr loc,
                   const PTBaseType::SharedPtr& elems_type)
    : PTPrimitiveType<InternalType::kFrozenValue, DataType::FROZEN, true>(memctx, loc),
      elems_type_(elems_type) {
  ql_type_ = QLType::CreateTypeFrozen(elems_type->ql_type());
}

PTFrozen::~PTFrozen() {
}

Status PTFrozen::Analyze(SemContext *sem_context) {
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
