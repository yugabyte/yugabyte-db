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
// This module defines the API to execute YQL builtin functions. It processes OP_BFCALL in an
// expression tree of YQLValues.
// - See file "yql_protocol.proto" for definitions of operators (OP_BFCALL).
// - During compilation or first execution, a treenode for OP_BFCALL should be generated.
// - Either DocDB or ProxyServer should execute the node.
//
// NOTES:
// Because builtin opcodes are auto-generated during compilation, when a module includes this file,
// it must add dependency to yb_bfyql library in the CMakeLists.txt to enforce that the compiler
// will generate the code for yb_bfyql lib first before building the dependent library.
//   Add the following line in file CMakeLists.txt.
//   add_dependencies(<yql_bfunc.h included library> yb_bfyql)
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_YQL_BFUNC_H_
#define YB_COMMON_YQL_BFUNC_H_

#include "yb/common/yql_value.h"
#include "yb/util/bfyql/bfyql.h"

namespace yb {

// YQLBfunc defines a set of static functions to execute OP_BFUNC in YQLValue expression tree.
// NOTE:
// - OP_BFUNC is not yet defined or generated.
// - Do not add non-static members to this class as YQLBfunc is not meant for creating different
//   objects with different behaviors. For compability reason, all builtin calls must be processed
//   the same way across all processes and all releases in YugaByte.
class YQLBfunc {
 public:
  static Status Exec(bfyql::BFOpcode opcode,
                     const std::vector<std::shared_ptr<YQLValue>>& params,
                     const std::shared_ptr<YQLValue>& result);

  static Status Exec(bfyql::BFOpcode opcode,
                     const std::vector<YQLValue*>& params,
                     YQLValue *result);

  static Status Exec(bfyql::BFOpcode opcode,
                     std::vector<YQLValue> *params,
                     YQLValue *result);

  static Status Exec(bfyql::BFOpcode opcode,
                     const std::vector<std::shared_ptr<YQLValueWithPB>>& params,
                     const std::shared_ptr<YQLValueWithPB>& result);

  static Status Exec(bfyql::BFOpcode opcode,
                     const std::vector<YQLValueWithPB*>& params,
                     YQLValueWithPB *result);

  static Status Exec(bfyql::BFOpcode opcode,
                     std::vector<YQLValueWithPB> *params,
                     YQLValueWithPB *result);
};

} // namespace yb

#endif // YB_COMMON_YQL_BFUNC_H_
