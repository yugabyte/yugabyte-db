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

#include "yb/util/memory/mc_types.h"

#include "yb/util/memory/arena.h"

namespace yb {

//--------------------------------------------------------------------------------------------------

char *MCStrdup(MemoryContext *memctx, const char *str) {
  if (str == nullptr) {
    return nullptr;
  }

  const size_t bytes = strlen(str) + 1;
  return static_cast<char*>(memcpy(memctx->AllocateBytes(bytes), str, bytes));
}

//--------------------------------------------------------------------------------------------------

MCBase::MCBase(MemoryContext *memctx) {
}

MCBase::~MCBase() {
}

// Delete operator is a NO-OP. The custom allocator (e.g. Arena) will free it when the associated
// memory context is deleted.
void MCBase::operator delete(void *ptr) noexcept {
}

// Operator new with placement allocate an object of any derived classes of MCBase.
void *MCBase::operator new(size_t bytes, Arena *arena) noexcept {
  return arena->AllocateBytesAligned(bytes, sizeof(void*));
}

void *MCBase::operator new(size_t bytes, void* ptr) noexcept {
  return ptr;
}

}  // namespace yb
