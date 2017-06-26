// Copyright (c) YugaByte, Inc.

#include "yb/common/yql_type.h"

namespace yb {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------
// The following functions are to construct YQLType objects.

shared_ptr<YQLType> YQLType::Create(DataType data_type, const vector<shared_ptr<YQLType>>& params) {
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
    case DataType::TUPLE:
      LOG(FATAL) << "Tuple datatype not supported";
      return nullptr;
    default:
      DCHECK_EQ(params.size(), 0);
      return Create(data_type);
  }
}

shared_ptr<YQLType> YQLType::Create(DataType data_type) {
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
    case DataType::UUID:
      return CreatePrimitiveType<DataType::UUID>();
    case DataType::TIMEUUID:
      return CreatePrimitiveType<DataType::TIMEUUID>();

    // Create empty collection type and raise error during semantic check.
    case DataType::LIST:
      return CreateTypeList();
    case DataType::MAP:
      return CreateTypeMap();
    case DataType::SET:
      return CreateTypeSet();

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

    default:
      LOG(FATAL) << "Not supported datatype " << ToCQLString(data_type);
      return nullptr;
  }
}

bool YQLType::IsValidPrimaryType(DataType type) {
  switch (type) {
    case DataType::DOUBLE: FALLTHROUGH_INTENDED;
    case DataType::FLOAT: FALLTHROUGH_INTENDED;
    case DataType::BOOL:FALLTHROUGH_INTENDED;
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST:
      return false;
    default:
      // Let all other types go. Because we already process column datatype before getting here,
      // just assume that they are all valid types.
      return true;
  }
}

shared_ptr<YQLType> YQLType::CreateTypeMap(DataType key_type, DataType value_type) {
  vector<shared_ptr<YQLType>> params = { YQLType::Create(key_type), YQLType::Create(value_type) };
  return CreateCollectionType<DataType::MAP>(params);
}

shared_ptr<YQLType> YQLType::CreateTypeList(DataType value_type) {
  vector<shared_ptr<YQLType>> params(1, YQLType::Create(value_type));
  return CreateCollectionType<DataType::LIST>(params);
}

shared_ptr<YQLType> YQLType::CreateTypeSet(DataType value_type) {
  vector<shared_ptr<YQLType>> params(1, YQLType::Create(value_type));
  return CreateCollectionType<DataType::SET>(params);
}

//--------------------------------------------------------------------------------------------------
// ToPB and FromPB.

void YQLType::ToYQLTypePB(YQLTypePB *pb_type) const {
  pb_type->set_main(id_);
  for (auto &param : params_) {
    param->ToYQLTypePB(pb_type->add_params());
  }
}

shared_ptr<YQLType> YQLType::FromYQLTypePB(const YQLTypePB& pb_type) {
  if (pb_type.params().empty()) {
    return Create(pb_type.main());
  }

  vector<shared_ptr<YQLType>> params;
  for (auto &param : pb_type.params()) {
    params.push_back(FromYQLTypePB(param));
  }
  return Create(pb_type.main(), params);
}

//--------------------------------------------------------------------------------------------------
// Logging routines.
const string YQLType::ToCQLString(const DataType& datatype) {
  switch (datatype) {
    case DataType::UNKNOWN_DATA: return "unknown";
    case DataType::NULL_VALUE_TYPE: return "null";
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
    case DataType::LIST: return "list";
    case DataType::MAP: return "map";
    case DataType::SET: return "set";
    case DataType::UUID: return "uuid";
    case DataType::TIMEUUID: return "timeuuid";
    case DataType::TUPLE: return "tuple";
    case DataType::TYPEARGS: return "typeargs";
    case DataType::UINT8: return "uint8";
    case DataType::UINT16: return "uint16";
    case DataType::UINT32: return "uint32";
    case DataType::UINT64: return "uint64";
  }
  LOG (FATAL) << "Invalid datatype: " << datatype;
  return "Undefined Type";
}

const string YQLType::ToString() const {
  std::stringstream ss;
  ToString(ss);
  return ss.str();
}

void YQLType::ToString(std::stringstream& os) const {
  os << ToCQLString(id_);
  if (!params_.empty()) {
    os << "<";
    for (int i = 0; i < params_.size(); i++) {
      if (i > 0) {
        os << ", ";
        }
      params_[i]->ToString(os);
    }
    os << ">";
  }
}

}  // namespace yb
