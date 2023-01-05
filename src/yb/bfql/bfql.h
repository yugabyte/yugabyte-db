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
// This module defines the entries to the builtin library functions.
//   FindQLOpcode() - Compiling builtin call into opcode.
//   ExecQLOpcode() - Execute builtin call using opcode.
//
// NOTES ON BUILTIN DEFINITIONS
// ----------------------------
// Here's how new builtin functions are implemented or added to this library.
// * Define C++ function (e.g. Token) in this library or any C++ library (yb_bfql).
//   If the function is defined in a different library, link it to this library.
// * Define associated QL function ("token") by adding it to QL parser.
// * In file "directory.cc", add an entry at the end of the kBFDirectory table.
//     { C++ func_name , QL func_name , return_type, { formal_parameter_types } }
//     Example:
//     { "Token", "token", STRING, {TYPEARGS} }
// * The rest of the code would be auto-generated.
//
// NOTES ON PROCESSING BUILTIN CALLS
// ---------------------------------
// Here's how builtin calls can be processed using this library.
// - Getting the opcode from a process (such as client or proxy server).
//     FindQLOpcode(function_name,              // Input:         Such as "+"
//                   actual_parameter_types,     // Input:         Types of arguments.
//                   opcode,                     // Output:        Found associated opcode.
//                   formal_parameter_types,     // Output:        Types of formal parameters.
//                   return_type)                // Input/Output:  Return type.
// - Send the opcode & parameter values to any processes (such as tablet server).
// - The receiving process can then execute it.
//     ExecQLOpcode(opcode,                     // Input:         Opcode from compilation.
//                   param_values,               // Input:         Arguments.
//                   return_value)               // Output:        Computed result.
//
// NOTES ON COMPILING BUILTIN CALLS
// --------------------------------
// FindQLOpcode() should be called to type check a builtin calls.
// * FindQLOpcode() does type checking for the parameters (function signature).
// * FindQLOpcode() outputs a BFOpcode to be used at execution time.
// * FindQLOpcode() outputs the formal parameter types.
//   - This is the signature of the function.
//   - The arguments to a builtin call must be converted to these exact formal types.
// * FindQLOpcode() inputs / outputs the return type.
//   - If return type is not given (UNKNOWN_DATA), it returns the expected return type to caller.
//   - If return type is given, it checks if the type is compatible with the function's return type.
//
// NOTES ON EXECUTING BUILTIN CALLS
// --------------------------------
// Call ExecQLFunc(opcode, args, result)
// * Input arguments must be of exact datatypes as the formal parameter types. Operator "cast"
//   should have been used to convert these arguments when needed.
//
// * The return result must also be of expected type. The return value of expected type would be
//   written to this result.
//
// EXAMPPLE
// --------
// The test "/yb/util/bfql/bfql-test.cc" would be a good example on builtin-call usage.
// The file "/yb/yql/cql/ql/ptree/pt_bfunc.cc" can be used as example at the moment.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <vector>
#include <list>

#include "yb/bfql/bfql_template.h"
#include "yb/bfql/tserver_opcodes.h"
#include "yb/bfql/bfunc_names.h"

#include "yb/util/logging.h"
#include "yb/util/memory/arena.h"

namespace yb {
namespace bfql {

//--------------------------------------------------------------------------------------------------
// class BFCompileApi<PType, RType> has one main entry function - FindQLOpcode().
//
// FindQLOpcode() finds the builtin opcode, signature, and return type using the function name.
// - This function is a template function which accept with any parameter and result classes that
//   implement the following functions.
//     DataType ql_type_id();
//     void set_ql_type_id(DataType);
//
// - Example
//   . QL expression treenode can be used for both PType and RType because it has the two required
//     functions for set & get ql_type_ids.
//   . Pseudo code.
//     using BFCompileQL = BFCompileApi<std::shared_ptr<QLValue>, QLValue>.
//
// - Example for a class that can be used as PType and Rtype for this template.
//     class YourArg {
//       DataType ql_type_id() { return ql_type_id_; }
//       void set_ql_type_id(DataType t) { ql_type_id_ = t; }
//     };
//
//   Builtin library provides interface for both raw and shared pointers.
//     vector<YourArgPointer> params = { ... };
//     BFCompileApi<YourArg, YourArg>::FindQLOpcode(name, params, opcode, decl, result_ptr);

template<typename PType, typename RType>
class BFCompileApi {
 public:
  //------------------------------------------------------------------------------------------------
  // Because we using Arena allocator, our compiler don't use standard collection types such as
  // std::vector. The following templates allow the compiler to resolve builtin calls with with
  // various collection types.

  // Interface for any collections of shared_ptrs.
  template<template<typename, typename> class CType, typename AType>
  static Status FindQLOpcodeImpl(const std::string& ql_name,
                                  const CType<std::shared_ptr<PType>, AType>& param_types,
                                  BFOpcode *opcode,
                                  const BFDecl **bfdecl,
                                  const std::shared_ptr<RType>& result) {
    Status s = FindOpcode<CType<std::shared_ptr<PType>, AType>, const std::shared_ptr<RType>&>(
                   ql_name, param_types, opcode, bfdecl, result);
    VLOG(3) << "Compiled function call " << ql_name << ". Status: " << s.ToString();
    return s;
  }

  // Interface for any collections of raw pointers.
  template<template<typename, typename> class CType, typename AType>
  static Status FindQLOpcodeImpl(const std::string& ql_name,
                                  const CType<PType*, AType>& param_types,
                                  BFOpcode *opcode,
                                  const BFDecl **bfdecl,
                                  const std::shared_ptr<RType>& result) {
    Status s = FindOpcode<CType<PType*, AType>, const std::shared_ptr<RType>&>(
                   ql_name, param_types, opcode, bfdecl, result);
    VLOG(3) << "Compiled function call " << ql_name << ". Status: " << s.ToString();
    return s;
  }

  //------------------------------------------------------------------------------------------------
  // Seeks builtin opcode using the given the std::vector of shared pointers.
  static Status FindQLOpcode(const std::string& ql_name,
                              const std::vector<std::shared_ptr<PType>>& param_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              const std::shared_ptr<RType>& result) {
    return FindQLOpcodeImpl<std::vector>(ql_name, param_types, opcode, bfdecl, result);
  }

  // Seeks builtin opcode using the given the std::vector of raw pointers.
  static Status FindQLOpcode(const std::string& ql_name,
                              const std::vector<PType*>& param_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              const std::shared_ptr<RType>& result) {
    return FindQLOpcodeImpl<std::vector>(ql_name, param_types, opcode, bfdecl, result);
  }

  // Seeks builtin opcode using the given the std::vector of Datatypes.
  static Status FindQLOpcode(const std::string& ql_name,
                              const std::vector<DataType>& actual_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              DataType *return_type) {
    Status s = FindOpcodeByType(ql_name, actual_types, opcode, bfdecl, return_type);
    VLOG(3) << "Compiled function call " << ql_name << ". Status: " << s.ToString();
    return s;
  }

  // Seeks CAST opcode from one type to another.
  static Status FindCastOpcode(DataType source, DataType target, BFOpcode *opcode) {
    if (source == target ||
        source == DataType::NULL_VALUE_TYPE ||
        target == DataType::NULL_VALUE_TYPE) {
      *opcode = yb::bfql::BFOPCODE_NOOP;
      return Status::OK();
    }

    // Find conversion opcode.
    const BFDecl *found_decl = nullptr;
    std::vector<DataType> actual_types = { source, target };
    DataType return_type = DataType::UNKNOWN_DATA;
    return FindQLOpcode(bfql::kCastFuncName, actual_types, opcode, &found_decl, &return_type);
  }
};

//--------------------------------------------------------------------------------------------------
// class BFExecApi<PType, RType> has one main entry function - ExecQLOpcode().
// ExecQLOpcode() executes builtin calls with the given opcode and arguments and writes result
// to the output parameter result. It reports error by returning Status.
//
// NOTES:
// The API are using templates which have the following requirements.
//
// - The datatype of parameters must have the following member functions.
//     bool IsNull() const;
//     InternalType type() const;
//     int8_t int8_value() const;
//     int16_t int16_value() const;
//     int32_t int32_value() const;
//     int64_t int64_value() const;
//     float float_value() const;
//     double double_value() const;
//     bool bool_value() const;
//     const std::string& string_value() const;
//     Timestamp timestamp_value() const;
//     const std::string& binary_value() const;
//     InetAddress inetaddress_value() const;
//
// - The datatype of return-results must have the following member functions.
//     void SetNull();
//     InternalType type() const;
//     void set_int8_value(int8_t val);
//     void set_int16_value(int16_t val);
//     void set_int32_value(int32_t val);
//     void set_int64_value(int64_t val);
//     void set_float_value(float val);
//     void set_double_value(double val);
//     void set_bool_value(bool val);
//     void set_string_value(const std::string& val);
//     void set_string_value(const char* val);
//     void set_string_value(const char* val, size_t size);
//     void set_timestamp_value(const Timestamp& val);
//     void set_timestamp_value(int64_t val);
//     void set_binary_value(const std::string& val);
//     void set_binary_value(const void* val, size_t size);
//     void set_inetaddress_value(const InetAddress& val);
//
// - Builtin-calls don't do implicit data conversion. They expect parameters to have the expected
//   type, and they always return data of the expected type. Arguments must be converted to correct
//   types before passing by using "cast" operator.
class BFExecApi {
 public:
  // Declare table of function pointers that take ref as arguments and raw pointers as result
  static const BFFunctions kBFExecFuncsRefAndRaw;

  //------------------------------------------------------------------------------------------------
  // Runs the associated entry in the table of function pointers on the given raw pointers.
  //   kBFExecFuncsRefAndRaw[opcode](args)
  static Result<BFRetValue> ExecQLOpcode(BFOpcode opcode, const BFParams& params) {
    // TODO(neil) There has to be some sanity error check here.
    RETURN_NOT_OK(CheckError(opcode, params));
    auto result = kBFExecFuncsRefAndRaw[to_underlying(opcode)](params);
    VLOG(3) << "Executed builtin call(" << to_underlying(opcode) << "). Status: "
            << ResultToStatus(result);
    return result;
  }

  static Status CheckError(BFOpcode opcode, const BFParams& params) {
    // TODO(neil) Currently, the execution phase is not yet implemented, so it'd be immature to
    // code for error-check here. Once it is implemented, we'll know what to check.
    if (VLOG_IS_ON(3)) {
      LOG(INFO) << "Executing opcode " << to_underlying(opcode);
    }
    return Status::OK();
  }
};

} // namespace bfql
} // namespace yb
