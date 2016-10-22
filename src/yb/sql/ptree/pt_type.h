//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for datatypes.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_TYPE_H_
#define YB_SQL_PTREE_PT_TYPE_H_

#include <cstdlib>

#include "yb/sql/ptree/parse_tree.h"

namespace yb {
namespace sql {

enum class PTTypeId {
  kInt = 0,
  kTinyInt,
  kSmallInt,
  kBigInt,
  kFloat,
  kDouble,
  kBoolean,

  kCharBaseType,
  kChar,
  kVarchar,
};

class PTBaseType : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBaseType> SharedPtr;
  typedef MCSharedPtr<const PTBaseType> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTBaseType(MemoryContext *memctx = nullptr) {
  }
  virtual ~PTBaseType() {
  }
};

template<PTTypeId type_id_>
class PTPrimitiveType : public PTBaseType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimitiveType<type_id_>> SharedPtr;
  typedef MCSharedPtr<const PTPrimitiveType<type_id_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTPrimitiveType(MemoryContext *memctx = nullptr) {
  }
  virtual ~PTPrimitiveType() {
  }

  template<typename... TypeArgs>
  inline static PTPrimitiveType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimitiveType>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual PTTypeId type_id() {
    return type_id_;
  }
};

//--------------------------------------------------------------------------------------------------
// Numeric Types.

using PTBoolean = PTPrimitiveType<PTTypeId::kBoolean>;
using PTTinyInt = PTPrimitiveType<PTTypeId::kTinyInt>;
using PTSmallInt = PTPrimitiveType<PTTypeId::kSmallInt>;
using PTInt = PTPrimitiveType<PTTypeId::kInt>;
using PTBigInt = PTPrimitiveType<PTTypeId::kBigInt>;

class PTFloat : public PTPrimitiveType<PTTypeId::kFloat> {
 public:
  typedef MCSharedPtr<PTFloat> SharedPtr;
  typedef MCSharedPtr<const PTFloat> SharedPtrConst;

  explicit PTFloat(MemoryContext *memctx = nullptr, int8_t precision = 24);
  virtual ~PTFloat();

  template<typename... TypeArgs>
  inline static PTFloat::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTFloat>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() {
    return precision_;
  }

 private:
  int8_t precision_;
};

class PTDouble : public PTPrimitiveType<PTTypeId::kDouble> {
 public:
  typedef MCSharedPtr<PTDouble> SharedPtr;
  typedef MCSharedPtr<const PTDouble> SharedPtrConst;

  explicit PTDouble(MemoryContext *memctx = nullptr, int8_t precision = 24);
  virtual ~PTDouble();

  template<typename... TypeArgs>
  inline static PTDouble::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTDouble>(memctx, std::forward<TypeArgs>(args)...);
  }

  int8_t precision() {
    return precision_;
  }

 private:
  int8_t precision_;
};

//--------------------------------------------------------------------------------------------------
// Char-based types.

class PTCharBaseType : public PTPrimitiveType<PTTypeId::kCharBaseType> {
 public:
  typedef MCSharedPtr<PTCharBaseType> SharedPtr;
  typedef MCSharedPtr<const PTCharBaseType> SharedPtrConst;

  explicit PTCharBaseType(MemoryContext *memctx = nullptr, int32_t max_length = -1);
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

  explicit PTChar(MemoryContext *memctx = nullptr, int32_t max_length = 30);
  virtual ~PTChar();

  template<typename... TypeArgs>
  inline static PTChar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTChar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual PTTypeId type_id() {
    return PTTypeId::kChar;
  }
};

class PTVarchar : public PTCharBaseType {
 public:
  typedef MCSharedPtr<PTVarchar> SharedPtr;
  typedef MCSharedPtr<const PTVarchar> SharedPtrConst;

  explicit PTVarchar(MemoryContext *memctx = nullptr, int32_t max_length = 64*1024);
  virtual ~PTVarchar();

  template<typename... TypeArgs>
  inline static PTVarchar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTVarchar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual PTTypeId type_id() {
    return PTTypeId::kVarchar;
  }
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_TYPE_H_
