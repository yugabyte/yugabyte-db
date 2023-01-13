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
// This module is used to specify builtin functions. Builtin "directory" is the repository of
// declarations for all builtin functions.
//
// USAGE NOTES
// -----------
// These notes discusses the procedure for implementing a new builtin function.
// * Implement the C++ function to be executed when the builtin-function is called. It can be
//   defined in this library or a linked library.
// * Add an entry to the builtin-directory table (File directory.cc).
//   Example: { "Add", "+", STRING, { STRING, STRING} }
//   - "Add" is a C++ function
//     string Add(string x, string y);
//   - "+" is a QL function, a builtin method.
//   - STRING of type yb::DataType represents the return type of the QL "+" function.
//   - { STRING, STRING } represents the signature of the QL "+" function.
// * The rest of the code is auto-generated.
//
// IMPLEMENTATION NOTES
// --------------------
// Developers who use builtin library don't need to know the following information unless some work
// on this library is needed.
//
// These notes discusses the general ideas on how this builtin library is implemented. To support
// the compilation and execution of a built-in call, this library auto-generates code according to
// the builtin function specifications in file "directory.cc".
//
// * An entry in builtin directory would be an input to BFDecl constructor.
//   - Entry:         { "Add", "+", STRING, {STRING, DOUBLE} }
//   - Constructor:   BFDecl("Add", "+", STRING, vector({STRING, DOUBLE}));
//   - Directory:     vector<all BFDecl entries>
//
// * For each of these entries, three main components are generated.
//   - a "BFOpcode" (an enum value):
//     Each builtin call is associated with a unique enum value to be used accross all processes.
//     The opcode that is generated in one process can be used in another process as long as they
//     are both linked with this builtin library.
//
//   - a "BFOperator" (a class):
//     A structure that is used during compilation for type checking & resolution. This struct
//     contains a BFDecl which defines specification of a builtin function.
//
//   - a "BFOperator::Exec" (function pointer):
//     A wrapper around actual C++ function pointer. This wrapper function pointer takes a vector
//     of parameters and result, and return a STATUS. The of BFunction implementation is upto the
//     application developers. Generally, a BFunction would read C++ values such as "int32_t" or
//     "std::string" from parameter and write the C++ return value to the output result.
//
// * Three main tables - vector<BFOpcode>, vector<BFOperator>, and vector<Exec> - are created. The
//   order of table entries matches exactly with the order of their BFDecls in the directory.
//   This is how we can associate all builtin functions with these entries in order to compile
//   them as well as execute them.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <vector>

#include "yb/bfql/bfdecl.h"

namespace yb {
namespace bfql {

extern const std::vector<BFDecl> kBFDirectory;

} // namespace bfql
} // namespace yb
