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
// This module is to define a few supporting functions for QLTYPE.
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_QL_TYPE_H_
#define YB_COMMON_QL_TYPE_H_

#include <boost/optional.hpp>

#include "yb/common/common_fwd.h"
#include "yb/common/value.pb.h"
#include "yb/util/status_fwd.h"

namespace yb {

// Class for storing the additional fields of user-defined types (compared to primitive QL Types)
// Used internally in QLType and only set for user-defined types.
class UDTypeInfo {
 public:
  UDTypeInfo(std::string keyspace_name, std::string name)
      : keyspace_name_(keyspace_name), name_(name) {
  }

  const std::string& keyspace_name() const {
    return keyspace_name_;
  }

  const std::string& name() const {
    return name_;
  }

  const std::string& id() const {
    return id_;
  }

  const std::vector<std::string>& field_names() const {
    return field_names_;
  }

  const std::string& field_name(size_t index) const {
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

class QLType {
 public:
  typedef std::shared_ptr<QLType> SharedPtr;

  //------------------------------------------------------------------------------------------------
  // The Create() functions are to construct QLType objects.
  template<DataType data_type>
  static const std::shared_ptr<QLType>& CreatePrimitiveType() {
    static std::shared_ptr<QLType> ql_type = std::make_shared<QLType>(data_type);
    return ql_type;
  }

  template<DataType data_type>
  static std::shared_ptr<QLType> CreateCollectionType(
      const std::vector<std::shared_ptr<QLType>>& params) {
    return std::make_shared<QLType>(data_type, params);
  }

  // Create all builtin types including collection.
  static std::shared_ptr<QLType> Create(DataType data_type,
                                        const std::vector<std::shared_ptr<QLType>>& params);

  // Create primitive types, all builtin types except collection.
  static std::shared_ptr<QLType> Create(DataType data_type);

  // Check type methods.
  static bool IsValidPrimaryType(DataType type);

  // Create map datatype.
  static std::shared_ptr<QLType> CreateTypeMap(std::shared_ptr<QLType> key_type,
                                               std::shared_ptr<QLType> value_type);
  static std::shared_ptr<QLType> CreateTypeMap(DataType key_type, DataType value_type);
  static std::shared_ptr<QLType> CreateTypeMap() {
    // Create default map type: MAP <UNKNOWN -> UNKNOWN>.
    static const std::shared_ptr<QLType> default_map =
        CreateTypeMap(QLType::Create(DataType::UNKNOWN_DATA),
                      QLType::Create(DataType::UNKNOWN_DATA));
    return default_map;
  }

  // Create list datatype.
  static std::shared_ptr<QLType> CreateTypeList(std::shared_ptr<QLType> value_type);
  static std::shared_ptr<QLType> CreateTypeList(DataType val_type);
  static std::shared_ptr<QLType> CreateTypeList() {
    // Create default list type: LIST <UNKNOWN>.
    static const std::shared_ptr<QLType> default_list = CreateTypeList(DataType::UNKNOWN_DATA);
    return default_list;
  }

  // Create set datatype.
  static std::shared_ptr<QLType> CreateTypeSet(std::shared_ptr<QLType> value_type);
  static std::shared_ptr<QLType> CreateTypeSet(DataType value_type);
  static std::shared_ptr<QLType> CreateTypeSet() {
    // Create default set type: SET <UNKNOWN>.
    static const std::shared_ptr<QLType> default_set = CreateTypeSet(DataType::UNKNOWN_DATA);
    return default_set;
  }

  // Create frozen datatype
  static std::shared_ptr<QLType> CreateTypeFrozen(std::shared_ptr<QLType> value_type);
  static std::shared_ptr<QLType> CreateTypeFrozen() {
  // Create default frozen type: FROZEN <UNKNOWN>.
    static const std::shared_ptr<QLType> default_frozen =
        CreateTypeFrozen(QLType::Create(DataType::UNKNOWN_DATA));
    return default_frozen;
  }

  //------------------------------------------------------------------------------------------------
  // Constructors.

  // Constructor for elementary types
  explicit QLType(DataType ql_typeid) : id_(ql_typeid), params_(0) {
  }

  // Constructor for collection types
  QLType(DataType ql_typeid, const std::vector<std::shared_ptr<QLType>>& params)
      : id_(ql_typeid), params_(params) {
  }

  // Constructor for user-defined types
  QLType(const std::string& keyspace_name, const std::string& type_name)
      : id_(USER_DEFINED_TYPE), params_(0) {
    udtype_info_ = std::make_shared<UDTypeInfo>(keyspace_name, type_name);
  }

  virtual ~QLType() {
  }

  //------------------------------------------------------------------------------------------------
  // Protobuf support.

  void ToQLTypePB(QLTypePB *pb_type) const;
  static std::shared_ptr<QLType> FromQLTypePB(const QLTypePB& pb_type);

  //------------------------------------------------------------------------------------------------
  // Access functions.

  DataType main() const {
    return id_;
  }

  const std::vector<std::shared_ptr<QLType>>& params() const {
    return params_;
  }

  std::shared_ptr<QLType> keys_type() const;

  std::shared_ptr<QLType> values_type() const;

  const QLType::SharedPtr& param_type(size_t member_index = 0) const;

  const TypeInfo* type_info() const;

  //------------------------------------------------------------------------------------------------
  // Methods for User-Defined types.

  const std::shared_ptr<UDTypeInfo>& udtype_info() const {
    return udtype_info_;
  }

  const std::vector<std::string>& udtype_field_names() const {
    return udtype_info_->field_names();
  }

  const std::string& udtype_field_name(size_t index) const {
    return udtype_info_->field_name(index);
  }

  const std::string& udtype_keyspace_name() const {
    return udtype_info_->keyspace_name();
  }

  const std::string& udtype_name() const {
    return udtype_info_->name();
  }

  const std::string& udtype_id() const {
    return udtype_info_->id();
  }

  void SetUDTypeFields(const std::string &type_id,
                       const std::vector<std::string> &field_names,
                       const std::vector<std::shared_ptr<QLType>> &field_types) {
    udtype_info_->set_udt_fields(type_id, field_names);
    params_ = field_types;
  }

  // returns position of "field_name" in udtype_field_names() vector if found, otherwise -1
  boost::optional<size_t> GetUDTypeFieldIdxByName(const std::string &field_name) const;

  // Get the type ids of all UDTs (transitively) referenced by this UDT.
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

  // Check whether the type id exists among type ids of all UDTs referenced by this UDT.
  static bool DoesUserDefinedTypeIdExist(const QLTypePB& type_pb,
                                         const bool transitive,
                                         const std::string& udt_id);

  // Get the type ids of all UDTs referenced by this UDT.
  static void GetUserDefinedTypeIds(const QLTypePB& type_pb,
                                    const bool transitive,
                                    std::vector<std::string>* udt_ids);

  // Returns the type of given field, or nullptr if that field is not found in this UDT.R
  Result<QLType::SharedPtr> GetUDTFieldTypeByName(const std::string& field_name) const;

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

  bool IsAnyType() const {
    return IsNull(id_);
  }

  bool IsInteger() const {
    return IsInteger(id_);
  }

  bool IsJson() const {
    return IsJson(id_);
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
        return false; // expect one type parameter for set and list
      } else if (id_ == TUPLE && params_.size() == 0) {
        return false; // expect at least one type parameters for tuples
      } else if (id_ == FROZEN && params_.size() != 1) {
        return false; // expect one type parameter for frozen
      }
      // recursively checking params
      for (const auto &param : params_) {
        if (!param->IsValid()) return false;
      }
      return true;
    }
  }

  bool Contains(DataType id) const {
    for (const std::shared_ptr<QLType>& param : params_) {
      if (param->Contains(id)) {
        return true;
      }
    }
    return id_ == id;
  }

  bool operator ==(const QLType& other) const {
    if (IsUserDefined()) {
      return other.IsUserDefined() && udtype_id() == other.udtype_id();
    }
    if (id_ == other.id_ && params_.size() == other.params_.size()) {
      for (size_t i = 0; i < params_.size(); i++) {
        if (*params_[i] == *other.params_[i]) {
          continue;
        }
        return false;
      }
      return true;
    }

    return false;
  }

  bool operator !=(const QLType& other) const {
    return !(*this == other);
  }

  void add_param(std::shared_ptr<QLType> param) {
    params_.push_back(param);
    return;
  }

  //------------------------------------------------------------------------------------------------
  // Logging supports.
  std::string ToString() const;
  void ToString(std::stringstream& os) const;
  static const std::string ToCQLString(const DataType& datatype);

  //------------------------------------------------------------------------------------------------
  // static methods
  static const int kMaxTypeIndex = DataType::JSONB + 1;

  // When a new type is added in the enum "DataType", kMaxTypeIndex should be updated for this
  // module to work properly. The DCHECKs in this struct would failed if kMaxTypeIndex is wrong.
  static bool IsValid(DataType type) {
    return (type >= 0 && type < kMaxTypeIndex);
  }

  static bool IsInteger(DataType t) {
    return (t >= INT8 && t <= INT64) || t == VARINT;
  }

  static bool IsJson(DataType t) {
    return t == JSONB;
  }

  static bool IsNumeric(DataType t) {
    return IsInteger(t) || t == FLOAT || t == DOUBLE || t == DECIMAL;
  }

  // NULL_VALUE_TYPE represents type of a null value.
  static bool IsNull(DataType t) {
    return t == NULL_VALUE_TYPE;
  }

  // Type is not yet set (VOID).
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

  static ConversionMode GetConversionMode(DataType left, DataType right);

  static bool IsIdentical(DataType left, DataType right) {
    return GetConversionMode(left, right) == ConversionMode::kIdentical;
  }

  static bool IsSimilar(DataType left, DataType right);

  static bool IsImplicitlyConvertible(DataType left, DataType right) {
    return GetConversionMode(left, right) <= ConversionMode::kImplicit;
  }

  static bool IsPotentiallyConvertible(DataType left, DataType right);

  static bool IsExplicitlyConvertible(DataType left, DataType right) {
    return GetConversionMode(left, right) <= ConversionMode::kExplicit;
  }

  static bool IsImplicitlyConvertible(const std::shared_ptr<QLType>& lhs_type,
                                      const std::shared_ptr<QLType>& rhs_type);

  static bool IsComparable(DataType left, DataType right);

 private:
  //------------------------------------------------------------------------------------------------
  // Data members.
  DataType id_;
  std::vector<std::shared_ptr<QLType>> params_;

  // Members for User-Defined Types
  std::shared_ptr<UDTypeInfo> udtype_info_ = nullptr; // default
};


}; // namespace yb

#endif  // YB_COMMON_QL_TYPE_H_
