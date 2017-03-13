//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Implementation for MemoryContext class.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/util/memory_context.h"
#include "yb/util/logging.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using std::unordered_map;

//--------------------------------------------------------------------------------------------------
// Default MemoryContext
//--------------------------------------------------------------------------------------------------
MemoryContext::MemoryContext(shared_ptr<MemTracker> mem_tracker)
    : manager_(mem_tracker != nullptr ?
               static_cast<ArenaBase<false>*>(
                   new MemoryTrackingArena(kStartBlockSize, kMaxBlockSize,
                                           std::make_shared<MemoryTrackingBufferAllocator>(
                                               HeapBufferAllocator::Get(), mem_tracker))) :
               static_cast<ArenaBase<false>*>(new Arena(kStartBlockSize, kMaxBlockSize))),
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
  return manager_->AllocateBytes(size);
}

void MemoryContext::Reset() {
  // Clear allocators allocated from Arena before resetting Arena to release allocated memory.
  allocator_map_.clear();
  manager_->Reset();
}

//--------------------------------------------------------------------------------------------------
// Standard MemoryContext - Not yet immplemented.
//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
