//
// Copyright (c) YugaByte, Inc.
//

#include "yb/util/concurrent_value.h"

namespace yb {
namespace internal {

#if defined(__APPLE__) && __clang_major__ < 8
boost::thread_specific_ptr<URCUThreadData> URCU::data_{&URCU::CleanupThreadData};
#else
thread_local std::unique_ptr<URCUThreadData, URCU::CleanupThreadData> URCU::data_;
#endif

} // namespace internal
} // namespace yb
