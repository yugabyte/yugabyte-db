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
//
// This module defines standard C++ functions that are used to support QL builtin functions.
// Each of these functions have one or more entries in builtin library directory. Note that C++
// functions don't have to be defined here as long as they are linked to this lib.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfql/bfql.h" for more general info.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFQL_BFUNC_STANDARD_H_
#define YB_UTIL_BFQL_BFUNC_STANDARD_H_

#include <iostream>
#include <string>

#include "yb/util/status.h"
#include "yb/util/logging.h"
#include "yb/util/yb_partition.h"

namespace yb {
namespace bfql {

//--------------------------------------------------------------------------------------------------
// Dummy function for minimum opcode.
template<typename PTypePtr, typename RTypePtr>
Status NoOp() {
  return Status::OK();
}

// ServerOperator that takes no argument and has no return value.
template<typename PTypePtr, typename RTypePtr>
Status ServerOperator() {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// ServerOperator that takes 1 argument and has a return value.
template<typename PTypePtr, typename RTypePtr>
Status ServerOperator(PTypePtr arg1, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// This is not used but implemented as an example for future coding.
// ServerOperator that takes 2 arguments and has a return value.
template<typename PTypePtr, typename RTypePtr>
Status ServerOperator(PTypePtr arg1, PTypePtr arg2, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

//--------------------------------------------------------------------------------------------------

template<typename PTypePtr, typename RTypePtr>
Status Token(const vector<PTypePtr>& params, RTypePtr result) {
  string encoded_key = "";
  for (int i = 0; i < params.size(); i++) {
    const PTypePtr& param = params[i];
    RETURN_NOT_OK(param->AppendToKeyBytes(&encoded_key));
  }

  uint16_t hash = YBPartition::HashColumnCompoundValue(encoded_key);
  // Convert to CQL hash since this may be used in expressions above.
  result->set_int64_value(YBPartition::YBToCqlHashCode(hash));

  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ttl(PTypePtr col, PTypePtr result) {
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status writetime(PTypePtr col, PTypePtr result) {
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

  QLSeqValuePB xl = x->list_value();
  QLSeqValuePB yl = y->list_value();
  for (const QLValuePB& x_elem : xl.elems()) {
    bool should_remove = false;
    for (const QLValuePB& y_elem : yl.elems()) {
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
// Now().
template<typename PTypePtr, typename RTypePtr>
Status NowTimeUuid(RTypePtr result) {
  return STATUS(QLError, "Not yet implemented");
}

//--------------------------------------------------------------------------------------------------

} // namespace bfql
} // namespace yb

#endif  // YB_UTIL_BFQL_BFUNC_STANDARD_H_
