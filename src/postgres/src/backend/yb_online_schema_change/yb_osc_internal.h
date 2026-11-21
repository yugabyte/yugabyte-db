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
#include "storage/lockdefs.h"
#include "utils/rel.h"
#include "utils/yb_online_schema_change.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

#define YB_OSC_TRIGGER_NAME_PREFIX	"yb_osc_"
#define YB_OSC_OLD_TABLE_PREFIX		"yb_osc_old_"


extern char *YbOscQuoteQualifiedRelName(Relation rel);
extern List *YbOscGetPrimaryKeyAttnums(Relation rel);
extern char *YbOscBuildColumnList(Relation rel);
extern void	 YbOscRenameTable(Relation rel, const char *newName);

extern Oid	YbOscTransformOwner(Oid transformOid);

extern List *YbOscHierarchyNodes(Oid rootOid, LOCKMODE lockmode);
extern List *YbOscHierarchyLeaves(List *nodes);
extern YbcPgOid *YbOscRelfileNodeArray(List *oids);

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
						  Oid transformFnOid, const YbOscBeginOptions *opts);


/* ---- yb_osc_constraints.c ---- */

/*
 * These take OIDs rather than open Relations because the ALTER TABLE they
 * issue fails on a table this session holds open (CheckTableNotInUse).
 * Callers keep their locks but close the relations first.
 */
extern void YbOscPromoteForeignKeys(Oid srcOid, Oid dstOid);
extern void YbOscCheckForeignKeysPromoted(Oid srcOid, Oid dstOid);
extern void YbOscDropForeignKeys(Oid relOid);


/* ---- yb_osc_catalog.c ---- */

extern int64 YbOscInsertJob(Oid srcOid, Oid dstOid, Oid transformOid,
							const YbOscBeginOptions *opts);
extern void YbOscSetStatus(int64 jobId, const char *status);
extern bool YbOscHasActiveJob(Oid srcOid, Oid dstOid);


/* ---- yb_osc_trigger.c ---- */

extern void YbOscCreateTrigger(Relation srcRel, const char *triggerName,
							   Oid targetOid, Oid transformOid);
extern void YbOscDropTrigger(Relation srcRel);

#endif							/* YB_OSC_INTERNAL_H */
