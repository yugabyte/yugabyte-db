// Copyright (c) YugaByte, Inc.

#ifndef YB_TSERVER_TABLET_SERVER_INTERFACE_H
#define YB_TSERVER_TABLET_SERVER_INTERFACE_H

#include "yb/server/clock.h"
#include "yb/tserver/scanners.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/metrics.h"

namespace yb {
namespace tserver {

class TabletServerIf {
 public:

  virtual ~TabletServerIf() {}

  virtual TSTabletManager* tablet_manager() = 0;

  virtual ScannerManager* scanner_manager() = 0;

  virtual server::Clock* Clock() = 0;
  virtual const scoped_refptr<MetricEntity>& MetricEnt() const = 0;
};

} // namespace tserver
} // namespace yb

#endif // YB_TSERVER_TABLET_SERVER_INTERFACE_H
