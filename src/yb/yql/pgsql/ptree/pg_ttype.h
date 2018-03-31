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

#ifndef YB_YQL_PGSQL_PTREE_PG_TTYPE_H_
#define YB_YQL_PGSQL_PTREE_PG_TTYPE_H_

#include "yb/client/schema.h"

#include "yb/common/types.h"
#include "yb/common/ql_value.h"

#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"

namespace yb {
namespace pgsql {

class PgTBaseType : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTBaseType> SharedPtr;
  typedef MCSharedPtr<const PgTBaseType> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTBaseType(MemoryContext *memctx = nullptr, PgTLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PgTBaseType() {
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) {
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

  static PgTBaseType::SharedPtr FromQLType(MemoryContext *memctx,
                                          const std::shared_ptr<QLType>& ql_type);
};

template<InternalType itype_, DataType data_type_, bool applicable_for_primary_key_>
class PgTPrimitiveType : public PgTBaseType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>> SharedPtr;
  typedef MCSharedPtr<const PgTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>>
      SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTPrimitiveType(MemoryContext *memctx = nullptr,
                           PgTLocation::SharedPtr loc = nullptr)
      : PgTBaseType(memctx, loc) {
  }
  virtual ~PgTPrimitiveType() {
  }

  template<typename... TypeArgs>
  inline static PgTPrimitiveType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTPrimitiveType>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType internal_type() const {
    return itype_;
  }

  virtual DataType data_type() const {
    return data_type_;
  }

  virtual std::shared_ptr<QLType> ql_type() const {
    // Since all instances of a primitive type share one static QLType object, we can just call
    // "Create" to get the shared object.
    return QLType::Create(data_type_);
  }

  virtual bool IsApplicableForPrimaryKey() {
    return applicable_for_primary_key_;
  }
};

// types with no type arguments
template<InternalType itype_, DataType data_type_, bool applicable_for_primary_key_ = true>
class PgTSimpleType : public PgTPrimitiveType<itype_, data_type_, applicable_for_primary_key_> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTSimpleType<itype_, data_type_>> SharedPtr;
  typedef MCSharedPtr<const PgTSimpleType<itype_, data_type_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTSimpleType(MemoryContext *memctx = nullptr,
                        PgTLocation::SharedPtr loc = nullptr)
      : PgTPrimitiveType<itype_, data_type_, applicable_for_primary_key_>(memctx, loc) {
  }
  virtual ~PgTSimpleType() {
  }
};

//--------------------------------------------------------------------------------------------------
// Numeric Types.
using PgTBoolean = PgTSimpleType<InternalType::kBoolValue, DataType::BOOL, false>;
using PgTTinyInt = PgTSimpleType<InternalType::kInt8Value, DataType::INT8>;
using PgTSmallInt = PgTSimpleType<InternalType::kInt16Value, DataType::INT16>;
using PgTInt = PgTSimpleType<InternalType::kInt32Value, DataType::INT32>;
using PgTBigInt = PgTSimpleType<InternalType::kInt64Value, DataType::INT64>;

using PgTVarInt = PgTSimpleType<InternalType::kVarintValue, DataType::VARINT>;
using PgTDecimal = PgTSimpleType<InternalType::kDecimalValue, DataType::DECIMAL>;

class PgTFloat : public PgTSimpleType<InternalType::kFloatValue, DataType::FLOAT, true> {
 public:
  typedef MCSharedPtr<PgTFloat> SharedPtr;
  typedef MCSharedPtr<const PgTFloat> SharedPtrConst;

  explicit PgTFloat(MemoryContext *memctx,
                   PgTLocation::SharedPtr loc = nullptr,
                   int8_t precision = 24);
  virtual ~PgTFloat();

  template<typename... TypeArgs>
  inline static PgTFloat::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTFloat>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() const {
    return precision_;
  }

 private:
  int8_t precision_;
};

class PgTDouble : public PgTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE, true> {
 public:
  typedef MCSharedPtr<PgTDouble> SharedPtr;
  typedef MCSharedPtr<const PgTDouble> SharedPtrConst;

  explicit PgTDouble(MemoryContext *memctx,
                    PgTLocation::SharedPtr loc = nullptr,
                    int8_t precision = 24);
  virtual ~PgTDouble();

  template<typename... TypeArgs>
  inline static PgTDouble::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTDouble>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() const {
    return precision_;
  }

 private:
  int8_t precision_;
};

//--------------------------------------------------------------------------------------------------
// UUID types.
using PgTUuid = PgTSimpleType<InternalType::kUuidValue, DataType::UUID>;
using PgTTimeUuid = PgTSimpleType<InternalType::kTimeuuidValue, DataType::TIMEUUID>;

//--------------------------------------------------------------------------------------------------
// Char-based types.

class PgTCharBaseType
    : public PgTSimpleType<InternalType::kStringValue, DataType::STRING> {
 public:
  typedef MCSharedPtr<PgTCharBaseType> SharedPtr;
  typedef MCSharedPtr<const PgTCharBaseType> SharedPtrConst;

  explicit PgTCharBaseType(MemoryContext *memctx = nullptr,
                          PgTLocation::SharedPtr loc = nullptr,
                          int32_t max_length = -1);
  virtual ~PgTCharBaseType();

  int32_t max_length() {
    return max_length_;
  }
  void set_max_length(int32_t length) {
    max_length_ = length;
  }

 protected:
  int32_t max_length_;
};

class PgTChar : public PgTCharBaseType {
 public:
  typedef MCSharedPtr<PgTChar> SharedPtr;
  typedef MCSharedPtr<const PgTChar> SharedPtrConst;

  explicit PgTChar(MemoryContext *memctx = nullptr,
                  PgTLocation::SharedPtr loc = nullptr,
                  int32_t max_length = 1);
  virtual ~PgTChar();

  template<typename... TypeArgs>
  inline static PgTChar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTChar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType type_id() const {
    return InternalType::kStringValue;
  }
};

class PgTVarchar : public PgTCharBaseType {
 public:
  typedef MCSharedPtr<PgTVarchar> SharedPtr;
  typedef MCSharedPtr<const PgTVarchar> SharedPtrConst;

  explicit PgTVarchar(MemoryContext *memctx = nullptr,
                     PgTLocation::SharedPtr loc = nullptr,
                     int32_t max_length = 64*1024);
  virtual ~PgTVarchar();

  template<typename... TypeArgs>
  inline static PgTVarchar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTVarchar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType type_id() const {
    return InternalType::kStringValue;
  }
};

//--------------------------------------------------------------------------------------------------
// Datetime types.
using PgTTimestamp = PgTSimpleType<InternalType::kTimestampValue, DataType::TIMESTAMP>;

};  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TTYPE_H_
