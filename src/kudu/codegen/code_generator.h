// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef KUDU_CODEGEN_CODE_GENERATOR_H
#define KUDU_CODEGEN_CODE_GENERATOR_H

#include "kudu/codegen/row_projector.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/status.h"

namespace llvm {
class LLVMContext;
} // namespace llvm

namespace kudu {

class Schema;

namespace codegen {

class RowProjectorFunctions;

// CodeGenerator is a top-level class that manages a per-module
// LLVM context, ExecutionEngine initialization, native target loading,
// and memory management.
//
// This generator is intended for JIT compilation of functions that
// are generated at runtime. These functions can make calls to pre-compiled
// C++ functions, which must be loaded from their *.ll files.
//
// Since the CodeGenerator is the owner of most of the LLVM compilation
// mechanisms (which in turn own most of the LLVM generated code), it also
// functions as a factory for the classes that use LLVM constructs dependent
// on the CodeGenerator's information.
//
// This class is thread-safe.
//
// The execution engine has a global lock for compilations. When a function
// is compiling, other threads will be blocked on their own compilations or
// runs. Because of this, a CodeGenerator should be assigned to a single
// compilation thread (See CompilationManager class). Threads may run
// codegen'd functions concurrently.
//
// Code generation may be disabled globally at compile time by defining
// the preprocessor macro KUDU_DISABLE_CODEGEN.
class CodeGenerator {
 public:
  // The constructor makes all the appropriate static LLVM initialization
  // calls exactly once.
  CodeGenerator();
  ~CodeGenerator();

  // Attempts to initialize row projector functions by compiling code
  // for the parameter schemas. Writes to 'out' upon success.
  Status CompileRowProjector(const Schema& base, const Schema& proj,
                             scoped_refptr<RowProjectorFunctions>* out);

 private:
  static void GlobalInit();

  // TODO static ObjectCache shared b/w engines

  DISALLOW_COPY_AND_ASSIGN(CodeGenerator);
};

} // namespace codegen
} // namespace kudu

#endif
