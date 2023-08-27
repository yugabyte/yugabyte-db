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

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/common/common.pb.h"
#include "yb/common/types.h"

#include "yb/gutil/macros.h"

#include "yb/util/result.h"
#include "yb/util/string_case.h"

namespace yb {

namespace {

struct EmptyUDTypeInfo {};

template<DataType type>
struct Traits {
  using UDTInfo = EmptyUDTypeInfo;
};

template<>
struct Traits<DataType::USER_DEFINED_TYPE> {
  using UDTInfo = UDTypeInfo;
};

constexpr int kMaxValidYCQLTypeIndex = to_underlying(DataType::JSONB) + 1;

// Set of keywords in CQL.
// Derived from .../cassandra/src/java/org/apache/cassandra/cql3/Cql.g
static const std::unordered_set<std::string> cql_keywords({
    "add", "allow", "alter", "and", "apply", "asc", "authorize", "batch", "begin", "by",
    "columnfamily", "create", "default", "delete", "desc", "describe", "drop", "entries",
    "execute", "from", "frozen", "full", "grant", "if", "in", "index", "infinity", "insert",
    "into", "is", "keyspace", "limit", "list", "map", "materialized", "mbean", "mbeans",
    "modify", "nan", "norecursive", "not", "null", "of", "on", "or", "order", "primary",
    "rename", "replace", "revoke", "schema", "select", "set", "table", "to", "token",
    "truncate", "unlogged", "unset", "update", "use", "using", "view", "where", "with"
});

} // namespace

struct QLType::Internals {
  template<DataType type, class... Args>
  static SharedPtr Make(Args&&... args) {
    auto holder = std::make_shared<TypeHolder<typename Traits<type>::UDTInfo>>(
        type, std::forward<Args>(args)...);
    auto* ql_type = &holder->type;
    // holder object stores type as a member. Create shared_ptr<QLType> on this member.
    return std::shared_ptr<QLType>(std::move(holder), ql_type);
  }

 private:
  template<class UDTInfo>
  struct TypeHolder : private UDTInfo {
    template<class... Args>
    TypeHolder(DataType id, Params&& params = Params(), Args&&... args)
      : UDTInfo(std::forward<Args>(args)...),
        type(id, std::move(params), GetUDTypeInfoPtr(this)) {}

    QLType type;
  };

  static const UDTypeInfo* GetUDTypeInfoPtr(const EmptyUDTypeInfo*) {
    static_assert(sizeof(TypeHolder<EmptyUDTypeInfo>) == sizeof(QLType));
    return nullptr;
  }

  static const UDTypeInfo* GetUDTypeInfoPtr(const UDTypeInfo* info) {
    return info;
  }
};

namespace {

static const QLType::SharedPtr kNullType;

template<DataType type>
const auto kDefaultType = QLType::Internals::Make<type>();

} // namespace

//--------------------------------------------------------------------------------------------------
// The following functions are to construct QLType objects.

QLType::SharedPtr QLType::Create(DataType type, Params params) {
  switch (type) {
    case DataType::LIST:
      DCHECK_EQ(params.size(), 1);
      return Internals::Make<DataType::LIST>(std::move(params));
    case DataType::MAP:
      DCHECK_EQ(params.size(), 2);
      return Internals::Make<DataType::MAP>(std::move(params));
    case DataType::SET:
      DCHECK_EQ(params.size(), 1);
      return Internals::Make<DataType::SET>(std::move(params));
    case DataType::FROZEN:
      DCHECK_EQ(params.size(), 1);
      return Internals::Make<DataType::FROZEN>(std::move(params));
    case DataType::TUPLE:
      return Internals::Make<DataType::TUPLE>(std::move(params));
    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return kNullType;
    default:
      break;
  }
  DCHECK_EQ(params.size(), 0);
  return Create(type);
}

QLType::SharedPtr QLType::Create(DataType type) {
  switch (type) {
    case DataType::UNKNOWN_DATA:    return kDefaultType<DataType::UNKNOWN_DATA>;
    case DataType::NULL_VALUE_TYPE: return kDefaultType<DataType::NULL_VALUE_TYPE>;
    case DataType::INT8:            return kDefaultType<DataType::INT8>;
    case DataType::INT16:           return kDefaultType<DataType::INT16>;
    case DataType::INT32:           return kDefaultType<DataType::INT32>;
    case DataType::INT64:           return kDefaultType<DataType::INT64>;
    case DataType::STRING:          return kDefaultType<DataType::STRING>;
    case DataType::BOOL:            return kDefaultType<DataType::BOOL>;
    case DataType::FLOAT:           return kDefaultType<DataType::FLOAT>;
    case DataType::DOUBLE:          return kDefaultType<DataType::DOUBLE>;
    case DataType::BINARY:          return kDefaultType<DataType::BINARY>;
    case DataType::TIMESTAMP:       return kDefaultType<DataType::TIMESTAMP>;
    case DataType::DECIMAL:         return kDefaultType<DataType::DECIMAL>;
    case DataType::VARINT:          return kDefaultType<DataType::VARINT>;
    case DataType::INET:            return kDefaultType<DataType::INET>;
    case DataType::JSONB:           return kDefaultType<DataType::JSONB>;
    case DataType::UUID:            return kDefaultType<DataType::UUID>;
    case DataType::TIMEUUID:        return kDefaultType<DataType::TIMEUUID>;
    case DataType::DATE:            return kDefaultType<DataType::DATE>;
    case DataType::TIME:            return kDefaultType<DataType::TIME>;

    // Create empty parametric types and raise error during semantic check.
    case DataType::LIST: {
      static const auto default_list = CreateTypeList(DataType::UNKNOWN_DATA);
      return default_list;
    }
    case DataType::MAP: {
      static const auto default_map = CreateTypeMap(DataType::UNKNOWN_DATA, DataType::UNKNOWN_DATA);
      return default_map;
    }
    case DataType::SET: {
      static const auto default_set = CreateTypeSet(DataType::UNKNOWN_DATA);
      return default_set;
    }
    case DataType::TUPLE:
      // Unfortunately tuple type allows params modification.
      // As a result static object can't be used.
      return Create(DataType::TUPLE, {});
    case DataType::FROZEN: {
      static const auto default_frozen = CreateTypeFrozen(QLType::Create(DataType::UNKNOWN_DATA));
      return default_frozen;
    }

    // Kudu datatypes.
    case DataType::UINT8:  return kDefaultType<DataType::UINT8>;
    case DataType::UINT16: return kDefaultType<DataType::UINT16>;
    case DataType::UINT32: return kDefaultType<DataType::UINT32>;
    case DataType::UINT64: return kDefaultType<DataType::UINT64>;

    // Datatype for variadic builtin function.
    case DataType::TYPEARGS: return kDefaultType<DataType::TYPEARGS>;

    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return kNullType;

    case DataType::GIN_NULL: return kDefaultType<DataType::GIN_NULL>;
  }
  LOG(FATAL) << "Not supported datatype " << ToCQLString(type);
  return kNullType;
}

bool QLType::IsValidPrimaryType(DataType type) {
  switch (type) {
    case DataType::MAP:   [[fallthrough]];
    case DataType::SET:   [[fallthrough]];
    case DataType::LIST:  [[fallthrough]];
    case DataType::TUPLE: [[fallthrough]];
    case DataType::JSONB: [[fallthrough]];
    case DataType::USER_DEFINED_TYPE:
      return false;

    default:
      // Let all other types go. Because we already process column datatype before getting here,
      // just assume that they are all valid types.
      return true;
  }
}

QLType::SharedPtr QLType::CreateTypeMap(SharedPtr key_type, SharedPtr value_type) {
  return Create(DataType::MAP, {std::move(key_type), std::move(value_type)});
}

QLType::SharedPtr QLType::CreateTypeMap(DataType key_type, DataType value_type) {
  return CreateTypeMap(Create(key_type), Create(value_type));
}

QLType::SharedPtr QLType::CreateTypeList(SharedPtr value_type) {
  return Create(DataType::LIST, {std::move(value_type)});
}

QLType::SharedPtr QLType::CreateTypeList(DataType value_type) {
  return CreateTypeList(Create(value_type));
}

QLType::SharedPtr QLType::CreateTypeSet(SharedPtr value_type) {
  return Create(DataType::SET, {std::move(value_type)});
}

QLType::SharedPtr QLType::CreateTypeSet(DataType value_type) {
  return CreateTypeSet(Create(value_type));
}

QLType::SharedPtr QLType::CreateTypeFrozen(SharedPtr value_type) {
  return Create(DataType::FROZEN, {std::move(value_type)});
}

QLType::SharedPtr QLType::CreateUDType(
    std::string keyspace_name,
    std::string type_name,
    std::string type_id,
    std::vector<std::string> field_names,
    Params field_types) {
  return Internals::Make<DataType::USER_DEFINED_TYPE>(
      std::move(field_types), std::move(keyspace_name), std::move(type_name),
      std::move(type_id), std::move(field_names));
}

//--------------------------------------------------------------------------------------------------
// ToPB and FromPB.

void QLType::ToQLTypePB(QLTypePB* pb_type) const {
  pb_type->set_main(ToPB(id_));
  for (const auto& param : params_) {
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

QLType::SharedPtr QLType::FromQLTypePB(const QLTypePB& pb_type) {
  Params params;
  params.reserve(pb_type.params().size());
  for (const auto& param : pb_type.params()) {
    params.push_back(FromQLTypePB(param));
  }

  if (pb_type.main() == PersistentDataType::USER_DEFINED_TYPE) {
    const auto& udt = pb_type.udtype_info();
    std::vector<std::string> field_names;
    field_names.reserve(udt.field_names().size());
    for (const auto& field_name : udt.field_names()) {
      field_names.push_back(field_name);
    }

    return CreateUDType(
        udt.keyspace_name(), udt.name(), udt.id(), std::move(field_names), std::move(params));
  }

  return params.empty() ? Create(ToLW(pb_type.main()))
                        : Create(ToLW(pb_type.main()), std::move(params));
}

const QLType::SharedPtr& QLType::keys_type() const {
  switch (id_) {
    case DataType::MAP:
      return params_[0];
    case DataType::LIST:
      return kDefaultType<DataType::INT32>;
    case DataType::SET:
      // set has no keys, only values
      return kNullType;
    case DataType::TUPLE:
      LOG(FATAL) << "The term 'key' is not applicable to the Tuple type";

    default:
      // elementary types have no keys or values
      return kNullType;
  }
}

const QLType::SharedPtr& QLType::values_type() const {
  switch (id_) {
    case DataType::MAP:
      return params_[1];
    case DataType::LIST:
      return params_[0];
    case DataType::SET:
      return params_[0];
    case DataType::TUPLE:
      LOG(FATAL) << "The term 'value' is not applicable to the Tuple type";

    default:
      // other types have no keys or values
      return kNullType;
  }
}

const QLType::SharedPtr& QLType::param_type(size_t member_index) const {
  DCHECK_LT(member_index, params_.size());
  return params_[member_index];
}

//--------------------------------------------------------------------------------------------------
// Logging routines.
const std::string& QLType::ToCQLString(DataType type) {
#define DATA_TYPE_SWITCH_CASE_IMPL(type, name) \
    case type: { static const std::string name_str(name); return name_str; }
#define DATA_TYPE_SWITCH_CASE(r, data, item) DATA_TYPE_SWITCH_CASE_IMPL item;
#define DATA_TYPE_ENUM_ELEMENTS \
    ((DataType::UNKNOWN_DATA, "unknown")) \
    ((DataType::NULL_VALUE_TYPE, "anytype")) \
    ((DataType::INT8, "tinyint")) \
    ((DataType::INT16, "smallint")) \
    ((DataType::INT32, "int")) \
    ((DataType::INT64, "bigint")) \
    ((DataType::STRING, "text")) \
    ((DataType::BOOL, "boolean")) \
    ((DataType::FLOAT, "float")) \
    ((DataType::DOUBLE, "double")) \
    ((DataType::BINARY, "blob")) \
    ((DataType::TIMESTAMP, "timestamp")) \
    ((DataType::DECIMAL, "decimal")) \
    ((DataType::VARINT, "varint")) \
    ((DataType::INET, "inet")) \
    ((DataType::JSONB, "jsonb")) \
    ((DataType::LIST, "list")) \
    ((DataType::MAP, "map")) \
    ((DataType::SET, "set")) \
    ((DataType::UUID, "uuid")) \
    ((DataType::TIMEUUID, "timeuuid")) \
    ((DataType::TUPLE, "tuple")) \
    ((DataType::TYPEARGS, "typeargs")) \
    ((DataType::FROZEN, "frozen")) \
    ((DataType::USER_DEFINED_TYPE, "user_defined_type")) \
    ((DataType::DATE, "date")) \
    ((DataType::TIME, "time")) \
    ((DataType::UINT8, "uint8")) \
    ((DataType::UINT16, "uint16")) \
    ((DataType::UINT32, "uint32")) \
    ((DataType::UINT64, "uint64")) \
    ((DataType::GIN_NULL, "gin_null"))
  switch (type) {
    BOOST_PP_SEQ_FOR_EACH(DATA_TYPE_SWITCH_CASE, ~, DATA_TYPE_ENUM_ELEMENTS)
  }
#undef DATA_TYPE_SWITCH_CASE_IMPL
#undef DATA_TYPE_SWITCH_CASE
#undef DATA_TYPE_ENUM_ELEMENTS
  LOG(FATAL) << "Invalid datatype: " << type;
  static const std::string undefined_type_name("Undefined Type");
  return undefined_type_name;
}

std::string QLType::ToString() const {
  std::stringstream ss;
  ToString(ss);
  return ss.str();
}

void QLType::ToString(std::stringstream& os) const {
  if (IsUserDefined()) {
    // UDTs can only be used in the keyspace they are defined in, so keyspace name is implied.
    const std::string udt_name = udtype_name();
    // Identifiers in cassandra/ycql are case-insensitive unless specified under double quotes.
    // See: https://docs.datastax.com/en/cql-oss/3.x/cql/cql_reference/ucase-lcase_r.html
    if (cql_keywords.contains(ToLowerCase(udt_name))) {
      os << "\"" <<  udt_name << "\"";
    } else {
      os << udt_name;
    }
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
                                        bool transitive,
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
                                   bool transitive,
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

bool QLType::IsImplicitlyConvertible(const SharedPtr& lhs_type, const SharedPtr& rhs_type) {
  switch (QLType::GetConversionMode(lhs_type->main(), rhs_type->main())) {
    case QLType::ConversionMode::kIdentical: [[fallthrough]];
    case QLType::ConversionMode::kSimilar: [[fallthrough]];
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

    case QLType::ConversionMode::kExplicit: [[fallthrough]];
    case QLType::ConversionMode::kNotAllowed:
      return false;
  }

  LOG(FATAL) << "Unsupported conversion mode in switch statement";
  return false;
}

QLType::ConversionMode QLType::GetConversionMode(DataType left, DataType right) {
  const size_t left_index = to_underlying(left);
  const size_t right_index = to_underlying(right);
  DCHECK_LT(left_index, kMaxValidYCQLTypeIndex);
  DCHECK_LT(right_index, kMaxValidYCQLTypeIndex);

  static const ConversionMode kID = ConversionMode::kIdentical;
  static const ConversionMode kSI = ConversionMode::kSimilar;
  static const ConversionMode kIM = ConversionMode::kImplicit;
  static const ConversionMode kFC = ConversionMode::kFurtherCheck;
  static const ConversionMode kEX = ConversionMode::kExplicit;
  static const ConversionMode kNA = ConversionMode::kNotAllowed;
  static const ConversionMode kConversionMode[kMaxValidYCQLTypeIndex][kMaxValidYCQLTypeIndex] = {
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

  return kConversionMode[left_index][right_index];
}

bool QLType::IsComparable(DataType left, DataType right) {
  const size_t left_index = to_underlying(left);
  const size_t right_index = to_underlying(right);
  DCHECK_LT(left_index, kMaxValidYCQLTypeIndex);
  DCHECK_LT(right_index, kMaxValidYCQLTypeIndex);

  static const bool kYS = true;
  static const bool kNO = false;
  static const bool kCompareMode[kMaxValidYCQLTypeIndex][kMaxValidYCQLTypeIndex] = {
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

  return kCompareMode[left_index][right_index];
}

bool QLType::IsPotentiallyConvertible(DataType left, DataType right) {
  return GetConversionMode(left, right) <= ConversionMode::kFurtherCheck;
}

bool QLType::IsSimilar(DataType left, DataType right) {
  return GetConversionMode(left, right) <= ConversionMode::kSimilar;
}

boost::optional<size_t> QLType::GetUDTypeFieldIdxByName(const std::string& field_name) const {
  const auto& field_names = udtype_field_names();
  for (size_t i = 0; i != field_names.size(); ++i) {
    if (field_names[i] == field_name) {
      return i;
    }
  }
  return boost::none;
}

}  // namespace yb
