// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <string>
#include <vector>

#include "yb/master/master_fwd.h"
#include "yb/util/result.h"
#include "yb/util/status_callback.h"
#include "yb/common/entity_ids_types.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/util/enums.h"
#include "yb/util/status.h"

namespace yb {
namespace master {
class CatalogManager;
class CatalogManagerIf;
class SysUniverseReplicationEntryPB;
}  // namespace master
}  // namespace yb

namespace yb::master {

// Should the table be automatically added to xCluster replication?
bool IsTableEligibleForXClusterReplication(
    const master::TableInfo& table, bool is_automatic_ddl_mode = false);

// Get the table name along with the YSQL schema name if this is a YSQL table.
std::string GetFullTableName(const TableInfo& table_info);

// A wrapper over TableInfo that allows us to use an alias TableId. This is useful for
// sequences_data table for which we use a special TableId in xCluster. Check
// GetSequencesDataAliasForNamespace.
struct TableDesignator {
  // Non-sequences_data table constructor.
  explicit TableDesignator(TableInfoPtr table_info);

  static TableDesignator CreateSequenceTableDesignator(
      TableInfoPtr sequence_table_info, const NamespaceId& namespace_id);

  const TableId id;
  const TableInfoPtr table_info;
  TableName name() const;
  PgSchemaName pgschema_name() const;

  std::string ToString() const;

 private:
  // Constructor for sequences_data table.
  TableDesignator(TableInfoPtr sequence_table_info, const NamespaceId& namespace_id);
};

Result<std::vector<TableDesignator>> GetTablesEligibleForXClusterReplication(
    const CatalogManager& catalog_manager, const NamespaceId& namespace_id,
    bool automatic_ddl_mode);

bool IsDbScoped(const SysUniverseReplicationEntryPB& replication_info);

bool IsAutomaticDdlMode(const SysUniverseReplicationEntryPB& replication_info);

YB_DEFINE_ENUM(XClusterDDLReplicationRole, (kSource)(kTarget));

Status SetupDDLReplicationExtension(
    CatalogManagerIf& catalog_manager, const NamespaceId& namespace_id,
    XClusterDDLReplicationRole role, StdStatusCallback callback);

Status DropDDLReplicationExtensionIfExists(
    CatalogManagerIf& catalog_manager, const NamespaceId& namespace_id, StdStatusCallback callback);

}  // namespace yb::master
