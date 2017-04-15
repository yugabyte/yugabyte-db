//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module is to define a few supporting functions for YQLTYPE.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_YQL_TYPE_H_
#define YB_COMMON_YQL_TYPE_H_

#include <glog/logging.h>

#include "yb/common/key_encoder.h"
#include "yb/util/status.h"

namespace yb {

class YQLType {
 public:
  explicit YQLType(DataType main = DataType::UNKNOWN_DATA, vector<YQLType> params = {})
      : main_(main),
        params_(std::make_shared<vector<YQLType>>(params)) {
    DCHECK(IsValid());
  }

  explicit YQLType(YQLTypePB pb_type) : main_(pb_type.main()),
                                        params_(std::make_shared<vector<YQLType>>()) {
    for (auto &param : pb_type.params()) {
      params_->push_back(YQLType(param));
    }
    DCHECK(IsValid());
  }

  void ToYQLTypePB(YQLTypePB *pb_type) const {
    pb_type->set_main(main_);
    for (auto &param : *params_) {
      param.ToYQLTypePB(pb_type->add_params());
    }
  }

  const DataType main() const {
    return main_;
  }

  const std::shared_ptr<const vector<YQLType>> params() const {
    return params_;
  }

  const TypeInfo* type_info() const {
    return GetTypeInfo(main_);
  }

  bool IsParametric() const {
    return main_ == MAP || main_ == SET || main_ == LIST || main_ == TUPLE;
  }

  bool IsElementary() const {
    return !IsParametric();
  }

  bool IsInteger() const {
    return IsInteger(main_);
  }

  bool IsNumeric() const {
    return IsNumeric(main_);
  }

  bool IsValid() const {
    if (IsElementary()) {
      return params_->empty();
    } else {
      // checking number of params
      if (main_ == MAP && params_->size() != 2) {
        return false; // expect two type parameters for maps
      } else if ((main_ == SET || main_ == LIST) && params_->size() != 1) {
        return false; // expect one type parameters for set and list
      } else if (main_ == TUPLE && params_->size() == 0) {
        return false; // expect at least one type parameters for tuples
      }
      // recursively checking params
      for (const auto &param : *params_) {
        if (!param.IsValid()) return false;
      }
      return true;
    }
  }

  bool operator ==(const YQLType &other) const {
    return main_ == other.main_ &&
        *params_ == *other.params_;
  }

  const string ToString() const {
    std::stringstream ss;
    ToString(ss);
    return ss.str();
  }

  void ToString(std::stringstream& os) const {
    os << DataType_Name(main_);
    if (!params_->empty()) {
      os << "<";
      for (int i = 0; i < params_->size(); i++) {
        if (i > 0) {
          os << ",";
        }
        params_->at(i).ToString(os);
      }
      os << ">";
    }
  }

  //------------------------------------------------------------------------------------------------
  // static methods
  //------------------------------------------------------------------------------------------------
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
        /* uid */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA },
        /* tui */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA },
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
        /* uid */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* tui */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* tup */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* arg */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
    };
    return kCompareMode[left][right];
  }

  //------------------------------------------------------------------------------------------------

 private:
  DataType main_;
  std::shared_ptr<vector<YQLType>> params_;
};

} // namespace yb

#endif  // YB_COMMON_YQL_TYPE_H_
