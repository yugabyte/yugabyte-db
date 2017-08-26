// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
#define ENT_SRC_YB_MASTER_CATALOG_MANAGER_H

#include "../../../../src/yb/master/catalog_manager.h"

namespace yb {
namespace master {
namespace enterprise {

class CatalogManager : public yb::master::CatalogManager {
  typedef yb::master::CatalogManager super;
 public:
  explicit CatalogManager(yb::master::Master* master) : super(master) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(CatalogManager);
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CATALOG_MANAGER_H
