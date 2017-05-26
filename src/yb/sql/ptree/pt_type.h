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

  virtual InternalType internal_type() const = 0;
  virtual std::shared_ptr<YQLType> yql_type() const = 0;
};

template<InternalType itype_, DataType data_type_>
class PTPrimitiveType : public PTBaseType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTPrimitiveType<itype_, data_type_>> SharedPtr;
  typedef MCSharedPtr<const PTPrimitiveType<itype_, data_type_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTPrimitiveType(MemoryContext *memctx = nullptr,
                           YBLocation::SharedPtr loc = nullptr)
      : PTBaseType(memctx, loc) {
  }
  virtual ~PTPrimitiveType() {
  }

  template<typename... TypeArgs>
  inline static PTPrimitiveType::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTPrimitiveType>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual InternalType internal_type() const {
    return itype_;
  }

  virtual DataType data_type() const {
    return data_type_;
  }

  virtual std::shared_ptr<YQLType> yql_type() const {
    // Since all instances of a primitive type share one static YQLType object, we can just call
    // "Create" to get the shared object.
    return YQLType::Create(data_type_);
  }
};

// types with no type arguments
template<InternalType itype_, DataType data_type_>
class PTSimpleType : public PTPrimitiveType<itype_, data_type_> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSimpleType<itype_, data_type_>> SharedPtr;
  typedef MCSharedPtr<const PTSimpleType<itype_, data_type_>> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTSimpleType(MemoryContext *memctx = nullptr,
      YBLocation::SharedPtr loc = nullptr) : PTPrimitiveType<itype_, data_type_>(memctx, loc) {
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

using PTVarInt = PTPrimitiveType<InternalType::kVarintValue, DataType::VARINT>;
using PTDecimal = PTPrimitiveType<InternalType::kDecimalValue, DataType::DECIMAL>;

class PTFloat : public PTSimpleType<InternalType::kFloatValue, DataType::FLOAT> {
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

class PTDouble : public PTSimpleType<InternalType::kDoubleValue, DataType::DOUBLE> {
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
    : public PTSimpleType<InternalType::kStringValue, DataType::STRING> {
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

class PTInet : public PTPrimitiveType<InternalType::kInetaddressValue, DataType::INET> {
 public:
  typedef MCSharedPtr<PTInet> SharedPtr;
  typedef MCSharedPtr<const PTInet> SharedPtrConst;

  explicit PTInet(MemoryContext *memctx = nullptr,
                       YBLocation::SharedPtr loc = nullptr);

  virtual ~PTInet();

  template<typename... TypeArgs>
  inline static PTInet::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTInet>(memctx, std::forward<TypeArgs>(args)...);
  }
};

class PTBlob : public PTPrimitiveType<InternalType::kBinaryValue, DataType::BINARY> {
 public:
  typedef MCSharedPtr<PTBlob> SharedPtr;
  typedef MCSharedPtr<const PTBlob> SharedPtrConst;

  explicit PTBlob(MemoryContext *memctx = nullptr,
                  YBLocation::SharedPtr loc = nullptr);

  virtual ~PTBlob();

  template<typename... TypeArgs>
  inline static PTBlob::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTBlob>(memctx, std::forward<TypeArgs>(args)...);
  }
};

//--------------------------------------------------------------------------------------------------
// UUID types.
class PTUuid : public PTPrimitiveType<InternalType::kUuidValue, DataType::UUID> {
 public:
  typedef MCSharedPtr<PTUuid> SharedPtr;
  typedef MCSharedPtr<const PTUuid> SharedPtrConst;

  explicit PTUuid(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr);

  virtual ~PTUuid();

  template<typename... TypeArgs>
  inline static PTUuid::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTUuid>(memctx, std::forward<TypeArgs>(args)...);
  }
};

class PTTimeUuid : public PTPrimitiveType<InternalType::kTimeuuidValue, DataType::TIMEUUID> {
 public:
  typedef MCSharedPtr<PTTimeUuid> SharedPtr;
  typedef MCSharedPtr<const PTTimeUuid> SharedPtrConst;

  explicit PTTimeUuid(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr);

  virtual ~PTTimeUuid();

  template<typename... TypeArgs>
  inline static PTTimeUuid::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTTimeUuid>(memctx, std::forward<TypeArgs>(args)...);
  }
};

//--------------------------------------------------------------------------------------------------
// Datetime types.

class PTTimestamp : public PTSimpleType<InternalType::kTimestampValue, DataType::TIMESTAMP> {
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

//--------------------------------------------------------------------------------------------------
// Collection types.
class PTMap : public PTPrimitiveType<InternalType::kMapValue, DataType::MAP> {
 public:
  typedef MCSharedPtr<PTMap> SharedPtr;
  typedef MCSharedPtr<const PTMap> SharedPtrConst;

  PTMap(MemoryContext *memctx,
        YBLocation::SharedPtr loc,
        const std::shared_ptr<YQLType>& keys_type = YQLType::Create(UNKNOWN_DATA),
        const std::shared_ptr<YQLType>& values_type = YQLType::Create(UNKNOWN_DATA));

  virtual ~PTMap();

  template<typename... TypeArgs>
  inline static PTMap::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<YQLType> yql_type() const {
    return yql_type_;
  }

 protected:
  std::shared_ptr<YQLType> yql_type_;
};

class PTSet : public PTPrimitiveType<InternalType::kSetValue, DataType::SET> {
 public:
  typedef MCSharedPtr<PTSet> SharedPtr;
  typedef MCSharedPtr<const PTSet> SharedPtrConst;

  PTSet(MemoryContext *memctx,
        YBLocation::SharedPtr loc,
        const std::shared_ptr<YQLType>& elems_type = YQLType::Create(UNKNOWN_DATA));

  virtual ~PTSet();

  template<typename... TypeArgs>
  inline static PTSet::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTSet>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<YQLType> yql_type() const {
    return yql_type_;
  }

 protected:
  std::shared_ptr<YQLType> yql_type_;
};

class PTList : public PTPrimitiveType<InternalType::kListValue, DataType::LIST> {
 public:
  typedef MCSharedPtr<PTList> SharedPtr;
  typedef MCSharedPtr<const PTList> SharedPtrConst;

  PTList(MemoryContext *memctx,
         YBLocation::SharedPtr loc,
         const std::shared_ptr<YQLType>& elems_type = YQLType::Create(UNKNOWN_DATA));

  virtual ~PTList();

  template<typename... TypeArgs>
  inline static PTList::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTList>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual std::shared_ptr<YQLType> yql_type() const {
    return yql_type_;
  }

 protected:
  std::shared_ptr<YQLType> yql_type_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_TYPE_H_
