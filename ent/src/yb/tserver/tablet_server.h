// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TSERVER_TABLET_SERVER_H
#define ENT_SRC_YB_TSERVER_TABLET_SERVER_H

#include "../../../../src/yb/tserver/tablet_server.h"

namespace yb {
namespace tserver {
namespace enterprise {

class TabletServer : public yb::tserver::TabletServer {
  typedef yb::tserver::TabletServer super;
 public:
  explicit TabletServer(const TabletServerOptions& opts) : super(opts) {}

 protected:
  CHECKED_STATUS RegisterServices() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabletServer);
};

} // namespace enterprise
} // namespace tserver
} // namespace yb
#endif // ENT_SRC_YB_TSERVER_TABLET_SERVER_H
