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

#include "kudu/codegen/module_builder.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include "kudu/codegen/precompiled.ll.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/status.h"

#ifndef CODEGEN_MODULE_BUILDER_DO_OPTIMIZATIONS
#if NDEBUG
#define CODEGEN_MODULE_BUILDER_DO_OPTIMIZATIONS 1
#else
#define CODEGEN_MODULE_BUILDER_DO_OPTIMIZATIONS 0
#endif
#endif

using llvm::CodeGenOpt::Level;
using llvm::ConstantExpr;
using llvm::ConstantInt;
using llvm::EngineBuilder;
using llvm::ExecutionEngine;
using llvm::Function;
using llvm::FunctionType;
using llvm::IntegerType;
using llvm::legacy::FunctionPassManager;
using llvm::legacy::PassManager;
using llvm::LLVMContext;
using llvm::Module;
using llvm::PassManagerBuilder;
using llvm::PointerType;
using llvm::raw_os_ostream;
using llvm::SMDiagnostic;
using llvm::TargetMachine;
using llvm::Type;
using llvm::Value;
using std::move;
using std::ostream;
using std::string;
using std::stringstream;
using std::unique_ptr;
using std::vector;
using strings::Substitute;

namespace kudu {
namespace codegen {

namespace {

string ToString(const SMDiagnostic& err) {
  stringstream sstr;
  raw_os_ostream os(sstr);
  err.print("precompiled.ll", os);
  os.flush();
  return Substitute("line $0 col $1: $2",
                    err.getLineNo(), err.getColumnNo(),
                    sstr.str());
}

string ToString(const Module& m) {
  stringstream sstr;
  raw_os_ostream os(sstr);
  os << m;
  return sstr.str();
}

// This method is needed for the implicit conversion from
// llvm::StringRef to std::string
string ToString(const Function* f) {
  return f->getName();
}

bool ModuleContains(const Module& m, const Function* fptr) {
  for (const auto& function : m) {
    if (&function == fptr) return true;
  }
  return false;
}

} // anonymous namespace

ModuleBuilder::ModuleBuilder()
  : state_(kUninitialized),
    context_(new LLVMContext()),
    builder_(*context_) {}

ModuleBuilder::~ModuleBuilder() {}

Status ModuleBuilder::Init() {
  CHECK_EQ(state_, kUninitialized) << "Cannot Init() twice";

  // Even though the LLVM API takes an explicit length for the input IR,
  // it appears to actually depend on NULL termination. We assert for it
  // here because otherwise we end up with very strange LLVM errors which
  // are tough to debug.
  CHECK_EQ('\0', precompiled_ll_data[precompiled_ll_len]) << "IR not properly NULL-terminated";

  // However, despite depending on the buffer being null terminated, it doesn't
  // expect the null terminator to be included in the length of the buffer.
  // Per http://llvm.org/docs/doxygen/html/classllvm_1_1MemoryBuffer.html :
  //   > In addition to basic access to the characters in the file, this interface
  //   > guarantees you can read one character past the end of the file, and that this
  //   > character will read as '\0'.
  llvm::StringRef ir_data(precompiled_ll_data, precompiled_ll_len);
  CHECK_GT(ir_data.size(), 0) << "IR not properly linked";

  // Parse IR.
  SMDiagnostic err;
  unique_ptr<llvm::MemoryBuffer> ir_buf(llvm::MemoryBuffer::getMemBuffer(ir_data));
  module_ = llvm::parseIR(ir_buf->getMemBufferRef(), err, *context_);
  if (!module_) {
    return Status::ConfigurationError("Could not parse IR", ToString(err));
  }
  VLOG(3) << "Successfully parsed IR:\n" << ToString(*module_);

  // TODO: consider parsing this module once instead of on each invocation.
  state_ = kBuilding;
  return Status::OK();
}

Function* ModuleBuilder::Create(FunctionType* fty, const string& name) {
  CHECK_EQ(state_, kBuilding);
  return Function::Create(fty, Function::ExternalLinkage, name, module_.get());
}

Function* ModuleBuilder::GetFunction(const string& name) {
  CHECK_EQ(state_, kBuilding);
  // All extern "C" functions are guaranteed to have the same
  // exact name as declared in the source file.
  return CHECK_NOTNULL(module_->getFunction(name));
}

Type* ModuleBuilder::GetType(const string& name) {
  CHECK_EQ(state_, kBuilding);
  // Technically clang is not obligated to name every
  // class as "class.kudu::ClassName" but so long as there
  // are no naming conflicts in the LLVM context it appears
  // to do so (naming conflicts are avoided by having 1 context
  // per module)
  return CHECK_NOTNULL(module_->getTypeByName(name));
}

Value* ModuleBuilder::GetPointerValue(void* ptr) const {
  CHECK_EQ(state_, kBuilding);
  // No direct way of creating constant pointer values in LLVM, so
  // first a constant int has to be created and then casted to a pointer
  IntegerType* llvm_uintptr_t = Type::getIntNTy(*context_, 8 * sizeof(ptr));
  uintptr_t int_value = reinterpret_cast<uintptr_t>(ptr);
  ConstantInt* llvm_int_value = ConstantInt::get(llvm_uintptr_t,
                                                 int_value, false);
  Type* llvm_ptr_t = Type::getInt8PtrTy(*context_);
  return ConstantExpr::getIntToPtr(llvm_int_value, llvm_ptr_t);
}


void ModuleBuilder::AddJITPromise(llvm::Function* llvm_f,
                                  FunctionAddress* actual_f) {
  CHECK_EQ(state_, kBuilding);
  DCHECK(ModuleContains(*module_, llvm_f))
    << "Function " << ToString(llvm_f) << " does not belong to ModuleBuilder.";
  JITFuture fut;
  fut.llvm_f_ = llvm_f;
  fut.actual_f_ = actual_f;
  futures_.push_back(fut);
}

namespace {

#if CODEGEN_MODULE_BUILDER_DO_OPTIMIZATIONS

void DoOptimizations(ExecutionEngine* engine,
                     Module* module,
                     const vector<const char*>& external_functions) {
  PassManagerBuilder pass_builder;
  pass_builder.OptLevel = 2;
  // Don't optimize for code size (this corresponds to -O2/-O3)
  pass_builder.SizeLevel = 0;
  pass_builder.Inliner = llvm::createFunctionInliningPass();

  FunctionPassManager fpm(module);
  pass_builder.populateFunctionPassManager(fpm);
  fpm.doInitialization();

  // For each function in the module, optimize it
  for (Function& f : *module) {
    // The bool return value here just indicates whether the passes did anything.
    // We can safely expect that many functions are too small to do any optimization.
    ignore_result(fpm.run(f));
  }
  fpm.doFinalization();

  PassManager module_passes;

  // Internalize all functions that aren't explicitly specified with external linkage.
  module_passes.add(llvm::createInternalizePass(external_functions));
  pass_builder.populateModulePassManager(module_passes);

  // Same as above, the result here just indicates whether optimization made any changes.
  // Don't need to check it.
  ignore_result(module_passes.run(*module));
}

#endif

} // anonymous namespace

Status ModuleBuilder::Compile(unique_ptr<ExecutionEngine>* out) {
  CHECK_EQ(state_, kBuilding);

  // Attempt to generate the engine
  string str;
#ifdef NDEBUG
  Level opt_level = llvm::CodeGenOpt::Aggressive;
#else
  Level opt_level = llvm::CodeGenOpt::None;
#endif
  Module* module = module_.get();
  EngineBuilder ebuilder(move(module_));
  ebuilder.setErrorStr(&str);
  ebuilder.setOptLevel(opt_level);
  target_ = ebuilder.selectTarget();
  unique_ptr<ExecutionEngine> local_engine(ebuilder.create(target_));
  if (!local_engine) {
    return Status::ConfigurationError("Code generation for module failed. "
                                      "Could not start ExecutionEngine",
                                      str);
  }
  module->setDataLayout(target_->createDataLayout());

#if CODEGEN_MODULE_BUILDER_DO_OPTIMIZATIONS
  DoOptimizations(local_engine.get(), module, GetFunctionNames());
#endif

  // Compile the module
  local_engine->finalizeObject();

  // Satisfy the promises
  for (JITFuture& fut : futures_) {
    *fut.actual_f_ = local_engine->getPointerToFunction(fut.llvm_f_);
    if (*fut.actual_f_ == nullptr) {
      return Status::NotFound(
        "Code generation for module failed. Could not find function \""
        + ToString(fut.llvm_f_) + "\".");
    }
  }

  // For LLVM 3.7, generated code lasts exactly as long as the execution engine
  // that created it does. Furthermore, if the module is removed from the
  // engine's ownership, neither the context nor the module have to stick
  // around for the jitted code to run.
  CHECK(local_engine->removeModule(module)); // releases ownership
  module_.reset(module);

  // Upon success write to the output parameter
  out->swap(local_engine);
  state_ = kCompiled;
  return Status::OK();
}

TargetMachine* ModuleBuilder::GetTargetMachine() const {
  CHECK_EQ(state_, kCompiled);
  return CHECK_NOTNULL(target_);
}

vector<const char*> ModuleBuilder::GetFunctionNames() const {
  vector<const char*> ret;
  for (const JITFuture& fut : futures_) {
    const char* name = CHECK_NOTNULL(fut.llvm_f_)->getName().data();
    ret.push_back(name);
  }
  return ret;
}

} // namespace codegen
} // namespace kudu
