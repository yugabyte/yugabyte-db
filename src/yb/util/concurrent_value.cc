//
// Copyright (c) YugaByte, Inc.
//

#include "yb/util/concurrent_value.h"

namespace yb {
namespace internal {

thread_local std::unique_ptr<URCUThreadData, URCU::CleanupThreadData> URCU::data_;

} // namespace internal
} // namespace yb
