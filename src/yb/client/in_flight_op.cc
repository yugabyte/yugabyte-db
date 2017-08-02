// Copyright (c) YugaByte, Inc.

#include <string>
#include "yb/client/in_flight_op.h"
#include "yb/client/meta_cache.h"
#include "yb/client/yb_op.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace client {
namespace internal {

std::string InFlightOp::ToString() const {
  return strings::Substitute("op[state=$0, yb_op=$1]",
                             internal::ToString(state),
                             yb_op->ToString());
}

} // namespace internal
} // namespace client
} // namespace yb
