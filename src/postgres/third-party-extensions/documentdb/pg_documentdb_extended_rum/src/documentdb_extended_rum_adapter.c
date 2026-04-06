/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/documentdb_extended_rum_adapter.c
 *
 * Initialize rum at the initialization of the index.
 * This has overrides for the documentdb_rum index that is an
 * extensibility access method for documentdb's query engine.
 *
 * This provides an alternate index_am that can be enabled in documentbd
 * using the AlternateIndexHandler before creating indexes.
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include <miscadmin.h>
#include "pg_documentdb_rum.h"
#include <catalog/pg_am.h>
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "commands/defrem.h"

#include "index_am/index_am_exports.h"
#include "index_am/documentdb_rum.h"


PG_MODULE_MAGIC;

void _PG_init(void);

/* Data Type declarations */
typedef struct DocumentDBRumOidCacheData
{
	Oid DocumentDBRumAmOid;
	Oid BsonDocumentDBRumSinglePathOperatorFamilyId;
	Oid BsonDocumentDBRumCompositePathOperatorFamilyId;
} DocumentDBRumOidCacheData;

extern bool EnableCompositeIndexPlanner;

/* Method Declarations */
static Oid DocumentDBExtendedRumIndexAmId(void);
static Oid DocumentDBExtendedRumSinglePathOpFamilyOid(void);
static Oid DocumentDBExtendedRumCompositePathOpFamilyOid(void);
static const char * GetDocumentDBCatalogSchema(void);
static void LoadBaseIndexAmRoutine(void);

extern PGDLLIMPORT void try_explain_documentdb_rum_index(IndexScanDesc scan, struct
														 ExplainState *es);
extern PGDLLIMPORT bool can_documentdb_rum_index_scan_ordered(IndexScanDesc scan);
extern PGDLLIMPORT Datum documentdb_rumhandler(PG_FUNCTION_ARGS);
extern PGDLLEXPORT bool documentdb_rum_get_multi_key_status(Relation indexRelation);
extern PGDLLEXPORT void documentdb_rum_update_multi_key_status(Relation indexRelation);

/* Static Globals */
static BsonIndexAmEntry DocumentDBIndexAmEntry = {
	.is_single_path_index_supported = true,
	.is_unique_index_supported = false,
	.is_wild_card_supported = false,
	.is_composite_index_supported = true,
	.is_text_index_supported = false,
	.is_hashed_index_supported = false,
	.is_order_by_supported = true,
	.is_backwards_scan_supported = true,
	.is_index_only_scan_supported = true,
	.can_support_parallel_scans = true,
	.get_am_oid = DocumentDBExtendedRumIndexAmId,
	.get_single_path_op_family_oid = DocumentDBExtendedRumSinglePathOpFamilyOid,
	.get_composite_path_op_family_oid = DocumentDBExtendedRumCompositePathOpFamilyOid,
	.get_text_path_op_family_oid = NULL,
	.get_unique_path_op_family_oid = NULL,
	.get_hashed_path_op_family_oid = NULL,
	.add_explain_output = try_explain_documentdb_rum_index,
	.am_name = "extended_rum",
	.get_opclass_catalog_schema = GetDocumentDBCatalogSchema,
	.get_opclass_internal_catalog_schema = GetDocumentDBCatalogSchema,
	.get_multikey_status = documentdb_rum_get_multi_key_status,
	.get_truncation_status = RumGetTruncationStatus,
};
static DocumentDBRumOidCacheData Cache = { 0 };
static bool has_custom_routine = false;
static IndexAmRoutine core_rum_routine = { 0 };
static void InitializeDocumentDBRum(void);

/* Top level method exports */
PG_FUNCTION_INFO_V1(documentdb_extended_rumhandler);

PGDLLEXPORT void
_PG_init(void)
{
	InitializeDocumentDBRum();
}


static void
InitializeDocumentDBRum(void)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"pg_documentdb_extended_rum can only be loaded via shared_preload_libraries"),
						errdetail_log(
							"Add pg_documentdb_extended_rum to shared_preload_libraries configuration "
							"variable in postgresql.conf. ")));
	}

	LoadBaseIndexAmRoutine();
	RegisterIndexAm(DocumentDBIndexAmEntry);
}


/* Method implementations */

inline static void
EnsureDocumentDBExtendedRumLib(void)
{
	if (unlikely(!has_custom_routine))
	{
		ereport(ERROR, (errmsg(
							"The documentdb_rum library should be loaded as part of shared_preload_libraries")));
	}
}


static IndexScanDesc
extension_documentdb_extended_rumbeginscan(Relation rel, int nkeys, int norderbys)
{
	EnsureDocumentDBExtendedRumLib();
	return extension_rumbeginscan_core(rel, nkeys, norderbys,
									   &core_rum_routine);
}


static void
extension_documentdb_extended_rumendscan(IndexScanDesc scan)
{
	EnsureDocumentDBExtendedRumLib();
	extension_rumendscan_core(scan, &core_rum_routine);
}


static void
extension_documentdb_extended_rumrescan(IndexScanDesc scan, ScanKey scankey, int
										nscankeys,
										ScanKey orderbys, int norderbys)
{
	EnsureDocumentDBExtendedRumLib();
	extension_rumrescan_core(scan, scankey, nscankeys,
							 orderbys, norderbys, &core_rum_routine,
							 documentdb_rum_get_multi_key_status,
							 can_documentdb_rum_index_scan_ordered);
}


static int64
extension_documentdb_extended_rumgetbitmap(IndexScanDesc scan, TIDBitmap *tbm)
{
	EnsureDocumentDBExtendedRumLib();
	return extension_rumgetbitmap_core(scan, tbm, &core_rum_routine);
}


static bool
extension_documentdb_extended_rumgettuple(IndexScanDesc scan, ScanDirection direction)
{
	EnsureDocumentDBExtendedRumLib();
	return extension_rumgettuple_core(scan, direction,
									  &core_rum_routine);
}


static IndexBuildResult *
extension_documentdb_extended_rumbuild(Relation heapRelation,
									   Relation indexRelation,
									   struct IndexInfo *indexInfo)
{
	bool amCanBuildParallel = false;
	return extension_rumbuild_core(heapRelation, indexRelation,
								   indexInfo, &core_rum_routine,
								   documentdb_rum_update_multi_key_status,
								   amCanBuildParallel);
}


static bool
extension_documentdb_extended_ruminsert(Relation indexRelation,
										Datum *values,
										bool *isnull,
										ItemPointer heap_tid,
										Relation heapRelation,
										IndexUniqueCheck checkUnique,
										bool indexUnchanged,
										struct IndexInfo *indexInfo)
{
	return extension_ruminsert_core(indexRelation, values, isnull,
									heap_tid, heapRelation, checkUnique,
									indexUnchanged, indexInfo,
									&core_rum_routine,
									documentdb_rum_update_multi_key_status);
}


static void
LoadBaseIndexAmRoutine(void)
{
	LOCAL_FCINFO(fcinfo, 0);
	InitFunctionCallInfoData(*fcinfo, NULL, 1, InvalidOid, NULL, NULL);
	IndexAmRoutine *amroutine = (IndexAmRoutine *) DatumGetPointer(documentdb_rumhandler(
																	   fcinfo));

	/* Store the base routine */
	core_rum_routine = *amroutine;
	has_custom_routine = true;
}


static const char *
GetDocumentDBCatalogSchema(void)
{
	return "documentdb_extended_rum_catalog";
}


static Oid
DocumentDBExtendedRumIndexAmId(void)
{
	if (Cache.DocumentDBRumAmOid == InvalidOid)
	{
		HeapTuple tuple = SearchSysCache1(AMNAME, CStringGetDatum(
											  "documentdb_extended_rum"));
		if (!HeapTupleIsValid(tuple))
		{
			return InvalidOid;
		}

		Form_pg_am accessMethodForm = (Form_pg_am) GETSTRUCT(tuple);
		Cache.DocumentDBRumAmOid = accessMethodForm->oid;
		ReleaseSysCache(tuple);
	}

	return Cache.DocumentDBRumAmOid;
}


static Oid
DocumentDBExtendedRumSinglePathOpFamilyOid(void)
{
	if (Cache.BsonDocumentDBRumSinglePathOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.BsonDocumentDBRumSinglePathOperatorFamilyId = get_opfamily_oid(
			DocumentDBExtendedRumIndexAmId(),
			list_make2(makeString((char *) GetDocumentDBCatalogSchema()), makeString(
						   "bson_extended_rum_single_path_ops")),
			missingOk);
	}

	return Cache.BsonDocumentDBRumSinglePathOperatorFamilyId;
}


static Oid
DocumentDBExtendedRumCompositePathOpFamilyOid(void)
{
	if (Cache.BsonDocumentDBRumCompositePathOperatorFamilyId == InvalidOid)
	{
		bool missingOk = false;
		Cache.BsonDocumentDBRumCompositePathOperatorFamilyId = get_opfamily_oid(
			DocumentDBExtendedRumIndexAmId(),
			list_make2(makeString((char *) GetDocumentDBCatalogSchema()), makeString(
						   "bson_extended_rum_composite_path_ops")),
			missingOk);
	}

	return Cache.BsonDocumentDBRumCompositePathOperatorFamilyId;
}


static void
extension_documentdb_extended_rumcostestimate(PlannerInfo *root, IndexPath *path,
											  double loop_count,
											  Cost *indexStartupCost,
											  Cost *indexTotalCost,
											  Selectivity *indexSelectivity,
											  double *indexCorrelation,
											  double *indexPages)
{
	/* Do not force index cost to zero unless explicitly requested */
	bool forceIndexCostToZero = !EnableCompositeIndexPlanner;
	extension_rumcostestimate_core(root, path, loop_count, indexStartupCost,
								   indexTotalCost, indexSelectivity, indexCorrelation,
								   indexPages, &core_rum_routine,
								   forceIndexCostToZero);
}


PGDLLEXPORT Datum
documentdb_extended_rumhandler(PG_FUNCTION_ARGS)
{
	/* Ensure that the base rum handler is loaded */
	if (!has_custom_routine)
	{
		LoadBaseIndexAmRoutine();
	}

	IndexAmRoutine *amroutine = (IndexAmRoutine *) palloc0(sizeof(IndexAmRoutine));
	*amroutine = core_rum_routine;

	amroutine->ambeginscan = extension_documentdb_extended_rumbeginscan;
	amroutine->amendscan = extension_documentdb_extended_rumendscan;
	amroutine->amrescan = extension_documentdb_extended_rumrescan;
	amroutine->amgetbitmap = extension_documentdb_extended_rumgetbitmap;
	amroutine->amgettuple = extension_documentdb_extended_rumgettuple;
	amroutine->ambuild = extension_documentdb_extended_rumbuild;
	amroutine->aminsert = extension_documentdb_extended_ruminsert;
	amroutine->amcostestimate = extension_documentdb_extended_rumcostestimate;
	PG_RETURN_POINTER(amroutine);
}


PGDLLEXPORT bool
documentdb_rum_get_multi_key_status(Relation indexRelation)
{
	Buffer metabuffer;
	Page metapage;
	RumMetaPageData *metadata;
	bool hasMultiKeyPaths = false;

	metabuffer = ReadBuffer(indexRelation, RUM_METAPAGE_BLKNO);
	LockBuffer(metabuffer, RUM_SHARE);
	metapage = BufferGetPage(metabuffer);
	metadata = RumPageGetMeta(metapage);
	hasMultiKeyPaths = metadata->nPendingHeapTuples > 0;
	UnlockReleaseBuffer(metabuffer);

	return hasMultiKeyPaths;
}


PGDLLEXPORT void
documentdb_rum_update_multi_key_status(Relation index)
{
	/* First do a get to see if we even need to update */
	bool isMultiKey = documentdb_rum_get_multi_key_status(index);
	if (isMultiKey)
	{
		return;
	}

	Buffer metaBuffer;
	Page metapage;
	RumMetaPageData *metadata;
	GenericXLogState *state;

	metaBuffer = ReadBuffer(index, RUM_METAPAGE_BLKNO);
	LockBuffer(metaBuffer, RUM_EXCLUSIVE);

	state = GenericXLogStart(index);
	metapage = GenericXLogRegisterBuffer(state, metaBuffer, 0);
	metadata = RumPageGetMeta(metapage);

	/* Set pending heap tuples to 1 to indicate this is a multi-key index */
	metadata->nPendingHeapTuples = 1;

	GenericXLogFinish(state);
	UnlockReleaseBuffer(metaBuffer);
}
