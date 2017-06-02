//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
