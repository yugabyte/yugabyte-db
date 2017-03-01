//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the evaluating expressions.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EVAL_EXPR_H_
#define YB_SQL_EXEC_EVAL_EXPR_H_

#include "yb/sql/ptree/pt_expr.h"
#include "yb/common/yql_value.h"

namespace yb {
namespace sql {

struct EvalValue {
  virtual InternalType datatype() = 0;
  bool is_null() {
    return is_null_;
  }
  void set_null() {
    is_null_ = true;
  }
  void set_not_null() {
    is_null_ = false;
  }
  bool is_null_ = false;
};

// To avoid overflow, all integer expressions are resulted in "int64_t".
struct EvalIntValue : public EvalValue {
  InternalType datatype() {
    return InternalType::kInt64Value;
  }

  int64_t value_;
};

// To avoid overflow, all floating expressions are resulted in "long double".
struct EvalDoubleValue : public EvalValue {
  InternalType datatype() {
    return InternalType::kDoubleValue;
  }

  long double value_;
};

// All text expressions are resulted in std::string.
struct EvalStringValue : public EvalValue {
  InternalType datatype() {
    return InternalType::kStringValue;
  }

  MCString::SharedPtr value_;
};

// All boolean expressions are resulted in std::bool.
struct EvalBoolValue : public EvalValue {
  InternalType datatype() {
    return InternalType::kBoolValue;
  }

  bool value_;
};

struct EvalTimestampValue : public EvalValue {
  InternalType datatype() {
    return InternalType::kTimestampValue;
  }

  int64_t value_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EVAL_EXPR_H_
