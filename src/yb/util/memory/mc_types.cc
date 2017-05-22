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

  const size_t bytes = strlen(str);
  char *const sdup = static_cast<char *>(memctx->Malloc(bytes + 1));
  memcpy(sdup, str, bytes);
  sdup[bytes] = '\0';
  return sdup;
}

//--------------------------------------------------------------------------------------------------

MCBase::MCBase(MemoryContext *memctx) {
}

MCBase::~MCBase() {
}

// Delete operator is a NO-OP. The custom allocator (e.g. Arena) will free it when the associated
// memory context is deleted.
void MCBase::operator delete(void *ptr) {
}

// Delete[] operator is a NO-OP. The custom allocator (Arena) will free it when the associated
// memory context is deleted.
void MCBase::operator delete[](void* ptr) {
}

// Operator new with placement allocate an object of any derived classes of MCBase.
void *MCBase::operator new(size_t bytes, MemoryContext *mem_ctx) throw(std::bad_alloc) {
  // Allocate memory of size 'bytes'.
  void *ptr = mem_ctx->Malloc(bytes);
  return ptr;
}

// Allocate an array of objects of any derived classes of MCBase. Do not use this feature
// except as it is still experimental.
void *MCBase::operator new[](size_t bytes,
                             MemoryContext *mem_ctx) throw(std::bad_alloc) {
  // Allocate the array and return the pointer to the allocated space.
  // IMPORTANT NOTE: This pointer is not necessarily the pointer to the array.
  void *ptr = mem_ctx->Malloc(bytes);
  return ptr;
}

}  // namespace yb
