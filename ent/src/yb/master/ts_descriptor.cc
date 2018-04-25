// Copyright (c) YugaByte, Inc.

#include "yb/master/ts_descriptor.h"
#include "yb/master/master.pb.h"

using std::set;

namespace yb {
namespace master {
namespace enterprise {

Status TSDescriptor::RegisterUnlocked(const NodeInstancePB& instance,
                              const TSRegistrationPB& registration) {
  RETURN_NOT_OK(super::RegisterUnlocked(instance, registration));
  return Status::OK();
}

} // namespace enterprise
} // namespace master
} // namespace yb
