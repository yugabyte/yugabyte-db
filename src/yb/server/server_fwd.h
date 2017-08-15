//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_SERVER_SERVER_FWD_H
#define YB_SERVER_SERVER_FWD_H

#include "yb/gutil/ref_counted.h"

namespace yb {
namespace server {

class Clock;
typedef scoped_refptr<Clock> ClockPtr;

} // namespace server
} // namespace yb

#endif // YB_SERVER_SERVER_FWD_H
