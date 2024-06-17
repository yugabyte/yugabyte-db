/*--------------------------------------------------------------------------
 *
 * ybvector.c
 *	  Implementation of Yugabyte Vector Index access method.
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
 * IDENTIFICATION
 *		third-party-extensions/pgvector/ybvector/ybvector.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "ybvector.h"

#include "access/amapi.h"
#include "catalog/namespace.h"
#include "catalog/pg_type_d.h"
#include "commands/defrem.h"
#include "commands/extension.h"
#include "fmgr.h"
#include "nodes/nodes.h"
#include "postgres_ext.h"
#include "utils/lsyscache.h"

/*
 * makeBaseYbVectorHandler: Makes a handler that handles all functionality
 * common to YB vector index access methods.
 */
IndexAmRoutine *
makeBaseYbVectorHandler()
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = 1;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = true;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = false; /* TODO(tanuj): support multicolumn */
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = true; /* TODO(tanuj): check what this is */
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = ybvectorbuild;
	amroutine->ambuildempty = ybvectorbuildempty;
	amroutine->aminsert = NULL; /* use yb_aminsert below instead */
	amroutine->ambulkdelete = ybvectorbulkdelete;
	amroutine->amvacuumcleanup = ybvectorvacuumcleanup;
	amroutine->amcanreturn = ybvectorcanreturn;
	amroutine->amcostestimate = ybvectorcostestimate;
	amroutine->amoptions = ybvectoroptions;
	amroutine->amproperty = NULL;
	amroutine->amvalidate = ybvectorvalidate;
	amroutine->ambeginscan = ybvectorbeginscan;
	amroutine->amrescan = ybvectorrescan;
	amroutine->amgettuple = ybvectorgettuple;
	amroutine->amgetbitmap = NULL; /* TODO(tanuj): support bitmap scan */
	amroutine->amendscan = ybvectorendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;
	amroutine->yb_amisforybrelation = true;
	amroutine->yb_aminsert = ybvectorinsert;
	amroutine->yb_amdelete = ybvectordelete;
	amroutine->yb_ambackfill = ybvectorbackfill;
	amroutine->yb_ammightrecheck = ybvectormightrecheck;
	amroutine->yb_ambindschema = NULL;

	return amroutine;
}
