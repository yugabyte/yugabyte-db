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
// Tree node definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/client/schema.h"

#include "yb/common/ql_type.h"

#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

class PTBaseType : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBaseType> SharedPtr;
  typedef MCSharedPtr<const PTBaseType> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTBaseType(MemoryContext *memctx = nullptr, YBLocationPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTBaseType() {
  }

  virtual Status Analyze(SemContext *sem_context) {
    return Status::OK();
  }

  virtual bool IsApplicableForPrimaryKey() {
    return true;
  }

  virtual bool is_counter() const {
    return false;
  }

  virtual InternalType internal_type() const = 0;
  virtual std::shared_ptr<QLType> ql_type() const = 0;

  static PTBaseType::SharedPtr FromQLType(MemoryContext *memctx,
                                          const std::shared_ptr<QLType>& ql_type);
};

template<InternalType itype_, DataType data_type_, bool applicable_for_primary_key_>
class PTPrimitiveType : public PTBaseType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>> SharedPtr;
  typedef MCSharedPtr<const PTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>>
      SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTPrimitiveType(MemoryContext *memctx = nullptr,
                           YBLocationPtr loc = nullptr)
      : PTBaseType(memctx, loc) {
  }
  virtual ~PTPrimitiveType() {
  }

  template<typename... TypeArgs>
  inline static PTPrimitiveType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimitiveType>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTPrimitiveType;
  }

  virtual InternalType internal_type() const override {
    return itype_;
  }

  virtual DataType data_type() const {
    return data_type_;
  }

  virtual std::shared_ptr<QLType> ql_type() const override {
    // Since all instances of a primitive type share one static QLType object, we can just call
    // "Create" to get the shared object.
    return QLType::Create(data_type_);
  }

  virtual bool IsApplicableForPrimaryKey() override {
    return applicable_for_primary_key_;
  }
};

// types with no type arguments
template<InternalType itype_, DataType data_type_, bool applicable_for_primary_key_ = true>
class PTSimpleType : public PTPrimitiveType<itype_, data_type_, applicable_for_primary_key_> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSimpleType<itype_, data_type_>> SharedPtr;
  typedef MCSharedPtr<const PTSimpleType<itype_, data_type_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTSimpleType(MemoryContext *memctx = nullptr,
                        YBLocationPtr loc = nullptr)
      : PTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>(memctx, loc) {
  }
  virtual ~PTSimpleType() {
  }
};

//--------------------------------------------------------------------------------------------------
// Numeric Types.
using PTBoolean = PTSimpleType<InternalType::kBoolValue, DataType::BOOL>;
using PTTinyInt = PTSimpleType<InternalType::kInt8Value, DataType::INT8>;
using PTSmallInt = PTSimpleType<InternalType::kInt16Value, DataType::INT16>;
using PTInt = PTSimpleType<InternalType::kInt32Value, DataType::INT32>;
using PTBigInt = PTSimpleType<InternalType::kInt64Value, DataType::INT64>;

using PTVarInt = PTSimpleType<InternalType::kVarintValue, DataType::VARINT>;
using PTDecimal = PTSimpleType<InternalType::kDecimalValue, DataType::DECIMAL>;

class PTFloat : public PTSimpleType<InternalType::kFloatValue, DataType::FLOAT> {
 public:
  typedef MCSharedPtr<PTFloat> SharedPtr;
  typedef MCSharedPtr<const PTFloat> SharedPtrConst;

  explicit PTFloat(MemoryContext *memctx,
                   YBLocationPtr loc = nullptr,
                   int8_t precision = 24);
  virtual ~PTFloat();

  template<typename... TypeArgs>
  inline static PTFloat::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTFloat>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() const {
    return precision_;
  }

 private:
  int8_t precision_;
};

class PTDouble : public PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE> {
 public:
  typedef MCSharedPtr<PTDouble> SharedPtr;
  typedef MCSharedPtr<const PTDouble> SharedPtrConst;

  explicit PTDouble(MemoryContext *memctx,
                    YBLocationPtr loc = nullptr,
                    int8_t precision = 24);
  virtual ~PTDouble();

  template<typename... TypeArgs>
  inline static PTDouble::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDouble>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() const {
    return precision_;
  }

 private:
  int8_t precision_;
};

//--------------------------------------------------------------------------------------------------
// Counter Types.
class PTCounter : public PTSimpleType<InternalType::kInt64Value, DataType::INT64, false> {
 public:
  typedef MCSharedPtr<PTCounter> SharedPtr;
  typedef MCSharedPtr<const PTCounter> SharedPtrConst;

  explicit PTCounter(MemoryContext *memctx, YBLocationPtr loc);
  virtual ~PTCounter();

  template<typename... TypeArgs>
  inline static PTCounter::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCounter>(memctx, std::forward<TypeArgs>(args)...);
  }

  bool is_counter() const {
    return true;
  }
};

//--------------------------------------------------------------------------------------------------
// Char-based types.

class PTCharBaseType : public PTSimpleType<InternalType::kStringValue, DataType::STRING> {
 public:
  typedef MCSharedPtr<PTCharBaseType> SharedPtr;
  typedef MCSharedPtr<const PTCharBaseType> SharedPtrConst;

  explicit PTCharBaseType(MemoryContext *memctx = nullptr,
                          YBLocationPtr loc = nullptr,
                          ssize_t max_length = -1);
  virtual ~PTCharBaseType();

  ssize_t max_length() {
    return max_length_;
  }

  void set_max_length(ssize_t length) {
    max_length_ = length;
  }

 protected:
  ssize_t max_length_;
};

class PTChar : public PTCharBaseType {
 public:
  typedef MCSharedPtr<PTChar> SharedPtr;
  typedef MCSharedPtr<const PTChar> SharedPtrConst;

  explicit PTChar(MemoryContext *memctx = nullptr,
                  YBLocationPtr loc = nullptr,
                  int32_t max_length = 1);
  virtual ~PTChar();

  template<typename... TypeArgs>
  inline static PTChar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTChar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType type_id() const {
    return InternalType::kStringValue;
  }
};

class PTVarchar : public PTCharBaseType {
 public:
  typedef MCSharedPtr<PTVarchar> SharedPtr;
  typedef MCSharedPtr<const PTVarchar> SharedPtrConst;

  explicit PTVarchar(MemoryContext *memctx = nullptr,
                     YBLocationPtr loc = nullptr,
                     int32_t max_length = 64*1024);
  virtual ~PTVarchar();

  template<typename... TypeArgs>
  inline static PTVarchar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTVarchar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType type_id() const {
    return InternalType::kStringValue;
  }
};

// Other char-based types.
using PTJsonb = PTSimpleType<InternalType::kJsonbValue, DataType::JSONB, false>;
using PTInet = PTSimpleType<InternalType::kInetaddressValue, DataType::INET>;
using PTBlob = PTSimpleType<InternalType::kBinaryValue, DataType::BINARY>;

//--------------------------------------------------------------------------------------------------
// UUID types.
using PTUuid = PTSimpleType<InternalType::kUuidValue, DataType::UUID>;
using PTTimeUuid = PTSimpleType<InternalType::kTimeuuidValue, DataType::TIMEUUID>;

//--------------------------------------------------------------------------------------------------
// Datetime types.
using PTTimestamp = PTSimpleType<InternalType::kTimestampValue, DataType::TIMESTAMP>;
using PTDate = PTSimpleType<InternalType::kDateValue, DataType::DATE>;
using PTTime = PTSimpleType<InternalType::kTimeValue, DataType::TIME>;

//--------------------------------------------------------------------------------------------------
// Collection types.
class PTMap : public PTPrimitiveType<InternalType::kMapValue, DataType::MAP, false> {
 public:
  typedef MCSharedPtr<PTMap> SharedPtr;
  typedef MCSharedPtr<const PTMap> SharedPtrConst;

  PTMap(MemoryContext *memctx,
        YBLocationPtr loc,
        const PTBaseType::SharedPtr& keys_type,
        const PTBaseType::SharedPtr& values_type);

  virtual ~PTMap();

  template<typename... TypeArgs>
  inline static PTMap::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

  virtual Status Analyze(SemContext *sem_context);

 protected:
  PTBaseType::SharedPtr keys_type_;
  PTBaseType::SharedPtr values_type_;
  std::shared_ptr<QLType> ql_type_;
};

class PTSet : public PTPrimitiveType<InternalType::kSetValue, DataType::SET, false> {
 public:
  typedef MCSharedPtr<PTSet> SharedPtr;
  typedef MCSharedPtr<const PTSet> SharedPtrConst;

  PTSet(MemoryContext *memctx,
        YBLocationPtr loc,
        const PTBaseType::SharedPtr& elems_type);

  virtual ~PTSet();

  template<typename... TypeArgs>
  inline static PTSet::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTSet>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

  virtual Status Analyze(SemContext *sem_context);

 protected:
  PTBaseType::SharedPtr elems_type_;
  std::shared_ptr<QLType> ql_type_;
};

class PTList : public PTPrimitiveType<InternalType::kListValue, DataType::LIST, false> {
 public:
  typedef MCSharedPtr<PTList> SharedPtr;
  typedef MCSharedPtr<const PTList> SharedPtrConst;

  PTList(MemoryContext *memctx,
         YBLocationPtr loc,
         const PTBaseType::SharedPtr& elems_type);

  virtual ~PTList();

  template<typename... TypeArgs>
  inline static PTList::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTList>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

  virtual Status Analyze(SemContext *sem_context);

 protected:
  PTBaseType::SharedPtr elems_type_;
  std::shared_ptr<QLType> ql_type_;
};

class PTUserDefinedType : public PTPrimitiveType<InternalType::kMapValue,
                                                 DataType::USER_DEFINED_TYPE,
                                                 false> {
 public:
  typedef MCSharedPtr<PTUserDefinedType> SharedPtr;
  typedef MCSharedPtr<const PTUserDefinedType> SharedPtrConst;

  PTUserDefinedType(MemoryContext *memctx,
                    YBLocationPtr loc,
                    const PTQualifiedName::SharedPtr& name);

  virtual ~PTUserDefinedType();

  template<typename... TypeArgs>
  inline static PTUserDefinedType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTUserDefinedType>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context);

  virtual std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

 private:
  PTQualifiedName::SharedPtr name_;

  // These fields will be set during analysis (retrieving type info from metadata server)
  std::shared_ptr<QLType> ql_type_;
};

class PTFrozen : public PTPrimitiveType<InternalType::kFrozenValue, DataType::FROZEN, true> {
 public:
  typedef MCSharedPtr<PTFrozen> SharedPtr;
  typedef MCSharedPtr<const PTFrozen> SharedPtrConst;

  PTFrozen(MemoryContext *memctx,
           YBLocationPtr loc,
           const PTBaseType::SharedPtr& elems_type);

  virtual ~PTFrozen();

  template<typename... TypeArgs>
  inline static PTFrozen::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTFrozen>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context);

  virtual std::shared_ptr<QLType> ql_type() const {
    return ql_type_;
  }

 private:
  PTBaseType::SharedPtr elems_type_;
  std::shared_ptr<QLType> ql_type_;
};

}  // namespace ql
}  // namespace yb
