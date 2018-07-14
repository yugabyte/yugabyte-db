/*--------------------------------------------------------------------------------------------------
 *
 * ybccmds.c
 *        Commands for creating and altering table structures and settings
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http: *www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/backend/commands/ybccmds.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "commands/ybccmds.h"
#include "commands/ybctype.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

void YBCCreateTable(CreateStmt *stmt) {
  YBCPgStatement handle;
  ListCell *listptr;
  int col_order;

  YBCStatus s = YBCPgAllocCreateTable(ybc_pg_session,
                                      stmt->relation->catalogname,
                                      stmt->relation->schemaname,
                                      stmt->relation->relname,
                                      false,
                                      &handle);

  // First process the range column.
  col_order = 0;
  foreach(listptr, stmt->tableElts) {
    ColumnDef *colDef = lfirst(listptr);
    YBCPgDataType col_type = YBCDataTypeFromName(colDef->typeName);

    // TODO(mihnea or neil) Need to learn how to check the constraints in ColumnDef and
    // CreateStatement and identify the primary column.
    if (0) {
      YBCPgCreateTableAddColumn(handle, colDef->colname, col_order, col_type, false, true);
    }
    col_order++;
  }

  // Process the regular column.
  col_order = 0;
  foreach(listptr, stmt->tableElts) {
    ColumnDef *colDef = lfirst(listptr);
    YBCPgDataType col_type = YBCDataTypeFromName(colDef->typeName);

    // TODO(mihnea or neil) Need to learn how to check the constraints in ColumnDef and
    // CreateStatement and identify the primary column.
    if (1) {
      YBCPgCreateTableAddColumn(handle, colDef->colname, col_order, col_type, false, false);
    }
    col_order++;
  }

  // Create the table.
  YBCPgExecCreateTable(handle);
}
