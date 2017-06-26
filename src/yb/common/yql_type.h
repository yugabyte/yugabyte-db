//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module is to define a few supporting functions for YQLTYPE.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_YQL_TYPE_H_
#define YB_COMMON_YQL_TYPE_H_

#include <glog/logging.h>

#include "yb/common/key_encoder.h"
#include "yb/common/common.pb.h"
#include "yb/util/status.h"

namespace yb {

class YQLType {
 public:
  //------------------------------------------------------------------------------------------------
  // The Create() functions are to construct YQLType objects.
  template<DataType data_type>
  static const std::shared_ptr<YQLType>& CreatePrimitiveType() {
    static std::shared_ptr<YQLType> yql_type = std::make_shared<YQLType>(data_type);
    return yql_type;
  }

  template<DataType data_type>
  static std::shared_ptr<YQLType> CreateCollectionType(
      const vector<std::shared_ptr<YQLType>>& params) {
    return std::make_shared<YQLType>(data_type, params);
  }

  // Create all builtin types including collection.
  static std::shared_ptr<YQLType> Create(DataType data_type,
                                         const vector<std::shared_ptr<YQLType>>& params);

  // Create primitive types, all builtin types except collection.
  static std::shared_ptr<YQLType> Create(DataType data_type);

  // Check type methods.
  static bool IsValidPrimaryType(DataType type);

  // Create map datatype.
  static std::shared_ptr<YQLType> CreateTypeMap(DataType key_type, DataType value_type);
  static std::shared_ptr<YQLType> CreateTypeMap() {
    // Create default map type: MAP <UNKNOWN -> UNKNOWN>.
    static const std::shared_ptr<YQLType> default_map =
      CreateTypeMap(DataType::UNKNOWN_DATA, DataType::UNKNOWN_DATA);
    return default_map;
  }

  // Create list datatype.
  static std::shared_ptr<YQLType> CreateTypeList(DataType value_type);
  static std::shared_ptr<YQLType> CreateTypeList() {
    // Create default list type: LIST <UNKNOWN>.
    static const std::shared_ptr<YQLType> default_list = CreateTypeList(DataType::UNKNOWN_DATA);
    return default_list;
  }

  // Create set datatype.
  static std::shared_ptr<YQLType> CreateTypeSet(DataType value_type);
  static std::shared_ptr<YQLType> CreateTypeSet() {
    // Create default set type: SET <UNKNOWN>.
    static const std::shared_ptr<YQLType> default_set = CreateTypeSet(DataType::UNKNOWN_DATA);
    return default_set;
  }

  // Constructors.
  explicit YQLType(DataType yql_typeid) : id_(yql_typeid), params_(0) {
  }

  YQLType(DataType yql_typeid, const vector<std::shared_ptr<YQLType>>& params)
      : id_(yql_typeid), params_(params) {
  }

  virtual ~YQLType() {
  }

  //------------------------------------------------------------------------------------------------
  // Protobuf support.

  void ToYQLTypePB(YQLTypePB *pb_type) const;
  static std::shared_ptr<YQLType> FromYQLTypePB(const YQLTypePB& pb_type);

  //------------------------------------------------------------------------------------------------
  // Access functions.

  const DataType main() const {
    return id_;
  }

  const vector<std::shared_ptr<YQLType>>& params() const {
    return params_;
  }

  const std::shared_ptr<YQLType>& param_type(int member_index = 0) const {
    DCHECK_LT(member_index, params_.size());
    return params_[member_index];
  }

  const TypeInfo* type_info() const {
    return GetTypeInfo(id_);
  }

  //------------------------------------------------------------------------------------------------
  // Predicates.

  bool IsParametric() const {
    return id_ == MAP || id_ == SET || id_ == LIST || id_ == TUPLE;
  }

  bool IsElementary() const {
    return !IsParametric();
  }

  bool IsUnknown() const {
    return IsUnknown(id_);
  }

  bool IsInteger() const {
    return IsInteger(id_);
  }

  bool IsNumeric() const {
    return IsNumeric(id_);
  }

  bool IsValid() const {
    if (IsElementary()) {
      return params_.empty();
    } else {
      // checking number of params
      if (id_ == MAP && params_.size() != 2) {
        return false; // expect two type parameters for maps
      } else if ((id_ == SET || id_ == LIST) && params_.size() != 1) {
        return false; // expect one type parameters for set and list
      } else if (id_ == TUPLE && params_.size() == 0) {
        return false; // expect at least one type parameters for tuples
      }
      // recursively checking params
      for (const auto &param : params_) {
        if (!param->IsValid()) return false;
      }
      return true;
    }
  }

  bool operator ==(const YQLType& other) const {
    if (id_ == other.id_ && params_.size() == other.params_.size()) {
      for (int i = 0; i < params_.size(); i++) {
        if (*params_[i] == *other.params_[i]) {
          continue;
        }
        return false;
      }
      return true;
    }

    return false;
  }

  //------------------------------------------------------------------------------------------------
  // Logging supports.
  const string ToString() const;
  void ToString(std::stringstream& os) const;
  static const string ToCQLString(const DataType& datatype);

  //------------------------------------------------------------------------------------------------
  // static methods
  static const int kMaxTypeIndex = DataType::TYPEARGS + 1;

  // When a new type is added in the enum "DataType", kMaxTypeIndex should be updated for this
  // module to work properly. The DCHECKs in this struct would failed if kMaxTypeIndex is wrong.
  static bool IsValid(DataType type) {
    return (type >= 0 && type < kMaxTypeIndex);
  }

  static bool IsInteger(DataType t) {
    return (t >= INT8 && t <= INT64) || t == VARINT;
  }

  static bool IsNumeric(DataType t) {
    return IsInteger(t) || t == FLOAT || t == DOUBLE || t == DECIMAL;
  }

  // NULL_VALUE_TYPE represents type of a null value or "void".
  static bool IsNull(DataType t) {
    return t == NULL_VALUE_TYPE;
  }

  static bool IsUnknown(DataType t) {
    return t == DataType::UNKNOWN_DATA;
  }

  // There are a few compatibility modes between different datatypes. We use these modes when it is
  // necessary to convert a value from one type to another.
  // * kIdentical: The same type (INT8 === INT8).
  // * kSimilar: These types share the same logical representation even though they might be
  //   represented or implemented differently.
  //   - INT8, INT16, INT32, INT64, and VARINT are similar.
  //   - DOUBLE and FLOAT are similar.
  // * kImplicit: Values can be converted automatically between two different datatypes.
  //   - All integer types are convertible to DOUBLE and FLOAT.
  // * kExplicit: An explicit CAST must be used to trigger the conversion from one type to another.
  //   - DOUBLE and FLOAT are not automatically convertible to integer types.
  //   - Once we support "cast" operator, DOUBLE & FLOAT can be explicitly casted to int types.
  // * kNotAllowed: No conversion is allowed between two different datatypes.
  enum class ConversionMode : int {
    kIdentical = 0,
    kSimilar = 1,
    kImplicit = 2,
    kExplicit = 3,
    kNotAllowed = 4,
  };

  static ConversionMode GetConversionMode(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const ConversionMode kID = ConversionMode::kIdentical;
    static const ConversionMode kSI = ConversionMode::kSimilar;
    static const ConversionMode kIM = ConversionMode::kImplicit;
    static const ConversionMode kEX = ConversionMode::kExplicit;
    static const ConversionMode kNA = ConversionMode::kNotAllowed;
    static const ConversionMode kConversionMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS :=  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg
        /* nul */{ kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i8 */ { kIM,  kID,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i16 */{ kIM,  kSI,  kID,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i32 */{ kIM,  kSI,  kSI,  kID,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i64 */{ kIM,  kSI,  kSI,  kSI,  kID,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* str */{ kIM,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kEX,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bln */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* flt */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kID,  kSI,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dbl */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kSI,  kID,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bin */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* tst */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dec */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kSI,  kSI,  kNA,  kNA,  kID,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* vit */{ kIM,  kSI,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* ine */{ kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* lst */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* map */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* set */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* uid */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kIM,  kNA,  kNA },
        /* tui */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kID,  kNA,  kNA },
        /* tup */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* arg */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID },
    };
    return kConversionMode[left][right];
  }

  static bool IsIdentical(DataType left, DataType right) {
    return GetConversionMode(left, right) == ConversionMode::kIdentical;
  }

  static bool IsSimilar(DataType left, DataType right) {
    return GetConversionMode(left, right) <= ConversionMode::kSimilar;
  }

  static bool IsImplicitlyConvertible(DataType left, DataType right) {
    return GetConversionMode(left, right) <= ConversionMode::kImplicit;
  }

  static bool IsComparable(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const bool kYS = true;
    static const bool kNO = false;
    static const bool kCompareMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS ==  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg
        /* nul */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i8  */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i16 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i32 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i64 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* str */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* bln */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* flt */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* dbl */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* bin */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* tst */{ kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* dec */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* vit */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* ine */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* lst */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* map */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* set */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* uid */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO },
        /* tui */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO },
        /* tup */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* arg */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
    };
    return kCompareMode[left][right];
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Data members.
  DataType id_;
  vector<std::shared_ptr<YQLType>> params_;
};

} // namespace yb

#endif  // YB_COMMON_YQL_TYPE_H_
