//
// Copyright (c) YugabyteDB, Inc.
//

#include "yb/util/concurrent_value.h"

namespace yb {
namespace internal {

thread_local std::unique_ptr<URCUThreadData, URCU::CleanupThreadData> URCU::data_;

// Defined here, out of line, so the registry is a single instance for the whole process rather than
// one per shared library.
ThreadList<URCUThreadData>& URCU::GetThreadList() {
  static ThreadList<URCUThreadData> result;
  return result;
}

} // namespace internal
} // namespace yb
