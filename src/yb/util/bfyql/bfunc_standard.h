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

  // TODO sample hash-function -- should eventually compute the correct hash value here
  int64_t hash = 0;
  const char *cstr = value.c_str();
  while (*cstr) {
    hash = hash << 1 ^ *cstr++;
  }

  result->set_int64_value(hash);

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Special ops for counter: "+counter" and "-counter".

template<typename PTypePtr, typename RTypePtr>
Status IncCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull()) {
    result->set_int64_value(y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status DecCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull()) {
    result->set_int64_value(-y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// "+" and "-".

template<typename PTypePtr, typename RTypePtr>
Status AddI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() + y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + std::to_string(y->double_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(std::to_string(x->double_value()) + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddMapMap(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status AddMapSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status AddSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status AddListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() - y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubMapSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // TODO All calls allowed for this builtin should be optimized away to avoid evaluating here.
  // But this is not yet implemented in DocDB so evaluating inefficiently and in-memory for now.
  // For clarity, this implementation should be removed (see e.g. SubSetSet above) as soon as
  // RemoveFromList is implemented in DocDB.
  result->set_list_value();
  if (x->IsNull() || y->IsNull()) {
    return Status::OK();
  }

  YQLSeqValuePB xl = x->list_value();
  YQLSeqValuePB yl = y->list_value();
  for (const YQLValuePB& x_elem : xl.elems()) {
    bool should_remove = false;
    for (const YQLValuePB& y_elem : yl.elems()) {
      if (x_elem == y_elem) {
        should_remove = true;
        break;
      }
    }
    if (!should_remove) {
      auto elem = result->add_list_elem();
      elem->CopyFrom(x_elem);
    }
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

} // namespace bfyql
} // namespace yb

#endif  // YB_UTIL_BFYQL_BFUNC_STANDARD_H_
