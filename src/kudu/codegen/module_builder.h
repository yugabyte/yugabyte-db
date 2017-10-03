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

#ifndef KUDU_CODEGEN_FUNCTION_BUILDER_H
#define KUDU_CODEGEN_FUNCTION_BUILDER_H

#include <memory>
#include <string>
#include <vector>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include "kudu/util/status.h"

namespace llvm {

class ExecutionEngine;
class Function;
class FunctionType;
class LLVMContext;
class Module;
class TargetMachine;
class Type;
class Value;

} // namespace llvm

namespace kudu {
namespace codegen {

// A ModuleBuilder provides an interface to generate code for procedures
// given a CodeGenerator to refer to. Builder can be used to create multiple
// functions. It is intended to make building functions easier than using
// LLVM's IRBuilder<> directly. Finally, a builder also provides an interface
// to precompiled functions and makes sure that the bytecode is linked to
// the working module.
//
// This class is not thread-safe. It is intended to be used by a single
// thread to build a set of functions.
//
// This class is just a helper for other classes within the codegen
// directory. It is intended to be used within *.cc files, and not to be
// included in outward-facing classes so other directories do not have a
// dependency on LLVM (this class is necessary because the templated
// IRBuilder<> cannot be forward-declared since it has default arguments).
// This class, however, can easily be forward-declared.
class ModuleBuilder {
 private:
  typedef void* FunctionAddress;

 public:
  // Provide alias so template arguments can be changed in one place
  typedef llvm::IRBuilder<> LLVMBuilder;

  // Creates a builder with a fresh module and context.
  ModuleBuilder();

  // Deletes own module and context if they have not been compiled.
  ~ModuleBuilder();

  // Inits a new module with parsed precompiled IR from precompiled.cc.
  // TODO: with multiple *.ll files, each file should be loaded on demand
  Status Init();

  // Create a new, empty function in the module with external linkage
  llvm::Function* Create(llvm::FunctionType* fty, const std::string& name);
  // Retrieve a precompiled type
  llvm::Type* GetType(const std::string& name);
  // Retrieve a precompiled function
  llvm::Function* GetFunction(const std::string& name);
  // Get the LLVM wrapper for a constant pointer value of type i8*
  llvm::Value* GetPointerValue(void* ptr) const;

  LLVMBuilder* builder() { return &builder_; }

  // Once a function is complete, it may be offered to the module builder
  // along with the location of the function pointer to be written to
  // with the value of the JIT-compiled function pointer. Once the module
  // builder's Compile() method is called, these value are filled.
  // Requires that llvm::Function belong to this ModuleBuilder's module.
  template<class FuncPtr>
  void AddJITPromise(llvm::Function* llvm_f, FuncPtr* actual_f) {
    // The below cast is technically yields undefined behavior for
    // versions of the standard prior to C++0x. However, the llvm
    // interface forces us to use object-pointer to function-pointer
    // casting.
    AddJITPromise(llvm_f, reinterpret_cast<FunctionAddress*>(actual_f));
  }

  // Compiles all promised functions. Builder may not be used after
  // this method, only destructed. Upon success, releases ownership
  // of the execution engine through the 'out' parameter.
  //
  // After this method has been called, the jit-compiled code may be
  // called as long as 'out' remains alive. Once 'out' destructs,
  // the code will be freed.
  Status Compile(std::unique_ptr<llvm::ExecutionEngine>* out);

  // Retrieves the TargetMachine that the engine builder guessed was
  // the native target. Requires compilation is complete.
  // Pointer is valid while Compile()'s ExecutionEngine is.
  llvm::TargetMachine* GetTargetMachine() const;

 private:
  // The different states a ModuleBuilder can be in.
  enum MBState {
    kUninitialized,
    kBuilding,
    kCompiled
  };
  // Basic POD which associates an llvm::Function to the location where its
  // function pointer should be written to after compilation.
  struct JITFuture {
    llvm::Function* llvm_f_;
    FunctionAddress* actual_f_;
  };

  void AddJITPromise(llvm::Function* llvm_f, FunctionAddress* actual_f);
  // Returns a vector of the function names for the functions stored in the
  // JITFutures. The pointers are valid so long as the futures_ vector's
  // elements have valid llvm::Function* values.
  std::vector<const char*> GetFunctionNames() const;

  MBState state_;
  std::vector<JITFuture> futures_;
  std::unique_ptr<llvm::LLVMContext> context_;
  std::unique_ptr<llvm::Module> module_;
  LLVMBuilder builder_;
  llvm::TargetMachine* target_; // not owned

  DISALLOW_COPY_AND_ASSIGN(ModuleBuilder);
};

} // namespace codegen
} // namespace kudu

#endif
