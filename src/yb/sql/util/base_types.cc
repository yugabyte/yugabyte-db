//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/base_types.h"

namespace yb {
namespace sql {

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

MCString::MCString(MemoryContext *mem_ctx)
    : MCStringBase(mem_ctx->GetAllocator<char>()) {
}

MCString::MCString(MemoryContext *mem_ctx, const char *str)
    : MCStringBase(str, mem_ctx->GetAllocator<char>()) {
}

MCString::MCString(MemoryContext *mem_ctx, const char *str, size_t len)
    : MCStringBase(str, len, mem_ctx->GetAllocator<char>()) {
}

MCString::MCString(MemoryContext *mem_ctx, size_t len, char c)
    : MCStringBase(len, c, mem_ctx->GetAllocator<char>()) {
}

MCString::~MCString() {
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

}  // namespace sql
}  // namespace yb
