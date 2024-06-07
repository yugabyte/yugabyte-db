/*-------------------------------------------------------------------------
 *
 * hypopg_index.h: Implementation of hypothetical indexes for PostgreSQL
 *
 * This file contains all includes for the internal code related to
 * hypothetical indexes support.
 *
 * This program is open source, licensed under the PostgreSQL license.
 * For license terms, see the LICENSE file.
 *
 * Copyright (C) 2015-2024: Julien Rouhaud
 *
 *-------------------------------------------------------------------------
*/
#ifndef _HYPOPG_INDEX_H_
#define _HYPOPG_INDEX_H_

#if PG_VERSION_NUM >= 90600
#include "access/amapi.h"
#endif
#include "optimizer/plancat.h"
#include "tcop/utility.h"

#define HYPO_INDEX_NB_COLS		12	/* # of column hypopg() returns */
#define HYPO_INDEX_CREATE_COLS	2	/* # of column hypopg_create_index()
									 * returns */
#define HYPO_HIDDEN_INDEX_COLS	1	/* # of column hypopg_hidden_indexes()
                                     * returns */

#if PG_VERSION_NUM >= 90600
/* hardcode some bloom values, bloom.h is not exported */
#define sizeof_BloomPageOpaqueData 8
#define sizeof_SignType 2
#define BLOOMTUPLEHDRSZ 6
#endif


/*--- Structs --- */

/*--------------------------------------------------------
 * Hypothetical index storage, pretty much an IndexOptInfo
 * Some dynamic informations such as pages and lines are not stored but
 * computed when the hypothetical index is used.
 */
typedef struct hypoIndex
{
	Oid			oid;			/* hypothetical index unique identifier */
	Oid			relid;			/* related relation Oid */
	Oid			reltablespace;	/* tablespace of the index, if set */
	char	   *indexname;		/* hypothetical index name */

	BlockNumber pages;			/* number of estimated disk pages for the
								 * index */
	double		tuples;			/* number of estimated tuples in the index */
#if PG_VERSION_NUM >= 90300
	int			tree_height;	/* estimated index tree height, -1 if unknown */
#endif

	/* index descriptor informations */
	int			ncolumns;		/* number of columns, only 1 for now */
	int			nkeycolumns;	/* number of (hash & range) key columns in index */
	int			nhashcolumns;	/* number of hash key columns in index */
	short int  *indexkeys;		/* attnums */
	Oid		   *indexcollations;	/* OIDs of collations of index columns */
	Oid		   *opfamily;		/* OIDs of operator families for columns */
	Oid		   *opclass;		/* OIDs of opclass data types */
	Oid		   *opcintype;		/* OIDs of opclass declared input data types */
	Oid		   *sortopfamily;	/* OIDs of btree opfamilies, if orderable */
	bool	   *reverse_sort;	/* is sort order descending? */
	bool	   *nulls_first;	/* do NULLs come first in the sort order? */
	Oid			relam;			/* OID of the access method (in pg_am) */

#if PG_VERSION_NUM >= 90600
	amcostestimate_function amcostestimate;
	amcanreturn_function amcanreturn;
#else
	RegProcedure amcostestimate;	/* OID of the access method's cost fcn */
	RegProcedure amcanreturn;	/* OID of the access method's canreturn fcn */
#endif

	List	   *indexprs;		/* expressions for non-simple index columns */
	List	   *indpred;		/* predicate if a partial index, else NIL */

	bool		predOK;			/* true if predicate matches query */
	bool		unique;			/* true if a unique index */
	bool		immediate;		/* is uniqueness enforced immediately? */
#if PG_VERSION_NUM >= 90500
	bool	   *canreturn;		/* which index cols can be returned in an
								 * index-only scan? */
#else
	bool		canreturn;		/* can index return IndexTuples? */
#endif
	bool		amcanorderbyop; /* does AM support order by operator result? */
	bool		amoptionalkey;	/* can query omit key for the first column? */
	bool		amsearcharray;	/* can AM handle ScalarArrayOpExpr quals? */
	bool		amsearchnulls;	/* can AM search for NULL/NOT NULL entries? */
	bool		amhasgettuple;	/* does AM have amgettuple interface? */
	bool		amhasgetbitmap; /* does AM have amgetbitmap interface? */
#if PG_VERSION_NUM >= 110000
	bool		amcanparallel;	/* does AM support parallel scan? */
	bool		amcaninclude;	/* does AM support columns included with clause
								   INCLUDE? */
#endif
	bool		amcanunique;	/* does AM support UNIQUE indexes? */
	bool		amcanmulticol;	/* does AM support multi-column indexes? */

	/* store some informations usually saved in catalogs */
	List	   *options;		/* WITH clause options: a list of DefElem */
	bool		amcanorder;		/* does AM support order by column value? */

}			hypoIndex;

/* List of hypothetic indexes for current backend */
extern List *hypoIndexes;

/* List of hypothetical hidden existing indexes for current backend */
extern List *hypoHiddenIndexes;

/*--- Functions --- */

void		hypo_index_reset(void);

PGDLLEXPORT Datum hypopg(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_create_index(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_drop_index(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_relation_size(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_get_indexdef(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_reset_index(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_hide_index(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_unhide_index(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_unhide_all_indexes(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum hypopg_hidden_indexes(PG_FUNCTION_ARGS);

extern explain_get_index_name_hook_type prev_explain_get_index_name_hook;
hypoIndex *hypo_get_index(Oid indexId);
const char *hypo_explain_get_index_name_hook(Oid indexId);

void		hypo_injectHypotheticalIndex(PlannerInfo *root,
										 Oid relationObjectId,
										 bool inhparent,
										 RelOptInfo *rel,
										 Relation relation,
										 hypoIndex * entry);
void hypo_hideIndexes(RelOptInfo *rel);

#endif
