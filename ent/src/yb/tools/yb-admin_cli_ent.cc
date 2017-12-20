// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_cli.h"

#include <iostream>

namespace yb {
namespace tools {
namespace enterprise {

using std::cerr;
using std::endl;

void ClusterAdminCli::RegisterCommandHandlers(ClusterAdminClientClass* client) {
  super::RegisterCommandHandlers(client);

  // TODO: Add ENTERPRISE-ONLY commands here
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
