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
extern bool yb_osc_keep_old_table;
extern bool yb_test_fail_osc_cutover;

/*
 * What phase 2 of an ALTER TABLE decided for its main relation, recorded
 * between phase 2 and phase 3 when AlterTableUtilityContext.yb_osc_capture is
 * set.  The driver runs the statement on the empty shadow to learn how each
 * column is recomputed.
 */
typedef struct YbNewColumnValue
{
	AttrNumber	attnum;			/* which column of the altered relation */
	Expr	   *expr;			/* over the old tuple; over the new tuple
								 * when is_generated */
	bool		is_generated;
} YbNewColumnValue;

typedef struct YbAlterTableCapture
{
	int			rewrite;		/* AT_REWRITE_* bitmask, 0 when no rewrite */
	List	   *newvals;		/* of YbNewColumnValue * */
} YbAlterTableCapture;

/*
 * Run ALTER TABLE ... CONCURRENTLY.  Called by ProcessUtilitySlow in place of
 * AlterTable(), with relid locked in lockmode (ShareUpdateExclusiveLock).
 * Spans several transactions; see yb_alter_concurrently.c.
 */
extern void YbAlterTableConcurrently(AlterTableStmt *stmt, Oid relid,
									 LOCKMODE lockmode,
									 AlterTableUtilityContext *context);

/*
 * The swap cutover of an ALTER TABLE ... CONCURRENTLY job.  Called by
 * YbOscFinish in a DDL transaction, with AccessExclusiveLock held on both
 * tables, the capture triggers dropped and the job marked finished.  Neither
 * relation may be open in the caller (CheckTableNotInUse).  Replays query,
 * the job's ALTER TABLE statement, on srcOid with
 * AlterTableUtilityContext.yb_osc_shadow_oid set to dstOid, which makes
 * phase 3 call YbOscSwapWithShadow instead of copying rows.  An error rolls
 * the whole transaction back and leaves the job active for a retry.
 */
extern void YbOscCutover(Oid srcOid, Oid dstOid, int64 jobId, const char *query);

/* Phase 3 of the cutover's ALTER TABLE; see YbOscCutover. */
extern void YbOscSwapWithShadow(Oid relid, Oid shadowOid);

#endif							/* YB_ALTER_CONCURRENTLY_H */
