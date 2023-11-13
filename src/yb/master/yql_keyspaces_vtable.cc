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

#include "yb/master/yql_keyspaces_vtable.h"

#include <stdint.h>

#include "yb/util/logging.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"

#include "yb/util/status_log.h"
#include "yb/util/uuid.h"

namespace yb {
namespace master {

YQLKeyspacesVTable::YQLKeyspacesVTable(const TableName& table_name,
                                       const NamespaceName& namespace_name,
                                       Master * const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

Result<std::shared_ptr<QLRowBlock>> YQLKeyspacesVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  auto vtable = std::make_shared<QLRowBlock>(schema());
  std::vector<scoped_refptr<NamespaceInfo> > namespaces;
  catalog_manager().GetAllNamespaces(&namespaces, true);
  for (scoped_refptr<NamespaceInfo> ns : namespaces) {
    // Skip non-YQL namespace.
    if (!IsYcqlNamespace(*ns)) {
      continue;
    }

    QLRow& row = vtable->Extend();
    RETURN_NOT_OK(SetColumnValue(kKeyspaceName, ns->name(), &row));
    RETURN_NOT_OK(SetColumnValue(kDurableWrites, true, &row));

    auto repl_factor = VERIFY_RESULT(catalog_manager().GetReplicationFactor(ns->name()));
    RETURN_NOT_OK(SetColumnValue(kReplication, util::GetReplicationValue(repl_factor), &row));
  }

  return vtable;
}

Schema YQLKeyspacesVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kDurableWrites, QLType::Create(DataType::BOOL)));
  // TODO: replication needs to be a frozen map.
  CHECK_OK(builder.AddColumn(kReplication,
                             QLType::CreateTypeMap(DataType::STRING, DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
