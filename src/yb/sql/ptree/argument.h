//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Structure definitions for arguments of a statement or function call.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_ARGUMENT_H_
#define YB_SQL_PTREE_ARGUMENT_H_

#include "yb/client/client.h"
#include "yb/sql/ptree/pt_expr.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

// This class can be used for argument of any statements or function calls.
class Argument {
 public:
  Argument()
      : index_(-1),
        is_hash_(false),
        is_primary_(false),
        expected_type_(client::YBColumnSchema::DataType::MAX_TYPE_INDEX),
        expr_(nullptr) {
  }

  void Init(int index,
            bool is_hash,
            bool is_primary,
            client::YBColumnSchema::DataType expected_type,
            const PTExpr::SharedPtr& expr) {
    index_ = index;
    is_hash_ = is_hash;
    is_primary_ = is_primary;
    expected_type_ = expected_type;
    expr_ = expr;
  }

  bool IsInitialized() const {
    return (index_ >= 0);
  }

  int index() const {
    return index_;
  }

  bool is_hash() const {
    return is_hash_;
  }

  bool is_primary() const {
    return is_primary_;
  }

  client::YBColumnSchema::DataType expected_type() const {
    return expected_type_;
  }

  const PTExpr::SharedPtr& expr() const {
    return expr_;
  }

 private:
  int index_;
  bool is_hash_;
  bool is_primary_;
  client::YBColumnSchema::DataType expected_type_;
  PTExpr::SharedPtr expr_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_ARGUMENT_H_
