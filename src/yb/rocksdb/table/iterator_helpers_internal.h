// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include "yb/rocksdb/status.h"
#include "yb/rocksdb/util/arena.h"

namespace rocksdb {

template <class IteratorType>
IteratorType* DoNewEmptyIterator(const Status& status, Arena* arena) {
  if (arena == nullptr) {
    return new typename IteratorType::Empty(status);
  } else {
    auto mem = arena->AllocateAligned(sizeof(typename IteratorType::Empty));
    return new (mem) typename IteratorType::Empty(status);
  }
}

template <class IteratorType>
IteratorType* NewEmptyIterator(Arena* arena) {
  return DoNewEmptyIterator<IteratorType>(Status::OK(), arena);
}

template <class IteratorType>
IteratorType* NewErrorIterator(const Status& status, Arena* arena) {
  return DoNewEmptyIterator<IteratorType>(status, arena);
}

}  // namespace rocksdb
