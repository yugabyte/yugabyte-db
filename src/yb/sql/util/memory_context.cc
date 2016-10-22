//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Implementation for MemoryContext class.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/memory_context.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::unordered_map;

//--------------------------------------------------------------------------------------------------
// Default MemoryContext
//--------------------------------------------------------------------------------------------------
MemoryContext::MemoryContext()
    : manager_(kStartBlockSize, kMaxBlockSize),
      deleter_(MCDeleter<>()) {
}

MemoryContext::~MemoryContext() {
  for (auto allocator_entry : allocator_map_) {
    // In GetAllocator() function, MemoryContext called Arena::NewObject() to allocate spaces for
    // MCAllocator objects, so only the destructors should be called. Delete expression should not
    // be used here as it also calls free().
    AllocatorBase *allocator = allocator_entry.second;
    allocator->~AllocatorBase();
  }
}

void *MemoryContext::Malloc(size_t size) {
  return manager_.AllocateBytes(size);
}

//--------------------------------------------------------------------------------------------------
// Standard MemoryContext - Not yet immplemented.
//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
