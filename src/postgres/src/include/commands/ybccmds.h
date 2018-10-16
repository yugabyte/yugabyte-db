/*--------------------------------------------------------------------------------------------------
 *
 * ybccmds.h
 *	  prototypes for ybccmds.c
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
 * src/include/commands/ybccmds.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YBCCMDS_H
#define YBCCMDS_H

#include "access/htup.h"
#include "catalog/dependency.h"
#include "catalog/objectaddress.h"
#include "nodes/parsenodes.h"
#include "storage/lock.h"
#include "utils/relcache.h"

#include "yb/yql/pggate/ybc_pggate.h"

/*  Database Functions -------------------------------------------------------------------------- */

extern void YBCCreateDatabase(Oid dboid, const char *dbname, Oid src_dboid);

extern void YBCDropDatabase(Oid dboid, const char *dbname);

/*  Table Functions ----------------------------------------------------------------------------- */

extern void YBCCreateTable(CreateStmt *stmt, char relkind, Oid relationId);

extern void YBCDropTable(Oid relationId, const char *relname, const char *schemaname);

#endif
