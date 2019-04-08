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
//--------------------------------------------------------------------------------------------------

#include <unordered_map>

#include "yb/client/schema.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_dml.h"
#include "yb/util/string_util.h"

#include "postgres/src/include/pg_config_manual.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::placeholders::_1;
using std::placeholders::_2;

//--------------------------------------------------------------------------------------------------
// Mapping Postgres operator names to YugaByte opcodes.
// When constructing expresions, Postgres layer will pass the operator name.
const std::unordered_map<string, PgExpr::Opcode> kOperatorNames = {
  { "!", PgExpr::Opcode::PG_EXPR_NOT },
  { "not", PgExpr::Opcode::PG_EXPR_NOT },
  { "=", PgExpr::Opcode::PG_EXPR_EQ },
  { "<>", PgExpr::Opcode::PG_EXPR_NE },
  { "!=", PgExpr::Opcode::PG_EXPR_NE },
  { ">", PgExpr::Opcode::PG_EXPR_GT },
  { ">=", PgExpr::Opcode::PG_EXPR_GE },
  { "<", PgExpr::Opcode::PG_EXPR_LT },
  { "<=", PgExpr::Opcode::PG_EXPR_LE },

  { "avg", PgExpr::Opcode::PG_EXPR_AVG },
  { "sum", PgExpr::Opcode::PG_EXPR_SUM },
  { "count", PgExpr::Opcode::PG_EXPR_COUNT },
  { "max", PgExpr::Opcode::PG_EXPR_MAX },
  { "min", PgExpr::Opcode::PG_EXPR_MIN },
};

PgExpr::PgExpr(Opcode opcode, const YBCPgTypeEntity *type_entity)
    : opcode_(opcode), type_entity_(type_entity) , type_attrs_({0}) {
  DCHECK(type_entity_) << "Datatype of result must be specified for expression";
  DCHECK(type_entity_->yb_type != YB_YQL_DATA_TYPE_NOT_SUPPORTED &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_UNKNOWN_DATA &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_NULL_VALUE_TYPE)
    << "Invalid datatype for YSQL expressions";
  DCHECK(type_entity_->datum_to_yb) << "Conversion from datum to YB format not defined";
  DCHECK(type_entity_->yb_to_datum) << "Conversion from YB to datum format not defined";
}

PgExpr::PgExpr(Opcode opcode, const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs)
    : opcode_(opcode), type_entity_(type_entity), type_attrs_(*type_attrs) {
  DCHECK(type_entity_) << "Datatype of result must be specified for expression";
  DCHECK(type_entity_->yb_type != YB_YQL_DATA_TYPE_NOT_SUPPORTED &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_UNKNOWN_DATA &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_NULL_VALUE_TYPE)
    << "Invalid datatype for YSQL expressions";
  DCHECK(type_entity_->datum_to_yb) << "Conversion from datum to YB format not defined";
  DCHECK(type_entity_->yb_to_datum) << "Conversion from YB to datum format not defined";
}

PgExpr::PgExpr(const char *opname, const YBCPgTypeEntity *type_entity)
    : PgExpr(NameToOpcode(opname), type_entity) {
}

PgExpr::~PgExpr() {
}

Status PgExpr::CheckOperatorName(const char *name) {
  auto iter = kOperatorNames.find(name);
  if (iter == kOperatorNames.end()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Wrong operator name: $0", name);
  }
  return Status::OK();
}

PgExpr::Opcode PgExpr::NameToOpcode(const char *name) {
  auto iter = kOperatorNames.find(name);
  DCHECK(iter != kOperatorNames.end()) << "Wrong operator name: " << name;
  return iter->second;
}

Status PgExpr::PrepareForRead(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // For expression that doesn't need to be setup and prepared at construction time.
  return Status::OK();
}

Status PgExpr::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // Expressions that are neither bind_variable nor constant don't need to be updated.
  // Only values for bind variables and constants need to be updated in the SQL requests.
  return Status::OK();
}

void PgExpr::TranslateText(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t text_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &text_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(yb_cursor->cdata(), text_size, type_attrs));
  yb_cursor->remove_prefix(text_size);
}

void PgExpr::TranslateBinary(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                             const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                             PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }
  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(yb_cursor->data(), data_size, type_attrs));
  yb_cursor->remove_prefix(data_size);
}


// Expects a serialized string representation of YB Decimal.
void PgExpr::TranslateDecimal(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                              const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                              PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  std::string serialized_decimal = yb_cursor->ToBuffer();
  yb_cursor->remove_prefix(data_size);

  util::Decimal yb_decimal;
  if (!yb_decimal.DecodeFromComparable(serialized_decimal).ok()) {
    LOG(FATAL) << "Failed to deserialize DECIMAL from " << serialized_decimal;
    return;
  }
  auto plaintext = yb_decimal.ToString();

  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(plaintext.c_str(), data_size, type_attrs));
}

//--------------------------------------------------------------------------------------------------
// Translating system columns.
void PgExpr::TranslateSysCol(Slice *yb_cursor, const PgWireDataHeader& header, PgTuple *pg_tuple,
                             uint8_t **pgbuf) {
  *pgbuf = nullptr;
  if (header.is_null()) {
    return;
  }

  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->Write(pgbuf, header, yb_cursor->data(), data_size);
  yb_cursor->remove_prefix(data_size);
}

void PgExpr::TranslateCtid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  TranslateSysCol<uint64_t>(yb_cursor, header, &pg_tuple->syscols()->ctid);
}

void PgExpr::TranslateOid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                          const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                          PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->oid);
}

void PgExpr::TranslateTableoid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                               const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                               PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->tableoid);
}

void PgExpr::TranslateXmin(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmin);
}

void PgExpr::TranslateCmin(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmin);
}

void PgExpr::TranslateXmax(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmax);
}

void PgExpr::TranslateCmax(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                           PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmax);
}

void PgExpr::TranslateYBCtid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                             const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                             PgTuple *pg_tuple) {
  TranslateSysCol(yb_cursor, header, pg_tuple, &pg_tuple->syscols()->ybctid);
}

void PgExpr::TranslateYBBasectid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                                 const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                                 PgTuple *pg_tuple) {
  TranslateSysCol(yb_cursor, header, pg_tuple, &pg_tuple->syscols()->ybbasectid);
}

Status PgExpr::ReadHashValue(const char *doc_key, int key_size, uint16_t *hash_value) {
  // Because DocDB is using its own encoding for the key, we hack the system to read hash_value.
  if (doc_key == NULL || key_size < sizeof(hash_value) + 1) {
    return STATUS(InvalidArgument, "Key has unexpected size");
  }
  *hash_value = BigEndian::Load16(doc_key + 1);
  return Status::OK();
}

InternalType PgExpr::internal_type() const {
  DCHECK(type_entity_) << "Type entity is not set up";
  return client::YBColumnSchema::ToInternalDataType(
      QLType::Create(static_cast<DataType>(type_entity_->yb_type)));
}

//--------------------------------------------------------------------------------------------------

PgConstant::PgConstant(const YBCPgTypeEntity *type_entity, uint64_t datum, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, type_entity) {

  switch (type_entity_->yb_type) {
    case YB_YQL_DATA_TYPE_INT8:
      if (!is_null) {
        int8_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int8_value(value);
      }
      translate_data_ = TranslateNumber<int8_t>;
      break;

    case YB_YQL_DATA_TYPE_INT16:
      if (!is_null) {
        int16_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int16_value(value);
      }
      translate_data_ = TranslateNumber<int16_t>;
      break;

    case YB_YQL_DATA_TYPE_INT32:
      if (!is_null) {
        int32_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int32_value(value);
      }
      translate_data_ = TranslateNumber<int32_t>;
      break;

    case YB_YQL_DATA_TYPE_INT64:
      if (!is_null) {
        int64_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int64_value(value);
      }
      translate_data_ = TranslateNumber<int64_t>;
      break;

    case YB_YQL_DATA_TYPE_STRING:
      if (!is_null) {
        char *value;
        int64_t bytes = type_entity_->datum_fixed_size;
        type_entity_->datum_to_yb(datum, &value, &bytes);
        ql_value_.set_string_value(value, bytes);
      }
      translate_data_ = TranslateText;
      break;

    case YB_YQL_DATA_TYPE_BOOL:
      if (!is_null) {
        bool value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_bool_value(value);
      }
      translate_data_ = TranslateNumber<bool>;
      break;

    case YB_YQL_DATA_TYPE_FLOAT:
      if (!is_null) {
        float value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_float_value(value);
      }
      translate_data_ = TranslateNumber<float>;
      break;

    case YB_YQL_DATA_TYPE_DOUBLE:
      if (!is_null) {
        double value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_double_value(value);
      }
      translate_data_ = TranslateNumber<double>;
      break;

    case YB_YQL_DATA_TYPE_BINARY:
      if (!is_null) {
        uint8_t *value;
        int64_t bytes = type_entity_->datum_fixed_size;
        type_entity_->datum_to_yb(datum, &value, &bytes);
        ql_value_.set_binary_value(value, bytes);
      }
      translate_data_ = TranslateBinary;
      break;

    case YB_YQL_DATA_TYPE_TIMESTAMP:
      if (!is_null) {
        int64_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int64_value(value);
      }
      translate_data_ = TranslateNumber<int64_t>;
      break;

    case YB_YQL_DATA_TYPE_DECIMAL:
      if (!is_null) {
        char* plaintext;
        // Calls YBCDatumToDecimalText in ybctype.c
        type_entity_->datum_to_yb(datum, &plaintext, nullptr);
        util::Decimal yb_decimal(plaintext);
        ql_value_.set_decimal_value(yb_decimal.EncodeToComparable());
      }
      translate_data_ = TranslateDecimal;
      break;

    case YB_YQL_DATA_TYPE_VARINT:
    case YB_YQL_DATA_TYPE_INET:
    case YB_YQL_DATA_TYPE_LIST:
    case YB_YQL_DATA_TYPE_MAP:
    case YB_YQL_DATA_TYPE_SET:
    case YB_YQL_DATA_TYPE_UUID:
    case YB_YQL_DATA_TYPE_TIMEUUID:
    case YB_YQL_DATA_TYPE_TUPLE:
    case YB_YQL_DATA_TYPE_TYPEARGS:
    case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE:
    case YB_YQL_DATA_TYPE_FROZEN:
    case YB_YQL_DATA_TYPE_DATE: // Not used for PG storage
    case YB_YQL_DATA_TYPE_TIME: // Not used for PG storage
    case YB_YQL_DATA_TYPE_JSONB:
    case YB_YQL_DATA_TYPE_UINT8:
    case YB_YQL_DATA_TYPE_UINT16:
    case YB_YQL_DATA_TYPE_UINT32:
    case YB_YQL_DATA_TYPE_UINT64:
    default:
      LOG(DFATAL) << "Internal error: unsupported type " << type_entity_->yb_type;
  }
}

PgConstant::~PgConstant() {
}

void PgConstant::UpdateConstant(int8_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int8_value(value);
  }
}

void PgConstant::UpdateConstant(int16_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int16_value(value);
  }
}

void PgConstant::UpdateConstant(int32_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int32_value(value);
  }
}

void PgConstant::UpdateConstant(int64_t value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_int64_value(value);
  }
}

void PgConstant::UpdateConstant(float value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_float_value(value);
  }
}

void PgConstant::UpdateConstant(double value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_double_value(value);
  }
}

void PgConstant::UpdateConstant(const char *value, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_string_value(value);
  }
}

void PgConstant::UpdateConstant(const void *value, size_t bytes, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    ql_value_.set_binary_value(value, bytes);
  }
}

Status PgConstant::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  QLValuePB *result = expr_pb->mutable_value();
  *result = ql_value_;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgColumnRef::PgColumnRef(int attr_num,
                         const YBCPgTypeEntity *type_entity,
                         const PgTypeAttrs *type_attrs)
    : PgExpr(PgExpr::Opcode::PG_EXPR_COLREF, type_entity, type_attrs), attr_num_(attr_num) {

  if (attr_num_ < 0) {
    // Setup system columns.
    switch (attr_num_) {
      case static_cast<int>(PgSystemAttrNum::kSelfItemPointer):
        translate_data_ = TranslateCtid;
        break;
      case static_cast<int>(PgSystemAttrNum::kObjectId):
        translate_data_ = TranslateOid;
        break;
      case static_cast<int>(PgSystemAttrNum::kMinTransactionId):
        translate_data_ = TranslateXmin;
        break;
      case static_cast<int>(PgSystemAttrNum::kMinCommandId):
        translate_data_ = TranslateCmin;
        break;
      case static_cast<int>(PgSystemAttrNum::kMaxTransactionId):
        translate_data_ = TranslateXmax;
        break;
      case static_cast<int>(PgSystemAttrNum::kMaxCommandId):
        translate_data_ = TranslateCmax;
        break;
      case static_cast<int>(PgSystemAttrNum::kTableOid):
        translate_data_ = TranslateTableoid;
        break;
      case static_cast<int>(PgSystemAttrNum::kYBTupleId):
        translate_data_ = TranslateYBCtid;
        break;
      case static_cast<int>(PgSystemAttrNum::kYBBaseTupleId):
        translate_data_ = TranslateYBBasectid;
        break;
    }
  } else {
    // Setup regular columns.
    switch (type_entity_->yb_type) {
      case YB_YQL_DATA_TYPE_INT8:
        translate_data_ = TranslateNumber<int8_t>;
        break;

      case YB_YQL_DATA_TYPE_INT16:
        translate_data_ = TranslateNumber<int16_t>;
        break;

      case YB_YQL_DATA_TYPE_INT32:
        translate_data_ = TranslateNumber<int32_t>;
        break;

      case YB_YQL_DATA_TYPE_INT64:
        translate_data_ = TranslateNumber<int64_t>;
        break;

      case YB_YQL_DATA_TYPE_STRING:
        translate_data_ = TranslateText;
        break;

      case YB_YQL_DATA_TYPE_BOOL:
        translate_data_ = TranslateNumber<bool>;
        break;

      case YB_YQL_DATA_TYPE_FLOAT:
        translate_data_ = TranslateNumber<float>;
        break;

      case YB_YQL_DATA_TYPE_DOUBLE:
        translate_data_ = TranslateNumber<double>;
        break;

      case YB_YQL_DATA_TYPE_BINARY:
        translate_data_ = TranslateBinary;
        break;

      case YB_YQL_DATA_TYPE_TIMESTAMP:
        translate_data_ = TranslateNumber<int64_t>;
        break;

      case YB_YQL_DATA_TYPE_DECIMAL:
        translate_data_ = TranslateDecimal;
        break;

      case YB_YQL_DATA_TYPE_VARINT:
      case YB_YQL_DATA_TYPE_INET:
      case YB_YQL_DATA_TYPE_LIST:
      case YB_YQL_DATA_TYPE_MAP:
      case YB_YQL_DATA_TYPE_SET:
      case YB_YQL_DATA_TYPE_UUID:
      case YB_YQL_DATA_TYPE_TIMEUUID:
      case YB_YQL_DATA_TYPE_TUPLE:
      case YB_YQL_DATA_TYPE_TYPEARGS:
      case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE:
      case YB_YQL_DATA_TYPE_FROZEN:
      case YB_YQL_DATA_TYPE_DATE: // Not used for PG storage
      case YB_YQL_DATA_TYPE_TIME: // Not used for PG storage
      case YB_YQL_DATA_TYPE_JSONB:
      case YB_YQL_DATA_TYPE_UINT8:
      case YB_YQL_DATA_TYPE_UINT16:
      case YB_YQL_DATA_TYPE_UINT32:
      case YB_YQL_DATA_TYPE_UINT64:
      default:
        LOG(DFATAL) << "Internal error: unsupported type " << type_entity_->yb_type;
    }
  }
}

PgColumnRef::~PgColumnRef() {
}

Status PgColumnRef::PrepareForRead(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  const PgColumn *col;
  RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(attr_num_, expr_pb, &col));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgOperator::PgOperator(const char *opname, const YBCPgTypeEntity *type_entity)
  : PgExpr(opname, type_entity), opname_(opname) {
}

PgOperator::~PgOperator() {
}

void PgOperator::AppendArg(PgExpr *arg) {
  args_.push_back(arg);
}

//--------------------------------------------------------------------------------------------------
namespace {
#define POSTGRESQL_BYTEAOID 17
};

PgGenerateRowId::PgGenerateRowId() :
    PgExpr(Opcode::PG_EXPR_GENERATE_ROWID, YBCPgFindTypeEntity(POSTGRESQL_BYTEAOID)) {
}

PgGenerateRowId::~PgGenerateRowId() {
}

Status PgGenerateRowId::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  expr_pb->mutable_value()->set_binary_value(pg_stmt->pg_session()->GenerateNewRowid());
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
