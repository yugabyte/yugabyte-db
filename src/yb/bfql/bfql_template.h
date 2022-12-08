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
// This module defines a few templates to be used in file "bfql.h". It defines the actual
// implementation for compilation and execution of a builtin call.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <fcntl.h>

#include <functional>
#include <vector>

#include "yb/bfql/gen_opcodes.h"
#include "yb/bfql/gen_operator.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/util/fault_injection.h"
#include "yb/util/logging.h"

namespace yb {
namespace bfql {

//--------------------------------------------------------------------------------------------------
// Find the builtin opcode, declaration, and return type for a builtin call.
// Inputs: Builtin function name and parameter types.
// Outputs: opcode and bfdecl.
// In/Out parameter: return_type
//   If return_type is given, check if it is compatible with the declaration.
//   If not, return_type is an output parameter whose value is the return type of the builtin.
Status FindOpcodeByType(const std::string& ql_name,
                        const std::vector<DataType>& actual_types,
                        BFOpcode *opcode,
                        const BFDecl **bfdecl,
                        DataType *return_type);

// The effect is the same as function "FindOpcodeByType()", but it takes arguments instead types.
// NOTE:
//   RTypePtr can be either a raw (*) or shared (const shared_ptr&) pointer.
//   PTypePtrCollection can be any standard collection of PType raw or shared pointer.
//     std::vector<PTypePtr>, std::list<PTypePtr>,  std::set<PTypePtr>, ...
template<typename PTypePtrCollection, typename RTypePtr>
Status FindOpcode(const std::string& ql_name,
                  const PTypePtrCollection& params,
                  BFOpcode *opcode,
                  const BFDecl **bfdecl,
                  RTypePtr result) {

  // Read argument types.
  std::vector<DataType> actual_types(params.size(), DataType::UNKNOWN_DATA);
  int pindex = 0;
  for (const auto& param : params) {
    actual_types[pindex] = param->ql_type_id();
    pindex++;
  }

  // Get the opcode and declaration.
  if (result == nullptr) {
    return FindOpcodeByType(ql_name, actual_types, opcode, bfdecl, nullptr);
  }

  // Get the opcode, declaration, and return type.
  DataType return_type = result->ql_type_id();
  RETURN_NOT_OK(FindOpcodeByType(ql_name, actual_types, opcode, bfdecl, &return_type));
  result->set_ql_type_id(return_type);

  return Status::OK();
}
} // namespace bfql
} // namespace yb
