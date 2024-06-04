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

#pragma once

#include <functional>
#include <initializer_list>
#include <vector>

#include "yb/common/common_fwd.h"
#include "yb/common/value.fwd.h"
#include "yb/common/value.pb.h"

#include "yb/util/result.h"

namespace yb::bfcommon {

// This class contains the metadata of a builtin function, which has to principal components.
// 1. Specification of a builtin function.
//    - A public name: This is the name of the builtin function.
//    - A parameter types: This is the signature of the builtin function.
//    - A return type.
// 2. Definition or body of a builtin function.
//    - A C++ function name: This represents the implementation of the builtin function in C++.
template <class Opcode, class Traits>
class BFDecl {
 public:
  BFDecl(const char *cpp_name,
         const char *ql_name,
         const char *bfopcode_name,
         DataType return_type,
         std::initializer_list<DataType> param_types,
         Opcode tsopcode = Opcode::kNoOp,
         bool implemented = true)
      : cpp_name_(cpp_name),
        ql_name_(ql_name),
        bfopcode_name_(bfopcode_name),
        return_type_(return_type),
        param_types_(param_types),
        tsopcode_(tsopcode),
        implemented_(implemented) {
  }

  BFDecl(const char *cpp_name,
         const char *ql_name,
         DataType return_type,
         std::initializer_list<DataType> param_types,
         Opcode tsopcode = Opcode::kNoOp,
         bool implemented = true)
      : BFDecl(cpp_name, ql_name, "", return_type, param_types, tsopcode, implemented) {
  }

  const char *cpp_name() const {
    return cpp_name_;
  }

  const char *ql_name() const {
    return ql_name_;
  }

  const char *bfopcode_name() const {
    return bfopcode_name_;
  }

  const DataType& return_type() const {
    return return_type_;
  }

  const std::vector<DataType>& param_types() const {
    return param_types_;
  }

  size_t param_count() const {
    return param_types_.size();
  }

  Opcode tsopcode() const {
    return tsopcode_;
  }

  bool is_server_operator() const {
    return tsopcode_ != Opcode::kNoOp;
  }

  bool implemented() const {
    return implemented_;
  }

  bool is_collection_bcall() const {
    return is_collection_op(tsopcode_);
  }

  static bool is_collection_op(Opcode opcode) {
    return Traits::is_collection_op(opcode);
  }

  bool is_aggregate_bcall() const {
    return is_aggregate_op(tsopcode_);
  }

  static bool is_aggregate_op(Opcode opcode) {
    return Traits::is_aggregate_op(opcode);
  }

 private:
  const char *cpp_name_;
  const char *ql_name_;
  const char *bfopcode_name_;
  DataType return_type_;
  std::vector<DataType> param_types_;
  Opcode tsopcode_;
  bool implemented_;
};

using BFValue = QLValuePB;
using BFRetValue = BFValue;
using BFParam = const BFValue&;
using BFCollectionEntry = BFValue;
using BFParams = std::vector<BFCollectionEntry>;
using BFFunctions = std::vector<std::function<Result<BFRetValue>(const BFParams&)>>;

class BFFactory {
 public:
  BFRetValue operator()() const;
};

} // namespace yb::bfcommon
