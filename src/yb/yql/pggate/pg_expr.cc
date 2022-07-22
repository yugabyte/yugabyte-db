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
#include "yb/common/ql_type.h"
#include "yb/common/pg_system_attr.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/util/decimal.h"
#include "yb/util/flag_tags.h"
#include "yb/util/status_format.h"


DEFINE_test_flag(bool, do_not_add_enum_sort_order, false,
                 "Do not add enum type sort order when buidling a constant "
                 "for an enum type value. Used to test database upgrade "
                 "where we have pre-existing enum type column values that "
                 "did not have sort order added.");

namespace yb {
namespace pggate {

using std::make_shared;
using std::placeholders::_1;
using std::placeholders::_2;

namespace {
// Collation flags. kCollationMarker ensures the collation byte is non-zero.
constexpr uint8_t kDeterministicCollation = 0x01;
constexpr uint8_t kCollationMarker = 0x80;

Slice MakeCollationEncodedString(
  Arena* arena, const char* value, int64_t bytes, uint8_t collation_flags, const char* sortkey) {
  // A postgres character value cannot have \0 byte.
  DCHECK(memchr(value, '\0', bytes) == nullptr);

  // We need to build a collation encoded string to include both the
  // collation sortkey and the original character value.
  size_t sortkey_len = strlen(sortkey);
  size_t len = 2 + sortkey_len + 1 + bytes;
  char* result = static_cast<char*>(arena->AllocateBytes(len));

  char* buf = result;
  // We set the first byte to '\0' which indicates that collstr is
  // collation encoded. We also put the collation flags byte in case
  // it is of any use in the future.
  *buf++ = 0;
  static_assert(sizeof(collation_flags) == 1, "invalid size");
  *buf++ = collation_flags;

  // Add the sort key. This will appends a copy of sortkey. The sortkey itself
  // was allocated using palloc and therefore will be freed automatically at
  // the end of each transaction.
  memcpy(buf, sortkey, sortkey_len);
  buf += sortkey_len;

  // Append a \0 byte which acts as a separator between sort key and the
  // original value.
  *buf++ = 0;

  // Add the original value.
  memcpy(buf, value, bytes);
  DCHECK_EQ(buf - result, len - bytes);
  return Slice(result, len);
}

} // namespace

void DecodeCollationEncodedString(const char** text_ptr, int64_t* text_len_ptr) {
  DCHECK(*text_len_ptr >= 0 && (*text_ptr)[*text_len_ptr] == '\0')
    << "Data received from DocDB does not have expected format";
  // is_original_value = true means that we have done storage space optimization
  // to only store the original value for non-key columns.
  const bool is_original_value = (*text_len_ptr == 0 || (*text_ptr)[0] != '\0');
  if (!is_original_value) {
    // This is a collation encoded string, we need to fetch the original value.
    CHECK_GE(*text_len_ptr, 3);
    uint8_t collation_flags = (*text_ptr)[1];
    CHECK_EQ(collation_flags, kCollationMarker | kDeterministicCollation);
    // Skip the collation and sortkey get the original character value.
    const char *p = static_cast<const char*>(memchr(*text_ptr + 2, '\0', *text_len_ptr - 2));
    CHECK(p);
    ++p;
    const char* end = *text_ptr + *text_len_ptr;
    CHECK_LE(p, end);
    // update *text_ptr && *text_len_ptr to reflect original string value
    *text_ptr = p;
    *text_len_ptr = end - p;
  }
}

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
  { "eval_expr_call", PgExpr::Opcode::PG_EXPR_EVAL_EXPR_CALL }
};

PgExpr::PgExpr(Opcode opcode,
               const YBCPgTypeEntity *type_entity,
               bool collate_is_valid_non_c,
               const PgTypeAttrs *type_attrs)
    : opcode_(opcode), type_entity_(type_entity),
      collate_is_valid_non_c_(collate_is_valid_non_c),
      type_attrs_(type_attrs ? *type_attrs : PgTypeAttrs({0})) {
  DCHECK(type_entity_) << "Datatype of result must be specified for expression";
  DCHECK(type_entity_->yb_type != YB_YQL_DATA_TYPE_NOT_SUPPORTED &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_UNKNOWN_DATA &&
         type_entity_->yb_type != YB_YQL_DATA_TYPE_NULL_VALUE_TYPE)
    << "Invalid datatype for YSQL expressions";
  DCHECK(type_entity_->datum_to_yb) << "Conversion from datum to YB format not defined";
  DCHECK(type_entity_->yb_to_datum) << "Conversion from YB to datum format not defined";
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

bfpg::TSOpcode PgExpr::PGOpcodeToTSOpcode(const PgExpr::Opcode opcode) {
  switch (opcode) {
    case Opcode::PG_EXPR_COUNT:
      return bfpg::TSOpcode::kCount;

    case Opcode::PG_EXPR_MAX:
      return bfpg::TSOpcode::kMax;

    case Opcode::PG_EXPR_MIN:
      return bfpg::TSOpcode::kMin;

    case Opcode::PG_EXPR_EVAL_EXPR_CALL:
      return bfpg::TSOpcode::kPgEvalExprCall;

    default:
      LOG(DFATAL) << "No supported TSOpcode for PG opcode: " << static_cast<int32_t>(opcode);
      return bfpg::TSOpcode::kNoOp;
  }
}

bfpg::TSOpcode PgExpr::OperandTypeToSumTSOpcode(InternalType type) {
  switch (type) {
    case InternalType::kInt8Value:
      return bfpg::TSOpcode::kSumInt8;

    case InternalType::kInt16Value:
      return bfpg::TSOpcode::kSumInt16;

    case InternalType::kInt32Value:
      return bfpg::TSOpcode::kSumInt32;

    case InternalType::kInt64Value:
      return bfpg::TSOpcode::kSumInt64;

    case InternalType::kFloatValue:
      return bfpg::TSOpcode::kSumFloat;

    case InternalType::kDoubleValue:
      return bfpg::TSOpcode::kSumDouble;

    default:
      LOG(DFATAL) << "No supported Sum TSOpcode for operand type: " << static_cast<int32_t>(type);
      return bfpg::TSOpcode::kNoOp;
  }
}

Status PgExpr::PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) {
  // For expression that doesn't need to be setup and prepared at construction time.
  return Status::OK();
}

Status PgExpr::EvalTo(LWPgsqlExpressionPB *expr_pb) {
  auto value = VERIFY_RESULT(Eval());
  if (value) {
    expr_pb->ref_value(value);
  }
  return Status::OK();
}

Status PgExpr::EvalTo(LWQLValuePB *out) {
  auto value = VERIFY_RESULT(Eval());
  if (value) {
    *out = *value;
  }
  return Status::OK();
}

Result<LWQLValuePB*> PgExpr::Eval() {
  // Expressions that are neither bind_variable nor constant don't need to be updated.
  // Only values for bind variables and constants need to be updated in the SQL requests.
  return nullptr;
}

namespace {

// Translate system column.
template<typename data_type>
void TranslateSysCol(Slice *yb_cursor, const PgWireDataHeader& header, data_type *value) {
  *value = 0;
  if (header.is_null()) {
    // 0 is an invalid OID.
    return;
  }
  size_t read_size = PgDocData::ReadNumber(yb_cursor, value);
  yb_cursor->remove_prefix(read_size);
}

void TranslateText(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  // Get data from RPC buffer.
  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  // Expects data from DocDB matches the following format.
  // - Right trim spaces for CHAR type. This should be done by DocDB when evaluate SELECTed or
  //   RETURNed expression. Note that currently, Postgres layer (and not DocDB) evaluate
  //   expressions, so DocDB doesn't trim for CHAR type.
  // - NULL terminated string. This should be done by DocDB when serializing.
  // - Text size == strlen(). When sending data over the network, RPC layer would use the actual
  //   size of data being serialized including the '\0' character. This is not necessarily be the
  //   length of a string.
  // Find strlen() of STRING by right-trimming all '\0' characters.
  const char* text = yb_cursor->cdata();
  int64_t text_len = data_size - 1;

  DCHECK(text_len >= 0 && text[text_len] == '\0' && (text_len == 0 || text[text_len - 1] != '\0'))
    << "Data received from DocDB does not have expected format";

  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(text, text_len, type_attrs));
  yb_cursor->remove_prefix(data_size);
}

void TranslateCollateText(
    Slice *yb_cursor, const PgWireDataHeader& header, int index,
    const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs, PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  // Get data from RPC buffer.
  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  // See comments in PgExpr::TranslateText.
  const char* text = yb_cursor->cdata();
  int64_t text_len = data_size - 1;

  DecodeCollationEncodedString(&text, &text_len);
  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(text, text_len, type_attrs));
  yb_cursor->remove_prefix(data_size);
}

void TranslateBinary(Slice *yb_cursor, const PgWireDataHeader& header, int index,
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
void TranslateDecimal(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                      const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                      PgTuple *pg_tuple) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  // Get the value size.
  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  // Read the decimal value from Protobuf and decode it to internal format.
  std::string serialized_decimal;
  read_size = PgDocData::ReadString(yb_cursor, &serialized_decimal, data_size);
  yb_cursor->remove_prefix(read_size);
  util::Decimal yb_decimal;
  if (!yb_decimal.DecodeFromComparable(serialized_decimal).ok()) {
    LOG(FATAL) << "Failed to deserialize DECIMAL from " << serialized_decimal;
    return;
  }

  // Translate to decimal format and write to datum.
  auto plaintext = yb_decimal.ToString();
  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(plaintext.c_str(), data_size, type_attrs));
}

//--------------------------------------------------------------------------------------------------
// Translating system columns.
void TranslateSysCol(Slice *yb_cursor, const PgWireDataHeader& header, PgTuple *pg_tuple,
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

bool TranslateNumberHelper(
    const PgWireDataHeader& header, int index, const YBCPgTypeEntity *type_entity,
    PgTuple *pg_tuple) {
  if (header.is_null()) {
    pg_tuple->WriteNull(index, header);
    return true;
  }

  DCHECK(type_entity) << "Type entity not provided";
  DCHECK(type_entity->yb_to_datum) << "Type entity converter not provided";
  return false;
}

// Implementation for "translate_data()" for each supported datatype.
// Translates DocDB-numeric datatypes.
template<typename data_type>
void TranslateNumber(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                     const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                     PgTuple *pg_tuple) {
  if (TranslateNumberHelper(header, index, type_entity, pg_tuple)) {
    return;
  }

  data_type result = 0;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &result);
  yb_cursor->remove_prefix(read_size);
  pg_tuple->WriteDatum(index, type_entity->yb_to_datum(&result, read_size, type_attrs));
}

void TranslateCtid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  TranslateSysCol<uint64_t>(yb_cursor, header, &pg_tuple->syscols()->ctid);
}

void TranslateOid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                  const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                  PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->oid);
}

void TranslateTableoid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                       const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                       PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->tableoid);
}

void TranslateXmin(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmin);
}

void TranslateCmin(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmin);
}

void TranslateXmax(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmax);
}

void TranslateCmax(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                   const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                   PgTuple *pg_tuple) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmax);
}

void TranslateYBCtid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                     const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                     PgTuple *pg_tuple) {
  TranslateSysCol(yb_cursor, header, pg_tuple, &pg_tuple->syscols()->ybctid);
}

void TranslateYBBasectid(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                         const YBCPgTypeEntity *type_entity, const PgTypeAttrs *type_attrs,
                         PgTuple *pg_tuple) {
  TranslateSysCol(yb_cursor, header, pg_tuple, &pg_tuple->syscols()->ybbasectid);
}

} // namespace

void PgExpr::TranslateData(Slice *yb_cursor, const PgWireDataHeader& header, int index,
                           PgTuple *pg_tuple) const {
  CHECK(translate_data_) << "Data format translation is not provided";
  translate_data_(yb_cursor, header, index, type_entity_, &type_attrs_, pg_tuple);
}

InternalType PgExpr::internal_type() const {
  DCHECK(type_entity_) << "Type entity is not set up";
  return client::YBColumnSchema::ToInternalDataType(
      QLType::Create(static_cast<DataType>(type_entity_->yb_type)));
}

int PgExpr::get_pg_typid() const {
  return type_entity_->type_oid;
}

int PgExpr::get_pg_typmod() const {
  return type_attrs_.typmod;
}

int PgExpr::get_pg_collid() const {
  // We do not support collations in DocDB, in future a field should be added to set, store and
  // pass around a collation id. For now, return a dummy value.
  // TODO
  return 0;  /* InvalidOid */
}

void PgExpr::InitializeTranslateData() {
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

    case YB_YQL_DATA_TYPE_UINT32:
      translate_data_ = TranslateNumber<uint32_t>;
      break;

    case YB_YQL_DATA_TYPE_UINT64:
      translate_data_ = TranslateNumber<uint64_t>;
      break;

    case YB_YQL_DATA_TYPE_STRING:
      if (collate_is_valid_non_c_) {
        translate_data_ = TranslateCollateText;
      } else {
        translate_data_ = TranslateText;
      }
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

    case YB_YQL_DATA_TYPE_GIN_NULL:
      translate_data_ = TranslateNumber<uint8_t>;
      break;

    YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
    YB_PG_INVALID_TYPES_IN_SWITCH:
      LOG(DFATAL) << "Internal error: unsupported type " << type_entity_->yb_type;
  }
}

//--------------------------------------------------------------------------------------------------

PgConstant::PgConstant(Arena* arena,
                       const YBCPgTypeEntity *type_entity,
                       bool collate_is_valid_non_c,
                       const char *collation_sortkey,
                       uint64_t datum,
                       bool is_null,
                       PgExpr::Opcode opcode)
    : PgExpr(opcode, type_entity, collate_is_valid_non_c),
      ql_value_(arena), collation_sortkey_(collation_sortkey) {

  switch (type_entity_->yb_type) {
    case YB_YQL_DATA_TYPE_INT8:
      if (!is_null) {
        int8_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int8_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_INT16:
      if (!is_null) {
        int16_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int16_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_INT32:
      if (!is_null) {
        int32_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int32_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_INT64:
      if (!is_null) {
        int64_t value;
        if (PREDICT_TRUE(!FLAGS_TEST_do_not_add_enum_sort_order)) {
          type_entity_->datum_to_yb(datum, &value, nullptr);
        } else {
          // pass &value as the third argument to tell datum_to_yb not
          // to add sort order to datum.
          type_entity_->datum_to_yb(datum, &value, &value);
        }
        ql_value_.set_int64_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_UINT32:
      if (!is_null) {
        uint32_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_uint32_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_UINT64:
      if (!is_null) {
        uint64_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_uint64_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_STRING:
      if (!is_null) {
        char *value;
        int64_t bytes = type_entity_->datum_fixed_size;
        type_entity_->datum_to_yb(datum, &value, &bytes);
        if (collate_is_valid_non_c_) {
          CHECK(collation_sortkey_);
          // Once YSQL supports non-deterministic collations, we need to compute
          // the deterministic attribute properly.
          ql_value_.ref_string_value(MakeCollationEncodedString(
              arena, value, bytes, kCollationMarker | kDeterministicCollation, collation_sortkey_));
        } else {
          CHECK(!collation_sortkey_);
          ql_value_.dup_string_value(Slice(value, bytes));
        }
      }
      break;

    case YB_YQL_DATA_TYPE_BOOL:
      if (!is_null) {
        bool value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_bool_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_FLOAT:
      if (!is_null) {
        float value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_float_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_DOUBLE:
      if (!is_null) {
        double value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_double_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_BINARY:
      if (!is_null) {
        uint8_t *value;
        int64_t bytes = type_entity_->datum_fixed_size;
        type_entity_->datum_to_yb(datum, &value, &bytes);
        ql_value_.dup_binary_value(Slice(value, bytes));
      }
      break;

    case YB_YQL_DATA_TYPE_TIMESTAMP:
      if (!is_null) {
        int64_t value;
        type_entity_->datum_to_yb(datum, &value, nullptr);
        ql_value_.set_int64_value(value);
      }
      break;

    case YB_YQL_DATA_TYPE_DECIMAL:
      if (!is_null) {
        char* plaintext;
        // Calls YBCDatumToDecimalText in yb_type.c
        type_entity_->datum_to_yb(datum, &plaintext, nullptr);
        util::Decimal yb_decimal(plaintext);
        ql_value_.dup_decimal_value(yb_decimal.EncodeToComparable());
      }
      break;

    case YB_YQL_DATA_TYPE_GIN_NULL:
      CHECK(is_null) << "gin null type should be marked null";
      uint8_t value;
      type_entity_->datum_to_yb(datum, &value, nullptr);
      ql_value_.set_gin_null_value(value);
      break;

    YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
    YB_PG_INVALID_TYPES_IN_SWITCH:
      LOG(DFATAL) << "Internal error: unsupported type " << type_entity_->yb_type;
  }

  InitializeTranslateData();
}

PgConstant::PgConstant(Arena* arena,
                       const YBCPgTypeEntity *type_entity,
                       bool collate_is_valid_non_c,
                       PgDatumKind datum_kind,
                       PgExpr::Opcode opcode)
    : PgExpr(opcode, type_entity, collate_is_valid_non_c), ql_value_(arena),
      collation_sortkey_(nullptr) {
  switch (datum_kind) {
    case PgDatumKind::YB_YQL_DATUM_STANDARD_VALUE:
      // Leave the result as NULL.
      break;
    case PgDatumKind::YB_YQL_DATUM_LIMIT_MAX:
      ql_value_.set_virtual_value(QLVirtualValuePB::LIMIT_MAX);
      break;
    case PgDatumKind::YB_YQL_DATUM_LIMIT_MIN:
      ql_value_.set_virtual_value(QLVirtualValuePB::LIMIT_MIN);
      break;
  }
  InitializeTranslateData();
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
    // Currently this is only used in C++ test code. In the future if this
    // is used in production code we need to consider collation encoding.
    CHECK(!collate_is_valid_non_c_);
    ql_value_.dup_string_value(value);
  }
}

void PgConstant::UpdateConstant(const void *value, size_t bytes, bool is_null) {
  if (is_null) {
    ql_value_.Clear();
  } else {
    // Currently this is only used in C++ test code. In the future if this
    // is used in production code we need to consider collation encoding.
    CHECK(!collate_is_valid_non_c_);
    ql_value_.dup_binary_value(Slice(static_cast<const char*>(value), bytes));
  }
}

Result<LWQLValuePB*> PgConstant::Eval() {
  return &ql_value_;
}

//--------------------------------------------------------------------------------------------------

PgColumnRef::PgColumnRef(int attr_num,
                         const YBCPgTypeEntity *type_entity,
                         bool collate_is_valid_non_c,
                         const PgTypeAttrs *type_attrs)
    : PgExpr(PgExpr::Opcode::PG_EXPR_COLREF, type_entity,
             collate_is_valid_non_c, type_attrs), attr_num_(attr_num) {

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
      case static_cast<int>(PgSystemAttrNum::kYBIdxBaseTupleId):
        translate_data_ = TranslateYBBasectid;
        break;
    }
  } else {
    // Setup regular columns.
    InitializeTranslateData();
  }
}

bool PgColumnRef::is_ybbasetid() const {
  return attr_num_ == static_cast<int>(PgSystemAttrNum::kYBIdxBaseTupleId);
}

Status PgColumnRef::PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) {
  RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(attr_num_, expr_pb));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgOperator::PgOperator(Arena* arena,
                       const char *opname,
                       const YBCPgTypeEntity *type_entity,
                       bool collate_is_valid_non_c)
    : PgExpr(NameToOpcode(opname), type_entity, collate_is_valid_non_c),
      opname_(opname), args_(arena) {
  InitializeTranslateData();
}

void PgOperator::AppendArg(PgExpr *arg) {
  args_.push_back_ref(arg);
}

Status PgOperator::PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) {
  auto *tscall = expr_pb->mutable_tscall();
  bfpg::TSOpcode tsopcode;
  if (opcode_ == Opcode::PG_EXPR_SUM) {
    // SUM is special case as it has input type of the operand column but output
    // type of a larger similar type (e.g. INT64 for integers).
    tsopcode = OperandTypeToSumTSOpcode(args_.front().internal_type());
  } else {
    tsopcode = PGOpcodeToTSOpcode(opcode_);
  }
  tscall->set_opcode(static_cast<int32_t>(tsopcode));
  for (auto& arg : args_) {
    LWPgsqlExpressionPB *op = tscall->add_operands();
    RETURN_NOT_OK(arg.PrepareForRead(pg_stmt, op));
    RETURN_NOT_OK(arg.EvalTo(op));
  }
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
