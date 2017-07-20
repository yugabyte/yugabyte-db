// Copyright (c) YugaByte, Inc.

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
