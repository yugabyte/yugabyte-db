/*--------------------------------------------------------------------------
 *
 * ybginutil.c
 *	  Utility routines for the Yugabyte inverted index access method.
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
 *			src/backend/access/ybgin/ybginutil.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/gin_private.h"
#include "access/relation.h"
#include "access/reloptions.h"
#include "c.h"
#include "nodes/execnodes.h"
#include "commands/ybccmds.h"
#include "nodes/nodes.h"
#include "utils/index_selfuncs.h"

void
ybgincostestimate(struct PlannerInfo *root, struct IndexPath *path,
				  double loop_count, Cost *indexStartupCost,
				  Cost *indexTotalCost, Selectivity *indexSelectivity,
				  double *indexCorrelation, double *indexPages)
{
	gincostestimate(root, path, loop_count, indexStartupCost, indexTotalCost,
					indexSelectivity, indexCorrelation, indexPages);
}

bytea *
ybginoptions(Datum reloptions, bool validate)
{
	relopt_value *options;
	int			numoptions;
	int			i;

	/*
	 * Rather than creating a new RELOPT_KIND_YBGIN, reuse RELOPT_KIND_GIN and
	 * disallow the options that aren't supported.  The downside is that most
	 * gin options will probably apply to only one of (pg)gin or ybgin.  The
	 * upside is that the unsupported options can be ignored to be compatible
	 * with existing scripts without needing modification.
	 *
	 * Since, currently, we disallow any gin reloptions being set, we should be
	 * able to change our mind later and introduce a RELOPT_KIND_YBGIN if
	 * desired.
	 *
	 * TODO(jason): ignore and clear, rather than error, on unsupported options
	 *
	 * To automatically disallow new options in case upstream postgres creates
	 * them and we import them,
	 * 1. if the relopt kind is not gin, allow
	 * 2. allow specific gin relopts (currently none)
	 * 3. disallow the rest
	 */
	options = parseRelOptions(reloptions, validate, RELOPT_KIND_GIN,
							  &numoptions);
	for (i = 0; i < numoptions; i++)
	{
		if (!options[i].isset)
			continue;
		if (options[i].gen->kinds != RELOPT_KIND_GIN)
			continue;
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ybgin indexes do not support reloption %s",
						options[i].gen->name)));
	}

	return NULL;
}

bool
ybginvalidate(Oid opclassoid)
{
	/*
	 * Calling ginvalidate is probably right.  It's hard to tell since it
	 * doesn't work in the first place (issue #8949).
	 *
	 * TODO(jason): when it starts working, refactor to use "ybgin" instead of
	 * "gin" for error messages (probably should create a layer to accept this
	 * string and pass it down).
	 */
	return ginvalidate(opclassoid);
}

void
ybginbindschema(YBCPgStatement handle,
				struct IndexInfo *indexInfo,
				TupleDesc indexTupleDesc,
				int16 *coloptions)
{
	YBCBindCreateIndexColumns(handle,
							  indexInfo,
							  indexTupleDesc,
							  coloptions,
							  indexInfo->ii_NumIndexKeyAttrs);
}

/*
 * Given gin null category, return human-readable string.
 */
const char *
ybginNullCategoryToString(GinNullCategory category)
{
	switch (category)
	{
		case GIN_CAT_NORM_KEY:
			return "normal";
		case GIN_CAT_NULL_KEY:
			return "null-key";
		case GIN_CAT_EMPTY_ITEM:
			return "empty-item";
		case GIN_CAT_NULL_ITEM:
			return "null-item";
		case GIN_CAT_EMPTY_QUERY:
			return "empty-query";
		default:
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("unrecognized null category: %d",
							category)));
	}
}

/*
 * Given gin search mode, return human-readable string.
 */
const char *
ybginSearchModeToString(int32 searchMode)
{
	switch (searchMode)
	{
		case GIN_SEARCH_MODE_DEFAULT:
			return "default";
		case GIN_SEARCH_MODE_INCLUDE_EMPTY:
			return "include-empty";
		case GIN_SEARCH_MODE_ALL:
			return "all";
		case GIN_SEARCH_MODE_EVERYTHING:
			return "everything";
		default:
			ereport(ERROR,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("unrecognized search mode: %d",
							searchMode)));
	}
}
