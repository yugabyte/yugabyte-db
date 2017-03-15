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
    return (t >= INT8 && t <= INT64);
  }

  static bool IsNumeric(DataType t) {
    return IsInteger(t) || t == FLOAT || t == DOUBLE;
  }

  enum class ConversionMode : int {
    kImplicit = 0,                // Implicit conversion (automatic).
    kExplicit = 1,                // Explicit conversion is available.
    kNotAllowed = 2,              // Not available.
  };

  static ConversionMode GetConversionMode(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const ConversionMode kIM = ConversionMode::kImplicit;
    static const ConversionMode kEX = ConversionMode::kExplicit;
    static const ConversionMode kNA = ConversionMode::kNotAllowed;
    static const ConversionMode kConversionMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS :=  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg
        /* nul */{ kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i8 */ { kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i16 */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i32 */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i64 */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* str */{ kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kEX,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bln */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* flt */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dbl */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bin */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* tst */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dec */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* vit */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* ine */{ kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* lst */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* map */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* set */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* uid */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* tui */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* tup */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* arg */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
    };
    return kConversionMode[left][right];
  }


  static bool IsImplicitlyConvertible(DataType left, DataType right) {
    return GetConversionMode(left, right) == ConversionMode::kImplicit;
  }

  static bool IsComparable(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const bool kYS = true;
    static const bool kNO = false;
    static const bool kCompareMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS ==  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg
        /* nul */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i8 */ { kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
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
