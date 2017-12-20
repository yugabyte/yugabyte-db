// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H
#define ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H

namespace yb {
namespace tools {
namespace enterprise {

class ClusterAdminClient;

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#include "../../../../src/yb/tools/yb-admin_cli.h"

namespace yb {
namespace tools {
namespace enterprise {

class ClusterAdminClient;

class ClusterAdminCli : public yb::tools::ClusterAdminCli {
  typedef yb::tools::ClusterAdminCli super;

 private:
  void RegisterCommandHandlers(ClusterAdminClientClass* client) override;
};

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#endif // ENT_SRC_YB_TOOLS_YB_ADMIN_CLI_H
