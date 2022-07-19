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

#include "yb/common/ql_type.h"

#include "yb/common/common.pb.h"
#include "yb/common/types.h"

#include "yb/gutil/macros.h"

#include "yb/util/result.h"

namespace yb {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------
// The following functions are to construct QLType objects.

shared_ptr<QLType> QLType::Create(
    DataType data_type, const std::vector<shared_ptr<QLType>>& params) {
  switch (data_type) {
    case DataType::LIST:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::LIST>(params);
    case DataType::MAP:
      DCHECK_EQ(params.size(), 2);
      return CreateCollectionType<DataType::MAP>(params);
    case DataType::SET:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::SET>(params);
    case DataType::FROZEN:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::FROZEN>(params);
    case DataType::TUPLE:
      return CreateCollectionType<DataType::TUPLE>(params);
    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return nullptr;
    default:
      DCHECK_EQ(params.size(), 0);
      return Create(data_type);
  }
}

shared_ptr<QLType> QLType::Create(DataType data_type) {
  switch (data_type) {
    case DataType::UNKNOWN_DATA:
      return CreatePrimitiveType<DataType::UNKNOWN_DATA>();
    case DataType::NULL_VALUE_TYPE:
      return CreatePrimitiveType<DataType::NULL_VALUE_TYPE>();
    case DataType::INT8:
      return CreatePrimitiveType<DataType::INT8>();
    case DataType::INT16:
      return CreatePrimitiveType<DataType::INT16>();
    case DataType::INT32:
      return CreatePrimitiveType<DataType::INT32>();
    case DataType::INT64:
      return CreatePrimitiveType<DataType::INT64>();
    case DataType::STRING:
      return CreatePrimitiveType<DataType::STRING>();
    case DataType::BOOL:
      return CreatePrimitiveType<DataType::BOOL>();
    case DataType::FLOAT:
      return CreatePrimitiveType<DataType::FLOAT>();
    case DataType::DOUBLE:
      return CreatePrimitiveType<DataType::DOUBLE>();
    case DataType::BINARY:
      return CreatePrimitiveType<DataType::BINARY>();
    case DataType::TIMESTAMP:
      return CreatePrimitiveType<DataType::TIMESTAMP>();
    case DataType::DECIMAL:
      return CreatePrimitiveType<DataType::DECIMAL>();
    case DataType::VARINT:
      return CreatePrimitiveType<DataType::VARINT>();
    case DataType::INET:
      return CreatePrimitiveType<DataType::INET>();
    case DataType::JSONB:
      return CreatePrimitiveType<DataType::JSONB>();
    case DataType::UUID:
      return CreatePrimitiveType<DataType::UUID>();
    case DataType::TIMEUUID:
      return CreatePrimitiveType<DataType::TIMEUUID>();
    case DataType::DATE:
      return CreatePrimitiveType<DataType::DATE>();
    case DataType::TIME:
      return CreatePrimitiveType<DataType::TIME>();

    // Create empty parametric types and raise error during semantic check.
    case DataType::LIST:
      return CreateTypeList();
    case DataType::MAP:
      return CreateTypeMap();
    case DataType::SET:
      return CreateTypeSet();
    case DataType::TUPLE:
      return CreateCollectionType<DataType::TUPLE>({});
    case DataType::FROZEN:
      return CreateTypeFrozen();

    // Kudu datatypes.
    case UINT8:
      return CreatePrimitiveType<DataType::UINT8>();
    case UINT16:
      return CreatePrimitiveType<DataType::UINT16>();
    case UINT32:
      return CreatePrimitiveType<DataType::UINT32>();
    case UINT64:
      return CreatePrimitiveType<DataType::UINT64>();

    // Datatype for variadic builtin function.
    case TYPEARGS:
      return CreatePrimitiveType<DataType::TYPEARGS>();

    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return nullptr;

    case DataType::GIN_NULL:
      return CreatePrimitiveType<DataType::GIN_NULL>();
  }
  LOG(FATAL) << "Not supported datatype " << ToCQLString(data_type);
  return nullptr;
}

bool QLType::IsValidPrimaryType(DataType type) {
  switch (type) {
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::JSONB: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE:
      return false;

    default:
      // Let all other types go. Because we already process column datatype before getting here,
      // just assume that they are all valid types.
      return true;
  }
}

shared_ptr<QLType> QLType::CreateTypeMap(std::shared_ptr<QLType> key_type,
                                           std::shared_ptr<QLType> value_type) {
  std::vector<shared_ptr<QLType>> params = {key_type, value_type};
  return CreateCollectionType<DataType::MAP>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeMap(DataType key_type, DataType value_type) {
  return CreateTypeMap(QLType::Create(key_type), QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeList(std::shared_ptr<QLType> value_type) {
  std::vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::LIST>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeList(DataType value_type) {
  return CreateTypeList(QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeSet(std::shared_ptr<QLType> value_type) {
  std::vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::SET>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeSet(DataType value_type) {
  return CreateTypeSet(QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeFrozen(shared_ptr<QLType> value_type) {
  std::vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::FROZEN>(params);
}

//--------------------------------------------------------------------------------------------------
// ToPB and FromPB.

void QLType::ToQLTypePB(QLTypePB *pb_type) const {
  pb_type->set_main(id_);
  for (auto &param : params_) {
    param->ToQLTypePB(pb_type->add_params());
  }

  if (IsUserDefined()) {
    auto udtype_info = pb_type->mutable_udtype_info();
    udtype_info->set_keyspace_name(udtype_keyspace_name());
    udtype_info->set_name(udtype_name());
    udtype_info->set_id(udtype_id());

    for (const auto &field_name : udtype_field_names()) {
      udtype_info->add_field_names(field_name);
    }
  }
}

shared_ptr<QLType> QLType::FromQLTypePB(const QLTypePB& pb_type) {
  if (pb_type.main() == USER_DEFINED_TYPE) {
    auto ql_type = std::make_shared<QLType>(pb_type.udtype_info().keyspace_name(),
                                              pb_type.udtype_info().name());
    std::vector<std::string> field_names;
    for (const auto& field_name : pb_type.udtype_info().field_names()) {
      field_names.push_back(field_name);
    }

    std::vector<shared_ptr<QLType>> field_types;
    for (const auto& field_type : pb_type.params()) {
      field_types.push_back(QLType::FromQLTypePB(field_type));
    }

    ql_type->SetUDTypeFields(pb_type.udtype_info().id(), field_names, field_types);
    return ql_type;
  }

  if (pb_type.params().empty()) {
    return Create(pb_type.main());
  }

  std::vector<shared_ptr<QLType>> params;
  for (auto &param : pb_type.params()) {
    params.push_back(FromQLTypePB(param));
  }
  return Create(pb_type.main(), params);
}

std::shared_ptr<QLType> QLType::keys_type() const {
  switch (id_) {
    case MAP:
      return params_[0];
    case LIST:
      return QLType::Create(INT32);
    case SET:
      // set has no keys, only values
      return nullptr;
    case TUPLE:
      // https://github.com/YugaByte/yugabyte-db/issues/936
      LOG(FATAL) << "Tuple type not implemented yet";

    default:
      // elementary types have no keys or values
      return nullptr;
  }
}

std::shared_ptr<QLType> QLType::values_type() const {
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

const QLType::SharedPtr& QLType::param_type(size_t member_index) const {
  DCHECK_LT(member_index, params_.size());
  return params_[member_index];
}

//--------------------------------------------------------------------------------------------------
// Logging routines.
const std::string QLType::ToCQLString(const DataType& datatype) {
  switch (datatype) {
    case DataType::UNKNOWN_DATA: return "unknown";
    case DataType::NULL_VALUE_TYPE: return "anytype";
    case DataType::INT8: return "tinyint";
    case DataType::INT16: return "smallint";
    case DataType::INT32: return "int";
    case DataType::INT64: return "bigint";
    case DataType::STRING: return "text";
    case DataType::BOOL: return "boolean";
    case DataType::FLOAT: return "float";
    case DataType::DOUBLE: return "double";
    case DataType::BINARY: return "blob";
    case DataType::TIMESTAMP: return "timestamp";
    case DataType::DECIMAL: return "decimal";
    case DataType::VARINT: return "varint";
    case DataType::INET: return "inet";
    case DataType::JSONB: return "jsonb";
    case DataType::LIST: return "list";
    case DataType::MAP: return "map";
    case DataType::SET: return "set";
    case DataType::UUID: return "uuid";
    case DataType::TIMEUUID: return "timeuuid";
    case DataType::TUPLE: return "tuple";
    case DataType::TYPEARGS: return "typeargs";
    case DataType::FROZEN: return "frozen";
    case DataType::USER_DEFINED_TYPE: return "user_defined_type";
    case DataType::DATE: return "date";
    case DataType::TIME: return "time";
    case DataType::UINT8: return "uint8";
    case DataType::UINT16: return "uint16";
    case DataType::UINT32: return "uint32";
    case DataType::UINT64: return "uint64";
    case DataType::GIN_NULL: return "gin_null";
  }
  LOG(FATAL) << "Invalid datatype: " << datatype;
  return "Undefined Type";
}

std::string QLType::ToString() const {
  std::stringstream ss;
  ToString(ss);
  return ss.str();
}

void QLType::ToString(std::stringstream& os) const {
  if (IsUserDefined()) {
    // UDTs can only be used in the keyspace they are defined in, so keyspace name is implied.
    os << udtype_name();
  } else {
    os << ToCQLString(id_);
    if (!params_.empty()) {
      os << "<";
      for (size_t i = 0; i < params_.size(); i++) {
        if (i > 0) {
          os << ", ";
        }
        params_[i]->ToString(os);
      }
      os << ">";
    }
  }
}

bool QLType::DoesUserDefinedTypeIdExist(const QLTypePB& type_pb,
                                        const bool transitive,
                                        const std::string& udt_id) {
  if (type_pb.main() == USER_DEFINED_TYPE) {
    if (type_pb.udtype_info().id() == udt_id) {
      return true;
    }
    if (!transitive) {
      return false; // Do not check params of the UDT if only looking for direct dependencies.
    }
  }

  for (const auto& param : type_pb.params()) {
    if (DoesUserDefinedTypeIdExist(param, transitive, udt_id)) {
      return true;
    }
  }
  return false;
}

// Get the type ids of all UDTs referenced by this UDT.
void QLType::GetUserDefinedTypeIds(const QLTypePB& type_pb,
                                   const bool transitive,
                                   std::vector<std::string>* udt_ids) {
  if (type_pb.main() == USER_DEFINED_TYPE) {
    udt_ids->push_back(type_pb.udtype_info().id());
    if (!transitive) {
      return; // Do not check params of the UDT if only looking for direct dependencies.
    }
  }

  for (const auto& param : type_pb.params()) {
    GetUserDefinedTypeIds(param, transitive, udt_ids);
  }
}

Result<QLType::SharedPtr> QLType::GetUDTFieldTypeByName(const std::string& field_name) const {
  SCHECK(IsUserDefined(), InternalError, "Can only be called on UDT");
  const auto idx = GetUDTypeFieldIdxByName(field_name);
  if (!idx) {
    return nullptr;
  }
  return param_type(*idx);
}

const TypeInfo* QLType::type_info() const {
  return GetTypeInfo(id_);
}

bool QLType::IsImplicitlyConvertible(const std::shared_ptr<QLType>& lhs_type,
                                            const std::shared_ptr<QLType>& rhs_type) {
  switch (QLType::GetConversionMode(lhs_type->main(), rhs_type->main())) {
    case QLType::ConversionMode::kIdentical: FALLTHROUGH_INTENDED;
    case QLType::ConversionMode::kSimilar: FALLTHROUGH_INTENDED;
    case QLType::ConversionMode::kImplicit:
      return true;

    case QLType::ConversionMode::kFurtherCheck:
      // checking params convertibility
      if (lhs_type->params().size() != rhs_type->params().size()) {
        return false;
      }
      for (size_t i = 0; i < lhs_type->params().size(); i++) {
        if (!IsImplicitlyConvertible(lhs_type->params().at(i), rhs_type->params().at(i))) {
          return false;
        }
      }
      return true;

    case QLType::ConversionMode::kExplicit: FALLTHROUGH_INTENDED;
    case QLType::ConversionMode::kNotAllowed:
      return false;
  }

  LOG(FATAL) << "Unsupported conversion mode in switch statement";
  return false;
}

QLType::ConversionMode QLType::GetConversionMode(DataType left, DataType right) {
  DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

  static const ConversionMode kID = ConversionMode::kIdentical;
  static const ConversionMode kSI = ConversionMode::kSimilar;
  static const ConversionMode kIM = ConversionMode::kImplicit;
  static const ConversionMode kFC = ConversionMode::kFurtherCheck;
  static const ConversionMode kEX = ConversionMode::kExplicit;
  static const ConversionMode kNA = ConversionMode::kNotAllowed;
  static const ConversionMode kConversionMode[kMaxTypeIndex][kMaxTypeIndex] = {
      // LHS :=  RHS (source)
      //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg | udt | frz | dat | tim | jso    // NOLINT
      /* nul */{ kID,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM,  kIM }, // NOLINT
      /* i8 */ { kIM,  kID,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* i16 */{ kIM,  kSI,  kID,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* i32 */{ kIM,  kSI,  kSI,  kID,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* i64 */{ kIM,  kSI,  kSI,  kSI,  kID,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kSI,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kEX,  kEX,  kNA }, // NOLINT
      /* str */{ kIM,  kEX,  kEX,  kEX,  kEX,  kID,  kEX,  kEX,  kEX,  kEX,  kEX,  kEX,  kNA,  kEX,  kNA,  kNA,  kNA,  kEX,  kEX,  kNA,  kNA,  kNA,  kNA,  kEX,  kEX,  kEX }, // NOLINT
      /* bln */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* flt */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kID,  kSI,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* dbl */{ kIM,  kIM,  kIM,  kIM,  kIM,  kNA,  kNA,  kSI,  kID,  kNA,  kNA,  kSI,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* bin */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* tst */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kEX,  kNA,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA }, // NOLINT
      /* dec */{ kIM,  kIM,  kIM,  kIM,  kIM,  kEX,  kNA,  kSI,  kSI,  kNA,  kEX,  kID,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* vit */{ kIM,  kSI,  kSI,  kSI,  kSI,  kNA,  kNA,  kEX,  kEX,  kNA,  kEX,  kEX,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kEX,  kEX,  kNA }, // NOLINT
      /* ine */{ kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* lst */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* map */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* set */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* uid */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* tui */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kID,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* tup */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* arg */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* udt */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kNA,  kNA,  kNA,  kNA }, // NOLINT
      /* frz */{ kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kFC,  kFC,  kFC,  kNA,  kNA,  kFC,  kNA,  kFC,  kFC,  kNA,  kNA,  kNA }, // NOLINT
      /* dat */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kEX,  kNA,  kNA,  kNA,  kNA,  kID,  kNA,  kNA }, // NOLINT
      /* tim */{ kIM,  kNA,  kNA,  kNA,  kIM,  kIM,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID,  kNA }, // NOLINT
      /* jso */{ kNA,  kNA,  kNA,  kNA,  kNA,  kIM,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kNA,  kID }, // NOLINT
  };
  return kConversionMode[left][right];
}

bool QLType::IsComparable(DataType left, DataType right) {
  DCHECK(IsValid(left) && IsValid(right)) << left << ", " << right;

  static const bool kYS = true;
  static const bool kNO = false;
  static const bool kCompareMode[kMaxTypeIndex][kMaxTypeIndex] = {
      // LHS ==  RHS (source)
      //         nul | i8  | i16 | i32 | i64 | str | bln | flt | dbl | bin | tst | dec | vit | ine | lst | map | set | uid | tui | tup | arg | udt | frz | dat | tim | jso    // NOLINT
      /* nul */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* i8  */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* i16 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* i32 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* i64 */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* str */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* bln */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* flt */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* dbl */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* bin */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* tst */{ kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO }, // NOLINT
      /* dec */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* vit */{ kNO,  kYS,  kYS,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* ine */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* lst */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* map */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* set */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* uid */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* tui */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* tup */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* arg */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* udt */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO }, // NOLINT
      /* frz */{ kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO }, // NOLINT
      /* dat */{ kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO }, // NOLINT
      /* tim */{ kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kYS,  kNO }, // NOLINT
      /* jso */{ kNO,  kNO,  kNO,  kNO,  kNO,  kYS,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kNO,  kYS }, // NOLINT
  };
  return kCompareMode[left][right];
}

bool QLType::IsPotentiallyConvertible(DataType left, DataType right) {
  return GetConversionMode(left, right) <= ConversionMode::kFurtherCheck;
}

bool QLType::IsSimilar(DataType left, DataType right) {
  return GetConversionMode(left, right) <= ConversionMode::kSimilar;
}

boost::optional<size_t> QLType::GetUDTypeFieldIdxByName(const std::string &field_name) const {
  const auto& field_names = udtype_field_names();
  for (size_t i = 0; i != field_names.size(); ++i) {
    if (field_names[i] == field_name) {
      return i;
    }
  }
  return boost::none;
}

}  // namespace yb
