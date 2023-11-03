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
// This module defines a few templates to be used in file "bfpg.h". It defines the actual
// implementation for compilation and execution of a builtin call.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <fcntl.h>

#include <functional>
#include <vector>

#include "yb/bfpg/gen_opcodes.h"
#include "yb/bfpg/gen_operator.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/logging.h"

namespace yb {
namespace bfpg {

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

} // namespace bfpg
} // namespace yb
