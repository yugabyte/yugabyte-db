// Copyright (c) YugaByte, Inc.

#include "yb/master/ts_descriptor.h"
#include "yb/master/master.pb.h"

using std::set;

namespace yb {
namespace master {
namespace enterprise {

Status TSDescriptor::Register(const NodeInstancePB& instance,
                              const TSRegistrationPB& registration) {
  RETURN_NOT_OK(super::Register(instance, registration));
  if (registration.common().has_placement_uuid()) {
    placement_uuid_ = registration.common().placement_uuid();
  } else {
    placement_uuid_ = "";
  }
  return Status::OK();
}

} // namespace enterprise
} // namespace master
} // namespace yb
