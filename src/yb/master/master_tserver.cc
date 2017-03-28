// Copyright (c) YugaByte, Inc.

#include "yb/master/master_tserver.h"

namespace yb {
namespace master {

MasterTabletServer::MasterTabletServer()
    : metric_registry_(new MetricRegistry()),
      metric_entity_(METRIC_ENTITY_server.Instantiate(metric_registry_.get(),
                                                      "yb.master.tabletserver")) {
}

tserver::TSTabletManager* MasterTabletServer::tablet_manager() {
  return nullptr;
}

tserver::ScannerManager* MasterTabletServer::scanner_manager() {
  return nullptr;
}

server::Clock* MasterTabletServer::Clock() {
  return nullptr;
}

const scoped_refptr<MetricEntity>& MasterTabletServer::MetricEnt() const {
  return metric_entity_;
}

} // namespace master
} // namespace yb
