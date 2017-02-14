//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// A builtin function is specified in YQL but implemented in C++, and this module represents the
// metadata of a builtin function.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFYQL_BFDECL_H_
#define YB_UTIL_BFYQL_BFDECL_H_

#include "yb/client/client.h"
#include "yb/util/logging.h"

namespace yb {
namespace bfyql {

// This class contains the metadata of a builtin function, which has to principal components.
// 1. Specification of a builtin function.
//    - A YQL name: This is the name of the builtin function.
//    - A YQL parameter types: This is the signature of the builtin function.
//    - A YQL return type.
// 2. Definition or body of a builtin function.
//    - A C++ function name: This represents the implementation of the builtin function in C++.
class BFDecl {
 public:
  BFDecl(const char *cpp_name,
         const char *yql_name,
         DataType return_type,
         std::initializer_list<DataType> param_types)
      : cpp_name_(cpp_name),
        yql_name_(yql_name),
        return_type_(return_type),
        param_types_(param_types) {
  }

  const char *cpp_name() const {
    return cpp_name_;
  }

  const char *yql_name() const {
    return yql_name_;
  }

  const DataType& return_type() const {
    return return_type_;
  }

  const std::vector<DataType>& param_types() const {
    return param_types_;
  }

  int param_count() const {
    return param_types_.size();
  }

 private:
  const char *cpp_name_;
  const char *yql_name_;
  DataType return_type_;
  std::vector<DataType> param_types_;
};

} // namespace bfyql
} // namespace yb

#endif  // YB_UTIL_BFYQL_BFDECL_H_
