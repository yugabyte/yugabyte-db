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

// Class for storing the additional fields of user-defined types (compared to primitive YQL Types)
// Used internally in YQLType and only set for user-defined types.
class UDTypeInfo {
 public:

  UDTypeInfo(std::string keyspace_name, std::string name)
      : keyspace_name_(keyspace_name), name_(name) {
  }

  const string& keyspace_name() const {
    return keyspace_name_;
  }

  const string& name() const {
    return name_;
  }

  const string& id() const {
    return id_;
  }

  const std::vector<string>& field_names() const {
    return field_names_;
  }

  const string& field_name(int index) const {
    return field_names_[index];
  }

  void set_udt_fields(const std::string& type_id,
                      const std::vector<std::string>& field_names) {
    id_ = type_id;
    field_names_ = field_names;
  }

 private:
  std::string keyspace_name_;
  std::string name_;
  std::string id_;
  std::vector<std::string> field_names_ = {};
};

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
  static std::shared_ptr<YQLType> CreateTypeMap(std::shared_ptr<YQLType> key_type,
                                                std::shared_ptr<YQLType> value_type);
  static std::shared_ptr<YQLType> CreateTypeMap(DataType key_type, DataType value_type);
  static std::shared_ptr<YQLType> CreateTypeMap() {
    // Create default map type: MAP <UNKNOWN -> UNKNOWN>.
    static const std::shared_ptr<YQLType> default_map =
        CreateTypeMap(YQLType::Create(DataType::UNKNOWN_DATA),
                      YQLType::Create(DataType::UNKNOWN_DATA));
    return default_map;
  }

  // Create list datatype.
  static std::shared_ptr<YQLType> CreateTypeList(std::shared_ptr<YQLType> value_type);
  static std::shared_ptr<YQLType> CreateTypeList(DataType val_type);
  static std::shared_ptr<YQLType> CreateTypeList() {
    // Create default list type: LIST <UNKNOWN>.
    static const std::shared_ptr<YQLType> default_list = CreateTypeList(DataType::UNKNOWN_DATA);
    return default_list;
  }

  // Create set datatype.
  static std::shared_ptr<YQLType> CreateTypeSet(std::shared_ptr<YQLType> value_type);
  static std::shared_ptr<YQLType> CreateTypeSet(DataType value_type);
  static std::shared_ptr<YQLType> CreateTypeSet() {
    // Create default set type: SET <UNKNOWN>.
    static const std::shared_ptr<YQLType> default_set = CreateTypeSet(DataType::UNKNOWN_DATA);
    return default_set;
  }

  // Create frozen datatype
  static std::shared_ptr<YQLType> CreateTypeFrozen(std::shared_ptr<YQLType> value_type);
  static std::shared_ptr<YQLType> CreateTypeFrozen() {
  // Create default frozen type: FROZEN <UNKNOWN>.
    static const std::shared_ptr<YQLType> default_frozen =
        CreateTypeFrozen(YQLType::Create(DataType::UNKNOWN_DATA));
    return default_frozen;
  }

  //------------------------------------------------------------------------------------------------
  // Constructors.

  // Constructor for elementary types
  explicit YQLType(DataType yql_typeid) : id_(yql_typeid), params_(0) {
  }

  // Constructor for collection types
  YQLType(DataType yql_typeid, const vector<std::shared_ptr<YQLType>>& params)
      : id_(yql_typeid), params_(params) {
  }

  // Constructor for user-defined types
  YQLType(const string& keyspace_name, const string& type_name)
      : id_(USER_DEFINED_TYPE), params_(0) {
    udtype_info_ = std::make_shared<UDTypeInfo>(keyspace_name, type_name);
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

  std::shared_ptr<YQLType> keys_type() const {
    switch (id_) {
      case MAP:
        return params_[0];
      case LIST:
        return YQLType::Create(INT32);
      case SET:
        // set has no keys, only values
        return nullptr;
      case TUPLE:
        LOG(FATAL) << "Tuple type not implemented yet";

      default:
        // elementary types have no keys or values
        return nullptr;
    }
  }

  std::shared_ptr<YQLType> values_type() const {
    switch (id_) {
      case MAP:
        return params_[1];
      case LIST:
        return params_[0];
      case SET:
        return params_[0];
      case TUPLE:
        LOG(FATAL) << "Tuple type not implemented yet";

      default:
        // other types have no keys or values
        return nullptr;
    }
  }

  const std::shared_ptr<YQLType>& param_type(int member_index = 0) const {
    DCHECK_LT(member_index, params_.size());
    return params_[member_index];
  }

  const TypeInfo* type_info() const {
    return GetTypeInfo(id_);
  }

  //------------------------------------------------------------------------------------------------
  // Methods for User-Defined types.

  const std::vector<string>& udtype_field_names() const {
    return udtype_info_->field_names();
  }

  const string& udtype_field_name(int index) const {
    return udtype_info_->field_name(index);
  }

  const string& udtype_keyspace_name() const {
    return udtype_info_->keyspace_name();
  }

  const string& udtype_name() const {
    return udtype_info_->name();
  }

  const string& udtype_id() const {
    return udtype_info_->id();
  }

  void SetUDTypeFields(const std::string &type_id,
                       const std::vector<std::string> &field_names,
                       const std::vector<std::shared_ptr<YQLType>> &field_types) {
    udtype_info_->set_udt_fields(type_id, field_names);
    params_ = field_types;
  }

  // returns position of "field_name" in udtype_field_names() vector if found, otherwise -1
  const int GetUDTypeFieldIdxByName(const string &field_name) const {
    const std::vector<string>& field_names = udtype_field_names();
    int i = 0;
    while (i != field_names.size()) {
      if (field_names[i] == field_name) return i;
      i++;
    }
    return -1;
  }

  std::vector<std::string> GetUserDefinedTypeIds() const {
    std::vector<std::string> udt_ids;
    GetUserDefinedTypeIds(&udt_ids);
    return udt_ids;
  }

  void GetUserDefinedTypeIds(std::vector<std::string>* udt_ids) const {
    if (IsUserDefined()) {
      udt_ids->push_back(udtype_info_->id());
    }
    for (auto& param : params_) {
      param->GetUserDefinedTypeIds(udt_ids);
    }
  }

  //------------------------------------------------------------------------------------------------
  // Predicates.

  bool IsCollection() const {
    return id_ == MAP || id_ == SET || id_ == LIST || id_ == TUPLE;
  }

  bool IsUserDefined() const {
    return id_ == USER_DEFINED_TYPE;
  }

  bool IsFrozen() const {
    return id_ == FROZEN;
  }

  bool IsParametric() const {
    return IsCollection() || IsUserDefined() || IsFrozen();
  }

  bool IsElementary() const {
    return !IsParametric();
  }

  // Collections and UDT values are stored as complex objects internally, unlike Elementary and
  // Frozen types which are stored as single values.
  bool HasComplexValues() const {
    return IsCollection() || IsUserDefined();
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
  static const int kMaxTypeIndex = DataType::FROZEN + 1;

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
    kFurtherCheck = 3,
    kExplicit = 4,
    kNotAllowed = 5,
  };

  static ConversionMode GetConversionMode(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const ConversionMode kID = ConversionMode::kIdentical;
    static const ConversionMode kSI = ConversionMode::kSimilar;
    static const ConversionMode kIM = ConversionMode::kImplicit;
    static const ConversionMode kFC = ConversionMode::kFurtherCheck;
    static const ConversionMode kEX = ConversionMode::kExplicit;
    static const ConversionMode kNA = ConversionMode::kNotAllowed;
    static const ConversionMode kConversionMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS :=  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg | udt | frz
        /* nul */{ kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i8 */ { kIM,  kID,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i16 */{ kIM,  kSI,  kID,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i32 */{ kIM,  kSI,  kSI,  kID,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* i64 */{ kIM,  kSI,  kSI,  kSI,  kID,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* str */{ kIM,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kEX,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bln */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* flt */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kID,  kSI,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dbl */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kSI,  kID,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* bin */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* tst */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* dec */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kSI,  kSI,  kNA,  kNA,  kID,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* vit */{ kIM,  kSI,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* ine */{ kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* lst */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* map */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* set */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA },
        /* uid */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kIM,  kNA,  kNA,  kNA,  kNA },
        /* tui */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kID,  kNA,  kNA,  kNA,  kNA },
        /* tup */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA },
        /* arg */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA },
        /* udt */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA },
        /* frz */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kFC,  kFC,  kNA,  kNA,  kFC,  kNA,  kFC,  kFC },
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

  static bool IsImplicitlyConvertible(const std::shared_ptr<YQLType>& lhs_type,
                                      const std::shared_ptr<YQLType>& rhs_type) {
    switch (YQLType::GetConversionMode(lhs_type->main(), rhs_type->main())) {
      case YQLType::ConversionMode::kIdentical: FALLTHROUGH_INTENDED;
      case YQLType::ConversionMode::kSimilar: FALLTHROUGH_INTENDED;
      case YQLType::ConversionMode::kImplicit:
        return true;

      case YQLType::ConversionMode::kFurtherCheck:
        // checking params convertibility
        if (lhs_type->params().size() != rhs_type->params().size()) {
          return false;
        }
        for (int i = 0; i < lhs_type->params().size(); i++) {
          if (!IsImplicitlyConvertible(lhs_type->params().at(i), rhs_type->params().at(i))) {
            return false;
          }
        }
        return true;

      case YQLType::ConversionMode::kExplicit: FALLTHROUGH_INTENDED;
      case YQLType::ConversionMode::kNotAllowed:
        return false;
    }

    LOG(FATAL) << "Unsupported conversion mode in switch statement";
    return false;
  }

  static bool IsComparable(DataType left, DataType right) {
    DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

    static const bool kYS = true;
    static const bool kNO = false;
    static const bool kCompareMode[kMaxTypeIndex][kMaxTypeIndex] = {
        // LHS ==  RHS (source)
        //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg | udt | frz
        /* nul */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i8  */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i16 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i32 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* i64 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* str */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* bln */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* flt */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* dbl */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* bin */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* tst */{ kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* dec */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* vit */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* ine */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* lst */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* map */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* set */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* uid */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO },
        /* tui */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO },
        /* tup */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* arg */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* udt */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO },
        /* frz */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS },

    };
    return kCompareMode[left][right];
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Data members.
  DataType id_;
  std::vector<std::shared_ptr<YQLType>> params_;

  // Members for User-Defined Types
  std::shared_ptr<UDTypeInfo> udtype_info_ = nullptr; // default
};


}; // namespace yb

#endif  // YB_COMMON_YQL_TYPE_H_
