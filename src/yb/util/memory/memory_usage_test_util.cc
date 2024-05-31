//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/util/memory/memory_usage_test_util.h"

#include <map>

#include "yb/util/memory/arena.h"
#include "yb/util/size_literals.h"

// Malloc hooks are not suppported in Google's TCMalloc as of Dec 12 2022.
// See issue: https://github.com/google/tcmalloc/issues/44.
#if YB_GPERFTOOLS_TCMALLOC
#include <gperftools/malloc_hook.h>
#define MEMORY_USAGE_SUPPORTED
#endif // YB_GPERFTOOLS_TCMALLOC

#if !defined(MEMORY_USAGE_SUPPORTED)
#include "yb/util/format.h"
#endif

namespace yb {

#if defined(MEMORY_USAGE_SUPPORTED)

size_t heap_requested_bytes = 0;
bool heap_allocation_tracking_enabled = false;

// We use preallocated arena for heap_allocations map to avoid this map memory allocations be
// counted by tcmalloc during memory usage tests.
ThreadSafeArena heap_allocations_map_arena(10_MB, 10_MB);

template<class Key, class Value, class Compare = std::less<Key>>
using MapOnArena =
    std::map<Key, Value, Compare, ThreadSafeArenaAllocator<std::pair<const Key, Value>>>;
MapOnArena<const void*, size_t> heap_allocations(&heap_allocations_map_arena);

void NewHook(const void* ptr, const std::size_t size) {
  if (heap_allocation_tracking_enabled) {
    heap_allocation_tracking_enabled = false;
    heap_requested_bytes += size;
    auto emplaced = heap_allocations.emplace(ptr, size);
    if (!emplaced.second) {
      LOG(FATAL) << "Double allocation at ptr: " << ptr << " size requested: " << size
                 << " previous size: " << emplaced.first->second;
    }
    heap_allocation_tracking_enabled = true;
  }
}

void DeleteHook(const void* ptr) {
  if (heap_allocation_tracking_enabled) {
    heap_allocation_tracking_enabled = false;
    auto it = heap_allocations.find(ptr);
    if (it != heap_allocations.end()) {
      heap_requested_bytes -= it->second;
      heap_allocations.erase(it);
    }
    heap_allocation_tracking_enabled = true;
  }
}

void StartAllocationsTracking() {
  heap_requested_bytes = 0;
  heap_allocations.clear();
  heap_allocation_tracking_enabled = true;
  MallocHook_AddNewHook(&NewHook);
  MallocHook_AddDeleteHook(&DeleteHook);
}

void StopAllocationsTracking() {
  heap_allocation_tracking_enabled = false;
  MallocHook_RemoveNewHook(&NewHook);
  MallocHook_RemoveDeleteHook(&DeleteHook);
}

size_t GetHeapRequestedBytes() {
  return heap_requested_bytes;
}

#else

std::string kNotSupported("$0 is not supported under ASAN/TSAN or without TCMalloc");

void StartAllocationsTracking() {
  LOG(FATAL) << Format(kNotSupported, __FUNCTION__);
}

void StopAllocationsTracking() {
  LOG(FATAL) << Format(kNotSupported, __FUNCTION__);
}

size_t GetHeapRequestedBytes() {
  LOG(FATAL) << Format(kNotSupported, __FUNCTION__);
  return 0;
}

#endif // defined(MEMORY_USAGE_SUPPORTED)

std::string DumpMemoryUsage(const MemoryUsage& memory_usage) {
  std::ostringstream ss;
  ss << "Entities: " << memory_usage.entities_count << std::endl;
  auto dump_memory = [&ss, &memory_usage](const char* label, const size_t bytes) {
    ss << "Memory " << label << ": " << bytes
       << ", per entity: " << bytes / memory_usage.entities_count << std::endl;
  };
  dump_memory("requested", memory_usage.heap_requested_bytes);
  dump_memory("allocated", memory_usage.heap_allocated_bytes);
  dump_memory("tracked", memory_usage.tracked_consumption);
  return ss.str();
}

}  // namespace yb
