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

#include "yb/master/system_tables_handler.h"

namespace yb {
namespace master {

using std::string;

SystemTablesHandler::SystemTablesHandler()
    : master_supported_system_tables_(
    {
        std::make_pair(string(kSystemNamespaceId),
                       string(kSystemPeersTableName)),
        std::make_pair(string(kSystemNamespaceId),
                       string(kSystemLocalTableName)),
        std::make_pair(string(kSystemNamespaceId),
                       string(kSystemPartitionsTableName)),
        std::make_pair(string(kSystemNamespaceId),
                       string(kSystemSizeEstimatesTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaAggregatesTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaColumnsTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaFunctionsTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaIndexesTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaTriggersTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaTypesTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaViewsTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaKeyspacesTableName)),
        std::make_pair(string(kSystemSchemaNamespaceId),
                       string(kSystemSchemaTablesTableName)),
        std::make_pair(string(kSystemAuthNamespaceId),
                       string(kSystemAuthRolesTableName)),
        std::make_pair(string(kSystemAuthNamespaceId),
                       string(kSystemAuthRolePermissionsTableName)),
        std::make_pair(string(kSystemAuthNamespaceId),
                       string(kSystemAuthResourceRolePermissionsIndexTableName)),

    }) {
}

CHECKED_STATUS SystemTablesHandler::RetrieveTabletByTableName(
    const TableName& table_name, std::shared_ptr<tablet::AbstractTablet>* tablet) {
  for (const auto& system_tablet : system_tablets_) {
    if (system_tablet->GetTableName() == table_name) {
      *tablet = system_tablet;
      return Status::OK();
    }
  }
  return STATUS_SUBSTITUTE(InvalidArgument, "Couldn't find system tablet for table $0", table_name);
}

CHECKED_STATUS SystemTablesHandler::AddTablet(std::shared_ptr<SystemTablet> tablet) {
  system_tablets_.push_back(tablet);
  return Status::OK();
}

}  // namespace master
}  // namespace yb
