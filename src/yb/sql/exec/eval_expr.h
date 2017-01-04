//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the evaluating expressions.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EVAL_EXPR_H_
#define YB_SQL_EXEC_EVAL_EXPR_H_

#include "yb/sql/ptree/pt_expr.h"
#include "yb/common/ysql_value.h"

namespace yb {
namespace sql {

struct EvalValue {
  virtual yb::DataType datatype() = 0;
};

// To avoid overflow, all integer expressions are resulted in "int64_t".
struct EvalIntValue : public EvalValue {
  yb::DataType datatype() {
    return yb::DataType::INT64;
  }

  int64_t value_;
};

// To avoid overflow, all floating expressions are resulted in "long double".
struct EvalDoubleValue : public EvalValue {
  yb::DataType datatype() {
    return yb::DataType::DOUBLE;
  }

  long double value_;
};

// All text expressions are resulted in std::string.
struct EvalStringValue : public EvalValue {
  yb::DataType datatype() {
    return yb::DataType::STRING;
  }

  MCString::SharedPtr value_;
};

// All boolean expressions are resulted in std::bool.
struct EvalBoolValue : public EvalValue {
  yb::DataType datatype() {
    return yb::DataType::BOOL;
  }

  bool value_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EVAL_EXPR_H_
