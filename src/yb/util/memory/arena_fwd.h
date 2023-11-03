//
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
//

#pragma once

#include <vector>

namespace yb {

namespace internal {

template<class Traits>
class ArenaBase;

template<class T, class Traits>
class ArenaAllocatorBase;

class ArenaObjectDeleter;

struct ArenaTraits;
struct ThreadSafeArenaTraits;

} // namespace internal

typedef internal::ArenaBase<internal::ArenaTraits> Arena;
typedef internal::ArenaBase<internal::ThreadSafeArenaTraits> ThreadSafeArena;
using internal::ArenaObjectDeleter;
template<class T>
using ArenaAllocator = internal::ArenaAllocatorBase<T, internal::ArenaTraits>;
template<class T>
using ThreadSafeArenaAllocator = internal::ArenaAllocatorBase<T, internal::ThreadSafeArenaTraits>;

template <class Entry>
class ArenaList;
template <class Object>
using ArenaVector = std::vector<Object, ThreadSafeArenaAllocator<Object>>;

} // namespace yb
