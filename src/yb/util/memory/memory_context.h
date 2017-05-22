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
//     MemoryContext::UniPtr mem_ctx;
//
// - To allocate a buffer
//     char *buffer = static_cast<char*>(mem_ctx->Malloc(size_in_bytes));
//
// - Freeing this buffer would be a noop except maybe for debugging.
//     mem_ctx->Free(buffer);
//
// - To allocate a container, one can get the associated allocator by calling GetAllocator.
//     mem_ctx->GetAllocator<ElementType>();
//   The file "yb/util/memory/mc_types.h" defines several containers including MCString that use
//   custom allocator from MemoryContext.
//
// - When "mem_ctx" is destructed, its private allocator would be freed, and all associated
//   allocated memory spaces would be deleted and released back to the system.
//--------------------------------------------------------------------------------------------------
#ifndef YB_UTIL_MEMORY_MEMORY_CONTEXT_H
#define YB_UTIL_MEMORY_MEMORY_CONTEXT_H

#include <stdarg.h>
#include <stdio.h>

#include "yb/util/mem_tracker.h"
#include "yb/util/memory/arena.h"

namespace yb {

class MemoryContext;

//--------------------------------------------------------------------------------------------------
// MC deleter class for shared_ptr and unique_ptr.
class MCDeleter {
 public:
  template<class MCObject>
  void operator()(MCObject *obj) {
    obj->~MCObject();
  }
};

//--------------------------------------------------------------------------------------------------
// Context-control shared_ptr and unique_ptr
template<class MCObject> using MCUniPtr = std::unique_ptr<MCObject, MCDeleter>;
template<class MCObject> using MCSharedPtr = std::shared_ptr<MCObject>;

template<class MCObject>
class MCAllocator : public ArenaAllocator<MCObject, false> {
 public:
  MCAllocator() {}
  MCAllocator(MemoryContext* memory_context); // NOLINT

  template<class U>
  MCAllocator(const MCAllocator<U>& other)
      : ArenaAllocator<MCObject, false>(other),
        memory_context_(other.memory_context()) { }

  MemoryContext* memory_context() const {
    return memory_context_;
  }

  template<class U>
  struct rebind {
    typedef MCAllocator<U> other;
  };

 private:
  MemoryContext* memory_context_;
};

//--------------------------------------------------------------------------------------------------

class MemoryContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<MemoryContext> UniPtr;
  typedef std::unique_ptr<const MemoryContext> UniPtrConst;

  // Constant variable.
  static constexpr size_t kStartBlockSize = 4 * 1024;
  static constexpr size_t kMaxBlockSize = 256 * 1024;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  explicit MemoryContext(std::shared_ptr<MemTracker> mem_tracker = nullptr);

  //------------------------------------------------------------------------------------------------
  // Char* buffer support.

  // Allocate a memory space and save the free operator in the deallocation map.
  void *Malloc(size_t size);

  // Free() is a no-op. This context does not free allocated spaces individually. All allocated
  // spaces will be destroyed when memory context is out of scope.
  void Free(void *ptr) {
  }

  //------------------------------------------------------------------------------------------------
  // Standard STL container support.

  // Get the correct allocator for certain datatype.
  template<class MCObject>
  MCAllocator<MCObject> GetAllocator() {
    return MCAllocator<MCObject>(this);
  }

  //------------------------------------------------------------------------------------------------
  // Shared_ptr support.

  // Allocate shared_ptr object.
  template<class MCObject, typename... TypeArgs>
  MCSharedPtr<MCObject> AllocateShared(TypeArgs&&... args) {
    MCAllocator<MCObject> allocator(this);
    return std::allocate_shared<MCObject>(allocator, std::forward<TypeArgs>(args)...);
  }

  // Convert raw pointer to shared pointer.
  template<class MCObject>
  MCSharedPtr<MCObject> ToShared(MCObject *raw_ptr) {
    MCAllocator<MCObject> allocator(this);
    return MCSharedPtr<MCObject>(raw_ptr, MCDeleter(), allocator);
  }

  //------------------------------------------------------------------------------------------------
  // Allocate an object.
  template<class MCObject, typename... TypeArgs>
  MCObject *NewObject(TypeArgs&&... args) {
    return manager_.NewObject<MCObject>(std::forward<TypeArgs>(args)...);
  }

  // Reset the memory context to free the previously allocated memory.
  void Reset();

 private:
  template<class MCObject>
  friend class MCAllocator;

  //------------------------------------------------------------------------------------------------
  std::shared_ptr<MemoryTrackingBufferAllocator> tracking_allocator_;
  // Allocate and deallocate memory from heap.
  ArenaBase<false> manager_;
};

template<class MCObject>
MCAllocator<MCObject>::MCAllocator(MemoryContext* memory_context)
    : ArenaAllocator<MCObject, false>(&memory_context->manager_),
      memory_context_(memory_context) {
}

}  // namespace yb

#endif // YB_UTIL_MEMORY_MEMORY_CONTEXT_H
