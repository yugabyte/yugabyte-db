/*--------------------------------------------------------------------------
 *
 * ybgin.c
 *	  Implementation of Yugabyte Generalized Inverted Index access method.
 *
 * Copyright (c) YugaByte, Inc.
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
 * IDENTIFICATION
 *			src/backend/access/ybgin/ybgin.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/amapi.h"
#include "access/gin.h"
#include "c.h"
#include "fmgr.h"
#include "nodes/nodes.h"

/*
 * YBGIN handler function: return IndexAmRoutine with access method parameters
 * and callbacks.
 */
Datum
ybginhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = GINNProcs;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = false; /* TODO(jason): support multicolumn */
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = true; /* TODO(jason): check what this is */
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = NULL;
	amroutine->ambuildempty = NULL;
	amroutine->aminsert = NULL; /* use yb_aminsert below instead */
	amroutine->ambulkdelete = NULL;
	amroutine->amvacuumcleanup = NULL;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = NULL;
	amroutine->amoptions = NULL;
	amroutine->amproperty = NULL;
	amroutine->amvalidate = NULL;
	amroutine->ambeginscan = NULL;
	amroutine->amrescan = NULL;
	amroutine->amgettuple = NULL;
	amroutine->amgetbitmap = NULL; /* TODO(jason): support bitmap scan */
	amroutine->amendscan = NULL;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;
	amroutine->yb_aminsert = NULL;
	amroutine->yb_amdelete = NULL;
	amroutine->yb_ambackfill = NULL;

	PG_RETURN_POINTER(amroutine);
}
