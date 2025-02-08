#include "postgres.h"

#ifdef YB_IVFFLAT_INDEX_SUPPORT
#include <float.h>

#include "access/amapi.h"
#include "commands/vacuum.h"
#endif

#include "ivfflat.h"

#ifdef YB_IVFFLAT_INDEX_SUPPORT
#include "utils/guc.h"
#include "utils/selfuncs.h"
#include "utils/spccache.h"

#if PG_VERSION_NUM >= 120000
#include "commands/progress.h"
#endif

int			ivfflat_probes;
static relopt_kind ivfflat_relopt_kind;
#endif

/*
 * YB: _PG_init has been moved to vector.c. PgVector 0.8.0 has _PG_init
 * in vector.c as well. The below code will be removed once PgVector 0.8.0
 * is merged into YB.
 */
#if 0
/*
 * Initialize index options and variables
 */
void
_PG_init(void)
{
#ifdef YB_IVFFLAT_INDEX_SUPPORT
	ivfflat_relopt_kind = add_reloption_kind();
	add_int_reloption(ivfflat_relopt_kind, "lists", "Number of inverted lists",
					  IVFFLAT_DEFAULT_LISTS, 1, IVFFLAT_MAX_LISTS
#if PG_VERSION_NUM >= 130000
					  ,AccessExclusiveLock
#endif
		);

	DefineCustomIntVariable("ivfflat.probes", "Sets the number of probes",
							"Valid range is 1..lists.", &ivfflat_probes,
							1, 1, IVFFLAT_MAX_LISTS, PGC_USERSET, 0, NULL, NULL, NULL);
#endif
}
#endif

#ifdef YB_IVFFLAT_INDEX_SUPPORT
/*
 * Get the name of index build phase
 */
#if PG_VERSION_NUM >= 120000
static char *
ivfflatbuildphasename(int64 phasenum)
{
	switch (phasenum)
	{
		case PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE:
			return "initializing";
		case PROGRESS_IVFFLAT_PHASE_KMEANS:
			return "performing k-means";
		case PROGRESS_IVFFLAT_PHASE_SORT:
			return "sorting tuples";
		case PROGRESS_IVFFLAT_PHASE_LOAD:
			return "loading tuples";
		default:
			return NULL;
	}
}
#endif

/*
 * Estimate the cost of an index scan
 */
static void
ivfflatcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
					Cost *indexStartupCost, Cost *indexTotalCost,
					Selectivity *indexSelectivity, double *indexCorrelation,
					double *indexPages)
{
	GenericCosts costs;
	int			lists;
	double		ratio;
	double		spc_seq_page_cost;
	Relation	indexRel;
#if PG_VERSION_NUM < 120000
	List	   *qinfos;
#endif

	/* Never use index without order */
	if (path->indexorderbys == NULL)
	{
		*indexStartupCost = DBL_MAX;
		*indexTotalCost = DBL_MAX;
		*indexSelectivity = 0;
		*indexCorrelation = 0;
		*indexPages = 0;
		return;
	}

	MemSet(&costs, 0, sizeof(costs));

	indexRel = index_open(path->indexinfo->indexoid, NoLock);
	lists = IvfflatGetLists(indexRel);
	index_close(indexRel, NoLock);

	/* Get the ratio of lists that we need to visit */
	ratio = ((double) ivfflat_probes) / lists;
	if (ratio > 1.0)
		ratio = 1.0;

	/*
	 * This gives us the subset of tuples to visit. This value is passed into
	 * the generic cost estimator to determine the number of pages to visit
	 * during the index scan.
	 */
	costs.numIndexTuples = path->indexinfo->tuples * ratio;

#if PG_VERSION_NUM >= 120000
	genericcostestimate(root, path, loop_count, &costs);
#else
	qinfos = deconstruct_indexquals(path);
	genericcostestimate(root, path, loop_count, qinfos, &costs);
#endif

	get_tablespace_page_costs(path->indexinfo->reltablespace, NULL, &spc_seq_page_cost);

	/* Adjust cost if needed since TOAST not included in seq scan cost */
	if (costs.numIndexPages > path->indexinfo->rel->pages && ratio < 0.5)
	{
		/* Change all page cost from random to sequential */
		costs.indexTotalCost -= costs.numIndexPages * (costs.spc_random_page_cost - spc_seq_page_cost);

		/* Remove cost of extra pages */
		costs.indexTotalCost -= (costs.numIndexPages - path->indexinfo->rel->pages) * spc_seq_page_cost;
	}
	else
	{
		/* Change some page cost from random to sequential */
		costs.indexTotalCost -= 0.5 * costs.numIndexPages * (costs.spc_random_page_cost - spc_seq_page_cost);
	}

	/*
	 * If the list selectivity is lower than what is returned from the generic
	 * cost estimator, use that.
	 */
	if (ratio < costs.indexSelectivity)
		costs.indexSelectivity = ratio;

	/* Use total cost since most work happens before first tuple is returned */
	*indexStartupCost = costs.indexTotalCost;
	*indexTotalCost = costs.indexTotalCost;
	*indexSelectivity = costs.indexSelectivity;
	*indexCorrelation = costs.indexCorrelation;
	*indexPages = costs.numIndexPages;
}

/*
 * Parse and validate the reloptions
 */
static bytea *
ivfflatoptions(Datum reloptions, bool validate)
{
	static const relopt_parse_elt tab[] = {
		{"lists", RELOPT_TYPE_INT, offsetof(IvfflatOptions, lists)},
	};

#if PG_VERSION_NUM >= 130000
	return (bytea *) build_reloptions(reloptions, validate,
									  ivfflat_relopt_kind,
									  sizeof(IvfflatOptions),
									  tab, lengthof(tab));
#else
	relopt_value *options;
	int			numoptions;
	IvfflatOptions *rdopts;

	options = parseRelOptions(reloptions, validate, ivfflat_relopt_kind, &numoptions);
	rdopts = allocateReloptStruct(sizeof(IvfflatOptions), options, numoptions);
	fillRelOptions((void *) rdopts, sizeof(IvfflatOptions), options, numoptions,
				   validate, tab, lengthof(tab));

	return (bytea *) rdopts;
#endif
}

/*
 * Validate catalog entries for the specified operator class
 */
static bool
ivfflatvalidate(Oid opclassoid)
{
	return true;
}

/*
 * Define index handler
 *
 * See https://www.postgresql.org/docs/current/index-api.html
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(ivfflathandler);
Datum
ivfflathandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = 4;
#if PG_VERSION_NUM >= 130000
	amroutine->amoptsprocnum = 0;
#endif
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = true;
	amroutine->amcanbackward = false;	/* can change direction mid-scan */
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = false;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = false;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = false;
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
#if PG_VERSION_NUM >= 130000
	amroutine->amusemaintenanceworkmem = false; /* not used during VACUUM */
	amroutine->amparallelvacuumoptions = VACUUM_OPTION_PARALLEL_BULKDEL;
#endif
	amroutine->amkeytype = InvalidOid;

	/* Interface functions */
	amroutine->ambuild = ivfflatbuild;
	amroutine->ambuildempty = ivfflatbuildempty;
	amroutine->aminsert = ivfflatinsert;
	amroutine->ambulkdelete = ivfflatbulkdelete;
	amroutine->amvacuumcleanup = ivfflatvacuumcleanup;
	amroutine->amcanreturn = NULL;	/* tuple not included in heapsort */
	amroutine->amcostestimate = ivfflatcostestimate;
	amroutine->amoptions = ivfflatoptions;
	amroutine->amproperty = NULL;	/* TODO AMPROP_DISTANCE_ORDERABLE */
#if PG_VERSION_NUM >= 120000
	amroutine->ambuildphasename = ivfflatbuildphasename;
#endif
	amroutine->amvalidate = ivfflatvalidate;
#if PG_VERSION_NUM >= 140000
	amroutine->amadjustmembers = NULL;
#endif
	amroutine->ambeginscan = ivfflatbeginscan;
	amroutine->amrescan = ivfflatrescan;
	amroutine->amgettuple = ivfflatgettuple;
	amroutine->amgetbitmap = NULL;
	amroutine->amendscan = ivfflatendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;

	/* Interface functions to support parallel index scans */
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;

	PG_RETURN_POINTER(amroutine);
}
#endif
