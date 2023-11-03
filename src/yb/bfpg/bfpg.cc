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
//--------------------------------------------------------------------------------------------------

#include "yb/bfpg/bfpg.h"

#include <functional>
#include <unordered_map>
#include <string>

#include "yb/common/ql_type.h"

using std::function;
using std::vector;
using std::string;
using strings::Substitute;

namespace yb {
namespace bfpg {

//--------------------------------------------------------------------------------------------------
bool IsAggregateOpcode(TSOpcode op) {
  switch (op) {
  case TSOpcode::kAvg: FALLTHROUGH_INTENDED;
  case TSOpcode::kCount: FALLTHROUGH_INTENDED;
  case TSOpcode::kMax: FALLTHROUGH_INTENDED;
  case TSOpcode::kMin: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumInt8: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumInt16: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumInt32: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumInt64: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumFloat: FALLTHROUGH_INTENDED;
  case TSOpcode::kSumDouble:
    return true;
  default:
    return false;
  }
}

//--------------------------------------------------------------------------------------------------
// Check compatible type in function call.
inline bool IsCompatible(DataType left, DataType right) {
  return QLType::IsPotentiallyConvertible(left, right);
}

//--------------------------------------------------------------------------------------------------
// HasExactSignature() is a predicate to check if the datatypes of actual parameters (arguments)
// and formal parameters (signature) are identical.
// NOTES:
//   PTypePtr can be a (shared_ptr<MyClass>) or a raw pointer (MyClass*)

static bool HasExactTypeSignature(const std::vector<DataType>& signature,
                                  const std::vector<DataType>& actual_types) {
  // Check parameter count.
  const auto formal_count = signature.size();
  const auto actual_count = actual_types.size();

  // Check for exact match.
  size_t index;
  for (index = 0; index < formal_count; index++) {
    // Check if the signature accept varargs which can be matched with the rest of arguments.
    if (signature[index] == DataType::TYPEARGS) {
      return true;
    }

    // Return false if one of the following is true.
    // - The number of arguments is less than the formal count.
    // - The datatype of an argument is not an exact match of the signature type.
    if (index >= actual_count || signature[index] != actual_types[index]) {
      return false;
    }
  }

  // Assert that the number of arguments is the same as the formal count.
  if (index < actual_count) {
    return false;
  }
  return true;
}

//--------------------------------------------------------------------------------------------------
// HasSimilarSignature() is a predicate to check if the datatypes of actual parameters (arguments)
// and formal parameters (signature) are similar.
//
// Similar is mainly used for integers vs floating point values.
//   INT8 is "similar" to INT64.
//   INT8 is NOT "similar" to DOUBLE.
//   FLOAT is "similar" to DOUBLE.
// This rule is to help resolve the overloading functions between integers and float point data.
//
// NOTES:
//   PTypePtr and RTypePtr can be either a (shared_ptr<MyClass>) or a raw pointer (MyClass*)

static bool HasSimilarTypeSignature(const std::vector<DataType>& signature,
                                    const std::vector<DataType>& actual_types) {
  const auto formal_count = signature.size();
  const auto actual_count = actual_types.size();

  // Check for exact match.
  size_t index;
  for (index = 0; index < formal_count; index++) {
    // Check if the signature accept varargs which can be matched with the rest of arguments.
    if (signature[index] == DataType::TYPEARGS) {
      return true;
    }

    // Return false if one of the following is true.
    // - The number of arguments is less than the formal count.
    // - The datatype of an argument is not an similar match of the signature type.
    if (index >= actual_count || !QLType::IsSimilar(signature[index], actual_types[index])) {
      return false;
    }
  }

  // Assert that the number of arguments is the same as the formal count.
  if (index < actual_count) {
    return false;
  }

  return true;
}

//--------------------------------------------------------------------------------------------------
// HasCompatibleSignature() is a predicate to check if the arguments is convertible to the
// signature.
//
// TODO(neil) Needs to allow passing double to int.
// NOTES:
//   PTypePtr and RTypePtr can be either a (shared_ptr<MyClass>) or a raw pointer (MyClass*).

static bool HasCompatibleTypeSignature(const std::vector<DataType>& signature,
                                       const std::vector<DataType>& actual_types) {

  const auto formal_count = signature.size();
  const auto actual_count = actual_types.size();

  // Check for compatible match.
  size_t index;
  for (index = 0; index < formal_count; index++) {
    // Check if the signature accept varargs which can be matched with the rest of arguments.
    if (signature[index] == DataType::TYPEARGS) {
      return true;
    }

    // Return false if one of the following is true.
    // - The number of arguments is less than the formal count.
    // - The datatype of an argument is not a compatible match of the signature type.
    if (index >= actual_count || !IsCompatible(signature[index], actual_types[index])) {
      return false;
    }
  }

  // Assert that the number of arguments is the same as the formal count.
  if (index < actual_count) {
    return false;
  }

  return true;
}

//--------------------------------------------------------------------------------------------------
// Searches all overloading versions of a function and finds exactly one function specification
// whose signature matches with the datatypes of the arguments.
// NOTES:
//   "compare_signature" is a predicate to compare datatypes of formal and actual parameters.
//   PTypePtr and RTypePtr can be either a (shared_ptr<MyClass>) or a raw pointer (MyClass*)
static Status FindMatch(
    const string& ql_name,
    function<bool(const std::vector<DataType>&, const std::vector<DataType>&)> compare_signature,
    BFOpcode max_opcode,
    const std::vector<DataType>& actual_types,
    BFOpcode *found_opcode,
    const BFDecl **found_decl,
    DataType *return_type) {

  // Find a compatible operator, and raise error if there's more than one match.
  const BFOperator *compatible_operator = nullptr;
  while (true) {
    const BFOperator *bf_operator = kBFOperators[static_cast<int>(max_opcode)].get();
    DCHECK(max_opcode == bf_operator->opcode());

    // Check if each parameter has compatible type match.
    if (compare_signature(bf_operator->param_types(), actual_types)) {
      // Found a compatible match. Make sure that it is the only match.
      if (compatible_operator != nullptr) {
        return STATUS(InvalidArgument,
                      Substitute("Found too many matches for builtin function '$0'", ql_name));
      }
      compatible_operator = bf_operator;
    }

    // Break the loop if we have processed all operators in the overloading chain.
    if (max_opcode == bf_operator->overloaded_opcode()) {
      break;
    }

    // Jump to the next overloading opcode.
    max_opcode = bf_operator->overloaded_opcode();
  }

  // Returns error if no match is found.
  if (compatible_operator == nullptr) {
    return STATUS(NotFound,
                  Substitute("Signature mismatch in call to builtin function '$0'", ql_name));
  }

  // Returns error if the return type is not compatible.
  if (return_type != nullptr) {
    if (QLType::IsUnknown(*return_type)) {
      *return_type = compatible_operator->return_type();
    } else if (!IsCompatible(*return_type, compatible_operator->return_type())) {
      return STATUS(InvalidArgument,
                    Substitute("Return-type mismatch in call to builtin function '$0'", ql_name));
    }
  }

  // Raise error if the function execution was not yet implemented.
  if (!compatible_operator->op_decl()->implemented()) {
    return STATUS(NotSupported,
                  Substitute("Builtin function '$0' is not yet implemented", ql_name));
  }

  *found_opcode = compatible_operator->opcode();
  *found_decl = compatible_operator->op_decl();
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Find the builtin opcode, declaration, and return type for a builtin call.
// Inputs: Builtin function name and parameter types.
// Outputs: opcode and bfdecl.
// In/Out parameter: return_type
//   If return_type is given, check if it is compatible with the declaration.
//   If not, return_type is an output parameter whose value is the return type of the builtin.
Status FindOpcodeByType(const string& ql_name,
                        const std::vector<DataType>& actual_types,
                        BFOpcode *opcode,
                        const BFDecl **bfdecl,
                        DataType *return_type) {
  auto entry = kBfPgsqlName2Opcode.find(ql_name);
  if (entry == kBfPgsqlName2Opcode.end()) {
    VLOG(3) << strings::Substitute("Function '$0' does not exist", ql_name);
    return STATUS(NotFound, strings::Substitute("Function '$0' does not exist", ql_name));
  }

  // Seek the correct overload functions in the following order:
  // - Find the exact signature match.
  //   Example:
  //   . Overload #1: FuncX(int8_t i) would be used for the call FuncX(int8_t(7)).
  //   . Overload #2: FuncX(int16_t i) would be used for the call FuncX(int16_t(7)).
  //
  // - For "cast" operator, if exact match is not found, return error. For all other operators,
  //   continue to the next steps.
  //
  // - Find the similar signature match.
  //   Example:
  //   . Overload #2: FuncY(int64_t i) would be used for FuncY(int8_t(7)).
  //       int64_t and int8_t are both integer values.
  //   . Overload #1: FuncY(double d) would be used for FuncY(float(7)).
  //       double & float are both floating values.
  //
  // - Find the compatible match. Signatures are of convertible datatypes.
  Status s = FindMatch(ql_name, HasExactTypeSignature, entry->second, actual_types,
                       opcode, bfdecl, return_type);
  VLOG(3) << "Seek exact match for function call " << ql_name << "(): " << s.ToString();

  if (ql_name != kCastFuncName && s.IsNotFound()) {
    s = FindMatch(ql_name, HasSimilarTypeSignature, entry->second, actual_types,
                  opcode, bfdecl, return_type);
    VLOG(3) << "Seek similar match for function call " << ql_name << "(): " << s.ToString();

    if (s.IsNotFound()) {
      s = FindMatch(ql_name, HasCompatibleTypeSignature, entry->second, actual_types,
                    opcode, bfdecl, return_type);
      VLOG(3) << "Seek compatible match for function call " << ql_name << "(): " << s.ToString();
    }
  }

  return s;
}

} // namespace bfpg
} // namespace yb
