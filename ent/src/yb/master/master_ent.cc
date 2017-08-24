// Copyright (c) YugaByte, Inc.

#include "yb/master/master.h"
#include "yb/util/flag_tags.h"

namespace yb {
namespace master {
namespace enterprise {

using yb::rpc::ServiceIf;

Status Master::RegisterServices() {
  return super::RegisterServices();
}

} // namespace enterprise
} // namespace master
} // namespace yb
