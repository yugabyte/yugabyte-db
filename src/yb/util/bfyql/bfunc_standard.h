//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines standard C++ functions that are used to support YQL builtin functions.
// Each of these functions have one or more entries in builtin library directory. Note that C++
// functions don't have to be defined here as long as they are linked to this lib.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfyql/bfyql.h" for more general info.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFYQL_BFUNC_STANDARD_H_
#define YB_UTIL_BFYQL_BFUNC_STANDARD_H_

#include <iostream>
#include <string>

#include "yb/util/status.h"
#include "yb/util/logging.h"

namespace yb {
namespace bfyql {

//--------------------------------------------------------------------------------------------------
// Dummy function for minimum opcode.
template<typename PTypePtr, typename RTypePtr>
Status NoOp() {
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
template<typename PTypePtr, typename RTypePtr>
Status Token(const vector<PTypePtr>& params, RTypePtr result) {
  static const string kSeparator = "|";

  string value = kSeparator;
  for (auto param : params) {
    switch (param->type()) {
      case InternalType::kInt8Value:
        value += std::to_string(param->int8_value());
        break;
      case InternalType::kInt16Value:
        value += std::to_string(param->int16_value());
        break;
      case InternalType::kInt32Value:
        value += std::to_string(param->int32_value());
        break;
      case InternalType::kInt64Value:
        value += std::to_string(param->int64_value());
        break;
      case InternalType::kStringValue:
        value += param->string_value();
        break;
      case InternalType::kBoolValue:
        value += std::string(param->bool_value() ? "t" : "f");
        break;
      case InternalType::kFloatValue:
        value += std::to_string(param->float_value());
        break;
      case InternalType::kDoubleValue:
        value += std::to_string(param->double_value());
        break;

      case InternalType::kTimestampValue: FALLTHROUGH_INTENDED;
      default:
        LOG(FATAL) << "Runtime error: This datatype("
                   << int(param->type())
                   << ") is not supported";
    }
    value += kSeparator;
  }

  // TODO(neil) Return a hash value of result instead of the full string.
  result->set_string_value(value);

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

template<typename PTypePtr, typename RTypePtr>
Status AddI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  result->set_int64_value(x->int64_value() + y->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  result->set_double_value(x->double_value() + y->double_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringString(PTypePtr x, PTypePtr y, RTypePtr result) {
  result->set_string_value(x->string_value() + y->string_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  result->set_string_value(x->string_value() + std::to_string(y->double_value()));
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleString(PTypePtr x, PTypePtr y, RTypePtr result) {
  result->set_string_value(std::to_string(x->double_value()) + y->string_value());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

} // namespace bfyql
} // namespace yb

#endif  // YB_UTIL_BFYQL_BFUNC_STANDARD_H_
