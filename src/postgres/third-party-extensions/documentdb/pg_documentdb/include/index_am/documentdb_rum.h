/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/index_am/documentdb_rum.h
 *
 * Common declarations for RUM specific helper functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef DOCUMENTDB_RUM_H
#define DOCUMENTDB_RUM_H

#include <fmgr.h>
#include <access/amapi.h>
#include <nodes/pathnodes.h>
#include "index_am/index_am_exports.h"

typedef void *(*CreateIndexArrayTrackerState)(void);
typedef bool (*IndexArrayTrackerAdd)(void *state, ItemPointer item);
typedef void (*FreeIndexArrayTrackerState)(void *);
typedef void (*UpdateMultikeyStatusFunc)(Relation index);

/*
 * Adapter struct that provides function pointers to allow
 * for extensibility in managing index array state for index scans.
 * The current requirements on the interface is to provide an abstraction
 * that can be used to deduplicate array entries in the index scan.
 */
typedef struct RumIndexArrayStateFuncs
{
	/* Create opaque state to manage entries in this specific index scan */
	CreateIndexArrayTrackerState createState;

	/* Add an item to the index scan and return whether or not it is new or existing */
	IndexArrayTrackerAdd addItem;

	/* Frees the temporary state used for the adding of items */
	FreeIndexArrayTrackerState freeState;
} RumIndexArrayStateFuncs;


/* How to load the RUM library into the process */
typedef enum RumLibraryLoadOptions
{
	/* Apply no customizations - load the default RUM lib */
	RumLibraryLoadOption_None = 0,

	/* Prefer to load the custom documentdb_rum if available and fall back */
	RumLibraryLoadOption_PreferDocumentDBRum = 1,

	/* Require hte custom documentdb_rum */
	RumLibraryLoadOption_RequireDocumentDBRum = 2,
} RumLibraryLoadOptions;


typedef bool (*CanOrderInIndexScan)(IndexScanDesc scan);

extern RumLibraryLoadOptions DocumentDBRumLibraryLoadOption;
void LoadRumRoutine(void);

IndexScanDesc extension_rumbeginscan_core(Relation rel, int nkeys, int norderbys,
										  IndexAmRoutine *coreRoutine);
void extension_rumendscan_core(IndexScanDesc scan, IndexAmRoutine *coreRoutine);
void extension_rumrescan_core(IndexScanDesc scan, ScanKey scankey, int nscankeys,
							  ScanKey orderbys, int norderbys,
							  IndexAmRoutine *coreRoutine,
							  GetMultikeyStatusFunc multiKeyStatusFunc,
							  CanOrderInIndexScan indexScanOrderedFunc);
int64 extension_rumgetbitmap_core(IndexScanDesc scan, TIDBitmap *tbm,
								  IndexAmRoutine *coreRoutine);
bool extension_rumgettuple_core(IndexScanDesc scan, ScanDirection direction,
								IndexAmRoutine *coreRoutine);


void extension_rumcostestimate_core(PlannerInfo *root, IndexPath *path, double
									loop_count,
									Cost *indexStartupCost, Cost *indexTotalCost,
									Selectivity *indexSelectivity,
									double *indexCorrelation,
									double *indexPages, IndexAmRoutine *coreRoutine,
									bool forceIndexPushdownCostToZero);

IndexBuildResult * extension_rumbuild_core(Relation heapRelation, Relation indexRelation,
										   struct IndexInfo *indexInfo,
										   IndexAmRoutine *coreRoutine,
										   UpdateMultikeyStatusFunc updateMultikeyStatus,
										   bool amCanBuildParallel);

bool extension_ruminsert_core(Relation indexRelation,
							  Datum *values,
							  bool *isnull,
							  ItemPointer heap_tid,
							  Relation heapRelation,
							  IndexUniqueCheck checkUnique,
							  bool indexUnchanged,
							  struct IndexInfo *indexInfo,
							  IndexAmRoutine *coreRoutine,
							  UpdateMultikeyStatusFunc updateMultikeyStatus);

bool RumGetTruncationStatus(Relation indexRelation);

struct ExplainState;
void ExplainCompositeScan(IndexScanDesc scan, struct ExplainState *es);
void ExplainRawCompositeScan(Relation index_rel, List *indexQuals, List *indexOrderBy,
							 ScanDirection indexScanDir, struct ExplainState *es);

void ExplainRegularIndexScan(IndexScanDesc scan, struct ExplainState *es);
#endif
