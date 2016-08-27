//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// MemoryContext
// - This class is not thread safe.
// - This is to allocate memory spaces that have the same lifetime using one allocator such that we
//   can delete all of them together by freeing the allocator pool.
//
// Examples:
// - Suppose we have the following memory context.
//     MemoryContext::Ptr mem_ctx;
//
// - To allocate a buffer
//     char *buffer = static_cast<char*>(mem_ctx->Malloc(size_in_bytes));
//
// - Freeing this buffer would be a noop except maybe for debugging.
//     mem_ctx->Free(buffer);
//
// - To allocate a shared_pointer.
//     AClassName::Ptr ptr = mem_ctx->MakeShared<AClassName>("Constructor arguments", 1, 2, 3);
//
// - Freeing shared pointer. This is a must. Otherwise, the destructor will not be called.
//     ptr = nullptr;
//
// - When "mem_ctx" is destructed, its private allocator would be freed, and all associated
//   allocated memory spaces would be deleted and released back to the system.
//
// NOTE: We haven't implemented this yet. The current code implementation simply forwards all calls
// to std library without using allocator.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_UTIL_MEMORY_CONTEXT_H_
#define YB_SQL_UTIL_MEMORY_CONTEXT_H_

#include <stdarg.h>
#include <stdio.h>

#include <memory>
#include <unordered_map>

namespace yb {
namespace sql {

class MemoryContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<MemoryContext> SharedPtr;
  typedef std::shared_ptr<const MemoryContext> SharedPtrConst;

  typedef std::unique_ptr<MemoryContext> UniPtr;
  typedef std::unique_ptr<const MemoryContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  MemoryContext();

  ~MemoryContext();

  // Allocate a memory space and save the free operator in the deallocation map.
  void *Malloc(size_t size);

  // Free() is a no-op. This context does not free allocated spaces individually. All allocated
  // spaces will be destroyed when memory context is out of scope.
  void Free(void *ptr) {
  }

  // Duplicate string.
  char *Strdup(const char *str);

 private:
  //------------------------------------------------------------------------------------------------
  // Private types.
  typedef std::function<void()> DeallocFuncType;

  // Free the allocated space referenced by the given "ptr".
  void Deallocate(void *ptr);

  //------------------------------------------------------------------------------------------------
  // Consists of all std::function() for all deallocations.
  std::unordered_map<const void *, DeallocFuncType> deallocate_map_;
};

}  // namespace sql.
}  // namespace yb.

#endif  // YB_SQL_UTIL_MEMORY_CONTEXT_H_
