/*-------------------------------------------------------------------------
 *
 * indxpath.c
 *	  Routines to determine which indexes are usable for scanning a
 *	  given relation, and create Paths accordingly.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/optimizer/path/indxpath.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>

#include "access/stratnum.h"
#include "access/sysattr.h"
#include "catalog/pg_am.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/supportnodes.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/prep.h"
#include "optimizer/restrictinfo.h"
#include "utils/lsyscache.h"
#include "utils/selfuncs.h"

/* Yugabyte includes */
#include "pg_yb_utils.h"
#include "access/yb_scan.h"
#include "catalog/pg_proc.h"
#include "executor/ybcExpr.h"
#include "optimizer/planmain.h"
#include "optimizer/tlist.h"
#include "utils/guc.h"
#include "utils/rel.h"

/* XXX see PartCollMatchesExprColl */
#define IndexCollMatchesExprColl(idxcollation, exprcollation) \
	((idxcollation) == InvalidOid || (idxcollation) == (exprcollation))

/* Whether we are looking for plain indexscan, bitmap scan, or either */
typedef enum
{
	ST_INDEXSCAN,				/* must support amgettuple */
	ST_BITMAPSCAN,				/* must support amgetbitmap */
	ST_ANYSCAN					/* either is okay */
} ScanTypeControl;

/* Data structure for collecting qual clauses that match an index */
typedef struct
{
	bool		nonempty;		/* True if lists are not all empty */
	/* Lists of IndexClause nodes, one list per index column */
	List	   *indexclauses[INDEX_MAX_KEYS];
} IndexClauseSet;

/* Per-path data used within choose_bitmap_and() */
typedef struct
{
	Path	   *path;			/* IndexPath, BitmapAndPath, or BitmapOrPath */
	List	   *quals;			/* the WHERE clauses it uses */
	List	   *preds;			/* predicates of its partial index(es) */
	Bitmapset  *clauseids;		/* quals+preds represented as a bitmapset */
	bool		unclassifiable; /* has too many quals+preds to process? */
} PathClauseUsage;

/* Callback argument for ec_member_matches_indexcol */
typedef struct
{
	IndexOptInfo *index;		/* index we're considering */
	int			indexcol;		/* index column we want to match to */
} ec_member_matches_arg;


static void consider_index_join_clauses(PlannerInfo *root, RelOptInfo *rel,
										IndexOptInfo *index,
										IndexClauseSet *rclauseset,
										IndexClauseSet *jclauseset,
										IndexClauseSet *eclauseset,
										List **bitindexpaths);
static void consider_index_join_outer_rels(PlannerInfo *root, RelOptInfo *rel,
										   IndexOptInfo *index,
										   IndexClauseSet *rclauseset,
										   IndexClauseSet *jclauseset,
										   IndexClauseSet *eclauseset,
										   List **bitindexpaths,
										   List *indexjoinclauses,
										   int considered_clauses,
										   List **considered_relids);
static void get_join_index_paths(PlannerInfo *root, RelOptInfo *rel,
								 IndexOptInfo *index,
								 IndexClauseSet *rclauseset,
								 IndexClauseSet *jclauseset,
								 IndexClauseSet *eclauseset,
								 List **bitindexpaths,
								 Relids relids,
								 List **considered_relids);
static bool eclass_already_used(EquivalenceClass *parent_ec, Relids oldrelids,
								List *indexjoinclauses);
static bool bms_equal_any(Relids relids, List *relids_list);
static void get_index_paths(PlannerInfo *root, RelOptInfo *rel,
							IndexOptInfo *index, IndexClauseSet *clauses,
							List *yb_bitmap_idx_pushdowns,
							List **bitindexpaths);
static List *build_index_paths(PlannerInfo *root, RelOptInfo *rel,
							   IndexOptInfo *index, IndexClauseSet *clauses,
							   List *yb_bitmap_idx_pushdowns,
							   bool useful_predicate,
							   ScanTypeControl scantype,
							   bool *skip_nonnative_saop,
							   bool *skip_lower_saop);
static List *build_paths_for_OR(PlannerInfo *root, RelOptInfo *rel,
								List *clauses, List *other_clauses);
static List *generate_bitmap_or_paths(PlannerInfo *root, RelOptInfo *rel,
									  List *clauses, List *other_clauses);
static Path *choose_bitmap_and(PlannerInfo *root, RelOptInfo *rel,
							   List *paths);
static int	path_usage_comparator(const void *a, const void *b);
static Cost bitmap_scan_cost_est(PlannerInfo *root, RelOptInfo *rel,
								 Path *ipath);
static Cost yb_bitmap_scan_cost_est(PlannerInfo *root, RelOptInfo *rel,
					 Path *ipath);
static Cost bitmap_and_cost_est(PlannerInfo *root, RelOptInfo *rel,
								List *paths);
static PathClauseUsage *classify_index_clause_usage(Path *path,
													List **clauselist);
static void find_indexpath_quals(Path *bitmapqual, List **quals, List **preds);
static int	find_list_position(Node *node, List **nodelist);
static bool check_index_only(RelOptInfo *rel, IndexOptInfo *index);
static double adjust_rowcount_for_semijoins(PlannerInfo *root,
											Index cur_relid,
											Index outer_relid,
											double rowcount);
static double approximate_joinrel_size(PlannerInfo *root, Relids relids);
static void match_restriction_clauses_to_index(PlannerInfo *root,
											   IndexOptInfo *index,
											   IndexClauseSet *clauseset);
static void match_join_clauses_to_index(PlannerInfo *root,
										RelOptInfo *rel, IndexOptInfo *index,
										IndexClauseSet *clauseset,
										List **joinorclauses);
static void match_eclass_clauses_to_index(PlannerInfo *root,
										  IndexOptInfo *index,
										  IndexClauseSet *clauseset);
static void match_clauses_to_index(PlannerInfo *root,
								   List *clauses,
								   IndexOptInfo *index,
								   IndexClauseSet *clauseset,
								   List **yb_bitmap_idx_pushdowns);
static void match_clause_to_index(PlannerInfo *root,
								  RestrictInfo *rinfo,
								  IndexOptInfo *index,
								  IndexClauseSet *clauseset,
								  List **yb_bitmap_idx_pushdowns);
static IndexClause *match_clause_to_indexcol(PlannerInfo *root,
											 RestrictInfo *rinfo,
											 int indexcol,
											 IndexOptInfo *index);
static IndexClause *match_boolean_index_clause(PlannerInfo *root,
											   RestrictInfo *rinfo,
											   int indexcol, IndexOptInfo *index);
static IndexClause *match_opclause_to_indexcol(PlannerInfo *root,
											   RestrictInfo *rinfo,
											   int indexcol,
											   IndexOptInfo *index);
static IndexClause *match_funcclause_to_indexcol(PlannerInfo *root,
												 RestrictInfo *rinfo,
												 int indexcol,
												 IndexOptInfo *index);
static IndexClause *get_index_clause_from_support(PlannerInfo *root,
												  RestrictInfo *rinfo,
												  Oid funcid,
												  int indexarg,
												  int indexcol,
												  IndexOptInfo *index);
static IndexClause *match_saopclause_to_indexcol(PlannerInfo *root,
												 RestrictInfo *rinfo,
												 int indexcol,
												 IndexOptInfo *index);
static IndexClause *match_rowcompare_to_indexcol(PlannerInfo *root,
												 RestrictInfo *rinfo,
												 int indexcol,
												 IndexOptInfo *index);
static IndexClause *expand_indexqual_rowcompare(PlannerInfo *root,
												RestrictInfo *rinfo,
												int indexcol,
												IndexOptInfo *index,
												Oid expr_op,
												bool var_on_left);
static void match_pathkeys_to_index(IndexOptInfo *index, List *pathkeys,
									List **orderby_clauses_p,
									List **clause_columns_p);
static Expr *match_clause_to_ordering_op(IndexOptInfo *index,
										 int indexcol, Expr *clause, Oid pk_opfamily);
static bool ec_member_matches_indexcol(PlannerInfo *root, RelOptInfo *rel,
									   EquivalenceClass *ec, EquivalenceMember *em,
									   void *arg);
static bool is_hash_column_in_lsm_index(const IndexOptInfo* index, int columnIndex);
static bool yb_can_pushdown_distinct(PlannerInfo *root, IndexOptInfo *index);
static bool yb_can_pushdown_as_filter(IndexOptInfo *index, RestrictInfo *rinfo);

/*
 * create_index_paths()
 *	  Generate all interesting index paths for the given relation.
 *	  Candidate paths are added to the rel's pathlist (using add_path).
 *
 * To be considered for an index scan, an index must match one or more
 * restriction clauses or join clauses from the query's qual condition,
 * or match the query's ORDER BY condition, or have a predicate that
 * matches the query's qual condition.
 *
 * There are two basic kinds of index scans.  A "plain" index scan uses
 * only restriction clauses (possibly none at all) in its indexqual,
 * so it can be applied in any context.  A "parameterized" index scan uses
 * join clauses (plus restriction clauses, if available) in its indexqual.
 * When joining such a scan to one of the relations supplying the other
 * variables used in its indexqual, the parameterized scan must appear as
 * the inner relation of a nestloop join; it can't be used on the outer side,
 * nor in a merge or hash join.  In that context, values for the other rels'
 * attributes are available and fixed during any one scan of the indexpath.
 * YB: Values are not fixed in batched nested loop joins by nature of a batch.
 *
 * An IndexPath is generated and submitted to add_path() for each plain or
 * parameterized index scan this routine deems potentially interesting for
 * the current query.
 *
 * 'rel' is the relation for which we want to generate index paths
 *
 * Note: check_index_predicates() must have been run previously for this rel.
 *
 * Note: in cases involving LATERAL references in the relation's tlist, it's
 * possible that rel->lateral_relids is nonempty.  Currently, we include
 * lateral_relids into the parameterization reported for each path, but don't
 * take it into account otherwise.  The fact that any such rels *must* be
 * available as parameter sources perhaps should influence our choices of
 * index quals ... but for now, it doesn't seem worth troubling over.
 * In particular, comments below about "unparameterized" paths should be read
 * as meaning "unparameterized so far as the indexquals are concerned".
 */
void
create_index_paths(PlannerInfo *root, RelOptInfo *rel)
{
	List	   *indexpaths;
	List	   *bitindexpaths;
	List	   *bitjoinpaths;
	List	   *joinorclauses;
	IndexClauseSet rclauseset;
	IndexClauseSet jclauseset;
	IndexClauseSet eclauseset;
	ListCell   *lc;

	/* Skip the whole mess if no indexes */
	if (rel->indexlist == NIL)
		return;

	/* Bitmap paths are collected and then dealt with at the end */
	bitindexpaths = bitjoinpaths = joinorclauses = NIL;

	/* Examine each index in turn */
	foreach(lc, rel->indexlist)
	{
		IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);

		/* Protect limited-size array in IndexClauseSets */
		Assert(index->nkeycolumns <= INDEX_MAX_KEYS);

		/*
		 * Ignore partial indexes that do not match the query.
		 * (generate_bitmap_or_paths() might be able to do something with
		 * them, but that's of no concern here.)
		 */
		if (index->indpred != NIL && !index->predOK)
			continue;

		/*
		 * Identify the restriction clauses that can match the index.
		 */
		MemSet(&rclauseset, 0, sizeof(rclauseset));
		match_restriction_clauses_to_index(root, index, &rclauseset);

		/*
		 * Build index paths from the restriction clauses.  These will be
		 * non-parameterized paths.  Plain paths go directly to add_path(),
		 * bitmap paths are added to bitindexpaths to be handled below.
		 */
		get_index_paths(root, rel, index, &rclauseset,
						NIL /* yb_bitmap_idx_pushdowns */,
						&bitindexpaths);

		/*
		 * Identify the join clauses that can match the index.  For the moment
		 * we keep them separate from the restriction clauses.  Note that this
		 * step finds only "loose" join clauses that have not been merged into
		 * EquivalenceClasses.  Also, collect join OR clauses for later.
		 */
		MemSet(&jclauseset, 0, sizeof(jclauseset));
		match_join_clauses_to_index(root, rel, index,
									&jclauseset, &joinorclauses);

		/*
		 * Look for EquivalenceClasses that can generate joinclauses matching
		 * the index.
		 */
		MemSet(&eclauseset, 0, sizeof(eclauseset));
		match_eclass_clauses_to_index(root, index,
									  &eclauseset);

		/*
		 * If we found any plain or eclass join clauses, build parameterized
		 * index paths using them.
		 */
		if (jclauseset.nonempty || eclauseset.nonempty)
			consider_index_join_clauses(root, rel, index,
										&rclauseset,
										&jclauseset,
										&eclauseset,
										&bitjoinpaths);
	}

	/*
	 * Generate BitmapOrPaths for any suitable OR-clauses present in the
	 * restriction list.  Add these to bitindexpaths.
	 */
	indexpaths = generate_bitmap_or_paths(root, rel,
										  rel->baserestrictinfo, NIL);
	bitindexpaths = list_concat(bitindexpaths, indexpaths);

	/*
	 * Likewise, generate BitmapOrPaths for any suitable OR-clauses present in
	 * the joinclause list.  Add these to bitjoinpaths.
	 */
	indexpaths = generate_bitmap_or_paths(root, rel,
										  joinorclauses, rel->baserestrictinfo);
	bitjoinpaths = list_concat(bitjoinpaths, indexpaths);

	/*
	 * If we found anything usable, generate a BitmapHeapPath for the most
	 * promising combination of restriction bitmap index paths.  Note there
	 * will be only one such path no matter how many indexes exist.  This
	 * should be sufficient since there's basically only one figure of merit
	 * (total cost) for such a path.
	 */
	if (bitindexpaths != NIL)
	{
		Path *bitmapqual;
		bitmapqual = choose_bitmap_and(root, rel, bitindexpaths);

		if (IsYugaByteEnabled() && rel->is_yb_relation)
		{
			if (yb_enable_bitmapscan)
			{
				YbBitmapTablePath *bpath;
				bpath = create_yb_bitmap_table_path(root, rel, bitmapqual,
													rel->lateral_relids, 1.0,
													0);
				add_path(rel, (Path *) bpath);

				/* TODO(#20575): support parallel bitmap scans */
			}
		}
		else
		{
			BitmapHeapPath *bpath;
			bpath = create_bitmap_heap_path(root, rel, bitmapqual,
											rel->lateral_relids, 1.0, 0);
			add_path(rel, (Path *) bpath);

			/* create a partial bitmap heap path */
			if (rel->consider_parallel && rel->lateral_relids == NULL)
				create_partial_bitmap_paths(root, rel, bitmapqual);
		}
	}

	/*
	 * Likewise, if we found anything usable, generate BitmapHeapPaths for the
	 * most promising combinations of join bitmap index paths.  Our strategy
	 * is to generate one such path for each distinct parameterization seen
	 * among the available bitmap index paths.  This may look pretty
	 * expensive, but usually there won't be very many distinct
	 * parameterizations.  (This logic is quite similar to that in
	 * consider_index_join_clauses, but we're working with whole paths not
	 * individual clauses.)
	 */
	if (bitjoinpaths != NIL)
	{
		List	   *all_path_outers;
		ListCell   *lc;

		/* Identify each distinct parameterization seen in bitjoinpaths */
		all_path_outers = NIL;
		foreach(lc, bitjoinpaths)
		{
			Path	   *path = (Path *) lfirst(lc);
			Relids		required_outer = PATH_REQ_OUTER(path);

			if (!bms_equal_any(required_outer, all_path_outers))
				all_path_outers = lappend(all_path_outers, required_outer);
		}

		/* Now, for each distinct parameterization set ... */
		foreach(lc, all_path_outers)
		{
			Relids		max_outers = (Relids) lfirst(lc);
			List	   *this_path_set;
			Path	   *bitmapqual;
			Relids		required_outer;
			double		loop_count;
			Path	   *bpath;
			ListCell   *lcp;

			/* Identify all the bitmap join paths needing no more than that */
			this_path_set = NIL;
			foreach(lcp, bitjoinpaths)
			{
				Path	   *path = (Path *) lfirst(lcp);

				if (bms_is_subset(PATH_REQ_OUTER(path), max_outers))
					this_path_set = lappend(this_path_set, path);
			}

			/*
			 * Add in restriction bitmap paths, since they can be used
			 * together with any join paths.
			 */
			this_path_set = list_concat(this_path_set, bitindexpaths);

			/* Select best AND combination for this parameterization */
			bitmapqual = choose_bitmap_and(root, rel, this_path_set);

			/* And push that path into the mix */
			required_outer = PATH_REQ_OUTER(bitmapqual);
			loop_count = get_loop_count(root, rel->relid, required_outer);

			if (IsYugaByteEnabled() && rel->is_yb_relation)
			{
				if (yb_enable_bitmapscan)
				{
					bpath = (Path *) create_yb_bitmap_table_path(root, rel,
																bitmapqual,
																required_outer,
																loop_count, 0);
					add_path(rel, bpath);
				}
			}
			else
			{
				bpath = (Path *) create_bitmap_heap_path(root, rel, bitmapqual,
														 required_outer,
														 loop_count, 0);
				add_path(rel, bpath);
			}
		}
	}
}

/*
 * consider_index_join_clauses
 *	  Given sets of join clauses for an index, decide which parameterized
 *	  index paths to build.
 *
 * Plain indexpaths are sent directly to add_path, while potential
 * bitmap indexpaths are added to *bitindexpaths for later processing.
 *
 * 'rel' is the index's heap relation
 * 'index' is the index for which we want to generate paths
 * 'rclauseset' is the collection of indexable restriction clauses
 * 'jclauseset' is the collection of indexable simple join clauses
 * 'eclauseset' is the collection of indexable clauses from EquivalenceClasses
 * '*bitindexpaths' is the list to add bitmap paths to
 */
static void
consider_index_join_clauses(PlannerInfo *root, RelOptInfo *rel,
							IndexOptInfo *index,
							IndexClauseSet *rclauseset,
							IndexClauseSet *jclauseset,
							IndexClauseSet *eclauseset,
							List **bitindexpaths)
{
	int			considered_clauses = 0;
	List	   *considered_relids = NIL;
	int			indexcol;

	/*
	 * The strategy here is to identify every potentially useful set of outer
	 * rels that can provide indexable join clauses.  For each such set,
	 * select all the join clauses available from those outer rels, add on all
	 * the indexable restriction clauses, and generate plain and/or bitmap
	 * index paths for that set of clauses.  This is based on the assumption
	 * that it's always better to apply a clause as an indexqual than as a
	 * filter (qpqual); which is where an available clause would end up being
	 * applied if we omit it from the indexquals.
	 *
	 * This looks expensive, but in most practical cases there won't be very
	 * many distinct sets of outer rels to consider.  As a safety valve when
	 * that's not true, we use a heuristic: limit the number of outer rel sets
	 * considered to a multiple of the number of clauses considered.  (We'll
	 * always consider using each individual join clause, though.)
	 *
	 * For simplicity in selecting relevant clauses, we represent each set of
	 * outer rels as a maximum set of clause_relids --- that is, the indexed
	 * relation itself is also included in the relids set.  considered_relids
	 * lists all relids sets we've already tried.
	 */
	for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
	{
		/* Consider each applicable simple join clause */
		considered_clauses += list_length(jclauseset->indexclauses[indexcol]);
		consider_index_join_outer_rels(root, rel, index,
									   rclauseset, jclauseset, eclauseset,
									   bitindexpaths,
									   jclauseset->indexclauses[indexcol],
									   considered_clauses,
									   &considered_relids);
		/* Consider each applicable eclass join clause */
		considered_clauses += list_length(eclauseset->indexclauses[indexcol]);
		consider_index_join_outer_rels(root, rel, index,
									   rclauseset, jclauseset, eclauseset,
									   bitindexpaths,
									   eclauseset->indexclauses[indexcol],
									   considered_clauses,
									   &considered_relids);
	}
}

/*
 * consider_index_join_outer_rels
 *	  Generate parameterized paths based on clause relids in the clause list.
 *
 * Workhorse for consider_index_join_clauses; see notes therein for rationale.
 *
 * 'rel', 'index', 'rclauseset', 'jclauseset', 'eclauseset', and
 *		'bitindexpaths' as above
 * 'indexjoinclauses' is a list of IndexClauses for join clauses
 * 'considered_clauses' is the total number of clauses considered (so far)
 * '*considered_relids' is a list of all relids sets already considered
 */
static void
consider_index_join_outer_rels(PlannerInfo *root, RelOptInfo *rel,
							   IndexOptInfo *index,
							   IndexClauseSet *rclauseset,
							   IndexClauseSet *jclauseset,
							   IndexClauseSet *eclauseset,
							   List **bitindexpaths,
							   List *indexjoinclauses,
							   int considered_clauses,
							   List **considered_relids)
{
	ListCell   *lc;

	/* Examine relids of each joinclause in the given list */
	foreach(lc, indexjoinclauses)
	{
		IndexClause *iclause = (IndexClause *) lfirst(lc);
		Relids		clause_relids = iclause->rinfo->clause_relids;
		EquivalenceClass *parent_ec = iclause->rinfo->parent_ec;
		int			num_considered_relids;

		/* If we already tried its relids set, no need to do so again */
		if (bms_equal_any(clause_relids, *considered_relids))
			continue;

		/*
		 * Generate the union of this clause's relids set with each
		 * previously-tried set.  This ensures we try this clause along with
		 * every interesting subset of previous clauses.  However, to avoid
		 * exponential growth of planning time when there are many clauses,
		 * limit the number of relid sets accepted to 10 * considered_clauses.
		 *
		 * Note: get_join_index_paths appends entries to *considered_relids,
		 * but we do not need to visit such newly-added entries within this
		 * loop, so we don't use foreach() here.  No real harm would be done
		 * if we did visit them, since the subset check would reject them; but
		 * it would waste some cycles.
		 */
		num_considered_relids = list_length(*considered_relids);
		for (int pos = 0; pos < num_considered_relids; pos++)
		{
			Relids		oldrelids = (Relids) list_nth(*considered_relids, pos);

			/*
			 * If either is a subset of the other, no new set is possible.
			 * This isn't a complete test for redundancy, but it's easy and
			 * cheap.  get_join_index_paths will check more carefully if we
			 * already generated the same relids set.
			 */
			if (bms_subset_compare(clause_relids, oldrelids) != BMS_DIFFERENT)
				continue;

			/*
			 * If this clause was derived from an equivalence class, the
			 * clause list may contain other clauses derived from the same
			 * eclass.  We should not consider that combining this clause with
			 * one of those clauses generates a usefully different
			 * parameterization; so skip if any clause derived from the same
			 * eclass would already have been included when using oldrelids.
			 */
			if (parent_ec &&
				eclass_already_used(parent_ec, oldrelids,
									indexjoinclauses))
				continue;

			/*
			 * If the number of relid sets considered exceeds our heuristic
			 * limit, stop considering combinations of clauses.  We'll still
			 * consider the current clause alone, though (below this loop).
			 */
			if (list_length(*considered_relids) >= 10 * considered_clauses)
				break;

			/* OK, try the union set */
			get_join_index_paths(root, rel, index,
								 rclauseset, jclauseset, eclauseset,
								 bitindexpaths,
								 bms_union(clause_relids, oldrelids),
								 considered_relids);
		}

		/* Also try this set of relids by itself */
		get_join_index_paths(root, rel, index,
							 rclauseset, jclauseset, eclauseset,
							 bitindexpaths,
							 clause_relids,
							 considered_relids);
	}
}

static bool
yb_get_batched_index_paths(PlannerInfo *root, RelOptInfo *rel,
						   IndexOptInfo *index, IndexClauseSet *clauses,
						   List **bitindexpaths)
{
	List	   *indexpaths;
	bool		skip_nonnative_saop = false;
	bool		skip_lower_saop = false;
	ListCell   *lc;

	Relids batchedrelids = NULL;
	Relids unbatchablerelids = NULL;

	Bitmapset *batched_inner_attnos = NULL;

	List *batched_rinfos = NIL;

	Relids inner_relids = bms_make_singleton(index->rel->relid);

	bool batched_paths_added = false;

	Relids total_relids = NULL;

	for (size_t i = 0; i < INDEX_MAX_KEYS && clauses->nonempty; i++)
	{
		List *colclauses = clauses->indexclauses[i];
		foreach (lc, colclauses)
		{
			IndexClause *iclause = (IndexClause *) lfirst(lc);
			RestrictInfo *rinfo = iclause->rinfo;

			/*
			 * If we can batch up outer vars in rinfo then do so.
			 * We are prohibiting batching outer rels that have already
			 * been batched in the interest of simplicity for now.
			 */
			Relids outer_relids =
				bms_difference(rinfo->required_relids, inner_relids);
			RestrictInfo *tmp_batched = NULL;

			/* TODO: We don't support expression indexes yet. */
			if (index->indexkeys[i] != 0)
			{
				tmp_batched =
					yb_get_batched_restrictinfo(rinfo, outer_relids, inner_relids);
			}

			if (tmp_batched)
			{
				batchedrelids =
					bms_union(batchedrelids,
							  tmp_batched->right_relids);
				batched_rinfos = lappend(batched_rinfos, rinfo);

				Node *innervar = get_leftop(tmp_batched->clause);
				pull_varattnos(innervar,
							   index->rel->relid,
							   &batched_inner_attnos);
			}
			else
			{
				/*
				 * Couldn't batch this clause so its outer rels are not
				 * batchable.
				 */
				unbatchablerelids = bms_union(unbatchablerelids,
											  rinfo->clause_relids);
			}

			total_relids = bms_union(total_relids, outer_relids);
		}
	}

	total_relids = bms_union(total_relids, inner_relids);

	batchedrelids = bms_del_member(batchedrelids,
								   index->rel->relid);
	batchedrelids = bms_difference(batchedrelids, unbatchablerelids);

	/* See if we have any unbatchable filters. */
	List *pclauses = NIL;
	if (!bms_is_empty(batchedrelids)) {
		pclauses = generate_join_implied_equalities(root,
													bms_union(batchedrelids,
															  index->rel->relids),
													batchedrelids,
													rel);

		/*
		 * Anything in joininfo that can be pushed down to this scan
		 * will end up as a qpqual. Add these to the list of clauses
		 * to check for batchability.
		 */
		foreach(lc, rel->joininfo)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
			if (join_clause_is_movable_into(rinfo, rel->relids,
											total_relids))
				pclauses = lappend(pclauses, rinfo);
		}
	}

	Bitmapset *pclause_batched_inner_attnos = NULL;

	foreach(lc, pclauses)
	{
		RestrictInfo *rinfo = lfirst(lc);
		RestrictInfo *batched =
			yb_get_batched_restrictinfo(rinfo,
									 batchedrelids,
									 index->rel->relids);
		if (!bms_overlap(rinfo->clause_relids, batchedrelids))
			continue;

		Assert(bms_overlap(rinfo->clause_relids, batchedrelids));
		/*
		 * If an unbatchable clause involves a batched relid, stop that relid
		 * from being batched.
		 */
		if (!batched)
		{
			unbatchablerelids = bms_union(unbatchablerelids,
										  rinfo->clause_relids);
			continue;
		}

		/*
		 * Make sure we don't allow any clauses that involve a batched relid
		 * and an attno not in batched_inner_attnos.
		 */
		Assert(batched);
		Node *innervar = get_leftop(batched->clause);
		Bitmapset *attnos = NULL;
		pull_varattnos(innervar,
						index->rel->relid,
						&attnos);
		if (!bms_is_subset(attnos, batched_inner_attnos))
			unbatchablerelids = bms_union(unbatchablerelids,
											rinfo->clause_relids);

		/*
		 * Disabling batching the same inner attno twice for now.
		 * We don't do this check above this loop because it could be
		 * the case that there are batchable clauses that are qpquals
		 * for this index. See GHI #21954.
		 */
		if (bms_overlap(pclause_batched_inner_attnos, attnos))
			unbatchablerelids = bms_union(unbatchablerelids,
										  rinfo->clause_relids);

		pclause_batched_inner_attnos =
			bms_union(pclause_batched_inner_attnos, attnos);
		bms_free(attnos);
	}

	Assert(!bms_is_empty(unbatchablerelids) ||
		   bms_is_subset(pclause_batched_inner_attnos, batched_inner_attnos));

	bms_free(batched_inner_attnos);
	bms_free(pclause_batched_inner_attnos);
	bms_free(total_relids);

	unbatchablerelids = bms_del_member(unbatchablerelids,
									   index->rel->relid);
	batchedrelids = bms_difference(batchedrelids, unbatchablerelids);

	Assert(!root->yb_cur_batched_relids);
	root->yb_cur_batched_relids = batchedrelids;

	/*
	 * Build simple index paths using the clauses.  Allow ScalarArrayOpExpr
	 * clauses only if the index AM supports them natively, and skip any such
	 * clauses for index columns after the first (so that we produce ordered
	 * paths if possible).
	 */
	indexpaths = build_index_paths(root, rel,
								   index, clauses,
								   NIL /* yb_bitmap_idx_pushdowns */,
								   index->predOK,
								   ST_ANYSCAN,
								   &skip_nonnative_saop,
								   &skip_lower_saop);

	/*
	 * If we skipped any lower-order ScalarArrayOpExprs on an index with an AM
	 * that supports them, then try again including those clauses. This will
	 * produce paths with more selectivity but no ordering.
	 */
	if (skip_lower_saop)
	{
		indexpaths = list_concat(indexpaths,
								 build_index_paths(root, rel,
												   index, clauses,
												   NIL /* yb_bitmap_idx_pushdowns */,
												   index->predOK,
												   ST_ANYSCAN,
												   &skip_nonnative_saop,
												   NULL));
	}

	/*
	 * Submit all the ones that can form plain IndexScan plans to add_path. (A
	 * plain IndexPath can represent either a plain IndexScan or an
	 * IndexOnlyScan, but for our purposes here that distinction does not
	 * matter.  However, some of the indexes might support only bitmap scans,
	 * and those we mustn't submit to add_path here.)
	 *
	 * Also, pick out the ones that are usable as bitmap scans.  For that, we
	 * must discard indexes that don't support bitmap scans, and we also are
	 * only interested in paths that have some selectivity; we should discard
	 * anything that was generated solely for ordering purposes.
	 */
	foreach(lc, indexpaths)
	{
		IndexPath  *ipath = (IndexPath *) lfirst(lc);

		if (index->amhasgettuple)
		{
			batched_paths_added = true;
			add_path(rel, (Path *) ipath);
		}

		if (((index->amhasgetbitmap || index->yb_amhasgetbitmap) &&
			 !IsA(ipath, UpperUniquePath)) &&
			(ipath->path.pathkeys == NIL ||
			 ipath->indexselectivity < 1.0))
			*bitindexpaths = lappend(*bitindexpaths, ipath);
	}

	root->yb_cur_batched_relids = NULL;

	return batched_paths_added;
}

/*
 * get_join_index_paths
 *	  Generate index paths using clauses from the specified outer relations.
 *	  In addition to generating paths, relids is added to *considered_relids
 *	  if not already present.
 *
 * Workhorse for consider_index_join_clauses; see notes therein for rationale.
 *
 * 'rel', 'index', 'rclauseset', 'jclauseset', 'eclauseset',
 *		'bitindexpaths', 'considered_relids' as above
 * 'relids' is the current set of relids to consider (the target rel plus
 *		one or more outer rels)
 */
static void
get_join_index_paths(PlannerInfo *root, RelOptInfo *rel,
					 IndexOptInfo *index,
					 IndexClauseSet *rclauseset,
					 IndexClauseSet *jclauseset,
					 IndexClauseSet *eclauseset,
					 List **bitindexpaths,
					 Relids relids,
					 List **considered_relids)
{
	IndexClauseSet clauseset;
	int			indexcol;

	/* If we already considered this relids set, don't repeat the work */
	if (bms_equal_any(relids, *considered_relids))
		return;

	/* Identify indexclauses usable with this relids set */
	MemSet(&clauseset, 0, sizeof(clauseset));

	for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
	{
		ListCell   *lc;

		/* First find applicable simple join clauses */
		foreach(lc, jclauseset->indexclauses[indexcol])
		{
			IndexClause *iclause = (IndexClause *) lfirst(lc);

			if (bms_is_subset(iclause->rinfo->clause_relids, relids))
				clauseset.indexclauses[indexcol] =
					lappend(clauseset.indexclauses[indexcol], iclause);
		}

		/*
		 * Add applicable eclass join clauses.  The clauses generated for each
		 * column are redundant (cf generate_implied_equalities_for_column),
		 * so we need at most one.  This is the only exception to the general
		 * rule of using all available index clauses.
		 */
		foreach(lc, eclauseset->indexclauses[indexcol])
		{
			IndexClause *iclause = (IndexClause *) lfirst(lc);

			if (bms_is_subset(iclause->rinfo->clause_relids, relids))
			{
				clauseset.indexclauses[indexcol] =
					lappend(clauseset.indexclauses[indexcol], iclause);
				break;
			}
		}

		/* Add restriction clauses */
		clauseset.indexclauses[indexcol] =
			list_concat(clauseset.indexclauses[indexcol],
						rclauseset->indexclauses[indexcol]);

		if (clauseset.indexclauses[indexcol] != NIL)
			clauseset.nonempty = true;
	}

	/* We should have found something, else caller passed silly relids */
	Assert(clauseset.nonempty);

	/*
	 * YB: We collect batched paths first to prioritize them in the path queue.
	 */
	if (yb_bnl_batch_size > 1)
		yb_get_batched_index_paths(root, rel, index,&clauseset,
											bitindexpaths);

	/* Build index path(s) using the collected set of clauses */
	get_index_paths(root, rel, index, &clauseset,
					NIL /* yb_bitmap_idx_pushdowns */, bitindexpaths);

	/*
	 * Remember we considered paths for this set of relids.
	 */
	*considered_relids = lappend(*considered_relids, relids);
}

/*
 * eclass_already_used
 *		True if any join clause usable with oldrelids was generated from
 *		the specified equivalence class.
 */
static bool
eclass_already_used(EquivalenceClass *parent_ec, Relids oldrelids,
					List *indexjoinclauses)
{
	ListCell   *lc;

	foreach(lc, indexjoinclauses)
	{
		IndexClause *iclause = (IndexClause *) lfirst(lc);
		RestrictInfo *rinfo = iclause->rinfo;

		if (rinfo->parent_ec == parent_ec &&
			bms_is_subset(rinfo->clause_relids, oldrelids))
			return true;
	}
	return false;
}

/*
 * bms_equal_any
 *		True if relids is bms_equal to any member of relids_list
 *
 * Perhaps this should be in bitmapset.c someday.
 */
static bool
bms_equal_any(Relids relids, List *relids_list)
{
	ListCell   *lc;

	foreach(lc, relids_list)
	{
		if (bms_equal(relids, (Relids) lfirst(lc)))
			return true;
	}
	return false;
}


/*
 * get_index_paths
 *	  Given an index and a set of index clauses for it, construct IndexPaths.
 *
 * Plain indexpaths are sent directly to add_path, while potential
 * bitmap indexpaths are added to *bitindexpaths for later processing.
 *
 * YB: Conditions that could be pushed down to the index for this bitmap index
 * path are appended to yb_bitmap_idx_pushdowns. This function is sometimes
 * called for individual sub clauses of an OR clause, so it allows us to extract
 * useful conditions from the subclause without being concerned by the
 * pushability of the other sub clauses.
 *
 * This is a fairly simple frontend to build_index_paths().  Its reason for
 * existence is mainly to handle ScalarArrayOpExpr quals properly.  If the
 * index AM supports them natively, we should just include them in simple
 * index paths.  If not, we should exclude them while building simple index
 * paths, and then make a separate attempt to include them in bitmap paths.
 * Furthermore, we should consider excluding lower-order ScalarArrayOpExpr
 * quals so as to create ordered paths.
 */
static void
get_index_paths(PlannerInfo *root, RelOptInfo *rel,
				IndexOptInfo *index, IndexClauseSet *clauses,
				List *yb_bitmap_idx_pushdowns, List **bitindexpaths)
{
	List	   *indexpaths;
	bool		skip_nonnative_saop = false;
	bool		skip_lower_saop = false;
	ListCell   *lc;

	/*
	 * Build simple index paths using the clauses.  Allow ScalarArrayOpExpr
	 * clauses only if the index AM supports them natively, and skip any such
	 * clauses for index columns after the first (so that we produce ordered
	 * paths if possible).
	 */
	indexpaths = build_index_paths(root, rel,
								   index, clauses,
								   yb_bitmap_idx_pushdowns,
								   index->predOK,
								   ST_ANYSCAN,
								   &skip_nonnative_saop,
								   &skip_lower_saop);

	/*
	 * If we skipped any lower-order ScalarArrayOpExprs on an index with an AM
	 * that supports them, then try again including those clauses.  This will
	 * produce paths with more selectivity but no ordering.
	 */
	if (skip_lower_saop)
	{
		indexpaths = list_concat(indexpaths,
								 build_index_paths(root, rel,
												   index, clauses,
												   yb_bitmap_idx_pushdowns,
												   index->predOK,
												   ST_ANYSCAN,
												   &skip_nonnative_saop,
												   NULL));
	}

	/*
	 * Submit all the ones that can form plain IndexScan plans to add_path. (A
	 * plain IndexPath can represent either a plain IndexScan or an
	 * IndexOnlyScan, but for our purposes here that distinction does not
	 * matter.  However, some of the indexes might support only bitmap scans,
	 * and those we mustn't submit to add_path here.)
	 *
	 * Also, pick out the ones that are usable as bitmap scans.  For that, we
	 * must discard indexes that don't support bitmap scans, and we also are
	 * only interested in paths that have some selectivity; we should discard
	 * anything that was generated solely for ordering purposes.
	 */
	foreach(lc, indexpaths)
	{
		IndexPath  *ipath = (IndexPath *) lfirst(lc);

		if (index->amhasgettuple)
			add_path(rel, (Path *) ipath);

		if (((index->amhasgetbitmap || index->yb_amhasgetbitmap) &&
			 !IsA(ipath, UpperUniquePath)) &&
			(ipath->path.pathkeys == NIL ||
			 ipath->indexselectivity < 1.0))
			*bitindexpaths = lappend(*bitindexpaths, ipath);
	}

	/*
	 * If there were ScalarArrayOpExpr clauses that the index can't handle
	 * natively, generate bitmap scan paths relying on executor-managed
	 * ScalarArrayOpExpr.
	 */
	if (skip_nonnative_saop)
	{
		indexpaths = build_index_paths(root, rel,
									   index, clauses,
									   yb_bitmap_idx_pushdowns,
									   false,
									   ST_BITMAPSCAN,
									   NULL,
									   NULL);
		*bitindexpaths = list_concat(*bitindexpaths, indexpaths);
	}
}

/*
 * Return TRUE if yb_hash_code() is LHS input, FALSE otherwise.
 */
static bool yb_hash_code_on_left(ScalarArrayOpExpr *saop)
{
	Expr *leftop = (Expr *) linitial(saop->args);

	if (leftop && IsA(leftop, RelabelType))
		leftop = ((RelabelType *) leftop)->arg;

	Assert(leftop != NULL);

	return IsA(leftop, FuncExpr) && ((FuncExpr *) leftop)->funcid == YB_HASH_CODE_OID;
}

/*
 * build_index_paths
 *	  Given an index and a set of index clauses for it, construct zero
 *	  or more IndexPaths. It also constructs zero or more partial IndexPaths.
 *
 * We return a list of paths because (1) this routine checks some cases
 * that should cause us to not generate any IndexPath, and (2) in some
 * cases we want to consider both a forward and a backward scan, so as
 * to obtain both sort orders.  Note that the paths are just returned
 * to the caller and not immediately fed to add_path().
 *
 * At top level, useful_predicate should be exactly the index's predOK flag
 * (ie, true if it has a predicate that was proven from the restriction
 * clauses).  When working on an arm of an OR clause, useful_predicate
 * should be true if the predicate required the current OR list to be proven.
 * Note that this routine should never be called at all if the index has an
 * unprovable predicate.
 *
 * scantype indicates whether we want to create plain indexscans, bitmap
 * indexscans, or both.  When it's ST_BITMAPSCAN, we will not consider
 * index ordering while deciding if a Path is worth generating.
 *
 * If skip_nonnative_saop is non-NULL, we ignore ScalarArrayOpExpr clauses
 * unless the index AM supports them directly, and we set *skip_nonnative_saop
 * to true if we found any such clauses (caller must initialize the variable
 * to false).  If it's NULL, we do not ignore ScalarArrayOpExpr clauses.
 *
 * If skip_lower_saop is non-NULL, we ignore ScalarArrayOpExpr clauses for
 * non-first index columns, and we set *skip_lower_saop to true if we found
 * any such clauses (caller must initialize the variable to false).  If it's
 * NULL, we do not ignore non-first ScalarArrayOpExpr clauses, but they will
 * result in considering the scan's output to be unordered.
 *
 * YB: Apart from creating index paths for index predicates, and ordering
 * usecases, we also also create distinct index scan paths for queries where
 * the elements are expected to be distinct. Such scans allow us to skip
 * reading duplicate values, thus fetching fewer rows from the storage layer.
 *
 * 'rel' is the index's heap relation
 * 'index' is the index for which we want to generate paths
 * 'clauses' is the collection of indexable clauses (IndexClause nodes)
 * 'yb_bitmap_idx_pushdowns' is a set of pushable clauses for a bitmap index scan.
 *    These are extracted during bitmap planning and allow pushdowns that are
 *    not possible to determine at a later stage.
 * 'useful_predicate' indicates whether the index has a useful predicate
 * 'scantype' indicates whether we need plain or bitmap scan support
 * 'skip_nonnative_saop' indicates whether to accept SAOP if index AM doesn't
 * 'skip_lower_saop' indicates whether to accept non-first-column SAOP
 */
static List *
build_index_paths(PlannerInfo *root, RelOptInfo *rel,
				  IndexOptInfo *index, IndexClauseSet *clauses,
				  List *yb_bitmap_idx_pushdowns,
				  bool useful_predicate,
				  ScanTypeControl scantype,
				  bool *skip_nonnative_saop,
				  bool *skip_lower_saop)
{
	List	   *result = NIL;
	IndexPath  *ipath;
	List	   *index_clauses;
	Relids		outer_relids;
	double		loop_count;
	List	   *orderbyclauses;
	List	   *orderbyclausecols;
	List	   *index_pathkeys;
	List	   *useful_pathkeys;
	bool		found_lower_saop_clause;
	bool		pathkeys_possibly_useful;
	bool		index_is_ordered;
	bool		index_only_scan;
	int			indexcol;
	bool		yb_supports_distinct_pushdown;
	int			yb_distinct_prefixlen;
	int			yb_distinct_nkeys;

	/*
	 * Check that index supports the desired scan type(s)
	 */
	switch (scantype)
	{
		case ST_INDEXSCAN:
			if (!index->amhasgettuple)
				return NIL;
			break;
		case ST_BITMAPSCAN:
			if (!index->amhasgetbitmap && !index->yb_amhasgetbitmap)
				return NIL;
			break;
		case ST_ANYSCAN:
			/* either or both are OK */
			break;
	}

	/*
	 * YB: Avoid generating distinct index scans when not applicable.
	 */
	yb_supports_distinct_pushdown = yb_can_pushdown_distinct(root, index);

	/*
	 * 1. Collect the index clauses into a single list.
	 *
	 * In the resulting list, clauses are ordered by index key, so that the
	 * column numbers form a nondecreasing sequence.  (This order is depended
	 * on by btree and possibly other places.)  The list can be empty, if the
	 * index AM allows that.
	 *
	 * found_lower_saop_clause is set true if we accept a ScalarArrayOpExpr
	 * index clause for a non-first index column.  This prevents us from
	 * assuming that the scan result is ordered.  (Actually, the result is
	 * still ordered if there are equality constraints for all earlier
	 * columns, but it seems too expensive and non-modular for this code to be
	 * aware of that refinement.)
	 *
	 * We also build a Relids set showing which outer rels are required by the
	 * selected clauses.  Any lateral_relids are included in that, but not
	 * otherwise accounted for.
	 */
	index_clauses = NIL;
	found_lower_saop_clause = false;
	outer_relids = bms_copy(rel->lateral_relids);
	for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
	{
		ListCell   *lc;
		bool		found_clause;

		found_clause = false;
		foreach(lc, clauses->indexclauses[indexcol])
		{
			IndexClause *iclause = (IndexClause *) lfirst(lc);
			RestrictInfo *rinfo = iclause->rinfo;

			/* We might need to omit ScalarArrayOpExpr clauses */
			if (IsA(rinfo->clause, ScalarArrayOpExpr))
			{
				/*
				 * YB: Do not consider SAOP exprs with yb_hash_code() in the LHS as index clauses,
				 * e.g. "WHERE yb_hash_code(i) in (1, 2, 3)".
				*/
				if (yb_hash_code_on_left((ScalarArrayOpExpr *) (rinfo->clause)))
					continue;

				if (!index->amsearcharray)
				{
					if (skip_nonnative_saop)
					{
						/* Ignore because not supported by index */
						*skip_nonnative_saop = true;
						continue;
					}
					/* Caller had better intend this only for bitmap scan */
					Assert(scantype == ST_BITMAPSCAN);
				}
				/*
				 * YB: No reason to believe lower saop prevents ordering.
				 * LSM index uses skip based scan, a machinery that also
				 * enables distinct index scans.
				 * Moreover, LSM index supports scalar array ops as
				 * index clauses without sacrificing ordering.
				 */
				bool is_yb_index = IsYugaByteEnabled() &&
					index->relam == LSM_AM_OID;
				if (!is_yb_index && indexcol > 0)
				{
					if (skip_lower_saop)
					{
						/* Caller doesn't want to lose index ordering */
						*skip_lower_saop = true;
						continue;
					}
					found_lower_saop_clause = true;
				}
			}

			/* OK to include this clause */
			index_clauses = lappend(index_clauses, iclause);
			outer_relids = bms_add_members(outer_relids,
										   rinfo->clause_relids);
			found_clause = true;
		}

		/*
		 * If no clauses match the first index column, check for amoptionalkey
		 * restriction.  We can't generate a scan over an index with
		 * amoptionalkey = false unless there's at least one index clause.
		 * (When working on columns after the first, this test cannot fail. It
		 * is always okay for columns after the first to not have any
		 * clauses.)
		 */
		if (index_clauses == NIL && !index->amoptionalkey)
			return NIL;

		/*
		 * YB: Even though amoptionalkey of YB LSM indexes is true, hash
		 * columns are not optional in hash partioned indexes as they are
		 * required to determine the appropriate index partition.
		 */
		if (yb_supports_distinct_pushdown && !found_clause &&
			indexcol < index->nhashcolumns)
		{
			index_clauses = NIL;
			outer_relids = NULL;
			break;
		}
	}

	/* We do not want the index's rel itself listed in outer_relids */
	outer_relids = bms_del_member(outer_relids, rel->relid);
	/* Enforce convention that outer_relids is exactly NULL if empty */
	if (bms_is_empty(outer_relids))
		outer_relids = NULL;

	/* Compute loop_count for cost estimation purposes */
	loop_count = get_loop_count(root, rel->relid, outer_relids);

	/*
	 * 2. Compute pathkeys describing index's ordering, if any, then see how
	 * many of them are actually useful for this query.  This is not relevant
	 * if we are only trying to build bitmap indexscans, nor if we have to
	 * assume the scan is unordered.
	 */

	/*
	 * YB: LSM indexes support prefix based scans that can skip duplicate
	 * values of a prefix entirely. Here, a prefix refers to a contiguous list
	 * of leading columns on which the index is sorted. Such scans are called
	 * distinct index scans. These paths scan much fewer rows than the regular
	 * index scans for DISTINCT operations and thus can be more efficient for
	 * such purposes.
	 *
	 * YB: To generate such a scan,
	 * First, we compute the minimal prefix that encompasses all the keys
	 * that need to be distinct. Smaller prefixes lead to more efficient
	 * scans. However, a prefix that is smaller than necessary may lead to
	 * inaccurate results. The task here is to compute the right prefix length.
	 *
	 * YB: We use the sentinel value -1 to indicate that a distinct index scan
	 * is not possible/useful.
	 */
	if (yb_supports_distinct_pushdown)
		yb_distinct_prefixlen = yb_calculate_distinct_prefixlen(root, index,
																index_clauses);
	else
		yb_distinct_prefixlen = -1;

	pathkeys_possibly_useful = (scantype != ST_BITMAPSCAN &&
								!found_lower_saop_clause &&
								has_useful_pathkeys(root, rel));
	index_is_ordered = (index->sortopfamily != NULL);
	if (index_is_ordered && pathkeys_possibly_useful)
	{
		/*
		 * YB: A distinct index scan may sometimes return duplicate results
		 * for range partitioned LSM indexes by nature of the underlying
		 * architecture. This behavior necessitates that we stick a Unique
		 * node on top of the distinct index scan. However, the distinct index
		 * scan fetches distinct values of only a prefix of the index key
		 * columns. Hence, we need the set of the index pathkeys that
		 * corresponds to this prefix for the Unique node to do its work.
		 *
		 * YB: We need this set of pathkeys because ...
		 * Observe that, in the current architecture, the Unique node
		 * does not de-duplicate all the columns but only the columns specified
		 * by the PathKey's. This forces us to fetch the pathkeys corresponding
		 * to our distinct prefix. Moreover, we only fetch the number of
		 * pathkeys in 'yb_distinct_nkeys' and not the entire set of pathkeys
		 * since the resulting set is also a prefix.
		 *
		 * Example: SELECT DISTINCT r1, r2 WHERE r1 = r2
		 * 			yb_distinct_prefixlen = 2 (Scan prefix)
		 * 			yb_distinct_nkeys = 1 (num pathkeys for distinct).
		 *
		 * YB: Currently, yb_distinct_nkeys is required only for a
		 * range-partitioned index.
		 */
		yb_distinct_nkeys = index->nhashcolumns ? -1 : yb_distinct_prefixlen;
		index_pathkeys = build_index_pathkeys(root, index,
											  ForwardScanDirection,
											  &yb_distinct_nkeys);
		useful_pathkeys = truncate_useless_pathkeys(root, rel,
													index_pathkeys,
													yb_distinct_nkeys);
		orderbyclauses = NIL;
		orderbyclausecols = NIL;
	}
	else if (index->amcanorderbyop && pathkeys_possibly_useful)
	{
		/* see if we can generate ordering operators for query_pathkeys */
		match_pathkeys_to_index(index, root->query_pathkeys,
								&orderbyclauses,
								&orderbyclausecols);
		if (orderbyclauses)
			useful_pathkeys = root->query_pathkeys;
		else
			useful_pathkeys = NIL;
	}
	else
	{
		useful_pathkeys = NIL;
		orderbyclauses = NIL;
		orderbyclausecols = NIL;
		/* YB: Only relevant when all requested targets are constant. */
		yb_distinct_nkeys = 0;
	}

	/*
	 * 3. Check if an index-only scan is possible.  If we're not building
	 * plain indexscans, this isn't relevant since bitmap scans don't support
	 * index data retrieval anyway.
	 */
	index_only_scan = (scantype != ST_BITMAPSCAN &&
					   check_index_only(rel, index));

	/*
	 * 4. Generate an indexscan path if there are relevant restriction clauses
	 * in the current clauses, OR the index ordering is potentially useful for
	 * later merging or final output ordering, OR the index has a useful
	 * predicate, OR an index-only scan is possible.
	 *
	 * YB: We also generate distinct index paths if there is a valid distinct
	 * prefix, i.e. 'yb_distinct_prefixlen' >= 0.
	 */
	if (index_clauses != NIL || useful_pathkeys != NIL || useful_predicate ||
		index_only_scan || yb_distinct_prefixlen >= 0)
	{
		ipath = create_index_path(root, index,
								  index_clauses,
								  yb_bitmap_idx_pushdowns,
								  orderbyclauses,
								  orderbyclausecols,
								  useful_pathkeys,
								  index_is_ordered ?
								  ForwardScanDirection :
								  NoMovementScanDirection,
								  index_only_scan,
								  outer_relids,
								  loop_count,
								  false);

		/*
		 * YB: Generate a distinct index path.
		 * Must be done before generating parallel paths since ipath is reused.
		 */
		if (yb_distinct_prefixlen >= 0)
		{
			Path *distinct_index_path =
				yb_create_distinct_index_path(root, index, ipath,
											  yb_distinct_prefixlen,
											  yb_distinct_nkeys);
			result = lappend(result, distinct_index_path);
		}

		/*
		 * YB: This append can occur before or after generating the distinct
		 * index path above. Here, we choose to add the regular path after the
		 * distinct path since distinct paths are generally cheaper.
		 */
		result = lappend(result, ipath);

		/*
		 * If appropriate, consider parallel index scan.  We don't allow
		 * parallel index scan for bitmap index scans.
		 */
		if (index->amcanparallel &&
			rel->consider_parallel && outer_relids == NULL &&
			scantype != ST_BITMAPSCAN)
		{
			ipath = create_index_path(root, index,
									  index_clauses,
									  yb_bitmap_idx_pushdowns,
									  orderbyclauses,
									  orderbyclausecols,
									  useful_pathkeys,
									  index_is_ordered ?
									  ForwardScanDirection :
									  NoMovementScanDirection,
									  index_only_scan,
									  outer_relids,
									  loop_count,
									  true);

			/*
			 * if, after costing the path, we find that it's not worth using
			 * parallel workers, just free it.
			 */
			if (ipath->path.parallel_workers > 0)
				add_partial_path(rel, (Path *) ipath);
			else
				pfree(ipath);
		}
	}

	/*
	 * 5. If the index is ordered, a backwards scan might be interesting.
	 */
	if (index_is_ordered && pathkeys_possibly_useful)
	{
		/*
		 * YB: Currently, yb_distinct_nkeys is required only for a
		 * range-partitioned index.
		 */
		yb_distinct_nkeys = index->nhashcolumns ? -1 : yb_distinct_prefixlen;
		index_pathkeys = build_index_pathkeys(root, index,
											  BackwardScanDirection,
											  &yb_distinct_nkeys);
		useful_pathkeys = truncate_useless_pathkeys(root, rel,
													index_pathkeys,
													yb_distinct_nkeys);
		if (useful_pathkeys != NIL)
		{
			ipath = create_index_path(root, index,
									  index_clauses,
									  yb_bitmap_idx_pushdowns,
									  NIL,
									  NIL,
									  useful_pathkeys,
									  BackwardScanDirection,
									  index_only_scan,
									  outer_relids,
									  loop_count,
									  false);

			/*
			 * YB: Generate a backwards scanning distinct index path.
			 * Must be done before generating parallel path since ipath is
			 * reused.
			 */
			if (yb_distinct_prefixlen >= 0)
			{
				Path *distinct_index_path =
					yb_create_distinct_index_path(root, index, ipath,
												  yb_distinct_prefixlen,
												  yb_distinct_nkeys);
				result = lappend(result, distinct_index_path);
			}

			result = lappend(result, ipath);

			/* If appropriate, consider parallel index scan */
			if (index->amcanparallel &&
				rel->consider_parallel && outer_relids == NULL &&
				scantype != ST_BITMAPSCAN)
			{
				ipath = create_index_path(root, index,
										  index_clauses,
										  yb_bitmap_idx_pushdowns,
										  NIL,
										  NIL,
										  useful_pathkeys,
										  BackwardScanDirection,
										  index_only_scan,
										  outer_relids,
										  loop_count,
										  true);

				/*
				 * if, after costing the path, we find that it's not worth
				 * using parallel workers, just free it.
				 */
				if (ipath->path.parallel_workers > 0)
					add_partial_path(rel, (Path *) ipath);
				else
					pfree(ipath);
			}
		}
	}

	return result;
}

/*
 * build_paths_for_OR
 *	  Given a list of restriction clauses from one arm of an OR clause,
 *	  construct all matching IndexPaths for the relation.
 *
 * Here we must scan all indexes of the relation, since a bitmap OR tree
 * can use multiple indexes.
 *
 * The caller actually supplies two lists of restriction clauses: some
 * "current" ones and some "other" ones.  Both lists can be used freely
 * to match keys of the index, but an index must use at least one of the
 * "current" clauses to be considered usable.  The motivation for this is
 * examples like
 *		WHERE (x = 42) AND (... OR (y = 52 AND z = 77) OR ....)
 * While we are considering the y/z subclause of the OR, we can use "x = 42"
 * as one of the available index conditions; but we shouldn't match the
 * subclause to any index on x alone, because such a Path would already have
 * been generated at the upper level.  So we could use an index on x,y,z
 * or an index on x,y for the OR subclause, but not an index on just x.
 * When dealing with a partial index, a match of the index predicate to
 * one of the "current" clauses also makes the index usable.
 *
 * 'rel' is the relation for which we want to generate index paths
 * 'clauses' is the current list of clauses (RestrictInfo nodes)
 * 'other_clauses' is the list of additional upper-level clauses
 */
static List *
build_paths_for_OR(PlannerInfo *root, RelOptInfo *rel,
				   List *clauses, List *other_clauses)
{
	List	   *result = NIL;
	List	   *all_clauses = NIL;	/* not computed till needed */
	ListCell   *lc;

	foreach(lc, rel->indexlist)
	{
		IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);
		IndexClauseSet clauseset;
		List	   *yb_bitmap_idx_pushdowns = NIL;
		List	   *indexpaths;
		bool		useful_predicate;

		/* Ignore index if it doesn't support bitmap scans */
		if (!index->amhasgetbitmap && !index->yb_amhasgetbitmap)
			continue;

		/*
		 * Ignore partial indexes that do not match the query.  If a partial
		 * index is marked predOK then we know it's OK.  Otherwise, we have to
		 * test whether the added clauses are sufficient to imply the
		 * predicate. If so, we can use the index in the current context.
		 *
		 * We set useful_predicate to true iff the predicate was proven using
		 * the current set of clauses.  This is needed to prevent matching a
		 * predOK index to an arm of an OR, which would be a legal but
		 * pointlessly inefficient plan.  (A better plan will be generated by
		 * just scanning the predOK index alone, no OR.)
		 */
		useful_predicate = false;
		if (index->indpred != NIL)
		{
			if (index->predOK)
			{
				/* Usable, but don't set useful_predicate */
			}
			else
			{
				/* Form all_clauses if not done already */
				if (all_clauses == NIL)
					all_clauses = list_concat_copy(clauses, other_clauses);

				if (!predicate_implied_by(index->indpred, all_clauses, false))
					continue;	/* can't use it at all */

				if (!predicate_implied_by(index->indpred, other_clauses, false))
					useful_predicate = true;
			}
		}

		/*
		 * Identify the restriction clauses that can match the index.
		 * YB: Since clauses is the set of everything in this particular branch
		 * of a logical operation, we try to collect pushdown clauses from this
		 * list because we can't collect them later.
		 *
		 * For example, consider the parent clause ((q1 AND q2) OR q3).
		 *
		 * If q1 and q2 can be applied to this index, but q3 can't, then the
		 * full condition ((q1 AND q2) OR q3) can't be pushed down on this
		 * index. However, in a bitmap scan plan, a particular Bitmap Index Scan
		 * only needs to be concerned with the quals from its portion of the
		 * tree.
		 *
		 * This function is called recursively for each argument of the OR
		 * clause and builds an Index Path. At this point, we know exactly what
		 * conditions the bitmap index path should be concerned with and we can
		 * choose to push them down. The first bitmap index scan covers the
		 * (q1 AND q2) condition. In Yugabyte, we can use one as a pushdown
		 * clause, even if it's not a good index condition.
		 *
		 * Because this function operates recursively on subtrees of the
		 * original condition, it is easy to evaluate one clause of the OR
		 * separately from the others. Whether or not q3 is pushable for this
		 * index has no effect on whether q1 or q2 can be pushed down for the
		 * first bitmap index scan.
		 */
		MemSet(&clauseset, 0, sizeof(clauseset));
		match_clauses_to_index(root, clauses, index, &clauseset,
							   index->rel->is_yb_relation
									? &yb_bitmap_idx_pushdowns : NULL);

		/*
		 * If no matches so far, and the index predicate isn't useful, we
		 * don't want it.
		 */
		if (!clauseset.nonempty && !useful_predicate)
			continue;

		/*
		 * Add "other" restriction clauses to the clauseset.
		 * YB: We do not try to collect pushdown clauses for bitmap index scans
		 * here because those will be gathered from all clauses by
		 * create_indexscan_plan.
		 */
		match_clauses_to_index(root, other_clauses, index, &clauseset, NULL);

		/*
		 * Construct paths if possible.
		 */
		indexpaths = build_index_paths(root, rel,
									   index, &clauseset,
									   yb_bitmap_idx_pushdowns,
									   useful_predicate,
									   ST_BITMAPSCAN,
									   NULL,
									   NULL);
		result = list_concat(result, indexpaths);
	}

	return result;
}

/*
 * generate_bitmap_or_paths
 *		Look through the list of clauses to find OR clauses, and generate
 *		a BitmapOrPath for each one we can handle that way.  Return a list
 *		of the generated BitmapOrPaths.
 *
 * other_clauses is a list of additional clauses that can be assumed true
 * for the purpose of generating indexquals, but are not to be searched for
 * ORs.  (See build_paths_for_OR() for motivation.)
 */
static List *
generate_bitmap_or_paths(PlannerInfo *root, RelOptInfo *rel,
						 List *clauses, List *other_clauses)
{
	List	   *result = NIL;
	List	   *all_clauses;
	ListCell   *lc;

	if (IsYugaByteEnabled() && rel->is_yb_relation && !yb_enable_bitmapscan)
		return NIL;

	/*
	 * We can use both the current and other clauses as context for
	 * build_paths_for_OR; no need to remove ORs from the lists.
	 */
	all_clauses = list_concat_copy(clauses, other_clauses);

	foreach(lc, clauses)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);
		List	   *pathlist;
		Path	   *bitmapqual;
		ListCell   *j;

		/* Ignore RestrictInfos that aren't ORs */
		if (!restriction_is_or_clause(rinfo))
			continue;

		/*
		 * We must be able to match at least one index to each of the arms of
		 * the OR, else we can't use it.
		 */
		pathlist = NIL;
		foreach(j, ((BoolExpr *) rinfo->orclause)->args)
		{
			Node	   *orarg = (Node *) lfirst(j);
			List	   *indlist;

			/* OR arguments should be ANDs or sub-RestrictInfos */
			if (is_andclause(orarg))
			{
				List	   *andargs = ((BoolExpr *) orarg)->args;

				indlist = build_paths_for_OR(root, rel,
											 andargs,
											 all_clauses);

				/* Recurse in case there are sub-ORs */
				indlist = list_concat(indlist,
									  generate_bitmap_or_paths(root, rel,
															   andargs,
															   all_clauses));
			}
			else
			{
				RestrictInfo *rinfo = castNode(RestrictInfo, orarg);
				List	   *orargs;

				Assert(!restriction_is_or_clause(rinfo));
				orargs = list_make1(rinfo);

				indlist = build_paths_for_OR(root, rel,
											 orargs,
											 all_clauses);
			}

			/*
			 * If nothing matched this arm, we can't do anything with this OR
			 * clause.
			 */
			if (indlist == NIL)
			{
				pathlist = NIL;
				break;
			}

			/*
			 * OK, pick the most promising AND combination, and add it to
			 * pathlist.
			 */
			bitmapqual = choose_bitmap_and(root, rel, indlist);
			pathlist = lappend(pathlist, bitmapqual);
		}

		/*
		 * If we have a match for every arm, then turn them into a
		 * BitmapOrPath, and add to result list.
		 */
		if (pathlist != NIL)
		{
			bitmapqual = (Path *) create_bitmap_or_path(root, rel, pathlist);
			result = lappend(result, bitmapqual);
		}
	}

	return result;
}


/*
 * choose_bitmap_and
 *		Given a nonempty list of bitmap paths, AND them into one path.
 *
 * This is a nontrivial decision since we can legally use any subset of the
 * given path set.  We want to choose a good tradeoff between selectivity
 * and cost of computing the bitmap.
 *
 * The result is either a single one of the inputs, or a BitmapAndPath
 * combining multiple inputs.
 */
static Path *
choose_bitmap_and(PlannerInfo *root, RelOptInfo *rel, List *paths)
{
	int			npaths = list_length(paths);
	PathClauseUsage **pathinfoarray;
	PathClauseUsage *pathinfo;
	List	   *clauselist;
	List	   *bestpaths = NIL;
	Cost		bestcost = 0;
	int			i,
				j;
	ListCell   *l;

	Assert(npaths > 0);			/* else caller error */
	if (npaths == 1)
		return (Path *) linitial(paths);	/* easy case */

	/*
	 * In theory we should consider every nonempty subset of the given paths.
	 * In practice that seems like overkill, given the crude nature of the
	 * estimates, not to mention the possible effects of higher-level AND and
	 * OR clauses.  Moreover, it's completely impractical if there are a large
	 * number of paths, since the work would grow as O(2^N).
	 *
	 * As a heuristic, we first check for paths using exactly the same sets of
	 * WHERE clauses + index predicate conditions, and reject all but the
	 * cheapest-to-scan in any such group.  This primarily gets rid of indexes
	 * that include the interesting columns but also irrelevant columns.  (In
	 * situations where the DBA has gone overboard on creating variant
	 * indexes, this can make for a very large reduction in the number of
	 * paths considered further.)
	 *
	 * We then sort the surviving paths with the cheapest-to-scan first, and
	 * for each path, consider using that path alone as the basis for a bitmap
	 * scan.  Then we consider bitmap AND scans formed from that path plus
	 * each subsequent (higher-cost) path, adding on a subsequent path if it
	 * results in a reduction in the estimated total scan cost. This means we
	 * consider about O(N^2) rather than O(2^N) path combinations, which is
	 * quite tolerable, especially given than N is usually reasonably small
	 * because of the prefiltering step.  The cheapest of these is returned.
	 *
	 * We will only consider AND combinations in which no two indexes use the
	 * same WHERE clause.  This is a bit of a kluge: it's needed because
	 * costsize.c and clausesel.c aren't very smart about redundant clauses.
	 * They will usually double-count the redundant clauses, producing a
	 * too-small selectivity that makes a redundant AND step look like it
	 * reduces the total cost.  Perhaps someday that code will be smarter and
	 * we can remove this limitation.  (But note that this also defends
	 * against flat-out duplicate input paths, which can happen because
	 * match_join_clauses_to_index will find the same OR join clauses that
	 * extract_restriction_or_clauses has pulled OR restriction clauses out
	 * of.)
	 *
	 * For the same reason, we reject AND combinations in which an index
	 * predicate clause duplicates another clause.  Here we find it necessary
	 * to be even stricter: we'll reject a partial index if any of its
	 * predicate clauses are implied by the set of WHERE clauses and predicate
	 * clauses used so far.  This covers cases such as a condition "x = 42"
	 * used with a plain index, followed by a clauseless scan of a partial
	 * index "WHERE x >= 40 AND x < 50".  The partial index has been accepted
	 * only because "x = 42" was present, and so allowing it would partially
	 * double-count selectivity.  (We could use predicate_implied_by on
	 * regular qual clauses too, to have a more intelligent, but much more
	 * expensive, check for redundancy --- but in most cases simple equality
	 * seems to suffice.)
	 */

	/*
	 * Extract clause usage info and detect any paths that use exactly the
	 * same set of clauses; keep only the cheapest-to-scan of any such groups.
	 * The surviving paths are put into an array for qsort'ing.
	 */
	pathinfoarray = (PathClauseUsage **)
		palloc(npaths * sizeof(PathClauseUsage *));
	clauselist = NIL;
	npaths = 0;
	foreach(l, paths)
	{
		Path	   *ipath = (Path *) lfirst(l);

		/* TODO(#21039): Support Distinct Bitmap Scans */
		if (IsA(ipath, UpperUniquePath))
			continue;

		pathinfo = classify_index_clause_usage(ipath, &clauselist);

		/* If it's unclassifiable, treat it as distinct from all others */
		if (pathinfo->unclassifiable)
		{
			pathinfoarray[npaths++] = pathinfo;
			continue;
		}

		for (i = 0; i < npaths; i++)
		{
			if (!pathinfoarray[i]->unclassifiable &&
				bms_equal(pathinfo->clauseids, pathinfoarray[i]->clauseids))
				break;
		}
		if (i < npaths)
		{
			/* duplicate clauseids, keep the cheaper one */
			Cost		ncost;
			Cost		ocost;
			Selectivity nselec;
			Selectivity oselec;

			if (IsYugaByteEnabled() && yb_enable_base_scans_cost_model &&
				pathinfo->path->parent->is_yb_relation)
			{
				yb_cost_bitmap_tree_node(pathinfo->path, &ncost, &nselec, NULL);
				yb_cost_bitmap_tree_node(pathinfoarray[i]->path, &ocost,
										 &oselec, NULL);
			}
			else
			{
				cost_bitmap_tree_node(pathinfo->path, &ncost, &nselec);
				cost_bitmap_tree_node(pathinfoarray[i]->path, &ocost, &oselec);
			}
			if (ncost < ocost)
				pathinfoarray[i] = pathinfo;
		}
		else
		{
			/* not duplicate clauseids, add to array */
			pathinfoarray[npaths++] = pathinfo;
		}
	}

	/* If only one surviving path, we're done */
	if (npaths == 1)
		return pathinfoarray[0]->path;

	/* Sort the surviving paths by index access cost */
	qsort(pathinfoarray, npaths, sizeof(PathClauseUsage *),
		  path_usage_comparator);

	/*
	 * For each surviving index, consider it as an "AND group leader", and see
	 * whether adding on any of the later indexes results in an AND path with
	 * cheaper total cost than before.  Then take the cheapest AND group.
	 *
	 * Note: paths that are either clauseless or unclassifiable will have
	 * empty clauseids, so that they will not be rejected by the clauseids
	 * filter here, nor will they cause later paths to be rejected by it.
	 */
	for (i = 0; i < npaths; i++)
	{
		Cost		costsofar;
		List	   *qualsofar;
		Bitmapset  *clauseidsofar;

		pathinfo = pathinfoarray[i];
		paths = list_make1(pathinfo->path);
		if (rel->is_yb_relation && yb_enable_base_scans_cost_model)
			costsofar = yb_bitmap_scan_cost_est(root, rel, pathinfo->path);
		else
			costsofar = bitmap_scan_cost_est(root, rel, pathinfo->path);

		qualsofar = list_concat_copy(pathinfo->quals, pathinfo->preds);
		clauseidsofar = bms_copy(pathinfo->clauseids);

		for (j = i + 1; j < npaths; j++)
		{
			Cost		newcost;

			pathinfo = pathinfoarray[j];
			/* Check for redundancy */
			if (bms_overlap(pathinfo->clauseids, clauseidsofar))
				continue;		/* consider it redundant */
			if (pathinfo->preds)
			{
				bool		redundant = false;

				/* we check each predicate clause separately */
				foreach(l, pathinfo->preds)
				{
					Node	   *np = (Node *) lfirst(l);

					if (predicate_implied_by(list_make1(np), qualsofar, false))
					{
						redundant = true;
						break;	/* out of inner foreach loop */
					}
				}
				if (redundant)
					continue;
			}
			/* tentatively add new path to paths, so we can estimate cost */
			paths = lappend(paths, pathinfo->path);
			newcost = bitmap_and_cost_est(root, rel, paths);
			if (newcost < costsofar)
			{
				/* keep new path in paths, update subsidiary variables */
				costsofar = newcost;
				qualsofar = list_concat(qualsofar, pathinfo->quals);
				qualsofar = list_concat(qualsofar, pathinfo->preds);
				clauseidsofar = bms_add_members(clauseidsofar,
												pathinfo->clauseids);
			}
			else
			{
				/* reject new path, remove it from paths list */
				paths = list_truncate(paths, list_length(paths) - 1);
			}
		}

		/* Keep the cheapest AND-group (or singleton) */
		if (i == 0 || costsofar < bestcost)
		{
			bestpaths = paths;
			bestcost = costsofar;
		}

		/* some easy cleanup (we don't try real hard though) */
		list_free(qualsofar);
	}

	if (list_length(bestpaths) == 1)
		return (Path *) linitial(bestpaths);	/* no need for AND */
	return (Path *) create_bitmap_and_path(root, rel, bestpaths);
}

/* qsort comparator to sort in increasing index access cost order */
static int
path_usage_comparator(const void *a, const void *b)
{
	PathClauseUsage *pa = *(PathClauseUsage *const *) a;
	PathClauseUsage *pb = *(PathClauseUsage *const *) b;
	Cost		acost;
	Cost		bcost;
	Selectivity aselec;
	Selectivity bselec;

	if (IsYugaByteEnabled() && yb_enable_base_scans_cost_model &&
		pa->path->parent->is_yb_relation)
	{
		yb_cost_bitmap_tree_node(pa->path, &acost, &aselec, NULL);
		yb_cost_bitmap_tree_node(pb->path, &bcost, &bselec, NULL);
	}
	else {
		cost_bitmap_tree_node(pa->path, &acost, &aselec);
		cost_bitmap_tree_node(pb->path, &bcost, &bselec);
	}

	/*
	 * If costs are the same, sort by selectivity.
	 */
	if (acost < bcost)
		return -1;
	if (acost > bcost)
		return 1;

	if (aselec < bselec)
		return -1;
	if (aselec > bselec)
		return 1;

	return 0;
}

/*
 * Estimate the cost of actually executing a bitmap scan with a single
 * index path (which could be a BitmapAnd or BitmapOr node).
 */
static Cost
bitmap_scan_cost_est(PlannerInfo *root, RelOptInfo *rel, Path *ipath)
{
	BitmapHeapPath bpath;

	/* Set up a dummy BitmapHeapPath */
	bpath.path.type = T_BitmapHeapPath;
	bpath.path.pathtype = T_BitmapHeapScan;
	bpath.path.parent = rel;
	bpath.path.pathtarget = rel->reltarget;
	bpath.path.param_info = ipath->param_info;
	bpath.path.pathkeys = NIL;
	bpath.bitmapqual = ipath;

	/*
	 * Check the cost of temporary path without considering parallelism.
	 * Parallel bitmap heap path will be considered at later stage.
	 */
	bpath.path.parallel_workers = 0;

	/* Now we can do cost_bitmap_heap_scan */
	cost_bitmap_heap_scan(&bpath.path, root, rel,
						  bpath.path.param_info,
						  ipath,
						  get_loop_count(root, rel->relid,
										 PATH_REQ_OUTER(ipath)));

	return bpath.path.total_cost;
}

/*
 * Estimate the cost of actually executing a YB bitmap scan with a single
 * index path (which could be a BitmapAnd or BitmapOr node).
 */
static Cost
yb_bitmap_scan_cost_est(PlannerInfo *root, RelOptInfo *rel, Path *ipath)
{
	YbBitmapTablePath bpath;

	/* Set up a dummy YbBitmapTablePath */
	bpath.path.type = T_YbBitmapTablePath;
	bpath.path.pathtype = T_YbBitmapTableScan;
	bpath.path.parent = rel;
	bpath.path.pathtarget = rel->reltarget;
	bpath.path.param_info = ipath->param_info;
	bpath.path.pathkeys = NIL;
	bpath.bitmapqual = ipath;

	/* required for yb_parallel_cost */
	bpath.path.parallel_aware = false;

	/*
	 * Check the cost of temporary path without considering parallelism.
	 * Parallel bitmap heap path will be considered at later stage.
	 */
	bpath.path.parallel_workers = 0;

	if (yb_enable_bitmapscan)
		/* Now we can do cost_yb_bitmap_table_scan */
		yb_cost_bitmap_table_scan(&bpath.path, root, rel,
								  bpath.path.param_info,
								  ipath,
								  get_loop_count(root, rel->relid,
												 PATH_REQ_OUTER(ipath)));

	return bpath.path.total_cost;
}

/*
 * Estimate the cost of actually executing a BitmapAnd scan with the given
 * inputs.
 */
static Cost
bitmap_and_cost_est(PlannerInfo *root, RelOptInfo *rel, List *paths)
{
	BitmapAndPath *apath;

	/*
	 * Might as well build a real BitmapAndPath here, as the work is slightly
	 * too complicated to be worth repeating just to save one palloc.
	 */
	apath = create_bitmap_and_path(root, rel, paths);

	if (rel->is_yb_relation && yb_enable_base_scans_cost_model)
		return yb_bitmap_scan_cost_est(root, rel, (Path *) apath);
	else
		return bitmap_scan_cost_est(root, rel, (Path *) apath);
}


/*
 * classify_index_clause_usage
 *		Construct a PathClauseUsage struct describing the WHERE clauses and
 *		index predicate clauses used by the given indexscan path.
 *		We consider two clauses the same if they are equal().
 *
 * At some point we might want to migrate this info into the Path data
 * structure proper, but for the moment it's only needed within
 * choose_bitmap_and().
 *
 * *clauselist is used and expanded as needed to identify all the distinct
 * clauses seen across successive calls.  Caller must initialize it to NIL
 * before first call of a set.
 */
static PathClauseUsage *
classify_index_clause_usage(Path *path, List **clauselist)
{
	PathClauseUsage *result;
	Bitmapset  *clauseids;
	ListCell   *lc;

	result = (PathClauseUsage *) palloc(sizeof(PathClauseUsage));
	result->path = path;

	/* Recursively find the quals and preds used by the path */
	result->quals = NIL;
	result->preds = NIL;
	find_indexpath_quals(path, &result->quals, &result->preds);

	/*
	 * Some machine-generated queries have outlandish numbers of qual clauses.
	 * To avoid getting into O(N^2) behavior even in this preliminary
	 * classification step, we want to limit the number of entries we can
	 * accumulate in *clauselist.  Treat any path with more than 100 quals +
	 * preds as unclassifiable, which will cause calling code to consider it
	 * distinct from all other paths.
	 */
	if (list_length(result->quals) + list_length(result->preds) > 100)
	{
		result->clauseids = NULL;
		result->unclassifiable = true;
		return result;
	}

	/* Build up a bitmapset representing the quals and preds */
	clauseids = NULL;
	foreach(lc, result->quals)
	{
		Node	   *node = (Node *) lfirst(lc);

		clauseids = bms_add_member(clauseids,
								   find_list_position(node, clauselist));
	}
	foreach(lc, result->preds)
	{
		Node	   *node = (Node *) lfirst(lc);

		clauseids = bms_add_member(clauseids,
								   find_list_position(node, clauselist));
	}
	result->clauseids = clauseids;
	result->unclassifiable = false;

	return result;
}


/*
 * find_indexpath_quals
 *
 * Given the Path structure for a plain or bitmap indexscan, extract lists
 * of all the index clauses and index predicate conditions used in the Path.
 * These are appended to the initial contents of *quals and *preds (hence
 * caller should initialize those to NIL).
 *
 * Note we are not trying to produce an accurate representation of the AND/OR
 * semantics of the Path, but just find out all the base conditions used.
 *
 * The result lists contain pointers to the expressions used in the Path,
 * but all the list cells are freshly built, so it's safe to destructively
 * modify the lists (eg, by concat'ing with other lists).
 */
static void
find_indexpath_quals(Path *bitmapqual, List **quals, List **preds)
{
	if (IsA(bitmapqual, BitmapAndPath))
	{
		BitmapAndPath *apath = (BitmapAndPath *) bitmapqual;
		ListCell   *l;

		foreach(l, apath->bitmapquals)
		{
			find_indexpath_quals((Path *) lfirst(l), quals, preds);
		}
	}
	else if (IsA(bitmapqual, BitmapOrPath))
	{
		BitmapOrPath *opath = (BitmapOrPath *) bitmapqual;
		ListCell   *l;

		foreach(l, opath->bitmapquals)
		{
			find_indexpath_quals((Path *) lfirst(l), quals, preds);
		}
	}
	else if (IsA(bitmapqual, IndexPath))
	{
		IndexPath  *ipath = (IndexPath *) bitmapqual;
		ListCell   *l;

		foreach(l, ipath->indexclauses)
		{
			IndexClause *iclause = (IndexClause *) lfirst(l);

			*quals = lappend(*quals, iclause->rinfo->clause);
		}
		*preds = list_concat(*preds, ipath->indexinfo->indpred);
	}
	else
		elog(ERROR, "unrecognized node type: %d", nodeTag(bitmapqual));
}


/*
 * find_list_position
 *		Return the given node's position (counting from 0) in the given
 *		list of nodes.  If it's not equal() to any existing list member,
 *		add it at the end, and return that position.
 */
static int
find_list_position(Node *node, List **nodelist)
{
	int			i;
	ListCell   *lc;

	i = 0;
	foreach(lc, *nodelist)
	{
		Node	   *oldnode = (Node *) lfirst(lc);

		if (equal(node, oldnode))
			return i;
		i++;
	}

	*nodelist = lappend(*nodelist, node);

	return i;
}


/*
 * check_index_only
 *		Determine whether an index-only scan is possible for this index.
 */
static bool
check_index_only(RelOptInfo *rel, IndexOptInfo *index)
{
	bool		result;
	Bitmapset  *attrs_used = NULL;
	Bitmapset  *index_canreturn_attrs = NULL;
	ListCell   *lc;
	int			i;

	/* Index-only scans must be enabled */
	if (!enable_indexonlyscan)
		return false;

	/*
	 * Check that all needed attributes of the relation are available from the
	 * index.
	 */

	/*
	 * First, identify all the attributes needed for joins or final output.
	 * Note: we must look at rel's targetlist, not the attr_needed data,
	 * because attr_needed isn't computed for inheritance child rels.
	 */
	pull_varattnos((Node *) rel->reltarget->exprs, rel->relid, &attrs_used);

	/*
	 * Add all the attributes used by restriction clauses; but consider only
	 * those clauses not implied by the index predicate, since ones that are
	 * so implied don't need to be checked explicitly in the plan.
	 *
	 * Note: attributes used only in index quals would not be needed at
	 * runtime either, if we are certain that the index is not lossy.  However
	 * it'd be complicated to account for that accurately, and it doesn't
	 * matter in most cases, since we'd conclude that such attributes are
	 * available from the index anyway.
	 */
	foreach(lc, index->indrestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		pull_varattnos((Node *) rinfo->clause, rel->relid, &attrs_used);
	}

	/*
	 * Construct a bitmapset of columns that the index can return back in an
	 * index-only scan.
	 */
	for (i = 0; i < index->ncolumns; i++)
	{
		int			attno = index->indexkeys[i];

		/*
		 * For the moment, we just ignore index expressions.  It might be nice
		 * to do something with them, later.
		 */
		if (attno == 0)
			continue;

		if (index->canreturn[i])
			index_canreturn_attrs =
				bms_add_member(index_canreturn_attrs,
							   attno - FirstLowInvalidHeapAttributeNumber);
	}

	/* Do we have all the necessary attributes? */
	result = bms_is_subset(attrs_used, index_canreturn_attrs);

	bms_free(attrs_used);
	bms_free(index_canreturn_attrs);

	return result;
}

/*
 * get_loop_count
 *		Choose the loop count estimate to use for costing a parameterized path
 *		with the given set of outer relids.
 *
 * Since we produce parameterized paths before we've begun to generate join
 * relations, it's impossible to predict exactly how many times a parameterized
 * path will be iterated; we don't know the size of the relation that will be
 * on the outside of the nestloop.  However, we should try to account for
 * multiple iterations somehow in costing the path.  The heuristic embodied
 * here is to use the rowcount of the smallest other base relation needed in
 * the join clauses used by the path.  (We could alternatively consider the
 * largest one, but that seems too optimistic.)  This is of course the right
 * answer for single-other-relation cases, and it seems like a reasonable
 * zero-order approximation for multiway-join cases.
 *
 * In addition, we check to see if the other side of each join clause is on
 * the inside of some semijoin that the current relation is on the outside of.
 * If so, the only way that a parameterized path could be used is if the
 * semijoin RHS has been unique-ified, so we should use the number of unique
 * RHS rows rather than using the relation's raw rowcount.
 *
 * Note: for this to work, allpaths.c must establish all baserel size
 * estimates before it begins to compute paths, or at least before it
 * calls create_index_paths().
 */
double
get_loop_count(PlannerInfo *root, Index cur_relid, Relids outer_relids)
{
	double		result;
	int			outer_relid;

	/* For a non-parameterized path, just return 1.0 quickly */
	if (outer_relids == NULL)
		return 1.0;

	result = 0.0;
	outer_relid = -1;
	while ((outer_relid = bms_next_member(outer_relids, outer_relid)) >= 0)
	{
		RelOptInfo *outer_rel;
		double		rowcount;

		/* Paranoia: ignore bogus relid indexes */
		if (outer_relid >= root->simple_rel_array_size)
			continue;
		outer_rel = root->simple_rel_array[outer_relid];
		if (outer_rel == NULL)
			continue;
		Assert(outer_rel->relid == outer_relid);	/* sanity check on array */

		/* Other relation could be proven empty, if so ignore */
		if (IS_DUMMY_REL(outer_rel))
			continue;

		/* Otherwise, rel's rows estimate should be valid by now */
		Assert(outer_rel->rows > 0);

		/* Check to see if rel is on the inside of any semijoins */
		rowcount = adjust_rowcount_for_semijoins(root,
												 cur_relid,
												 outer_relid,
												 outer_rel->rows);

		/* Remember smallest row count estimate among the outer rels */
		if (result == 0.0 || result > rowcount)
			result = rowcount;
	}

	if (!bms_is_empty(root->yb_cur_batched_relids) &&
		 yb_enable_base_scans_cost_model)
		result /= yb_bnl_batch_size;

	/* Return 1.0 if we found no valid relations (shouldn't happen) */
	return (result > 0.0) ? result : 1.0;
}

/*
 * Check to see if outer_relid is on the inside of any semijoin that cur_relid
 * is on the outside of.  If so, replace rowcount with the estimated number of
 * unique rows from the semijoin RHS (assuming that's smaller, which it might
 * not be).  The estimate is crude but it's the best we can do at this stage
 * of the proceedings.
 */
static double
adjust_rowcount_for_semijoins(PlannerInfo *root,
							  Index cur_relid,
							  Index outer_relid,
							  double rowcount)
{
	ListCell   *lc;

	foreach(lc, root->join_info_list)
	{
		SpecialJoinInfo *sjinfo = (SpecialJoinInfo *) lfirst(lc);

		if (sjinfo->jointype == JOIN_SEMI &&
			bms_is_member(cur_relid, sjinfo->syn_lefthand) &&
			bms_is_member(outer_relid, sjinfo->syn_righthand))
		{
			/* Estimate number of unique-ified rows */
			double		nraw;
			double		nunique;

			nraw = approximate_joinrel_size(root, sjinfo->syn_righthand);
			nunique = estimate_num_groups(root,
										  sjinfo->semi_rhs_exprs,
										  nraw,
										  NULL,
										  NULL);
			if (rowcount > nunique)
				rowcount = nunique;
		}
	}
	return rowcount;
}

/*
 * Make an approximate estimate of the size of a joinrel.
 *
 * We don't have enough info at this point to get a good estimate, so we
 * just multiply the base relation sizes together.  Fortunately, this is
 * the right answer anyway for the most common case with a single relation
 * on the RHS of a semijoin.  Also, estimate_num_groups() has only a weak
 * dependency on its input_rows argument (it basically uses it as a clamp).
 * So we might be able to get a fairly decent end result even with a severe
 * overestimate of the RHS's raw size.
 */
static double
approximate_joinrel_size(PlannerInfo *root, Relids relids)
{
	double		rowcount = 1.0;
	int			relid;

	relid = -1;
	while ((relid = bms_next_member(relids, relid)) >= 0)
	{
		RelOptInfo *rel;

		/* Paranoia: ignore bogus relid indexes */
		if (relid >= root->simple_rel_array_size)
			continue;
		rel = root->simple_rel_array[relid];
		if (rel == NULL)
			continue;
		Assert(rel->relid == relid);	/* sanity check on array */

		/* Relation could be proven empty, if so ignore */
		if (IS_DUMMY_REL(rel))
			continue;

		/* Otherwise, rel's rows estimate should be valid by now */
		Assert(rel->rows > 0);

		/* Accumulate product */
		rowcount *= rel->rows;
	}
	return rowcount;
}


/****************************************************************************
 *				----  ROUTINES TO CHECK QUERY CLAUSES  ----
 ****************************************************************************/

/*
 * match_restriction_clauses_to_index
 *	  Identify restriction clauses for the rel that match the index.
 *	  Matching clauses are added to *clauseset.
 */
static void
match_restriction_clauses_to_index(PlannerInfo *root,
								   IndexOptInfo *index,
								   IndexClauseSet *clauseset)
{
	/* We can ignore clauses that are implied by the index predicate */
	match_clauses_to_index(root, index->indrestrictinfo, index, clauseset,
						   NULL /* yb_bitmap_idx_pushdowns */);
}

/*
 * match_join_clauses_to_index
 *	  Identify join clauses for the rel that match the index.
 *	  Matching clauses are added to *clauseset.
 *	  Also, add any potentially usable join OR clauses to *joinorclauses.
 */
static void
match_join_clauses_to_index(PlannerInfo *root,
							RelOptInfo *rel, IndexOptInfo *index,
							IndexClauseSet *clauseset,
							List **joinorclauses)
{
	ListCell   *lc;

	/* Scan the rel's join clauses */
	foreach(lc, rel->joininfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		/* Check if clause can be moved to this rel */
		if (!join_clause_is_movable_to(rinfo, rel))
			continue;

		/* Potentially usable, so see if it matches the index or is an OR */
		if (restriction_is_or_clause(rinfo))
			*joinorclauses = lappend(*joinorclauses, rinfo);
		else
			match_clause_to_index(root, rinfo, index, clauseset,
								  NULL /* yb_bitmap_idx_pushdowns */);
	}
}

/*
 * match_eclass_clauses_to_index
 *	  Identify EquivalenceClass join clauses for the rel that match the index.
 *	  Matching clauses are added to *clauseset.
 */
static void
match_eclass_clauses_to_index(PlannerInfo *root, IndexOptInfo *index,
							  IndexClauseSet *clauseset)
{
	int			indexcol;

	/* No work if rel is not in any such ECs */
	if (!index->rel->has_eclass_joins)
		return;

	for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
	{
		ec_member_matches_arg arg;
		List	   *clauses;

		/* Generate clauses, skipping any that join to lateral_referencers */
		arg.index = index;
		arg.indexcol = indexcol;
		clauses = generate_implied_equalities_for_column(root,
														 index->rel,
														 ec_member_matches_indexcol,
														 (void *) &arg,
														 index->rel->lateral_referencers);

		/*
		 * We have to check whether the results actually do match the index,
		 * since for non-btree indexes the EC's equality operators might not
		 * be in the index opclass (cf ec_member_matches_indexcol).
		 */
		match_clauses_to_index(root, clauses, index, clauseset,
							   NULL /* yb_bitmap_idx_pushdowns */);
	}
}

/*
 * match_clauses_to_index
 *	  Perform match_clause_to_index() for each clause in a list.
 *	  Matching clauses are added to *clauseset.
 */
static void
match_clauses_to_index(PlannerInfo *root,
					   List *clauses,
					   IndexOptInfo *index,
					   IndexClauseSet *clauseset,
					   List **yb_bitmap_idx_pushdowns)
{
	ListCell   *lc;

	foreach(lc, clauses)
	{
		RestrictInfo *rinfo = lfirst_node(RestrictInfo, lc);

		match_clause_to_index(root, rinfo, index, clauseset,
							  yb_bitmap_idx_pushdowns);
	}
}

/*
 * match_clause_to_index
 *	  Test whether a qual clause can be used with an index.
 *
 * If the clause is usable, add an IndexClause entry for it to the appropriate
 * list in *clauseset.  (*clauseset must be initialized to zeroes before first
 * call.)
 *
 * Note: in some circumstances we may find the same RestrictInfos coming from
 * multiple places.  Defend against redundant outputs by refusing to add a
 * clause twice (pointer equality should be a good enough check for this).
 *
 * Note: it's possible that a badly-defined index could have multiple matching
 * columns.  We always select the first match if so; this avoids scenarios
 * wherein we get an inflated idea of the index's selectivity by using the
 * same clause multiple times with different index columns.
 */
static void
match_clause_to_index(PlannerInfo *root,
					  RestrictInfo *rinfo,
					  IndexOptInfo *index,
					  IndexClauseSet *clauseset,
					  List **yb_bitmap_idx_pushdowns)
{
	int			indexcol;

	/*
	 * Never match pseudoconstants to indexes.  (Normally a match could not
	 * happen anyway, since a pseudoconstant clause couldn't contain a Var,
	 * but what if someone builds an expression index on a constant? It's not
	 * totally unreasonable to do so with a partial index, either.)
	 */
	if (rinfo->pseudoconstant)
		return;

	/*
	 * If clause can't be used as an indexqual because it must wait till after
	 * some lower-security-level restriction clause, reject it.
	 */
	if (!restriction_is_securely_promotable(rinfo, index->rel))
		return;

	/* OK, check each index key column for a match */
	for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
	{
		IndexClause *iclause;
		ListCell   *lc;

		/* Ignore duplicates */
		foreach(lc, clauseset->indexclauses[indexcol])
		{
			IndexClause *iclause = (IndexClause *) lfirst(lc);

			if (iclause->rinfo == rinfo)
				return;
		}

		/* OK, try to match the clause to the index column */
		iclause = match_clause_to_indexcol(root,
										   rinfo,
										   indexcol,
										   index);
		if (iclause)
		{
			/* Success, so record it */
			clauseset->indexclauses[indexcol] =
				lappend(clauseset->indexclauses[indexcol], iclause);
			clauseset->nonempty = true;
			return;
		}
	}

	if (IsYugaByteEnabled() && yb_bitmap_idx_pushdowns &&
		yb_can_pushdown_as_filter(index, rinfo))
	{
		rinfo->yb_pushable = true;
		*yb_bitmap_idx_pushdowns = lappend(*yb_bitmap_idx_pushdowns, rinfo);
	}
}

/*
 * yb_can_pushdown_as_filter
 *	  If a condition is not valid as an index condition, it might be able to be
 *	  pushed down as a filter on the index. For example, modulo or LIKE
 *	  operations aren't indexable but are valid as filters.
 */
static bool
yb_can_pushdown_as_filter(IndexOptInfo *index, RestrictInfo *rinfo)
{
	List   *colrefs = NIL;
	int		required_relid;

	/* Does the clause reference only the given index? */
	if (!bms_get_singleton_member(rinfo->required_relids, &required_relid) ||
		required_relid != index->rel->relid)
		return false;

	/* Can DocDB evaluate this operation? */
	if (!YbCanPushdownExpr(rinfo->clause, &colrefs))
		return false;

	/* Do all of the clause's referenced attributes exist in the index? */
	if (!is_index_only_attribute_nums(colrefs, index, true /* bitmapindex */))
		return false;

	return true;
}

static bool is_yb_hash_code_call(Node *clause)
{
	return clause && IsA(clause, FuncExpr)
				&& (((FuncExpr *) clause)->funcid == YB_HASH_CODE_OID);
}


/*
 * yb_hash_code_call_matches_indexcol
 * 	  Returns true if the index is on yb_hash_code(a, b, ...) and this index column is
 *	  a matching yb_hash_code(a, b, ...) clause.
 */
static bool
yb_hash_code_call_matches_indexcol(Node *yb_hash_code_clause,
								   IndexOptInfo *index,
								   int indexcol)
{
	if (!index->indexprs)
		return false;
	int			pos;
	ListCell   *indexpr_item;

	/* Iterate over each column of the index until the column of interest. */
	indexpr_item = list_head(index->indexprs);
	for (pos = 0; pos <= indexcol; pos++)
	{
		if (index->indexkeys[pos] == 0)
		{
			/* The index column refers to the next expression of indexprs. */
			Node	   *indexkey;

			if (indexpr_item == NULL)
			{
				elog(WARNING, "too few entries in indexprs list");
				return false;
			}

			if (pos == indexcol)
			{
				indexkey = (Node *) lfirst(indexpr_item);
				if (indexkey && IsA(indexkey, RelabelType))
						indexkey = (Node *) ((RelabelType *) indexkey)->arg;
				if (equal(yb_hash_code_clause, indexkey))
				{
					return true;
				}
			}

			indexpr_item = lnext(index->indexprs, indexpr_item);
		}
	}

	return false;
}

/*
 * match_clause_to_indexcol()
 *	  Determine whether a restriction clause matches a column of an index,
 *	  and if so, build an IndexClause node describing the details.
 *
 *	  To match an index normally, an operator clause:
 *
 *	  (1)  must be in the form (indexkey op const) or (const op indexkey);
 *		   and
 *	  (2)  must contain an operator which is in the index's operator family
 *		   for this column; and
 *	  (3)  must match the collation of the index, if collation is relevant.
 *
 *	  Our definition of "const" is exceedingly liberal: we allow anything that
 *	  doesn't involve a volatile function or a Var of the index's relation.
 *	  In particular, Vars belonging to other relations of the query are
 *	  accepted here, since a clause of that form can be used in a
 *	  parameterized indexscan.  It's the responsibility of higher code levels
 *	  to manage restriction and join clauses appropriately.
 *
 *	  Note: we do need to check for Vars of the index's relation on the
 *	  "const" side of the clause, since clauses like (a.f1 OP (b.f2 OP a.f3))
 *	  are not processable by a parameterized indexscan on a.f1, whereas
 *	  something like (a.f1 OP (b.f2 OP c.f3)) is.
 *
 *	  Presently, the executor can only deal with indexquals that have the
 *	  indexkey on the left, so we can only use clauses that have the indexkey
 *	  on the right if we can commute the clause to put the key on the left.
 *	  We handle that by generating an IndexClause with the correctly-commuted
 *	  opclause as a derived indexqual.
 *
 *	  If the index has a collation, the clause must have the same collation.
 *	  For collation-less indexes, we assume it doesn't matter; this is
 *	  necessary for cases like "hstore ? text", wherein hstore's operators
 *	  don't care about collation but the clause will get marked with a
 *	  collation anyway because of the text argument.  (This logic is
 *	  embodied in the macro IndexCollMatchesExprColl.)
 *
 *	  It is also possible to match RowCompareExpr clauses to indexes (but
 *	  currently, only btree indexes handle this).
 *
 *	  It is also possible to match ScalarArrayOpExpr clauses to indexes, when
 *	  the clause is of the form "indexkey op ANY (arrayconst)".
 *
 *	  For boolean indexes, it is also possible to match the clause directly
 *	  to the indexkey; or perhaps the clause is (NOT indexkey).
 *
 *	  And, last but not least, some operators and functions can be processed
 *	  to derive (typically lossy) indexquals from a clause that isn't in
 *	  itself indexable.  If we see that any operand of an OpExpr or FuncExpr
 *	  matches the index key, and the function has a planner support function
 *	  attached to it, we'll invoke the support function to see if such an
 *	  indexqual can be built.
 *
 * 'rinfo' is the clause to be tested (as a RestrictInfo node).
 * 'indexcol' is a column number of 'index' (counting from 0).
 * 'index' is the index of interest.
 *
 * Returns an IndexClause if the clause can be used with this index key,
 * or NULL if not.
 *
 * NOTE:  returns NULL if clause is an OR or AND clause; it is the
 * responsibility of higher-level routines to cope with those.
 */
static IndexClause *
match_clause_to_indexcol(PlannerInfo *root,
						 RestrictInfo *rinfo,
						 int indexcol,
						 IndexOptInfo *index)
{
	IndexClause *iclause;
	Expr	   *clause = rinfo->clause;
	Oid			opfamily;

	Assert(indexcol < index->nkeycolumns);

	/*
	 * Historically this code has coped with NULL clauses.  That's probably
	 * not possible anymore, but we might as well continue to cope.
	 */
	if (clause == NULL)
		return NULL;

	/* First check for boolean-index cases. */
	opfamily = index->opfamily[indexcol];
	if (IsBooleanOpfamily(opfamily))
	{
		iclause = match_boolean_index_clause(root, rinfo, indexcol, index);
		if (iclause)
			return iclause;
	}

	/*
	 * Clause must be an opclause, funcclause, ScalarArrayOpExpr, or
	 * RowCompareExpr.  Or, if the index supports it, we can handle IS
	 * NULL/NOT NULL clauses.
	 */
	if (IsA(clause, OpExpr))
	{
		return match_opclause_to_indexcol(root, rinfo, indexcol, index);
	}
	else if (IsA(clause, FuncExpr))
	{
		return match_funcclause_to_indexcol(root, rinfo, indexcol, index);
	}
	else if (IsA(clause, ScalarArrayOpExpr))
	{
		return match_saopclause_to_indexcol(root, rinfo, indexcol, index);
	}
	else if (IsA(clause, RowCompareExpr))
	{
		return match_rowcompare_to_indexcol(root, rinfo, indexcol, index);
	}
	else if (index->amsearchnulls && IsA(clause, NullTest))
	{
		NullTest   *nt = (NullTest *) clause;

		if (!nt->argisrow &&
			match_index_to_operand((Node *) nt->arg, indexcol, index))
		{
			/* Cannot push down IS NOT NULL on hash columns in LSM Index */
			if (IsYBRelationById(index->indexoid) &&
				nt->nulltesttype == IS_NOT_NULL &&
				is_hash_column_in_lsm_index(index, indexcol))
				return false;

			iclause = makeNode(IndexClause);
			iclause->rinfo = rinfo;
			iclause->indexquals = list_make1(rinfo);
			iclause->lossy = false;
			iclause->indexcol = indexcol;
			iclause->indexcols = NIL;
			return iclause;
		}
	}

	return NULL;
}

/*
 * match_boolean_index_clause
 *	  Recognize restriction clauses that can be matched to a boolean index.
 *
 * The idea here is that, for an index on a boolean column that supports the
 * BooleanEqualOperator, we can transform a plain reference to the indexkey
 * into "indexkey = true", or "NOT indexkey" into "indexkey = false", etc,
 * so as to make the expression indexable using the index's "=" operator.
 * Since Postgres 8.1, we must do this because constant simplification does
 * the reverse transformation; without this code there'd be no way to use
 * such an index at all.
 *
 * This should be called only when IsBooleanOpfamily() recognizes the
 * index's operator family.  We check to see if the clause matches the
 * index's key, and if so, build a suitable IndexClause.
 */
static IndexClause *
match_boolean_index_clause(PlannerInfo *root,
						   RestrictInfo *rinfo,
						   int indexcol,
						   IndexOptInfo *index)
{
	Node	   *clause = (Node *) rinfo->clause;
	Expr	   *op = NULL;

	/* Direct match? */
	if (match_index_to_operand(clause, indexcol, index))
	{
		/* convert to indexkey = TRUE */
		op = make_opclause(BooleanEqualOperator, BOOLOID, false,
						   (Expr *) clause,
						   (Expr *) makeBoolConst(true, false),
						   InvalidOid, InvalidOid);
	}
	/* NOT clause? */
	else if (is_notclause(clause))
	{
		Node	   *arg = (Node *) get_notclausearg((Expr *) clause);

		if (match_index_to_operand(arg, indexcol, index))
		{
			/* convert to indexkey = FALSE */
			op = make_opclause(BooleanEqualOperator, BOOLOID, false,
							   (Expr *) arg,
							   (Expr *) makeBoolConst(false, false),
							   InvalidOid, InvalidOid);
		}
	}

	/*
	 * Since we only consider clauses at top level of WHERE, we can convert
	 * indexkey IS TRUE and indexkey IS FALSE to index searches as well.  The
	 * different meaning for NULL isn't important.
	 */
	else if (clause && IsA(clause, BooleanTest))
	{
		BooleanTest *btest = (BooleanTest *) clause;
		Node	   *arg = (Node *) btest->arg;

		if (btest->booltesttype == IS_TRUE &&
			match_index_to_operand(arg, indexcol, index))
		{
			/* convert to indexkey = TRUE */
			op = make_opclause(BooleanEqualOperator, BOOLOID, false,
							   (Expr *) arg,
							   (Expr *) makeBoolConst(true, false),
							   InvalidOid, InvalidOid);
		}
		else if (btest->booltesttype == IS_FALSE &&
				 match_index_to_operand(arg, indexcol, index))
		{
			/* convert to indexkey = FALSE */
			op = make_opclause(BooleanEqualOperator, BOOLOID, false,
							   (Expr *) arg,
							   (Expr *) makeBoolConst(false, false),
							   InvalidOid, InvalidOid);
		}
	}

	/*
	 * If we successfully made an operator clause from the given qual, we must
	 * wrap it in an IndexClause.  It's not lossy.
	 */
	if (op)
	{
		IndexClause *iclause = makeNode(IndexClause);

		iclause->rinfo = rinfo;
		iclause->indexquals = list_make1(make_simple_restrictinfo(root, op));
		iclause->lossy = false;
		iclause->indexcol = indexcol;
		iclause->indexcols = NIL;
		return iclause;
	}

	return NULL;
}

/*
 * match_opclause_to_indexcol()
 *	  Handles the OpExpr case for match_clause_to_indexcol(),
 *	  which see for comments.
 */
static IndexClause *
match_opclause_to_indexcol(PlannerInfo *root,
						   RestrictInfo *rinfo,
						   int indexcol,
						   IndexOptInfo *index)
{
	IndexClause *iclause;
	OpExpr	   *clause = (OpExpr *) rinfo->clause;
	Node	   *leftop,
			   *rightop;
	Oid			expr_op;
	Oid			expr_coll;
	Index		index_relid;
	Oid			opfamily;
	Oid			idxcollation;

	/*
	 * Only binary operators need apply.  (In theory, a planner support
	 * function could do something with a unary operator, but it seems
	 * unlikely to be worth the cycles to check.)
	 */
	if (list_length(clause->args) != 2)
		return NULL;

	leftop = (Node *) linitial(clause->args);
	rightop = (Node *) lsecond(clause->args);
	expr_op = clause->opno;
	expr_coll = clause->inputcollid;

	index_relid = index->rel->relid;
	opfamily = index->opfamily[indexcol];
	idxcollation = index->indexcollations[indexcol];

	/*
	 * Check for clauses of the form: (indexkey operator constant) or
	 * (constant operator indexkey).  See match_clause_to_indexcol's notes
	 * about const-ness.
	 *
	 * Note that we don't ask the support function about clauses that don't
	 * have one of these forms.  Again, in principle it might be possible to
	 * do something, but it seems unlikely to be worth the cycles to check.
	 */
	if (match_index_to_operand(leftop, indexcol, index) &&
		!bms_is_member(index_relid, rinfo->right_relids) &&
		!contain_volatile_functions(rightop))
	{
		/*
		 * Do not use the yb_hash_code special case if we have an applicable
		 * functional index on yb_hash_code. For example, if the call is an
		 * index on yb_hash_code(x, y) and the clause is yb_hash_code(x, y),
		 * we will take the typical index path.
		 */

		if (is_yb_hash_code_call(leftop) &&
			!yb_hash_code_call_matches_indexcol(leftop, index, indexcol))
		{
			if (!op_in_opfamily(expr_op, INTEGER_LSM_FAM_OID) || !is_opclause(clause))
				return NULL;

			iclause = makeNode(IndexClause);
			iclause->rinfo = rinfo;
			iclause->indexquals = list_make1(rinfo);
			iclause->lossy = false;
			iclause->indexcol = indexcol;
			iclause->indexcols = NIL;
			return iclause;
		}

		/*
		 * If the column in the filter clause is part of the hash key for this
		 * index and the clause uses an inequality operator, then index scan
		 * cannot be used. This is because a hash index is sorted by the hash
		 * value and not by the value of the column. #13241
		 */
		if (is_hash_column_in_lsm_index(index, indexcol))
		{
			int op_strategy = get_op_opfamily_strategy(((OpExpr *) clause)->opno, opfamily);
			if (op_strategy != BTEqualStrategyNumber)
				return NULL;
		}

		if (IndexCollMatchesExprColl(idxcollation, expr_coll) &&
			op_in_opfamily(expr_op, opfamily))
		{
			iclause = makeNode(IndexClause);
			iclause->rinfo = rinfo;
			iclause->indexquals = list_make1(rinfo);
			iclause->lossy = false;
			iclause->indexcol = indexcol;
			iclause->indexcols = NIL;
			return iclause;
		}

		/*
		 * If we didn't find a member of the index's opfamily, try the support
		 * function for the operator's underlying function.
		 */
		set_opfuncid(clause);	/* make sure we have opfuncid */
		return get_index_clause_from_support(root,
											 rinfo,
											 clause->opfuncid,
											 0, /* indexarg on left */
											 indexcol,
											 index);
	}

	if (match_index_to_operand(rightop, indexcol, index) &&
		!bms_is_member(index_relid, rinfo->left_relids) &&
		!contain_volatile_functions(leftop))
	{
		/*
		 * Do not use the yb_hash_code special case if we have an applicable
		 * functional index on yb_hash_code. For example, if the call is an
		 * index on yb_hash_code(x, y) and the clause is yb_hash_code(x, y),
		 * we will take the typical index path.
		 */
		if (is_yb_hash_code_call(rightop) &&
			!yb_hash_code_call_matches_indexcol(rightop, index, indexcol))
		{
			if (!op_in_opfamily(expr_op, INTEGER_LSM_FAM_OID) || !is_opclause(clause))
				return NULL;

			Oid			comm_op = get_commutator(expr_op);
			RestrictInfo *commrinfo;

			/* Build a commuted OpExpr and RestrictInfo */
			commrinfo = commute_restrictinfo(rinfo, comm_op);
			iclause = makeNode(IndexClause);
			iclause->rinfo = rinfo;
			iclause->indexquals = list_make1(commrinfo);
			iclause->lossy = false;
			iclause->indexcol = indexcol;
			iclause->indexcols = NIL;
			return iclause;
		}

		/*
		 * If the column in the filter clause is part of the hash key for this
		 * index and the clause uses an inequality operator, then index scan
		 * cannot be used. This is because a hash index is sorted by the hash
		 * value and not by the value of the column. #13241
		 */
		if (is_hash_column_in_lsm_index(index, indexcol))
		{
			int op_strategy = get_op_opfamily_strategy(((OpExpr *) clause)->opno, opfamily);
			if (op_strategy != BTEqualStrategyNumber)
				return NULL;
		}

		if (IndexCollMatchesExprColl(idxcollation, expr_coll))
		{
			Oid			comm_op = get_commutator(expr_op);

			if (OidIsValid(comm_op) &&
				op_in_opfamily(comm_op, opfamily))
			{
				RestrictInfo *commrinfo;

				/* Build a commuted OpExpr and RestrictInfo */
				commrinfo = commute_restrictinfo(rinfo, comm_op);

				/* Make an IndexClause showing that as a derived qual */
				iclause = makeNode(IndexClause);
				iclause->rinfo = rinfo;
				iclause->indexquals = list_make1(commrinfo);
				iclause->lossy = false;
				iclause->indexcol = indexcol;
				iclause->indexcols = NIL;
				return iclause;
			}
		}

		/*
		 * If we didn't find a member of the index's opfamily, try the support
		 * function for the operator's underlying function.
		 */
		set_opfuncid(clause);	/* make sure we have opfuncid */
		return get_index_clause_from_support(root,
											 rinfo,
											 clause->opfuncid,
											 1, /* indexarg on right */
											 indexcol,
											 index);
	}

	return NULL;
}

/*
 * match_funcclause_to_indexcol()
 *	  Handles the FuncExpr case for match_clause_to_indexcol(),
 *	  which see for comments.
 */
static IndexClause *
match_funcclause_to_indexcol(PlannerInfo *root,
							 RestrictInfo *rinfo,
							 int indexcol,
							 IndexOptInfo *index)
{
	FuncExpr   *clause = (FuncExpr *) rinfo->clause;
	int			indexarg;
	ListCell   *lc;

	/*
	 * We have no built-in intelligence about function clauses, but if there's
	 * a planner support function, it might be able to do something.  But, to
	 * cut down on wasted planning cycles, only call the support function if
	 * at least one argument matches the target index column.
	 *
	 * Note that we don't insist on the other arguments being pseudoconstants;
	 * the support function has to check that.  This is to allow cases where
	 * only some of the other arguments need to be included in the indexqual.
	 */
	indexarg = 0;
	foreach(lc, clause->args)
	{
		Node	   *op = (Node *) lfirst(lc);

		if (match_index_to_operand(op, indexcol, index))
		{
			return get_index_clause_from_support(root,
												 rinfo,
												 clause->funcid,
												 indexarg,
												 indexcol,
												 index);
		}

		indexarg++;
	}

	return NULL;
}

/*
 * get_index_clause_from_support()
 *		If the function has a planner support function, try to construct
 *		an IndexClause using indexquals created by the support function.
 */
static IndexClause *
get_index_clause_from_support(PlannerInfo *root,
							  RestrictInfo *rinfo,
							  Oid funcid,
							  int indexarg,
							  int indexcol,
							  IndexOptInfo *index)
{
	Oid			prosupport = get_func_support(funcid);
	SupportRequestIndexCondition req;
	List	   *sresult;

	if (!OidIsValid(prosupport))
		return NULL;

	req.type = T_SupportRequestIndexCondition;
	req.root = root;
	req.funcid = funcid;
	req.node = (Node *) rinfo->clause;
	req.indexarg = indexarg;
	req.index = index;
	req.indexcol = indexcol;
	req.opfamily = index->opfamily[indexcol];
	req.indexcollation = index->indexcollations[indexcol];

	req.lossy = true;			/* default assumption */

	sresult = (List *)
		DatumGetPointer(OidFunctionCall1(prosupport,
										 PointerGetDatum(&req)));

	if (sresult != NIL)
	{
		IndexClause *iclause = makeNode(IndexClause);
		List	   *indexquals = NIL;
		ListCell   *lc;

		/*
		 * The support function API says it should just give back bare
		 * clauses, so here we must wrap each one in a RestrictInfo.
		 */
		foreach(lc, sresult)
		{
			Expr	   *clause = (Expr *) lfirst(lc);

			indexquals = lappend(indexquals,
								 make_simple_restrictinfo(root, clause));
		}

		iclause->rinfo = rinfo;
		iclause->indexquals = indexquals;
		iclause->lossy = req.lossy;
		iclause->indexcol = indexcol;
		iclause->indexcols = NIL;

		return iclause;
	}

	return NULL;
}

/*
 * match_saopclause_to_indexcol()
 *	  Handles the ScalarArrayOpExpr case for match_clause_to_indexcol(),
 *	  which see for comments.
 */
static IndexClause *
match_saopclause_to_indexcol(PlannerInfo *root,
							 RestrictInfo *rinfo,
							 int indexcol,
							 IndexOptInfo *index)
{
	ScalarArrayOpExpr *saop = (ScalarArrayOpExpr *) rinfo->clause;
	Node	   *leftop,
			   *rightop;
	Relids		right_relids;
	Oid			expr_op;
	Oid			expr_coll;
	Index		index_relid;
	Oid			opfamily;
	Oid			idxcollation;

	/* We only accept ANY clauses, not ALL */
	if (!saop->useOr)
		return NULL;
	leftop = (Node *) linitial(saop->args);
	rightop = (Node *) lsecond(saop->args);
	right_relids = pull_varnos(root, rightop);
	expr_op = saop->opno;
	expr_coll = saop->inputcollid;

	index_relid = index->rel->relid;
	opfamily = index->opfamily[indexcol];
	idxcollation = index->indexcollations[indexcol];

	/*
	 * We must have indexkey on the left and a pseudo-constant array argument.
	 */
	if (match_index_to_operand(leftop, indexcol, index) &&
		!bms_is_member(index_relid, right_relids) &&
		!contain_volatile_functions(rightop))
	{
		if (IndexCollMatchesExprColl(idxcollation, expr_coll) &&
			op_in_opfamily(expr_op, opfamily))
		{
			IndexClause *iclause = makeNode(IndexClause);

			iclause->rinfo = rinfo;
			iclause->indexquals = list_make1(rinfo);
			iclause->lossy = false;
			iclause->indexcol = indexcol;
			iclause->indexcols = NIL;
			return iclause;
		}

		/*
		 * We do not currently ask support functions about ScalarArrayOpExprs,
		 * though in principle we could.
		 */
	}

	return NULL;
}

/*
 * match_rowcompare_to_indexcol()
 *	  Handles the RowCompareExpr case for match_clause_to_indexcol(),
 *	  which see for comments.
 *
 * In this routine we check whether the first column of the row comparison
 * matches the target index column.  This is sufficient to guarantee that some
 * index condition can be constructed from the RowCompareExpr --- the rest
 * is handled by expand_indexqual_rowcompare().
 */
static IndexClause *
match_rowcompare_to_indexcol(PlannerInfo *root,
							 RestrictInfo *rinfo,
							 int indexcol,
							 IndexOptInfo *index)
{
	RowCompareExpr *clause = (RowCompareExpr *) rinfo->clause;
	Index		index_relid;
	Oid			opfamily;
	Oid			idxcollation;
	Node	   *leftop,
			   *rightop;
	bool		var_on_left;
	Oid			expr_op;
	Oid			expr_coll;

	/* Forget it if we're not dealing with a btree index or lsm index */
	if (index->relam != BTREE_AM_OID && index->relam != LSM_AM_OID)
		return NULL;

	if (is_hash_column_in_lsm_index(index, indexcol))
		return NULL;

	index_relid = index->rel->relid;
	opfamily = index->opfamily[indexcol];
	idxcollation = index->indexcollations[indexcol];

	/*
	 * We could do the matching on the basis of insisting that the opfamily
	 * shown in the RowCompareExpr be the same as the index column's opfamily,
	 * but that could fail in the presence of reverse-sort opfamilies: it'd be
	 * a matter of chance whether RowCompareExpr had picked the forward or
	 * reverse-sort family.  So look only at the operator, and match if it is
	 * a member of the index's opfamily (after commutation, if the indexkey is
	 * on the right).  We'll worry later about whether any additional
	 * operators are matchable to the index.
	 */
	leftop = (Node *) linitial(clause->largs);
	rightop = (Node *) linitial(castNode(List, clause->rargs));
	expr_op = linitial_oid(clause->opnos);
	expr_coll = linitial_oid(clause->inputcollids);

	/* Collations must match, if relevant */
	if (!IndexCollMatchesExprColl(idxcollation, expr_coll))
		return NULL;

	/*
	 * These syntactic tests are the same as in match_opclause_to_indexcol()
	 */
	if (match_index_to_operand(leftop, indexcol, index) &&
		!bms_is_member(index_relid, pull_varnos(root, rightop)) &&
		!contain_volatile_functions(rightop))
	{
		/* OK, indexkey is on left */
		var_on_left = true;
	}
	else if (match_index_to_operand(rightop, indexcol, index) &&
			 !bms_is_member(index_relid, pull_varnos(root, leftop)) &&
			 !contain_volatile_functions(leftop))
	{
		/* indexkey is on right, so commute the operator */
		expr_op = get_commutator(expr_op);
		if (expr_op == InvalidOid)
			return NULL;
		var_on_left = false;
	}
	else
		return NULL;

	/* We're good if the operator is the right type of opfamily member */
	switch (get_op_opfamily_strategy(expr_op, opfamily))
	{
		case BTLessStrategyNumber:
		case BTLessEqualStrategyNumber:
		case BTGreaterEqualStrategyNumber:
		case BTGreaterStrategyNumber:
			return expand_indexqual_rowcompare(root,
											   rinfo,
											   indexcol,
											   index,
											   expr_op,
											   var_on_left);
	}

	return NULL;
}

/*
 * expand_indexqual_rowcompare --- expand a single indexqual condition
 *		that is a RowCompareExpr
 *
 * It's already known that the first column of the row comparison matches
 * the specified column of the index.  We can use additional columns of the
 * row comparison as index qualifications, so long as they match the index
 * in the "same direction", ie, the indexkeys are all on the same side of the
 * clause and the operators are all the same-type members of the opfamilies.
 *
 * If all the columns of the RowCompareExpr match in this way, we just use it
 * as-is, except for possibly commuting it to put the indexkeys on the left.
 *
 * Otherwise, we build a shortened RowCompareExpr (if more than one
 * column matches) or a simple OpExpr (if the first-column match is all
 * there is).  In these cases the modified clause is always "<=" or ">="
 * even when the original was "<" or ">" --- this is necessary to match all
 * the rows that could match the original.  (We are building a lossy version
 * of the row comparison when we do this, so we set lossy = true.)
 *
 * Note: this is really just the last half of match_rowcompare_to_indexcol,
 * but we split it out for comprehensibility.
 */
static IndexClause *
expand_indexqual_rowcompare(PlannerInfo *root,
							RestrictInfo *rinfo,
							int indexcol,
							IndexOptInfo *index,
							Oid expr_op,
							bool var_on_left)
{
	IndexClause *iclause = makeNode(IndexClause);
	RowCompareExpr *clause = (RowCompareExpr *) rinfo->clause;
	int			op_strategy;
	Oid			op_lefttype;
	Oid			op_righttype;
	int			matching_cols;
	List	   *expr_ops;
	List	   *opfamilies;
	List	   *lefttypes;
	List	   *righttypes;
	List	   *new_ops;
	List	   *var_args;
	List	   *non_var_args;

	iclause->rinfo = rinfo;
	iclause->indexcol = indexcol;

	if (var_on_left)
	{
		var_args = clause->largs;
		non_var_args = castNode(List, clause->rargs);
	}
	else
	{
		var_args = castNode(List, clause->rargs);
		non_var_args = clause->largs;
	}

	get_op_opfamily_properties(expr_op, index->opfamily[indexcol], false,
							   &op_strategy,
							   &op_lefttype,
							   &op_righttype);

	/* Initialize returned list of which index columns are used */
	iclause->indexcols = list_make1_int(indexcol);

	/* Build lists of ops, opfamilies and operator datatypes in case needed */
	expr_ops = list_make1_oid(expr_op);
	opfamilies = list_make1_oid(index->opfamily[indexcol]);
	lefttypes = list_make1_oid(op_lefttype);
	righttypes = list_make1_oid(op_righttype);

	/*
	 * See how many of the remaining columns match some index column in the
	 * same way.  As in match_clause_to_indexcol(), the "other" side of any
	 * potential index condition is OK as long as it doesn't use Vars from the
	 * indexed relation.
	 */
	matching_cols = 1;

	while (matching_cols < list_length(var_args))
	{
		Node	   *varop = (Node *) list_nth(var_args, matching_cols);
		Node	   *constop = (Node *) list_nth(non_var_args, matching_cols);
		int			i;

		expr_op = list_nth_oid(clause->opnos, matching_cols);
		if (!var_on_left)
		{
			/* indexkey is on right, so commute the operator */
			expr_op = get_commutator(expr_op);
			if (expr_op == InvalidOid)
				break;			/* operator is not usable */
		}
		if (bms_is_member(index->rel->relid, pull_varnos(root, constop)))
			break;				/* no good, Var on wrong side */
		if (contain_volatile_functions(constop))
			break;				/* no good, volatile comparison value */

		/*
		 * The Var side can match any key column of the index.
		 */
		for (i = 0; i < index->nkeycolumns; i++)
		{
			if (match_index_to_operand(varop, i, index) &&
				get_op_opfamily_strategy(expr_op,
										 index->opfamily[i]) == op_strategy &&
				IndexCollMatchesExprColl(index->indexcollations[i],
										 list_nth_oid(clause->inputcollids,
													  matching_cols)))
				break;
		}
		if (i >= index->nkeycolumns)
			break;				/* no match found */

		/* Add column number to returned list */
		iclause->indexcols = lappend_int(iclause->indexcols, i);

		/* Add operator info to lists */
		get_op_opfamily_properties(expr_op, index->opfamily[i], false,
								   &op_strategy,
								   &op_lefttype,
								   &op_righttype);
		expr_ops = lappend_oid(expr_ops, expr_op);
		opfamilies = lappend_oid(opfamilies, index->opfamily[i]);
		lefttypes = lappend_oid(lefttypes, op_lefttype);
		righttypes = lappend_oid(righttypes, op_righttype);

		/* This column matches, keep scanning */
		matching_cols++;
	}

	/* Result is non-lossy if all columns are usable as index quals */
	iclause->lossy = (matching_cols != list_length(clause->opnos));

	/*
	 * We can use rinfo->clause as-is if we have var on left and it's all
	 * usable as index quals.
	 */
	if (var_on_left && !iclause->lossy)
		iclause->indexquals = list_make1(rinfo);
	else
	{
		/*
		 * We have to generate a modified rowcompare (possibly just one
		 * OpExpr).  The painful part of this is changing < to <= or > to >=,
		 * so deal with that first.
		 */
		if (!iclause->lossy)
		{
			/* very easy, just use the commuted operators */
			new_ops = expr_ops;
		}
		else if (op_strategy == BTLessEqualStrategyNumber ||
				 op_strategy == BTGreaterEqualStrategyNumber)
		{
			/* easy, just use the same (possibly commuted) operators */
			new_ops = list_truncate(expr_ops, matching_cols);
		}
		else
		{
			ListCell   *opfamilies_cell;
			ListCell   *lefttypes_cell;
			ListCell   *righttypes_cell;

			if (op_strategy == BTLessStrategyNumber)
				op_strategy = BTLessEqualStrategyNumber;
			else if (op_strategy == BTGreaterStrategyNumber)
				op_strategy = BTGreaterEqualStrategyNumber;
			else
				elog(ERROR, "unexpected strategy number %d", op_strategy);
			new_ops = NIL;
			forthree(opfamilies_cell, opfamilies,
					 lefttypes_cell, lefttypes,
					 righttypes_cell, righttypes)
			{
				Oid			opfam = lfirst_oid(opfamilies_cell);
				Oid			lefttype = lfirst_oid(lefttypes_cell);
				Oid			righttype = lfirst_oid(righttypes_cell);

				expr_op = get_opfamily_member(opfam, lefttype, righttype,
											  op_strategy);
				if (!OidIsValid(expr_op))	/* should not happen */
					elog(ERROR, "missing operator %d(%u,%u) in opfamily %u",
						 op_strategy, lefttype, righttype, opfam);
				new_ops = lappend_oid(new_ops, expr_op);
			}
		}

		/* If we have more than one matching col, create a subset rowcompare */
		if (matching_cols > 1)
		{
			RowCompareExpr *rc = makeNode(RowCompareExpr);

			rc->rctype = (RowCompareType) op_strategy;
			rc->opnos = new_ops;
			rc->opfamilies = list_truncate(list_copy(clause->opfamilies),
										   matching_cols);
			rc->inputcollids = list_truncate(list_copy(clause->inputcollids),
											 matching_cols);
			rc->largs = list_truncate(copyObject(var_args),
									  matching_cols);
			rc->rargs = (Node *) list_truncate(copyObject(non_var_args),
									  matching_cols);
			iclause->indexquals = list_make1(make_simple_restrictinfo(root,
																	  (Expr *) rc));
		}
		else
		{
			Expr	   *op;

			/* We don't report an index column list in this case */
			iclause->indexcols = NIL;

			op = make_opclause(linitial_oid(new_ops), BOOLOID, false,
							   copyObject(linitial(var_args)),
							   copyObject(linitial(non_var_args)),
							   InvalidOid,
							   linitial_oid(clause->inputcollids));
			iclause->indexquals = list_make1(make_simple_restrictinfo(root, op));
		}
	}

	return iclause;
}


/****************************************************************************
 *				----  ROUTINES TO CHECK ORDERING OPERATORS	----
 ****************************************************************************/

/*
 * match_pathkeys_to_index
 *		Test whether an index can produce output ordered according to the
 *		given pathkeys using "ordering operators".
 *
 * If it can, return a list of suitable ORDER BY expressions, each of the form
 * "indexedcol operator pseudoconstant", along with an integer list of the
 * index column numbers (zero based) that each clause would be used with.
 * NIL lists are returned if the ordering is not achievable this way.
 *
 * On success, the result list is ordered by pathkeys, and in fact is
 * one-to-one with the requested pathkeys.
 */
static void
match_pathkeys_to_index(IndexOptInfo *index, List *pathkeys,
						List **orderby_clauses_p,
						List **clause_columns_p)
{
	List	   *orderby_clauses = NIL;
	List	   *clause_columns = NIL;
	ListCell   *lc1;

	*orderby_clauses_p = NIL;	/* set default results */
	*clause_columns_p = NIL;

	/* Only indexes with the amcanorderbyop property are interesting here */
	if (!index->amcanorderbyop)
		return;

	foreach(lc1, pathkeys)
	{
		PathKey    *pathkey = (PathKey *) lfirst(lc1);
		bool		found = false;
		ListCell   *lc2;

		/*
		 * Note: for any failure to match, we just return NIL immediately.
		 * There is no value in matching just some of the pathkeys.
		 */

		/* Pathkey must request default sort order for the target opfamily */
		if (pathkey->pk_strategy != BTLessStrategyNumber ||
			pathkey->pk_nulls_first)
			return;

		/* If eclass is volatile, no hope of using an indexscan */
		if (pathkey->pk_eclass->ec_has_volatile)
			return;

		/*
		 * Try to match eclass member expression(s) to index.  Note that child
		 * EC members are considered, but only when they belong to the target
		 * relation.  (Unlike regular members, the same expression could be a
		 * child member of more than one EC.  Therefore, the same index could
		 * be considered to match more than one pathkey list, which is OK
		 * here.  See also get_eclass_for_sort_expr.)
		 */
		foreach(lc2, pathkey->pk_eclass->ec_members)
		{
			EquivalenceMember *member = (EquivalenceMember *) lfirst(lc2);
			int			indexcol;

			/* No possibility of match if it references other relations */
			if (!bms_equal(member->em_relids, index->rel->relids))
				continue;

			/*
			 * We allow any column of the index to match each pathkey; they
			 * don't have to match left-to-right as you might expect.  This is
			 * correct for GiST, and it doesn't matter for SP-GiST because
			 * that doesn't handle multiple columns anyway, and no other
			 * existing AMs support amcanorderbyop.  We might need different
			 * logic in future for other implementations.
			 */
			for (indexcol = 0; indexcol < index->nkeycolumns; indexcol++)
			{
				Expr	   *expr;

				expr = match_clause_to_ordering_op(index,
												   indexcol,
												   member->em_expr,
												   pathkey->pk_opfamily);
				if (expr)
				{
					orderby_clauses = lappend(orderby_clauses, expr);
					clause_columns = lappend_int(clause_columns, indexcol);
					found = true;
					break;
				}
			}

			if (found)			/* don't want to look at remaining members */
				break;
		}

		if (!found)				/* fail if no match for this pathkey */
			return;
	}

	*orderby_clauses_p = orderby_clauses;	/* success! */
	*clause_columns_p = clause_columns;
}

/*
 * match_clause_to_ordering_op
 *	  Determines whether an ordering operator expression matches an
 *	  index column.
 *
 *	  This is similar to, but simpler than, match_clause_to_indexcol.
 *	  We only care about simple OpExpr cases.  The input is a bare
 *	  expression that is being ordered by, which must be of the form
 *	  (indexkey op const) or (const op indexkey) where op is an ordering
 *	  operator for the column's opfamily.
 *
 * 'index' is the index of interest.
 * 'indexcol' is a column number of 'index' (counting from 0).
 * 'clause' is the ordering expression to be tested.
 * 'pk_opfamily' is the btree opfamily describing the required sort order.
 *
 * Note that we currently do not consider the collation of the ordering
 * operator's result.  In practical cases the result type will be numeric
 * and thus have no collation, and it's not very clear what to match to
 * if it did have a collation.  The index's collation should match the
 * ordering operator's input collation, not its result.
 *
 * If successful, return 'clause' as-is if the indexkey is on the left,
 * otherwise a commuted copy of 'clause'.  If no match, return NULL.
 */
static Expr *
match_clause_to_ordering_op(IndexOptInfo *index,
							int indexcol,
							Expr *clause,
							Oid pk_opfamily)
{
	Oid			opfamily;
	Oid			idxcollation;
	Node	   *leftop,
			   *rightop;
	Oid			expr_op;
	Oid			expr_coll;
	Oid			sortfamily;
	bool		commuted;

	Assert(indexcol < index->nkeycolumns);

	opfamily = index->opfamily[indexcol];
	idxcollation = index->indexcollations[indexcol];

	/*
	 * Clause must be a binary opclause.
	 */
	if (!is_opclause(clause))
		return NULL;
	leftop = get_leftop(clause);
	rightop = get_rightop(clause);
	if (!leftop || !rightop)
		return NULL;
	expr_op = ((OpExpr *) clause)->opno;
	expr_coll = ((OpExpr *) clause)->inputcollid;

	/*
	 * We can forget the whole thing right away if wrong collation.
	 */
	if (!IndexCollMatchesExprColl(idxcollation, expr_coll))
		return NULL;

	/*
	 * Check for clauses of the form: (indexkey operator constant) or
	 * (constant operator indexkey).
	 */
	if (match_index_to_operand(leftop, indexcol, index) &&
		!contain_var_clause(rightop) &&
		!contain_volatile_functions(rightop))
	{
		commuted = false;
	}
	else if (match_index_to_operand(rightop, indexcol, index) &&
			 !contain_var_clause(leftop) &&
			 !contain_volatile_functions(leftop))
	{
		/* Might match, but we need a commuted operator */
		expr_op = get_commutator(expr_op);
		if (expr_op == InvalidOid)
			return NULL;
		commuted = true;
	}
	else
		return NULL;

	/*
	 * Is the (commuted) operator an ordering operator for the opfamily? And
	 * if so, does it yield the right sorting semantics?
	 */
	sortfamily = get_op_opfamily_sortfamily(expr_op, opfamily);
	if (sortfamily != pk_opfamily)
		return NULL;

	/* We have a match.  Return clause or a commuted version thereof. */
	if (commuted)
	{
		OpExpr	   *newclause = makeNode(OpExpr);

		/* flat-copy all the fields of clause */
		memcpy(newclause, clause, sizeof(OpExpr));

		/* commute it */
		newclause->opno = expr_op;
		newclause->opfuncid = InvalidOid;
		newclause->args = list_make2(rightop, leftop);

		clause = (Expr *) newclause;
	}

	return clause;
}


/****************************************************************************
 *				----  ROUTINES TO DO PARTIAL INDEX PREDICATE TESTS	----
 ****************************************************************************/

/*
 * check_index_predicates
 *		Set the predicate-derived IndexOptInfo fields for each index
 *		of the specified relation.
 *
 * predOK is set true if the index is partial and its predicate is satisfied
 * for this query, ie the query's WHERE clauses imply the predicate.
 *
 * indrestrictinfo is set to the relation's baserestrictinfo list less any
 * conditions that are implied by the index's predicate.  (Obviously, for a
 * non-partial index, this is the same as baserestrictinfo.)  Such conditions
 * can be dropped from the plan when using the index, in certain cases.
 *
 * At one time it was possible for this to get re-run after adding more
 * restrictions to the rel, thus possibly letting us prove more indexes OK.
 * That doesn't happen any more (at least not in the core code's usage),
 * but this code still supports it in case extensions want to mess with the
 * baserestrictinfo list.  We assume that adding more restrictions can't make
 * an index not predOK.  We must recompute indrestrictinfo each time, though,
 * to make sure any newly-added restrictions get into it if needed.
 */
void
check_index_predicates(PlannerInfo *root, RelOptInfo *rel)
{
	List	   *clauselist;
	bool		have_partial;
	bool		is_target_rel;
	Relids		otherrels;
	ListCell   *lc;

	/* Indexes are available only on base or "other" member relations. */
	Assert(IS_SIMPLE_REL(rel));

	/*
	 * Initialize the indrestrictinfo lists to be identical to
	 * baserestrictinfo, and check whether there are any partial indexes.  If
	 * not, this is all we need to do.
	 */
	have_partial = false;
	foreach(lc, rel->indexlist)
	{
		IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);

		index->indrestrictinfo = rel->baserestrictinfo;
		if (index->indpred)
			have_partial = true;
	}
	if (!have_partial)
		return;

	/*
	 * Construct a list of clauses that we can assume true for the purpose of
	 * proving the index(es) usable.  Restriction clauses for the rel are
	 * always usable, and so are any join clauses that are "movable to" this
	 * rel.  Also, we can consider any EC-derivable join clauses (which must
	 * be "movable to" this rel, by definition).
	 */
	clauselist = list_copy(rel->baserestrictinfo);

	/* Scan the rel's join clauses */
	foreach(lc, rel->joininfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		/* Check if clause can be moved to this rel */
		if (!join_clause_is_movable_to(rinfo, rel))
			continue;

		clauselist = lappend(clauselist, rinfo);
	}

	/*
	 * Add on any equivalence-derivable join clauses.  Computing the correct
	 * relid sets for generate_join_implied_equalities is slightly tricky
	 * because the rel could be a child rel rather than a true baserel, and in
	 * that case we must remove its parents' relid(s) from all_baserels.
	 */
	if (rel->reloptkind == RELOPT_OTHER_MEMBER_REL)
		otherrels = bms_difference(root->all_baserels,
								   find_childrel_parents(root, rel));
	else
		otherrels = bms_difference(root->all_baserels, rel->relids);

	if (!bms_is_empty(otherrels))
		clauselist =
			list_concat(clauselist,
						generate_join_implied_equalities(root,
														 bms_union(rel->relids,
																   otherrels),
														 otherrels,
														 rel));

	/*
	 * Normally we remove quals that are implied by a partial index's
	 * predicate from indrestrictinfo, indicating that they need not be
	 * checked explicitly by an indexscan plan using this index.  However, if
	 * the rel is a target relation of UPDATE/DELETE/MERGE/SELECT FOR UPDATE,
	 * we cannot remove such quals from the plan, because they need to be in
	 * the plan so that they will be properly rechecked by EvalPlanQual
	 * testing.  Some day we might want to remove such quals from the main
	 * plan anyway and pass them through to EvalPlanQual via a side channel;
	 * but for now, we just don't remove implied quals at all for target
	 * relations.
	 */
	is_target_rel = (bms_is_member(rel->relid, root->all_result_relids) ||
					 get_plan_rowmark(root->rowMarks, rel->relid) != NULL);

	/*
	 * Now try to prove each index predicate true, and compute the
	 * indrestrictinfo lists for partial indexes.  Note that we compute the
	 * indrestrictinfo list even for non-predOK indexes; this might seem
	 * wasteful, but we may be able to use such indexes in OR clauses, cf
	 * generate_bitmap_or_paths().
	 */
	foreach(lc, rel->indexlist)
	{
		IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);
		ListCell   *lcr;

		if (index->indpred == NIL)
			continue;			/* ignore non-partial indexes here */

		if (!index->predOK)		/* don't repeat work if already proven OK */
			index->predOK = predicate_implied_by(index->indpred, clauselist,
												 false);

		/* If rel is an update target, leave indrestrictinfo as set above */
		if (is_target_rel)
			continue;

		/* Else compute indrestrictinfo as the non-implied quals */
		index->indrestrictinfo = NIL;
		foreach(lcr, rel->baserestrictinfo)
		{
			RestrictInfo *rinfo = (RestrictInfo *) lfirst(lcr);

			/* predicate_implied_by() assumes first arg is immutable */
			if (contain_mutable_functions((Node *) rinfo->clause) ||
				!predicate_implied_by(list_make1(rinfo->clause),
									  index->indpred, false))
				index->indrestrictinfo = lappend(index->indrestrictinfo, rinfo);
		}
	}
}

/****************************************************************************
 *				----  ROUTINES TO CHECK EXTERNALLY-VISIBLE CONDITIONS  ----
 ****************************************************************************/

/*
 * ec_member_matches_indexcol
 *	  Test whether an EquivalenceClass member matches an index column.
 *
 * This is a callback for use by generate_implied_equalities_for_column.
 */
static bool
ec_member_matches_indexcol(PlannerInfo *root, RelOptInfo *rel,
						   EquivalenceClass *ec, EquivalenceMember *em,
						   void *arg)
{
	IndexOptInfo *index = ((ec_member_matches_arg *) arg)->index;
	int			indexcol = ((ec_member_matches_arg *) arg)->indexcol;
	Oid			curFamily;
	Oid			curCollation;

	Assert(indexcol < index->nkeycolumns);

	curFamily = index->opfamily[indexcol];
	curCollation = index->indexcollations[indexcol];

	/*
	 * If it's a btree or lsm index, we can reject it if its opfamily isn't
	 * compatible with the EC, since no clause generated from the EC could be
	 * used with the index.  For non-btree indexes, we can't easily tell
	 * whether clauses generated from the EC could be used with the index, so
	 * don't check the opfamily.  This might mean we return "true" for a
	 * useless EC, so we have to recheck the results of
	 * generate_implied_equalities_for_column; see
	 * match_eclass_clauses_to_index.
	 */
	if ((index->relam == BTREE_AM_OID || index->relam == LSM_AM_OID) &&
		!list_member_oid(ec->ec_opfamilies, curFamily))
		return false;

	/* We insist on collation match for all index types, though */
	if (!IndexCollMatchesExprColl(curCollation, ec->ec_collation))
		return false;

	return match_index_to_operand((Node *) em->em_expr, indexcol, index);
}

/*
 * relation_has_unique_index_for
 *	  Determine whether the relation provably has at most one row satisfying
 *	  a set of equality conditions, because the conditions constrain all
 *	  columns of some unique index.
 *
 * The conditions can be represented in either or both of two ways:
 * 1. A list of RestrictInfo nodes, where the caller has already determined
 * that each condition is a mergejoinable equality with an expression in
 * this relation on one side, and an expression not involving this relation
 * on the other.  The transient outer_is_left flag is used to identify which
 * side we should look at: left side if outer_is_left is false, right side
 * if it is true.
 * 2. A list of expressions in this relation, and a corresponding list of
 * equality operators. The caller must have already checked that the operators
 * represent equality.  (Note: the operators could be cross-type; the
 * expressions should correspond to their RHS inputs.)
 *
 * The caller need only supply equality conditions arising from joins;
 * this routine automatically adds in any usable baserestrictinfo clauses.
 * (Note that the passed-in restrictlist will be destructively modified!)
 */
bool
relation_has_unique_index_for(PlannerInfo *root, RelOptInfo *rel,
							  List *restrictlist,
							  List *exprlist, List *oprlist)
{
	ListCell   *ic;

	Assert(list_length(exprlist) == list_length(oprlist));

	/* Short-circuit if no indexes... */
	if (rel->indexlist == NIL)
		return false;

	/*
	 * Examine the rel's restriction clauses for usable var = const clauses
	 * that we can add to the restrictlist.
	 */
	foreach(ic, rel->baserestrictinfo)
	{
		RestrictInfo *restrictinfo = (RestrictInfo *) lfirst(ic);

		/*
		 * Note: can_join won't be set for a restriction clause, but
		 * mergeopfamilies will be if it has a mergejoinable operator and
		 * doesn't contain volatile functions.
		 */
		if (restrictinfo->mergeopfamilies == NIL)
			continue;			/* not mergejoinable */

		/*
		 * The clause certainly doesn't refer to anything but the given rel.
		 * If either side is pseudoconstant then we can use it.
		 */
		if (bms_is_empty(restrictinfo->left_relids))
		{
			/* righthand side is inner */
			restrictinfo->outer_is_left = true;
		}
		else if (bms_is_empty(restrictinfo->right_relids))
		{
			/* lefthand side is inner */
			restrictinfo->outer_is_left = false;
		}
		else
			continue;

		/* OK, add to list */
		restrictlist = lappend(restrictlist, restrictinfo);
	}

	/* Short-circuit the easy case */
	if (restrictlist == NIL && exprlist == NIL)
		return false;

	/* Examine each index of the relation ... */
	foreach(ic, rel->indexlist)
	{
		IndexOptInfo *ind = (IndexOptInfo *) lfirst(ic);
		int			c;

		/*
		 * If the index is not unique, or not immediately enforced, or if it's
		 * a partial index that doesn't match the query, it's useless here.
		 */
		if (!ind->unique || !ind->immediate ||
			(ind->indpred != NIL && !ind->predOK))
			continue;

		/*
		 * Try to find each index column in the lists of conditions.  This is
		 * O(N^2) or worse, but we expect all the lists to be short.
		 */
		for (c = 0; c < ind->nkeycolumns; c++)
		{
			bool		matched = false;
			ListCell   *lc;
			ListCell   *lc2;

			foreach(lc, restrictlist)
			{
				RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
				Node	   *rexpr;

				/*
				 * The condition's equality operator must be a member of the
				 * index opfamily, else it is not asserting the right kind of
				 * equality behavior for this index.  We check this first
				 * since it's probably cheaper than match_index_to_operand().
				 */
				if (!list_member_oid(rinfo->mergeopfamilies, ind->opfamily[c]))
					continue;

				/*
				 * XXX at some point we may need to check collations here too.
				 * For the moment we assume all collations reduce to the same
				 * notion of equality.
				 */

				/* OK, see if the condition operand matches the index key */
				if (rinfo->outer_is_left)
					rexpr = get_rightop(rinfo->clause);
				else
					rexpr = get_leftop(rinfo->clause);

				if (match_index_to_operand(rexpr, c, ind))
				{
					matched = true; /* column is unique */
					break;
				}
			}

			if (matched)
				continue;

			forboth(lc, exprlist, lc2, oprlist)
			{
				Node	   *expr = (Node *) lfirst(lc);
				Oid			opr = lfirst_oid(lc2);

				/* See if the expression matches the index key */
				if (!match_index_to_operand(expr, c, ind))
					continue;

				/*
				 * The equality operator must be a member of the index
				 * opfamily, else it is not asserting the right kind of
				 * equality behavior for this index.  We assume the caller
				 * determined it is an equality operator, so we don't need to
				 * check any more tightly than this.
				 */
				if (!op_in_opfamily(opr, ind->opfamily[c]))
					continue;

				/*
				 * XXX at some point we may need to check collations here too.
				 * For the moment we assume all collations reduce to the same
				 * notion of equality.
				 */

				matched = true; /* column is unique */
				break;
			}

			if (!matched)
				break;			/* no match; this index doesn't help us */
		}

		/* Matched all key columns of this index? */
		if (c == ind->nkeycolumns)
			return true;
	}

	return false;
}

/*
 * indexcol_is_bool_constant_for_query
 *
 * If an index column is constrained to have a constant value by the query's
 * WHERE conditions, then it's irrelevant for sort-order considerations.
 * Usually that means we have a restriction clause WHERE indexcol = constant,
 * which gets turned into an EquivalenceClass containing a constant, which
 * is recognized as redundant by build_index_pathkeys().  But if the index
 * column is a boolean variable (or expression), then we are not going to
 * see WHERE indexcol = constant, because expression preprocessing will have
 * simplified that to "WHERE indexcol" or "WHERE NOT indexcol".  So we are not
 * going to have a matching EquivalenceClass (unless the query also contains
 * "ORDER BY indexcol").  To allow such cases to work the same as they would
 * for non-boolean values, this function is provided to detect whether the
 * specified index column matches a boolean restriction clause.
 */
bool
indexcol_is_bool_constant_for_query(PlannerInfo *root,
									IndexOptInfo *index,
									int indexcol)
{
	ListCell   *lc;

	/* If the index isn't boolean, we can't possibly get a match */
	if (!IsBooleanOpfamily(index->opfamily[indexcol]))
		return false;

	/* Check each restriction clause for the index's rel */
	foreach(lc, index->rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);

		/*
		 * As in match_clause_to_indexcol, never match pseudoconstants to
		 * indexes.  (It might be semantically okay to do so here, but the
		 * odds of getting a match are negligible, so don't waste the cycles.)
		 */
		if (rinfo->pseudoconstant)
			continue;

		/* See if we can match the clause's expression to the index column */
		if (match_boolean_index_clause(root, rinfo, indexcol, index))
			return true;
	}

	return false;
}


/****************************************************************************
 *				----  ROUTINES TO CHECK OPERANDS  ----
 ****************************************************************************/

/*
 * match_index_to_operand()
 *	  Generalized test for a match between an index's key
 *	  and the operand on one side of a restriction or join clause.
 *
 * operand: the nodetree to be compared to the index
 * indexcol: the column number of the index (counting from 0)
 * index: the index of interest
 *
 * Note that we aren't interested in collations here; the caller must check
 * for a collation match, if it's dealing with an operator where that matters.
 *
 * This is exported for use in selfuncs.c.
 */
bool
match_index_to_operand(Node *operand,
					   int indexcol,
					   IndexOptInfo *index)
{
	int			indkey;

	/*
	 * Ignore any RelabelType node above the operand.   This is needed to be
	 * able to apply indexscanning in binary-compatible-operator cases. Note:
	 * we can assume there is at most one RelabelType node;
	 * eval_const_expressions() will have simplified if more than one.
	 */
	if (operand && IsA(operand, RelabelType))
		operand = (Node *) ((RelabelType *) operand)->arg;

	indkey = index->indexkeys[indexcol];
	if (indkey != 0)
	{

		if (operand && IsA(operand, FuncExpr))
		{
			/*
			 * YB: Forming an estimate to see if this call can be pushed down
			 * by assessing whether or not its parameters are all column
			 * variables and whether or not the number of arguments to the call
			 * is the same as the number of hash columns in the primary key
			 * of the index in question
			 */
			FuncExpr *fn = (FuncExpr *) operand;
			if (fn->funcid == YB_HASH_CODE_OID
				&& fn->args->length > 0
				&& index->nhashcolumns == fn->args->length)
			{
				Relation indrel = RelationIdGetRelation(index->indexoid);
				Bitmapset *hash_keys = NULL;
				for (int natt = 1;
						natt <= indrel->rd_index->indnkeyatts; natt++)
				{
					if (indrel->rd_indoption[natt - 1] & INDOPTION_HASH)
					{
						int table_att = index->indexkeys[natt - 1];
						hash_keys = bms_add_member(hash_keys,
										YBAttnumToBmsIndex(indrel, table_att));
					}
				}
				ListCell *ls;
				bool can_pushdown_hash_call = true;
				Bitmapset *args_bms = NULL;
				int last_index_att = -1;
				foreach(ls, fn->args)
				{
					Expr *arg = (Expr *) lfirst(ls);
					if (!IsA(arg, Var))
					{
						can_pushdown_hash_call = false;
						break;
					}

					Var *var = (Var *) arg;

					if (index->rel->relid != var->varno)
					{
						can_pushdown_hash_call = false;
						break;
					}

					/* YB: Need to make sure that the arguments to
					 * yb_hash_code are in the correct order
					 * we can make this for loop to map from index att
					 * to table att slightly more efficient by starting the
					 * loop from last_index_att */
					int index_att = -1;
					for (int natt = 1;
						natt <= indrel->rd_index->indnkeyatts; natt++)
					{
						int cand_table_att = index->indexkeys[natt - 1];
						if (cand_table_att == var->varattno)
						{
							index_att = natt;
							break;
						}
					}

					if (index_att <= last_index_att) {
						can_pushdown_hash_call = false;
						break;
					} else {
						last_index_att = index_att;
					}

					int arg_bms_index = YBAttnumToBmsIndex(indrel,
															var->varattno);
					args_bms = bms_add_member(args_bms, arg_bms_index);
				}
				can_pushdown_hash_call &= bms_equal(args_bms, hash_keys);

				RelationClose(indrel);
				bms_free(args_bms);
				bms_free(hash_keys);
				return can_pushdown_hash_call;
			}
		}
		/*
		 * Simple index column; operand must be a matching Var
		 */

		Var *operand_var = NULL;
		if (operand && IsA(operand, Var))
			operand_var = (Var *) operand;

		if (operand_var &&
			index->rel->relid == operand_var->varno &&
			indkey == operand_var->varattno)
			return true;
	}
	else
	{
		/*
		 * Index expression; find the correct expression.  (This search could
		 * be avoided, at the cost of complicating all the callers of this
		 * routine; doesn't seem worth it.)
		 */
		ListCell   *indexpr_item;
		int			i;
		Node	   *indexkey;

		indexpr_item = list_head(index->indexprs);
		for (i = 0; i < indexcol; i++)
		{
			if (index->indexkeys[i] == 0)
			{
				if (indexpr_item == NULL)
					elog(ERROR, "wrong number of index expressions");
				indexpr_item = lnext(index->indexprs, indexpr_item);
			}
		}
		if (indexpr_item == NULL)
			elog(ERROR, "wrong number of index expressions");
		indexkey = (Node *) lfirst(indexpr_item);

		/*
		 * Does it match the operand?  Again, strip any relabeling.
		 */
		if (indexkey && IsA(indexkey, RelabelType))
			indexkey = (Node *) ((RelabelType *) indexkey)->arg;

		if (equal(indexkey, operand))
			return true;
	}

	return false;
}

/*
 * is_pseudo_constant_for_index()
 *	  Test whether the given expression can be used as an indexscan
 *	  comparison value.
 *
 * An indexscan comparison value must not contain any volatile functions,
 * and it can't contain any Vars of the index's own table.  Vars of
 * other tables are okay, though; in that case we'd be producing an
 * indexqual usable in a parameterized indexscan.  This is, therefore,
 * a weaker condition than is_pseudo_constant_clause().
 *
 * This function is exported for use by planner support functions,
 * which will have available the IndexOptInfo, but not any RestrictInfo
 * infrastructure.  It is making the same test made by functions above
 * such as match_opclause_to_indexcol(), but those rely where possible
 * on RestrictInfo information about variable membership.
 *
 * expr: the nodetree to be checked
 * index: the index of interest
 */
bool
is_pseudo_constant_for_index(PlannerInfo *root, Node *expr, IndexOptInfo *index)
{
	/* pull_varnos is cheaper than volatility check, so do that first */
	if (bms_is_member(index->rel->relid, pull_varnos(root, expr)))
		return false;			/* no good, contains Var of table */
	if (contain_volatile_functions(expr))
		return false;			/* no good, volatile comparison value */
	return true;
}

static bool
is_hash_column_in_lsm_index(const IndexOptInfo* index, int columnIndex)
{
	return (index->relam == LSM_AM_OID && columnIndex < index->nhashcolumns);
}

/*
 * YB: yb_can_pushdown_distinct
 *
 * Check if distinct index scan is appropriate for the given relation/index.
 *
 * Cannot pushdown distinct if:
 * - distinct pushdown is disabled by the user
 * - there is no distinct clause
 * - index is not YB LSM
 * - distinct clause does not support distinct pushdown
 * - any of the relevant join clauses do not support distinct pushdown
 * - any of the implied join equalities do not support distinct pushdown
 *
 * See yb_reject_distinct_pushdown to understand when pushdown is
 * not supported by clauses/expressions.
 */
static bool
yb_can_pushdown_distinct(PlannerInfo *root, IndexOptInfo *index)
{
	Relids	  otherrels;
	List	 *joininfo;
	List	 *clause_list;
	ListCell *lc;

	/*
	 * Computing the correct relids sets is tricky.
	 * See check_index_predicates for details.
	 */
	if (index->rel->reloptkind == RELOPT_OTHER_MEMBER_REL)
		otherrels = bms_difference(root->all_baserels,
								   find_childrel_parents(root, index->rel));
	else
		otherrels = bms_difference(root->all_baserels, index->rel->relids);

	/* Collect join clauses and implied join clauses. */
	joininfo = list_concat(list_copy(index->rel->joininfo),
						   generate_join_implied_equalities(root,
															bms_union(index->rel->relids, otherrels),
															otherrels,
															index->rel));

	clause_list = NIL;
	foreach(lc, joininfo)
	{
		RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
		clause_list = lappend(clause_list, rinfo->clause);
	}

	/*
	 * We use root->parse->distinctClause and not root->distinct_pathkeys
	 * because root->distinct_pathkeys can be NULL even for DISTINCT queries.
	 * This happens when all the requested columns are constant.
	 */
	return
		IsYugaByteEnabled() &&
		yb_enable_distinct_pushdown &&
		root->parse->distinctClause != NIL &&
		index->relam == LSM_AM_OID &&
		!root->parse->hasAggs &&
		!root->parse->hasWindowFuncs &&
		!root->parse->hasTargetSRFs &&
		!yb_reject_distinct_pushdown((Node *)
			get_sortgrouplist_exprs(root->parse->distinctClause,
									root->processed_tlist)) &&
		!yb_reject_distinct_pushdown((Node *) clause_list);
}
