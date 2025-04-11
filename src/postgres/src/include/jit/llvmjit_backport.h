/*
 * A small header than can be included by backported LLVM code or PostgreSQL
 * code, to control conditional compilation.
 */
#ifndef LLVMJIT_BACKPORT_H
#define LLVMJIT_BACKPORT_H

#include <llvm/Config/llvm-config.h>

/*
 * LLVM's RuntimeDyld can produce code that crashes on larger memory ARM
 * systems, because llvm::SectionMemoryManager allocates multiple pieces of
 * memory that can be placed too far apart for the generated code.  See
 * src/backend/jit/llvm/SectionMemoryManager.cpp for the patched replacement
 * class llvm::backport::SectionMemoryManager that we use as a workaround.
 * This header controls whether we use it.
 *
 * We have adjusted it to compile against a range of LLVM versions, but not
 * further back than 12 for now.
 */
#if defined(__aarch64__) && LLVM_VERSION_MAJOR > 11
#define USE_LLVM_BACKPORT_SECTION_MEMORY_MANAGER
#endif

#endif
