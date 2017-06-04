// Copyright (c) YugaByte, Inc.

#include "yb/master/master_tserver.h"

namespace yb {
namespace master {

MasterTabletServer::MasterTabletServer(scoped_refptr<MetricEntity> metric_entity)
    : metric_entity_(metric_entity) {
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
