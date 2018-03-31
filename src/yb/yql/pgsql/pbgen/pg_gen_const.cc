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

#include <yb/util/bytes_formatter.h>
#include "yb/yql/pgsql/pbgen/pg_coder.h"
#include "yb/util/logging.h"
#include "yb/util/bfpg/bfunc.h"

#include "yb/util/decimal.h"

namespace yb {
namespace pgsql {

CHECKED_STATUS PgCoder::TConstToPB(const PgTExpr::SharedPtr& expr,
                                   QLValuePB *const_pb,
                                   bool negate) {
  DCHECK(expr->expr_op() == ExprOperator::kConst);

  if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
      SetNull(const_pb);
  }

  const PgTExpr *const_pt = expr.get();
  switch (const_pt->ql_type_id()) {
    case DataType::NULL_VALUE_TYPE: {
      SetNull(const_pb);
      break;
    }
    case DataType::VARINT: {
      return TExprToPB(static_cast<const PgTConstVarInt*>(const_pt), const_pb, negate);
    }
    case DataType::DECIMAL: {
      return TExprToPB(static_cast<const PgTConstDecimal*>(const_pt), const_pb, negate);
    }
    case DataType::INT64: { // Might be an obsolete case.
      return TExprToPB(static_cast<const PgTConstInt*>(const_pt), const_pb, negate);
    }
    case DataType::DOUBLE: { // Might be an obsolete case.
      return TExprToPB(static_cast<const PgTConstDouble*>(const_pt), const_pb, negate);
    }
    case DataType::STRING: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return TExprToPB(static_cast<const PgTConstText*>(const_pt), const_pb);
    }
    case DataType::BOOL: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return TExprToPB(static_cast<const PgTConstBool*>(const_pt), const_pb);
    }
    case DataType::BINARY: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return TExprToPB(static_cast<const PgTConstBinary*>(const_pt), const_pb);
    }

    default:
      LOG(FATAL) << "Unknown datatype for QL constant value";
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstVarInt *const_pt, QLValuePB *const_pb,
                                  bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid integer.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int8_value(value);
      break;
    }
    case InternalType::kInt16Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid integer.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int16_value(value);
      break;
    }
    case InternalType::kInt32Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid integer.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int32_value(value);
      break;
    }
    case InternalType::kInt64Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid integer.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int64_value(value);
      break;
    }
    case InternalType::kFloatValue: {
      long double value;
      if (!const_pt->ToDouble(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid float.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_float_value(value);
      break;
    }
    case InternalType::kDoubleValue: {
      long double value;
      if (!const_pt->ToDouble(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid double.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_double_value(value);
      break;
    }
    case InternalType::kDecimalValue: {
      return const_pt->ToDecimal(const_pb->mutable_decimal_value(), negate);
    }
    case InternalType::kTimestampValue: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return compile_context_->Error(const_pt->loc(), "Invalid integer.",
                                       ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_timestamp_value(DateTime::TimestampFromInt(value).ToInt64());
      break;
    }
    case InternalType::kVarintValue: {
      return const_pt->ToVarInt(const_pb->mutable_varint_value(), negate);
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstDecimal *const_pt, QLValuePB *const_pb,
                                  bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kDecimalValue: {
      return const_pt->ToDecimal(const_pb->mutable_decimal_value(), negate);
    }
    case InternalType::kFloatValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_float_value(value);
      break;
    }
    case InternalType::kDoubleValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_double_value(value);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

// The following numeric functions might be needed if we fold constant at compile time.
// Leave them here for now.
CHECKED_STATUS PgCoder::TExprToPB(const PgTConstInt *const_pt, QLValuePB *const_pb,
                                  bool negate) {
  int64_t value = const_pt->value();
  if (negate) {
    value = -value;
  }

  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value:
      const_pb->set_int8_value(value);
      break;
    case InternalType::kInt16Value:
      const_pb->set_int16_value(value);
      break;
    case InternalType::kInt32Value:
      const_pb->set_int32_value(value);
      break;
    case InternalType::kInt64Value:
      const_pb->set_int64_value(value);
      break;
    case InternalType::kFloatValue:
      const_pb->set_float_value(value);
      break;
    case InternalType::kDoubleValue:
      const_pb->set_double_value(value);
      break;
    case InternalType::kTimestampValue:
      const_pb->set_timestamp_value(DateTime::TimestampFromInt(value).ToInt64());
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion from "
                 << static_cast<int>(const_pt->expected_internal_type());
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstDouble *const_pt, QLValuePB *const_pb,
                                  bool negate) {
  long double value = const_pt->value();
  if (negate) {
    value = -value;
  }

  switch (const_pt->expected_internal_type()) {
    case InternalType::kFloatValue:
      const_pb->set_float_value(value);
      break;
    case InternalType::kDoubleValue:
      const_pb->set_double_value(value);
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstText *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kStringValue:
      return const_pt->ToString(const_pb->mutable_string_value());
    case InternalType::kTimestampValue: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToTimestamp(&value));
      const_pb->set_timestamp_value(value);
      break;
    }
    case InternalType::kInetaddressValue: {
      InetAddress value;
      RETURN_NOT_OK(const_pt->ToInetaddress(&value));

      QLValue ql_const;
      ql_const.set_inetaddress_value(value);
      const_pb->CopyFrom(ql_const.value());
      break;
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstBool *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBoolValue:
      const_pb->set_bool_value(const_pt->value());
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS PgCoder::TExprToPB(const PgTConstBinary *const_pt, QLValuePB *const_pb) {
  const auto& value = const_pt->value();
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBinaryValue: {
      int input_size = static_cast<int>(value->size());
      if (input_size % 2 != 0) {
        return STATUS(RuntimeError, "Invalid binary input, expected even number of hex digits");
      }

      string bytes;
      a2b_hex(value->c_str(), &bytes, input_size / 2);
      const_pb->set_binary_value(bytes);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
