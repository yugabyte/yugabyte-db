/*------------------------------------------------------------------------------
 *
 * yb_alter_concurrently.h
 *	  prototypes for yb_alter_concurrently.c
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/commands/yb_alter_concurrently.h
 *
 *------------------------------------------------------------------------------
 */

#ifndef YB_ALTER_CONCURRENTLY_H
#define YB_ALTER_CONCURRENTLY_H

#include "postgres.h"

#include "nodes/parsenodes.h"
#include "storage/lockdefs.h"
#include "tcop/utility.h"

extern bool yb_enable_alter_table_concurrently;
extern int	yb_alter_table_concurrently_poll_interval_ms;
extern bool yb_test_fail_osc_cutover;

/*
 * Run ALTER TABLE ... CONCURRENTLY.  Called by ProcessUtilitySlow in place of
 * AlterTable(), with relid locked in lockmode (ShareUpdateExclusiveLock) and
 * context->queryString holding the statement text.  Spans several
 * transactions; see yb_alter_concurrently.c.
 */
extern void YbAlterTableConcurrently(AlterTableStmt *stmt, Oid relid,
									 LOCKMODE lockmode,
									 AlterTableUtilityContext *context);

/*
 * The swap cutover of an online schema change job started by ALTER TABLE ...
 * CONCURRENTLY.
 *
 * Called by YbOscFinish in a DDL transaction, with AccessExclusiveLock held
 * on both tables, the capture triggers dropped and the job marked
 * "cutting_over".  Neither relation may be open in the caller
 * (CheckTableNotInUse).  Drops the job's transform function,
 * applies alterCmds, the job's ALTER TABLE statement text, to srcOid,
 * exchanges the DocDB storage of srcOid and dstOid (base table and indexes
 * pairwise), and drops dstOid.  An error rolls the whole transaction back,
 * the status change included, and leaves the job "ready" for a retry.
 */
extern void YbOscCutover(Oid srcOid, Oid dstOid, int64 jobId,
						 const char *alterCmds);

#endif							/* YB_ALTER_CONCURRENTLY_H */
