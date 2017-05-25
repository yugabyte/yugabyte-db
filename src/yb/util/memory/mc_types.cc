//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/util/memory/mc_types.h"

namespace yb {

//--------------------------------------------------------------------------------------------------

char *MCStrdup(MemoryContext *memctx, const char *str) {
  if (str == nullptr) {
    return nullptr;
  }

  const size_t bytes = strlen(str) + 1;
  return static_cast<char*>(memcpy(memctx->AllocateBytes(bytes), str, bytes));
}

//--------------------------------------------------------------------------------------------------

MCBase::MCBase(MemoryContext *memctx) {
}

MCBase::~MCBase() {
}

// Delete operator is a NO-OP. The custom allocator (e.g. Arena) will free it when the associated
// memory context is deleted.
void MCBase::operator delete(void *ptr) noexcept {
}

// Operator new with placement allocate an object of any derived classes of MCBase.
void *MCBase::operator new(size_t bytes, Arena *arena) noexcept {
  return arena->AllocateBytesAligned(bytes, sizeof(void*));
}

}  // namespace yb
