//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_TYPE_H_
#define YB_SQL_PTREE_PT_TYPE_H_

#include "yb/client/client.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/common/types.h"
#include "yb/common/yql_value.h"

namespace yb {
namespace sql {

class PTBaseType : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBaseType> SharedPtr;
  typedef MCSharedPtr<const PTBaseType> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTBaseType(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTBaseType() {
  }

  virtual InternalType type_id() const = 0;
  virtual DataType sql_type() const = 0;
};

template<InternalType type_id_, DataType sql_type_>
class PTPrimitiveType : public PTBaseType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimitiveType<type_id_, sql_type_>> SharedPtr;
  typedef MCSharedPtr<const PTPrimitiveType<type_id_, sql_type_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTPrimitiveType(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : PTBaseType(memctx, loc) {
  }
  virtual ~PTPrimitiveType() {
  }

  template<typename... TypeArgs>
  inline static PTPrimitiveType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimitiveType>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType type_id() const {
    return type_id_;
  }

  virtual DataType sql_type() const {
    return sql_type_;
  }
};

//--------------------------------------------------------------------------------------------------
// Numeric Types.

using PTBoolean = PTPrimitiveType<InternalType::kBoolValue, DataType::BOOL>;
using PTTinyInt = PTPrimitiveType<InternalType::kInt8Value, DataType::INT8>;
using PTSmallInt = PTPrimitiveType<InternalType::kInt16Value, DataType::INT16>;
using PTInt = PTPrimitiveType<InternalType::kInt32Value, DataType::INT32>;
using PTBigInt = PTPrimitiveType<InternalType::kInt64Value, DataType::INT64>;

class PTFloat : public PTPrimitiveType<InternalType::kFloatValue, DataType::FLOAT> {
 public:
  typedef MCSharedPtr<PTFloat> SharedPtr;
  typedef MCSharedPtr<const PTFloat> SharedPtrConst;

  explicit PTFloat(MemoryContext *memctx = nullptr,
                   YBLocation::SharedPtr loc = nullptr,
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

class PTDouble : public PTPrimitiveType<InternalType::kDoubleValue, DataType::DOUBLE> {
 public:
  typedef MCSharedPtr<PTDouble> SharedPtr;
  typedef MCSharedPtr<const PTDouble> SharedPtrConst;

  explicit PTDouble(MemoryContext *memctx = nullptr,
                    YBLocation::SharedPtr loc = nullptr,
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
// Char-based types.

class PTCharBaseType
    : public PTPrimitiveType<InternalType::kStringValue, DataType::STRING> {
 public:
  typedef MCSharedPtr<PTCharBaseType> SharedPtr;
  typedef MCSharedPtr<const PTCharBaseType> SharedPtrConst;

  explicit PTCharBaseType(MemoryContext *memctx = nullptr,
                          YBLocation::SharedPtr loc = nullptr,
                          int32_t max_length = -1);
  virtual ~PTCharBaseType();

  int32_t max_length() {
    return max_length_;
  }
  void set_max_length(int32_t length) {
    max_length_ = length;
  }

 protected:
  int32_t max_length_;
};

class PTChar : public PTCharBaseType {
 public:
  typedef MCSharedPtr<PTChar> SharedPtr;
  typedef MCSharedPtr<const PTChar> SharedPtrConst;

  explicit PTChar(MemoryContext *memctx = nullptr,
                  YBLocation::SharedPtr loc = nullptr,
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
                     YBLocation::SharedPtr loc = nullptr,
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

//--------------------------------------------------------------------------------------------------
// Datetime types.

class PTTimestamp : public PTPrimitiveType<InternalType::kTimestampValue,
    DataType::TIMESTAMP> {
 public:
  typedef MCSharedPtr<PTTimestamp> SharedPtr;
  typedef MCSharedPtr<const PTTimestamp> SharedPtrConst;

  explicit PTTimestamp(MemoryContext *memctx = nullptr,
                       YBLocation::SharedPtr loc = nullptr);

  virtual ~PTTimestamp();

  template<typename... TypeArgs>
  inline static PTTimestamp::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTimestamp>(memctx, std::forward<TypeArgs>(args)...);
  }
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_TYPE_H_
