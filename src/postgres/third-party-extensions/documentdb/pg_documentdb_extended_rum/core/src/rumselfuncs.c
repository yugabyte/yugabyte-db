/*-------------------------------------------------------------------------
 *
 * rumselfuncs.c
 *	  Cost estimate handling for the rum index
 *
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "utils/selfuncs.h"
#include "utils/index_selfuncs.h"
#include "utils/spccache.h"
#include "utils/lsyscache.h"
#include "catalog/pg_collation.h"
#include "utils/array.h"
#include "math.h"

#include "pg_documentdb_rum.h"

#define DEFAULT_PAGE_CPU_MULTIPLIER 50.0

typedef struct
{
	bool attHasFullScan[INDEX_MAX_KEYS];
	bool attHasNormalScan[INDEX_MAX_KEYS];
	double partialEntries;
	double exactEntries;
	double searchEntries;
	double arrayScans;
} GinQualCounts;


static void RumCostEstimateCore(PlannerInfo *root, IndexPath *path, double loop_count,
								Cost *indexStartupCost, Cost *indexTotalCost,
								Selectivity *indexSelectivity, double *indexCorrelation,
								double *indexPages);
static bool gincost_opexpr(PlannerInfo *root,
						   IndexOptInfo *index,
						   int indexcol,
						   OpExpr *clause,
						   GinQualCounts *counts);
static bool gincost_scalararrayopexpr(PlannerInfo *root,
									  IndexOptInfo *index,
									  int indexcol,
									  ScalarArrayOpExpr *clause,
									  double numIndexEntries,
									  GinQualCounts *counts);

/*
 * Cost estimate logic for documentdb_extended_rum. Implements logic handling
 * how to cost pushdown to the index.
 */
PGDLLEXPORT void
documentdb_rum_costestimate(PlannerInfo *root, IndexPath *path, double loop_count,
							Cost *indexStartupCost, Cost *indexTotalCost,
							Selectivity *indexSelectivity, double *indexCorrelation,
							double *indexPages)
{
	/* Escape hatch to disable custom logic */
	if (!RumEnableCustomCostEstimate)
	{
		gincostestimate(root, path, loop_count, indexStartupCost, indexTotalCost,
						indexSelectivity, indexCorrelation, indexPages);
	}
	else
	{
		RumCostEstimateCore(root, path, loop_count, indexStartupCost, indexTotalCost,
							indexSelectivity, indexCorrelation, indexPages);
	}
}


/*
 * Core implementation of the index cost estimate.
 * This is currently an exact copy of gincostestimate, and will be modified
 * in subsequent changes.
 */
static void
RumCostEstimateCore(PlannerInfo *root, IndexPath *path, double loop_count,
					Cost *indexStartupCost, Cost *indexTotalCost,
					Selectivity *indexSelectivity, double *indexCorrelation,
					double *indexPages)
{
	IndexOptInfo *index = path->indexinfo;
	List *indexQuals = get_quals_from_indexclauses(path->indexclauses);
	List *selectivityQuals;
	double numPages = index->pages,
		   numTuples = index->tuples;
	double numEntryPages,
		   numDataPages,
		   numPendingPages,
		   numEntries;
	GinQualCounts counts;
	bool matchPossible;
	bool fullIndexScan;
	double partialScale;
	double entryPagesFetched,
		   dataPagesFetched,
		   dataPagesFetchedBySel;
	double qual_op_cost,
		   qual_arg_cost,
		   spc_random_page_cost,
		   outer_scans;
	Cost descentCost;
	Relation indexRel;
	RumStatsData ginStats;
	ListCell *lc;
	int i;

	/*
	 * Obtain statistical information from the meta page, if possible.  Else
	 * set ginStats to zeroes, and we'll cope below.
	 */
	if (!index->hypothetical)
	{
		/* Lock should have already been obtained in plancat.c */
		indexRel = index_open(index->indexoid, NoLock);
		rumGetStats(indexRel, &ginStats);
		index_close(indexRel, NoLock);
	}
	else
	{
		memset(&ginStats, 0, sizeof(ginStats));
	}

	/*
	 * Assuming we got valid (nonzero) stats at all, nPendingPages can be
	 * trusted, but the other fields are data as of the last VACUUM.  We can
	 * scale them up to account for growth since then, but that method only
	 * goes so far; in the worst case, the stats might be for a completely
	 * empty index, and scaling them will produce pretty bogus numbers.
	 * Somewhat arbitrarily, set the cutoff for doing scaling at 4X growth; if
	 * it's grown more than that, fall back to estimating things only from the
	 * assumed-accurate index size.  But we'll trust nPendingPages in any case
	 * so long as it's not clearly insane, ie, more than the index size.
	 */
	if (ginStats.nPendingPages < numPages)
	{
		numPendingPages = ginStats.nPendingPages;
	}
	else
	{
		numPendingPages = 0;
	}

	if (numPages > 0 && ginStats.nTotalPages <= numPages &&
		ginStats.nTotalPages > numPages / 4 &&
		ginStats.nEntryPages > 0 && ginStats.nEntries > 0)
	{
		/*
		 * OK, the stats seem close enough to sane to be trusted.  But we
		 * still need to scale them by the ratio numPages / nTotalPages to
		 * account for growth since the last VACUUM.
		 */
		double scale = numPages / ginStats.nTotalPages;

		numEntryPages = ceil(ginStats.nEntryPages * scale);
		numDataPages = ceil(ginStats.nDataPages * scale);
		numEntries = ceil(ginStats.nEntries * scale);

		/* ensure we didn't round up too much */
		numEntryPages = Min(numEntryPages, numPages - numPendingPages);
		numDataPages = Min(numDataPages,
						   numPages - numPendingPages - numEntryPages);
	}
	else
	{
		/*
		 * We might get here because it's a hypothetical index, or an index
		 * created pre-9.1 and never vacuumed since upgrading (in which case
		 * its stats would read as zeroes), or just because it's grown too
		 * much since the last VACUUM for us to put our faith in scaling.
		 *
		 * Invent some plausible internal statistics based on the index page
		 * count (and clamp that to at least 10 pages, just in case).  We
		 * estimate that 90% of the index is entry pages, and the rest is data
		 * pages.  Estimate 100 entries per entry page; this is rather bogus
		 * since it'll depend on the size of the keys, but it's more robust
		 * than trying to predict the number of entries per heap tuple.
		 */
		numPages = Max(numPages, 10);
		numEntryPages = floor((numPages - numPendingPages) * 0.90);
		numDataPages = numPages - numPendingPages - numEntryPages;
		numEntries = floor(numEntryPages * 100);
	}

	/* In an empty index, numEntries could be zero.  Avoid divide-by-zero */
	if (numEntries < 1)
	{
		numEntries = 1;
	}

	/*
	 * If the index is partial, AND the index predicate with the index-bound
	 * quals to produce a more accurate idea of the number of rows covered by
	 * the bound conditions.
	 */
	selectivityQuals = add_predicate_to_index_quals(index, indexQuals);

	/* Estimate the fraction of main-table tuples that will be visited */
	*indexSelectivity = clauselist_selectivity(root, selectivityQuals,
											   index->rel->relid,
											   JOIN_INNER,
											   NULL);

	/* fetch estimated page cost for tablespace containing index */
	get_tablespace_page_costs(index->reltablespace,
							  &spc_random_page_cost,
							  NULL);

	/*
	 * Generic assumption about index correlation: there isn't any.
	 */
	*indexCorrelation = 0.0;

	/*
	 * Examine quals to estimate number of search entries & partial matches
	 */
	memset(&counts, 0, sizeof(counts));
	counts.arrayScans = 1;
	matchPossible = true;

	foreach(lc, path->indexclauses)
	{
		IndexClause *iclause = lfirst_node(IndexClause, lc);
		ListCell *lc2;

		foreach(lc2, iclause->indexquals)
		{
			RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc2);
			Expr *clause = rinfo->clause;

			if (IsA(clause, OpExpr))
			{
				matchPossible = gincost_opexpr(root,
											   index,
											   iclause->indexcol,
											   (OpExpr *) clause,
											   &counts);
				if (!matchPossible)
				{
					break;
				}
			}
			else if (IsA(clause, ScalarArrayOpExpr))
			{
				matchPossible = gincost_scalararrayopexpr(root,
														  index,
														  iclause->indexcol,
														  (ScalarArrayOpExpr *) clause,
														  numEntries,
														  &counts);
				if (!matchPossible)
				{
					break;
				}
			}
			else
			{
				/* shouldn't be anything else for a GIN index */
				elog(ERROR, "unsupported GIN indexqual type: %d",
					 (int) nodeTag(clause));
			}
		}
	}

	/* Fall out if there were any provably-unsatisfiable quals */
	if (!matchPossible)
	{
		*indexStartupCost = 0;
		*indexTotalCost = 0;
		*indexSelectivity = 0;
		return;
	}

	/*
	 * If attribute has a full scan and at the same time doesn't have normal
	 * scan, then we'll have to scan all non-null entries of that attribute.
	 * Currently, we don't have per-attribute statistics for GIN.  Thus, we
	 * must assume the whole GIN index has to be scanned in this case.
	 */
	fullIndexScan = false;
	for (i = 0; i < index->nkeycolumns; i++)
	{
		if (counts.attHasFullScan[i] && !counts.attHasNormalScan[i])
		{
			fullIndexScan = true;
			break;
		}
	}

	if (fullIndexScan || indexQuals == NIL)
	{
		/*
		 * Full index scan will be required.  We treat this as if every key in
		 * the index had been listed in the query; is that reasonable?
		 */
		counts.partialEntries = 0;
		counts.exactEntries = numEntries;
		counts.searchEntries = numEntries;
	}

	/* Will we have more than one iteration of a nestloop scan? */
	outer_scans = loop_count;

	/*
	 * Compute cost to begin scan, first of all, pay attention to pending
	 * list.
	 */
	entryPagesFetched = numPendingPages;

	/*
	 * Estimate number of entry pages read.  We need to do
	 * counts.searchEntries searches.  Use a power function as it should be,
	 * but tuples on leaf pages usually is much greater. Here we include all
	 * searches in entry tree, including search of first entry in partial
	 * match algorithm
	 */
	entryPagesFetched += ceil(counts.searchEntries * rint(pow(numEntryPages, 0.15)));

	/*
	 * Add an estimate of entry pages read by partial match algorithm. It's a
	 * scan over leaf pages in entry tree.  We haven't any useful stats here,
	 * so estimate it as proportion.  Because counts.partialEntries is really
	 * pretty bogus (see code above), it's possible that it is more than
	 * numEntries; clamp the proportion to ensure sanity.
	 */
	partialScale = counts.partialEntries / numEntries;
	partialScale = Min(partialScale, 1.0);

	entryPagesFetched += ceil(numEntryPages * partialScale);

	/*
	 * Partial match algorithm reads all data pages before doing actual scan,
	 * so it's a startup cost.  Again, we haven't any useful stats here, so
	 * estimate it as proportion.
	 */
	dataPagesFetched = ceil(numDataPages * partialScale);

	*indexStartupCost = 0;
	*indexTotalCost = 0;

	/*
	 * Add a CPU-cost component to represent the costs of initial entry btree
	 * descent.  We don't charge any I/O cost for touching upper btree levels,
	 * since they tend to stay in cache, but we still have to do about log2(N)
	 * comparisons to descend a btree of N leaf tuples.  We charge one
	 * cpu_operator_cost per comparison.
	 *
	 * If there are ScalarArrayOpExprs, charge this once per SA scan.  The
	 * ones after the first one are not startup cost so far as the overall
	 * plan is concerned, so add them only to "total" cost.
	 */
	if (numEntries > 1)         /* avoid computing log(0) */
	{
		descentCost = ceil(log(numEntries) / log(2.0)) * cpu_operator_cost;
		*indexStartupCost += descentCost * counts.searchEntries;
		*indexTotalCost += counts.arrayScans * descentCost * counts.searchEntries;
	}

	/*
	 * Add a cpu cost per entry-page fetched. This is not amortized over a
	 * loop.
	 */
	*indexStartupCost += entryPagesFetched * DEFAULT_PAGE_CPU_MULTIPLIER *
						 cpu_operator_cost;
	*indexTotalCost += entryPagesFetched * counts.arrayScans *
					   DEFAULT_PAGE_CPU_MULTIPLIER * cpu_operator_cost;

	/*
	 * Add a cpu cost per data-page fetched. This is also not amortized over a
	 * loop. Since those are the data pages from the partial match algorithm,
	 * charge them as startup cost.
	 */
	*indexStartupCost += DEFAULT_PAGE_CPU_MULTIPLIER * cpu_operator_cost *
						 dataPagesFetched;

	/*
	 * Since we add the startup cost to the total cost later on, remove the
	 * initial arrayscan from the total.
	 */
	*indexTotalCost += dataPagesFetched * (counts.arrayScans - 1) *
					   DEFAULT_PAGE_CPU_MULTIPLIER * cpu_operator_cost;

	/*
	 * Calculate cache effects if more than one scan due to nestloops or array
	 * quals.  The result is pro-rated per nestloop scan, but the array qual
	 * factor shouldn't be pro-rated (compare genericcostestimate).
	 */
	if (outer_scans > 1 || counts.arrayScans > 1)
	{
		entryPagesFetched *= outer_scans * counts.arrayScans;
		entryPagesFetched = index_pages_fetched(entryPagesFetched,
												(BlockNumber) numEntryPages,
												numEntryPages, root);
		entryPagesFetched /= outer_scans;
		dataPagesFetched *= outer_scans * counts.arrayScans;
		dataPagesFetched = index_pages_fetched(dataPagesFetched,
											   (BlockNumber) numDataPages,
											   numDataPages, root);
		dataPagesFetched /= outer_scans;
	}

	/*
	 * Here we use random page cost because logically-close pages could be far
	 * apart on disk.
	 */
	*indexStartupCost += (entryPagesFetched + dataPagesFetched) * spc_random_page_cost;

	/*
	 * Now compute the number of data pages fetched during the scan.
	 *
	 * We assume every entry to have the same number of items, and that there
	 * is no overlap between them. (XXX: tsvector and array opclasses collect
	 * statistics on the frequency of individual keys; it would be nice to use
	 * those here.)
	 */
	dataPagesFetched = ceil(numDataPages * counts.exactEntries / numEntries);

	/*
	 * If there is a lot of overlap among the entries, in particular if one of
	 * the entries is very frequent, the above calculation can grossly
	 * under-estimate.  As a simple cross-check, calculate a lower bound based
	 * on the overall selectivity of the quals.  At a minimum, we must read
	 * one item pointer for each matching entry.
	 *
	 * The width of each item pointer varies, based on the level of
	 * compression.  We don't have statistics on that, but an average of
	 * around 3 bytes per item is fairly typical.
	 */
	dataPagesFetchedBySel = ceil(*indexSelectivity *
								 (numTuples / (BLCKSZ / 3)));
	if (dataPagesFetchedBySel > dataPagesFetched)
	{
		dataPagesFetched = dataPagesFetchedBySel;
	}

	/* Add one page cpu-cost to the startup cost */
	*indexStartupCost += DEFAULT_PAGE_CPU_MULTIPLIER * cpu_operator_cost *
						 counts.searchEntries;

	/*
	 * Add once again a CPU-cost for those data pages, before amortizing for
	 * cache.
	 */
	*indexTotalCost += dataPagesFetched * counts.arrayScans *
					   DEFAULT_PAGE_CPU_MULTIPLIER * cpu_operator_cost;

	/* Account for cache effects, the same as above */
	if (outer_scans > 1 || counts.arrayScans > 1)
	{
		dataPagesFetched *= outer_scans * counts.arrayScans;
		dataPagesFetched = index_pages_fetched(dataPagesFetched,
											   (BlockNumber) numDataPages,
											   numDataPages, root);
		dataPagesFetched /= outer_scans;
	}

	/* And apply random_page_cost as the cost per page */
	*indexTotalCost += *indexStartupCost +
					   dataPagesFetched * spc_random_page_cost;

	/*
	 * Add on index qual eval costs, much as in genericcostestimate. We charge
	 * cpu but we can disregard indexorderbys, since GIN doesn't support
	 * those.
	 */
	qual_arg_cost = index_other_operands_eval_cost(root, indexQuals);
	qual_op_cost = cpu_operator_cost * list_length(indexQuals);

	*indexStartupCost += qual_arg_cost;
	*indexTotalCost += qual_arg_cost;

	/*
	 * Add a cpu cost per search entry, corresponding to the actual visited
	 * entries.
	 */
	*indexTotalCost += (counts.searchEntries * counts.arrayScans) * (qual_op_cost);

	/* Now add a cpu cost per tuple in the posting lists / trees */
	*indexTotalCost += (numTuples * *indexSelectivity) * (cpu_index_tuple_cost);
	*indexPages = dataPagesFetched;
}


/*
 * Estimate the number of index terms that need to be searched for while
 * testing the given GIN query, and increment the counts in *counts
 * appropriately.  If the query is unsatisfiable, return false.
 */
static bool
gincost_pattern(IndexOptInfo *index, int indexcol,
				Oid clause_op, Datum query,
				GinQualCounts *counts)
{
	FmgrInfo flinfo;
	Oid extractProcOid;
	Oid collation;
	int strategy_op;
	Oid lefttype,
		righttype;
	int32 nentries = 0;
	bool *partial_matches = NULL;
	Pointer *extra_data = NULL;
	bool *nullFlags = NULL;
	int32 searchMode = GIN_SEARCH_MODE_DEFAULT;
	int32 i;

	Assert(indexcol < index->nkeycolumns);

	/*
	 * Get the operator's strategy number and declared input data types within
	 * the index opfamily.  (We don't need the latter, but we use
	 * get_op_opfamily_properties because it will throw error if it fails to
	 * find a matching pg_amop entry.)
	 */
	get_op_opfamily_properties(clause_op, index->opfamily[indexcol], false,
							   &strategy_op, &lefttype, &righttype);

	/*
	 * GIN always uses the "default" support functions, which are those with
	 * lefttype == righttype == the opclass' opcintype (see
	 * IndexSupportInitialize in relcache.c).
	 */
	extractProcOid = get_opfamily_proc(index->opfamily[indexcol],
									   index->opcintype[indexcol],
									   index->opcintype[indexcol],
									   GIN_EXTRACTQUERY_PROC);

	if (!OidIsValid(extractProcOid))
	{
		/* should not happen; throw same error as index_getprocinfo */
		elog(ERROR, "missing support function %d for attribute %d of index \"%s\"",
			 GIN_EXTRACTQUERY_PROC, indexcol + 1,
			 get_rel_name(index->indexoid));
	}

	/*
	 * Choose collation to pass to extractProc (should match initGinState).
	 */
	if (OidIsValid(index->indexcollations[indexcol]))
	{
		collation = index->indexcollations[indexcol];
	}
	else
	{
		collation = DEFAULT_COLLATION_OID;
	}

	fmgr_info(extractProcOid, &flinfo);

	set_fn_opclass_options(&flinfo, index->opclassoptions[indexcol]);

	FunctionCall7Coll(&flinfo,
					  collation,
					  query,
					  PointerGetDatum(&nentries),
					  UInt16GetDatum(strategy_op),
					  PointerGetDatum(&partial_matches),
					  PointerGetDatum(&extra_data),
					  PointerGetDatum(&nullFlags),
					  PointerGetDatum(&searchMode));

	if (nentries <= 0 && searchMode == GIN_SEARCH_MODE_DEFAULT)
	{
		/* No match is possible */
		return false;
	}

	for (i = 0; i < nentries; i++)
	{
		/*
		 * For partial match we haven't any information to estimate number of
		 * matched entries in index, so, we just estimate it as 100
		 */
		if (partial_matches && partial_matches[i])
		{
			counts->partialEntries += 100;
		}
		else
		{
			counts->exactEntries++;
		}

		counts->searchEntries++;
	}

	if (searchMode == GIN_SEARCH_MODE_DEFAULT)
	{
		counts->attHasNormalScan[indexcol] = true;
	}
	else if (searchMode == GIN_SEARCH_MODE_INCLUDE_EMPTY)
	{
		/* Treat "include empty" like an exact-match item */
		counts->attHasNormalScan[indexcol] = true;
		counts->exactEntries++;
		counts->searchEntries++;
	}
	else
	{
		/* It's GIN_SEARCH_MODE_ALL */
		counts->attHasFullScan[indexcol] = true;
	}

	return true;
}


/*
 * Estimate the number of index terms that need to be searched for while
 * testing the given GIN index clause, and increment the counts in *counts
 * appropriately.  If the query is unsatisfiable, return false.
 */
static bool
gincost_opexpr(PlannerInfo *root,
			   IndexOptInfo *index,
			   int indexcol,
			   OpExpr *clause,
			   GinQualCounts *counts)
{
	Oid clause_op = clause->opno;
	Node *operand = (Node *) lsecond(clause->args);

	/* aggressively reduce to a constant, and look through relabeling */
	operand = estimate_expression_value(root, operand);

	if (IsA(operand, RelabelType))
	{
		operand = (Node *) ((RelabelType *) operand)->arg;
	}

	/*
	 * It's impossible to call extractQuery method for unknown operand. So
	 * unless operand is a Const we can't do much; just assume there will be
	 * one ordinary search entry from the operand at runtime.
	 */
	if (!IsA(operand, Const))
	{
		counts->exactEntries++;
		counts->searchEntries++;
		return true;
	}

	/* If Const is null, there can be no matches */
	if (((Const *) operand)->constisnull)
	{
		return false;
	}

	/* Otherwise, apply extractQuery and get the actual term counts */
	return gincost_pattern(index, indexcol, clause_op,
						   ((Const *) operand)->constvalue,
						   counts);
}


/*
 * Estimate the number of index terms that need to be searched for while
 * testing the given GIN index clause, and increment the counts in *counts
 * appropriately.  If the query is unsatisfiable, return false.
 *
 * A ScalarArrayOpExpr will give rise to N separate indexscans at runtime,
 * each of which involves one value from the RHS array, plus all the
 * non-array quals (if any).  To model this, we average the counts across
 * the RHS elements, and add the averages to the counts in *counts (which
 * correspond to per-indexscan costs).  We also multiply counts->arrayScans
 * by N, causing gincostestimate to scale up its estimates accordingly.
 */
static bool
gincost_scalararrayopexpr(PlannerInfo *root,
						  IndexOptInfo *index,
						  int indexcol,
						  ScalarArrayOpExpr *clause,
						  double numIndexEntries,
						  GinQualCounts *counts)
{
	Oid clause_op = clause->opno;
	Node *rightop = (Node *) lsecond(clause->args);
	ArrayType *arrayval;
	int16 elmlen;
	bool elmbyval;
	char elmalign;
	int numElems;
	Datum *elemValues;
	bool *elemNulls;
	GinQualCounts arraycounts;
	int numPossible = 0;
	int i;

	Assert(clause->useOr);

	/* aggressively reduce to a constant, and look through relabeling */
	rightop = estimate_expression_value(root, rightop);

	if (IsA(rightop, RelabelType))
	{
		rightop = (Node *) ((RelabelType *) rightop)->arg;
	}

	/*
	 * It's impossible to call extractQuery method for unknown operand. So
	 * unless operand is a Const we can't do much; just assume there will be
	 * one ordinary search entry from each array entry at runtime, and fall
	 * back on a probably-bad estimate of the number of array entries.
	 */
	if (!IsA(rightop, Const))
	{
		counts->exactEntries++;
		counts->searchEntries++;
#if PG_VERSION_NUM >= 170000
		counts->arrayScans *= estimate_array_length(root, rightop);
#else
		counts->arrayScans *= estimate_array_length(rightop);
#endif
		return true;
	}

	/* If Const is null, there can be no matches */
	if (((Const *) rightop)->constisnull)
	{
		return false;
	}

	/* Otherwise, extract the array elements and iterate over them */
	arrayval = DatumGetArrayTypeP(((Const *) rightop)->constvalue);
	get_typlenbyvalalign(ARR_ELEMTYPE(arrayval),
						 &elmlen, &elmbyval, &elmalign);
	deconstruct_array(arrayval,
					  ARR_ELEMTYPE(arrayval),
					  elmlen, elmbyval, elmalign,
					  &elemValues, &elemNulls, &numElems);

	memset(&arraycounts, 0, sizeof(arraycounts));

	for (i = 0; i < numElems; i++)
	{
		GinQualCounts elemcounts;

		/* NULL can't match anything, so ignore, as the executor will */
		if (elemNulls[i])
		{
			continue;
		}

		/* Otherwise, apply extractQuery and get the actual term counts */
		memset(&elemcounts, 0, sizeof(elemcounts));

		if (gincost_pattern(index, indexcol, clause_op, elemValues[i],
							&elemcounts))
		{
			/* We ignore array elements that are unsatisfiable patterns */
			numPossible++;

			if (elemcounts.attHasFullScan[indexcol] &&
				!elemcounts.attHasNormalScan[indexcol])
			{
				/*
				 * Full index scan will be required.  We treat this as if
				 * every key in the index had been listed in the query; is
				 * that reasonable?
				 */
				elemcounts.partialEntries = 0;
				elemcounts.exactEntries = numIndexEntries;
				elemcounts.searchEntries = numIndexEntries;
			}
			arraycounts.partialEntries += elemcounts.partialEntries;
			arraycounts.exactEntries += elemcounts.exactEntries;
			arraycounts.searchEntries += elemcounts.searchEntries;
		}
	}

	if (numPossible == 0)
	{
		/* No satisfiable patterns in the array */
		return false;
	}

	/*
	 * Now add the averages to the global counts.  This will give us an
	 * estimate of the average number of terms searched for in each indexscan,
	 * including contributions from both array and non-array quals.
	 */
	counts->partialEntries += arraycounts.partialEntries / numPossible;
	counts->exactEntries += arraycounts.exactEntries / numPossible;
	counts->searchEntries += arraycounts.searchEntries / numPossible;

	counts->arrayScans *= numPossible;

	return true;
}
