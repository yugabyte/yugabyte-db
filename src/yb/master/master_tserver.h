// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_TSERVER_H
#define YB_MASTER_MASTER_TSERVER_H

#include "yb/tserver/tablet_server_interface.h"
#include "yb/util/metrics.h"

namespace yb {
namespace master {

// Master's version of a TabletServer which is required to support virtual tables in the Master.
// This isn't really an actual server and is just a nice way of overriding the default tablet
// server interface to support virtual tables.
class MasterTabletServer : public tserver::TabletServerIf {
 public:
  MasterTabletServer();
  tserver::TSTabletManager* tablet_manager() override;

  tserver::ScannerManager* scanner_manager() override;

  server::Clock* Clock() override;
  const scoped_refptr<MetricEntity>& MetricEnt() const override;

 private:
  std::unique_ptr<MetricRegistry> metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
};

} // namespace master
} // namespace yb
#endif // YB_MASTER_MASTER_TSERVER_H
