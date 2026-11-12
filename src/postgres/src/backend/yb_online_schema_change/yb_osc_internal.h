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

#include "nodes/pg_list.h"
#include "utils/rel.h"


extern char *YbOscQuoteQualifiedRelName(Relation rel);
extern List *YbOscGetPrimaryKeyAttnums(Relation rel);
extern char *YbOscBuildColumnList(Relation rel);
extern void	 YbOscRenameTable(Relation rel, const char *newName);

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

#endif							/* YB_OSC_INTERNAL_H */
