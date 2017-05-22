//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Implementation for MemoryContext class.
//--------------------------------------------------------------------------------------------------

#include "yb/util/memory/memory_context.h"
#include "yb/util/logging.h"

namespace yb {

namespace {

std::shared_ptr<MemoryTrackingBufferAllocator> CreateTrackingAllocator(
  std::shared_ptr<MemTracker> mem_tracker) {
  if (mem_tracker) {
    return std::make_shared<MemoryTrackingBufferAllocator>(HeapBufferAllocator::Get(),
                                                           std::move(mem_tracker));
  } else {
    return nullptr;
  }
}

BufferAllocator* SelectAllocator(
    const std::shared_ptr<MemoryTrackingBufferAllocator>& tracking_allocator) {
  if (tracking_allocator) {
    return tracking_allocator.get();
  } else {
    return HeapBufferAllocator::Get();
  }
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Default MemoryContext
//--------------------------------------------------------------------------------------------------
MemoryContext::MemoryContext(std::shared_ptr<MemTracker> mem_tracker)
    : tracking_allocator_(CreateTrackingAllocator(std::move(mem_tracker))),
      manager_(SelectAllocator(tracking_allocator_), kStartBlockSize, kMaxBlockSize) {
}

void *MemoryContext::Malloc(size_t size) {
  return manager_.AllocateBytes(size);
}

void MemoryContext::Reset() {
  manager_.Reset();
}

//--------------------------------------------------------------------------------------------------
// Standard MemoryContext - Not yet implemented.
//--------------------------------------------------------------------------------------------------

}  // namespace yb
