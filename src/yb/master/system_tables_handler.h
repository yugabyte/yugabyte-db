// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_SYSTEM_TABLES_HANDLER_H
#define YB_MASTER_SYSTEM_TABLES_HANDLER_H

#include "yb/master/system_tablet.h"

namespace yb {
namespace master {

// Responsible for keeping track of all of our virtual system tables.
class SystemTablesHandler {
 public:
  SystemTablesHandler();
  CHECKED_STATUS RetrieveTabletByTableName(const TableName& table_name,
                                           std::shared_ptr<tablet::AbstractTablet>* tablet);
  CHECKED_STATUS AddTablet(std::shared_ptr<SystemTablet> tablet);

  const SystemTableSet& supported_system_tables() const { return master_supported_system_tables_; }
 private:
  std::vector<std::shared_ptr<SystemTablet>> system_tablets_;

  // The set of system tables supported by the master. No locks are needed for this set since its
  // created once during initialization and after that all operations are read operations.
  const SystemTableSet master_supported_system_tables_;
};

}  // namespace master
}  // namespace yb

#endif // YB_MASTER_SYSTEM_TABLES_HANDLER_H
