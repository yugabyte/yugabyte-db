//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Primary include file for YugaByte SQL engine.
// This should be the first file included by YugaByte backend modules.
//--------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <iostream>
#include <cstddef>
#include <cstring>

#include "yb/sql/util/memory_context.h"

namespace yb {
namespace sql {

using std::unordered_map;

//--------------------------------------------------------------------------------------------------
// Default MemoryContext
//--------------------------------------------------------------------------------------------------
MemoryContext::MemoryContext() {
}

MemoryContext::~MemoryContext() {
  for (auto deallocate_iter : deallocate_map_) {
    deallocate_iter.second();
  }
}

void *MemoryContext::Malloc(size_t size) {
  void *space = malloc(size);

  // Add allocated spaces into deallocate map. The entries should be removed when the associated
  // memory space is deallocated.
  DeallocFuncType func = std::bind(&MemoryContext::Deallocate, this, space);
  deallocate_map_[space] = func;
  return space;
}

void MemoryContext::Deallocate(void *ptr) {
  free(ptr);
}

//--------------------------------------------------------------------------------------------------

char *MemoryContext::Strdup(const char *str) {
  if (str == nullptr) {
    return nullptr;
  }

  const size_t bytes = strlen(str);
  char *const sdup = static_cast<char *>(Malloc(bytes + 1));
  memcpy(sdup, str, bytes);
  sdup[bytes] = '\0';
  return sdup;
}

//--------------------------------------------------------------------------------------------------
// Standard MemoryContext - Not yet immplemented.
//--------------------------------------------------------------------------------------------------

}  // namespace sql.
}  // namespace yb.
