/*-------------------------------------------------------------------------
 *
 * rumutil.c
 *	  utilities routines for the postgres inverted index access method.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/reloptions.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/index_selfuncs.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
#include "commands/progress.h"
#include "commands/vacuum.h"

#include "pg_documentdb_rum.h"

PG_MODULE_MAGIC;

void _PG_init(void);

PG_FUNCTION_INFO_V1(documentdb_rumhandler);
extern PGDLLIMPORT void InitializeCommonDocumentDBGUCs(const char *rumGucPrefix, const
													   char *documentDBRumGucPrefix);

static char * rumbuildphasename(int64 phasenum);

extern PGDLLIMPORT bool DocumentDBRumLoadCommonGUCs;

extern PGDLLIMPORT bool RumThrowErrorOnInvalidDataPage;

extern PGDLLIMPORT bool RumUseNewItemPtrDecoding;
extern PGDLLIMPORT bool RumEnableParallelVacuumFlags;

/*
 * Module load callback
 */
PGDLLEXPORT void
_PG_init(void)
{
#define RUM_GUC_PREFIX "documentdb_rum"
#define DOCUMENTDB_RUM_GUC_PREFIX "documentdb_rum"

	/* Assert things about the storage format */
	StaticAssertExpr(offsetof(RumPageOpaqueData, dataPageMaxoff) == sizeof(uint64_t),
					 "maxoff must be the 3rd field with a specific offset");
	StaticAssertExpr(offsetof(RumPageOpaqueData, entryPageUnused) == sizeof(uint64_t),
					 "entryPageCycleId must be the 3rd field with a specific offset");
	StaticAssertExpr(offsetof(RumPageOpaqueData, dataPageFreespace) == sizeof(uint64_t) +
					 sizeof(uint16_t),
					 "freespace must be the 3rd field with a specific offset");
	StaticAssertExpr(offsetof(RumPageOpaqueData, flags) == sizeof(uint64_t) +
					 sizeof(uint32_t),
					 "flags must be the 3rd field with a specific offset");

	StaticAssertExpr(offsetof(RumPageOpaqueData, cycleId) == sizeof(uint64_t) +
					 sizeof(uint32_t) + sizeof(uint16_t),
					 "cycleId must be the 4th field with a specific offset");
	StaticAssertExpr(sizeof(RumPageOpaqueData) == sizeof(uint64_t) + sizeof(uint64_t),
					 "RumPageOpaqueData must be the 2 bigint fields worth");

	StaticAssertExpr(sizeof(RumItem) == 16 && MAXALIGN(sizeof(RumItem)) == 16,
					 "rum item aligned should be 16 bytes");
	StaticAssertExpr(sizeof(RumDataLeafItemIndex) == 24, "LeafItemIndex is 24 bytes");

	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_documentdb_extended_rum_core can only be loaded via shared_preload_libraries"),
						errdetail_log(
							"Add the caller library to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	InitializeRumVacuumState();

	/* Define custom GUC variables. */
	RumTrackIncompleteSplit = RUM_DEFAULT_TRACK_INCOMPLETE_SPLIT;
	DefineCustomBoolVariable(
		DOCUMENTDB_RUM_GUC_PREFIX ".track_incomplete_split",
		"Sets whether or not to track incomplete splits",
		NULL,
		&RumTrackIncompleteSplit,
		RUM_DEFAULT_TRACK_INCOMPLETE_SPLIT,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	RumFixIncompleteSplit = RUM_DEFAULT_FIX_INCOMPLETE_SPLIT;
	DefineCustomBoolVariable(
		DOCUMENTDB_RUM_GUC_PREFIX ".fix_incomplete_split",
		"Sets whether or not to fix incomplete splits",
		NULL,
		&RumFixIncompleteSplit,
		RUM_DEFAULT_FIX_INCOMPLETE_SPLIT,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	if (DocumentDBRumLoadCommonGUCs)
	{
		InitializeCommonDocumentDBGUCs(RUM_GUC_PREFIX, DOCUMENTDB_RUM_GUC_PREFIX);
	}

	MarkGUCPrefixReserved(DOCUMENTDB_RUM_GUC_PREFIX);
}


/*
 * RUM handler function: return IndexAmRoutine with access method parameters
 * and callbacks.
 */
PGDLLEXPORT Datum
documentdb_rumhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = 0;
	amroutine->amsupport = RUMNProcs;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = true;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = true;

	/* Since not every rum index can support this
	 * Set this to false. Specific indexes can override
	 * it in the planner as needed.
	 * TODO: After more confidence is built up, we can enable this more
	 * globally.
	 */
	amroutine->amcanparallel = false;

#if PG_VERSION_NUM >= 170000
	amroutine->amcanbuildparallel = RumEnableParallelIndexBuild;
#endif
	amroutine->amkeytype = InvalidOid;

	if (RumEnableParallelVacuumFlags)
	{
		amroutine->amusemaintenanceworkmem = true;
		amroutine->amparallelvacuumoptions =
			VACUUM_OPTION_PARALLEL_BULKDEL | VACUUM_OPTION_PARALLEL_CLEANUP;
	}

	amroutine->ambuild = rumbuild;
	amroutine->ambuildempty = rumbuildempty;
	amroutine->aminsert = ruminsert;
	amroutine->ambulkdelete = rumbulkdelete;
	amroutine->amvacuumcleanup = rumvacuumcleanup;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = documentdb_rum_costestimate;
	amroutine->amoptions = documentdb_rumoptions;
	amroutine->amproperty = rumproperty;
	amroutine->amvalidate = rumvalidate;
	amroutine->ambeginscan = rumbeginscan;
	amroutine->amrescan = rumrescan;
	amroutine->amgettuple = rumgettuple;
	amroutine->amgetbitmap = rumgetbitmap;
	amroutine->amendscan = rumendscan;
	amroutine->ambuildphasename = rumbuildphasename;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = rumestimateparallelscan;
	amroutine->aminitparallelscan = ruminitparallelscan;
	amroutine->amparallelrescan = rumparallelrescan;

	/* Allow operator classes to define custom opclass-options */
	amroutine->amoptsprocnum = RUM_INDEX_CONFIG_PROC;

	PG_RETURN_POINTER(amroutine);
}


/*
 * initRumState: fill in an empty RumState struct to describe the index
 *
 * Note: assorted subsidiary data is allocated in the CurrentMemoryContext.
 */
void
initRumState(RumState *state, Relation index)
{
	TupleDesc origTupdesc = RelationGetDescr(index);
	int i;

	MemSet(state, 0, sizeof(RumState));

	state->index = index;
	state->isBuild = false;
	state->oneCol = (origTupdesc->natts == 1) ? true : false;
	state->origTupdesc = origTupdesc;

	state->attrnAttachColumn = InvalidAttrNumber;
	state->attrnAddToColumn = InvalidAttrNumber;
	if (index->rd_options)
	{
		RumOptions *options = (RumOptions *) index->rd_options;

		if (options->attachColumn > 0)
		{
			char *colname = (char *) options + options->attachColumn;
			AttrNumber attrnOrderByHeapColumn;

			attrnOrderByHeapColumn = get_attnum(index->rd_index->indrelid, colname);

			if (!AttributeNumberIsValid(attrnOrderByHeapColumn))
			{
				elog(ERROR, "attribute \"%s\" is not found in table", colname);
			}

			state->attrnAttachColumn = get_attnum(index->rd_id, colname);

			if (!AttributeNumberIsValid(state->attrnAttachColumn))
			{
				elog(ERROR, "attribute \"%s\" is not found in index", colname);
			}
		}

		if (options->addToColumn > 0)
		{
			char *colname = (char *) options + options->addToColumn;
			AttrNumber attrnAddToHeapColumn;

			attrnAddToHeapColumn = get_attnum(index->rd_index->indrelid, colname);

			if (!AttributeNumberIsValid(attrnAddToHeapColumn))
			{
				elog(ERROR, "attribute \"%s\" is not found in table", colname);
			}

			state->attrnAddToColumn = get_attnum(index->rd_id, colname);

			if (!AttributeNumberIsValid(state->attrnAddToColumn))
			{
				elog(ERROR, "attribute \"%s\" is not found in index", colname);
			}

			if (state->attrnAddToColumn == state->attrnAttachColumn)
			{
				elog(ERROR, "column \"%s\" and attached column cannot be the same",
					 colname);
			}
		}

		if (!(AttributeNumberIsValid(state->attrnAttachColumn) &&
			  AttributeNumberIsValid(state->attrnAddToColumn)))
		{
			elog(ERROR, "AddTo and OrderBy columns should be defined both");
		}

		if (options->useAlternativeOrder)
		{
			if (!(AttributeNumberIsValid(state->attrnAttachColumn) &&
				  AttributeNumberIsValid(state->attrnAddToColumn)))
			{
				elog(ERROR,
					 "to use alternative ordering AddTo and OrderBy should be defined");
			}

			state->useAlternativeOrder = true;
		}
	}

	for (i = 0; i < origTupdesc->natts; i++)
	{
		RumConfig *rumConfig = state->rumConfig + i;
		Form_pg_attribute origAttr = RumTupleDescAttr(origTupdesc, i);

		rumConfig->addInfoTypeOid = InvalidOid;

		if (index_getprocid(index, i + 1, RUM_CONFIG_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->configFn[i]),
						   index_getprocinfo(index, i + 1, RUM_CONFIG_PROC),
						   CurrentMemoryContext);

			FunctionCall1(&state->configFn[i], PointerGetDatum(rumConfig));
		}

		if (state->attrnAddToColumn == i + 1)
		{
			Form_pg_attribute origAddAttr = RumTupleDescAttr(origTupdesc,
															 state->attrnAttachColumn -
															 1);

			if (OidIsValid(rumConfig->addInfoTypeOid))
			{
				elog(ERROR, "AddTo could should not have AddInfo");
			}

			if (state->useAlternativeOrder && origAddAttr->attbyval == false)
			{
				elog(ERROR, "doesn't support order index over pass-by-reference column");
			}

			rumConfig->addInfoTypeOid = origAddAttr->atttypid;
		}

		if (state->oneCol)
		{
			state->tupdesc[i] = CreateTemplateTupleDesc(
#if PG_VERSION_NUM >= 120000
				OidIsValid(rumConfig->addInfoTypeOid) ? 2 : 1);
#else
				OidIsValid(rumConfig->addInfoTypeOid) ? 2 : 1, false);
#endif
			TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 1, NULL,
							   origAttr->atttypid,
							   origAttr->atttypmod,
							   origAttr->attndims);
			TupleDescInitEntryCollation(state->tupdesc[i], (AttrNumber) 1,
										origAttr->attcollation);
			if (OidIsValid(rumConfig->addInfoTypeOid))
			{
				TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 2, NULL,
								   rumConfig->addInfoTypeOid, -1, 0);
				state->addAttrs[i] = RumTupleDescAttr(state->tupdesc[i], 1);
			}
			else
			{
				state->addAttrs[i] = NULL;
			}
		}
		else
		{
			state->tupdesc[i] = CreateTemplateTupleDesc(
#if PG_VERSION_NUM >= 120000
				OidIsValid(rumConfig->addInfoTypeOid) ? 3 : 2);
#else
				OidIsValid(rumConfig->addInfoTypeOid) ? 3 : 2, false);
#endif
			TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 1, NULL,
							   INT2OID, -1, 0);
			TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 2, NULL,
							   origAttr->atttypid,
							   origAttr->atttypmod,
							   origAttr->attndims);
			TupleDescInitEntryCollation(state->tupdesc[i], (AttrNumber) 2,
										origAttr->attcollation);
			if (OidIsValid(rumConfig->addInfoTypeOid))
			{
				TupleDescInitEntry(state->tupdesc[i], (AttrNumber) 3, NULL,
								   rumConfig->addInfoTypeOid, -1, 0);
				state->addAttrs[i] = RumTupleDescAttr(state->tupdesc[i], 2);
			}
			else
			{
				state->addAttrs[i] = NULL;
			}
		}

		/*
		 * If the compare proc isn't specified in the opclass definition, look
		 * up the index key type's default btree comparator.
		 */
		if (index_getprocid(index, i + 1, GIN_COMPARE_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->compareFn[i]),
						   index_getprocinfo(index, i + 1, GIN_COMPARE_PROC),
						   CurrentMemoryContext);
		}
		else
		{
			TypeCacheEntry *typentry;

#if PG_VERSION_NUM < 100000
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("array indexing is only available on PostgreSQL 10+")));
#endif

			typentry = lookup_type_cache(origAttr->atttypid,
										 TYPECACHE_CMP_PROC_FINFO);
			if (!OidIsValid(typentry->cmp_proc_finfo.fn_oid))
			{
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_FUNCTION),
						 errmsg("could not identify a comparison function for type %s",
								format_type_be(origAttr->atttypid))));
			}
			fmgr_info_copy(&(state->compareFn[i]),
						   &(typentry->cmp_proc_finfo),
						   CurrentMemoryContext);
		}

		fmgr_info_copy(&(state->extractValueFn[i]),
					   index_getprocinfo(index, i + 1, GIN_EXTRACTVALUE_PROC),
					   CurrentMemoryContext);
		fmgr_info_copy(&(state->extractQueryFn[i]),
					   index_getprocinfo(index, i + 1, GIN_EXTRACTQUERY_PROC),
					   CurrentMemoryContext);
		fmgr_info_copy(&(state->consistentFn[i]),
					   index_getprocinfo(index, i + 1, GIN_CONSISTENT_PROC),
					   CurrentMemoryContext);

		/*
		 * Check opclass capability to do partial match.
		 */
		if (index_getprocid(index, i + 1, GIN_COMPARE_PARTIAL_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->comparePartialFn[i]),
						   index_getprocinfo(index, i + 1, GIN_COMPARE_PARTIAL_PROC),
						   CurrentMemoryContext);
			state->canPartialMatch[i] = true;
		}
		else
		{
			state->canPartialMatch[i] = false;
		}

		/*
		 * Check opclass capability to do pre consistent check.
		 */
		if (index_getprocid(index, i + 1, RUM_PRE_CONSISTENT_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->preConsistentFn[i]),
						   index_getprocinfo(index, i + 1, RUM_PRE_CONSISTENT_PROC),
						   CurrentMemoryContext);
			state->canPreConsistent[i] = true;
		}
		else
		{
			state->canPreConsistent[i] = false;
		}

		if (index_getprocid(index, i + 1, RUM_CAN_PRE_CONSISTENT_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->canPreConsistentFn[i]),
						   index_getprocinfo(index, i + 1, RUM_CAN_PRE_CONSISTENT_PROC),
						   CurrentMemoryContext);
			state->hasCanPreConsistentFn[i] = true;
		}
		else
		{
			state->hasCanPreConsistentFn[i] = false;
		}

		/*
		 * Check opclass capability to do order by.
		 */
		if (index_getprocid(index, i + 1, RUM_ORDERING_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->orderingFn[i]),
						   index_getprocinfo(index, i + 1, RUM_ORDERING_PROC),
						   CurrentMemoryContext);
			state->canOrdering[i] = true;
		}
		else
		{
			state->canOrdering[i] = false;
		}

		if (index_getprocid(index, i + 1, RUM_OUTER_ORDERING_PROC) != InvalidOid)
		{
			fmgr_info_copy(&(state->outerOrderingFn[i]),
						   index_getprocinfo(index, i + 1, RUM_OUTER_ORDERING_PROC),
						   CurrentMemoryContext);
			state->canOuterOrdering[i] = true;
		}
		else
		{
			state->canOuterOrdering[i] = false;
		}

		if (index_getprocid(index, i + 1, RUM_ADDINFO_JOIN) != InvalidOid)
		{
			fmgr_info_copy(&(state->joinAddInfoFn[i]),
						   index_getprocinfo(index, i + 1, RUM_ADDINFO_JOIN),
						   CurrentMemoryContext);
			state->canJoinAddInfo[i] = true;
		}
		else
		{
			state->canJoinAddInfo[i] = false;
		}

		/*
		 * If the index column has a specified collation, we should honor that
		 * while doing comparisons.  However, we may have a collatable storage
		 * type for a noncollatable indexed data type (for instance, hstore
		 * uses text index entries).  If there's no index collation then
		 * specify default collation in case the support functions need
		 * collation.  This is harmless if the support functions don't care
		 * about collation, so we just do it unconditionally.  (We could
		 * alternatively call get_typcollation, but that seems like expensive
		 * overkill --- there aren't going to be any cases where a RUM storage
		 * type has a nondefault collation.)
		 */
		if (OidIsValid(index->rd_indcollation[i]))
		{
			state->supportCollation[i] = index->rd_indcollation[i];
		}
		else
		{
			state->supportCollation[i] = DEFAULT_COLLATION_OID;
		}
	}
}


/*
 * Extract attribute (column) number of stored entry from RUM tuple
 */
OffsetNumber
rumtuple_get_attrnum(RumState *rumstate, IndexTuple tuple)
{
	OffsetNumber colN;

	if (rumstate->oneCol)
	{
		/* column number is not stored explicitly */
		colN = FirstOffsetNumber;
	}
	else
	{
		Datum res;
		bool isnull;

		/*
		 * First attribute is always int16, so we can safely use any tuple
		 * descriptor to obtain first attribute of tuple
		 */
		res = index_getattr(tuple, FirstOffsetNumber, rumstate->tupdesc[0],
							&isnull);
		Assert(!isnull);

		colN = DatumGetUInt16(res);
		Assert(colN >= FirstOffsetNumber && colN <= rumstate->origTupdesc->natts);
	}

	return colN;
}


/*
 * Extract stored datum (and possible null category) from RUM tuple
 */
Datum
rumtuple_get_key(RumState *rumstate, IndexTuple tuple,
				 RumNullCategory *category)
{
	Datum res;
	bool isnull;

	if (rumstate->oneCol)
	{
		/*
		 * Single column index doesn't store attribute numbers in tuples
		 */
		res = index_getattr(tuple, FirstOffsetNumber, rumstate->origTupdesc,
							&isnull);
	}
	else
	{
		/*
		 * Since the datum type depends on which index column it's from, we
		 * must be careful to use the right tuple descriptor here.
		 */
		OffsetNumber colN = rumtuple_get_attrnum(rumstate, tuple);

		res = index_getattr(tuple, OffsetNumberNext(FirstOffsetNumber),
							rumstate->tupdesc[colN - 1],
							&isnull);
	}

	if (isnull)
	{
		*category = RumGetNullCategory(tuple);
	}
	else
	{
		*category = RUM_CAT_NORM_KEY;
	}

	return res;
}


/*
 * Allocate a new page (either by recycling, or by extending the index file)
 * The returned buffer is already pinned and exclusive-locked
 * Caller is responsible for initializing the page by calling RumInitBuffer
 */
Buffer
RumNewBuffer(Relation index)
{
	Buffer buffer;
	bool needLock;

	/* First, try to get a page from FSM */
	for (;;)
	{
		BlockNumber blkno = GetFreeIndexPage(index);

		if (blkno == InvalidBlockNumber)
		{
			break;
		}

		buffer = ReadBuffer(index, blkno);

		/*
		 * We have to guard against the possibility that someone else already
		 * recycled this page; the buffer may be locked if so.
		 */
		if (ConditionalLockBuffer(buffer))
		{
			Page page = BufferGetPage(buffer);

			if (PageIsNew(page))
			{
				return buffer;  /* OK to use, if never initialized */
			}
			if (RumPageIsDeleted(page))
			{
				return buffer;  /* OK to use */
			}
			LockBuffer(buffer, RUM_UNLOCK);
		}

		/* Can't use it, so release buffer and try again */
		ReleaseBuffer(buffer);
	}

	/* Must extend the file */
	needLock = !RELATION_IS_LOCAL(index);
	if (needLock)
	{
		LockRelationForExtension(index, ExclusiveLock);
	}

	buffer = ReadBuffer(index, P_NEW);
	LockBuffer(buffer, RUM_EXCLUSIVE);

	if (needLock)
	{
		UnlockRelationForExtension(index, ExclusiveLock);
	}

	return buffer;
}


void
RumInitPage(Page page, uint32 f, Size pageSize)
{
	RumPageOpaque opaque;

	PageInit(page, pageSize, sizeof(RumPageOpaqueData));

	opaque = RumPageGetOpaque(page);
	memset(opaque, 0, sizeof(RumPageOpaqueData));
	opaque->flags = f;
	opaque->leftlink = InvalidBlockNumber;
	opaque->rightlink = InvalidBlockNumber;
	RumItemSetMin(RumDataPageGetRightBound(page));
}


void
RumInitBuffer(GenericXLogState *state, Buffer buffer, uint32 flags,
			  bool isBuild)
{
	Page page;

	if (isBuild)
	{
		page = BufferGetPage(buffer);
	}
	else
	{
		page = GenericXLogRegisterBuffer(state, buffer,
										 GENERIC_XLOG_FULL_IMAGE);
	}

	RumInitPage(page, flags, BufferGetPageSize(buffer));
}


void
RumInitMetabuffer(GenericXLogState *state, Buffer metaBuffer, bool isBuild)
{
	Page metaPage;
	RumMetaPageData *metadata;

	/* Initialize contents of meta page */
	if (isBuild)
	{
		metaPage = BufferGetPage(metaBuffer);
	}
	else
	{
		metaPage = GenericXLogRegisterBuffer(state, metaBuffer,
											 GENERIC_XLOG_FULL_IMAGE);
	}

	RumInitPage(metaPage, RUM_META, BufferGetPageSize(metaBuffer));
	metadata = RumPageGetMeta(metaPage);
	memset(metadata, 0, sizeof(RumMetaPageData));

	metadata->head = metadata->tail = InvalidBlockNumber;
	metadata->tailFreeSize = 0;
	metadata->nPendingPages = 0;
	metadata->nPendingHeapTuples = 0;
	metadata->nTotalPages = 0;
	metadata->nEntryPages = 0;
	metadata->nDataPages = 0;
	metadata->nEntries = 0;
	metadata->rumVersion = RUM_CURRENT_VERSION;

	((PageHeader) metaPage)->pd_lower += sizeof(RumMetaPageData);
}


/*
 * Compare two keys of the same index column
 */
int
rumCompareEntries(RumState *rumstate, OffsetNumber attnum,
				  Datum a, RumNullCategory categorya,
				  Datum b, RumNullCategory categoryb)
{
	/* if not of same null category, sort by that first */
	if (categorya != categoryb)
	{
		return (categorya < categoryb) ? -1 : 1;
	}

	/* all null items in same category are equal */
	if (categorya != RUM_CAT_NORM_KEY)
	{
		return 0;
	}

	/* both not null, so safe to call the compareFn */
	return DatumGetInt32(FunctionCall2Coll(&rumstate->compareFn[attnum - 1],
										   rumstate->supportCollation[attnum - 1],
										   a, b));
}


/*
 * Compare two keys of possibly different index columns
 */
int
rumCompareAttEntries(RumState *rumstate,
					 OffsetNumber attnuma, Datum a, RumNullCategory categorya,
					 OffsetNumber attnumb, Datum b, RumNullCategory categoryb)
{
	/* attribute number is the first sort key */
	if (attnuma != attnumb)
	{
		return (attnuma < attnumb) ? -1 : 1;
	}

	return rumCompareEntries(rumstate, attnuma, a, categorya, b, categoryb);
}


/*
 * Support for sorting key datums in rumExtractEntries
 *
 * Note: we only have to worry about null and not-null keys here;
 * rumExtractEntries never generates more than one placeholder null,
 * so it doesn't have to sort those.
 */
typedef struct
{
	Datum datum;
	Datum addInfo;
	bool isnull;
	bool addInfoIsNull;
} keyEntryData;

typedef struct
{
	FmgrInfo *cmpDatumFunc;
	Oid collation;
	bool haveDups;
} cmpEntriesArg;

static int
cmpEntries(const void *a, const void *b, void *arg)
{
	const keyEntryData *aa = (const keyEntryData *) a;
	const keyEntryData *bb = (const keyEntryData *) b;
	cmpEntriesArg *data = (cmpEntriesArg *) arg;
	int res;

	if (aa->isnull)
	{
		if (bb->isnull)
		{
			res = 0;            /* NULL "=" NULL */
		}
		else
		{
			res = 1;            /* NULL ">" not-NULL */
		}
	}
	else if (bb->isnull)
	{
		res = -1;               /* not-NULL "<" NULL */
	}
	else
	{
		res = DatumGetInt32(FunctionCall2Coll(data->cmpDatumFunc,
											  data->collation,
											  aa->datum, bb->datum));
	}

	/*
	 * Detect if we have any duplicates.  If there are equal keys, qsort must
	 * compare them at some point, else it wouldn't know whether one should go
	 * before or after the other.
	 */
	if (res == 0)
	{
		data->haveDups = true;
	}

	return res;
}


/*
 * Extract the index key values from an indexable item
 *
 * The resulting key values are sorted, and any duplicates are removed.
 * This avoids generating redundant index entries.
 */
Datum *
rumExtractEntries(RumState *rumstate, OffsetNumber attnum,
				  Datum value, bool isNull,
				  int32 *nentries, RumNullCategory **categories,
				  Datum **addInfo, bool **addInfoIsNull)
{
	Datum *entries;
	bool *nullFlags;
	int32 i;

	/*
	 * We don't call the extractValueFn on a null item.  Instead generate a
	 * placeholder.
	 */
	if (isNull)
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = (Datum) 0;
		*addInfo = (Datum *) palloc(sizeof(Datum));
		(*addInfo)[0] = (Datum) 0;
		*addInfoIsNull = (bool *) palloc(sizeof(bool));
		(*addInfoIsNull)[0] = true;
		*categories = (RumNullCategory *) palloc(sizeof(RumNullCategory));
		(*categories)[0] = RUM_CAT_NULL_ITEM;
		return entries;
	}

	/* OK, call the opclass's extractValueFn */
	nullFlags = NULL;           /* in case extractValue doesn't set it */
	*addInfo = NULL;
	*addInfoIsNull = NULL;
	entries = (Datum *)
			  DatumGetPointer(FunctionCall5Coll(&rumstate->extractValueFn[attnum - 1],
												rumstate->supportCollation[attnum - 1],
												value,
												PointerGetDatum(nentries),
												PointerGetDatum(&nullFlags),
												PointerGetDatum(addInfo),
												PointerGetDatum(addInfoIsNull)
												));

	/*
	 * Generate a placeholder if the item contained no keys.
	 */
	if (entries == NULL || *nentries <= 0)
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = (Datum) 0;
		*addInfo = (Datum *) palloc(sizeof(Datum));
		(*addInfo)[0] = (Datum) 0;
		*addInfoIsNull = (bool *) palloc(sizeof(bool));
		(*addInfoIsNull)[0] = true;
		*categories = (RumNullCategory *) palloc(sizeof(RumNullCategory));
		(*categories)[0] = RUM_CAT_EMPTY_ITEM;
		return entries;
	}

	if (!(*addInfo))
	{
		(*addInfo) = (Datum *) palloc(sizeof(Datum) * *nentries);
		for (i = 0; i < *nentries; i++)
		{
			(*addInfo)[i] = (Datum) 0;
		}
	}
	if (!(*addInfoIsNull))
	{
		(*addInfoIsNull) = (bool *) palloc(sizeof(bool) * *nentries);
		for (i = 0; i < *nentries; i++)
		{
			(*addInfoIsNull)[i] = true;
		}
	}

	/*
	 * If the extractValueFn didn't create a nullFlags array, create one,
	 * assuming that everything's non-null.  Otherwise, run through the array
	 * and make sure each value is exactly 0 or 1; this ensures binary
	 * compatibility with the RumNullCategory representation.
	 */
	if (nullFlags == NULL)
	{
		nullFlags = (bool *) palloc0(*nentries * sizeof(bool));
	}
	else
	{
		for (i = 0; i < *nentries; i++)
		{
			nullFlags[i] = (nullFlags[i] ? true : false);
		}
	}

	/* now we can use the nullFlags as category codes */
	*categories = (RumNullCategory *) nullFlags;

	/*
	 * If there's more than one key, sort and unique-ify.
	 *
	 * XXX Using qsort here is notationally painful, and the overhead is
	 * pretty bad too.  For small numbers of keys it'd likely be better to use
	 * a simple insertion sort.
	 */
	if (*nentries > 1)
	{
		keyEntryData *keydata;
		cmpEntriesArg arg;

		keydata = (keyEntryData *) palloc(*nentries * sizeof(keyEntryData));
		for (i = 0; i < *nentries; i++)
		{
			keydata[i].datum = entries[i];
			keydata[i].isnull = nullFlags[i];
			keydata[i].addInfo = (*addInfo)[i];
			keydata[i].addInfoIsNull = (*addInfoIsNull)[i];
		}

		arg.cmpDatumFunc = &rumstate->compareFn[attnum - 1];
		arg.collation = rumstate->supportCollation[attnum - 1];
		arg.haveDups = false;
		qsort_arg(keydata, *nentries, sizeof(keyEntryData),
				  cmpEntries, (void *) &arg);

		if (arg.haveDups)
		{
			/* there are duplicates, must get rid of 'em */
			int32 j;

			entries[0] = keydata[0].datum;
			nullFlags[0] = keydata[0].isnull;
			(*addInfo)[0] = keydata[0].addInfo;
			(*addInfoIsNull)[0] = keydata[0].addInfoIsNull;
			j = 1;
			for (i = 1; i < *nentries; i++)
			{
				if (cmpEntries(&keydata[i - 1], &keydata[i], &arg) != 0)
				{
					entries[j] = keydata[i].datum;
					nullFlags[j] = keydata[i].isnull;
					(*addInfo)[j] = keydata[i].addInfo;
					(*addInfoIsNull)[j] = keydata[i].addInfoIsNull;
					j++;
				}
			}
			*nentries = j;
		}
		else
		{
			/* easy, no duplicates */
			for (i = 0; i < *nentries; i++)
			{
				entries[i] = keydata[i].datum;
				nullFlags[i] = keydata[i].isnull;
				(*addInfo)[i] = keydata[i].addInfo;
				(*addInfoIsNull)[i] = keydata[i].addInfoIsNull;
			}
		}

		pfree(keydata);
	}

	return entries;
}


bool
rumproperty(Oid index_oid, int attno,
			IndexAMProperty prop, const char *propname,
			bool *res, bool *isnull)
{
	HeapTuple tuple;
	Form_pg_index rd_index PG_USED_FOR_ASSERTS_ONLY;
	Form_pg_opclass rd_opclass;
	Datum datum;
	bool disnull;
	oidvector *indclass;
	Oid opclass,
		opfamily,
		opcintype;
	int16 procno;

	/* Only answer column-level inquiries */
	if (attno == 0)
	{
		return false;
	}

	switch (prop)
	{
		case AMPROP_DISTANCE_ORDERABLE:
		{
			procno = RUM_ORDERING_PROC;
			break;
		}

		default:
			return false;
	}

	/* First we need to know the column's opclass. */

	tuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(index_oid));
	if (!HeapTupleIsValid(tuple))
	{
		*isnull = true;
		return true;
	}
	rd_index = (Form_pg_index) GETSTRUCT(tuple);

	/* caller is supposed to guarantee this */
	Assert(attno > 0 && attno <= rd_index->indnatts);

	datum = SysCacheGetAttr(INDEXRELID, tuple,
							Anum_pg_index_indclass, &disnull);
	Assert(!disnull);

	indclass = ((oidvector *) DatumGetPointer(datum));
	opclass = indclass->values[attno - 1];

	ReleaseSysCache(tuple);

	/* Now look up the opclass family and input datatype. */

	tuple = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclass));
	if (!HeapTupleIsValid(tuple))
	{
		*isnull = true;
		return true;
	}
	rd_opclass = (Form_pg_opclass) GETSTRUCT(tuple);

	opfamily = rd_opclass->opcfamily;
	opcintype = rd_opclass->opcintype;

	ReleaseSysCache(tuple);

	/* And now we can check whether the function is provided. */

	*res = SearchSysCacheExists4(AMPROCNUM,
								 ObjectIdGetDatum(opfamily),
								 ObjectIdGetDatum(opcintype),
								 ObjectIdGetDatum(opcintype),
								 Int16GetDatum(procno));
	return true;
}


/*
 * Fetch index's statistical data into *stats
 *
 * Note: in the result, nPendingPages can be trusted to be up-to-date,
 * as can rumVersion; but the other fields are as of the last VACUUM.
 */
void
rumGetStats(Relation index, RumStatsData *stats)
{
	Buffer metabuffer;
	Page metapage;
	RumMetaPageData *metadata;

	metabuffer = ReadBuffer(index, RUM_METAPAGE_BLKNO);
	LockBuffer(metabuffer, RUM_SHARE);
	metapage = BufferGetPage(metabuffer);
	metadata = RumPageGetMeta(metapage);

	stats->nPendingPages = metadata->nPendingPages;
	stats->nTotalPages = metadata->nTotalPages;
	stats->nEntryPages = metadata->nEntryPages;
	stats->nDataPages = metadata->nDataPages;
	stats->nEntries = metadata->nEntries;
	stats->ginVersion = metadata->rumVersion;

	if (stats->ginVersion != RUM_CURRENT_VERSION)
	{
		elog(ERROR, "unexpected RUM index version. Reindex");
	}

	UnlockReleaseBuffer(metabuffer);
}


/*
 * Write the given statistics to the index's metapage
 *
 * Note: nPendingPages and rumVersion are *not* copied over
 */
void
rumUpdateStats(Relation index, const RumStatsData *stats, bool isBuild)
{
	Buffer metaBuffer;
	Page metapage;
	RumMetaPageData *metadata;
	GenericXLogState *state;

	metaBuffer = ReadBuffer(index, RUM_METAPAGE_BLKNO);
	LockBuffer(metaBuffer, RUM_EXCLUSIVE);
	if (isBuild)
	{
		metapage = BufferGetPage(metaBuffer);
		START_CRIT_SECTION();
	}
	else
	{
		state = GenericXLogStart(index);
		metapage = GenericXLogRegisterBuffer(state, metaBuffer, 0);
	}
	metadata = RumPageGetMeta(metapage);

	metadata->nTotalPages = stats->nTotalPages;
	metadata->nEntryPages = stats->nEntryPages;
	metadata->nDataPages = stats->nDataPages;
	metadata->nEntries = stats->nEntries;

	if (isBuild)
	{
		MarkBufferDirty(metaBuffer);
	}
	else
	{
		GenericXLogFinish(state);
	}

	UnlockReleaseBuffer(metaBuffer);

	if (isBuild)
	{
		END_CRIT_SECTION();
	}
}


Datum
FunctionCall10Coll(FmgrInfo *flinfo, Oid collation, Datum arg1, Datum arg2,
				   Datum arg3, Datum arg4, Datum arg5,
				   Datum arg6, Datum arg7, Datum arg8,
				   Datum arg9, Datum arg10)
{
	Datum result;
#if PG_VERSION_NUM >= 120000
	LOCAL_FCINFO(fcinfo, 10);

	InitFunctionCallInfoData(*fcinfo, flinfo, 10, collation, NULL, NULL);

	fcinfo->args[0].value = arg1;
	fcinfo->args[0].isnull = false;
	fcinfo->args[1].value = arg2;
	fcinfo->args[1].isnull = false;
	fcinfo->args[2].value = arg3;
	fcinfo->args[2].isnull = false;
	fcinfo->args[3].value = arg4;
	fcinfo->args[3].isnull = false;
	fcinfo->args[4].value = arg5;
	fcinfo->args[4].isnull = false;
	fcinfo->args[5].value = arg6;
	fcinfo->args[5].isnull = false;
	fcinfo->args[6].value = arg7;
	fcinfo->args[6].isnull = false;
	fcinfo->args[7].value = arg8;
	fcinfo->args[7].isnull = false;
	fcinfo->args[8].value = arg9;
	fcinfo->args[8].isnull = false;
	fcinfo->args[9].value = arg10;
	fcinfo->args[9].isnull = false;

	result = FunctionCallInvoke(fcinfo);

	/* Check for null result, since caller is clearly not expecting one */
	if (fcinfo->isnull)
	{
		elog(ERROR, "function %u returned NULL", fcinfo->flinfo->fn_oid);
	}
#else
	FunctionCallInfoData fcinfo;

	InitFunctionCallInfoData(fcinfo, flinfo, 10, collation, NULL, NULL);

	fcinfo.arg[0] = arg1;
	fcinfo.arg[1] = arg2;
	fcinfo.arg[2] = arg3;
	fcinfo.arg[3] = arg4;
	fcinfo.arg[4] = arg5;
	fcinfo.arg[5] = arg6;
	fcinfo.arg[6] = arg7;
	fcinfo.arg[7] = arg8;
	fcinfo.arg[8] = arg9;
	fcinfo.arg[9] = arg10;
	fcinfo.argnull[0] = false;
	fcinfo.argnull[1] = false;
	fcinfo.argnull[2] = false;
	fcinfo.argnull[3] = false;
	fcinfo.argnull[4] = false;
	fcinfo.argnull[5] = false;
	fcinfo.argnull[6] = false;
	fcinfo.argnull[7] = false;
	fcinfo.argnull[8] = false;
	fcinfo.argnull[9] = false;

	result = FunctionCallInvoke(&fcinfo);

	/* Check for null result, since caller is clearly not expecting one */
	if (fcinfo.isnull)
	{
		elog(ERROR, "function %u returned NULL", fcinfo.flinfo->fn_oid);
	}
#endif

	return result;
}


static char *
rumbuildphasename(int64 phasenum)
{
	switch (phasenum)
	{
		case PROGRESS_CREATEIDX_SUBPHASE_INITIALIZE:
		{
			return "initializing";
		}

		case PROGRESS_RUM_PHASE_INDEXBUILD_TABLESCAN:
		{
			return "scanning table";
		}

		case PROGRESS_RUM_PHASE_PERFORMSORT_1:
		{
			return "sorting tuples (workers)";
		}

		case PROGRESS_RUM_PHASE_MERGE_1:
		{
			return "merging tuples (workers)";
		}

		case PROGRESS_RUM_PHASE_PERFORMSORT_2:
		{
			return "sorting tuples";
		}

		case PROGRESS_RUM_PHASE_MERGE_2:
		{
			return "merging tuples";
		}

		case PROGRESS_RUM_PHASE_WRITE_WAL:
		{
			return "writing WAL files";
		}

		default:
			return NULL;
	}
}
