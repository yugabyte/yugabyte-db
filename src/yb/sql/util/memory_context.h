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
//   The file "yb/sql/util/base_types.h" defines several containers including MCString that use
//   custom allocator from MemoryContext.
//
// - When "mem_ctx" is destructed, its private allocator would be freed, and all associated
//   allocated memory spaces would be deleted and released back to the system.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_UTIL_MEMORY_CONTEXT_H_
#define YB_SQL_UTIL_MEMORY_CONTEXT_H_

#include <stdarg.h>
#include <stdio.h>
#include <typeindex>

#include <type_traits>
#include <unordered_map>

#include "yb/util/mem_tracker.h"
#include "yb/util/memory/arena.h"

namespace yb {
namespace sql {

class MemoryContext;

//--------------------------------------------------------------------------------------------------
// MC allocator class.
template<class MCObject>
using MCAllocatorBase = ArenaAllocator<MCObject, false>;

template<class MCObject>
class MCAllocator : public MCAllocatorBase<MCObject> {
 public:
  // IMPORTANT NOTE:
  // Although C++ Standard does not require allocator to have default constructor, some linux C++
  // compilers need default constructor to support String copy constructors that take default
  // allocator as an argument.
  //
  // RESOLUTION:
  // - Define our own MC type - such as MCString - whose constructor always take allocators as
  //   argument.
  // - We provide default constructor for basic_string<char> because this type uses default
  //   constructor to create a dummy instance that is used for optimization but not for allocation.
  //   Search "_GLIBCXX_FULLY_DYNAMIC_STRING" for more info.
  // - The default constructor is marked as deprecated so that we don't mistakenly use it.
  // - The dummy object created by the default constructor should never be used. If it's used, the
  //   system will crash immediately due to nullptr memory manager. Our tests will catch it.
  MCAllocator() __attribute__((deprecated)) : MCAllocatorBase<MCObject>() {
  }
  MCAllocator(MemoryContext *memory_context, ArenaBase<false> *a) : MCAllocatorBase<MCObject>(a) {
  }

  virtual ~MCAllocator() {
  }
};

//--------------------------------------------------------------------------------------------------
// MC deleter class for shared_ptr and unique_ptr.
template<class MCObject = void>
class MCDeleter {
 public:
  // Delete is a no-op because we use allocator to allocate and deallocate.
  void operator()(MCObject *obj) {
  }
};

//--------------------------------------------------------------------------------------------------
// Context-control shared_ptr and unique_ptr
template<class MCObject> using MCUniPtr = std::unique_ptr<MCObject, MCDeleter<>>;
template<class MCObject> using MCSharedPtr = std::shared_ptr<MCObject>;

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
  virtual ~MemoryContext();

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
  const MCAllocator<MCObject>& GetAllocator() {
    const std::type_index type_idx = std::type_index(typeid(MCObject));
    std::unordered_map<std::type_index, AllocatorBase*>::iterator iter =
      allocator_map_.find(type_idx);
    if (iter != allocator_map_.end()) {
      return *static_cast<MCAllocator<MCObject> *>(iter->second);
    }

    // Use Arena to malloc the allocators for STD containers.
    MCAllocator<MCObject> *allocator = manager_->NewObject<MCAllocator<MCObject>>(this,
                                                                                  manager_.get());
    allocator_map_[type_idx] = allocator;
    return *allocator;
  }
  const MCDeleter<>& GetDeleter() {
    return deleter_;
  }

  //------------------------------------------------------------------------------------------------
  // Shared_ptr support.

  // Allocate shared_ptr object.
  template<class MCObject, typename... TypeArgs>
  MCSharedPtr<MCObject> AllocateShared(TypeArgs&&... args) {
    const MCAllocator<MCObject> alloc = GetAllocator<MCObject>();
    return std::allocate_shared<MCObject>(alloc, std::forward<TypeArgs>(args)...);
  }

  // Convert raw pointer to shared pointer.
  template<class MCObject>
  MCSharedPtr<MCObject> ToShared(MCObject *raw_ptr) {
    const MCAllocator<MCObject> alloc = GetAllocator<MCObject>();
    return MCSharedPtr<MCObject>(raw_ptr, deleter_, alloc);
  }

  //------------------------------------------------------------------------------------------------
  // Allocate an object.
  template<class MCObject, typename... TypeArgs>
  MCObject *NewObject(TypeArgs&&... args) {
    return manager_->NewObject<MCObject>(std::forward<TypeArgs>(args)...);
  }

  // Reset the memory context to free the previously allocated memory.
  void Reset();

 private:
  //------------------------------------------------------------------------------------------------
  // Allocate and deallocate memory from heap.
  std::unique_ptr<ArenaBase<false>> manager_;

  // Consists of all allocators for all types that are allocated by this context.
  std::unordered_map<std::type_index, AllocatorBase*> allocator_map_;

  // Deleter for all objects within this context.
  MCDeleter<> deleter_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_MEMORY_CONTEXT_H_
