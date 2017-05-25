//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_UTIL_MEMORY_ARENA_FWD_H
#define YB_UTIL_MEMORY_ARENA_FWD_H

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

} // namespace yb

#endif // YB_UTIL_MEMORY_ARENA_FWD_H
