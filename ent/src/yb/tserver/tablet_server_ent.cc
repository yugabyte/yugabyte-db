// Copyright (c) YugaByte, Inc.

#include "yb/tserver/tablet_server.h"
#include "yb/util/flag_tags.h"

namespace yb {
namespace tserver {
namespace enterprise {

using yb::rpc::ServiceIf;

Status TabletServer::RegisterServices() {
  return super::RegisterServices();
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
