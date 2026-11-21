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

#include <functional>
#include <memory>

#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"
#include "yb/common/transaction.h"

#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace master {
/*
 * Currently the metadata for YSQL Tables is stored in both the PG catalog and DocDB schema.
 * This class helps maintain consistency between the two schemas. When a DDL transaction fails,
 * since the PG catalog is modified using the DocDB transactions framework, the changes to the
 * PG catalog are automatically rolled-back. This tasks in this file help perform similar rollback
 * for the DocDB schema upon transaction failure.
 * When a DDL transaction modifies the DocDB schema, the transaction metadata, and the current
 * schema of the table is stored in the SysTablesEntryPB. At this point, future DDL operations on
 * this table are blocked until verification is complete. A poller is scheduled to monitor whether
 * the transaction is complete. Once the transaction is detected to be complete, it compares the PG
 * catalog and the DocDB schema and finds whether the transaction is a success or a failure.
 * Based on whether the transaction was a success or failure, the master will effect
 * rollback or roll-forward of the DocDB schema.
 *
 * Note that the above protocol introduces eventual consistency between the two types of metadata.
 * However this does not affect correctness because of the following two properties:
 * 1) When inconsistent, the DocDB schema always has more columns/constraints than PG schema.
 * 2) Clients always use the PG schema (which is guaranteed to not return uncommitted state)
 * to prepare their read/write requests.
 *
 * These two properties ensure that we can't have orphaned data or failed integrity checks
 * or use DDL entities created by uncommitted transactions.
 */

// The checks DDL verification makes, for the Rust master's host: whether the PG catalog has a DDL
// transaction's change to `table` (true), has it undone (false), or cannot tell (nullopt, the
// change did not alter the DocDB schema).
Result<std::optional<bool>> CheckYsqlDdlTableSchema(
    SysCatalogTable& sys_catalog, const scoped_refptr<TableInfo>& table);
// Whether the PG catalog table `pg_table_id` has a row with oid `entry_oid`.
Result<bool> YsqlCatalogEntryExists(
    SysCatalogTable& sys_catalog, const TableId& pg_table_id, PgOid entry_oid);
// Whether the table `table_id` with row `table` exists in the PG catalog:
// pg_class still has it (with its relfilenode, if rewritten), or pg_yb_tablegroup its tablegroup.
Result<bool> YsqlTableExists(
    SysCatalogTable& sys_catalog, const TableId& table_id, const SysTablesEntryPB& table);

}  // namespace master
}  // namespace yb
