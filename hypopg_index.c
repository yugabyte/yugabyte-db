/*-------------------------------------------------------------------------
 *
 * hypopg_index.c: Implementation of hypothetical indexes for PostgreSQL
 *
 * This file contains all the internal code related to hypothetical indexes
 * support.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (C) 2015-2023: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
 */

#include <unistd.h>
#include <math.h>

#include "postgres.h"
#include "fmgr.h"

#include "funcapi.h"
#include "miscadmin.h"

#if PG_VERSION_NUM >= 90500
#include "access/brin.h"
#include "access/brin_page.h"
#include "access/brin_tuple.h"
#endif
#include "access/gist.h"
#if PG_VERSION_NUM >= 90300
#include "access/htup_details.h"
#endif
#include "access/nbtree.h"
#include "access/reloptions.h"
#include "access/spgist.h"
#include "access/spgist_private.h"
#include "access/sysattr.h"
#include "access/xlog.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_class.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#if PG_VERSION_NUM >= 120000
#include "nodes/makefuncs.h"
#endif
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#if PG_VERSION_NUM < 120000
#include "optimizer/var.h"
#else
#include "optimizer/optimizer.h"
#endif
#include "parser/parse_utilcmd.h"
#include "parser/parser.h"
#if PG_VERSION_NUM >= 120000
#include "port/pg_bitutils.h"
#endif
#include "storage/bufmgr.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#if PG_VERSION_NUM >= 90500
#include "utils/ruleutils.h"
#endif
#include "utils/syscache.h"

#include "include/hypopg.h"
#include "include/hypopg_index.h"

#if PG_VERSION_NUM >= 90600
/* this will be updated, when needed, by hypo_discover_am */
static Oid	BLOOM_AM_OID = InvalidOid;
#endif

/*--- Variables exported ---*/

explain_get_index_name_hook_type prev_explain_get_index_name_hook;
List	   *hypoIndexes;
List	   *hypoHiddenIndexes;

/*--- Functions --- */

PG_FUNCTION_INFO_V1(hypopg);
PG_FUNCTION_INFO_V1(hypopg_create_index);
PG_FUNCTION_INFO_V1(hypopg_drop_index);
PG_FUNCTION_INFO_V1(hypopg_relation_size);
PG_FUNCTION_INFO_V1(hypopg_get_indexdef);
PG_FUNCTION_INFO_V1(hypopg_reset_index);
PG_FUNCTION_INFO_V1(hypopg_hide_index);
PG_FUNCTION_INFO_V1(hypopg_unhide_index);
PG_FUNCTION_INFO_V1(hypopg_unhide_all_indexes);
PG_FUNCTION_INFO_V1(hypopg_hidden_indexes);


static void hypo_addIndex(hypoIndex * entry);
static bool hypo_can_return(hypoIndex * entry, Oid atttype, int i, char *amname);
static void hypo_discover_am(char *amname, Oid oid);
static void hypo_estimate_index_simple(hypoIndex * entry,
									   BlockNumber *pages, double *tuples);
static void hypo_estimate_index(hypoIndex * entry, RelOptInfo *rel);
static int	hypo_estimate_index_colsize(hypoIndex * entry, int col);
static void hypo_index_pfree(hypoIndex * entry);
static bool hypo_index_remove(Oid indexid);
static bool hypo_index_unhide(Oid indexid);
static const hypoIndex *hypo_index_store_parsetree(IndexStmt *node,
												   const char *queryString);
static hypoIndex * hypo_newIndex(Oid relid, char *accessMethod, int nkeycolumns,
								 int ninccolumns,
								 List *options);
static void hypo_set_indexname(hypoIndex * entry, char *indexname);


/*
 * palloc a new hypoIndex, and give it a new OID, and some other global stuff.
 * This function also parse index storage options (if any) to check if they're
 * valid.
 */
static hypoIndex *
hypo_newIndex(Oid relid, char *accessMethod, int nkeycolumns, int ninccolumns,
			  List *options)
{
	/* must be declared "volatile", because used in a PG_CATCH() */
	hypoIndex  *volatile entry;
	MemoryContext oldcontext;
	HeapTuple	tuple;
	Oid			oid;

#if PG_VERSION_NUM >= 90600
	IndexAmRoutine *amroutine;
	amoptions_function amoptions;
#else
	RegProcedure amoptions;
#endif

	tuple = SearchSysCache1(AMNAME, PointerGetDatum(accessMethod));

	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("hypopg: access method \"%s\" does not exist",
						accessMethod)));
	}

#if PG_VERSION_NUM < 120000
	oid = HeapTupleGetOid(tuple);
#else
	oid = ((Form_pg_am) GETSTRUCT(tuple))->oid;
#endif

	hypo_discover_am(accessMethod, oid);

	oldcontext = MemoryContextSwitchTo(HypoMemoryContext);

	entry = palloc0(sizeof(hypoIndex));

	entry->relam = oid;

#if PG_VERSION_NUM >= 90600

	/*
	 * Since 9.6, AM informations are available through an amhandler function,
	 * returning an IndexAmRoutine containing what's needed.
	 */
	amroutine = GetIndexAmRoutine(((Form_pg_am) GETSTRUCT(tuple))->amhandler);
	entry->amcostestimate = amroutine->amcostestimate;
	entry->amcanreturn = amroutine->amcanreturn;
	entry->amcanorderbyop = amroutine->amcanorderbyop;
	entry->amoptionalkey = amroutine->amoptionalkey;
	entry->amsearcharray = amroutine->amsearcharray;
	entry->amsearchnulls = amroutine->amsearchnulls;
	entry->amhasgettuple = (amroutine->amgettuple != NULL);
	entry->amhasgetbitmap = (amroutine->amgetbitmap != NULL);
	entry->amcanunique = amroutine->amcanunique;
	entry->amcanmulticol = amroutine->amcanmulticol;
	amoptions = amroutine->amoptions;
	entry->amcanorder = amroutine->amcanorder;
#if PG_VERSION_NUM >= 110000
	entry->amcanparallel = amroutine->amcanparallel;
	entry->amcaninclude = amroutine->amcaninclude;
#endif
#else
	/* Up to 9.5, all information is available in the pg_am tuple */
	entry->amcostestimate = ((Form_pg_am) GETSTRUCT(tuple))->amcostestimate;
	entry->amcanreturn = ((Form_pg_am) GETSTRUCT(tuple))->amcanreturn;
	entry->amcanorderbyop = ((Form_pg_am) GETSTRUCT(tuple))->amcanorderbyop;
	entry->amoptionalkey = ((Form_pg_am) GETSTRUCT(tuple))->amoptionalkey;
	entry->amsearcharray = ((Form_pg_am) GETSTRUCT(tuple))->amsearcharray;
	entry->amsearchnulls = ((Form_pg_am) GETSTRUCT(tuple))->amsearchnulls;
	entry->amhasgettuple = OidIsValid(((Form_pg_am) GETSTRUCT(tuple))->amgettuple);
	entry->amhasgetbitmap = OidIsValid(((Form_pg_am) GETSTRUCT(tuple))->amgetbitmap);
	entry->amcanunique = ((Form_pg_am) GETSTRUCT(tuple))->amcanunique;
	entry->amcanmulticol = ((Form_pg_am) GETSTRUCT(tuple))->amcanmulticol;
	amoptions = ((Form_pg_am) GETSTRUCT(tuple))->amoptions;
	entry->amcanorder = ((Form_pg_am) GETSTRUCT(tuple))->amcanorder;
#endif

	ReleaseSysCache(tuple);
	entry->indexname = palloc0(NAMEDATALEN);
	/* palloc all arrays */
	entry->indexkeys = palloc0(sizeof(short int) * (nkeycolumns + ninccolumns));
	entry->indexcollations = palloc0(sizeof(Oid) * nkeycolumns);
	entry->opfamily = palloc0(sizeof(Oid) * nkeycolumns);
	entry->opclass = palloc0(sizeof(Oid) * nkeycolumns);
	entry->opcintype = palloc0(sizeof(Oid) * nkeycolumns);
	/* only palloc sort related fields if needed */
	if ((entry->relam == BTREE_AM_OID) || (entry->amcanorder))
	{
		if (entry->relam != BTREE_AM_OID)
			entry->sortopfamily = palloc0(sizeof(Oid) * nkeycolumns);
		entry->reverse_sort = palloc0(sizeof(bool) * nkeycolumns);
		entry->nulls_first = palloc0(sizeof(bool) * nkeycolumns);
	}
	else
	{
		entry->sortopfamily = NULL;
		entry->reverse_sort = NULL;
		entry->nulls_first = NULL;
	}
#if PG_VERSION_NUM >= 90500
	entry->canreturn = palloc0(sizeof(bool) * (nkeycolumns + ninccolumns));
#endif
	entry->indexprs = NIL;
	entry->indpred = NIL;
	entry->options = (List *) copyObject(options);

	MemoryContextSwitchTo(oldcontext);

	entry->oid = hypo_getNewOid(relid);
	entry->relid = relid;
	entry->immediate = true;

	if (options != NIL)
	{
		Datum		reloptions;

		/*
		 * Parse AM-specific options, convert to text array form, validate.
		 */
		reloptions = transformRelOptions((Datum) 0, options,
										 NULL, NULL, false, false);

		(void) index_reloptions(amoptions, reloptions, true);
	}

	PG_TRY();
	{
		/*
		 * reject unsupported am. It could be done earlier but it's simpler
		 * (and was previously done) here.
		 */
		if (entry->relam != BTREE_AM_OID
#if PG_VERSION_NUM >= 90500
			&& entry->relam != BRIN_AM_OID
#endif
#if PG_VERSION_NUM >= 90600
			&& entry->relam != BLOOM_AM_OID
#endif
#if PG_VERSION_NUM >= 100000
			/*
			 * Only support hash indexes for pg10+.  In previous version they
			 * weren't crash safe, and changes in pg10+ also significantly
			 * changed the disk space allocation.
			 */
			 && entry->relam != HASH_AM_OID
#endif
			)
		{
			/*
			 * do not store hypothetical indexes with access method not
			 * supported
			 */
			elog(ERROR, "hypopg: access method \"%s\" is not supported",
				 accessMethod);
			break;
		}

		/* No more elog beyond this point. */
	}
	PG_CATCH();
	{
		/* Free what was palloc'd in HypoMemoryContext */
		hypo_index_pfree(entry);

		PG_RE_THROW();
	}
	PG_END_TRY();

	return entry;
}

/* Add an hypoIndex to hypoIndexes */
static void
hypo_addIndex(hypoIndex * entry)
{
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(HypoMemoryContext);

	hypoIndexes = lappend(hypoIndexes, entry);

	MemoryContextSwitchTo(oldcontext);
}

/*
 * Remove cleanly all hypothetical indexes by calling hypo_index_remove() on
 * each entry. hypo_index_remove() function pfree all allocated memory
 */
void
hypo_index_reset(void)
{
	ListCell   *lc;

	/*
	 * The cell is removed in hypo_index_remove(), so we can't iterate using
	 * standard foreach / lnext macros.
	 */
	while ((lc = list_head(hypoIndexes)) != NULL)
	{
		hypoIndex  *entry = (hypoIndex *) lfirst(lc);

		hypo_index_remove(entry->oid);
	}

	list_free(hypoIndexes);
	hypoIndexes = NIL;

	hypo_reset_fake_oids();

	return;
}

/*
 * Create an hypothetical index from its CREATE INDEX parsetree.  This function
 * is where all the hypothetic index creation is done, except the index size
 * estimation.
 */
static const hypoIndex *
hypo_index_store_parsetree(IndexStmt *node, const char *queryString)
{
	/* must be declared "volatile", because used in a PG_CATCH() */
	hypoIndex  *volatile entry;
	Form_pg_attribute attform;
	Oid			relid;
	StringInfoData indexRelationName;
	int			nkeycolumns,
				ninccolumns;
	ListCell   *lc;
	int			attn;

	/*
	 * Support for hypothetical BRIN indexes is broken in some minor versions
	 * of pg10, pg11 and pg12.  For simplicity, check PG_VERSION_NUM rather
	 * than the real instance version, which should be right most of the
	 * time.  When it's not, the only effect is to have a less user-friendly
	 * error message.
	 */
#if ((PG_VERSION_NUM >= 100000 && PG_VERSION_NUM < 100012) || \
	(PG_VERSION_NUM >= 110000 && PG_VERSION_NUM < 110007) || \
	(PG_VERSION_NUM >= 120000 && PG_VERSION_NUM < 120002))
	if (get_am_oid(node->accessMethod, true) == BRIN_AM_OID)
	{
		elog(ERROR, "hypopg: BRIN hypothetical indexes are only supported"
				" with PostgreSQL "
#if PG_VERSION_NUM >= 120000
				"12.2"
#else
#if PG_VERSION_NUM >= 110000
				"11.7"
#else
				"10.12"
#endif					/* pg 11 */
#endif					/* pg 12 */
				" and later.");
	}
#endif

	relid = RangeVarGetRelid(node->relation, AccessShareLock, false);

	/* Some sanity checks */
	switch (get_rel_relkind(relid))
	{
#if PG_VERSION_NUM >= 90300
		case RELKIND_MATVIEW:
#endif
#if PG_VERSION_NUM >= 110000
		case RELKIND_PARTITIONED_TABLE:
#endif
		case RELKIND_RELATION:
			/* this is supported */
			break;
#if PG_VERSION_NUM >= 100000 && PG_VERSION_NUM < 110000
		case RELKIND_PARTITIONED_TABLE:
			elog(ERROR, "hypopg: cannot create hypothetical index on"
				 " partitioned table \"%s\"", node->relation->relname);
			break;
#endif
		default:
#if PG_VERSION_NUM >= 90300
			elog(ERROR, "hypopg: \"%s\" is not a table or materialized view",
				 node->relation->relname);
#else
			elog(ERROR, "hypopg: \"%s\" is not a table",
				 node->relation->relname);
#endif
	}

	/* Run parse analysis ... */
	node = transformIndexStmt(relid, node, queryString);

	nkeycolumns = list_length(node->indexParams);
#if PG_VERSION_NUM >= 110000
	if (list_intersection(node->indexParams, node->indexIncludingParams) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("hypopg: included columns must not intersect with key columns")));

	ninccolumns = list_length(node->indexIncludingParams);
#else
	ninccolumns = 0;
#endif

	if (nkeycolumns > INDEX_MAX_KEYS)
		elog(ERROR, "hypopg: cannot use more thant %d columns in an index",
			 INDEX_MAX_KEYS);

	initStringInfo(&indexRelationName);
	appendStringInfo(&indexRelationName, "%s", node->accessMethod);
	appendStringInfo(&indexRelationName, "_");

	if (node->relation->schemaname != NULL &&
		(strcmp(node->relation->schemaname, "public") != 0))
	{
		appendStringInfo(&indexRelationName, "%s", node->relation->schemaname);
		appendStringInfo(&indexRelationName, "_");
	}

	appendStringInfo(&indexRelationName, "%s", node->relation->relname);

	/* now create the hypothetical index entry */
	entry = hypo_newIndex(relid, node->accessMethod, nkeycolumns, ninccolumns,
						  node->options);

	PG_TRY();
	{
		HeapTuple	tuple;
		int			ind_avg_width = 0;

		if (node->unique && !entry->amcanunique)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("hypopg: access method \"%s\" does not support unique indexes",
							node->accessMethod)));
		if (nkeycolumns > 1 && !entry->amcanmulticol)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("hypopg: access method \"%s\" does not support multicolumn indexes",
							node->accessMethod)));

#if PG_VERSION_NUM >= 110000
		if (node-> indexIncludingParams != NIL && !entry->amcaninclude)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("hypopg: access method \"%s\" does not support included columns",
							node->accessMethod)));
#endif

		entry->unique = node->unique;
		entry->ncolumns = nkeycolumns + ninccolumns;
		entry->nkeycolumns = nkeycolumns;

		/* handle predicate if present */
		if (node->whereClause)
		{
			MemoryContext oldcontext;
			List	   *pred;

			CheckPredicate((Expr *) node->whereClause);

			pred = make_ands_implicit((Expr *) node->whereClause);
			oldcontext = MemoryContextSwitchTo(HypoMemoryContext);

			entry->indpred = (List *) copyObject(pred);
			MemoryContextSwitchTo(oldcontext);
		}
		else
		{
			entry->indpred = NIL;
		}

		/*
		 * process attributeList
		 */
		attn = 0;
		foreach(lc, node->indexParams)
		{
			IndexElem  *attribute = (IndexElem *) lfirst(lc);
			Oid			atttype = InvalidOid;
			Oid			opclass;

			appendStringInfo(&indexRelationName, "_");

			/*
			 * Process the column-or-expression to be indexed.
			 */
			if (attribute->name != NULL)
			{
				/* Simple index attribute */
				appendStringInfo(&indexRelationName, "%s", attribute->name);
				/* get the attribute catalog info */
				tuple = SearchSysCacheAttName(relid, attribute->name);

				if (!HeapTupleIsValid(tuple))
				{
					elog(ERROR, "hypopg: column \"%s\" does not exist",
						 attribute->name);
				}
				attform = (Form_pg_attribute) GETSTRUCT(tuple);

				/* setup the attnum */
				entry->indexkeys[attn] = attform->attnum;

				/* setup the collation */
				entry->indexcollations[attn] = attform->attcollation;

				/* get the atttype */
				atttype = attform->atttypid;

				ReleaseSysCache(tuple);
			}
			else
			{
				/*---------------------------
				 * handle index on expression
				 *
				 * Adapted from DefineIndex() and ComputeIndexAttrs()
				 *
				 * Statistics on expression index will be really wrong, since
				 * they're only computed when a real index exists (selectivity
				 * and average width).
				 */
				MemoryContext oldcontext;
				Node	   *expr = attribute->expr;

				Assert(expr != NULL);
				entry->indexcollations[attn] = exprCollation(attribute->expr);
				atttype = exprType(attribute->expr);

				appendStringInfo(&indexRelationName, "expr");

				/*
				 * Strip any top-level COLLATE clause.  This ensures that we
				 * treat "x COLLATE y" and "(x COLLATE y)" alike.
				 */
				while (IsA(expr, CollateExpr))
					expr = (Node *) ((CollateExpr *) expr)->arg;

				if (IsA(expr, Var) &&
					((Var *) expr)->varattno != InvalidAttrNumber)
				{
					/*
					 * User wrote "(column)" or "(column COLLATE something)".
					 * Treat it like simple attribute anyway.
					 */
					entry->indexkeys[attn] = ((Var *) expr)->varattno;

					/*
					 * Generated index name will have _expr instead of attname
					 * in generated index name, and error message will also be
					 * slighty different in case on unexisting column from a
					 * simple attribute, but that's how ComputeIndexAttrs()
					 * proceed.
					 */

				}
				else
				{
					/*
					 * transformExpr() should have already rejected
					 * subqueries, aggregates, and window functions, based on
					 * the EXPR_KIND_ for an index expression.
					 */

					/*
					 * An expression using mutable functions is probably
					 * wrong, since if you aren't going to get the same result
					 * for the same data every time, it's not clear what the
					 * index entries mean at all.
					 */
					if (CheckMutability((Expr *) expr))
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								 errmsg("hypopg: functions in index expression must be marked IMMUTABLE")));


					entry->indexkeys[attn] = 0; /* marks expression */

					oldcontext = MemoryContextSwitchTo(HypoMemoryContext);
					entry->indexprs = lappend(entry->indexprs,
											  (Node *) copyObject(attribute->expr));
					MemoryContextSwitchTo(oldcontext);
				}
			}

			ind_avg_width += hypo_estimate_index_colsize(entry, attn);

			/*
			 * Apply collation override if any
			 */
			if (attribute->collation)
				entry->indexcollations[attn] =
					get_collation_oid(attribute->collation, false);

			/*
			 * Check we have a collation iff it's a collatable type.  The only
			 * expected failures here are (1) COLLATE applied to a
			 * noncollatable type, or (2) index expression had an unresolved
			 * collation.  But we might as well code this to be a complete
			 * consistency check.
			 */
			if (type_is_collatable(atttype))
			{
				if (!OidIsValid(entry->indexcollations[attn]))
					ereport(ERROR,
							(errcode(ERRCODE_INDETERMINATE_COLLATION),
							 errmsg("hypopg: could not determine which collation to use for index expression"),
							 errhint("Use the COLLATE clause to set the collation explicitly.")));
			}
			else
			{
				if (OidIsValid(entry->indexcollations[attn]))
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("hypopg: collations are not supported by type %s",
									format_type_be(atttype))));
			}

			/* get the opclass */
#if PG_VERSION_NUM < 100000
			opclass = GetIndexOpClass(attribute->opclass,
									  atttype,
									  node->accessMethod,
									  entry->relam);
#else
			opclass = ResolveOpClass(attribute->opclass,
									  atttype,
									  node->accessMethod,
									  entry->relam);
#endif
			entry->opclass[attn] = opclass;
			/* setup the opfamily */
			entry->opfamily[attn] = get_opclass_family(opclass);

			entry->opcintype[attn] = get_opclass_input_type(opclass);

			/* setup the sort info if am handles it */
			if (entry->amcanorder)
			{
				/* setup NULLS LAST, NULLS FIRST cases are handled below */
				entry->nulls_first[attn] = false;
				/* default ordering is ASC */
				entry->reverse_sort[attn] = (attribute->ordering == SORTBY_DESC);
				/* default null ordering is LAST for ASC, FIRST for DESC */
				if (attribute->nulls_ordering == SORTBY_NULLS_DEFAULT)
				{
					if (attribute->ordering == SORTBY_DESC)
						entry->nulls_first[attn] = true;
				}
				else if (attribute->nulls_ordering == SORTBY_NULLS_FIRST)
					entry->nulls_first[attn] = true;
			}

			/* handle index-only scan info */
#if PG_VERSION_NUM < 90500

			/*
			 * OIS info is global for the index before 9.5, so look for the
			 * information only once in that case.
			 */
			if (attn == 0)
			{
				/*
				 * specify first column, but it doesn't matter as this will
				 * only be used with GiST am, which cannot do IOS prior pg 9.5
				 */
				entry->canreturn = hypo_can_return(entry, atttype, 0,
												   node->accessMethod);
			}
#else
			/* per-column IOS information */
			entry->canreturn[attn] = hypo_can_return(entry, atttype, attn,
													 node->accessMethod);
#endif

			attn++;
		}
		Assert(attn == nkeycolumns);

		/*
		 * We disallow indexes on system columns other than OID.  They would
		 * not necessarily get updated correctly, and they don't seem useful
		 * anyway.
		 */
		for (attn = 0; attn < nkeycolumns; attn++)
		{
			AttrNumber	attno = entry->indexkeys[attn];

			if (attno < 0
#if PG_VERSION_NUM < 120000
					&& attno != ObjectIdAttributeNumber
#endif
					)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("hypopg: index creation on system columns is not supported")));
		}


#if PG_VERSION_NUM >= 110000
		attn = nkeycolumns;
		foreach(lc, node->indexIncludingParams)
		{
			IndexElem  *attribute = (IndexElem *) lfirst(lc);
			Oid			atttype = InvalidOid;

			appendStringInfo(&indexRelationName, "_");

			/* Handle not supported features as in ComputeIndexAttrs() */
			if (attribute->collation)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("hypopg: including column does not support a collation")));
			if (attribute->opclass)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("hypopg: including column does not support an operator class")));
			if (attribute->ordering != SORTBY_DEFAULT)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("hypopg: including column does not support ASC/DESC options")));
			if (attribute->nulls_ordering != SORTBY_NULLS_DEFAULT)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("hypopg: including column does not support NULLS FIRST/LAST options")));

			/*
			 * Process the column-or-expression to be indexed.
			 */
			if (attribute->name != NULL)
			{
				/* Simple index attribute */
				appendStringInfo(&indexRelationName, "%s", attribute->name);
				/* get the attribute catalog info */
				tuple = SearchSysCacheAttName(relid, attribute->name);

				if (!HeapTupleIsValid(tuple))
				{
					elog(ERROR, "hypopg: column \"%s\" does not exist",
						 attribute->name);
				}
				attform = (Form_pg_attribute) GETSTRUCT(tuple);

				/* setup the attnum */
				entry->indexkeys[attn] = attform->attnum;

				/* get the atttype */
				atttype = attform->atttypid;

				ReleaseSysCache(tuple);
			}
			else
			{
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("hypopg: expressions are not supported in included columns")));
			}

			ind_avg_width += hypo_estimate_index_colsize(entry, attn);

			/* per-column IOS information */
			entry->canreturn[attn] = hypo_can_return(entry, atttype, attn,
													 node->accessMethod);

			attn++;
		}
		Assert(attn == (nkeycolumns + ninccolumns));
#endif

		/*
		 * Also check for system columns used in expressions or predicates.
		 */
		if (entry->indexprs || entry->indpred)
		{
			Bitmapset  *indexattrs = NULL;
			int			i;

			pull_varattnos((Node *) entry->indexprs, 1, &indexattrs);
			pull_varattnos((Node *) entry->indpred, 1, &indexattrs);

			for (i = FirstLowInvalidHeapAttributeNumber + 1; i < 0; i++)
			{
				if (
#if PG_VERSION_NUM < 120000
						i != ObjectIdAttributeNumber &&
#endif
					bms_is_member(i - FirstLowInvalidHeapAttributeNumber,
								  indexattrs))
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("hypopg: index creation on system columns is not supported")));
			}
		}

		/* Check if the average size fits in a btree index */
		if (entry->relam == BTREE_AM_OID)
		{
			if (ind_avg_width >= HYPO_BTMaxItemSize)
				ereport(ERROR,
						(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
						 errmsg("hypopg: estimated index row size %d "
								"exceeds maximum %ld",
								ind_avg_width, HYPO_BTMaxItemSize),
						 errhint("Values larger than 1/3 of a buffer page "
								 "cannot be indexed.\nConsider a function index "
								 " of an MD5 hash of the value, or use full text "
								 "indexing\n(which is not yet supported by hypopg)."
								 )));
			/* Warn about posssible error with a 80% avg size */
			else if (ind_avg_width >= HYPO_BTMaxItemSize * .8)
				ereport(WARNING,
						(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
						 errmsg("hypopg: estimated index row size %d "
								"is close to maximum %ld",
								ind_avg_width, HYPO_BTMaxItemSize),
						 errhint("Values larger than 1/3 of a buffer page "
								 "cannot be indexed.\nConsider a function index "
								 " of an MD5 hash of the value, or use full text "
								 "indexing\n(which is not yet supported by hypopg)."
								 )));
		}

		/* No more elog beyond this point. */
	}
	PG_CATCH();
	{
		/* Free what was palloc'd in HypoMemoryContext */
		hypo_index_pfree(entry);

		PG_RE_THROW();
	}
	PG_END_TRY();

	/*
	 * Fetch the ordering information for the index, if any. Adapted from
	 * plancat.c - get_relation_info().
	 */
	if ((entry->relam != BTREE_AM_OID) && entry->amcanorder)
	{
		/*
		 * Otherwise, identify the corresponding btree opfamilies by trying to
		 * map this index's "<" operators into btree.  Since "<" uniquely
		 * defines the behavior of a sort order, this is a sufficient test.
		 *
		 * XXX This method is rather slow and also requires the undesirable
		 * assumption that the other index AM numbers its strategies the same
		 * as btree.  It'd be better to have a way to explicitly declare the
		 * corresponding btree opfamily for each opfamily of the other index
		 * type.  But given the lack of current or foreseeable amcanorder
		 * index types, it's not worth expending more effort on now.
		 */
		for (attn = 0; attn < nkeycolumns; attn++)
		{
			Oid			ltopr;
			Oid			btopfamily;
			Oid			btopcintype;
			int16		btstrategy;

			ltopr = get_opfamily_member(entry->opfamily[attn],
										entry->opcintype[attn],
										entry->opcintype[attn],
										BTLessStrategyNumber);
			if (OidIsValid(ltopr) &&
				get_ordering_op_properties(ltopr,
										   &btopfamily,
										   &btopcintype,
										   &btstrategy) &&
				btopcintype == entry->opcintype[attn] &&
				btstrategy == BTLessStrategyNumber)
			{
				/* Successful mapping */
				entry->sortopfamily[attn] = btopfamily;
			}
			else
			{
				/* Fail ... quietly treat index as unordered */
				/* also pfree allocated memory */
				pfree(entry->sortopfamily);
				pfree(entry->reverse_sort);
				pfree(entry->nulls_first);

				entry->sortopfamily = NULL;
				entry->reverse_sort = NULL;
				entry->nulls_first = NULL;

				break;
			}
		}
	}

	hypo_set_indexname(entry, indexRelationName.data);

	hypo_addIndex(entry);

	return entry;
}

/*
 * Remove an hypothetical index from the list of hypothetical indexes.
 * pfree (by calling hypo_index_pfree) all memory that has been allocated.
 */
static bool
hypo_index_remove(Oid indexid)
{
	ListCell   *lc;

	/* remove this index from the list of hidden indexes if present */
	hypo_index_unhide(indexid);

	foreach(lc, hypoIndexes)
	{
		hypoIndex  *entry = (hypoIndex *) lfirst(lc);

		if (entry->oid == indexid)
		{
			hypoIndexes = list_delete_ptr(hypoIndexes, entry);
			hypo_index_pfree(entry);
			return true;
		}
	}

	return false;
}

/* pfree all allocated memory for within an hypoIndex and the entry itself. */
static void
hypo_index_pfree(hypoIndex * entry)
{
	/* pfree all memory that has been allocated */
	pfree(entry->indexname);
	pfree(entry->indexkeys);
	pfree(entry->indexcollations);
	pfree(entry->opfamily);
	pfree(entry->opclass);
	pfree(entry->opcintype);
	if ((entry->relam == BTREE_AM_OID) || entry->amcanorder)
	{
		if ((entry->relam != BTREE_AM_OID) && entry->sortopfamily)
			pfree(entry->sortopfamily);
		if (entry->reverse_sort)
			pfree(entry->reverse_sort);
		if (entry->nulls_first)
			pfree(entry->nulls_first);
	}
	if (entry->indexprs)
		list_free_deep(entry->indexprs);
	if (entry->indpred)
		pfree(entry->indpred);
#if PG_VERSION_NUM >= 90500
	pfree(entry->canreturn);
#endif
	/* finally pfree the entry */
	pfree(entry);
}

/*--------------------------------------------------
 * Add an hypothetical index to the list of indexes.
 * Caller should have check that the specified hypoIndex does belong to the
 * specified relation.  This function also assume that the specified entry
 * already contains every needed information, so we just basically need to copy
 * it from the hypoIndex to the new IndexOptInfo.  Every specific handling is
 * done at store time (ie.  hypo_index_store_parsetree).  The only exception is
 * the size estimation, recomputed verytime, as it needs up to date statistics.
 */
void
hypo_injectHypotheticalIndex(PlannerInfo *root,
							 Oid relationObjectId,
							 bool inhparent,
							 RelOptInfo *rel,
							 Relation relation,
							 hypoIndex * entry)
{
	IndexOptInfo *index;
	int			ncolumns,

	/*
	 * For convenience and readability, use nkeycolumns even for pg10-
	 * version.  In this case, this var will be initialized to ncolumns
	 */
				nkeycolumns,
				i;


	/* create a node */
	index = makeNode(IndexOptInfo);

	index->relam = entry->relam;

	/* General stuff */
	index->indexoid = entry->oid;
	index->reltablespace = rel->reltablespace;	/* same tablespace as
												 * relation, TODO */
	index->rel = rel;
	index->ncolumns = ncolumns = entry->ncolumns;
#if PG_VERSION_NUM >= 110000
	index->nkeycolumns = nkeycolumns = entry->nkeycolumns;
#else
	nkeycolumns = ncolumns;
#endif

	index->indexkeys = (int *) palloc(sizeof(int) * ncolumns);
	index->indexcollations = (Oid *) palloc(sizeof(int) * nkeycolumns);
	index->opfamily = (Oid *) palloc(sizeof(int) * nkeycolumns);
	index->opcintype = (Oid *) palloc(sizeof(int) * nkeycolumns);

	if ((index->relam == BTREE_AM_OID) || entry->amcanorder)
	{
		if (index->relam != BTREE_AM_OID)
			index->sortopfamily = palloc0(sizeof(Oid) * nkeycolumns);

		index->reverse_sort = (bool *) palloc(sizeof(bool) * nkeycolumns);
		index->nulls_first = (bool *) palloc(sizeof(bool) * nkeycolumns);
	}
	else
	{
		index->sortopfamily = NULL;
		index->reverse_sort = NULL;
		index->nulls_first = NULL;
	}

#if PG_VERSION_NUM >= 90500
	index->canreturn = (bool *) palloc(sizeof(bool) * ncolumns);
#endif

	for (i = 0; i < ncolumns; i++)
	{
		index->indexkeys[i] = entry->indexkeys[i];
#if PG_VERSION_NUM >= 90500
		index->canreturn[i] = entry->canreturn[i];
#endif
	}

	for (i = 0; i < nkeycolumns; i++)
	{
		index->indexcollations[i] = entry->indexcollations[i];
		index->opfamily[i] = entry->opfamily[i];
		index->opcintype[i] = entry->opcintype[i];
	}

	/*
	 * Fetch the ordering information for the index, if any. This is handled
	 * in hypo_index_store_parsetree(). Again, adapted from plancat.c -
	 * get_relation_info()
	 */
	if (entry->relam == BTREE_AM_OID)
	{
		/*
		 * If it's a btree index, we can use its opfamily OIDs directly as the
		 * sort ordering opfamily OIDs.
		 */
		index->sortopfamily = index->opfamily;

		for (i = 0; i < nkeycolumns; i++)
		{
			index->reverse_sort[i] = entry->reverse_sort[i];
			index->nulls_first[i] = entry->nulls_first[i];
		}
	}
	else if (entry->amcanorder)
	{
		if (entry->sortopfamily)
		{
			for (i = 0; i < nkeycolumns; i++)
			{
				index->sortopfamily[i] = entry->sortopfamily[i];
				index->reverse_sort[i] = entry->reverse_sort[i];
				index->nulls_first[i] = entry->nulls_first[i];
			}
		}
		else
		{
			index->sortopfamily = NULL;
			index->reverse_sort = NULL;
			index->nulls_first = NULL;
		}
	}

	index->unique = entry->unique;

	index->amcostestimate = entry->amcostestimate;
	index->immediate = entry->immediate;
#if PG_VERSION_NUM < 90500
	index->canreturn = entry->canreturn;
#endif
	index->amcanorderbyop = entry->amcanorderbyop;
	index->amoptionalkey = entry->amoptionalkey;
	index->amsearcharray = entry->amsearcharray;
	index->amsearchnulls = entry->amsearchnulls;
	index->amhasgettuple = entry->amhasgettuple;
	index->amhasgetbitmap = entry->amhasgetbitmap;
#if PG_VERSION_NUM >= 110000
	index->amcanparallel = entry->amcanparallel;
#endif

	/* these has already been handled in hypo_index_store_parsetree() if any */
	index->indexprs = list_copy(entry->indexprs);
	index->indpred = list_copy(entry->indpred);
	index->predOK = false;		/* will be set later in indxpath.c */

	/*
	 * Build targetlist using the completed indexprs data. copied from
	 * PostgreSQL
	 */
	index->indextlist = build_index_tlist(root, index, relation);

	/*
	 * estimate most of the hypothyetical index stuff, more exactly: tuples,
	 * pages and tree_height (9.3+)
	 */
	hypo_estimate_index(entry, rel);

	index->pages = entry->pages;
	index->tuples = entry->tuples;
#if PG_VERSION_NUM >= 90300
	index->tree_height = entry->tree_height;
#endif

	/*
	 * obviously, setup this tag. However, it's only checked in
	 * selfuncs.c/get_actual_variable_range, so we still need to add
	 * hypothetical indexes *ONLY* in an explain-no-analyze command.
	 */
	index->hypothetical = true;

	/* add our hypothetical index in the relation's indexlist */
	rel->indexlist = lcons(index, rel->indexlist);
}

/*
 * Return the stored hypothetical index for a given oid if any, NULL otherwise
 */
hypoIndex *
hypo_get_index(Oid indexId)
{
	ListCell   *lc;

	foreach(lc, hypoIndexes)
	{
		hypoIndex  *entry = (hypoIndex *) lfirst(lc);

		if (entry->oid == indexId)
			return entry;
	}

	return NULL;
}

/* Return the hypothetical index name ifs indexId is ours, NULL otherwise, as
 * this is what explain_get_index_name expects to continue his job.
 */
const char *
hypo_explain_get_index_name_hook(Oid indexId)
{
	if (isExplain)
	{
		hypoIndex  *index = NULL;

		index = hypo_get_index(indexId);

		if (index)
			return index->indexname;
	}

	if (prev_explain_get_index_name_hook)
		return prev_explain_get_index_name_hook(indexId);

	return NULL;
}

/*
 * List created hypothetical indexes
 */
Datum
hypopg(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	ListCell   *lc;
	Datum		predDatum;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	foreach(lc, hypoIndexes)
	{
		hypoIndex  *entry = (hypoIndex *) lfirst(lc);
		Datum		values[HYPO_INDEX_NB_COLS];
		bool		nulls[HYPO_INDEX_NB_COLS];
		ListCell   *lc2;
		StringInfoData exprsString;
		int			i = 0;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));


		values[i++] = CStringGetTextDatum(entry->indexname);
		values[i++] = ObjectIdGetDatum(entry->oid);
		values[i++] = ObjectIdGetDatum(entry->relid);
		values[i++] = Int8GetDatum(entry->ncolumns);
		values[i++] = BoolGetDatum(entry->unique);
		values[i++] = PointerGetDatum(buildint2vector(entry->indexkeys, entry->ncolumns));
		values[i++] = PointerGetDatum(buildoidvector(entry->indexcollations, entry->ncolumns));
		values[i++] = PointerGetDatum(buildoidvector(entry->opclass, entry->ncolumns));
		nulls[i++] = true;		/* no indoption for now, TODO */

		/* get each of indexprs, if any */
		initStringInfo(&exprsString);
		foreach(lc2, entry->indexprs)
		{
			Node	   *expr = lfirst(lc2);

			appendStringInfo(&exprsString, "%s", nodeToString(expr));
		}
		if (exprsString.len == 0)
			nulls[i++] = true;
		else
			values[i++] = CStringGetTextDatum(exprsString.data);
		pfree(exprsString.data);

		/*
		 * Convert the index predicate (if any) to a text datum.  Note we
		 * convert implicit-AND format to normal explicit-AND for storage.
		 */
		if (entry->indpred != NIL)
		{
			char	   *predString;

			predString = nodeToString(make_ands_explicit(entry->indpred));
			predDatum = CStringGetTextDatum(predString);
			pfree(predString);
			values[i++] = predDatum;
		}
		else
			nulls[i++] = true;

		values[i++] = ObjectIdGetDatum(entry->relam);
		Assert(i == HYPO_INDEX_NB_COLS);

		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	return (Datum) 0;
}

/*
 * SQL wrapper to create an hypothetical index with his parsetree
 */
Datum
hypopg_create_index(PG_FUNCTION_ARGS)
{
	char	   *sql = TextDatumGetCString(PG_GETARG_DATUM(0));
	List	   *parsetree_list;
	ListCell   *parsetree_item;
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext per_query_ctx;
	MemoryContext oldcontext;
	TupleDesc	tupdesc;
	Tuplestorestate *tupstore;
	int			i = 1;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	per_query_ctx = rsinfo->econtext->ecxt_per_query_memory;
	oldcontext = MemoryContextSwitchTo(per_query_ctx);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	parsetree_list = pg_parse_query(sql);

	foreach(parsetree_item, parsetree_list)
	{
		Node	   *parsetree = (Node *) lfirst(parsetree_item);
		Datum		values[HYPO_INDEX_CREATE_COLS];
		bool		nulls[HYPO_INDEX_CREATE_COLS];
		const		hypoIndex *entry;

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

#if PG_VERSION_NUM >= 100000
		parsetree = ((RawStmt *) parsetree)->stmt;
#endif
		if (nodeTag(parsetree) != T_IndexStmt)
		{
			elog(WARNING,
				 "hypopg: SQL order #%d is not a CREATE INDEX statement",
				 i);
		}
		else
		{
			entry = hypo_index_store_parsetree((IndexStmt *) parsetree, sql);
			if (entry != NULL)
			{
				values[0] = ObjectIdGetDatum(entry->oid);
				values[1] = CStringGetTextDatum(entry->indexname);

				tuplestore_putvalues(tupstore, tupdesc, values, nulls);
			}
		}
		i++;
	}

	return (Datum) 0;
}

/*
 * SQL wrapper to drop an hypothetical index.
 */
Datum
hypopg_drop_index(PG_FUNCTION_ARGS)
{
	Oid			indexid = PG_GETARG_OID(0);

	PG_RETURN_BOOL(hypo_index_remove(indexid));
}

/*
 * SQL Wrapper around the hypothetical index size estimation
 */
Datum
hypopg_relation_size(PG_FUNCTION_ARGS)
{
	BlockNumber pages;
	double		tuples;
	Oid			indexid = PG_GETARG_OID(0);
	ListCell   *lc;
	bool		found = false;

	pages = 0;
	tuples = 0;
	foreach(lc, hypoIndexes)
	{
		hypoIndex  *entry = (hypoIndex *) lfirst(lc);

		if (entry->oid == indexid)
		{
			hypo_estimate_index_simple(entry, &pages, &tuples);
			found = true;
			break;
		}
	}

	if (!found)
		elog(ERROR, "oid %u is not a hypothetical index", indexid);

	PG_RETURN_INT64(pages * 1.0L * BLCKSZ);
}

/*
 * Deparse an hypoIndex, indentified by its indexid to the actual CREATE INDEX
 * command.
 *
 * Heavilty inspired on pg_get_indexdef_worker()
 */

Datum
hypopg_get_indexdef(PG_FUNCTION_ARGS)
{
	Oid			indexid = PG_GETARG_OID(0);
	ListCell   *indexpr_item;
	StringInfoData buf;
	hypoIndex  *entry = NULL;
	ListCell   *lc;
	List	   *context;
	int			keyno;

	foreach(lc, hypoIndexes)
	{
		entry = (hypoIndex *) lfirst(lc);

		if (entry->oid == indexid)
			break;
	}

	if (!entry || entry->oid != indexid)
		PG_RETURN_NULL();

	initStringInfo(&buf);
	appendStringInfo(&buf, "CREATE %s ON %s.%s USING %s (",
					 (entry->unique ? "UNIQUE INDEX" : "INDEX"),
					 quote_identifier(get_namespace_name(get_rel_namespace(entry->relid))),
					 quote_identifier(get_rel_name(entry->relid)),
					 get_am_name(entry->relam));

	indexpr_item = list_head(entry->indexprs);

	context = deparse_context_for(get_rel_name(entry->relid), entry->relid);

	for (keyno = 0; keyno < entry->nkeycolumns; keyno++)
	{
		Oid			indcoll;
		Oid			keycoltype;
		Oid			keycolcollation;
		char	   *str;

		if (keyno != 0)
			appendStringInfo(&buf, ", ");

		if (entry->indexkeys[keyno] != 0)
		{
			int32		keycoltypmod;
#if PG_VERSION_NUM >= 110000
			appendStringInfo(&buf, "%s", get_attname(entry->relid,
													 entry->indexkeys[keyno], false));
#else
			appendStringInfo(&buf, "%s", get_attname(entry->relid,
													 entry->indexkeys[keyno]));
#endif

			get_atttypetypmodcoll(entry->relid, entry->indexkeys[keyno],
								  &keycoltype, &keycoltypmod,
								  &keycolcollation);
		}
		else
		{
			/* expressional index */
			Node	   *indexkey;

			if (indexpr_item == NULL)
				elog(ERROR, "too few entries in indexprs list");
			indexkey = (Node *) lfirst(indexpr_item);
			indexpr_item = lnext(entry->indexprs, indexpr_item);

			/* Deparse */
			str = deparse_expression(indexkey, context, false, false);

			/* Need parens if it's not a bare function call */
			if (indexkey && IsA(indexkey, FuncExpr) &&
				((FuncExpr *) indexkey)->funcformat == COERCE_EXPLICIT_CALL)
				appendStringInfoString(&buf, str);
			else
				appendStringInfo(&buf, "(%s)", str);

			keycoltype = exprType(indexkey);
			keycolcollation = exprCollation(indexkey);
		}

		/* Add collation, if not default for column */
		indcoll = entry->indexcollations[keyno];
		if (OidIsValid(indcoll) && indcoll != keycolcollation)
			appendStringInfo(&buf, " COLLATE %s",
							 generate_collation_name((indcoll)));

		/* Add the operator class name, if not default */
		get_opclass_name(entry->opclass[keyno], entry->opcintype[keyno], &buf);

		/* Add options if relevant */
		if (entry->amcanorder)
		{
			/* if it supports sort ordering, report DESC and NULLS opts */
			if (entry->reverse_sort[keyno])
			{
				appendStringInfoString(&buf, " DESC");
				/* NULLS FIRST is the default in this case */
				if (!(entry->nulls_first[keyno]))
					appendStringInfoString(&buf, " NULLS LAST");
			}
			else
			{
				if (entry->nulls_first[keyno])
					appendStringInfoString(&buf, " NULLS FIRST");
			}
		}
	}

	appendStringInfo(&buf, ")");

#if PG_VERSION_NUM >= 110000
	Assert(entry->ncolumns >= entry->nkeycolumns);

	if (entry->ncolumns > entry->nkeycolumns)
	{
		appendStringInfo(&buf, " INCLUDE (");
		for (keyno = entry->nkeycolumns; keyno < entry->ncolumns; keyno++)
		{
			if (keyno != entry->nkeycolumns)
				appendStringInfo(&buf, ", ");

			appendStringInfo(&buf, "%s", get_attname(entry->relid,
													 entry->indexkeys[keyno], false));
		}
		appendStringInfo(&buf, ")");
	}
#endif

	if (entry->options)
	{
		appendStringInfo(&buf, " WITH (");

		foreach(lc, entry->options)
		{
			DefElem    *elem = (DefElem *) lfirst(lc);

			appendStringInfo(&buf, "%s = ", elem->defname);

			if (strcmp(elem->defname, "fillfactor") == 0)
				appendStringInfo(&buf, "%d", (int32) intVal(elem->arg));
			else if (strcmp(elem->defname, "pages_per_range") == 0)
				appendStringInfo(&buf, "%d", (int32) intVal(elem->arg));
			else if (strcmp(elem->defname, "length") == 0)
				appendStringInfo(&buf, "%d", (int32) intVal(elem->arg));
			else
				elog(WARNING, " hypopg: option %s unhandled, please report the bug",
					 elem->defname);
		}
		appendStringInfo(&buf, ")");
	}

	if (entry->indpred)
	{
		appendStringInfo(&buf, " WHERE %s", deparse_expression((Node *)
															   make_ands_explicit(entry->indpred), context, false, false));
	}

	PG_RETURN_TEXT_P(cstring_to_text(buf.data));
}

/*
 * SQL wrapper to remove all declared hypothetical indexes.
 */
Datum
hypopg_reset_index(PG_FUNCTION_ARGS)
{
	hypo_index_reset();
	PG_RETURN_VOID();
}

/*
 * Add the given oid for the list of hidden indexes
 * if it's a valid index (hypothetical or real), and if not hidden already.
 * Return true if the oid is added to the list, false otherwise.
 */
Datum
hypopg_hide_index(PG_FUNCTION_ARGS)
{
	Oid indexid = PG_GETARG_OID(0);
	MemoryContext old_context;
	bool is_hypo = false;
	ListCell *lc;

	/* first check if it is in hypoIndexes */
	foreach(lc, hypoIndexes)
	{
		hypoIndex *entry = (hypoIndex *) lfirst(lc);

		if (entry->oid == indexid)
		{
			is_hypo = true;
			break;
		}
	}

	if (!is_hypo)
	{
		HeapTuple index_tup = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexid));

		if (!HeapTupleIsValid(index_tup))
			return false;

		ReleaseSysCache(index_tup);
	}

	if (list_member_oid(hypoHiddenIndexes, indexid))
		return false;

	old_context = MemoryContextSwitchTo(HypoMemoryContext);
	hypoHiddenIndexes = lappend_oid(hypoHiddenIndexes, indexid);
	MemoryContextSwitchTo(old_context);

	return true;
}

/*
 * Unhide the given index oid (hypothetical or not) to make it visible to
 * the planner again.
 */
Datum
hypopg_unhide_index(PG_FUNCTION_ARGS)
{
	Oid indexid = PG_GETARG_OID(0);

	PG_RETURN_BOOL(hypo_index_unhide(indexid));
}

/*
 * Restore all hidden index.
 */
Datum
hypopg_unhide_all_indexes(PG_FUNCTION_ARGS)
{
	list_free(hypoHiddenIndexes);
	hypoHiddenIndexes = NIL;
	PG_RETURN_VOID();
}

/*
 * Get all hidden index oid.
 */
Datum
hypopg_hidden_indexes(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext oldcontext;
	TupleDesc tupdesc;
	Tuplestorestate *tupstore;
	ListCell *lc;

	/* check to see if caller supports us returning a tuplestore */
	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("materialize mode required, but it is not " \
						"allowed in this context")));

	oldcontext = MemoryContextSwitchTo(rsinfo->econtext->ecxt_per_query_memory);

	tupdesc = CreateTemplateTupleDesc(1
#if PG_VERSION_NUM < 120000
			, false
#endif
			);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "indexid", OIDOID, -1, 0);

	tupstore = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldcontext);

	foreach(lc, hypoHiddenIndexes)
	{
		Oid indexid = lfirst_oid(lc);
		Datum values[HYPO_HIDDEN_INDEX_COLS];
		bool nulls[HYPO_HIDDEN_INDEX_COLS];

		memset(values, 0, sizeof(values));
		memset(nulls, 0, sizeof(nulls));

		values[0] = ObjectIdGetDatum(indexid);
		tuplestore_putvalues(tupstore, tupdesc, values, nulls);
	}

	return (Datum) 0;
}

/*
 * Remove the oid to restore this index on EXPLAIN.
 */
bool
hypo_index_unhide(Oid indexid)
{
	int prev_length = list_length(hypoHiddenIndexes);

	hypoHiddenIndexes = list_delete_oid(hypoHiddenIndexes, indexid);
	return prev_length > list_length(hypoHiddenIndexes);
}

/*
 * Check rel and delete the same oid index as hypoHiddenIndexes
 * in rel->indexlist.
 */
void
hypo_hideIndexes(RelOptInfo *rel)
{
	ListCell *cell = NULL;

	if (rel == NULL)
		return;

	if (list_length(rel->indexlist) == 0 || list_length(hypoHiddenIndexes) == 0)
		return;

	foreach(cell, hypoHiddenIndexes)
	{
		Oid oid = lfirst_oid(cell);
		ListCell *lc = NULL;

#if PG_VERSION_NUM >= 130000
		foreach(lc, rel->indexlist)
		{
			IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);

			if (index->indexoid == oid)
				rel->indexlist = foreach_delete_current(rel->indexlist, lc);
		}
#else
		ListCell *next;
		ListCell *prev = NULL;

		for (lc = list_head(rel->indexlist); lc != NULL; lc = next)
		{
			IndexOptInfo *index = (IndexOptInfo *) lfirst(lc);

			next = lnext(lc);
			if (index->indexoid == oid)
				rel->indexlist = list_delete_cell(rel->indexlist, lc, prev);
			else
				prev = lc;
		}
#endif
	}
}

/* Simple function to set the indexname, dealing with max name length, and the
 * ending \0
 */
static void
hypo_set_indexname(hypoIndex * entry, char *indexname)
{
	char		oid[13];		/* store <oid>, oid is an unsigned int32, so it
								 * shouldn't be more than 4B, so 10 digits.
								 * We add 2 chars for the < and > separators
								 * and an extra null char.*/
	int			totalsize;

	snprintf(oid, sizeof(oid), "<%d>", entry->oid);

	/* we'll prefix the given indexname with the oid, and reserve a final \0 */
	totalsize = strlen(oid) + strlen(indexname) + 1;

	/* final index name must not exceed NAMEDATALEN */
	if (totalsize > NAMEDATALEN)
		totalsize = NAMEDATALEN;

	/* eventually truncate the given indexname at NAMEDATALEN-1 if needed */
	strcpy(entry->indexname, oid);
	strncat(entry->indexname, indexname, totalsize - strlen(oid) - 1);
}

/*
 * Fill the pages and tuples information for a given hypoIndex.
 */
static void
hypo_estimate_index_simple(hypoIndex * entry, BlockNumber *pages, double *tuples)
{
	RelOptInfo *rel;
	Relation	relation;

	/*
	 * retrieve number of tuples and pages of the related relation, adapted
	 * from plancat.c/get_relation_info().
	 */

	rel = makeNode(RelOptInfo);

	/* Open the hypo index' relation */
	relation = table_open(entry->relid, AccessShareLock);

	if (!RelationNeedsWAL(relation) && RecoveryInProgress())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("hypopg: cannot access temporary or unlogged relations during recovery")));

	rel->min_attr = FirstLowInvalidHeapAttributeNumber + 1;
	rel->max_attr = RelationGetNumberOfAttributes(relation);
	rel->reltablespace = RelationGetForm(relation)->reltablespace;

	Assert(rel->max_attr >= rel->min_attr);
	rel->attr_needed = (Relids *)
		palloc0((rel->max_attr - rel->min_attr + 1) * sizeof(Relids));
	rel->attr_widths = (int32 *)
		palloc0((rel->max_attr - rel->min_attr + 1) * sizeof(int32));

	estimate_rel_size(relation, rel->attr_widths - rel->min_attr,
					  &rel->pages, &rel->tuples, &rel->allvisfrac);

	/* Close the relation and release the lock now */
	table_close(relation, AccessShareLock);

	hypo_estimate_index(entry, rel);
	*pages = entry->pages;
	*tuples = entry->tuples;
}


/*
 * Fill the pages and tuples information for a given hypoIndex and a given
 * RelOptInfo
 */
static void
hypo_estimate_index(hypoIndex * entry, RelOptInfo *rel)
{
	int			i,
				ind_avg_width = 0;
	int			usable_page_size;
	int			line_size;
	double		bloat_factor;
	int			fillfactor = 0; /* for B-tree, hash, GiST and SP-Gist */
#if PG_VERSION_NUM >= 90500
	int			pages_per_range = BRIN_DEFAULT_PAGES_PER_RANGE;
#endif
#if PG_VERSION_NUM >= 90600
	int			bloomLength = 5;
#endif
	int			additional_bloat = 20;
	ListCell   *lc;

	for (i = 0; i < entry->ncolumns; i++)
		ind_avg_width += hypo_estimate_index_colsize(entry, i);

	if (entry->indpred == NIL)
	{
		/* No predicate, as much tuples as estmated on its relation */
		entry->tuples = rel->tuples;
	}
	else
	{
		/*
		 * We have a predicate. Find it's selectivity and setup the estimated
		 * number of line according to it
		 */
		Selectivity selectivity;
		PlannerInfo *root;
		PlannerGlobal *glob;
		Query	   *parse;
		List	   *rtable = NIL;
		RangeTblEntry *rte;

		/* create a fake minimal PlannerInfo */
		root = makeNode(PlannerInfo);

		glob = makeNode(PlannerGlobal);
		glob->boundParams = NULL;
		root->glob = glob;

		/* only 1 table: the one related to this hypothetical index */
		rte = makeNode(RangeTblEntry);
		rte->relkind = RTE_RELATION;
		rte->relid = entry->relid;
		rte->inh = false;		/* don't include inherited children */
		rtable = lappend(rtable, rte);

		parse = makeNode(Query);
		parse->rtable = rtable;
		root->parse = parse;

		/*
		 * allocate simple_rel_arrays and simple_rte_arrays. This function
		 * will also setup simple_rte_arrays with the previous rte.
		 */
		setup_simple_rel_arrays(root);
		/* also add our table info */
		root->simple_rel_array[1] = rel;

		/*
		 * per comment on clause_selectivity(), JOIN_INNER must be passed if
		 * the clause isn't a join clause, which is our case, and passing 0 to
		 * varRelid is appropriate for restriction clause.
		 */
		selectivity = clauselist_selectivity(root, entry->indpred, 0,
											 JOIN_INNER, NULL);

		elog(DEBUG1, "hypopg: selectivity for index \"%s\": %lf", entry->indexname, selectivity);

		entry->tuples = selectivity * rel->tuples;
	}

	/* handle index storage parameters */
	foreach(lc, entry->options)
	{
		DefElem    *elem = (DefElem *) lfirst(lc);

		if (strcmp(elem->defname, "fillfactor") == 0)
			fillfactor = (int32) intVal(elem->arg);

#if PG_VERSION_NUM >= 90500
		if (strcmp(elem->defname, "pages_per_range") == 0)
			pages_per_range = (int32) intVal(elem->arg);
#endif
#if PG_VERSION_NUM >= 90600
		if (strcmp(elem->defname, "length") == 0)
			bloomLength = (int32) intVal(elem->arg);
#endif
	}

	if (entry->relam == BTREE_AM_OID)
	{
		/* -------------------------------
		 * quick estimating of index size:
		 *
		 * sizeof(PageHeader) : 24 (1 per page)
		 * sizeof(BTPageOpaqueData): 16 (1 per page)
		 * sizeof(IndexTupleData): 8 (1 per tuple, referencing heap)
		 * sizeof(ItemIdData): 4 (1 per tuple, storing the index item)
		 * default fillfactor: 90%
		 * no NULL handling
		 * fixed additional bloat: 20%
		 *
		 * I'll also need to read more carefully nbtree code to check if
		 * this is accurate enough.
		 *
		 */
		line_size = ind_avg_width +
			+(sizeof(IndexTupleData) * entry->ncolumns)
			+ MAXALIGN(sizeof(ItemIdData) * entry->ncolumns);

		usable_page_size = BLCKSZ - SizeOfPageHeaderData - sizeof(BTPageOpaqueData);
		bloat_factor = (200.0
						- (fillfactor == 0 ? BTREE_DEFAULT_FILLFACTOR : fillfactor)
						+ additional_bloat) / 100;

		entry->pages = (BlockNumber) (entry->tuples * line_size * bloat_factor / usable_page_size);
#if PG_VERSION_NUM >= 90300
		entry->tree_height = -1;	/* TODO */
#endif
	}
#if PG_VERSION_NUM >= 90500
	else if (entry->relam == BRIN_AM_OID)
	{
		HeapTuple	ht_opc;
		Form_pg_opclass opcrec;
		char	   *opcname;
		int			ranges = rel->pages / pages_per_range + 1;
		bool		is_minmax = true;
		int			data_size;

		/* -------------------------------
		 * quick estimation of index size. A BRIN index contains
		 * - a root page
		 * - a range map: REVMAP_PAGE_MAXITEMS items (one per range
		 *	 block) per revmap block
		 * - regular type: sizeof(BrinTuple) per range, plus depending
		 *	 on opclass:
		 *	 - *_minmax_ops: 2 Datums (min & max obviously)
		 *	 - *_inclusion_ops: 3 datumes (inclusion and 2 bool)
		 *
		 * I assume same minmax VS. inclusion opclass for all columns.
		 * BRIN access method does not bloat, don't add any additional.
		 */

		entry->pages = 1		/* root page */
			+ (ranges / REVMAP_PAGE_MAXITEMS) + 1;	/* revmap */

		/* get the operator class name */
		ht_opc = SearchSysCache1(CLAOID,
								 ObjectIdGetDatum(entry->opclass[0]));
		if (!HeapTupleIsValid(ht_opc))
			elog(ERROR, "hypopg: cache lookup failed for opclass %u",
				 entry->opclass[0]);
		opcrec = (Form_pg_opclass) GETSTRUCT(ht_opc);

		opcname = NameStr(opcrec->opcname);
		ReleaseSysCache(ht_opc);

		/* is it a minmax or an inclusion operator class ? */
		if (!strstr(opcname, "minmax_ops"))
			is_minmax = false;

		/* compute data_size according to opclass kind */
		if (is_minmax)
			data_size = sizeof(BrinTuple) + 2 * ind_avg_width;
		else
			data_size = sizeof(BrinTuple) + ind_avg_width
				+ 2 * sizeof(bool);

		data_size = data_size * ranges
			/ (BLCKSZ - MAXALIGN(SizeOfPageHeaderData)) + 1;

		entry->pages += data_size;
	}
#endif
#if PG_VERSION_NUM >= 90600
	else if (entry->relam == BLOOM_AM_OID)
	{
		/* ----------------------------
		 * bloom indexes are fixed size, depending on bloomLength (default 5B),
		 * see blutils.c
		 *
		 * A bloom index contains a meta page.
		 * Each other pages contains:
		 * - page header
		 * - opaque data
		 * - lines:
		 *   - ItemPointerData (BLOOMTUPLEHDRSZ)
		 *   - SignType * bloomLength
		 *
		 */
		usable_page_size = BLCKSZ - MAXALIGN(SizeOfPageHeaderData)
			- MAXALIGN(sizeof_BloomPageOpaqueData);
		line_size = BLOOMTUPLEHDRSZ +
			sizeof_SignType * bloomLength;

		entry->pages = 1;		/* meta page */
		entry->pages += (BlockNumber) ceil(
										   ((double) entry->tuples * line_size) / usable_page_size);
	}
#endif
#if PG_VERSION_NUM >= 100000
	else if (entry->relam == HASH_AM_OID)
	{
		/* ----------------------------
		 * From hash AM readme (src/backend/access/hash/README):
		 *
		 *   There are four kinds of pages in a hash index: the meta page (page
		 *   zero), which contains statically allocated control information;
		 *   primary bucket pages; overflow pages; and bitmap pages, which keep
		 *   track of overflow pages that have been freed and are available for
		 *   re-use.  For addressing purposes, bitmap pages are regarded as a
		 *   subset of the overflow pages.
		 * [...]
		 *   A hash index consists of two or more "buckets", into which tuples
		 *   are placed whenever their hash key maps to the bucket number.
		 *   [...]
		 *   Each bucket in the hash index comprises one or more index pages.
		 *   The bucket's first page is permanently assigned to it when the
		 *   bucket is created.  Additional pages, called "overflow pages", are
		 *   added if the bucket receives too many tuples to fit in the primary
		 *   bucket page.
		 *
		 * Hash AM also already provides some functions to compute an initial
		 * number of buckets given the estimated number of tuples the index
		 * will contains, which is a good enough estimate for hypothetical
		 * index.
		 *
		 * The code below is simply an adaptation of original code to compute
		 * the initial number of bucket, modified to cope with hypothetical
		 * index, plus some naive estimates for the overflow and bitmap pages.
		 *
		 * For more details, refer to the original code, in:
		 *   - _hash_init()
		 *   - _hash_init_metabuffer()
		 */
		int32 data_width;
		int32 item_width;
		int32 ffactor;
		double dnumbuckets;
		uint32 num_buckets;
		uint32 num_overflow;
		uint32 num_bitmap;
		uint32 lshift;

	/*
	 * Determine the target fill factor (in tuples per bucket) for this index.
	 * The idea is to make the fill factor correspond to pages about as full
	 * as the user-settable fillfactor parameter says.  We can compute it
	 * exactly since the index datatype (i.e. uint32 hash key) is fixed-width.
	 */
	data_width = sizeof(uint32);
	item_width = MAXALIGN(sizeof(IndexTupleData)) + MAXALIGN(data_width) +
		sizeof(ItemIdData);		/* include the line pointer */
	ffactor = HypoHashGetTargetPageUsage(fillfactor) / item_width;
	/* keep to a sane range */
	if (ffactor < 10)
		ffactor = 10;

	/*
	 * Choose the number of initial bucket pages to match the fill factor
	 * given the estimated number of tuples.  We round up the result to the
	 * total number of buckets which has to be allocated before using its
	 * hashm_spares element. However always force at least 2 bucket pages. The
	 * upper limit is determined by considerations explained in
	 * _hash_expandtable().
	 */
	dnumbuckets = entry->tuples / ffactor;
	if (dnumbuckets <= 2.0)
		num_buckets = 2;
	else if (dnumbuckets >= (double) 0x40000000)
		num_buckets = 0x40000000;
	else
		num_buckets = _hash_get_totalbuckets(_hash_spareindex(dnumbuckets));

	/*
	 * Naive estimate of overflow pages, knowing that a page can store ffactor
	 * tuples: we compute the number of tuples that wouldn't fit in the
	 * previously computed number of buckets, and compute the number of pages
	 * needed to store them.
	 */
	num_overflow = Max(0, ((entry->tuples - (num_buckets * ffactor)) /
					   ffactor) + 1);

	/* find largest bitmap array size that will fit in page size */
#if PG_VERSION_NUM >= 120000
	lshift = pg_leftmost_one_pos32(HypoHashGetMaxBitmapSize());
#else
	for (lshift = _hash_log2(HypoHashGetMaxBitmapSize()); lshift > 0; --lshift)
	{
		if ((1 << lshift) <= HypoHashGetMaxBitmapSize())
			break;
	}
#endif

	/*
	 * Naive estimate of bitmap pages, using the previously computed number of
	 * overflow pages.
	 */
	num_bitmap = Max(1, num_overflow / (1 <<lshift));

	/* Simply add all computed pages, plus one extra block for the meta page */
	entry->pages = num_buckets + num_overflow + num_bitmap + 1;
	}
#endif
	else
	{
		/* we shouldn't raise this error */
		elog(WARNING, "hypopg: access method %d is not supported",
			 entry->relam);
	}

	/* make sure the index size is at least one block */
	if (entry->pages <= 0)
		entry->pages = 1;
}

/*
 * Estimate a single index's column of an hypothetical index.
 */
static int
hypo_estimate_index_colsize(hypoIndex * entry, int col)
{
	int			i,
				pos;
	Node	   *expr;

	/* If simple attribute, return avg width */
	if (entry->indexkeys[col] != 0)
		return get_attavgwidth(entry->relid, entry->indexkeys[col]);

	/* It's an expression */
	pos = 0;

	for (i = 0; i < col; i++)
	{
		/* get the position in the expression list */
		if (entry->indexkeys[i] == 0)
			pos++;
	}

	expr = (Node *) list_nth(entry->indexprs, pos);

	if (IsA(expr, Var) &&((Var *) expr)->varattno != InvalidAttrNumber)
		return get_attavgwidth(entry->relid, ((Var *) expr)->varattno);

	if (IsA(expr, FuncExpr))
	{
		FuncExpr   *funcexpr = (FuncExpr *) expr;

		switch (funcexpr->funcid)
		{
			case 2311:
				/* md5 */
				return 32;
				break;
			case 870:
			case 871:
				{
					/* lower and upper, detect if simple attr */
					Var		   *var;

					if (IsA(linitial(funcexpr->args), Var))
					{
						var = (Var *) linitial(funcexpr->args);

						if (var->varattno > 0)
							return get_attavgwidth(entry->relid, var->varattno);
					}
					break;
				}
			default:
				/* default fallback estimate will be used */
				break;
		}
	}

	return 50;					/* default fallback estimate */
}

/*
 * canreturn should been checked with the amcanreturn proc, but this
 * can't be done without a real Relation, so try to find it out
 */
static bool
hypo_can_return(hypoIndex * entry, Oid atttype, int i, char *amname)
{
	/* no amcanreturn entry, am does not handle IOS */
#if PG_VERSION_NUM >= 90600
	if (entry->amcanreturn == NULL)
		return false;
#else
	if (!RegProcedureIsValid(entry->amcanreturn))
		return false;
#endif

	switch (entry->relam)
	{
		case BTREE_AM_OID:
			/* btree always support Index-Only scan */
			return true;
			break;
		case GIST_AM_OID:
#if PG_VERSION_NUM >= 90500
			{
				HeapTuple	tuple;

				/*
				 * since 9.5, GiST can do IOS if the opclass define a
				 * GIST_FETCH_PROC support function.
				 */
				tuple = SearchSysCache4(AMPROCNUM,
										ObjectIdGetDatum(entry->opfamily[i]),
										ObjectIdGetDatum(entry->opcintype[i]),
										ObjectIdGetDatum(entry->opcintype[i]),
										Int8GetDatum(GIST_FETCH_PROC));

				if (!HeapTupleIsValid(tuple))
					return false;

				ReleaseSysCache(tuple);
				return true;
			}
#else
			return false;
#endif
			break;
		case SPGIST_AM_OID:
			{
				SpGistCache *cache;
				spgConfigIn in;
				HeapTuple	tuple;
				Oid			funcid;
				bool		res = false;

				/* support function 1 tells us if IOS is supported */
				tuple = SearchSysCache4(AMPROCNUM,
										ObjectIdGetDatum(entry->opfamily[i]),
										ObjectIdGetDatum(entry->opcintype[i]),
										ObjectIdGetDatum(entry->opcintype[i]),
										Int8GetDatum(SPGIST_CONFIG_PROC));

				/* just in case */
				if (!HeapTupleIsValid(tuple))
					return false;

				funcid = ((Form_pg_amproc) GETSTRUCT(tuple))->amproc;
				ReleaseSysCache(tuple);

				in.attType = atttype;
				cache = palloc0(sizeof(SpGistCache));

				OidFunctionCall2Coll(funcid, entry->indexcollations[i],
									 PointerGetDatum(&in),
									 PointerGetDatum(&cache->config));

				res = cache->config.canReturnData;
				pfree(cache);

				return res;
			}
			break;
		default:
			/* all specific case should have been handled */
			elog(WARNING, "hypopg: access method \"%s\" looks like it may"
				 " support Index-Only Scan, but it's unexpected.\n"
				 "Feel free to warn developper.",
				 amname);
			return false;
			break;
	}
}

/*
 * Given an access method name and its oid, try to find out if it's a supported
 * pluggable access method.  If so, save its oid for future use.
 */
static void
hypo_discover_am(char *amname, Oid oid)
{
#if PG_VERSION_NUM < 90600
	/* no (reliable) external am before 9.6 */
	return;
#else

	/* don't try to handle builtin access method */
	if (oid == BTREE_AM_OID ||
		oid == GIST_AM_OID ||
		oid == GIN_AM_OID ||
		oid == SPGIST_AM_OID ||
		oid == BRIN_AM_OID ||
		oid == HASH_AM_OID)
		return;

	/* Is it the bloom access method? */
	if (strcmp(amname, "bloom") == 0)
		BLOOM_AM_OID = oid;
#endif
}
