// Copyright (c) YugaByte, Inc.

#include "yb/master/catalog_manager.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master.h"

namespace yb {
namespace master {

// Available overloaded handlers of different types:

CatalogManager* MasterServiceBase::handler(CatalogManager*) {
  return server_->catalog_manager();
}

} // namespace master
} // namespace yb
