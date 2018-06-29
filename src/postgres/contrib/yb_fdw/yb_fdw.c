/*-------------------------------------------------------------------------
 *
 * yb_fdw.c
 *		  foreign-data wrapper for accessing YugaByte tables
 *
 * Portions Copyright (c) YugaByte, Inc.
 * Copyright (c) 2010-2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  contrib/yb_fdw/yb_fdw.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/stat.h>
#include <unistd.h>

#include "access/htup_details.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "catalog/pg_foreign_table.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "commands/vacuum.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/var.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/sampling.h"

#include "yb/util/ybc_util.h"

PG_MODULE_MAGIC;

/*
 * Describes the valid options for objects that use this wrapper.
 */
struct YbFdwOption
{
	const char *optname;
	Oid			optcontext;		/* Oid of catalog in which option may appear */
};

/*
 * Valid options for yb_fdw.
 * These options are based on the options for the COPY FROM command.
 * But note that force_not_null and force_null are handled as boolean options
 * attached to a column, not as table options.
 */
static const struct YbFdwOption valid_options[] = {
	/* Sentinel */
	{NULL, InvalidOid}
};

/*
 * FDW-specific information for RelOptInfo.fdw_private.
 */
typedef struct YbFdwPlanState
{
	BlockNumber pages;			/* estimate of file's physical size */
	double		ntuples;		/* estimate of number of data rows */
} YbFdwPlanState;

/*
 * FDW-specific information for ForeignScanState.fdw_state.
 */
typedef struct YbFdwExecutionState
{
} YbFdwExecutionState;

/*
 * SQL functions
 */
PG_FUNCTION_INFO_V1(yb_fdw_handler);
PG_FUNCTION_INFO_V1(yb_fdw_validator);

/*
 * FDW callback routines
 */
static void ybGetForeignRelSize(PlannerInfo *root,
					  RelOptInfo *baserel,
					  Oid foreigntableid);
static void ybGetForeignPaths(PlannerInfo *root,
					RelOptInfo *baserel,
					Oid foreigntableid);
static ForeignScan *ybGetForeignPlan(PlannerInfo *root,
				   RelOptInfo *baserel,
				   Oid foreigntableid,
				   ForeignPath *best_path,
				   List *tlist,
				   List *scan_clauses,
				   Plan *outer_plan);
static void ybExplainForeignScan(ForeignScanState *node, ExplainState *es);
static void ybBeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *ybIterateForeignScan(ForeignScanState *node);
static void ybReScanForeignScan(ForeignScanState *node);
static void ybEndForeignScan(ForeignScanState *node);
static bool ybAnalyzeForeignTable(Relation relation,
						AcquireSampleRowsFunc *func,
						BlockNumber *totalpages);
static bool ybIsForeignScanParallelSafe(PlannerInfo *root, RelOptInfo *rel,
							  RangeTblEntry *rte);

/*
 * Helper functions
 */
static bool is_valid_option(const char *option, Oid context);
static List *get_yb_fdw_attribute_options(Oid relid);
static void estimate_size(PlannerInfo *root, RelOptInfo *baserel,
			  YbFdwPlanState *fdw_private);
static void estimate_costs(PlannerInfo *root, RelOptInfo *baserel,
			   YbFdwPlanState *fdw_private,
			   Cost *startup_cost, Cost *total_cost);
static int yb_acquire_sample_rows(Relation onerel, int elevel,
						 HeapTuple *rows, int targrows,
						 double *totalrows, double *totaldeadrows);
// ----------------------------------------------------------------------------
// Utility functions

#define YB_FDW_LOG_FUNCTION_ENTRY() \
	YBCLogInfo("Entering function %s", __FUNCTION__)

// ----------------------------------------------------------------------------
// Implementation

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to my callback routines.
 */
Datum
yb_fdw_handler(PG_FUNCTION_ARGS)
{
	YB_FDW_LOG_FUNCTION_ENTRY();
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	fdwroutine->GetForeignRelSize = ybGetForeignRelSize;
	fdwroutine->GetForeignPaths = ybGetForeignPaths;
	fdwroutine->GetForeignPlan = ybGetForeignPlan;
	fdwroutine->ExplainForeignScan = ybExplainForeignScan;
	fdwroutine->BeginForeignScan = ybBeginForeignScan;
	fdwroutine->IterateForeignScan = ybIterateForeignScan;
	fdwroutine->ReScanForeignScan = ybReScanForeignScan;
	fdwroutine->EndForeignScan = ybEndForeignScan;
	fdwroutine->AnalyzeForeignTable = ybAnalyzeForeignTable;
	fdwroutine->IsForeignScanParallelSafe = ybIsForeignScanParallelSafe;

	PG_RETURN_POINTER(fdwroutine);
}

/*
 * Validate the generic options given to a FOREIGN DATA WRAPPER, SERVER,
 * USER MAPPING or FOREIGN TABLE that uses yb_fdw.
 *
 * Raise an ERROR if the option or its value is considered invalid.
 */
Datum
yb_fdw_validator(PG_FUNCTION_ARGS)
{
	YB_FDW_LOG_FUNCTION_ENTRY();
	List	   *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
	Oid			catalog = PG_GETARG_OID(1);
	DefElem	*force_not_null = NULL;
	DefElem	*force_null = NULL;
	List	   *other_options = NIL;
	ListCell   *cell;

	/*
	 * Only superusers are allowed to set options of a yb_fdw foreign table.
	 *
	 * Putting this sort of permissions check in a validator is a bit of a
	 * crock, but there doesn't seem to be any other place that can enforce
	 * the check more cleanly.
	 */
	if (catalog == ForeignTableRelationId && !superuser())
		ereport(
			ERROR,
			(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			 errmsg("only superuser can change options of a yb_fdw foreign "
					"table")));

	PG_RETURN_VOID();
}

/*
 * Check if the provided option is one of the valid options.
 * context is the Oid of the catalog holding the object the option is for.
 */
static bool
is_valid_option(const char *option, Oid context)
{
	const struct YbFdwOption *opt;

	for (opt = valid_options; opt->optname; opt++)
	{
		if (context == opt->optcontext && strcmp(opt->optname, option) == 0)
			return true;
	}
	return false;
}

/*
 * Retrieve per-column generic options from pg_attribute and construct a list
 * of DefElems representing them.
 *
 * At the moment we only have "force_not_null", and "force_null",
 * which should each be combined into a single DefElem listing all such
 * columns, since that's what COPY expects.
 */
static List *
get_yb_fdw_attribute_options(Oid relid)
{
	Relation	rel;
	TupleDesc	tupleDesc;
	AttrNumber	natts;
	AttrNumber	attnum;
	List	   *fnncolumns = NIL;
	List	   *fncolumns = NIL;

	List	   *options = NIL;

	rel = heap_open(relid, AccessShareLock);
	tupleDesc = RelationGetDescr(rel);
	natts = tupleDesc->natts;

	/* Retrieve FDW options for all user-defined attributes. */
	for (attnum = 1; attnum <= natts; attnum++)
	{
		Form_pg_attribute attr = tupleDesc->attrs[attnum - 1];
		List	   *options;
		ListCell   *lc;

		/* Skip dropped attributes. */
		if (attr->attisdropped)
			continue;

		options = GetForeignColumnOptions(relid, attnum);
		foreach(lc, options)
		{
			DefElem	*def = (DefElem *) lfirst(lc);

			if (strcmp(def->defname, "force_not_null") == 0)
			{
				if (defGetBoolean(def))
				{
					char	   *attname = pstrdup(NameStr(attr->attname));

					fnncolumns = lappend(fnncolumns, makeString(attname));
				}
			}
			else if (strcmp(def->defname, "force_null") == 0)
			{
				if (defGetBoolean(def))
				{
					char	   *attname = pstrdup(NameStr(attr->attname));

					fncolumns = lappend(fncolumns, makeString(attname));
				}
			}
			/* maybe in future handle other options here */
		}
	}

	heap_close(rel, AccessShareLock);

	/*
	 * Return DefElem only when some column(s) have force_not_null /
	 * force_null options set
	 */
	if (fnncolumns != NIL)
		options = lappend(options, makeDefElem("force_not_null", (Node *) fnncolumns, -1));

	if (fncolumns != NIL)
		options = lappend(options, makeDefElem("force_null", (Node *) fncolumns, -1));

	return options;
}

/*
 * ybGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
static void
ybGetForeignRelSize(PlannerInfo *root,
					  RelOptInfo *baserel,
					  Oid foreigntableid)
{
	YbFdwPlanState *fdw_private;

	YB_FDW_LOG_FUNCTION_ENTRY();

	/*
	 * We can fetch foreign data wrapper options here if needed.
	 * See how fileGetOptions is called in file_fdw as an example.
	 */
	fdw_private = (YbFdwPlanState *) palloc(sizeof(YbFdwPlanState));
	baserel->fdw_private = (void *) fdw_private;

	/* Estimate relation size */
	estimate_size(root, baserel, fdw_private);
}

/*
 * ybGetForeignPaths
 *		Create possible access paths for a scan on the foreign table
 *
 *		Currently we don't support any push-down feature, so there is only one
 *		possible access path, which simply returns all records in the order in
 *		the data file.
 */
static void
ybGetForeignPaths(PlannerInfo *root,
					RelOptInfo *baserel,
					Oid foreigntableid)
{
	YbFdwPlanState *fdw_private = (YbFdwPlanState *) baserel->fdw_private;
	Cost		startup_cost;
	Cost		total_cost;
	List	   *columns;
	List	   *coptions = NIL;

	YB_FDW_LOG_FUNCTION_ENTRY();

	/* Estimate costs */
	estimate_costs(root, baserel, fdw_private,
				   &startup_cost, &total_cost);

	/*
	 * Create a ForeignPath node and add it as only possible path.  We use the
	 * fdw_private list of the path to carry the convert_selectively option;
	 * it will be propagated into the fdw_private list of the Plan node.
	 */
	add_path(baserel, (Path *)
			 create_foreignscan_path(root, baserel,
									 NULL,	/* default pathtarget */
									 baserel->rows,
									 startup_cost,
									 total_cost,
									 NIL,	/* no pathkeys */
									 NULL,	/* no outer rel either */
									 NULL,	/* no extra plan */
									 coptions));

	/*
	 * If data file was sorted, and we knew it somehow, we could insert
	 * appropriate pathkeys into the ForeignPath node to tell the planner
	 * that.
	 */
}

/*
 * ybGetForeignPlan
 *			 Create a ForeignScan plan node for scanning the foreign table
 */
static ForeignScan *
ybGetForeignPlan(
	PlannerInfo *root,
	RelOptInfo *baserel,
	Oid foreigntableid,
	ForeignPath *best_path,
	List *tlist,
	List *scan_clauses,
	Plan *outer_plan)
{
	Index	scan_relid = baserel->relid;
	YB_FDW_LOG_FUNCTION_ENTRY();

	/*
	 * Original comment from file_fdw:
	 *
	 * We have no native ability to evaluate restriction clauses, so we just
	 * put all the scan_clauses into the plan node's qual list for the
	 * executor to check.  So all we have to do here is strip RestrictInfo
	 * nodes from the clauses and ignore pseudoconstants (which will be
	 * handled elsewhere).
	 *
	 * TODO: push some restriction clauses down into YB scan and drop them from
	 * the plan node's qual list.
	 */
	scan_clauses = extract_actual_clauses(
		scan_clauses,
		/* pseudoconstant */ false);

	/* Create the ForeignScan node */
	return make_foreignscan(
		tlist,
		scan_clauses,
		scan_relid,
		NIL,	/* no expressions to evaluate */
		best_path->fdw_private,
		NIL,	/* no custom tlist */
		NIL,	/* no remote quals */
		outer_plan);
}

/*
 * ybExplainForeignScan
 *		Produce extra output for EXPLAIN
 */
static void
ybExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
	YB_FDW_LOG_FUNCTION_ENTRY();
}

/*
 * ybBeginForeignScan
 *		Initiate access to the file by creating CopyState
 */
static void
ybBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *plan = (ForeignScan *) node->ss.ps.plan;
	List		*options;
	YbFdwExecutionState *festate;

	YB_FDW_LOG_FUNCTION_ENTRY();

	/*
	 * Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL.
	 */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	/* Add any options from the plan (currently only convert_selectively) */
	options = list_concat(options, plan->fdw_private);

	/*
	 * Save state in node->fdw_state.  We must save enough information to call
	 * BeginCopyFrom() again.
	 */
	festate = (YbFdwExecutionState *) palloc(sizeof(YbFdwExecutionState));

	node->fdw_state = (void *) festate;
}

/*
 * ybIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot *
ybIterateForeignScan(ForeignScanState *node)
{
	YbFdwExecutionState *festate = (YbFdwExecutionState *) node->fdw_state;
	TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
	bool		found;

	YB_FDW_LOG_FUNCTION_ENTRY();

	/*
	 * The protocol for loading a virtual tuple into a slot is first
	 * ExecClearTuple, then fill the values/isnull arrays, then
	 * ExecStoreVirtualTuple.  If we don't find another row in the file, we
	 * just skip the last step, leaving the slot empty as required.
	 *
	 * We can pass ExprContext = NULL because we read all columns from the
	 * file, so no need to evaluate default expressions.
	 *
	 * We can also pass tupleOid = NULL because we don't allow oids for
	 * foreign tables.
	 */
	ExecClearTuple(slot);
	found = false;
	if (found)
		ExecStoreVirtualTuple(slot);

	return slot;
}

/*
 * ybReScanForeignScan
 *		Rescan table, possibly with new parameters
 */
static void
ybReScanForeignScan(ForeignScanState *node)
{
	YbFdwExecutionState *festate = (YbFdwExecutionState *) node->fdw_state;

	YB_FDW_LOG_FUNCTION_ENTRY();
}

/*
 * ybEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void
ybEndForeignScan(ForeignScanState *node)
{
	YbFdwExecutionState *festate = (YbFdwExecutionState *) node->fdw_state;

	YB_FDW_LOG_FUNCTION_ENTRY();

	/* if festate is NULL, we are in EXPLAIN; nothing to do */
	if (festate) {
		// Can do some cleanup here, e.g. close the iterator.
	}
}

/*
 * ybAnalyzeForeignTable
 *		Test whether analyzing this foreign table is supported
 */
static bool
ybAnalyzeForeignTable(
	Relation relation,
	AcquireSampleRowsFunc *func,
	BlockNumber *totalpages)
{
	YB_FDW_LOG_FUNCTION_ENTRY();

	/*
	 * Must return at least 1 so that we can tell later on that
	 * pg_class.relpages is not default.
	 */
	*totalpages = 1;

	*func = yb_acquire_sample_rows;

	return true;
}

/*
 * ybIsForeignScanParallelSafe
 *		Running a YB scan in a parallel worker should work just the same as
 * 		reading it in the leader, so mark scans safe.
 */
static bool
ybIsForeignScanParallelSafe(
	PlannerInfo *root,
	RelOptInfo *rel,
	RangeTblEntry *rte)
{
	YB_FDW_LOG_FUNCTION_ENTRY();
	return true;
}

/*
 * Estimate size of a foreign table.
 *
 * The main result is returned in baserel->rows.  We also set
 * fdw_private->pages and fdw_private->ntuples for later use in the cost
 * calculation.
 */
static void
estimate_size(PlannerInfo *root, RelOptInfo *baserel,
			  YbFdwPlanState *fdw_private)
{
	BlockNumber pages;
	double		ntuples;
	double		nrows;

	/*
	 * Convert size to pages for use in I/O cost estimate later.
	 */
	fdw_private->pages = 0;

	/*
		* We have to fake it.  We back into this estimate using the
		* planner's idea of the relation width; which is bogus if not all
		* columns are being read, not to mention that the text representation
		* of a row probably isn't the same size as its internal
		* representation.  Possibly we could do something better, but the
		* real answer to anyone who complains is "ANALYZE" ...
		*/
	int			tuple_width;

	tuple_width = MAXALIGN(baserel->reltarget->width) +
		MAXALIGN(SizeofHeapTupleHeader);
	ntuples = 1;

	fdw_private->ntuples = ntuples;

	/*
	 * Now estimate the number of rows returned by the scan after applying the
	 * baserestrictinfo quals.
	 */
	nrows = ntuples *
		clauselist_selectivity(root,
							   baserel->baserestrictinfo,
							   0,
							   JOIN_INNER,
							   NULL);

	nrows = clamp_row_est(nrows);

	/* Save the output-rows estimate for the planner */
	baserel->rows = nrows;
}

/*
 * Estimate costs of scanning a foreign table.
 *
 * Results are returned in *startup_cost and *total_cost.
 */
static void
estimate_costs(PlannerInfo *root, RelOptInfo *baserel,
			   YbFdwPlanState *fdw_private,
			   Cost *startup_cost, Cost *total_cost)
{
	BlockNumber pages = fdw_private->pages;
	double		ntuples = fdw_private->ntuples;
	Cost		run_cost = 0;
	Cost		cpu_per_tuple;

	/*
	 * We estimate costs almost the same way as cost_seqscan(), thus assuming
	 * that I/O costs are equivalent to a regular table file of the same size.
	 * However, we take per-tuple CPU costs as 10x of a seqscan, to account
	 * for the cost of an external YB scan. Need to refine this.
	 */
	run_cost += seq_page_cost * pages;

	*startup_cost = baserel->baserestrictcost.startup;
	cpu_per_tuple = cpu_tuple_cost * 10 + baserel->baserestrictcost.per_tuple;
	run_cost += cpu_per_tuple * ntuples;
	*total_cost = *startup_cost + run_cost;
}

/*
 * yb_acquire_sample_rows -- acquire a random sample of rows from the table
 *
 * TODO: figure out when we actually need it and implement it on YB side.
 */
static int
yb_acquire_sample_rows(
	Relation onerel, int elevel,
	HeapTuple *rows, int targrows,
	double *totalrows, double *totaldeadrows)
{
	int			numrows = 0;
	double		rowstoskip = -1;	/* -1 means not set yet */
	ReservoirStateData rstate;
	TupleDesc	tupDesc;
	Datum	   *values;
	bool	   *nulls;
	bool		found;
	List	   *options;
	ErrorContextCallback errcallback;
	MemoryContext oldcontext = CurrentMemoryContext;
	MemoryContext tupcontext;

	Assert(onerel);
	Assert(targrows > 0);

	tupDesc = RelationGetDescr(onerel);
	values = (Datum *) palloc(tupDesc->natts * sizeof(Datum));
	nulls = (bool *) palloc(tupDesc->natts * sizeof(bool));

	/*
	 * Use per-tuple memory context to prevent leak of memory used to read
	 * rows from the file with Copy routines.
	 */
	tupcontext = AllocSetContextCreate(CurrentMemoryContext,
									   "yb_fdw temporary context",
									   ALLOCSET_DEFAULT_SIZES);

	/* Prepare for sampling rows */
	reservoir_init_selection_state(&rstate, targrows);

	*totalrows = 0;
	*totaldeadrows = 0;

	/* Clean up. */
	MemoryContextDelete(tupcontext);

	pfree(values);
	pfree(nulls);

	/*
	 * Emit some interesting relation info
	 */
	ereport(elevel,
			(errmsg("\"%s\": file contains %.0f rows; "
					"%d rows in sample",
					RelationGetRelationName(onerel),
					*totalrows, numrows)));

	return numrows;
}
