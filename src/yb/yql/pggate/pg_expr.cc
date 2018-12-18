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

#include "yb/yql/pggate/pg_expr.h"

#include <unordered_map>

#include "yb/yql/pggate/pg_dml.h"

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

PgExpr::PgExpr(PgExpr::Opcode opcode, InternalType internal_type)
    : opcode_(opcode), internal_type_(internal_type) {
}

PgExpr::PgExpr(const char *opname, InternalType internal_type)
    : PgExpr(NameToOpcode(opname), internal_type) {
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

Status PgExpr::Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // For expression that doesn't need to be setup and prepared at construction time.
  return Status::OK();
}

Status PgExpr::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  // Expressions that are neither bind_variable nor constant don't need to be updated.
  // Only values for bind variables and constants need to be updated in the SQL requests.
  return Status::OK();
}

void PgExpr::TranslateText(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t text_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &text_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->Write(index, header, yb_cursor->cdata(), text_size);
  yb_cursor->remove_prefix(text_size);
}

void PgExpr::TranslateBinary(Slice *yb_cursor, const PgWireDataHeader& header,
                             PgTuple *pg_tuple, int index) {
  if (header.is_null()) {
    return pg_tuple->WriteNull(index, header);
  }

  int64_t data_size;
  size_t read_size = PgDocData::ReadNumber(yb_cursor, &data_size);
  yb_cursor->remove_prefix(read_size);

  pg_tuple->Write(index, header, yb_cursor->data(), data_size);
  yb_cursor->remove_prefix(data_size);
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

void PgExpr::TranslateCtid(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint64_t>(yb_cursor, header, &pg_tuple->syscols()->ctid);
}

void PgExpr::TranslateOid(Slice *yb_cursor, const PgWireDataHeader& header,
                          PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->oid);
}

void PgExpr::TranslateTableoid(Slice *yb_cursor, const PgWireDataHeader& header,
                               PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->tableoid);
}

void PgExpr::TranslateXmin(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmin);
}

void PgExpr::TranslateCmin(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmin);
}

void PgExpr::TranslateXmax(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->xmax);
}

void PgExpr::TranslateCmax(Slice *yb_cursor, const PgWireDataHeader& header,
                           PgTuple *pg_tuple, int index) {
  TranslateSysCol<uint32_t>(yb_cursor, header, &pg_tuple->syscols()->cmax);
}

void PgExpr::TranslateYBCtid(Slice *yb_cursor, const PgWireDataHeader& header,
                             PgTuple *pg_tuple, int index) {
  TranslateSysCol(yb_cursor, header, pg_tuple, &pg_tuple->syscols()->ybctid);
}

Status PgExpr::ReadHashValue(const char *doc_key, int key_size, uint16_t *hash_value) {
  // Because DocDB is using its own encoding for the key, we hack the system to read hash_value.
  if (doc_key == NULL || key_size < sizeof(hash_value) + 1) {
    return STATUS(InvalidArgument, "Key has unexpected size");
  }
  *hash_value = BigEndian::Load16(doc_key + 1);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgConstant::PgConstant(bool value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kBoolValue) {
  if (!is_null) {
    ql_value_.set_bool_value(value);
  }
  TranslateData = TranslateNumber<bool>;
}

PgConstant::PgConstant(int8_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt8Value) {
  if (!is_null) {
    ql_value_.set_int8_value(value);
  }
  TranslateData = TranslateNumber<int8_t>;
}

PgConstant::PgConstant(int16_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt16Value) {
  if (!is_null) {
    ql_value_.set_int16_value(value);
  }
  TranslateData = TranslateNumber<int16_t>;
}

PgConstant::PgConstant(int32_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt32Value) {
  if (!is_null) {
    ql_value_.set_int32_value(value);
  }
  TranslateData = TranslateNumber<int32_t>;
}

PgConstant::PgConstant(int64_t value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kInt64Value) {
  if (!is_null) {
    ql_value_.set_int64_value(value);
  }
  TranslateData = TranslateNumber<int64_t>;
}

PgConstant::PgConstant(float value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kFloatValue) {
  if (!is_null) {
    ql_value_.set_float_value(value);
  }
  TranslateData = TranslateNumber<float>;
}

PgConstant::PgConstant(double value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kDoubleValue) {
  if (!is_null) {
    ql_value_.set_double_value(value);
  }
  TranslateData = TranslateNumber<double>;
}

PgConstant::PgConstant(const char *value, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kStringValue) {
  if (!is_null) {
    ql_value_.set_string_value(value);
  }
  TranslateData = TranslateText;
}

PgConstant::PgConstant(const void *value, size_t bytes, bool is_null)
    : PgExpr(PgExpr::Opcode::PG_EXPR_CONSTANT, InternalType::kBinaryValue) {
  if (!is_null) {
    ql_value_.set_binary_value(value, bytes);
  }
  TranslateData = TranslateBinary;
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

PgColumnRef::PgColumnRef(int attr_num)
  : PgExpr(PgExpr::Opcode::PG_EXPR_COLREF), attr_num_(attr_num) {
}

PgColumnRef::~PgColumnRef() {
}

Status PgColumnRef::Prepare(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  const PgColumn *col;
  RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(attr_num_, expr_pb, &col));

  if (attr_num_ < 0) {
    // Setup system columns.
    switch (attr_num_) {
      case static_cast<int>(PgSystemAttrNum::kSelfItemPointerAttributeNumber):
        TranslateData = TranslateCtid;
        break;
      case static_cast<int>(PgSystemAttrNum::kObjectIdAttributeNumber):
        TranslateData = TranslateOid;
        break;
      case static_cast<int>(PgSystemAttrNum::kMinTransactionIdAttributeNumber):
        TranslateData = TranslateXmin;
        break;
      case static_cast<int>(PgSystemAttrNum::kMinCommandIdAttributeNumber):
        TranslateData = TranslateCmin;
        break;
      case static_cast<int>(PgSystemAttrNum::kMaxTransactionIdAttributeNumber):
        TranslateData = TranslateXmax;
        break;
      case static_cast<int>(PgSystemAttrNum::kMaxCommandIdAttributeNumber):
        TranslateData = TranslateCmax;
        break;
      case static_cast<int>(PgSystemAttrNum::kTableOidAttributeNumber):
        TranslateData = TranslateTableoid;
        break;
      case static_cast<int>(PgSystemAttrNum::kYBTupleId):
        TranslateData = TranslateYBCtid;
        break;
    }
  } else {
    // Setup regular columns.
    switch (col->internal_type()) {
      case QLValue::InternalType::kBoolValue:
        TranslateData = TranslateNumber<bool>;
        break;
      case QLValue::InternalType::kInt8Value:
        TranslateData = TranslateNumber<int8_t>;
        break;
      case QLValue::InternalType::kInt16Value:
        TranslateData = TranslateNumber<int16_t>;
        break;
      case QLValue::InternalType::kInt32Value:
        TranslateData = TranslateNumber<int32_t>;
        break;
      case QLValue::InternalType::kInt64Value:
        TranslateData = TranslateNumber<int64_t>;
        break;
      case QLValue::InternalType::kFloatValue:
        TranslateData = TranslateNumber<float>;
        break;
      case QLValue::InternalType::kDoubleValue:
        TranslateData = TranslateNumber<double>;
        break;
      case QLValue::InternalType::kStringValue:
        TranslateData = TranslateText;
        break;
      case QLValue::InternalType::kBinaryValue:
        TranslateData = TranslateBinary;
        break;

      case QLValue::InternalType::kDecimalValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kTimestampValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kDateValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kTimeValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kInetaddressValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kJsonbValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kUuidValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kTimeuuidValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kVarintValue:
        return STATUS_SUBSTITUTE(NotSupported, "Datatype $0 is not yet supported",
                                 col->internal_type());

      case QLValue::InternalType::kMapValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kSetValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kListValue: FALLTHROUGH_INTENDED;
      case QLValue::InternalType::kFrozenValue:
        return STATUS_SUBSTITUTE(NotSupported, "CQL type $0 is not supported in Postgres",
                                 col->internal_type());

      case QLValue::InternalType::VALUE_NOT_SET:
        return STATUS(Corruption, "Unexpected code path");
    }
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgOperator::PgOperator(const char *opname) : PgExpr(opname), opname_(opname) {
}

PgOperator::~PgOperator() {
}

void PgOperator::AppendArg(PgExpr *arg) {
  args_.push_back(arg);
}

//--------------------------------------------------------------------------------------------------

PgGenerateRowId::PgGenerateRowId() :
    PgExpr(Opcode::PG_EXPR_GENERATE_ROWID, InternalType::kBinaryValue) {
}

PgGenerateRowId::~PgGenerateRowId() {
}

Status PgGenerateRowId::Eval(PgDml *pg_stmt, PgsqlExpressionPB *expr_pb) {
  expr_pb->mutable_value()->set_binary_value(pg_stmt->pg_session()->GenerateNewRowid());
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
