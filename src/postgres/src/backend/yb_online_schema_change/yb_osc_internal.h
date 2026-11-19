/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_internal.h
 *    Internal declarations shared by the yb_online_schema_change feature
 *    files.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_internal.h
 *--------------------------------------------------------------------------------------------------
 */
#ifndef YB_OSC_INTERNAL_H
#define YB_OSC_INTERNAL_H

#include "postgres.h"

#include "commands/trigger.h"
#include "nodes/pg_list.h"
#include "utils/rel.h"

/* Values stored in pg_yb_online_schema_change.status. */
#define YB_OSC_STATUS_ACTIVE			"active"
#define YB_OSC_STATUS_FINISHED			"finished"
#define YB_OSC_STATUS_ABORTED			"aborted"

/* Values the lifecycle functions store in origin and cutover_mode. */
#define YB_OSC_ORIGIN_MANUAL			"manual"
#define YB_OSC_CUTOVER_MODE_RENAME		"rename"

#define YB_OSC_TRIGGER_NAME_PREFIX	"yb_osc_"
#define YB_OSC_OLD_TABLE_PREFIX		"yb_osc_old_"


extern char *YbOscQuoteQualifiedRelName(Relation rel);
extern List *YbOscGetPrimaryKeyAttnums(Relation rel);
extern char *YbOscBuildColumnList(Relation rel);
extern void	 YbOscRenameTable(Relation rel, const char *newName);

extern Oid	YbOscTransformOwner(Oid transformOid);

extern List *YbOscGetKeyWitnessAttnums(Relation srcRel, Relation dstRel);

/*
 * An online schema change job's row transform, a PL/pgSQL function called once
 * per source row.  call is fn(<whole-row Var of varno 1>), returning the
 * target row.
 */
typedef struct YbOscTransform
{
	Oid			fnOid;
	Expr	   *call;
} YbOscTransform;

extern YbOscTransform *YbOscCompileTransform(Oid fnOid, Relation srcRel,
											 Relation dstRel);
extern char *YbOscTransformDeparse(YbOscTransform *xf, Relation srcRel,
								   Relation dstRel);
extern char *YbOscTransformDeparseCall(YbOscTransform *xf,
									   Relation srcRel);
extern void YbOscForceSpeculativePl(void);


/* ---- yb_osc_validate.c ---- */

extern void YbOscValidate(Relation srcRel, Relation dstRel,
						  Oid transformFnOid);
extern void YbOscEnsureSrcNoForeignKeys(Relation srcRel);


/* ---- yb_osc_catalog.c ---- */

/* A row of pg_yb_online_schema_change. */
typedef struct YbOscJob
{
	int64		job_id;
	Oid			source_table_oid;
	Oid			target_table_oid;
	Oid			transform_fn_oid;
	char	   *status;
} YbOscJob;

extern int64 YbOscInsertJob(Oid srcOid, Oid dstOid, Oid transformOid);
extern void YbOscSetStatus(int64 jobId, const char *status);
extern bool YbOscFetchJob(int64 jobId, YbOscJob *job);
extern bool YbOscHasActiveJob(Oid srcOid, Oid dstOid);


/* ---- yb_osc_trigger.c ---- */

extern void YbOscCreateTrigger(Relation srcRel, const char *triggerName,
							   Oid targetOid, Oid transformOid);
extern void YbOscDropTrigger(Relation srcRel);

#endif							/* YB_OSC_INTERNAL_H */
