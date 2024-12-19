/*-------------------------------------------------------------------------
 *
 * amapi.h
 *	  API for Postgres index access methods.
 *
 * Copyright (c) 2015-2022, PostgreSQL Global Development Group
 *
 * src/include/access/amapi.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef AMAPI_H
#define AMAPI_H

#include "access/genam.h"

/*
 * We don't wish to include planner header files here, since most of an index
 * AM's implementation isn't concerned with those data structures.  To allow
 * declaring amcostestimate_function here, use forward struct references.
 */
struct PlannerInfo;
struct IndexPath;

/* Likewise, this file shouldn't depend on execnodes.h. */
struct IndexInfo;

/*
 * BACKFILL input and output nodes.
 * As above, avoiding dependencies on execnodes.h and parsenodes.h.
 */
struct YbBackfillInfo;
struct YbPgExecOutParam;

/*
 * Properties for amproperty API.  This list covers properties known to the
 * core code, but an index AM can define its own properties, by matching the
 * string property name.
 */
typedef enum IndexAMProperty
{
	AMPROP_UNKNOWN = 0,			/* anything not known to core code */
	AMPROP_ASC,					/* column properties */
	AMPROP_DESC,
	AMPROP_NULLS_FIRST,
	AMPROP_NULLS_LAST,
	AMPROP_ORDERABLE,
	AMPROP_DISTANCE_ORDERABLE,
	AMPROP_RETURNABLE,
	AMPROP_SEARCH_ARRAY,
	AMPROP_SEARCH_NULLS,
	AMPROP_CLUSTERABLE,			/* index properties */
	AMPROP_INDEX_SCAN,
	AMPROP_BITMAP_SCAN,
	AMPROP_BACKWARD_SCAN,
	AMPROP_CAN_ORDER,			/* AM properties */
	AMPROP_CAN_UNIQUE,
	AMPROP_CAN_MULTI_COL,
	AMPROP_CAN_EXCLUDE,
	AMPROP_CAN_INCLUDE
} IndexAMProperty;

/*
 * We use lists of this struct type to keep track of both operators and
 * support functions while building or adding to an opclass or opfamily.
 * amadjustmembers functions receive lists of these structs, and are allowed
 * to alter their "ref" fields.
 *
 * The "ref" fields define how the pg_amop or pg_amproc entry should depend
 * on the associated objects (that is, which dependency type to use, and
 * which opclass or opfamily it should depend on).
 *
 * If ref_is_hard is true, the entry will have a NORMAL dependency on the
 * operator or support func, and an INTERNAL dependency on the opclass or
 * opfamily.  This forces the opclass or opfamily to be dropped if the
 * operator or support func is dropped, and requires the CASCADE option
 * to do so.  Nor will ALTER OPERATOR FAMILY DROP be allowed.  This is
 * the right behavior for objects that are essential to an opclass.
 *
 * If ref_is_hard is false, the entry will have an AUTO dependency on the
 * operator or support func, and also an AUTO dependency on the opclass or
 * opfamily.  This allows ALTER OPERATOR FAMILY DROP, and causes that to
 * happen automatically if the operator or support func is dropped.  This
 * is the right behavior for inessential ("loose") objects.
 */
typedef struct OpFamilyMember
{
	bool		is_func;		/* is this an operator, or support func? */
	Oid			object;			/* operator or support func's OID */
	int			number;			/* strategy or support func number */
	Oid			lefttype;		/* lefttype */
	Oid			righttype;		/* righttype */
	Oid			sortfamily;		/* ordering operator's sort opfamily, or 0 */
	bool		ref_is_hard;	/* hard or soft dependency? */
	bool		ref_is_family;	/* is dependency on opclass or opfamily? */
	Oid			refobjid;		/* OID of opclass or opfamily */
} OpFamilyMember;


/*
 * Callback function signatures --- see indexam.sgml for more info.
 */

/* build new index */
typedef IndexBuildResult *(*ambuild_function) (Relation heapRelation,
											   Relation indexRelation,
											   struct IndexInfo *indexInfo);

/* build empty index */
typedef void (*ambuildempty_function) (Relation indexRelation);

/* insert this tuple */
typedef bool (*aminsert_function) (Relation indexRelation,
								   Datum *values,
								   bool *isnull,
								   ItemPointer heap_tid,
								   Relation heapRelation,
								   IndexUniqueCheck checkUnique,
								   bool indexUnchanged,
								   struct IndexInfo *indexInfo);

/* alternate insert callback for YugaByte-based index that passs ybctid instead of ctid */
typedef bool (*yb_aminsert_function) (Relation indexRelation,
									  Datum *values,
									  bool *isnull,
									  Datum ybctid,
									  Relation heapRelation,
									  IndexUniqueCheck checkUnique,
									  struct IndexInfo *indexInfo,
									  bool shared_insert);

/* delete this tuple for YugaByte-based index */
typedef void (*yb_amdelete_function) (Relation indexRelation,
									  Datum *values,
									  bool *isnull,
									  Datum ybctid,
									  Relation heapRelation,
									  struct IndexInfo *indexInfo);

/* update the tuple identified by 'oldYbctid' for a Yugabyte-based index */
typedef void (*yb_amupdate_function) (Relation indexRelation,
									  Datum *values,
									  bool *isnull,
									  Datum oldYbctid,
									  Datum newYbctid,
									  Relation heapRelation,
									  struct IndexInfo *indexInfo);

/* backfill this Yugabyte-based index */
typedef IndexBuildResult *(*yb_ambackfill_function) (Relation heapRelation,
													 Relation indexRelation,
													 struct IndexInfo *indexInfo,
													 struct YbBackfillInfo *bfinfo,
													 struct YbPgExecOutParam *bfresult);

/* return whether this Yugabyte-based index might recheck indexquals */
typedef bool (*yb_ammightrecheck_function) (Relation heap,
											Relation index,
											bool xs_want_itup,
											ScanKey keys,
											int nkeys);

/* bulk delete */
typedef IndexBulkDeleteResult *(*ambulkdelete_function) (IndexVacuumInfo *info,
														 IndexBulkDeleteResult *stats,
														 IndexBulkDeleteCallback callback,
														 void *callback_state);

/* post-VACUUM cleanup */
typedef IndexBulkDeleteResult *(*amvacuumcleanup_function) (IndexVacuumInfo *info,
															IndexBulkDeleteResult *stats);

/* can indexscan return IndexTuples? */
typedef bool (*amcanreturn_function) (Relation indexRelation, int attno);

/* estimate cost of an indexscan */
typedef void (*amcostestimate_function) (struct PlannerInfo *root,
										 struct IndexPath *path,
										 double loop_count,
										 Cost *indexStartupCost,
										 Cost *indexTotalCost,
										 Selectivity *indexSelectivity,
										 double *indexCorrelation,
										 double *indexPages);

/* parse index reloptions */
typedef bytea *(*amoptions_function) (Datum reloptions,
									  bool validate);

/* report AM, index, or index column property */
typedef bool (*amproperty_function) (Oid index_oid, int attno,
									 IndexAMProperty prop, const char *propname,
									 bool *res, bool *isnull);

/* name of phase as used in progress reporting */
typedef char *(*ambuildphasename_function) (int64 phasenum);

/* validate definition of an opclass for this AM */
typedef bool (*amvalidate_function) (Oid opclassoid);

/* validate operators and support functions to be added to an opclass/family */
typedef void (*amadjustmembers_function) (Oid opfamilyoid,
										  Oid opclassoid,
										  List *operators,
										  List *functions);

/* prepare for index scan */
typedef IndexScanDesc (*ambeginscan_function) (Relation indexRelation,
											   int nkeys,
											   int norderbys);

/* (re)start index scan */
typedef void (*amrescan_function) (IndexScanDesc scan,
								   ScanKey keys,
								   int nkeys,
								   ScanKey orderbys,
								   int norderbys);

/* next valid tuple */
typedef bool (*amgettuple_function) (IndexScanDesc scan,
									 ScanDirection direction);

/* fetch all valid tuples */
typedef int64 (*amgetbitmap_function) (IndexScanDesc scan,
									   TIDBitmap *tbm);

/* YB: fetch all valid tuples */
typedef int64 (*yb_amgetbitmap_function) (IndexScanDesc scan,
										  YbTIDBitmap *ybtbm);

typedef void (*yb_ambindschema_function) (YBCPgStatement handle,
										  struct IndexInfo *indexInfo,
										  TupleDesc indexTupleDesc,
										  int16 *coloptions);

/* end index scan */
typedef void (*amendscan_function) (IndexScanDesc scan);

/* mark current scan position */
typedef void (*ammarkpos_function) (IndexScanDesc scan);

/* restore marked scan position */
typedef void (*amrestrpos_function) (IndexScanDesc scan);

/*
 * Callback function signatures - for parallel index scans.
 */

/* estimate size of parallel scan descriptor */
typedef Size (*amestimateparallelscan_function) (void);

/* prepare for parallel index scan */
typedef void (*aminitparallelscan_function) (void *target);

/* (re)start parallel index scan */
typedef void (*amparallelrescan_function) (IndexScanDesc scan);

/*
 * API struct for an index AM.  Note this must be stored in a single palloc'd
 * chunk of memory.
 */
typedef struct IndexAmRoutine
{
	NodeTag		type;

	/*
	 * Total number of strategies (operators) by which we can traverse/search
	 * this AM.  Zero if AM does not have a fixed set of strategy assignments.
	 */
	uint16		amstrategies;
	/* total number of support functions that this AM uses */
	uint16		amsupport;
	/* opclass options support function number or 0 */
	uint16		amoptsprocnum;
	/* does AM support ORDER BY indexed column's value? */
	bool		amcanorder;
	/* does AM support ORDER BY result of an operator on indexed column? */
	bool		amcanorderbyop;
	/* does AM support backward scanning? */
	bool		amcanbackward;
	/* does AM support UNIQUE indexes? */
	bool		amcanunique;
	/* does AM support multi-column indexes? */
	bool		amcanmulticol;
	/* does AM require scans to have a constraint on the first index column? */
	bool		amoptionalkey;
	/* does AM handle ScalarArrayOpExpr quals? */
	bool		amsearcharray;
	/* does AM handle IS NULL/IS NOT NULL quals? */
	bool		amsearchnulls;
	/* can index storage data type differ from column data type? */
	bool		amstorage;
	/* can an index of this type be clustered on? */
	bool		amclusterable;
	/* does AM handle predicate locks? */
	bool		ampredlocks;
	/* does AM support parallel scan? */
	bool		amcanparallel;
	/* does AM support columns included with clause INCLUDE? */
	bool		amcaninclude;
	/* does AM use maintenance_work_mem? */
	bool		amusemaintenanceworkmem;
	/* OR of parallel vacuum flags.  See vacuum.h for flags. */
	uint8		amparallelvacuumoptions;
	/* does AM support in-place update of non-key columns? */
	bool		ybamcanupdatetupleinplace;
	/* type of data stored in index, or InvalidOid if variable */
	Oid			amkeytype;

	/*
	 * If you add new properties to either the above or the below lists, then
	 * they should also (usually) be exposed via the property API (see
	 * IndexAMProperty at the top of the file, and utils/adt/amutils.c).
	 */

	/* interface functions */
	ambuild_function ambuild;
	ambuildempty_function ambuildempty;
	aminsert_function aminsert;
	ambulkdelete_function ambulkdelete;
	amvacuumcleanup_function amvacuumcleanup;
	amcanreturn_function amcanreturn;	/* can be NULL */
	amcostestimate_function amcostestimate;
	amoptions_function amoptions;
	amproperty_function amproperty; /* can be NULL */
	ambuildphasename_function ambuildphasename; /* can be NULL */
	amvalidate_function amvalidate;
	amadjustmembers_function amadjustmembers;	/* can be NULL */
	ambeginscan_function ambeginscan;
	amrescan_function amrescan;
	amgettuple_function amgettuple; /* can be NULL */
	amgetbitmap_function amgetbitmap;	/* can be NULL */
	amendscan_function amendscan;
	ammarkpos_function ammarkpos;	/* can be NULL */
	amrestrpos_function amrestrpos; /* can be NULL */

	/* interface functions to support parallel index scans */
	amestimateparallelscan_function amestimateparallelscan; /* can be NULL */
	aminitparallelscan_function aminitparallelscan; /* can be NULL */
	amparallelrescan_function amparallelrescan; /* can be NULL */

	/* YB properties */
	/* Whether this AM is for YB relations. */
	bool		yb_amisforybrelation;
	bool		yb_amiscopartitioned;

	/* YB functions */
	yb_aminsert_function yb_aminsert;
	yb_amdelete_function yb_amdelete;
	yb_amupdate_function yb_amupdate;
	yb_ambackfill_function yb_ambackfill;
	yb_ammightrecheck_function yb_ammightrecheck;
	yb_amgetbitmap_function yb_amgetbitmap;

	/*
	 * Allows YB AM's to specify how they want to lay out their
	 * DocDB schema and also gives them a chance to pass along any
	 * additional metadata to the index creation statement.
	 */
	yb_ambindschema_function yb_ambindschema;

} IndexAmRoutine;


/* Functions in access/index/amapi.c */
extern IndexAmRoutine *GetIndexAmRoutine(Oid amhandler);
extern IndexAmRoutine *GetIndexAmRoutineByAmId(Oid amoid, bool noerror);

#endif							/* AMAPI_H */
