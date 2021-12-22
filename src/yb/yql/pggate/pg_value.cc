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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_value.h"

#include "yb/common/ql_value.h"

#include "yb/util/decimal.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {


Status PgValueFromPB(const YBCPgTypeEntity *type_entity,
                     YBCPgTypeAttrs type_attrs,
                     const QLValuePB& ql_value,
                     uint64_t* datum,
                     bool *is_null) {

  // Handling null values.
  if (ql_value.value_case() == QLValuePB::VALUE_NOT_SET) {
    *is_null = true;
    *datum = 0;
    return Status::OK();
  }

  *is_null = false;
  switch (type_entity->yb_type) {
    case YB_YQL_DATA_TYPE_INT8: {
      SCHECK(ql_value.has_int8_value(), InternalError, "Unexpected type in the QL value");
      int8_t val = ql_value.int8_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT16: {
      SCHECK(ql_value.has_int16_value(), InternalError, "Unexpected type in the QL value");
      int16_t val = ql_value.int16_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }
    case YB_YQL_DATA_TYPE_INT32: {
      SCHECK(ql_value.has_int32_value(), InternalError, "Unexpected type in the QL value");
      int32_t val = ql_value.int32_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT64: {
      SCHECK(ql_value.has_int64_value(), InternalError, "Unexpected type in the QL value");
      int64_t val = ql_value.int64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_UINT32: {
      SCHECK(ql_value.has_uint32_value(), InternalError, "Unexpected type in the QL value");
      uint32_t val = ql_value.uint32_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_UINT64: {
      SCHECK(ql_value.has_uint64_value(), InternalError, "Unexpected type in the QL value");
      uint64_t val = ql_value.uint64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_STRING: {
      SCHECK(ql_value.has_string_value(), InternalError, "Unexpected type in the QL value");
      auto size = ql_value.string_value().size();
      auto val = const_cast<char *>(ql_value.string_value().c_str());
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(val), size, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_BOOL: {
      SCHECK(ql_value.has_bool_value(), InternalError, "Unexpected type in the QL value");
      bool val = ql_value.bool_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_FLOAT: {
      SCHECK(ql_value.has_float_value(), InternalError, "Unexpected type in the QL value");
      float val = ql_value.float_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DOUBLE: {
      SCHECK(ql_value.has_double_value(), InternalError, "Unexpected type in the QL value");
      double val = ql_value.double_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_BINARY: {
      SCHECK(ql_value.has_binary_value(), InternalError, "Unexpected type in the QL value");
      auto size = ql_value.binary_value().size();
      auto val = const_cast<char *>(ql_value.binary_value().c_str());
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(val), size, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_TIMESTAMP: {
      // Timestamp encoded as int64 (QL) value.
      SCHECK(ql_value.has_int64_value(), InternalError, "Unexpected type in the QL value");
      int64_t val = ql_value.int64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DECIMAL: {
      SCHECK(ql_value.has_decimal_value(), InternalError, "Unexpected type in the QL value");
      util::Decimal yb_decimal;
      if (!yb_decimal.DecodeFromComparable(ql_value.decimal_value()).ok()) {
        return STATUS_SUBSTITUTE(InternalError,
                                  "Failed to deserialize DECIMAL from $1",
                                  ql_value.decimal_value());
      }
      auto plaintext = yb_decimal.ToString();
      auto val = const_cast<char *>(plaintext.c_str());
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(val),
                                        plaintext.size(),
                                        &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_GIN_NULL: {
      SCHECK(ql_value.has_gin_null_value(), InternalError, "Unexpected type in the QL value");
      uint8_t val = ql_value.gin_null_value();
      *datum = type_entity->yb_to_datum(&val, 0, &type_attrs);
      break;
    }

    YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
    YB_PG_INVALID_TYPES_IN_SWITCH:
      return STATUS_SUBSTITUTE(InternalError, "unsupported type $0", type_entity->yb_type);
  }

  return Status::OK();
}


Status PgValueToPB(const YBCPgTypeEntity *type_entity,
                   uint64_t datum,
                   bool is_null,
                   QLValue* ql_value) {
  if (is_null) {
    ql_value->SetNull();
    return Status::OK();
  }

  switch (type_entity->yb_type) {
    case YB_YQL_DATA_TYPE_INT8: {
      int8_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_int8_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_INT16: {
      int16_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_int16_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_INT32: {
      int32_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_int32_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_INT64: {
      int64_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_int64_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_UINT32: {
      uint32_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_uint32_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_UINT64: {
      uint64_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_uint64_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_STRING: {
      char *value;
      int64_t bytes = type_entity->datum_fixed_size;
      type_entity->datum_to_yb(datum, &value, &bytes);
      ql_value->set_string_value(value, bytes);
      break;
    }
    case YB_YQL_DATA_TYPE_BOOL: {
      bool value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_bool_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_FLOAT: {
      float value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_float_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_DOUBLE: {
      double value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_double_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_BINARY: {
      uint8_t *value;
      int64_t bytes = type_entity->datum_fixed_size;
      type_entity->datum_to_yb(datum, &value, &bytes);
      ql_value->set_binary_value(value, bytes);
      break;
    }
    case YB_YQL_DATA_TYPE_TIMESTAMP: {
      int64_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_int64_value(value);
      break;
    }
    case YB_YQL_DATA_TYPE_DECIMAL: {
      char* plaintext;
      type_entity->datum_to_yb(datum, &plaintext, nullptr);
      util::Decimal yb_decimal(plaintext);
      ql_value->set_decimal_value(yb_decimal.EncodeToComparable());
      break;
    }
    case YB_YQL_DATA_TYPE_GIN_NULL: {
      uint8_t value;
      type_entity->datum_to_yb(datum, &value, nullptr);
      ql_value->set_gin_null_value(value);
      break;
    }
    YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
    YB_PG_INVALID_TYPES_IN_SWITCH:
      return STATUS_SUBSTITUTE(InternalError, "unsupported type $0", type_entity->yb_type);
  }
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
