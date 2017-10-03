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

#include "kudu/codegen/code_generator.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

#include <glog/logging.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCDisassembler.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetInstrInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetRegisterInfo.h>
#include <llvm/Target/TargetSubtargetInfo.h>

#include "kudu/codegen/jit_wrapper.h"
#include "kudu/codegen/module_builder.h"
#include "kudu/codegen/row_projector.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/once.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/status.h"

DEFINE_bool(codegen_dump_functions, false, "Whether to print the LLVM IR"
            " for generated functions");
TAG_FLAG(codegen_dump_functions, experimental);
TAG_FLAG(codegen_dump_functions, runtime);
DEFINE_bool(codegen_dump_mc, false, "Whether to dump the disassembly of the"
            " machine code for generated functions.");
TAG_FLAG(codegen_dump_mc, experimental);
TAG_FLAG(codegen_dump_mc, runtime);

namespace llvm {
class MCAsmInfo;
class MCInstrInfo;
class MCRegisterInfo;
} // namespace llvm

using llvm::ArrayRef;
using llvm::ExecutionEngine;
using llvm::MCAsmInfo;
using llvm::MCContext;
using llvm::MCDisassembler;
using llvm::MCInst;
using llvm::MCInstPrinter;
using llvm::MCInstrInfo;
using llvm::MCRegisterInfo;
using llvm::MCSubtargetInfo;
using llvm::Module;
using llvm::raw_os_ostream;
using llvm::StringRef;
using llvm::Target;
using llvm::TargetMachine;
using llvm::Triple;
using std::string;

namespace kudu {

class Schema;

namespace codegen {

namespace {

// Returns Status::OK() if codegen is not disabled and an error status indicating
// that codegen has been disabled otherwise.
Status CheckCodegenEnabled() {
#ifdef KUDU_DISABLE_CODEGEN
  return Status::NotSupported("Code generation has been disabled at compile time.");
#else
  return Status::OK();
#endif
}

const uint8_t* ptr_from_i64(uint64_t addr) {
  COMPILE_ASSERT(sizeof(uint64_t) <= sizeof(uintptr_t), cannot_represent_address_as_pointer);
  uintptr_t iptr = addr;
  return reinterpret_cast<const uint8_t*>(iptr);
}

template<class FuncPtr>
uint64_t i64_from_ptr(FuncPtr ptr) {
  COMPILE_ASSERT(sizeof(uintptr_t) <= sizeof(uint64_t),
                 cannot_represent_pointer_as_address);
  // This cast is undefined prior to C++11 and only optionally supported even
  // with it. However, we must use this because of the LLVM interface.
  uintptr_t iptr = reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(ptr));
  return iptr;
}

// Prints assembly for a function pointed to by 'fptr' given a target
// machine 'tm'. Method is more or less platform-independent, but relies
// on the return instruction containing the "RET" string to terminate in
// the right place. Prints at most 'max_instr' instructions.
//
// Returns number of lines printed.
template<class FuncPtr>
int DumpAsm(FuncPtr fptr, const TargetMachine& tm, std::ostream* out, int max_instr) {
  uint64_t base_addr = i64_from_ptr(fptr);

  const MCInstrInfo& instr_info = *CHECK_NOTNULL(tm.getMCInstrInfo());
  const MCRegisterInfo* register_info = CHECK_NOTNULL(tm.getMCRegisterInfo());
  const MCAsmInfo* asm_info = CHECK_NOTNULL(tm.getMCAsmInfo());
  const MCSubtargetInfo subtarget_info = *CHECK_NOTNULL(tm.getMCSubtargetInfo());
  const Triple& triple = tm.getTargetTriple();

  MCContext context(asm_info, register_info, nullptr);

  gscoped_ptr<MCDisassembler> disas(
    CHECK_NOTNULL(tm.getTarget().createMCDisassembler(subtarget_info, context)));

  // LLVM uses these completely undocumented magic syntax constants which had
  // to be found in lib/Target/$ARCH/MCTargetDesc/$(ARCH)TargetDesc.cpp.
  // Apparently this controls stuff like AT&T vs Intel syntax for x86, but
  // there aren't always multiple values to choose from on different architectures.
  // It seems that there's an unspoken rule to implement SyntaxVariant = 0.
  // This only has meaning for a *given* target, but at least the 0th syntax
  // will always be defined, so that's what we use.
  static const unsigned kSyntaxVariant = 0;
  gscoped_ptr<MCInstPrinter> printer(
    CHECK_NOTNULL(tm.getTarget().createMCInstPrinter(triple, kSyntaxVariant, *asm_info,
                                                     instr_info, *register_info)));

  // Make a memory object referring to the bytes with addresses ranging from
  // base_addr to base_addr + (maximum number of bytes instructions take).
  const size_t kInstrSizeMax = 16; // max on x86 is 15 bytes
  ArrayRef<uint8_t> mem_obj(ptr_from_i64(base_addr), max_instr * kInstrSizeMax);
  uint64_t addr = 0;

  for (int i = 0; i < max_instr; ++i) {
    raw_os_ostream os(*out);
    MCInst inst;
    uint64_t size;
    MCDisassembler::DecodeStatus stat =
      disas->getInstruction(inst, size, mem_obj.slice(addr), addr, llvm::nulls(), llvm::nulls());
    if (stat != MCDisassembler::Success) {
      *out << "<ERROR at 0x" << std::hex << addr
           << " (absolute 0x" << (addr + base_addr) << ")"
           << ", skipping instruction>\n" << std::dec;
    } else {
      string annotations;
      printer->printInst(&inst, os, annotations, subtarget_info);
      os << " " << annotations << "\n";
      // We need to check the opcode name for "RET" instead of comparing
      // the opcode to llvm::ReturnInst::getOpcode() because the native
      // opcode may be different, there may different types of returns, etc.
      // TODO: this may fail if there are multiple 'ret' instructions in one
      // function (in separate branches). In order to avoid this problem,
      // we need to offer the execution engine a custom memory manager
      // which tracks the exact sizes of the desired emitted functions.
      // In order to make a custom memory manager, we require enabling
      // LLVM RTTI, since subclassing an LLVM interface would require
      // identical RTTI settings between LLVM and Kudu (see:
      // http://llvm.org/docs/Packaging.html#c-features).
      string opname = printer->getOpcodeName(inst.getOpcode());
      std::transform(opname.begin(), opname.end(), opname.begin(), ::toupper);
      if (opname.find("RET") != string::npos) return i + 1;
    }
    addr += size;
  }

  return max_instr;
}

} // anonymous namespace

void CodeGenerator::GlobalInit() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  llvm::InitializeNativeTargetDisassembler();
  // TODO would be nice to just initialize the TargetMachine here, just once,
  // instead of constantly retrieving it from the codegen classes' expired
  // ModuleBuilders.
}

CodeGenerator::CodeGenerator() {
  static GoogleOnceType once = GOOGLE_ONCE_INIT;
  GoogleOnceInit(&once, &CodeGenerator::GlobalInit);
}

CodeGenerator::~CodeGenerator() {}


Status CodeGenerator::CompileRowProjector(const Schema& base, const Schema& proj,
                                          scoped_refptr<RowProjectorFunctions>* out) {
  RETURN_NOT_OK(CheckCodegenEnabled());

  TargetMachine* tm;
  RETURN_NOT_OK(RowProjectorFunctions::Create(base, proj, out, &tm));

  if (FLAGS_codegen_dump_mc) {
    static const int kInstrMax = 500;
    std::stringstream sstr;
    sstr << "Printing read projection function:\n";
    int instrs = DumpAsm((*out)->read(), *tm, &sstr, kInstrMax);
    sstr << "Printed " << instrs << " instructions.";
    LOG(INFO) << sstr.str();
  }

  return Status::OK();
}

} // namespace codegen
} // namespace kudu
