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

#include <tuple>
#include <type_traits>
#include <utility>
#include <unordered_map>

#include "yb/client/schema.h"

#include "yb/common/pg_system_attr.h"
#include "yb/common/ql_type.h"

#include "yb/util/decimal.h"
#include "yb/util/flags.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pg_dml.h"
#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

DEFINE_test_flag(bool, do_not_add_enum_sort_order, false,
                 "Do not add enum type sort order when buidling a constant "
                 "for an enum type value. Used to test database upgrade "
                 "where we have pre-existing enum type column values that "
                 "did not have sort order added.");

namespace yb {
namespace pggate {

using std::string;

namespace {
// Collation flags. kCollationMarker ensures the collation byte is non-zero.
constexpr uint8_t kDeterministicCollation = 0x01;
constexpr uint8_t kCollationMarker = 0x80;

Slice MakeCollationEncodedString(
  ThreadSafeArena* arena, const char* value, int64_t bytes, uint8_t collation_flags,
  const char* sortkey) {
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

Slice DecodeCollationEncodedString(Slice input) {
  DCHECK(*input.cend() == '\0') << "Data received from DocDB does not have expected format";
  // is_original_value = true means that we have done storage space optimization
  // to only store the original value for non-key columns.
  const bool is_original_value = input.empty() || input[0] != '\0';
  if (is_original_value) {
    return input;
  }

  // This is a collation encoded string, we need to fetch the original value.
  CHECK_GE(input.size(), 3);
  uint8_t collation_flags = input[1];
  CHECK_EQ(collation_flags, kCollationMarker | kDeterministicCollation);
  // Skip the collation and sortkey get the original character value.
  input.remove_prefix(2);
  const char *p = static_cast<const char*>(memchr(input.cdata(), '\0', input.size()));
  CHECK(p);
  ++p;
  CHECK_LE(p, input.cend());
  return Slice(p, input.cend());
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

Result<std::vector<std::reference_wrapper<PgColumn>>>
PgExpr::GetColumns(PgTable *pg_table) const {
  return STATUS_SUBSTITUTE(InvalidArgument, "Illegal call to GetColumns");
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

std::vector<std::reference_wrapper<const PgExpr>> PgExpr::Unpack() const {
  std::vector<std::reference_wrapper<const PgExpr>> vals;
  vals.push_back(*this);
  return vals;
}

namespace {

template <class GetSystemColumn>
class PgSysColumnRef : public PgColumnRef {
 public:
  template <class... Args>
  PgSysColumnRef(const GetSystemColumn& get_system_column, Args&&... args)
      : PgColumnRef(std::forward<Args>(args)...), get_system_column_(get_system_column) {
  }

  void SetNull(PgTuple* tuple) override {
    *get_system_column_(tuple->syscols()) = 0;
  }

  void SetValue(Slice* data, PgTuple* tuple) override {
    auto* out = get_system_column_(tuple->syscols());
    *out = PgDocData::ReadNumber<std::remove_reference_t<decltype(*out)>>(data);
  }

 private:
  GetSystemColumn get_system_column_;
};

template <class GetSystemColumn>
class PgYbSysColumnRef : public PgColumnRef {
 public:
  template <class... Args>
  PgYbSysColumnRef(const GetSystemColumn& get_system_column, Args&&... args)
      : PgColumnRef(std::forward<Args>(args)...), get_system_column_(get_system_column) {
  }

  void SetNull(PgTuple* tuple) override {
    *get_system_column_(tuple->syscols()) = nullptr;
  }

  void SetValue(Slice* data, PgTuple* tuple) override {
    auto data_size = PgDocData::ReadNumber<int64_t>(data);

  // TODO: return a status instead of crashing.
    CHECK_LE(data_size, kYBCMaxPostgresTextSizeBytes);
    CHECK_GE(data_size, 0);
    *get_system_column_(tuple->syscols()) = static_cast<uint8_t*>(
        YBCCStringToTextWithLen(data->cdata(), static_cast<int>(data_size)));
    data->remove_prefix(data_size);
  }

 private:
  GetSystemColumn get_system_column_;
};

template <class... Args>
class PgSysColumnRefFactory {
 public:
  explicit PgSysColumnRefFactory(ThreadSafeArena* arena, Args... args)
      : arena_(arena), args_(std::forward<Args>(args)...) {}

  PgColumnRef* operator()(PgSystemAttrNum attr_num) {
    // Setup system columns.
    switch (attr_num) {
      case PgSystemAttrNum::kSelfItemPointer:
        return PgColumn([](auto* syscols) { return &syscols->ctid; });
      case PgSystemAttrNum::kObjectId:
        return PgColumn([](auto* syscols) { return &syscols->oid; });
      case PgSystemAttrNum::kMinTransactionId:
        return PgColumn([](auto* syscols) { return &syscols->xmin; });
      case PgSystemAttrNum::kMinCommandId:
        return PgColumn([](auto* syscols) { return &syscols->cmin; });
      case PgSystemAttrNum::kMaxTransactionId:
        return PgColumn([](auto* syscols) { return &syscols->xmax; });
      case PgSystemAttrNum::kMaxCommandId:
        return PgColumn([](auto* syscols) { return &syscols->cmax; });
      case PgSystemAttrNum::kTableOid:
        return PgColumn([](auto* syscols) { return &syscols->tableoid; });
      case PgSystemAttrNum::kYBTupleId:
        return YbColumn([](auto* syscols) { return &syscols->ybctid; });
      case PgSystemAttrNum::kYBIdxBaseTupleId:
        return YbColumn([](auto* syscols) { return &syscols->ybbasectid; });
      case PgSystemAttrNum::kYBUniqueIdxKeySuffix: [[fallthrough]];
      case PgSystemAttrNum::kYBRowId:
        break;
    }
    FATAL_INVALID_ENUM_VALUE(PgSystemAttrNum, attr_num);
  }

 private:
  template <class GetSystemColumn>
  auto PgColumn(const GetSystemColumn& get_system_column) {
    return Apply<PgSysColumnRef>(get_system_column);
  }

  template <class GetSystemColumn>
  auto YbColumn(const GetSystemColumn& get_system_column) {
    return Apply<PgYbSysColumnRef>(get_system_column);
  }

  template <template<class> class ColumnType, class GetSystemColumn>
  auto Apply(const GetSystemColumn& get_system_column) {
    return std::apply(
        [arena = arena_, &get_system_column](Args... args) {
          return arena->template NewObject<ColumnType<GetSystemColumn>>(
              get_system_column, std::forward<Args>(args)...);
        },
        args_);
  }

  ThreadSafeArena* arena_;
  std::tuple<Args...> args_;
};

class PgIndexedColumnRef : public PgColumnRef {
 public:
  template <class... Args>
  explicit PgIndexedColumnRef(Args&&... args) : PgColumnRef(std::forward<Args>(args)...) {}

  void SetNull(PgTuple* tuple) override {
    tuple->WriteNull(index());
  }

  int index() const {
    return attr_num() - 1;
  }

  void DoSetDatum(PgTuple* tuple, uint64_t datum) {
    tuple->WriteDatum(index(), datum);
  }
};

template <class NumType, class Base, bool direct>
class PgNumericColumnRef : public Base {
 public:
  template <class... Args>
  explicit PgNumericColumnRef(Args&&... args) : Base(std::forward<Args>(args)...) {
    DCHECK_EQ(direct, Base::type_entity_->direct_datum);
  }

  void SetValue(Slice* data, PgTuple* tuple) override {
    if (!direct) {
      auto real_number = PgDocData::ReadNumber<NumType>(data);
      Base::DoSetDatum(tuple, Base::YbToDatum(&real_number, sizeof(NumType)));
      return;
    }
    auto unsigned_number = LoadRaw<NumType, NetworkByteOrder>(data->data());
    using IntType = std::conditional_t<
        std::is_signed_v<NumType>,
        std::make_signed_t<decltype(unsigned_number)>, decltype(unsigned_number)>;
    auto number = static_cast<IntType>(unsigned_number);
    auto datum = static_cast<uint64_t>(number);
#ifndef NDEBUG
    auto data_copy = *data;
    auto real_number = PgDocData::ReadNumber<NumType>(&data_copy);
    CHECK_EQ(datum, Base::YbToDatum(&real_number, sizeof(NumType)))
        << "yb_type: " << Base::type_entity_->yb_type
        << ", type: " << Base::type_entity_->type_oid;
#endif
    data->remove_prefix(sizeof(NumType));

    Base::DoSetDatum(tuple, datum);
  }
};

template <bool kCollate, class Base>
class PgTextColumnRef : public Base {
 public:
  template <class... Args>
  explicit PgTextColumnRef(Args&&... args) : Base(std::forward<Args>(args)...) {}

  void SetValue(Slice* data, PgTuple* tuple) override {
    // Get data from RPC buffer.
    auto data_size = PgDocData::ReadNumber<int64_t>(data);

    // Expects data from DocDB matches the following format.
    // - Right trim spaces for CHAR type. This should be done by DocDB when evaluate SELECTed or
    //   RETURNed expression. Note that currently, Postgres layer (and not DocDB) evaluate
    //   expressions, so DocDB doesn't trim for CHAR type.
    // - NULL terminated string. This should be done by DocDB when serializing.
    // - Text size == strlen(). When sending data over the network, RPC layer would use the actual
    //   size of data being serialized including the '\0' character. This is not necessarily be the
    //   length of a string.
    // Find strlen() of STRING by right-trimming all '\0' characters.
    Slice text = data->Prefix(data_size - 1);

    DCHECK(*text.cend() == '\0' && (text.empty() || !text.ends_with('\0')))
        << "Data received from DocDB does not have expected format";

    if (kCollate) {
      text = DecodeCollationEncodedString(text);
    }
    Base::DoSetDatum(tuple, Base::YbToDatum(text.cdata(), text.size()));
    data->remove_prefix(data_size);
  }
};

template <class Base>
class PgBinaryColumnRef : public Base {
 public:
  template <class... Args>
  explicit PgBinaryColumnRef(Args&&... args) : Base(std::forward<Args>(args)...) {}

  void SetValue(Slice* data, PgTuple* tuple) override {
    auto data_size = PgDocData::ReadNumber<int64_t>(data);
    Base::DoSetDatum(tuple, Base::YbToDatum(data->data(), data_size));
    data->remove_prefix(data_size);
  }
};

template <class Base>
class PgDecimalColumnRef : public Base {
 public:
  template <class... Args>
  explicit PgDecimalColumnRef(Args&&... args) : Base(std::forward<Args>(args)...) {}

  void SetValue(Slice* data, PgTuple* tuple) override {
    // Get the value size.
    auto data_size = PgDocData::ReadNumber<int64_t>(data);

    // Read the decimal value from Protobuf and decode it to internal format.
    util::Decimal yb_decimal;
    auto serialized_decimal = data->Prefix(data_size);
    if (!yb_decimal.DecodeFromComparable(serialized_decimal).ok()) {
      LOG(FATAL) << "Failed to deserialize DECIMAL from " << serialized_decimal.ToDebugHexString();
      return;
    }
    data->remove_prefix(data_size);

    // Translate to decimal format and write to datum.
    auto plaintext = yb_decimal.ToString();
    Base::DoSetDatum(tuple, Base::YbToDatum(plaintext.c_str(), plaintext.size()));
  }
};

template <class Base, class... Args>
struct PgColumnRefFactory {
  PgColumnRefFactory(Base*, ThreadSafeArena* arena, Args... args)
      : arena_(arena), args_(args...) {}

  Base* operator()(PgDataType type, bool direct, bool collate_is_valid_non_c) {
    switch (type) {
      case YB_YQL_DATA_TYPE_INT8:
        return ApplyNumeric<int8_t>(direct);

      case YB_YQL_DATA_TYPE_INT16:
        return ApplyNumeric<int16_t>(direct);

      case YB_YQL_DATA_TYPE_INT32:
        return ApplyNumeric<int32_t>(direct);

      case YB_YQL_DATA_TYPE_TIMESTAMP: FALLTHROUGH_INTENDED;
      case YB_YQL_DATA_TYPE_INT64:
        return ApplyNumeric<int64_t>(direct);

      case YB_YQL_DATA_TYPE_UINT32:
        return ApplyNumeric<uint32_t>(direct);

      case YB_YQL_DATA_TYPE_UINT64:
        return ApplyNumeric<uint64_t>(direct);

      case YB_YQL_DATA_TYPE_STRING:
        return collate_is_valid_non_c ? Apply<PgTextColumnRef<true, Base>>()
                                      : Apply<PgTextColumnRef<false, Base>>();

      case YB_YQL_DATA_TYPE_BOOL:
        return ApplyNumeric<bool>(direct);

      case YB_YQL_DATA_TYPE_FLOAT:
        return ApplyNumeric<float>(direct);

      case YB_YQL_DATA_TYPE_DOUBLE:
        return ApplyNumeric<double>(direct);

      case YB_YQL_DATA_TYPE_BINARY:
        return Apply<PgBinaryColumnRef<Base>>();

      case YB_YQL_DATA_TYPE_DECIMAL:
        return Apply<PgDecimalColumnRef<Base>>();

      case YB_YQL_DATA_TYPE_GIN_NULL:
        return ApplyNumeric<uint8_t>(direct);

      YB_PG_UNSUPPORTED_TYPES_IN_SWITCH:
      YB_PG_INVALID_TYPES_IN_SWITCH:
        break;
    }
    LOG(FATAL) << "Internal error: unsupported type " << type;
  }

 private:
  template <class IntType>
  Base* ApplyNumeric(bool direct) {
    return direct ? Apply<PgNumericColumnRef<IntType, Base, true>>()
                  : Apply<PgNumericColumnRef<IntType, Base, false>>();
  }

  template <class ColumnType>
  Base* Apply() {
    return std::apply([arena = arena_](auto... args) {
      return arena->template NewObject<ColumnType>(args...);
    }, args_);
  }

  ThreadSafeArena* arena_;
  std::tuple<Args...> args_;
};

} // namespace

InternalType PgExpr::internal_type() const {
  DCHECK(type_entity_) << "Type entity is not set up";
  // PersistentDataType and DataType has different values so have to use ToLW/ToPB for conversion.
  return client::YBColumnSchema::ToInternalDataType(ToLW(
      static_cast<PersistentDataType>(type_entity_->yb_type)));
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

std::string PgExpr::ToString() const {
  return Format("{ opcode: $0 }", to_underlying(opcode_));
}

//--------------------------------------------------------------------------------------------------

PgConstant::PgConstant(ThreadSafeArena* arena,
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
}

PgConstant::PgConstant(ThreadSafeArena* arena,
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

std::string PgConstant::ToString() const {
  return ql_value_.ShortDebugString();
}

//--------------------------------------------------------------------------------------------------

PgColumnRef* PgColumnRef::Create(
    ThreadSafeArena* arena,
    int attr_num,
    const PgTypeEntity *type_entity,
    bool collate_is_valid_non_c,
    const PgTypeAttrs *type_attrs) {
  if (attr_num < 0) {
    auto factory = PgSysColumnRefFactory(
        arena, attr_num, type_entity, collate_is_valid_non_c, type_attrs);
    return factory(static_cast<PgSystemAttrNum>(attr_num));
  }

  auto factory = PgColumnRefFactory(
      static_cast<PgIndexedColumnRef*>(nullptr), arena,
      attr_num, type_entity, collate_is_valid_non_c, type_attrs);
  return factory(type_entity->yb_type, type_entity->direct_datum, collate_is_valid_non_c);
}

bool PgColumnRef::is_ybbasetid() const {
  return attr_num_ == static_cast<int>(PgSystemAttrNum::kYBIdxBaseTupleId);
}

bool PgColumnRef::is_system() const {
  return attr_num_ < 0;
}

Status PgColumnRef::PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) {
  RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(attr_num_, expr_pb));
  return Status::OK();
}

Result<std::vector<std::reference_wrapper<PgColumn>>>
PgColumnRef::GetColumns(PgTable *pg_table) const {
  std::vector<std::reference_wrapper<PgColumn>> cols;
  cols.push_back(VERIFY_RESULT(pg_table->ColumnForAttr(attr_num())));
  return cols;
}

//--------------------------------------------------------------------------------------------------

PgOperator::PgOperator(ThreadSafeArena* arena,
                       Opcode opcode,
                       const YBCPgTypeEntity *type_entity,
                       bool collate_is_valid_non_c)
    : PgExpr(opcode, type_entity, collate_is_valid_non_c), args_(arena) {
}

PgOperator* PgOperator::Create(
    ThreadSafeArena* arena, const char* name, const YBCPgTypeEntity* type_entity,
    bool collate_is_valid_non_c) {
  auto opcode = NameToOpcode(name);
  if (!is_aggregate(opcode)) {
    return arena->NewObject<PgOperator>(arena, opcode, type_entity, collate_is_valid_non_c);
  }

  auto factory = PgColumnRefFactory(
      static_cast<PgAggregateOperator*>(nullptr), arena,
      arena, opcode, type_entity, collate_is_valid_non_c);
  return factory(type_entity->yb_type, type_entity->direct_datum, collate_is_valid_non_c);
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

void PgAggregateOperator::DoSetDatum(PgTuple* tuple, uint64_t datum) {
  tuple->WriteDatum(index_, datum);
}

//--------------------------------------------------------------------------------------------------

PgTupleExpr::PgTupleExpr(ThreadSafeArena* arena,
                         const YBCPgTypeEntity* type_entity,
                         const PgTypeAttrs *type_attrs,
                         int num_elems,
                         PgExpr *const *elems)
  : PgExpr(Opcode::PG_EXPR_TUPLE_EXPR, type_entity, false, type_attrs),
    elems_(arena),
    ql_tuple_expr_value_(arena) {
  for (int i = 0; i < num_elems; i++) {
    elems_.push_back_ref(elems[i]);
  }
}

Status PgTupleExpr::PrepareForRead(PgDml *pg_stmt, LWPgsqlExpressionPB *expr_pb) {
  auto *tup_elems = expr_pb->mutable_tuple();
  for (auto &elem : elems_) {
    auto new_elem = tup_elems->add_elems();

    if (elem.is_constant()) {
      RETURN_NOT_OK(elem.EvalTo(new_elem->mutable_value()));
    } else {
      DCHECK(elem.is_colref());
      PgColumnRef *cref = reinterpret_cast<PgColumnRef*>(&elem);
      RETURN_NOT_OK(pg_stmt->PrepareColumnForRead(cref->attr_num(), new_elem));
    }
  }
  return Status::OK();
}

Result<std::vector<std::reference_wrapper<PgColumn>>>
PgTupleExpr::GetColumns(PgTable *pg_table) const {
  std::vector<std::reference_wrapper<PgColumn>> cols;
  for (const auto &elem : GetElems()) {
    int attr_num = reinterpret_cast<const PgColumnRef*>(&elem)->attr_num();
    cols.push_back(VERIFY_RESULT(pg_table->ColumnForAttr(attr_num)));
  }
  return cols;
}

std::vector<std::reference_wrapper<const PgExpr>> PgTupleExpr::Unpack() const {
  std::vector<std::reference_wrapper<const PgExpr>> vals;
  for (auto &elem : GetElems()) {
    vals.push_back(elem);
  }
  return vals;
}

Result<LWQLValuePB*> PgTupleExpr::Eval() {
  auto *tup_elems = ql_tuple_expr_value_.mutable_tuple_value();
  for (auto &elem : elems_) {
    auto new_elem = tup_elems->add_elems();
    if (elem.is_constant()) {
      RETURN_NOT_OK(elem.EvalTo(new_elem));
    } else {
      return nullptr;
    }
  }
  return &ql_tuple_expr_value_;
}

}  // namespace pggate
}  // namespace yb
