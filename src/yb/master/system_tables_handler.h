// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

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
