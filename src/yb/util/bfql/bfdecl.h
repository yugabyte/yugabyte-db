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
// A builtin function is specified in QL but implemented in C++, and this module represents the
// metadata of a builtin function.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFQL_BFDECL_H_
#define YB_UTIL_BFQL_BFDECL_H_

#include "yb/client/client.h"
#include "yb/util/logging.h"
#include "yb/util/bfql/tserver_opcodes.h"

namespace yb {
namespace bfql {

// This class contains the metadata of a builtin function, which has to principal components.
// 1. Specification of a builtin function.
//    - A QL name: This is the name of the builtin function.
//    - A QL parameter types: This is the signature of the builtin function.
//    - A QL return type.
// 2. Definition or body of a builtin function.
//    - A C++ function name: This represents the implementation of the builtin function in C++.
class BFDecl {
 public:
  BFDecl(const char *cpp_name,
         const char *ql_name,
         DataType return_type,
         std::initializer_list<DataType> param_types,
         TSOpcode tsopcode = TSOpcode::kNoOp,
         bool implemented = true)
      : cpp_name_(cpp_name),
        ql_name_(ql_name),
        return_type_(return_type),
        param_types_(param_types),
        tsopcode_(tsopcode),
        implemented_(implemented) {
  }

  const char *cpp_name() const {
    return cpp_name_;
  }

  const char *ql_name() const {
    return ql_name_;
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

  TSOpcode tsopcode() const {
    return tsopcode_;
  }

  bool is_server_operator() const {
    return tsopcode_ != TSOpcode::kNoOp;
  }

  bool implemented() const {
    return implemented_;
  }

 private:
  const char *cpp_name_;
  const char *ql_name_;
  DataType return_type_;
  std::vector<DataType> param_types_;
  TSOpcode tsopcode_;
  bool implemented_;
};

} // namespace bfql
} // namespace yb

#endif  // YB_UTIL_BFQL_BFDECL_H_
