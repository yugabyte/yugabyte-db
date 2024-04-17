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

#include "yb/dockv/pg_row.h"

#include "yb/util/decimal.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {
namespace pggate {

Status PgValueToDatum(const YBCPgTypeEntity *type_entity,
                      YBCPgTypeAttrs type_attrs,
                      const dockv::PgValue& value,
                      uint64_t* datum) {
  switch (type_entity->yb_type) {
    case YB_YQL_DATA_TYPE_GIN_NULL: FALLTHROUGH_INTENDED;
    case YB_YQL_DATA_TYPE_BOOL: FALLTHROUGH_INTENDED;
    case YB_YQL_DATA_TYPE_INT8: {
      int8_t val = value.int8_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT16: {
      int16_t val = value.int16_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_FLOAT: FALLTHROUGH_INTENDED;
      // Float is represented using int32 in network byte order.
    case YB_YQL_DATA_TYPE_UINT32: FALLTHROUGH_INTENDED;
    case YB_YQL_DATA_TYPE_INT32: {
      int32_t val = value.int32_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DOUBLE: FALLTHROUGH_INTENDED;
      // Double is represented using int64 in network byte order.
    case YB_YQL_DATA_TYPE_TIMESTAMP: FALLTHROUGH_INTENDED;
    case YB_YQL_DATA_TYPE_UINT64: FALLTHROUGH_INTENDED;
    case YB_YQL_DATA_TYPE_INT64: {
      int64_t val = value.int64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_STRING: {
      auto str = value.string_value();
      *datum = type_entity->yb_to_datum(str.data(), str.size(), &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_BINARY: {
      auto str = value.binary_value();
      *datum = type_entity->yb_to_datum(str.data(), str.size(), &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DECIMAL: {
      util::Decimal yb_decimal;
      if (!yb_decimal.DecodeFromComparable(value.decimal_value()).ok()) {
        return STATUS_SUBSTITUTE(InternalError,
                                  "Failed to deserialize DECIMAL from $1",
                                  value.decimal_value().ToDebugHexString());
      }
      auto plaintext = yb_decimal.ToString();
      auto val = const_cast<char *>(plaintext.c_str());
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(val),
                                        plaintext.size(),
                                        &type_attrs);
      break;
    }

    YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
    YB_PG_INVALID_TYPES_IN_SWITCH:
      return STATUS_SUBSTITUTE(InternalError, "unsupported type $0", type_entity->yb_type);
  }

  return Status::OK();
}

Status PBToDatum(const YBCPgTypeEntity *type_entity,
                 YBCPgTypeAttrs type_attrs,
                 const QLValuePB& value,
                 uint64_t* datum,
                 bool* is_null) {
  if (IsNull(value)) {
    *is_null = true;
    return Status::OK();
  }
  *is_null = false;

  switch (type_entity->yb_type) {
    case YB_YQL_DATA_TYPE_INT8: {
      int8_t val = value.int8_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT16: {
      int16_t val = value.int16_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT32: {
      int32_t val = value.int32_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_INT64: {
      int64_t val = value.int64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_UINT32: {
      int32_t val = value.uint32_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_UINT64: {
      int64_t val = value.uint64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_STRING: {
      auto str = value.string_value();
      *datum = type_entity->yb_to_datum(str.data(), str.size(), &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_BOOL: {
      auto val = value.bool_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_FLOAT: {
      auto val = value.float_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DOUBLE: {
      auto val = value.double_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_BINARY: {
      auto str = value.binary_value();
      *datum = type_entity->yb_to_datum(str.data(), str.size(), &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_TIMESTAMP: {
      auto val = value.int64_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_DECIMAL: {
      util::Decimal yb_decimal;
      RETURN_NOT_OK_PREPEND(
          yb_decimal.DecodeFromComparable(value.decimal_value()),
          Format("Failed to deserialize DECIMAL from $0", value.decimal_value()));

      std::string plaintext;
      RETURN_NOT_OK(yb_decimal.ToPointString(&plaintext, std::numeric_limits<int32_t>::max()));
      auto val = const_cast<char *>(plaintext.c_str());
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(val),
                                        plaintext.size(),
                                        &type_attrs);
      break;
    }

    case YB_YQL_DATA_TYPE_GIN_NULL: {
      auto val = value.gin_null_value();
      *datum = type_entity->yb_to_datum(reinterpret_cast<uint8_t *>(&val), 0, &type_attrs);
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
                   QLValuePB* ql_value) {
  if (is_null) {
    SetNull(ql_value);
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
