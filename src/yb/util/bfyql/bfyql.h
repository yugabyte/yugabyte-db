//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines the entries to the builtin library functions.
//   FindYqlOpcode() - Compiling builtin call into opcode.
//   ExecYqlOpcode() - Execute builtin call using opcode.
//
// NOTES ON BUILTIN DEFINITIONS
// ----------------------------
// Here's how new builtin functions are implemented or added to this library.
// * Define C++ function (e.g. Token) in this library or any C++ library (yb_bfyql).
//   If the function is defined in a different library, link it to this library.
// * Define associated YQL function ("token") by adding it to YQL parser.
// * In file "directory.cc", add an entry at the end of the kBFDirectory table.
//     { C++ func_name , YQL func_name , return_type, { formal_parameter_types } }
//     Example:
//     { "Token", "token", STRING, {TYPEARGS} }
// * The rest of the code would be auto-generated.
//
// NOTES ON PROCESSING BUILTIN CALLS
// ---------------------------------
// Here's how builtin calls can be processed using this library.
// - Getting the opcode from a process (such as client or proxy server).
//     FindYqlOpcode(function_name,              // Input:         Such as "+"
//                   actual_parameter_types,     // Input:         Types of arguments.
//                   opcode,                     // Output:        Found associated opcode.
//                   formal_parameter_types,     // Output:        Types of formal parameters.
//                   return_type)                // Input/Output:  Return type.
// - Send the opcode & parameter values to any processes (such as tablet server).
// - The receiving process can then execute it.
//     ExecYqlOpcode(opcode,                     // Input:         Opcode from compilation.
//                   param_values,               // Input:         Arguments.
//                   return_value)               // Output:        Computed result.
//
// NOTES ON COMPILING BUILTIN CALLS
// --------------------------------
// FindYqlOpcode() should be called to type check a builtin calls.
// * FindYqlOpcode() does type checking for the parameters (function signature).
// * FindYqlOpcode() outputs a BFOpcode to be used at execution time.
// * FindYqlOpcode() outputs the formal parameter types.
//   - This is the signature of the function.
//   - The arguments to a builtin call must be converted to these exact formal types.
// * FindYqlOpcode() inputs / outputs the return type.
//   - If return type is not given (UNKNOWN_DATA), it returns the expected return type to caller.
//   - If return type is given, it checks if the type is compatible with the function's return type.
//
// NOTES ON EXECUTING BUILTIN CALLS
// --------------------------------
// Call ExecYqlFunc(opcode, args, result)
// * Input arguments must be of exact datatypes as the formal parameter types. Operator "cast"
//   should have been used to convert these arguments when needed.
//
// * The return result must also be of expected type. The return value of expected type would be
//   written to this result.
//
// EXAMPPLE
// --------
// The test "/yb/util/bfyql/bfyql-test.cc" would be a good example on builtin-call usage.
// The file "/yb/sql/ptree/pt_bfunc.cc" can be used as example at the moment.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFYQL_BFYQL_H_
#define YB_UTIL_BFYQL_BFYQL_H_

#include <vector>
#include <list>

#include "yb/client/client.h"
#include "yb/util/logging.h"
#include "yb/util/bfyql/bfyql_template.h"

namespace yb {
namespace bfyql {

const char *const kCastFuncName = "cast";

//--------------------------------------------------------------------------------------------------
// class BFCompileApi<PType, RType> has one main entry function - FindYqlOpcode().
//
// FindYqlOpcode() finds the builtin opcode, signature, and return type using the function name.
// - This function is a template function which accept with any parameter and result classes that
//   implement the following functions.
//     DataType yql_type_id();
//     void set_yql_type_id(DataType);
//
// - Example
//   . YQL expression treenode can be used for both PType and RType because it has the two required
//     functions for set & get yql_type_ids.
//   . Pseudo code.
//     using BFCompileYql = BFCompileApi<std::shared_ptr<YQLValue>, YQLValue>.
//
// - Example for a class that can be used as PType and Rtype for this template.
//     class YourArg {
//       DataType yql_type_id() { return yql_type_id_; }
//       void set_yql_type_id(DataType t) { yql_type_id_ = t; }
//     };
//
//   Builtin library provides interface for both raw and shared pointers.
//     vector<YourArgPointer> params = { ... };
//     BFCompileApi<YourArg, YourArg>::FindYsqlOpcode(name, params, opcode, decl, result_ptr);

template<typename PType, typename RType>
class BFCompileApi {
 public:
  //------------------------------------------------------------------------------------------------
  // Because we using Arena allocator, our compiler don't use standard collection types such as
  // std::vector. The following templates allow the compiler to resolve builtin calls with with
  // various collection types.

  // Interface for any collections of shared_ptrs.
  template<template<typename, typename> class CType, typename AType>
  static Status FindYqlOpcodeImpl(const string& yql_name,
                                  const CType<std::shared_ptr<PType>, AType>& param_types,
                                  BFOpcode *opcode,
                                  const BFDecl **bfdecl,
                                  const std::shared_ptr<RType>& result) {
    Status s = FindOpcode<CType<std::shared_ptr<PType>, AType>, const std::shared_ptr<RType>&>(
                   yql_name, param_types, opcode, bfdecl, result);
    VLOG(3) << "Compiled function call " << yql_name << ". Status: " << s.ToString();
    return s;
  }

  // Interface for any collections of raw pointers.
  template<template<typename, typename> class CType, typename AType>
  static Status FindYqlOpcodeImpl(const string& yql_name,
                                  const CType<PType*, AType>& param_types,
                                  BFOpcode *opcode,
                                  const BFDecl **bfdecl,
                                  const std::shared_ptr<RType>& result) {
    Status s = FindOpcode<CType<PType*, AType>, const std::shared_ptr<RType>&>(
                   yql_name, param_types, opcode, bfdecl, result);
    VLOG(3) << "Compiled function call " << yql_name << ". Status: " << s.ToString();
    return s;
  }

  //------------------------------------------------------------------------------------------------
  // Seeks builtin opcode using the given the std::vector of shared pointers.
  static Status FindYqlOpcode(const string& yql_name,
                              const std::vector<std::shared_ptr<PType>>& param_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              const std::shared_ptr<RType>& result) {
    return FindYqlOpcodeImpl<std::vector>(yql_name, param_types, opcode, bfdecl, result);
  }

  // Seeks builtin opcode using the given the std::vector of raw pointers.
  static Status FindYqlOpcode(const string& yql_name,
                              const std::vector<PType*>& param_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              const std::shared_ptr<RType>& result) {
    return FindYqlOpcodeImpl<std::vector>(yql_name, param_types, opcode, bfdecl, result);
  }

  // Seeks builtin opcode using the given the std::vector of Datatypes.
  static Status FindYqlOpcode(const string& yql_name,
                              const std::vector<DataType>& actual_types,
                              BFOpcode *opcode,
                              const BFDecl **bfdecl,
                              DataType *return_type) {
    Status s = FindOpcodeByType(yql_name, actual_types, opcode, bfdecl, return_type);
    VLOG(3) << "Compiled function call " << yql_name << ". Status: " << s.ToString();
    return s;
  }
};

//--------------------------------------------------------------------------------------------------
// class BFExecApi<PType, RType> has one main entry function - ExecYqlOpcode().
// ExecYqlOpcode() executes builtin calls with the given opcode and arguments and writes result
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
template<typename PType,
         typename RType,
         template<typename, typename> class CType = std::vector,
         template<typename> class AType = std::allocator>
class BFExecApi {
 public:
  // Declare table of function pointers that take shared_ptr as inputs.
  static const vector<std::function<Status(const std::vector<std::shared_ptr<PType>>&,
                                           const std::shared_ptr<RType>&)>>
      kBFExecFuncs;

  // Declare table of function pointers that take raw pointers as inputs.
  static const vector<std::function<Status(const std::vector<PType*>&, RType*)>>
      kBFExecFuncsRaw;

  // Runs the associated entry in the table of function pointers on the given shared_ptrs.
  //   kBFExecFuncs[opcode](args)
  static Status ExecYqlOpcode(BFOpcode opcode,
                              const std::vector<std::shared_ptr<PType>>& params,
                              const std::shared_ptr<RType>& result) {
    // TODO(neil) There has to be some sanity error check here.
    RETURN_NOT_OK(CheckError(opcode, params, result));
    Status s = kBFExecFuncs[static_cast<int>(opcode)](params, result);
    VLOG(3) << "Executed builtin call(" << int(opcode) << "). Status: " << s.ToString();
    return s;
  }

  // Runs the associated entry in the table of function pointers on the given raw pointers.
  //   kBFExecFuncs[opcode](args)
  static Status ExecYqlOpcode(BFOpcode opcode,
                              const std::vector<PType*>& params,
                              RType *result) {
    // TODO(neil) There has to be some sanity error check here.
    RETURN_NOT_OK(CheckError(opcode, params, result));
    Status s = kBFExecFuncsRaw[static_cast<int>(opcode)](params, result);
    VLOG(3) << "Executed builtin call(" << int(opcode) << "). Status: " << s.ToString();
    return s;
  }

  // TODO(neil) Because opcodes are created and executed by different processes, some sanity error
  // checking must be done at run time in logging mode.
  static Status CheckError(BFOpcode opcode,
                           const std::vector<std::shared_ptr<PType>>& params,
                           const std::shared_ptr<RType>& result) {
    // TODO(neil) Currently, the execution phase is not yet implemented, so it'd be immature to
    // code for error-check here. Once it is implemented, we'll know what to check.
    if (VLOG_IS_ON(3)) {
      LOG(INFO) << "Executing opcode " << int(opcode);
    }
    return Status::OK();
  }

  static Status CheckError(BFOpcode opcode,
                           const std::vector<PType*>& params,
                           RType *result) {
    // TODO(neil) Currently, the execution phase is not yet implemented, so it'd be immature to
    // code for error-check here. Once it is implemented, we'll know what to check.
    if (VLOG_IS_ON(3)) {
      LOG(INFO) << "Executing opcode " << int(opcode);
    }
    return Status::OK();
  }
};

//--------------------------------------------------------------------------------------------------
// This class is conveniently and ONLY for testing purpose. It executes builtin calls by names
// instead of opcode.
template<typename PType,
         typename RType,
         template<typename, typename> class CType = std::vector,
         template<typename> class AType = std::allocator>
class BFExecImmediateApi : public BFExecApi<PType, RType, CType, AType> {
 public:
  // Interface for shared_ptr.
  static Status ExecYqlFunc(const string& yql_name,
                            const std::vector<std::shared_ptr<PType>>& params,
                            const std::shared_ptr<RType>& result) {
    BFOpcode opcode;
    const BFDecl *bfdecl;
    RETURN_NOT_OK((FindOpcode<std::vector<std::shared_ptr<PType>>, const std::shared_ptr<RType>&>(
        yql_name, params, &opcode, &bfdecl, result)));
    return BFExecApi<PType, RType>::ExecYqlOpcode(opcode, params, result);
  }

  // Interface for raw pointer.
  static Status ExecYqlFunc(const string& yql_name,
                            const std::vector<PType*>& params,
                            RType *result) {
    BFOpcode opcode;
    const BFDecl *bfdecl;
    RETURN_NOT_OK(
        (FindOpcode<std::vector<PType*>, RType*>(yql_name, params, &opcode, &bfdecl, result)));
    return BFExecApi<PType, RType>::ExecYqlOpcode(opcode, params, result);
  }
};

} // namespace bfyql
} // namespace yb

//--------------------------------------------------------------------------------------------------
// Generated tables "kBFExecFuncs" and "kBFExecFuncsRaw".
// Because the tables must be initialized after the specification for "class BFExecApi", we have to
// include header file "gen_bfunc_table.h" at the end of this file.
#include "yb/util/bfyql/gen_bfunc_table.h"

#endif  // YB_UTIL_BFYQL_BFYQL_H_
